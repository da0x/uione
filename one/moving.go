// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"context"
	"reflect"
	"strings"
	"time"

	"cloud.google.com/go/firestore"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// A key made from another field, like a stage's name from its title, follows it:
// renaming the stage names it again, and it moves to the id its new name makes.
// Everything that points at it points at the new id in the same step, and what's
// named by it moves too, as a stage's steps do with it, and a board's stages with
// the board. Its history goes with it, and its old id is kept as an address that
// leads to the new one, so a link to it still opens it.

// moving is one entity the move changes: one that moves to a new id, or one that
// points at something that moves and stays where it is.
type moving struct {
	schema  *schema
	from    *firestore.DocumentRef // where it's stored now
	value   reflect.Value          // as it will be
	stored  map[string]any         // as it's stored now
	moves   bool                   // its id changes, as part of what names it points at something that moves
	to      string                 // its new id, once worked out
	repoint []repointing           // its fields that point at something that moves
}

// repointing is a field that points at something that moves: the whole field, or
// one place in a list of them.
type repointing struct {
	field  *field
	at     int // the place in a list, or -1 for the whole field
	target *moving
}

// move is everything a command's move changes, read before anything is written, as
// a transaction must.
type move struct {
	all      map[string]*moving // by where each is stored now
	order    []*moving
	writes   []write
	events   []event
	finalIDs map[string]string // what each id moving becomes, by collection and id
}

// keyFollows works out a key made from another field again, when the update changed
// that field or gave the key itself: a key given is made a key as a title is, so
// "Intake" names it intake. A key given as it was, as a form showing it sends it
// back, isn't given. Says whether a key changed, as only then does it move.
func (s *schema) keyFollows(v reflect.Value, before map[string]any, given map[string]any) bool {
	changed := false
	for _, f := range s.keys {
		if f.from == "" || f.typ.Kind() != reflect.String {
			continue
		}
		key := v.FieldByIndex(f.index)
		had, _ := before[f.name].(string)
		if said, ok := given[f.name].(string); ok && strings.TrimSpace(said) != "" && slugOf(said) != had {
			key.SetString(slugOf(said))
		} else if source := s.field(f.from); source != nil {
			now := v.FieldByIndex(source.index).String()
			if was, _ := before[f.from].(string); now != "" && now != was {
				key.SetString(slugOf(now))
			} else if had != "" {
				key.SetString(had) // what a body did to it is put back, as a key isn't set by hand
			}
		}
		if key.String() != had {
			changed = true
		}
	}
	return changed
}

// namedOnce says whether an entity keeps the name it was made with, whatever its
// title becomes: a service, whose id is who it is, held by its key, its profile and
// everything it did, and a role, which a defined role is found by.
func (a *App) namedOnce(s *schema) bool {
	for _, r := range a.reg.defined {
		if a.reg.schemas[r.role] == s || (r.service != nil && a.reg.schemas[r.service] == s) {
			return true
		}
	}
	return false
}

// reader reads what a move changes: in a command's transaction, or as it's stored,
// for a move done in the background, a batch at a time (relocating.go).
type reader interface {
	all(q firestore.Query) ([]*firestore.DocumentSnapshot, error)
	get(ref *firestore.DocumentRef) (*firestore.DocumentSnapshot, error)
}

// inTransaction reads in a command's transaction.
type inTransaction struct{ tx *firestore.Transaction }

func (r inTransaction) all(q firestore.Query) ([]*firestore.DocumentSnapshot, error) {
	return r.tx.Documents(q).GetAll()
}

func (r inTransaction) get(ref *firestore.DocumentRef) (*firestore.DocumentSnapshot, error) {
	return r.tx.Get(ref)
}

// asStored reads what's stored, outside any transaction.
type asStored struct{ ctx context.Context }

func (r asStored) all(q firestore.Query) ([]*firestore.DocumentSnapshot, error) {
	return q.Documents(r.ctx).GetAll()
}

func (r asStored) get(ref *firestore.DocumentRef) (*firestore.DocumentSnapshot, error) {
	return ref.Get(r.ctx)
}

