// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"context"
	"fmt"
	"reflect"
	"regexp"
	"strings"
	"time"

	"cloud.google.com/go/firestore"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// Permission is who may run a command: Anyone, SignedIn, Owner (the person in
// the entity's owner field), or a permission a role grants, like "book:withdraw".
type Permission string

const (
	Anyone   Permission = "anyone"
	SignedIn Permission = "signed_in"
	Owner    Permission = "owner"

	// Deprecated: Authenticated is what SignedIn was called for a while.
	Authenticated = SignedIn
)

// Ctx is what a command's body can see: the time it runs and who's running it. It
// also holds the command's transaction, so the body can read the entities it
// points at with Read.
type Ctx struct {
	context.Context
	me  string
	now time.Time

	app   *App
	tx    *firestore.Transaction
	reads []*read
	made  []*made

	counters map[string]*counting // every serial counted in the transaction, by counter
	before   map[string]any       // the entity as it was stored before the command, for Was
	entity   *owned               // the command's entity, for Held
	command  string               // the command running, like tracker::issue::close, for history
	given    map[string]any       // what it was sent besides its entity's fields, for Input
	gone     []*gone              // what it deletes besides its own entity, with DeleteWhere
	deleting string               // the path of its own entity, when the command deletes it
}

// gone is an entity a command's body deletes with DeleteWhere.
type gone struct {
	schema *schema
	ref    *firestore.DocumentRef
	before map[string]any
}

// made is an entity a command's body created with Create.
type made struct {
	schema *schema
	ref    *firestore.DocumentRef
	value  reflect.Value
	before map[string]any // what was stored under the same key, if anything
}

// Create makes another entity in the same step as the command, such as the first
// member of a new project. It starts, is named and is numbered the way a create
// command would make it, is checked against its own rules, and is saved with the
// command's change, both or neither. With a key that's already there it updates
// that entity, unless the key is unique.
func Create[E any, P entityPointer[E]](c *Ctx, entity *E) error {
	s := c.app.reg.schemas[reflect.TypeFor[E]()]
	if s == nil {
		return fmt.Errorf("one: %s isn't an entity any module uses", reflect.TypeFor[E]().Name())
	}
	return create(c, s, reflect.ValueOf(entity).Elem(), nil)
}

// create is Create for an entity known by its schema, like a project's roles. body,
// when there is one, runs once the entity has its id, before it's checked, as a
// dispatched command's does.
func create(c *Ctx, s *schema, v reflect.Value, body func() error) error {
	record := v.Addr().Interface().(interface{ record() *Record }).record()
	username := ""
	if s.startsWithUsername() {
		var err error
		if username, err = c.app.usernameOf(c.Context, c.me); err != nil {
			return err
		}
	}
	madeFrom := s.start(v, c.me, username, c.now)
	s.normalize(v)
	counted, err := c.app.serials(c.tx, s, v, c.counters)
	if err != nil {
		return err
	}
	collection := c.app.store.Collection(s.collection)
	ref := collection.NewDoc()
	if key, ok := s.id(v); ok {
		ref = collection.Doc(key)
	}
	record.ID = ref.ID
	if body != nil {
		if err := body(); err != nil {
			return err
		}
	}
	if err := s.validate(v); err != nil {
		return err
	}
	record.CreatedAt, record.CreatedBy = c.now, c.me
	var before map[string]any
	if snap, err := c.tx.Get(ref); err == nil {
		if taken := s.uniqueKey(); taken != nil {
			return invalid("%s is already taken", label(taken.name))
		}
		if madeFrom != "" {
			return invalid("%s is already taken", label(madeFrom))
		}
		if err := s.ownedByAnother(snap.Data(), c.me); err != nil {
			return err
		}
		before = snap.Data()
		kept := reflect.New(s.typ).Elem()
		if err := snap.DataTo(kept.Addr().Interface()); err != nil {
			return err
		}
		earlier := kept.Addr().Interface().(interface{ record() *Record }).record()
		record.CreatedAt, record.CreatedBy = earlier.CreatedAt, earlier.CreatedBy
		for _, f := range counted {
			v.FieldByIndex(f.index).Set(kept.FieldByIndex(f.index))
		}
	} else if status.Code(err) != codes.NotFound {
		return err
	}
	record.ID, record.UpdatedAt, record.UpdatedBy = ref.ID, c.now, c.me
	if err := c.app.unique(c.tx, s, v, ref.ID); err != nil {
		return err
	}
	c.made = append(c.made, &made{schema: s, ref: ref, value: v, before: before})
	return nil
}

// read is an entity a command read with Read, and how it was before.
type read struct {
	schema *schema
	ref    *firestore.DocumentRef
	value  reflect.Value
	stored map[string]any // as it was read
	before map[string]any // the same, in the form data() gives, to compare with
}

