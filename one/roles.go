// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"context"
	"fmt"
	"reflect"
	"strings"
	"time"

	"cloud.google.com/go/firestore"
)

// RolesSpec is the roles each entity of a kind, like a project, defines for itself,
// made with Roles: records its people edit, each saying what it allows, rather than
// roles written in code.
type RolesSpec struct {
	role, scope, member reflect.Type
	allows              string // the role's list of what it allows, like may
	defaults            []defaultRole
	ns                  string
}

type defaultRole struct {
	name, title string
	permissions []string // as the language writes them, like issue::create
}

// Roles declares the roles each entity of a kind defines for itself, as records of
// role: one points at the entity, like a project, has a name and a title, and lists
// in its allows field the commands it allows, written like issue::create. A member
// that points at the project, a person and a role gives that person what the role
// allows, on the project and on everything held within it, like its issues.
//
// Each project starts with the roles Default gives it, made with it. A project
// made before its roles were records gets them when the backend starts, and its
// members' roles, named as the defaults are, are pointed at them.
func Roles(role, scope, member Kind, allows string) *RolesSpec {
	return &RolesSpec{role: role.typ, scope: scope.typ, member: member.typ, allows: allows}
}

// Default adds a role every project starts with: its name, its title, and what it
// allows.
func (r *RolesSpec) Default(name, title string, permissions ...string) *RolesSpec {
	r.defaults = append(r.defaults, defaultRole{name, title, permissions})
	return r
}

func (r *RolesSpec) register(reg *registry, ns string) {
	r.ns = ns
	reg.schema(r.role, ns)
	reg.schema(r.scope, ns)
	reg.schema(r.member, ns)
	reg.defined = append(reg.defined, r)
}

// The fields roles are found by: the role's place, name, title and what it allows,
// and the member's place, person and role.
type rolesFields struct {
	role, scope, member                   *schema
	place, name, title, allows            *field
	memberPlace, memberPerson, memberRole *field
}

func (a *App) rolesFields(r *RolesSpec) (*rolesFields, error) {
	f := &rolesFields{role: a.reg.schemas[r.role], scope: a.reg.schemas[r.scope], member: a.reg.schemas[r.member]}
	for i := range f.role.fields {
		switch g := &f.role.fields[i]; {
		case g.refers == f.scope.entity:
			f.place = g
		case g.name == "name":
			f.name = g
		case g.name == "title":
			f.title = g
		case g.name == r.allows:
			f.allows = g
		}
	}
	for i := range f.member.fields {
		switch g := &f.member.fields[i]; g.refers {
		case f.scope.entity:
			f.memberPlace = g
		case "user":
			f.memberPerson = g
		case f.role.entity:
			f.memberRole = g
		}
	}
	if f.place == nil || f.name == nil || f.allows == nil {
		return nil, fmt.Errorf("one: roles of %s are %s records, which need a field pointing at %s, a name, and %s", f.scope.name, f.role.name, f.scope.name, r.allows)
	}
	if f.memberPlace == nil || f.memberPerson == nil || f.memberRole == nil {
		return nil, fmt.Errorf("one: roles of %s are given by %s, which needs fields pointing at %s, a person and a %s", f.scope.name, f.member.name, f.scope.name, f.role.name)
	}
	return f, nil
}

// permissionOf is what a role's record says it allows, like issue::create, as the
// permission a command needs, like projects::issue:create.
func permissionOf(ns, written string) Permission {
	at := strings.LastIndex(written, "::")
	if at < 0 {
		return Permission(written)
	}
	return qualified(ns, Permission(written[:at]+":"+written[at+2:]))
}

// roleID is the id of a project's role of a name: its keys, the project and the name.
func (a *App) roleID(f *rolesFields, place, name string) (string, error) {
	v := reflect.New(f.role.typ).Elem()
	v.FieldByIndex(f.place.index).SetString(place)
	v.FieldByIndex(f.name.index).SetString(name)
	id, ok := f.role.id(v)
	if !ok {
		return "", fmt.Errorf("one: a %s is named by its %s and its name, as its keys", f.role.name, f.scope.name)
	}
	return id, nil
}

