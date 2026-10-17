// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"fmt"
	"reflect"
	"strings"
)

// A role that holds another, in each of what's within: an organization's owner
// maintains each of its projects. Whoever holds it is given a member record in each
// of those, marked with the membership it comes from, made and taken away with it,
// and with each project that comes into the organization or leaves it. What a
// person holds there themselves is left as it is.
type holding struct {
	role  string // the role that holds, like owner
	ns    string // the namespace whose roles it holds one of, like projects
	held  string // the role it holds there, like maintainer
	field string // the field of that namespace's scope pointing here, like organization
}

// Holds says that whoever holds role among these roles also holds held among the
// roles of the namespace ns, in each of its scope that field points here from: an
// organization's owner holds maintainer in each project whose organization it is.
func (r *RolesSpec) Holds(role, ns, held, field string) *RolesSpec {
	r.holds = append(r.holds, holding{role, ns, held, field})
	return r
}

// A holding with what it's found by: the roles it's from and to, their fields, the
// field of the inner scope pointing at the outer, and the inner member's field
// saying which outer membership gave it.
type heldRole struct {
	holding
	from, to *RolesSpec
	outer    *rolesFields
	inner    *rolesFields
	pointing *field // like project.organization
	through  *field // like member.through
}

func (a *App) holdings() ([]heldRole, error) {
	var out []heldRole
	for _, r := range a.reg.defined {
		for _, h := range r.holds {
			var to *RolesSpec
			for _, other := range a.reg.defined {
				if other.ns == h.ns {
					to = other
				}
			}
			if to == nil {
				return nil, fmt.Errorf("one: role %s holds %s in %s, which has no roles", h.role, h.held, h.ns)
			}
			outer, err := a.rolesFields(r)
			if err != nil {
				return nil, err
			}
			inner, err := a.rolesFields(to)
			if err != nil {
				return nil, err
			}
			pointing := inner.scope.field(h.field)
			if pointing == nil || pointing.refers != outer.scope.entity {
				return nil, fmt.Errorf("one: role %s holds %s in each %s, but its %s doesn't point at a %s", h.role, h.held, inner.scope.name, h.field, outer.scope.name)
			}
			through := inner.member.field("through")
			if through == nil {
				return nil, fmt.Errorf("one: role %s holds %s in each %s, whose members don't say what gave them, a through field", h.role, h.held, inner.scope.name)
			}
			out = append(out, heldRole{h, r, to, outer, inner, pointing, through})
		}
	}
	return out, nil
}

// inherit keeps what's held through another role as it changes: an outer member
// made or gone, like an organization's owner, and an inner scope made, moved or
// deleted, like a project. An inner member that comes from an outer one is the
// outer's to take away, not its own.
func (a *App) inherit(c *Ctx, s *schema, action, id string, v reflect.Value, before map[string]any) error {
	hs, err := a.holdings()
	if err != nil || len(hs) == 0 {
		return err
	}
	for _, h := range hs {
		switch s {
		case h.outer.member:
			if err := a.inheritMember(c, h, action, id, v); err != nil {
				return err
			}
		case h.inner.scope:
			if err := a.inheritScope(c, h, action, id, v, before); err != nil {
				return err
			}
		case h.inner.member:
			if action == "delete" && v.FieldByIndex(h.through.index).String() != "" {
				return invalid("this role comes from the %s, so it's taken away there", h.outer.scope.name)
			}
		}
	}
	return nil
}

// The role an outer member holds, by its name: owner.
func (a *App) roleName(c *Ctx, f *rolesFields, roleID string) (string, error) {
	snap, err := c.tx.Get(a.store.Collection(f.role.collection).Doc(roleID))
	if err != nil {
		for _, m := range c.making {
			if m.schema == f.role && m.ref.ID == roleID {
				return m.value.FieldByIndex(f.name.index).String(), nil
			}
		}
		return "", nil // a role that's gone holds nothing
	}
	name, _ := snap.Data()[f.name.name].(string)
	return name, nil
}

// An outer member made gives its person the held role in each inner scope within
// its place; gone, it takes away what it gave.
func (a *App) inheritMember(c *Ctx, h heldRole, action, id string, v reflect.Value) error {
	switch action {
	case "create":
		role, err := a.roleName(c, h.outer, v.FieldByIndex(h.outer.memberRole.index).String())
		if err != nil || role != h.role {
			return err
		}
		place := v.FieldByIndex(h.outer.memberPlace.index).String()
		person := v.FieldByIndex(h.outer.memberPerson.index).String()
		inside, err := c.tx.Documents(a.store.Collection(h.inner.scope.collection).Where(h.pointing.name, "==", place)).GetAll()
		if err != nil {
			return err
		}
		for _, d := range inside {
			if err := a.giveHeld(c, h, d.Ref.ID, person, id); err != nil {
				return err
			}
		}
	case "delete":
		return a.dropHeld(c, h, h.through.name, id)
	}
	return nil
}