// Read reads an entity the command points at, such as a loan's book, inside the
// command's transaction. Whatever the body changes on it is checked against its
// rules and saved together with the command's own change, both or neither, and it
// gets an event of its own.
func Read[E any, P entityPointer[E]](c *Ctx, id string) (*E, error) {
	s := c.app.reg.schemas[reflect.TypeFor[E]()]
	if s == nil {
		return nil, fmt.Errorf("one: %s isn't an entity any module uses", reflect.TypeFor[E]().Name())
	}
	if id == "" {
		return nil, invalid("%s is required", label(s.name))
	}
	if !validID(id) {
		return nil, &Failure{Status: 404, Message: "that " + strings.ReplaceAll(s.name, "_", " ") + " doesn't exist"}
	}
	ref := c.app.store.Collection(s.collection).Doc(id)
	for _, r := range c.reads {
		if r.ref.Path == ref.Path {
			return r.value.Addr().Interface().(*E), nil
		}
	}
	snap, err := c.tx.Get(ref)
	if status.Code(err) == codes.NotFound {
		return nil, &Failure{Status: 404, Message: "that " + strings.ReplaceAll(s.name, "_", " ") + " doesn't exist"}
	}
	if err != nil {
		return nil, err
	}
	entity := new(E)
	if err := snap.DataTo(entity); err != nil {
		return nil, err
	}
	v := reflect.ValueOf(entity).Elem()
	c.reads = append(c.reads, &read{schema: s, ref: ref, value: v, stored: snap.Data(), before: s.data(v)})
	return entity, nil
}

// save writes every entity the body made, and every one it read and changed, after
// checking each against its rules, and returns the events to publish once the
// transaction commits.
func (c *Ctx) save() ([]event, error) {
	var events []event
	deleted := map[string]bool{}
	for _, g := range c.gone {
		deleted[g.ref.Path] = true
		if err := c.tx.Delete(g.ref); err != nil {
			return nil, err
		}
		kept, err := c.app.keep(c.Context, c.tx, g.schema, g.ref.ID, g.before, nil, c.command, c.me, c.now)
		if err != nil {
			return nil, err
		}
		events = append(events, kept...)
		events = append(events, event{Type: g.schema.name + ".updated", Entity: g.schema.entity, ID: g.ref.ID, Version: c.now.UnixNano(), Before: g.before})
	}
	for _, n := range c.counters {
		if err := c.tx.Set(n.ref, map[string]any{"last": n.value}); err != nil {
			return nil, err
		}
	}
	for _, m := range c.made {
		after := m.schema.data(m.value)
		if err := c.tx.Set(m.ref, after); err != nil {
			return nil, err
		}
		kept, err := c.app.keep(c.Context, c.tx, m.schema, m.ref.ID, m.before, after, c.command, c.me, c.now)
		if err != nil {
			return nil, err
		}
		events = append(events, kept...)
		events = append(events, event{
			Type:    m.schema.name + ".updated",
			Entity:  m.schema.entity,
			ID:      m.ref.ID,
			Version: c.now.UnixNano(),
			Before:  m.before,
			After:   after,
		})
	}
	for _, r := range c.reads {
		if deleted[r.ref.Path] || reflect.DeepEqual(r.schema.data(r.value), r.before) {
			continue
		}
		if err := r.schema.validate(r.value); err != nil {
			return nil, err
		}
		record := r.value.Addr().Interface().(interface{ record() *Record }).record()
		record.UpdatedAt, record.UpdatedBy = c.now, c.me
		after := r.schema.data(r.value)
		if err := c.tx.Set(r.ref, after); err != nil {
			return nil, err
		}
		kept, err := c.app.keep(c.Context, c.tx, r.schema, r.ref.ID, r.stored, after, c.command, c.me, c.now)
		if err != nil {
			return nil, err
		}
		events = append(events, kept...)
		events = append(events, event{
			Type:    r.schema.name + ".updated",
			Entity:  r.schema.entity,
			ID:      r.ref.ID,
			Version: c.now.UnixNano(),
			Before:  r.stored,
			After:   after,
		})
	}
	return events, nil
}

// keep writes the changes of an entity that keeps its history, in the command's
// transaction, and returns their events. before is nil for something just made,
// and after nil for something deleted; either way that's one change with no field.
func (a *App) keep(ctx context.Context, tx *firestore.Transaction, s *schema, id string, before, after map[string]any, action, me string, now time.Time) ([]event, error) {
	events, err := a.keepOwn(tx, s, id, before, after, action, me, now)
	if err != nil {
		return nil, err
	}
	more, err := a.keepInParent(ctx, tx, s, id, before, after, action, me, now)
	return append(events, more...), err
}