// allowedByRoles says whether a role the person holds where the entity is held, as
// its records define it, allows the permission.
func (a *App) allowedByRoles(tx *firestore.Transaction, me string, p Permission, entity *owned) (bool, error) {
	for _, r := range a.reg.defined {
		f, err := a.rolesFields(r)
		if err != nil {
			return false, err
		}
		within, err := a.within(tx, entity, f.scope)
		if err != nil || within == "" {
			return false, err
		}
		query := a.store.Collection(f.member.collection).Where(f.memberPlace.name, "==", within).Where(f.memberPerson.name, "==", me).Limit(20)
		docs, err := tx.Documents(query).GetAll()
		if err != nil {
			return false, err
		}
		for _, doc := range docs {
			id, _ := doc.Data()[f.memberRole.name].(string)
			if id == "" || !validID(id) {
				continue
			}
			role, err := tx.Get(a.store.Collection(f.role.collection).Doc(id))
			if err != nil {
				continue // a role that's gone allows nothing
			}
			// Only a role of the same project counts.
			if place, _ := role.Data()[f.place.name].(string); place != within {
				continue
			}
			list, _ := role.Data()[f.allows.name].([]any)
			for _, item := range list {
				if written, ok := item.(string); ok && permissionOf(r.ns, written) == p {
					return true, nil
				}
			}
		}
	}
	return false, nil
}

// seedRoles gives a project its default roles in the transaction that makes it.
func (a *App) seedRoles(c *Ctx, scope *schema, place string) error {
	for _, r := range a.reg.defined {
		if a.reg.schemas[r.scope] != scope {
			continue
		}
		f, err := a.rolesFields(r)
		if err != nil {
			return err
		}
		for _, d := range r.defaults {
			// A role that's there already, as when a project is made again under the
			// same name, keeps what its people made of it.
			id, err := a.roleID(f, place, d.name)
			if err != nil {
				return err
			}
			if _, err := c.tx.Get(a.store.Collection(f.role.collection).Doc(id)); err == nil {
				continue
			}
			v := reflect.New(f.role.typ).Elem()
			v.FieldByIndex(f.place.index).SetString(place)
			v.FieldByIndex(f.name.index).SetString(d.name)
			if f.title != nil {
				v.FieldByIndex(f.title.index).SetString(d.title)
			}
			v.FieldByIndex(f.allows.index).Set(reflect.ValueOf(append([]string{}, d.permissions...)))
			if err := create(c, f.role, v); err != nil {
				return err
			}
		}
	}
	return nil
}

// seedEarlierRoles gives each project made before its roles were records the
// default ones, and points its members' roles, named as the defaults are, at them.
// A project with any role records is left as it is, so this happens once.
func (a *App) seedEarlierRoles(ctx context.Context) error {
	for _, r := range a.reg.defined {
		f, err := a.rolesFields(r)
		if err != nil {
			return err
		}
		places, err := a.store.Collection(f.scope.collection).DocumentRefs(ctx).GetAll()
		if err != nil {
			return err
		}
		for _, place := range places {
			var events []event
			err := a.store.RunTransaction(ctx, func(ctx context.Context, tx *firestore.Transaction) error {
				events = nil
				some, err := tx.Documents(a.store.Collection(f.role.collection).Where(f.place.name, "==", place.ID).Limit(1)).GetAll()
				if err != nil || len(some) > 0 {
					return err
				}
				members, err := tx.Documents(a.store.Collection(f.member.collection).Where(f.memberPlace.name, "==", place.ID)).GetAll()
				if err != nil {
					return err
				}
				c := &Ctx{Context: ctx, now: time.Now().UTC(), app: a, tx: tx, counters: map[string]*counting{}, command: f.scope.entity + "::roles"}
				if err := a.seedRoles(c, f.scope, place.ID); err != nil {
					return err
				}
				for _, m := range members {
					name, _ := m.Data()[f.memberRole.name].(string)
					for _, d := range r.defaults {
						if name != d.name {
							continue
						}
						id, err := a.roleID(f, place.ID, d.name)
						if err != nil {
							return err
						}
						v := reflect.New(f.member.typ).Elem()
						if err := m.DataTo(v.Addr().Interface()); err != nil {
							return err
						}
						before := f.member.data(v)
						v.FieldByIndex(f.memberRole.index).SetString(id)
						c.reads = append(c.reads, &read{schema: f.member, ref: m.Ref, value: v, stored: m.Data(), before: before})
					}
				}
				events, err = c.save()
				return err
			})
			if err != nil {
				return err
			}
			for _, ev := range events {
				a.publish(ctx, ev)
			}
		}
	}
	return nil
}