// An inner scope made within an outer one gives the outer's holders their held role
// in it; moved to another, what the first gave goes and the second's comes.
func (a *App) inheritScope(c *Ctx, h heldRole, action, id string, v reflect.Value, before map[string]any) error {
	now := v.FieldByIndex(h.pointing.index).String()
	was, _ := before[h.pointing.name].(string)
	if (action == "create" || action == "update") && now != "" && now != was {
		// Only whoever holds the role there brings something into it, as only an
		// organization's owner brings a project in; what the system moves, as a
		// migration does, it moves.
		if err := a.mayBringIn(c, h, now); err != nil {
			return err
		}
	}
	switch action {
	case "create":
		return a.giveHolders(c, h, now, id)
	case "update":
		if was == now {
			return nil
		}
		given, err := c.tx.Documents(a.store.Collection(h.inner.member.collection).Where(h.inner.memberPlace.name, "==", id)).GetAll()
		if err != nil {
			return err
		}
		for _, d := range given {
			if through, _ := d.Data()[h.through.name].(string); through != "" {
				c.drop(h.inner.member, d)
			}
		}
		return a.giveHolders(c, h, now, id)
	}
	return nil // a scope deleted takes its members with it
}

// mayBringIn says whether whoever runs the command holds the role in the outer place.
func (a *App) mayBringIn(c *Ctx, h heldRole, outer string) error {
	if c.me == "" {
		return nil
	}
	members, err := c.tx.Documents(a.store.Collection(h.outer.member.collection).Where(h.outer.memberPlace.name, "==", outer).Where(h.outer.memberPerson.name, "==", c.me)).GetAll()
	if err != nil {
		return err
	}
	for _, m := range members {
		roleID, _ := m.Data()[h.outer.memberRole.name].(string)
		if role, err := a.roleName(c, h.outer, roleID); err == nil && role == h.role {
			return nil
		}
	}
	return invalid("only %s %s of that %s brings a %s into it", article(h.role), h.role, h.outer.scope.name, h.inner.scope.name)
}

// giveHolders gives each holder of the outer place the held role in one inner scope.
func (a *App) giveHolders(c *Ctx, h heldRole, outer, inner string) error {
	if outer == "" {
		return nil
	}
	members, err := c.tx.Documents(a.store.Collection(h.outer.member.collection).Where(h.outer.memberPlace.name, "==", outer)).GetAll()
	if err != nil {
		return err
	}
	for _, m := range members {
		roleID, _ := m.Data()[h.outer.memberRole.name].(string)
		role, err := a.roleName(c, h.outer, roleID)
		if err != nil {
			return err
		}
		if role != h.role {
			continue
		}
		person, _ := m.Data()[h.outer.memberPerson.name].(string)
		if err := a.giveHeld(c, h, inner, person, m.Ref.ID); err != nil {
			return err
		}
	}
	return nil
}

// giveHeld makes the member that gives a person the held role in one inner scope,
// marked with the outer membership it comes from, unless they hold it there already.
func (a *App) giveHeld(c *Ctx, h heldRole, place, person, through string) error {
	roleID, err := a.roleID(h.inner, place, h.held)
	if err != nil {
		return err
	}
	m := reflect.New(h.inner.member.typ).Elem()
	m.FieldByIndex(h.inner.memberPlace.index).SetString(place)
	m.FieldByIndex(h.inner.memberPerson.index).SetString(person)
	m.FieldByIndex(h.inner.memberRole.index).SetString(roleID)
	if key, ok := h.inner.member.id(m); ok {
		ref := a.store.Collection(h.inner.member.collection).Doc(key)
		if _, err := c.tx.Get(ref); err == nil {
			return nil // they hold it themselves
		}
		// Or are given it themselves in this same step, like whoever makes a project.
		for _, list := range [][]*made{c.made, c.making} {
			for _, m := range list {
				if m.ref.Path == ref.Path {
					return nil
				}
			}
		}
	}
	m.FieldByIndex(h.through.index).SetString(through)
	return create(c, h.inner.member, m, nil)
}

// dropHeld takes away the inner members an outer membership gave.
func (a *App) dropHeld(c *Ctx, h heldRole, field, value string) error {
	docs, err := c.tx.Documents(a.store.Collection(h.inner.member.collection).Where(field, "==", value)).GetAll()
	if err != nil {
		return err
	}
	for _, d := range docs {
		c.drop(h.inner.member, d)
	}
	return nil
}

// a or an, before a word.
func article(word string) string {
	if word != "" && strings.ContainsRune("aeiou", rune(word[0])) {
		return "an"
	}
	return "a"
}
