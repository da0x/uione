// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"context"
	"fmt"
	"reflect"
	"slices"
	"strings"
	"sync"
	"time"

	"cloud.google.com/go/firestore"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// A key given as it is, like a project's address, changes only by a command that
// says it may, as Fields("slug") does. Everything named by it moves with it, as
// with a key made from a field (moving.go), but there may be far more of it than a
// transaction writes, like a project's issues, their comments and their histories.
// So the command only says where it's going, as a relocation, and the move is done
// a batch at a time, outside any transaction. Until it's done, nothing held within
// it, or it itself, may change, so what the move read stays true. Each batch writes
// what it can again: everything at its new id first, then what was at the old ids
// is deleted, the most deeply named first, so a move that stops partway is worked
// out and finished from what's left.

// How many writes a batch holds, under the 500 Firestore allows in one.
const relocationBatch = 400

// How long a backend may take to finish a move before another may pick it up.
const relocationLease = 2 * time.Minute

// relocation is a move asked for and not yet done, kept in relocations by the kind
// and id of what moves.
type relocation struct {
	Collection string         `firestore:"collection"`
	Entity     string         `firestore:"entity"`
	From       string         `firestore:"from"`
	To         string         `firestore:"to"`
	Fields     map[string]any `firestore:"fields"` // the keys as given, like the slug
	By         string         `firestore:"by"`
	At         time.Time      `firestore:"at"`
	Until      time.Time      `firestore:"until"` // when whoever's moving it may be taken over
}

// relocating is the moves this backend is doing now, so it doesn't start one twice.
type relocating struct {
	mu     sync.Mutex
	moving map[string]bool
}

func relocationOf(collection, id string) string { return collection + ":" + id }

// retypes says which of a command's fields are keys given as they are, like a
// project's slug, which it may change, moving it: not one pointing at what it's
// within, or counted.
func (s *schema) retypes(fields []string) []string {
	var keys []string
	for _, name := range fields {
		if f := s.field(name); f != nil && f.key && f.from == "" && f.refers == "" && !f.serial {
			keys = append(keys, name)
		}
	}
	return keys
}

// relocate asks for the record to move to the id its new keys make, in the
// command's transaction, and gives it back the keys it has until then: none of it
// moves yet. It's refused when the new id is taken. It says where it's going, or
// "" when the keys say what they said.
func (a *App) relocate(c *Ctx, s *schema, id string, v reflect.Value) (string, error) {
	moved := reflect.New(s.typ).Elem()
	moved.Set(v)
	for _, k := range s.keys {
		if had, ok := c.before[k.name]; ok {
			v.FieldByIndex(k.index).Set(reflect.ValueOf(had).Convert(k.typ))
		}
	}
	to, ok := s.id(moved)
	if !ok || to == id {
		return "", nil
	}
	if !validID(to) {
		return "", invalid("that isn't an address")
	}
	if _, err := c.tx.Get(a.store.Collection(s.collection).Doc(to)); err == nil {
		return "", takenBy(s, moved)
	} else if status.Code(err) != codes.NotFound {
		return "", err
	}
	// Two moving onto the same id at once would both find it free.
	going, err := c.tx.Documents(a.store.Collection("relocations").Where("collection", "==", s.collection).Where("to", "==", to)).GetAll()
	if err != nil {
		return "", err
	}
	if len(going) > 0 {
		return "", takenBy(s, moved)
	}
	fields := map[string]any{}
	for _, k := range s.keys {
		fields[k.name] = moved.FieldByIndex(k.index).Interface()
	}
	r := relocation{Collection: s.collection, Entity: s.entity, From: id, To: to, Fields: fields, By: c.me, At: c.now}
	if err := c.tx.Set(a.store.Collection("relocations").Doc(relocationOf(s.collection, id)), r); err != nil {
		return "", err
	}
	return to, nil
}

// closed refuses a change to anything being moved, or held within something being
// moved, like an issue of a project moving to a new address, read in the command's
// transaction so a move asked for at the same time waits for it.
func (a *App) closed(tx *firestore.Transaction, s *schema, id string, v reflect.Value) error {
	if !a.anyRetyped() {
		return nil
	}
	check := func(scope *schema, at string) error {
		if at == "" || !validID(at) {
			return nil
		}
		_, err := tx.Get(a.store.Collection("relocations").Doc(relocationOf(scope.collection, at)))
		if err == nil {
			go a.relocated(context.Background(), relocationOf(scope.collection, at))
			return &Failure{Status: 409, Message: fmt.Sprintf("this %s is moving to its new address; try again in a minute", strings.ReplaceAll(scope.name, "_", " "))}
		}
		if status.Code(err) != codes.NotFound {
			return err
		}
		return nil
	}
	if err := check(s, id); err != nil {
		return err
	}
	for _, r := range a.reg.defined {
		f, err := a.rolesFields(r)
		if err != nil {
			return err
		}
		if f.scope == s || !f.scope.mayRetype() {
			continue
		}
		place, err := a.within(tx, &owned{s, v}, f.scope)
		if err != nil {
			return err
		}
		if err := check(f.scope, place); err != nil {
			return err
		}
	}
	return nil
}

// mayRetype says whether a command may change a key of the entity given as it is.
func (s *schema) mayRetype() bool {
	return slices.ContainsFunc(s.fields, func(f field) bool { return f.retyped })
}

func (a *App) anyRetyped() bool {
	for _, s := range a.reg.schemas {
		if s.mayRetype() {
			return true
		}
	}
	return false
}