// keepInParent keeps a change to an entity in the history of what it points at,
// when that keeps it, as an issue does its comments': one entry naming the issue,
// what the issue points at, like its board and assignees, and what the comment
// points at, like the people it mentions, with its field the comment's own name.
// What the issue points at is read as it's stored.
func (a *App) keepInParent(ctx context.Context, tx *firestore.Transaction, s *schema, id string, before, after map[string]any, action, me string, now time.Time) ([]event, error) {
	current := after
	if current == nil {
		current = before
	}
	var events []event
	for _, f := range s.fields {
		if !f.history {
			continue
		}
		parent := a.reg.entity(f.refers)
		if parent == nil || parent.history == nil {
			continue
		}
		parentID, _ := current[f.name].(string)
		if parentID == "" {
			continue
		}
		stored, err := a.stored(ctx, parent, parentID)
		if err != nil {
			return nil, err
		}
		doc := map[string]any{
			parent.name: parentID, s.name: id, "field": s.name, "action": action, "before": nil, "after": nil,
			"created_at": now, "created_by": me, "updated_at": now, "updated_by": me,
		}
		for _, pf := range parent.fields {
			if pf.refers != "" {
				doc[pf.name] = stored[pf.name]
			}
		}
		for _, own := range s.fields {
			if _, taken := doc[own.name]; own.refers != "" && !taken {
				doc[own.name] = current[own.name]
			}
		}
		ref := a.store.Collection(parent.history.collection).NewDoc()
		doc["id"] = ref.ID
		if err := tx.Set(ref, doc); err != nil {
			return nil, err
		}
		events = append(events, event{Type: parent.history.name + ".updated", Entity: parent.history.entity, ID: ref.ID, Version: now.UnixNano(), After: doc})
	}
	return events, nil
}

func (a *App) keepOwn(tx *firestore.Transaction, s *schema, id string, before, after map[string]any, action, me string, now time.Time) ([]event, error) {
	if s.history == nil {
		return nil, nil
	}
	current := after
	if current == nil {
		current = before
	}
	entry := func(field string, was, is any) map[string]any {
		doc := map[string]any{
			s.name: id, "field": field, "action": action, "before": was, "after": is,
			"created_at": now, "created_by": me, "updated_at": now, "updated_by": me,
		}
		for _, f := range s.fields {
			if f.refers != "" {
				doc[f.name] = current[f.name]
			}
		}
		return doc
	}
	var entries []map[string]any
	if before == nil || after == nil {
		entries = append(entries, entry("", nil, nil))
	} else {
		for _, f := range s.fields {
			if !alike(before[f.name], after[f.name]) {
				entries = append(entries, entry(f.name, before[f.name], after[f.name]))
			}
		}
	}
	collection := a.store.Collection(s.history.collection)
	var events []event
	for _, doc := range entries {
		ref := collection.NewDoc()
		doc["id"] = ref.ID
		if err := tx.Set(ref, doc); err != nil {
			return nil, err
		}
		events = append(events, event{Type: s.history.name + ".updated", Entity: s.history.entity, ID: ref.ID, Version: now.UnixNano(), After: doc})
	}
	return events, nil
}

// alike says whether two stored values are the same, whichever form each is in:
// a list as read or as written, a whole number as either kind, a time to the
// nanosecond.
func alike(x, y any) bool {
	// Nothing, empty text and an empty list are the same, so a field that wasn't
	// stored, written as empty, isn't a change.
	plain := func(v any) any {
		switch value := v.(type) {
		case string:
			if value == "" {
				return nil
			}
		case []any:
			if len(value) == 0 {
				return nil
			}
		case []string:
			if len(value) == 0 {
				return nil
			}
			list := make([]any, len(value))
			for i, item := range value {
				list[i] = item
			}
			return list
		case int64:
			return float64(value)
		case time.Time:
			if value.IsZero() {
				return nil
			}
			return value.UnixNano()
		}
		return v
	}
	return reflect.DeepEqual(plain(x), plain(y))
}

// Has says whether a list holds a value, like the roles that may take a step
// holding a project's contributor role.
func Has(list []string, value string) bool {
	for _, item := range list {
		if item == value {
			return true
		}
	}
	return false
}

// Match is the value a match gives for a choice, or otherwise for one it doesn't
// name: Match(l.Relation, map[string]string{RelationBlocks: RelationBlockedBy}, "").
func Match[T any](choice string, values map[string]T, otherwise T) T {
	if value, ok := values[choice]; ok {
		return value
	}
	return otherwise
}

// Add puts a value in a list, unless it's there already: Add(i.Assignees, c.Me()).
func Add(list []string, value string) []string {
	if value == "" || contains(list, value) {
		return list
	}
	return append(list, value)
}

// Remove takes a value out of a list, wherever it is.
func Remove(list []string, value string) []string {
	kept := []string{}
	for _, item := range list {
		if item != value {
			kept = append(kept, item)
		}
	}
	return kept
}

// Now is when the command runs: one moment for everything it does.
func (c *Ctx) Now() time.Time { return c.now }

// Me is the id of the person running the command, or empty when nobody is signed in.
func (c *Ctx) Me() string { return c.me }

// validID says whether an id a request names can be a document's id: Firestore
// refuses ids with a slash, ".", "..", ids like __this__, and ids over 1500 bytes.
func validID(id string) bool {
	return id != "." && id != ".." && len(id) <= 1500 && !strings.Contains(id, "/") &&
		!(strings.HasPrefix(id, "__") && strings.HasSuffix(id, "__"))
}

// Was is what a field of the command's entity held before the command changed it,
// like the phase an issue is moving from: the stored value, or nil for something
// new.
func (c *Ctx) Was(field string) any {
	value := c.before[field]
	if value == nil {
		return ""
	}
	return value
}

