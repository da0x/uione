// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"fmt"
	"reflect"
)

// A record that gives a role in a place to everyone in a group: a team given the
// reporter role in a project. Each person in the group holds the role there, as a
// member marked with the record it comes from, made and taken away as they join and
// leave the group, and as the record is made, changed or deleted. Where two records
// give a person the same role in the same place, they keep it while either does.
type giving struct {
	typ   reflect.Type
	inner string // the namespace whose roles it gives, like projects
	each  string // the namespace whose roles make up the group, like teams
}

// Gives declares that each G gives a role among the roles of the namespace inner, in
// the place its field pointing there names, to each member of the group of the
// namespace each that its other field names: one.Gives[Access]("projects", "teams").
func Gives[G any, P entityPointer[G]](inner, each string) Item {
	return &giving{typ: reflect.TypeFor[G](), inner: inner, each: each}
}

func (g *giving) register(r *registry, ns string) {
	r.schema(g.typ, ns)
	r.gives = append(r.gives, g)
}

// A giving with what it's found by: the record's schema, the roles it gives and the
// group's roles, and the record's fields naming the place, the role and the group.
type grant struct {
	schema             *schema
	inner, each        *rolesFields
	place, role, group *field
	through            *field // the inner member's field saying which record gave it
}

func (a *App) givings() ([]grant, error) {
	var out []grant
	for _, g := range a.reg.gives {
		var inner, each *RolesSpec
		for _, r := range a.reg.defined {
			if r.ns == g.inner {
				inner = r
			}
			if r.ns == g.each {
				each = r
			}
		}
		s := a.reg.schemas[g.typ]
		if inner == nil || each == nil || s == nil {
			return nil, fmt.Errorf("one: %v gives %s's roles to %s's members, but one of them has no roles", g.typ, g.inner, g.each)
		}
		in, err := a.rolesFields(inner)
		if err != nil {
			return nil, err
		}
		of, err := a.rolesFields(each)
		if err != nil {
			return nil, err
		}
		found := grant{schema: s, inner: in, each: of, through: in.member.field("through")}
		for i := range s.fields {
			f := &s.fields[i]
			switch f.refers {
			case in.scope.entity:
				found.place = f
			case in.role.entity:
				found.role = f
			case of.scope.entity:
				found.group = f
			}
		}
		if found.place == nil || found.role == nil || found.group == nil || found.through == nil {
			return nil, fmt.Errorf("one: %s gives a role, so it needs fields pointing at a %s, a %s and a %s, and %s needs a through field",
				s.entity, in.scope.name, in.role.name, of.scope.name, in.member.entity)
		}
		out = append(out, found)
	}
	return out, nil
}

// give keeps what records like a team's role in a project give, as they change, and
// as the group's members and the place's members do.
func (a *App) give(c *Ctx, s *schema, action, id string, v reflect.Value, before map[string]any) error {
	if len(a.reg.gives) == 0 {
		return nil
	}
	gs, err := a.givings()
	if err != nil {
		return err
	}
	for _, g := range gs {
		switch s {
		case g.schema:
			place, group, role := v.FieldByIndex(g.place.index).String(), v.FieldByIndex(g.group.index).String(), v.FieldByIndex(g.role.index).String()
			switch action {
			case "create":
				err = a.giveGroup(c, g, id, place, group, role)
			case "delete":
				err = a.takeGrant(c, g, id)
			case "update":
				was := func(f *field) string { s, _ := before[f.name].(string); return s }
				if was(g.place) == place && was(g.group) == group && was(g.role) == role {
					continue
				}
				if err = a.takeGrant(c, g, id); err == nil {
					err = a.giveGroup(c, g, id, place, group, role)
				}
			}
		case g.each.member:
			group, person := v.FieldByIndex(g.each.memberPlace.index).String(), v.FieldByIndex(g.each.memberPerson.index).String()
			given, err := c.tx.Documents(a.store.Collection(g.schema.collection).Where(g.group.name, "==", group)).GetAll()
			if err != nil {
				return err
			}
			for _, d := range given {
				if c.leaving[d.Ref.Path] {
					continue
				}
				place, _ := d.Data()[g.place.name].(string)
				role, _ := d.Data()[g.role.name].(string)
				switch action {
				case "create":
					err = a.giveOne(c, g, place, person, role, d.Ref.ID)
				case "delete":
					err = a.takeOne(c, g, place, person, role, "", id)
				}
				if err != nil {
					return err
				}
			}
		case g.inner.member:
			if action != "delete" {
				continue
			}
			through := v.FieldByIndex(g.through.index).String()
			if through == "" {
				continue
			}
			if _, err := c.tx.Get(a.store.Collection(g.schema.collection).Doc(through)); err == nil {
				return invalid("this role comes from the %s, so it's taken away there", g.each.scope.name)
			}
		}
		if err != nil {
			return err
		}
	}
	return nil
}