// isScope says whether an entity is a place roles are held in, like a project.
func (a *App) isScope(s *schema) bool {
	for _, r := range a.reg.defined {
		if a.reg.schemas[r.scope] == s {
			return true
		}
	}
	return false
}

// keepsFiles says whether an entity's records may have files attached.
func (s *schema) keepsFiles() bool {
	return slices.ContainsFunc(s.fields, func(f field) bool { return f.file })
}

// relocations finishes every move asked for and not done, as the backend starts.
func (a *App) relocations(ctx context.Context) {
	docs, err := a.store.Collection("relocations").Documents(ctx).GetAll()
	if err != nil {
		a.log.Printf("one: the moves not yet done weren't read: %v", err)
		return
	}
	for _, doc := range docs {
		go a.relocated(context.WithoutCancel(ctx), doc.Ref.ID)
	}
}

// relocated does a move asked for, unless this backend is doing it already or
// another is and hasn't stopped, and says what it did in the log when it can't.
func (a *App) relocated(ctx context.Context, job string) {
	a.relocating.mu.Lock()
	if a.relocating.moving == nil {
		a.relocating.moving = map[string]bool{}
	}
	if a.relocating.moving[job] {
		a.relocating.mu.Unlock()
		return
	}
	a.relocating.moving[job] = true
	a.relocating.mu.Unlock()
	defer func() {
		a.relocating.mu.Lock()
		delete(a.relocating.moving, job)
		a.relocating.mu.Unlock()
	}()
	if err := a.move(ctx, job); err != nil {
		a.log.Printf("one: moving %s stopped, and goes on when it's next asked for: %v", job, err)
	}
}

// claim takes a move for this backend for a while, unless another has it.
func (a *App) claim(ctx context.Context, ref *firestore.DocumentRef) (*relocation, error) {
	var r *relocation
	err := a.store.RunTransaction(ctx, func(ctx context.Context, tx *firestore.Transaction) error {
		r = nil
		snap, err := tx.Get(ref)
		if status.Code(err) == codes.NotFound {
			return nil
		}
		if err != nil {
			return err
		}
		var found relocation
		if err := snap.DataTo(&found); err != nil {
			return err
		}
		now := time.Now().UTC()
		if found.Until.After(now) {
			return nil
		}
		r = &found
		return tx.Update(ref, []firestore.Update{{Path: "until", Value: now.Add(relocationLease)}})
	})
	return r, err
}

// move does a move: works out all it changes from what's stored, writes each at its
// new id and changes what points at it, then deletes the old, and says what changed.
func (a *App) move(ctx context.Context, job string) error {
	ref := a.store.Collection("relocations").Doc(job)
	r, err := a.claim(ctx, ref)
	if err != nil || r == nil {
		return err
	}
	s := a.reg.entity(r.Entity)
	if s == nil {
		return fmt.Errorf("one: %s isn't an entity any module uses", r.Entity)
	}
	from := a.store.Collection(s.collection).Doc(r.From)
	snap, err := from.Get(ctx)
	if status.Code(err) == codes.NotFound {
		// It was moved, all but saying so.
		_, err := ref.Delete(ctx)
		return err
	}
	if err != nil {
		return err
	}
	v := reflect.New(s.typ).Elem()
	if err := snap.DataTo(v.Addr().Interface()); err != nil {
		return err
	}
	if err := s.decode(r.Fields, v); err != nil {
		return err
	}
	now := time.Now().UTC()
	m, err := a.moved(asStored{ctx}, now, s, from, v, snap.Data(), r.To, true)
	if err != nil {
		return err
	}
	var writes, left []write
	for _, w := range m.ops(a.store, now, r.By, nil) {
		if w.left {
			left = append(left, w)
		} else {
			writes = append(writes, w)
		}
	}
	// What was named by what moved goes before what named it, the moving record last.
	slices.Reverse(left)
	for start := 0; start < len(writes)+len(left); start += relocationBatch {
		batch := a.store.Batch()
		for _, w := range append(writes, left...)[start:min(start+relocationBatch, len(writes)+len(left))] {
			switch {
			case w.data == nil:
				batch.Delete(w.ref)
			case w.merged:
				batch.Set(w.ref, w.data, firestore.MergeAll)
			default:
				batch.Set(w.ref, w.data)
			}
		}
		if _, err := batch.Commit(ctx); err != nil {
			return err
		}
		if _, err := ref.Update(ctx, []firestore.Update{{Path: "until", Value: time.Now().UTC().Add(relocationLease)}}); err != nil {
			return err
		}
	}
	if _, err := ref.Delete(ctx); err != nil {
		return err
	}
	a.publish(ctx, m.events...)
	a.log.Printf("one: moved %s %s to %s, with %d records", s.entity, r.From, r.To, len(m.order))
	return nil
}

// firstID is the id something had when it was made, before it moved: what its
// GitHub secret is made from, so a project moved to a new address keeps it.
func (a *App) firstID(ctx context.Context, s *schema, id string) (string, error) {
	if s == nil || id == "" || !s.mayRetype() {
		return id, nil
	}
	docs, err := a.store.Collection("moved").Where("collection", "==", s.collection).Where("to", "==", id).Documents(ctx).GetAll()
	if err != nil {
		return "", err
	}
	first, at := id, time.Time{}
	for _, doc := range docs {
		when, _ := doc.Data()["at"].(time.Time)
		if was := strings.TrimPrefix(doc.Ref.ID, s.collection+":"); at.IsZero() || when.Before(at) {
			first, at = was, when
		}
	}
	return first, nil
}