// Fail stops the command with a message for the person who ran it.
// Input is what the command was sent besides its entity's fields, by its name, like
// the phase a removed phase's issues move to; nil when it wasn't sent.
func (c *Ctx) Input(name string) any { return c.given[name] }

// EachIn changes, with the command, every entity of a kind whose field holds a
// value, like each issue in a phase being removed: each is read in the command's
// transaction, body changes it, and it's checked and saved with the command's own
// change, both or neither, with its event and history.
func EachIn[E any, P entityPointer[E]](c *Ctx, field string, value any, body func(*E) error) error {
	s := c.app.reg.schemas[reflect.TypeFor[E]()]
	if s == nil {
		return fmt.Errorf("one: %s isn't an entity any module uses", reflect.TypeFor[E]().Name())
	}
	docs, err := c.tx.Documents(c.app.store.Collection(s.collection).Where(field, "==", value)).GetAll()
	if err != nil {
		return err
	}
	for _, doc := range docs {
		entity, err := Read[E, P](c, doc.Ref.ID)
		if err != nil {
			return err
		}
		if err := body(entity); err != nil {
			return err
		}
	}
	return nil
}

// DeleteWhere deletes, with the command, every entity of a kind whose field holds a
// value, like the steps to and from a phase being removed, in the command's
// transaction, both or neither, each with its event and history. Called again for
// another field, it deletes each one once.
func DeleteWhere[E any, P entityPointer[E]](c *Ctx, field string, value any) error {
	s := c.app.reg.schemas[reflect.TypeFor[E]()]
	if s == nil {
		return fmt.Errorf("one: %s isn't an entity any module uses", reflect.TypeFor[E]().Name())
	}
	docs, err := c.tx.Documents(c.app.store.Collection(s.collection).Where(field, "==", value)).GetAll()
	if err != nil {
		return err
	}
	for _, doc := range docs {
		seen := false
		for _, g := range c.gone {
			seen = seen || g.ref.Path == doc.Ref.Path
		}
		if !seen {
			c.gone = append(c.gone, &gone{schema: s, ref: doc.Ref, before: doc.Data()})
		}
	}
	return nil
}

func (c *Ctx) Fail(message string) error { return &Failure{Status: 400, Message: message} }

// Cmd is a command on entity E, named after its entity and action: signup::create.
// create, update and delete do what they say, using the entity's own rules. Any
// other action loads the entity, runs Do, and saves it.
type Cmd[E any, P entityPointer[E]] struct {
	entity     string // as the command's name says, checked against E
	action     string
	permission Permission
	fields     []string // what an update may change, when it says
	inputs     []string // what it's sent besides its entity's fields, when it says
	do         func(*Ctx, *E) error
	after      []func(*System, *E)
}

// Command declares a command on entity E, named after the entity and the action,
// like book::create.
func Command[E any, P entityPointer[E]](name string) *Cmd[E, P] {
	at := strings.LastIndex(name, "::")
	if at < 0 {
		panic("one: a command is named after its entity, like book::create, not " + name)
	}
	return &Cmd[E, P]{entity: name[:at], action: name[at+2:]}
}

// Allow changes who may run the command. Without it, the command needs the
// permission named after it: signup::create needs "signup:create".
func (c *Cmd[E, P]) Allow(p Permission) *Cmd[E, P] {
	c.permission = p
	return c
}

// Fields says which fields an update may change: those its forms ask for, like an
// issue's title and description, so whoever may edit an issue can't also set its
// status or who approved it by sending them. Without it, an update may change any
// field but its keys. Another command, like move, takes only the fields it names,
// like the phase an issue moves to, and without it takes none.
func (c *Cmd[E, P]) Fields(names ...string) *Cmd[E, P] {
	c.fields = names
	return c
}

// Inputs names what the command is sent besides its entity's fields, like the phase
// a removed phase's issues move to. Its body reads each with Input; none is stored.
func (c *Cmd[E, P]) Inputs(names ...string) *Cmd[E, P] {
	c.inputs = names
	return c
}

// Do gives the command a body, run on the entity before it's saved.
func (c *Cmd[E, P]) Do(body func(*Ctx, *E) error) *Cmd[E, P] {
	c.do = body
	return c
}

// After runs code of your own once the command's change is saved, with the entity
// as it was saved: to start work elsewhere, like a build, when a deploy is asked
// for. It runs before the command answers, so it should be quick; anything slow
// belongs elsewhere, reporting back through a Route. What it returns is logged,
// since the change it follows is already made.
func (c *Cmd[E, P]) After(then func(*System, *E) error) *Cmd[E, P] {
	c.after = append(c.after, func(s *System, e *E) {
		if err := then(s, e); err != nil {
			s.app.log.Printf("one: after %s::%s: %v", c.entity, c.action, err)
		}
	})
	return c
}

