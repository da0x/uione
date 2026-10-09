// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"fmt"
	"reflect"
)

// A command is the one way a record is made, changed or deleted, so a command that
// changes another record dispatches that record's command: it runs in the same
// transaction, as the same person, its body and requires too, and what it changes
// is saved with the rest, both or neither. Its permission isn't asked again, since
// the command dispatching it was allowed.
//
// body is the dispatched command's body, or nil for one without; given is what it's
// sent besides its entity's fields, which its body reads with Input.

// DispatchCreate makes entity as its create command does, like a board with the
// phases of the preset it starts from.
func DispatchCreate[E any, P entityPointer[E]](c *Ctx, entity *E, given map[string]any, body func(*Ctx, *E) error) error {
	s, err := dispatchedSchema[E](c)
	if err != nil {
		return err
	}
	v := reflect.ValueOf(entity).Elem()
	var run func() error
	if body != nil {
		run = func() error { return c.within(s, v, nil, given, func(d *Ctx) error { return body(d, entity) }) }
	}
	return create(c, s, v, run)
}

// DispatchUpdate changes the entity with that id as its command does: set gives it
// the values the dispatch names, then its body runs, with what it was before for
// was.
func DispatchUpdate[E any, P entityPointer[E]](c *Ctx, id string, set func(*E), given map[string]any, body func(*Ctx, *E) error) error {
	s, err := dispatchedSchema[E](c)
	if err != nil {
		return err
	}
	entity, err := Read[E, P](c, id)
	if err != nil {
		return err
	}
	v := reflect.ValueOf(entity).Elem()
	before := c.stored(c.app.store.Collection(s.collection).Doc(id).Path)
	if set != nil {
		set(entity)
	}
	s.normalize(v)
	if body == nil {
		return nil
	}
	return c.within(s, v, before, given, func(d *Ctx) error { return body(d, entity) })
}

// DispatchDelete deletes the entity with that id as its command does, its body
// first, which may say it can't go, or what goes with it. One that's being deleted
// already in this step, like a link's other side deleting it back, isn't deleted
// again, and its body doesn't run again.
func DispatchDelete[E any, P entityPointer[E]](c *Ctx, id string, given map[string]any, body func(*Ctx, *E) error) error {
	s, err := dispatchedSchema[E](c)
	if err != nil {
		return err
	}
	ref := c.app.store.Collection(s.collection).Doc(id)
	if c.deleting == ref.Path {
		return nil
	}
	for _, g := range c.gone {
		if g.ref.Path == ref.Path {
			return nil
		}
	}
	entity, err := Read[E, P](c, id)
	if err != nil {
		return err
	}
	before := c.stored(ref.Path)
	c.gone = append(c.gone, &gone{schema: s, ref: ref, before: before})
	if body == nil {
		return nil
	}
	return c.within(s, reflect.ValueOf(entity).Elem(), before, given, func(d *Ctx) error { return body(d, entity) })
}

func dispatchedSchema[E any](c *Ctx) (*schema, error) {
	s := c.app.reg.schemas[reflect.TypeFor[E]()]
	if s == nil {
		return nil, fmt.Errorf("one: %s isn't an entity any module uses", reflect.TypeFor[E]().Name())
	}
	return s, nil
}

// within runs a dispatched command's body in this step, with its own entity, what
// that was and what it was sent, and keeps what the body read, made and deleted.
func (c *Ctx) within(s *schema, v reflect.Value, before, given map[string]any, body func(*Ctx) error) error {
	d := *c
	d.entity = &owned{s, v}
	d.before = before
	d.given = given
	err := body(&d)
	c.reads, c.made, c.gone = d.reads, d.made, d.gone
	return err
}

// stored is an entity read in this step as it was stored, by its path.
func (c *Ctx) stored(path string) map[string]any {
	for _, r := range c.reads {
		if r.ref.Path == path {
			return r.stored
		}
	}
	return nil
}
