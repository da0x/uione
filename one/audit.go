// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"reflect"
	"slices"
	"time"
)

// An audit of a place, like an organization: a record of every command run in it
// and in what's within it, like its projects' issues, written in the command's own
// transaction, so nothing done goes unrecorded and nothing recorded wasn't done. No
// command changes or deletes one; each goes by itself once it's older than kept.
type auditing struct {
	typ  reflect.Type
	kept time.Duration
}

// Days is a number of days, as long as an audit is kept: one.Days(365).
func Days(n int) time.Duration { return time.Duration(n) * 24 * time.Hour }

// Audits keeps an audit of each place on records of entity A, for as long as kept:
// one points at the place, and says the command, who ran it and from where, what it
// ran on, the fields it changed and how, and until when it's kept.
func (r *RolesSpec) Audits(a Kind, kept time.Duration) *RolesSpec {
	r.audit = &auditing{typ: a.typ, kept: kept}
	return r
}

// The fields an audit record is written to.
type auditFields struct {
	schema                                *schema
	place, command, person, address, kind *field
	item, changed, changes, until         *field
}

func (a *App) auditOf(r *RolesSpec) *auditFields {
	s := a.reg.schemas[r.audit.typ]
	f := &auditFields{schema: s, command: s.field("command"), person: s.field("person"), address: s.field("address"), kind: s.field("kind"),
		item: s.field("item"), changed: s.field("changed"), changes: s.field("changes"), until: s.field("until")}
	for i := range s.fields {
		if s.fields[i].refers == a.reg.schemas[r.scope].entity {
			f.place = &s.fields[i]
		}
	}
	for _, g := range []*field{f.place, f.command, f.person, f.address, f.kind, f.item, f.changed, f.changes, f.until} {
		if g == nil {
			panic("one: an audit of " + a.reg.schemas[r.scope].name + " needs fields pointing at it, and command, person, address, kind, item, changed, changes and until")
		}
	}
	return f
}

// audited says whether any place keeps an audit, and isAudit whether a kind of
// record is one, which no command runs on.
func (a *App) audited() bool {
	for _, r := range a.reg.defined {
		if r.audit != nil {
			return true
		}
	}
	return false
}

func (a *App) isAudit(s *schema) bool {
	for _, r := range a.reg.defined {
		if r.audit != nil && a.reg.schemas[r.audit.typ] == s {
			return true
		}
	}
	return false
}

// The place of each audit a record is in: the place it's held within, or the one
// that place is within, like an issue's project's organization.
func (a *App) auditedIn(c *Ctx, o *owned) (map[*RolesSpec]string, error) {
	out := map[*RolesSpec]string{}
	for _, r := range a.reg.defined {
		if r.audit == nil {
			continue
		}
		scope := a.reg.schemas[r.scope]
		place, err := a.within(c.tx, o, scope)
		if err != nil {
			return nil, err
		}
		for _, inner := range a.placesWithin(r) {
			if place != "" {
				break
			}
			innerScope := a.reg.schemas[inner.scope]
			at, err := a.within(c.tx, o, innerScope)
			if err != nil {
				return nil, err
			}
			if at == "" {
				continue
			}
			if o.schema == innerScope {
				place, _ = o.value.FieldByIndex(pointsAt(innerScope, scope).index).Interface().(string)
				break
			}
			snap, err := c.tx.Get(a.store.Collection(innerScope.collection).Doc(at))
			if err != nil {
				continue // made in this same command, or gone
			}
			place, _ = snap.Data()[pointsAt(innerScope, scope).name].(string)
		}
		if place != "" {
			out[r] = place
		}
	}
	return out, nil
}

// audit writes, with the command's other changes, a record of it in the audit of
// each place its entity is in: before is nil for something made, after nil for
// something deleted.
func (a *App) audit(c *Ctx, s *schema, id string, v reflect.Value, before, after map[string]any) error {
	if !a.audited() {
		return nil
	}
	places, err := a.auditedIn(c, &owned{s, v})
	if err != nil || len(places) == 0 {
		return err
	}
	// What changed, field by field, leaving out when and by whom, which the record says.
	changes := map[string]any{}
	var names []string
	for _, f := range s.fields {
		was, now := before[f.name], after[f.name]
		if alike(was, now) {
			continue
		}
		names = append(names, f.name)
		changes[f.name] = map[string]any{"before": was, "after": now}
	}
	slices.Sort(names)
	if names == nil {
		names = []string{}
	}
	for r, place := range places {
		f := a.auditOf(r)
		rec := reflect.New(f.schema.typ).Elem()
		rec.FieldByIndex(f.place.index).SetString(place)
		rec.FieldByIndex(f.command.index).SetString(c.command)
		rec.FieldByIndex(f.person.index).SetString(c.me)
		rec.FieldByIndex(f.address.index).SetString(c.address)
		rec.FieldByIndex(f.kind.index).SetString(s.entity)
		rec.FieldByIndex(f.item.index).SetString(id)
		rec.FieldByIndex(f.changed.index).Set(reflect.ValueOf(names))
		rec.FieldByIndex(f.changes.index).Set(reflect.ValueOf(Data(changes)))
		rec.FieldByIndex(f.until.index).Set(reflect.ValueOf(c.now.Add(r.audit.kept)))
		ref := a.store.Collection(f.schema.collection).NewDoc()
		record := rec.Addr().Interface().(interface{ record() *Record }).record()
		record.ID, record.CreatedAt, record.CreatedBy, record.UpdatedAt, record.UpdatedBy = ref.ID, c.now, c.me, c.now, c.me
		data := f.schema.data(rec)
		c.writes = append(c.writes, write{ref: ref, data: data, event: &event{Type: f.schema.name + ".updated", Entity: f.schema.entity, ID: ref.ID, After: data}})
	}
	return nil
}