func (c *Cmd[E, P]) register(r *registry, ns string) {
	s := r.schema(reflect.TypeFor[E](), ns)
	if c.entity != s.name && c.entity != s.entity {
		panic("one: command " + c.entity + "::" + c.action + " is on " + s.entity + "; name it " + s.name + "::" + c.action)
	}
	permission := qualified(ns, c.permission)
	if permission == "" {
		permission = Permission(s.entity + ":" + c.action)
	}
	r.commands[s.entity+"::"+c.action] = func(a *App, call *call) (string, error) {
		return run[E, P](a, call, s, c.action, permission, c.fields, c.inputs, c.do, c.after)
	}
}

type call struct {
	ctx    context.Context
	me     string // the signed-in person's id, or empty
	input  map[string]any
	system bool // run by the backend's own code, which may run any command
}

func run[E any, P entityPointer[E]](a *App, c *call, s *schema, action string, permission Permission, fields, inputs []string, do func(*Ctx, *E) error, afterwards []func(*System, *E)) (string, error) {
	if c.system {
		permission = Anyone
	}
	// What it's sent besides its entity's fields is kept apart, for its body.
	given := map[string]any{}
	if len(inputs) > 0 {
		input := map[string]any{}
		for name, value := range c.input {
			if contains(inputs, name) {
				given[name] = value
			} else {
				input[name] = value
			}
		}
		apart := *c
		apart.input = input
		c = &apart
	}
	var saved *E
	// A field that starts as the person's username, like a project's owner, needs it
	// read from their profile before anything is made.
	username := ""
	if action == "create" && s.startsWithUsername() {
		var err error
		if username, err = a.usernameOf(c.ctx, c.me); err != nil {
			return "", err
		}
	}
	now := time.Now().UTC()
	collection := a.store.Collection(s.collection)
	var id string
	var before, after map[string]any
	var pointed []event // changes to entities the command points at

	// Creating is checked before anything is read, unless only a role within an
	// entity, such as a project, could allow it: then it's checked once what's sent
	// says which project.
	within := false
	if action == "create" {
		if err := a.permitted(c.ctx, nil, c.me, permission, nil); err != nil {
			if !a.scopes(permission) || c.me == "" {
				return "", err
			}
			within = true
		}
	}

	err := a.store.RunTransaction(c.ctx, func(ctx context.Context, tx *firestore.Transaction) error {
		entity := new(E)
		v := reflect.ValueOf(entity).Elem()
		record := P(entity).record()
		before, after = nil, nil
		body := &Ctx{Context: ctx, me: c.me, now: now, app: a, tx: tx, counters: map[string]*counting{}, command: s.entity + "::" + action, given: given}
		body.entity = &owned{s, v}
		pointed = nil

		if action == "create" {
			if err := s.decode(c.input, v); err != nil {
				return err
			}
			madeFrom := s.start(v, c.me, username, now)
			s.normalize(v)
			if err := a.mentionsIn(ctx, tx, s, v); err != nil {
				return err
			}
			if within {
				if err := a.permittedWithin(tx, c.me, permission, &owned{s, v}); err != nil {
					return err
				}
			}
			// The id is known before the body runs, so the body can name it, as in
			// a member made for the new project. Keys and serials never change in a
			// body, so it can't change either.
			counted, err := a.serials(tx, s, v, body.counters)
			if err != nil {
				return err
			}
			ref := collection.NewDoc()
			if key, ok := s.id(v); ok {
				ref = collection.Doc(key)
			}
			record.ID = ref.ID
			if do != nil {
				if err := do(body, entity); err != nil {
					return err
				}
			}
			// A project starts with its default roles, made with it.
			if err := a.seedRoles(body, s, ref.ID); err != nil {
				return err
			}
			if err := s.validate(v); err != nil {
				return err
			}
			record.CreatedAt, record.CreatedBy = now, c.me
			// With key fields, the same values are the same entity: creating it again
			// updates it, and it keeps when and by whom it was first made, and the
			// numbers it was given. A unique key refuses to be made twice instead.
			if snap, err := tx.Get(ref); err == nil {
				if taken := s.uniqueKey(); taken != nil {
					return invalid("%s is already taken", label(taken.name))
				}
				// A key made from another field, like a phase's from its title, names
				// a new one, so another already there isn't it made again.
				if madeFrom != "" {
					return invalid("%s is already taken", label(madeFrom))
				}
				if err := s.ownedByAnother(snap.Data(), c.me); err != nil {
					return err
				}
				before = snap.Data()
				existing := new(E)
				if err := snap.DataTo(existing); err != nil {
					return err
				}
				record.CreatedAt, record.CreatedBy = P(existing).record().CreatedAt, P(existing).record().CreatedBy
				kept := reflect.ValueOf(existing).Elem()
				for _, f := range counted {
					v.FieldByIndex(f.index).Set(kept.FieldByIndex(f.index))
				}
			} else if status.Code(err) != codes.NotFound {
				return err
			}
			record.ID, record.UpdatedAt, record.UpdatedBy = ref.ID, now, c.me
			if err := a.unique(tx, s, v, ref.ID); err != nil {
				return err
			}
			id, after = ref.ID, s.data(v)
			changed, err := body.save()
			if err != nil {
				return err
			}
			kept, err := a.keep(ctx, tx, s, ref.ID, before, after, body.command, c.me, now)
			if err != nil {
				return err
			}
			pointed = append(changed, kept...)
			saved = entity
			return tx.Set(ref, after)
		}

		id, _ = c.input["id"].(string)
		if id == "" {
			return invalid("the command needs the id of what it acts on")
		}
		if !validID(id) {
			return &Failure{Status: 404, Message: "that doesn't exist"}
		}
		ref := collection.Doc(id)
		snap, err := tx.Get(ref)
		if status.Code(err) == codes.NotFound {
			return &Failure{Status: 404, Message: "that doesn't exist any more"}
		}
		if err != nil {
			return err
		}
		if err := snap.DataTo(entity); err != nil {
			return err
		}
		before = snap.Data()
		body.before = before
		if err := a.permitted(ctx, tx, c.me, permission, &owned{s, v}); err != nil {
			return err
		}
		if action == "delete" {
			// Its body says whether it may go, as a require does, before it goes, and
			// what goes with it, like the issues of a phase moving to another. What it
			// dispatches to delete it again, like a link's other side, does nothing.
			body.deleting = ref.Path
			if do != nil {
				if err := do(body, entity); err != nil {
					return err
				}
			}
			changed, err := body.save()
			if err != nil {
				return err
			}
			kept, err := a.keep(ctx, tx, s, id, before, nil, body.command, c.me, now)
			if err != nil {
				return err
			}
			pointed = append(changed, kept...)
			saved = entity
			return tx.Delete(ref)
		}
		// An update takes what's sent; another command, like move, only the fields
		// it says it changes, like the phase an issue moves to.
		if action == "update" || fields != nil {
			input := map[string]any{}
			for name, value := range c.input {
				if name == "id" || s.isKey(name) {
					continue
				}
				if fields != nil && !c.system && !contains(fields, name) && s.field(name) != nil {
					return invalid("%s can't be changed by %s::%s", label(name), s.name, action)
				}
				input[name] = value
			}
			if err := s.decode(input, v); err != nil {
				return err
			}
			s.normalize(v)
			if err := a.mentionsIn(ctx, tx, s, v); err != nil {
				return err
			}
		}
		if do != nil {
			if err := do(body, entity); err != nil {
				return err
			}
		}
		if err := s.validate(v); err != nil {
			return err
		}
		// Moving something into another project needs the same permission there.
		if err := a.permittedWhereMoved(ctx, tx, c.me, permission, s, before, v); err != nil {
			return err
		}
		if err := a.unique(tx, s, v, id); err != nil {
			return err
		}
		record.UpdatedAt, record.UpdatedBy = now, c.me
		after = s.data(v)
		changed, err := body.save()
		if err != nil {
			return err
		}
		kept, err := a.keep(ctx, tx, s, id, before, after, body.command, c.me, now)
		if err != nil {
			return err
		}
		pointed = append(changed, kept...)
		saved = entity
		return tx.Set(ref, after)
	})
	if err != nil {
		return "", err
	}

	a.publish(c.ctx, append([]event{{
		Type:    s.name + ".updated",
		Entity:  s.entity,
		ID:      id,
		Version: now.UnixNano(),
		Before:  before,
		After:   after,
	}}, pointed...)...)
	for _, then := range afterwards {
		then(&System{app: a, ctx: c.ctx}, saved)
	}
	return id, nil
}