// keepsItsID says whether an entity keeps its id when what names it moves: a
// service, whose id is who it is, held by its key, its profile and everything it
// did. It points at where it moved, and stays as it's named.
func (a *App) keepsItsID(s *schema) bool {
	for _, r := range a.reg.defined {
		if r.service != nil && a.reg.schemas[r.service] == s {
			return true
		}
	}
	return false
}

// moved works out what moving an entity from id to its new one changes: what points
// at it, what's named by what points at it, all the way down, its history and
// theirs, the counters kept per each, the service keys held in it and the files
// attached to each, and the old ids as addresses of the new. resuming is a move
// done in the background picked up again, whose new ids may be there already.
func (a *App) moved(c reader, now time.Time, s *schema, ref *firestore.DocumentRef, v reflect.Value, stored map[string]any, newID string, resuming bool) (*move, error) {
	m := &move{all: map[string]*moving{}, finalIDs: map[string]string{}}
	root := &moving{schema: s, from: ref, value: v, stored: stored, moves: true, to: newID}
	m.all[ref.Path] = root
	m.order = append(m.order, root)

	// Each one moving is looked for in every field that points at its kind.
	for i := 0; i < len(m.order); i++ {
		target := m.order[i]
		if !target.moves {
			continue
		}
		for _, t := range a.reg.schemas {
			for fi := range t.fields {
				f := &t.fields[fi]
				if f.refers != target.schema.entity || f.typ == nil {
					continue
				}
				list := f.typ.Kind() == reflect.Slice
				op := "=="
				if list {
					op = "array-contains"
				}
				docs, err := c.all(a.store.Collection(t.collection).Where(f.name, op, target.from.ID))
				if err != nil {
					return nil, err
				}
				for _, doc := range docs {
					one, ok := m.all[doc.Ref.Path]
					if !ok {
						value := reflect.New(t.typ)
						if err := doc.DataTo(value.Interface()); err != nil {
							return nil, err
						}
						one = &moving{schema: t, from: doc.Ref, value: value.Elem(), stored: doc.Data(), to: doc.Ref.ID}
						m.all[doc.Ref.Path] = one
						m.order = append(m.order, one)
					}
					if list {
						ids, _ := doc.Data()[f.name].([]any)
						for at, id := range ids {
							if id == target.from.ID {
								one.repoint = append(one.repoint, repointing{field: f, at: at, target: target})
							}
						}
					} else {
						one.repoint = append(one.repoint, repointing{field: f, at: -1, target: target})
					}
					if f.key && !one.moves && !a.keepsItsID(t) {
						one.moves = true
					}
				}
			}
		}
	}

	// Their new ids depend on each other's, a step's on its stages' and its board's,
	// so they're worked out until none changes.
	for changed := true; changed; {
		changed = false
		for _, one := range m.order {
			for _, r := range one.repoint {
				field := one.value.FieldByIndex(r.field.index)
				if r.at < 0 {
					field.SetString(r.target.to)
				} else if r.at < field.Len() {
					field.Index(r.at).SetString(r.target.to)
				}
			}
			if one.moves && one != root {
				if id, ok := one.schema.id(one.value); ok && id != one.to {
					one.to, changed = id, true
				}
			}
		}
	}
	for _, one := range m.order {
		if one.moves {
			m.finalIDs[one.schema.collection+"/"+one.from.ID] = one.to
		}
	}

	// Nothing moves onto something already there, unless that moves away too.
	for _, one := range m.order {
		if !one.moves || one.to == one.from.ID {
			continue
		}
		to := a.store.Collection(one.schema.collection).Doc(one.to)
		if leaving, ok := m.all[to.Path]; (ok && leaving.moves) || resuming {
			continue
		}
		_, err := c.get(to)
		if err == nil {
			if one == root {
				return nil, takenBy(s, v)
			}
			return nil, invalid("%s %s is already there", strings.ReplaceAll(one.schema.name, "_", " "), one.to)
		}
		if status.Code(err) != codes.NotFound {
			return nil, err
		}
	}

	// Histories: each one's own, and every mention of one that moves in another's,
	// as what it points at or as the before and after of a change of it.
	histories := map[string]map[string]any{}
	historyOf := map[string]*schema{}
	change := func(doc *firestore.DocumentSnapshot, h *schema, name string, value any) {
		data, ok := histories[doc.Ref.Path]
		if !ok {
			data = doc.Data()
			histories[doc.Ref.Path], historyOf[doc.Ref.Path] = data, h
		}
		data[name] = value
	}
	refs := map[string]*firestore.DocumentRef{}
	for _, one := range m.order {
		if !one.moves || one.to == one.from.ID {
			continue
		}
		for _, t := range a.reg.schemas {
			h := t.history
			if h == nil {
				continue
			}
			for _, f := range h.fields {
				if f.refers != one.schema.entity {
					continue
				}
				docs, err := c.all(a.store.Collection(h.collection).Where(f.name, "==", one.from.ID))
				if err != nil {
					return nil, err
				}
				for _, doc := range docs {
					refs[doc.Ref.Path] = doc.Ref
					change(doc, h, f.name, one.to)
				}
			}
			for _, f := range t.fields {
				if f.refers != one.schema.entity || f.typ == nil || f.typ.Kind() == reflect.Slice {
					continue
				}
				for _, side := range []string{"before", "after"} {
					docs, err := c.all(a.store.Collection(h.collection).Where("field", "==", f.name).Where(side, "==", one.from.ID))
					if err != nil {
						return nil, err
					}
					for _, doc := range docs {
						refs[doc.Ref.Path] = doc.Ref
						change(doc, h, side, one.to)
					}
				}
			}
		}
	}

	// Counters kept per one that moves, like a board's numbers, go with it.
	for _, one := range m.order {
		if !one.moves || one.to == one.from.ID {
			continue
		}
		for _, t := range a.reg.schemas {
			for _, f := range t.fields {
				if !f.serial || f.per == "" {
					continue
				}
				if per := t.field(f.per); per == nil || per.refers != one.schema.entity {
					continue
				}
				counter := t.collection + "." + f.name + "."
				old := a.store.Collection("serials").Doc(counter + one.from.ID)
				snap, err := c.get(old)
				if status.Code(err) == codes.NotFound {
					continue
				}
				if err != nil {
					return nil, err
				}
				m.writes = append(m.writes, write{ref: a.store.Collection("serials").Doc(counter + one.to), data: snap.Data()}, write{ref: old})
			}
		}
	}

	// The old ids lead to the new ones, and so does every older id that led to an
	// old one; an id something moves onto is no longer an old address.
	for _, one := range m.order {
		if !one.moves || one.to == one.from.ID {
			continue
		}
		former := a.store.Collection("moved")
		earlier, err := c.all(former.Where("collection", "==", one.schema.collection).Where("to", "==", one.from.ID))
		if err != nil {
			return nil, err
		}
		for _, doc := range earlier {
			m.writes = append(m.writes, write{ref: doc.Ref, data: map[string]any{"to": one.to}, merged: true})
		}
		m.writes = append(m.writes,
			write{ref: former.Doc(movedID(one.schema.collection, one.from.ID)), data: map[string]any{"collection": one.schema.collection, "entity": one.schema.entity, "to": one.to, "at": now}},
			write{ref: former.Doc(movedID(one.schema.collection, one.to))})
	}

	// A service's key says which place it's held in, and a file which record it's
	// attached to.
	for _, one := range m.order {
		if !one.moves || one.to == one.from.ID {
			continue
		}
		if a.isScope(one.schema) {
			keys, err := c.all(a.store.Collection("keys").Where("place", "==", one.from.ID))
			if err != nil {
				return nil, err
			}
			for _, doc := range keys {
				m.writes = append(m.writes, write{ref: doc.Ref, data: map[string]any{"place": one.to}, merged: true})
			}
		}
		if one.schema.keepsFiles() {
			files, err := c.all(a.store.Collection("files").Where("entity", "==", one.schema.entity).Where("record", "==", one.from.ID))
			if err != nil {
				return nil, err
			}
			for _, doc := range files {
				m.writes = append(m.writes, write{ref: doc.Ref, data: map[string]any{"record": one.to}, merged: true})
			}
		}
	}

	for path, data := range histories {
		h := historyOf[path]
		m.writes = append(m.writes, write{ref: refs[path], data: data, event: &event{Type: h.name + ".updated", Entity: h.entity, ID: refs[path].ID, After: data}})
	}
	return m, nil
}

