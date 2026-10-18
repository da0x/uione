// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"reflect"
	"strings"
	"time"

	"cloud.google.com/go/firestore"
)

// How long a suspension can be undone. After that it's final: there's nothing left to
// restore, and the record of what they held goes.
const suspendedFor = 90 * 24 * time.Hour

// Suspends says how a person is suspended in a place rather than removed: their
// memberships there, and in what's within it, like an organization's projects, are
// set aside on a record of entity S, with the place and the person as its keys, when
// it lasts until, and what they held. Set aside, they're in no list of who holds
// what, so they hold nothing anywhere, through anything, and take no seat. A
// member's suspend command does it; the record's restore command undoes it.
func (r *RolesSpec) Suspends(s Kind) *RolesSpec {
	r.suspension = s.typ
	return r
}

// The fields a suspension is found by: the place, the person, until and what's held.
type suspensionFields struct {
	schema                     *schema
	place, person, until, held *field
}

func (a *App) suspensionOf(r *RolesSpec) *suspensionFields {
	if r.suspension == nil {
		return nil
	}
	s := a.reg.schemas[r.suspension]
	f := &suspensionFields{schema: s, until: s.field("until"), held: s.field("held")}
	for i := range s.fields {
		switch g := &s.fields[i]; g.refers {
		case a.reg.schemas[r.scope].entity:
			f.place = g
		case "user":
			f.person = g
		}
	}
	if f.place == nil || f.person == nil || f.until == nil || f.held == nil {
		panic("one: a suspension of " + s.name + " needs fields pointing at the place and the person, until, and held")
	}
	return f
}

// suspending says what a command suspends or restores, if it does: a define role's
// member being suspended, or a suspension being restored.
func (a *App) suspending(s *schema, action string) (*RolesSpec, bool) {
	for _, r := range a.reg.defined {
		if r.suspension == nil {
			continue
		}
		if action == "suspend" && a.reg.schemas[r.member] == s || action == "restore" && a.reg.schemas[r.suspension] == s {
			return r, true
		}
	}
	return nil, false
}

// One of a person's memberships, as a suspension keeps it: the namespace whose roles
// it is, the place, the role, and what gave it. None has a slash in it.
func heldText(ns, place, role, through string) string {
	return strings.Join([]string{ns, place, role, through}, "/")
}

// The roles a place's own are within, like an organization's projects: each
// namespace whose places point at it, with the field they point by.
func (a *App) placesWithin(r *RolesSpec) []*RolesSpec {
	outer := a.reg.schemas[r.scope]
	var out []*RolesSpec
	for _, other := range a.reg.defined {
		if other == r {
			continue
		}
		for _, f := range a.reg.schemas[other.scope].fields {
			if f.refers == outer.entity {
				out = append(out, other)
				break
			}
		}
	}
	return out
}

// pointsAt is the field of an inner place pointing at an outer one, like a
// project's organization.
func pointsAt(inner *schema, outer *schema) *field {
	for i := range inner.fields {
		if inner.fields[i].refers == outer.entity {
			return &inner.fields[i]
		}
	}
	return nil
}