// giveGroup gives a record's role to each person in its group.
func (a *App) giveGroup(c *Ctx, g grant, id, place, group, role string) error {
	people, err := c.tx.Documents(a.store.Collection(g.each.member.collection).Where(g.each.memberPlace.name, "==", group)).GetAll()
	if err != nil {
		return err
	}
	given := map[string]bool{}
	for _, m := range people {
		person, _ := m.Data()[g.each.memberPerson.name].(string)
		if given[person] || c.leaving[m.Ref.Path] {
			continue
		}
		given[person] = true
		if err := a.giveOne(c, g, place, person, role, id); err != nil {
			return err
		}
	}
	return nil
}

// The member a person holds a role in a place by, as its key names it.
func (g grant) member(place, person, role string) reflect.Value {
	m := reflect.New(g.inner.member.typ).Elem()
	m.FieldByIndex(g.inner.memberPlace.index).SetString(place)
	m.FieldByIndex(g.inner.memberPerson.index).SetString(person)
	m.FieldByIndex(g.inner.memberRole.index).SetString(role)
	return m
}

// giveOne makes the member that gives a person a role in a place through a record,
// unless they hold it there already, themselves or through another.
func (a *App) giveOne(c *Ctx, g grant, place, person, role, through string) error {
	m := g.member(place, person, role)
	if key, ok := g.inner.member.id(m); ok {
		ref := a.store.Collection(g.inner.member.collection).Doc(key)
		if _, err := c.tx.Get(ref); err == nil && !c.leaving[ref.Path] {
			return nil
		}
		for _, list := range [][]*made{c.made, c.making} {
			for _, made := range list {
				if made.ref.Path == ref.Path {
					return nil
				}
			}
		}
	}
	m.FieldByIndex(g.through.index).SetString(through)
	return create(c, g.inner.member, m, nil)
}

// takeGrant takes away what a record gave, each person keeping the role while
// another record still gives it to them.
func (a *App) takeGrant(c *Ctx, g grant, id string) error {
	given, err := c.tx.Documents(a.store.Collection(g.inner.member.collection).Where(g.through.name, "==", id)).GetAll()
	if err != nil {
		return err
	}
	for _, d := range given {
		place, _ := d.Data()[g.inner.memberPlace.name].(string)
		person, _ := d.Data()[g.inner.memberPerson.name].(string)
		role, _ := d.Data()[g.inner.memberRole.name].(string)
		if err := a.takeOne(c, g, place, person, role, id, ""); err != nil {
			return err
		}
	}
	return nil
}

// takeOne takes a role a record gave a person in a place, unless another record, or
// the same one through another membership of its group, still gives it: then it's
// kept, said to come from that. leavingGrant is a record going, and leavingMember a
// membership of the group going, neither of which gives anything any more.
func (a *App) takeOne(c *Ctx, g grant, place, person, role, leavingGrant, leavingMember string) error {
	key, ok := g.inner.member.id(g.member(place, person, role))
	if !ok {
		return nil
	}
	ref := a.store.Collection(g.inner.member.collection).Doc(key)
	held, err := c.tx.Get(ref)
	if err != nil || c.leaving[ref.Path] {
		return nil
	}
	through, _ := held.Data()[g.through.name].(string)
	if through == "" {
		return nil // their own, not given
	}
	if !a.isGrant(c, g, through) {
		return nil // given by something else, like an organization
	}
	other, err := a.stillGiven(c, g, place, person, role, leavingGrant, leavingMember)
	if err != nil {
		return err
	}
	switch {
	case other == through:
		return nil
	case other != "":
		m := g.member(place, person, role)
		m.FieldByIndex(g.through.index).SetString(other)
		return create(c, g.inner.member, m, nil)
	}
	c.drop(g.inner.member, held)
	return nil
}

func (a *App) isGrant(c *Ctx, g grant, id string) bool {
	_, err := c.tx.Get(a.store.Collection(g.schema.collection).Doc(id))
	return err == nil
}

// stillGiven is a record that still gives a person a role in a place, through a
// membership of its group that isn't going, or nothing.
func (a *App) stillGiven(c *Ctx, g grant, place, person, role, leavingGrant, leavingMember string) (string, error) {
	records, err := c.tx.Documents(a.store.Collection(g.schema.collection).Where(g.place.name, "==", place).Where(g.role.name, "==", role)).GetAll()
	if err != nil {
		return "", err
	}
	for _, r := range records {
		if r.Ref.ID == leavingGrant || c.leaving[r.Ref.Path] {
			continue
		}
		group, _ := r.Data()[g.group.name].(string)
		in, err := c.tx.Documents(a.store.Collection(g.each.member.collection).Where(g.each.memberPlace.name, "==", group).Where(g.each.memberPerson.name, "==", person)).GetAll()
		if err != nil {
			return "", err
		}
		for _, m := range in {
			if m.Ref.ID != leavingMember && !c.leaving[m.Ref.Path] {
				return r.Ref.ID, nil
			}
		}
	}
	return "", nil
}
