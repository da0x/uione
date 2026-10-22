// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"fmt"
	"reflect"
)

// Exists says whether the query finds an entity that keep, when given, also
// accepts, read in the command's transaction: whether there's a step from the
// stage an issue was in to the one it's moving to that the person's role may take,
// as in
//
//	one.Exists(c, one.Where[Step]("from", c.Was("stage")).And("to", i.Stage), func(s *Step) (bool, error) {
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
		case !cond.except && cond.value != nil:
			query = query.Where(cond.field, "==", cond.value)
		}
	}
	docs, err := c.tx.Documents(query).GetAll()
	if err != nil {
		return false, err
	}
	for _, doc := range docs {
		// A field wanted to have none is checked here, since Firestore can't ask it of
		// a field that's missing or empty.
		if !q.keeps(doc.Data()) {
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

// Count says how many entities the query finds, as they're stored, counted in the
// command's transaction without reading them: how many projects an organization
// has, before one more is made, as in
//
//	one.Count[Project](c, one.Where[Project]("organization", p.Organization))
//
// Only what a query can ask of the store is counted on: a field's value, or a list
// holding it. A condition Except leaves out reads each one instead.
func Count[E any, P entityPointer[E]](c *Ctx, q Query) (int, error) {
	s := c.app.reg.schemas[reflect.TypeFor[E]()]
	if s == nil {
		return 0, fmt.Errorf("one: %s isn't an entity any module uses", reflect.TypeFor[E]().Name())
	}
	query := c.app.store.Collection(s.collection).Query
	checked := false
	for _, cond := range q.conditions {
		switch {
		case cond.has:
			query = query.Where(cond.field, "array-contains", cond.value)
		case !cond.except && cond.value != nil:
			query = query.Where(cond.field, "==", cond.value)
		default:
			checked = true
		}
	}
	if checked {
		n := 0
		docs, err := c.tx.Documents(query).GetAll()
		if err != nil {
			return 0, err
		}
		for _, doc := range docs {
			if q.keeps(doc.Data()) {
				n++
			}
		}
		return n, nil
	}
	result, err := query.NewAggregationQuery().WithCount("n").Transaction(c.tx).Get(c.Context)
	if err != nil {
		return 0, err
	}
	n, ok := result["n"].(interface{ GetIntegerValue() int64 })
	if !ok {
		return 0, fmt.Errorf("one: counting %s gave %T, not a number", s.entity, result["n"])
	}
	return int(n.GetIntegerValue()), nil
}

// keeps says whether a document passes what a query asks after reading: what Except
// leaves out, and a field wanted to have none.
func (q Query) keeps(data map[string]any) bool {
	for _, cond := range q.conditions {
		if cond.except && same(data[cond.field], cond.value) {
			return false
		}
		if !cond.except && !cond.has && cond.value == nil && !none(data[cond.field]) {
			return false
		}
	}
	return true
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
		if err != nil {
			return false, err
		}
		// Roles of a place the record isn't within, like an organization's for an
		// issue, say nothing about it; the next set may.
		if within == "" {
			continue
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