// movedTo is the id something that moved went to, when id isn't there any more and
// was its id; "" when it's there, or never was.
func (a *App) movedTo(ctx context.Context, s *schema, id string) (string, error) {
	if s == nil || id == "" || !a.mayMove(s, 0) {
		return "", nil
	}
	if _, err := a.store.Collection(s.collection).Doc(id).Get(ctx); status.Code(err) != codes.NotFound {
		return "", err
	}
	snap, err := a.store.Collection("moved").Doc(movedID(s.collection, id)).Get(ctx)
	if status.Code(err) == codes.NotFound {
		return "", nil
	}
	if err != nil {
		return "", err
	}
	to, _ := snap.Data()["to"].(string)
	return to, nil
}

// mayMove says whether an entity can move: it's named by a key made from a field, or
// by one pointing at something that can move, as a step is by its stages.
func (a *App) mayMove(s *schema, depth int) bool {
	if depth > 8 {
		return false
	}
	for _, f := range s.keys {
		if f.from != "" || f.retyped {
			return true
		}
		if t := a.reg.entity(f.refers); f.refers != "" && t != nil && a.mayMove(t, depth+1) {
			return true
		}
	}
	return false
}

// movedID is where an old id's new one is kept: by its kind and the id.
func movedID(collection, id string) string {
	return collection + ":" + id
}

