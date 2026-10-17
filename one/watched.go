// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"strings"
	"sync"
)

// watch is what of one entity a view's documents are made from: the fields its
// lists, counts and values name, or every field, when the entity is more to the view
// than rows it picks.
type watch struct {
	fields map[string]bool
	all    bool
}

// watching keeps each view's watches, worked out the first time a change asks.
var watching sync.Map // *ViewSpec -> map[string]*watch

// watches works out, for each entity a view reads, the fields a change to it has to
// touch to change what the view shows. Rows a list, count or value picks are watched
// by the fields that pick, order and show them, and by every field pointing at
// another entity, since those say where a row belongs. Everything else the view
// reads, like the entity a page is for, what it's held within, who may read it, and
// what it reads through a field like stage.title, is watched whole.
func (v *ViewSpec) watches(r *registry) map[string]*watch {
	if known, ok := watching.Load(v); ok {
		return known.(map[string]*watch)
	}
	out := map[string]*watch{}
	of := func(q Query) *watch {
		s := r.schemas[q.typ]
		if s == nil {
			return nil
		}
		w := out[s.entity]
		if w == nil {
			w = &watch{fields: map[string]bool{}}
			out[s.entity] = w
			for _, f := range s.fields {
				if f.refers != "" {
					w.fields[f.name] = true
				}
			}
		}
		return w
	}
	// A field through another, like stage.title, is the field stage here.
	add := func(w *watch, field string) {
		if w == nil {
			return
		}
		if head, _, through := strings.Cut(field, "."); through {
			field = head
		}
		w.fields[field] = true
	}
	picked := func(q Query) *watch {
		var first *watch
		for _, way := range q.ways() {
			w := of(way)
			if first == nil {
				first = w
			}
			if w == nil {
				continue
			}
			for _, c := range way.conditions {
				if c.gate {
					w.all = true
					continue
				}
				add(w, c.field)
			}
		}
		return first
	}
	for _, c := range v.counts {
		picked(c.query)
	}
	for _, f := range v.firsts {
		add(picked(f.query), f.field)
	}
	for _, l := range v.lists {
		w := picked(l.query)
		if w == nil {
			continue
		}
		// Rows kept to those a page lets the reader see are watched whole: what that
		// page reads of them isn't said here.
		if len(l.fields) == 0 || len(l.guards) > 0 {
			w.all = true
		}
		for _, f := range l.fields {
			add(w, f)
		}
		for _, o := range l.order {
			add(w, o.field)
		}
		for _, x := range l.values {
			add(picked(x.query), x.field)
		}
		for _, t := range v.tallies {
			if t.list == l.name {
				for _, test := range t.tests {
					add(w, test.field)
				}
			}
		}
	}
	// What a page is for, held within, read by, or reads through, whole.
	for entity := range v.reads {
		w := out[entity]
		if w == nil {
			out[entity] = &watch{all: true}
			continue
		}
		for _, s := range []*schema{v.scope, r.schemas[v.per], r.schemas[v.readers]} {
			if s != nil && s.entity == entity {
				w.all = true
			}
		}
		if _, opens := v.openers[entity]; opens {
			w.all = true
		}
	}
	known, _ := watching.LoadOrStore(v, out)
	return known.(map[string]*watch)
}

// touches says whether a change can change what a view shows: anything made or
// deleted can, and a change to a record only through a field the view watches.
func (v *ViewSpec) touches(r *registry, ev event) bool {
	if ev.Before == nil || ev.After == nil {
		return true
	}
	w := v.watches(r)[ev.Entity]
	if w == nil || w.all {
		return true
	}
	for name := range ev.Before {
		if w.fields[name] && !alike(ev.Before[name], ev.After[name]) {
			return true
		}
	}
	for name := range ev.After {
		if _, before := ev.Before[name]; !before && w.fields[name] && !alike(nil, ev.After[name]) {
			return true
		}
	}
	return false
}