// counting is the last number a counter gave in a transaction, to be written back.
type counting struct {
	ref   *firestore.DocumentRef
	value float64
}

// serials gives a new entity its serials: the next number of its kind, or, for a
// serial per book, the next number within its book. Counters are kept in
// serials/<collection>.<field>[.<id>], and read and written in the command's
// transaction, so two entities made at once never get the same number. A counter
// is read once per transaction; counters holds what each has given so far, and
// is written back by save once every read is done. Returns the fields it numbered.
func (a *App) serials(tx *firestore.Transaction, s *schema, v reflect.Value, counters map[string]*counting) ([]*field, error) {
	var numbered []*field
	for i := range s.fields {
		f := &s.fields[i]
		if !f.serial {
			continue
		}
		name := s.collection + "." + f.name
		if f.per != "" {
			parent := s.field(f.per)
			if parent == nil {
				return nil, fmt.Errorf("one: %s is counted per %s, which %s doesn't have", f.name, f.per, s.name)
			}
			id := fmt.Sprint(v.FieldByIndex(parent.index).Interface())
			if id == "" {
				return nil, invalid("%s is required", label(f.per))
			}
			if !validID(id) {
				return nil, invalid("there's no such %s", strings.ToLower(label(f.per)))
			}
			name += "." + id
		}
		ref := a.store.Collection("serials").Doc(name)
		counter, read := counters[ref.Path]
		if !read {
			counter = &counting{ref: ref}
			snap, err := tx.Get(ref)
			if err == nil {
				switch last := snap.Data()["last"].(type) {
				case float64:
					counter.value = last
				case int64:
					counter.value = float64(last)
				}
			} else if status.Code(err) != codes.NotFound {
				return nil, err
			}
			counters[ref.Path] = counter
		}
		counter.value++
		v.FieldByIndex(f.index).SetFloat(counter.value)
		numbered = append(numbered, f)
	}
	return numbered, nil
}