// takenBy says what's already named as this would be, by the field it's named from,
// as making it again would: "Title is already taken".
func takenBy(s *schema, v reflect.Value) error {
	for _, f := range s.keys {
		if f.from != "" {
			return invalid("%s is already taken", label(f.from))
		}
	}
	return invalid("%s is already taken", label(s.keys[len(s.keys)-1].name))
}

// save writes the move in the command's transaction. The root, the command's own
// entity, is written by the command.
func (m *move) save(c *Ctx, root *moving) error {
	for _, w := range m.ops(c.app.store, c.now, c.me, root) {
		var err error
		switch {
		case w.data == nil:
			err = c.tx.Delete(w.ref)
		case w.merged:
			err = c.tx.Set(w.ref, w.data, firestore.MergeAll)
		default:
			err = c.tx.Set(w.ref, w.data)
		}
		if err != nil {
			return err
		}
	}
	return nil
}

// ops is the move as writes, in order: each one that moves under its new id, and gone
// from its old, which a write marks as left, each that points at one that moves
// changed in place, then the rest, its histories, counters, keys, files and old
// addresses. What each changes is added to its events. skip, when there is one, is
// left for whoever writes it.
func (m *move) ops(store *firestore.Client, now time.Time, me string, skip *moving) []write {
	var out []write
	for _, one := range m.order {
		if one == skip {
			continue
		}
		record := one.value.Addr().Interface().(interface{ record() *Record }).record()
		record.UpdatedAt, record.UpdatedBy = now, me
		if one.moves {
			record.ID = one.to
		}
		after := one.schema.data(one.value)
		out = append(out, write{ref: store.Collection(one.schema.collection).Doc(one.to), data: after})
		if one.moves && one.to != one.from.ID {
			// It's the same record at its new id, changed from what it was, and its old
			// id is where it moved from, which isn't deleting it, its files or its count.
			out = append(out, write{ref: one.from, left: true})
			m.events = append(m.events, event{Type: one.schema.name + ".updated", Entity: one.schema.entity, ID: one.from.ID, Version: now.UnixNano(), Before: one.stored, moved: true})
			m.events = append(m.events, event{Type: one.schema.name + ".updated", Entity: one.schema.entity, ID: one.to, Version: now.UnixNano(), Before: one.stored, After: after})
			continue
		}
		m.events = append(m.events, event{Type: one.schema.name + ".updated", Entity: one.schema.entity, ID: one.to, Version: now.UnixNano(), Before: one.stored, After: after})
	}
	for _, w := range m.writes {
		out = append(out, w)
		if w.event != nil {
			ev := *w.event
			ev.Version = now.UnixNano()
			m.events = append(m.events, ev)
		}
	}
	return out
}