// suspend sets aside a person's memberships in the member's place and in what's
// within it, each taken away as removing it would, so what it gave goes too and a
// place keeps the roles it keeps, and keeps them on a suspension record.
func (a *App) suspend(c *Ctx, r *RolesSpec, member reflect.Value) error {
	f, err := a.rolesFields(r)
	if err != nil {
		return err
	}
	sf := a.suspensionOf(r)
	place := member.FieldByIndex(f.memberPlace.index).String()
	person := member.FieldByIndex(f.memberPerson.index).String()
	type taken struct {
		schema *schema
		doc    *firestore.DocumentSnapshot
	}
	var own, given []taken
	var held []string
	// The person's memberships in a place: their own, and those something gave.
	collect := func(spec *RolesSpec, g *rolesFields, places map[string]bool) error {
		docs, err := c.tx.Documents(a.store.Collection(g.member.collection).Where(g.memberPerson.name, "==", person)).GetAll()
		if err != nil {
			return err
		}
		for _, d := range docs {
			at, _ := d.Data()[g.memberPlace.name].(string)
			if !places[at] {
				continue
			}
			role, _ := d.Data()[g.memberRole.name].(string)
			through, _ := d.Data()["through"].(string)
			if through == "" {
				own = append(own, taken{g.member, d})
				held = append(held, heldText(spec.ns, at, role, ""))
			} else {
				given = append(given, taken{g.member, d})
			}
		}
		return nil
	}
	if err := collect(r, f, map[string]bool{place: true}); err != nil {
		return err
	}
	for _, inner := range a.placesWithin(r) {
		g, err := a.rolesFields(inner)
		if err != nil {
			return err
		}
		pointing := pointsAt(g.scope, f.scope)
		inside, err := c.tx.Documents(a.store.Collection(g.scope.collection).Where(pointing.name, "==", place)).GetAll()
		if err != nil {
			return err
		}
		places := map[string]bool{}
		for _, d := range inside {
			places[d.Ref.ID] = true
		}
		if err := collect(inner, g, places); err != nil {
			return err
		}
	}
	if c.leaving == nil {
		c.leaving = map[string]bool{}
	}
	for _, t := range append(own, given...) {
		c.leaving[t.doc.Ref.Path] = true
	}
	// What was given them goes first, so nothing taking away their own gives it again:
	// kept to give back when something outside gave it, and otherwise given again by
	// their own once restored.
	for _, t := range given {
		through, _ := t.doc.Data()["through"].(string)
		if !c.leavingID(through) {
			spec := a.specOfMember(t.schema)
			at, _ := t.doc.Data()[spec.memberPlace.name].(string)
			role, _ := t.doc.Data()[spec.memberRole.name].(string)
			held = append(held, heldText(spec.ns, at, role, through))
		}
		c.drop(t.schema, t.doc)
	}
	// Each of their own goes as removing it would: what it holds and gives goes with
	// it, and a place left without a role it keeps refuses.
	for _, t := range own {
		v := reflect.New(t.schema.typ).Elem()
		if err := t.doc.DataTo(v.Addr().Interface()); err != nil {
			return err
		}
		if c.deleting != t.doc.Ref.Path {
			c.drop(t.schema, t.doc)
		}
		if err := a.inherit(c, t.schema, "delete", t.doc.Ref.ID, v, t.doc.Data()); err != nil {
			return err
		}
	}
	v := reflect.New(sf.schema.typ).Elem()
	v.FieldByIndex(sf.place.index).SetString(place)
	v.FieldByIndex(sf.person.index).SetString(person)
	v.FieldByIndex(sf.until.index).Set(reflect.ValueOf(c.now.Add(suspendedFor)))
	v.FieldByIndex(sf.held.index).Set(reflect.ValueOf(held))
	return create(c, sf.schema, v, nil)
}

// leavingID says whether a record of that id goes in this command, like the
// membership that gave someone a project's role.
func (c *Ctx) leavingID(id string) bool {
	if id == "" {
		return false
	}
	for path := range c.leaving {
		if strings.HasSuffix(path, "/"+id) {
			return true
		}
	}
	return false
}

// A member's roles' fields, with the namespace they're in.
type memberSpec struct {
	*rolesFields
	ns string
}

func (a *App) specOfMember(s *schema) memberSpec {
	for _, r := range a.reg.defined {
		if a.reg.schemas[r.member] == s {
			f, _ := a.rolesFields(r)
			return memberSpec{f, r.ns}
		}
	}
	return memberSpec{}
}