// ownedByAnother refuses to make again, under the same key, something that belongs
// to someone else: making it again would replace it and make the caller its owner.
func (s *schema) ownedByAnother(stored map[string]any, me string) error {
	for _, f := range s.fields {
		if f.initial != "me" {
			continue
		}
		if owner, _ := stored[f.name].(string); owner != "" && owner != me {
			return &Failure{Status: 409, Message: "that belongs to someone else"}
		}
	}
	return nil
}

// uniqueKey is a key field that's also unique, if the entity has one: then the
// same key can't be made twice.
func (s *schema) uniqueKey() *field {
	for _, key := range s.keys {
		if key.unique {
			return key
		}
	}
	return nil
}

func (s *schema) isKey(name string) bool {
	for _, key := range s.keys {
		if key.name == name {
			return true
		}
	}
	return false
}

func (a *App) unique(tx *firestore.Transaction, s *schema, v reflect.Value, id string) error {
	for _, f := range s.fields {
		if !f.unique {
			continue
		}
		if v.FieldByIndex(f.index).IsZero() {
			continue // an optional field left empty is never taken
		}
		value := v.FieldByIndex(f.index).Interface()
		docs, err := tx.Documents(a.store.Collection(s.collection).Where(f.name, "==", value).Limit(2)).GetAll()
		if err != nil {
			return err
		}
		for _, doc := range docs {
			if doc.Ref.ID != id {
				return invalid("%s is already taken", label(f.name))
			}
		}
	}
	return nil
}

// owned is an entity whose owner a permission check may need.
type owned struct {
	schema *schema
	value  reflect.Value
}

// permitted says whether someone may do something, and if not, why, in words. With
// a transaction and an entity, a role held within it, such as in a project, counts
// too.
func (a *App) permitted(ctx context.Context, tx *firestore.Transaction, me string, p Permission, entity *owned) error {
	switch p {
	case Anyone:
		return nil
	case SignedIn:
		if me == "" {
			return &Failure{Status: 401, Message: "sign in to do this"}
		}
		return nil
	case Owner:
		if me == "" {
			return &Failure{Status: 401, Message: "sign in to do this"}
		}
		if entity == nil {
			return nil // creating something: whoever creates it owns it
		}
		if f := entity.schema.field("owner"); f != nil && entity.value.FieldByIndex(f.index).Interface() == me {
			return nil
		}
		return &Failure{Status: 403, Message: "only its owner can do this"}
	}
	if me == "" {
		return &Failure{Status: 401, Message: "sign in to do this"}
	}
	if a.grants(ctx, me, string(p)) {
		return nil
	}
	if tx != nil && entity != nil {
		return a.permittedWithin(tx, me, p, entity)
	}
	return &Failure{Status: 403, Message: "you don't have permission to do this"}
}

// permittedWhereMoved checks a permission again when a change moves an entity into
// another project, or whatever a role is held within.
func (a *App) permittedWhereMoved(ctx context.Context, tx *firestore.Transaction, me string, p Permission, s *schema, before map[string]any, v reflect.Value) error {
	if !a.scopes(p) {
		return nil
	}
	var scopes []*schema
	for _, ro := range a.reg.scoped {
		scopes = append(scopes, a.reg.schemas[ro.scope])
	}
	for _, r := range a.reg.defined {
		scopes = append(scopes, a.reg.schemas[r.scope])
	}
	for _, scope := range scopes {
		after, err := a.within(tx, &owned{s, v}, scope)
		if err != nil {
			return err
		}
		was := reflect.New(s.typ).Elem()
		was.Addr().Interface().(interface{ record() *Record }).record().ID = v.Addr().Interface().(interface{ record() *Record }).record().ID
		for _, f := range s.fields {
			if f.refers != "" {
				if id, ok := before[f.name].(string); ok && was.FieldByIndex(f.index).Kind() == reflect.String {
					was.FieldByIndex(f.index).SetString(id)
				}
			}
		}
		earlier, err := a.within(tx, &owned{s, was}, scope)
		if err != nil {
			return err
		}
		if after != earlier {
			return a.permitted(ctx, tx, me, p, &owned{s, v})
		}
	}
	return nil
}

// scopes says whether a role held within an entity grants a permission.
func (a *App) scopes(p Permission) bool {
	if len(a.reg.defined) > 0 {
		return true // a project's own roles may allow anything
	}
	for _, ro := range a.reg.scoped {
		if contains(ro.permissions, string(p)) {
			return true
		}
	}
	return false
}

