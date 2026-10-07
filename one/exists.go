// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"fmt"
	"reflect"
)

// Exists says whether the query finds an entity that keep, when given, also
// accepts, read in the command's transaction: whether there's a step from the
// phase an issue was in to the one it's moving to that the person's role may take,
// as in
//
//	one.Exists(c, one.Where[Step]("from", c.Was("phase")).And("to", i.Phase), func(s *Step) (bool, error) {
//		return c.Held(s.Roles)
//	})
func Exists[E any, P entityPointer[E]](c *Ctx, q Query, keep func(*E) (bool, error)) (bool, error) {
	s := c.app.reg.schemas[reflect.TypeFor[E]()]
	if s == nil {
		return false, fmt.Errorf("one: %s isn't an entity any module uses", reflect.TypeFor[E]().Name())
	}
	query := c.app.store.Collection(s.collection).Query
	for _, cond := range q.conditions {
		switch {
		case cond.has:
			query = query.Where(cond.field, "array-contains", cond.value)
		case !cond.except:
			query = query.Where(cond.field, "==", cond.value)
		}
	}
	docs, err := c.tx.Documents(query).GetAll()
	if err != nil {
		return false, err
	}
	for _, doc := range docs {
		excepted := false
		for _, cond := range q.conditions {
			if cond.except && same(doc.Data()[cond.field], cond.value) {
				excepted = true
			}
		}
		if excepted {
			continue
		}
		if keep == nil {
			return true, nil
		}
		entity := new(E)
		if err := doc.DataTo(entity); err != nil {
			return false, err
		}
		if ok, err := keep(entity); err != nil || ok {
			return ok, err
		}
	}
	return false, nil
}

// Held says whether the person running the command holds one of these roles, by
// id, where the command's entity is held, like an issue's project.
func (c *Ctx) Held(roles []string) (bool, error) {
	if c.me == "" || c.entity == nil || len(roles) == 0 {
		return false, nil
	}
	for _, r := range c.app.reg.defined {
		f, err := c.app.rolesFields(r)
		if err != nil {
			return false, err
		}
		within, err := c.app.within(c.tx, c.entity, f.scope)
		if err != nil || within == "" {
			return false, err
		}
		query := c.app.store.Collection(f.member.collection).Where(f.memberPlace.name, "==", within).Where(f.memberPerson.name, "==", c.me).Limit(20)
		docs, err := c.tx.Documents(query).GetAll()
		if err != nil {
			return false, err
		}
		for _, doc := range docs {
			if id, _ := doc.Data()[f.memberRole.name].(string); contains(roles, id) {
				return true, nil
			}
		}
	}
	return false, nil
}