// restore gives back what a suspension set aside, unless it's over 90 days old: their
// own memberships first, within the place and then in it, which give again what they
// gave, and then whatever something outside gave them that still does.
func (a *App) restore(c *Ctx, r *RolesSpec, suspension reflect.Value) error {
	sf := a.suspensionOf(r)
	if until, _ := suspension.FieldByIndex(sf.until.index).Interface().(time.Time); !until.After(c.now) {
		return invalid("they were suspended more than 90 days ago, so it's final")
	}
	person := suspension.FieldByIndex(sf.person.index).String()
	held, _ := suspension.FieldByIndex(sf.held.index).Interface().([]string)
	type back struct {
		spec                 *RolesSpec
		place, role, through string
	}
	var inner, outer, given []back
	for _, text := range held {
		parts := strings.Split(text, "/")
		if len(parts) != 4 {
			continue
		}
		var spec *RolesSpec
		for _, d := range a.reg.defined {
			if d.ns == parts[0] {
				spec = d
			}
		}
		if spec == nil {
			continue // roles no longer there hold nothing
		}
		b := back{spec, parts[1], parts[2], parts[3]}
		switch {
		case b.through != "":
			given = append(given, b)
		case spec == r:
			outer = append(outer, b)
		default:
			inner = append(inner, b)
		}
	}
	for _, list := range [][]back{inner, outer, given} {
		for _, b := range list {
			f, err := a.rolesFields(b.spec)
			if err != nil {
				return err
			}
			// A place or a role gone since, or what gave it gone, gives nothing back.
			if _, err := c.tx.Get(a.store.Collection(f.scope.collection).Doc(b.place)); err != nil {
				continue
			}
			if _, err := c.tx.Get(a.store.Collection(f.role.collection).Doc(b.role)); err != nil {
				continue
			}
			v := reflect.New(f.member.typ).Elem()
			v.FieldByIndex(f.memberPlace.index).SetString(b.place)
			v.FieldByIndex(f.memberPerson.index).SetString(person)
			v.FieldByIndex(f.memberRole.index).SetString(b.role)
			if key, ok := f.member.id(v); ok && b.through != "" {
				if _, err := c.tx.Get(a.store.Collection(f.member.collection).Doc(key)); err == nil || c.makingPath(a.store.Collection(f.member.collection).Doc(key).Path) {
					continue // given again already, by what was restored
				}
				if !a.anyRecord(c, b.through) {
					continue
				}
				if through := f.member.field("through"); through != nil {
					v.FieldByIndex(through.index).SetString(b.through)
				}
			}
			if err := create(c, f.member, v, nil); err != nil {
				return err
			}
		}
	}
	return nil
}

// makingPath says whether the command makes something at that path.
func (c *Ctx) makingPath(path string) bool {
	for _, list := range [][]*made{c.made, c.making} {
		for _, m := range list {
			if m.ref.Path == path {
				return true
			}
		}
	}
	return false
}

// anyRecord says whether what gave someone a role, a membership or a record like a
// team's role in a project, is still there.
func (a *App) anyRecord(c *Ctx, id string) bool {
	var kinds []*schema
	for _, r := range a.reg.defined {
		kinds = append(kinds, a.reg.schemas[r.member])
	}
	for _, g := range a.reg.gives {
		kinds = append(kinds, a.reg.schemas[g.typ])
	}
	for _, s := range kinds {
		if _, err := c.tx.Get(a.store.Collection(s.collection).Doc(id)); err == nil {
			return true
		}
	}
	return false
}

// notSuspended refuses a membership made for someone suspended in its place, or in
// the place it's within, like an organization's project: they're restored instead. A
// suspension over 90 days old is final, and goes, so they may be given roles anew.
func (a *App) notSuspended(c *Ctx, s *schema, v reflect.Value) error {
	for _, r := range a.reg.defined {
		if a.reg.schemas[r.member] != s {
			continue
		}
		f, err := a.rolesFields(r)
		if err != nil {
			return err
		}
		place := v.FieldByIndex(f.memberPlace.index).String()
		person := v.FieldByIndex(f.memberPerson.index).String()
		check := func(outer *RolesSpec, at string) error {
			sf := a.suspensionOf(outer)
			if sf == nil || at == "" {
				return nil
			}
			k := reflect.New(sf.schema.typ).Elem()
			k.FieldByIndex(sf.place.index).SetString(at)
			k.FieldByIndex(sf.person.index).SetString(person)
			key, ok := sf.schema.id(k)
			if !ok {
				return nil
			}
			ref := a.store.Collection(sf.schema.collection).Doc(key)
			snap, err := c.tx.Get(ref)
			if err != nil || c.leaving[ref.Path] || c.deleting == ref.Path {
				return nil
			}
			if until, _ := snap.Data()[sf.until.name].(time.Time); until.After(c.now) {
				return invalid("they're suspended here; restore them instead")
			}
			c.drop(sf.schema, snap)
			return nil
		}
		if err := check(r, place); err != nil {
			return err
		}
		// The places this one is within, whose suspensions reach it.
		for _, outer := range a.reg.defined {
			if outer == r || outer.suspension == nil {
				continue
			}
			pointing := pointsAt(f.scope, a.reg.schemas[outer.scope])
			if pointing == nil {
				continue
			}
			snap, err := c.tx.Get(a.store.Collection(f.scope.collection).Doc(place))
			if err != nil {
				continue
			}
			at, _ := snap.Data()[pointing.name].(string)
			if err := check(outer, at); err != nil {
				return err
			}
		}
	}
	return nil
}