// permittedWithin says whether a person holds a role that grants the permission
// within the entity the command acts in: the project an issue points at, or the
// project itself. A member that points at both and names the role grants it.
func (a *App) permittedWithin(tx *firestore.Transaction, me string, p Permission, entity *owned) error {
	if allowed, err := a.allowedByRoles(tx, me, p, entity); err != nil || allowed {
		return err
	}
	for _, ro := range a.reg.scoped {
		if !contains(ro.permissions, string(p)) {
			continue
		}
		scope, member := a.reg.schemas[ro.scope], a.reg.schemas[ro.member]
		within, err := a.within(tx, entity, scope)
		if err != nil {
			return err
		}
		if within == "" {
			continue
		}
		var place, person *field
		for i := range member.fields {
			switch member.fields[i].refers {
			case scope.entity:
				place = &member.fields[i]
			case "user":
				person = &member.fields[i]
			}
		}
		if place == nil || person == nil {
			return fmt.Errorf("one: role %s is granted by %s, which needs a field pointing at %s and one holding a person", ro.name, member.name, scope.name)
		}
		query := a.store.Collection(member.collection).Where(place.name, "==", within).Where(person.name, "==", me).Limit(10)
		docs, err := tx.Documents(query).GetAll()
		if err != nil {
			return err
		}
		for _, doc := range docs {
			if doc.Data()["role"] == ro.name {
				return nil
			}
		}
	}
	return &Failure{Status: 403, Message: "you don't have permission to do this"}
}

// within is the id of the entity of a kind, such as a project, that an entity is
// held within: its own, when it's of that kind; the one a field of it points at,
// as an issue's project; or the one that points at, as a comment's issue's
// project, read in the command's transaction.
func (a *App) within(tx *firestore.Transaction, o *owned, scope *schema) (string, error) {
	if o.schema == scope {
		return o.value.Addr().Interface().(interface{ record() *Record }).record().ID, nil
	}
	for _, f := range o.schema.fields {
		if f.refers == scope.entity {
			id, _ := o.value.FieldByIndex(f.index).Interface().(string)
			return id, nil
		}
	}
	for _, f := range o.schema.fields {
		through := a.reg.entity(f.refers)
		if through == nil {
			continue
		}
		for _, g := range through.fields {
			if g.refers != scope.entity {
				continue
			}
			id, _ := o.value.FieldByIndex(f.index).Interface().(string)
			if id == "" {
				return "", nil
			}
			snap, err := tx.Get(a.store.Collection(through.collection).Doc(id))
			if status.Code(err) == codes.NotFound {
				return "", nil
			}
			if err != nil {
				return "", err
			}
			held, _ := snap.Data()[g.name].(string)
			return held, nil
		}
	}
	return "", nil
}

// grants reads a person's role, users/{id}.role_id, and whether roles/{role} holds
// the permission.
func (a *App) grants(ctx context.Context, me, permission string) bool {
	user, err := a.store.Collection("users").Doc(me).Get(ctx)
	if err != nil {
		return false
	}
	roleID, _ := user.Data()["role_id"].(string)
	if roleID == "" {
		return false
	}
	role, err := a.store.Collection("roles").Doc(roleID).Get(ctx)
	if err != nil {
		return false
	}
	permissions, _ := role.Data()["permissions"].([]any)
	for _, p := range permissions {
		if p == permission {
			return true
		}
	}
	return false
}

// mentionPattern is an @username in text: a GitHub username's letters, digits and
// single dashes, not part of an email address.
var mentionPattern = regexp.MustCompile(`(?:^|[^\w@.])@([A-Za-z0-9](?:[A-Za-z0-9]|-[A-Za-z0-9]){0,38})\b`)

// mentionsIn fills each list of people an entity's text names, like a comment's
// mentioned from its body: whoever's username follows an @, once each, in the order
// first named. A name nobody has is left out.
func (a *App) mentionsIn(ctx context.Context, tx *firestore.Transaction, s *schema, v reflect.Value) error {
	for _, f := range s.fields {
		if f.mentions == "" {
			continue
		}
		source := s.field(f.mentions)
		if source == nil {
			continue
		}
		// Each name as written and in lowercase, since a username is stored as
		// GitHub gives it; who it is is known by its lowercase.
		var names, asked []any
		seen := map[string]bool{}
		for _, m := range mentionPattern.FindAllStringSubmatch(v.FieldByIndex(source.index).String(), -1) {
			name := strings.ToLower(m[1])
			if !seen[name] {
				seen[name] = true
				names = append(names, name)
				asked = append(asked, name)
			}
			if !seen[m[1]] {
				seen[m[1]] = true
				asked = append(asked, m[1])
			}
		}
		found := map[string]string{}
		for start := 0; start < len(asked); start += 30 {
			docs, err := tx.Documents(a.store.Collection("users").Where("username", "in", asked[start:min(start+30, len(asked))])).GetAll()
			if err != nil {
				return err
			}
			for _, doc := range docs {
				if name, _ := doc.Data()["username"].(string); name != "" {
					found[strings.ToLower(name)] = doc.Ref.ID
				}
			}
		}
		people := []string{}
		for _, name := range names {
			if id, ok := found[name.(string)]; ok && !contains(people, id) {
				people = append(people, id)
			}
		}
		v.FieldByIndex(f.index).Set(reflect.ValueOf(people))
	}
	return nil
}
