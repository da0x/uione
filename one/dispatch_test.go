// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"context"
	"net/http"
	"testing"

	"github.com/da0x/uione/one"
)

// Trails of stops, and paths between stops that each make their other side: every
// change one command makes to another record is that record's command, dispatched.

type Trail struct {
	one.Record
	Title string `firestore:"title" one:"required"`
	Start string `firestore:"start" one:"refers=routes::stop"`
}

type Stop struct {
	one.Record
	Trail string `firestore:"trail" one:"required,refers=routes::trail"`
	Name  string `firestore:"name" one:"required"`
	Note  string `firestore:"note"`
}

type Path struct {
	one.Record
	From string `firestore:"from" one:"required,refers=routes::stop"`
	To   string `firestore:"to" one:"required,refers=routes::stop"`
	Pair string `firestore:"pair" one:"refers=routes::path"`
}

// A stop's own rules: one called closed can't be made, and one renamed says what
// it was called before.
func stopCreate(c *one.Ctx, s *Stop) error {
	if s.Name == "closed" {
		return c.Fail("that stop is closed")
	}
	return nil
}

func stopUpdate(c *one.Ctx, s *Stop) error {
	if was, _ := c.Was("name").(string); was != s.Name {
		s.Note = "was " + was
	}
	return nil
}

// A path makes its other side, which, having a pair, makes nothing more; deleting
// either deletes the other, which deletes the first back, and that does nothing.
func pathCreate(c *one.Ctx, p *Path) error {
	if p.Pair != "" {
		return nil
	}
	return one.DispatchCreate(c, &Path{From: p.To, To: p.From, Pair: p.ID}, nil, pathCreate)
}

// How many times a path's delete body has run, each path's once.
var pathDeletes int

func pathDelete(c *one.Ctx, p *Path) error {
	pathDeletes++
	if err := deleteOther(c, p.Pair); err != nil {
		return err
	}
	return deleteOther(c, otherOf(c, p))
}

// The side a path was made with names it as its pair, so it's found by the field.
func otherOf(c *one.Ctx, p *Path) string {
	found := ""
	_ = one.EachIn(c, "pair", p.ID, func(other *Path) error {
		found = other.ID
		return nil
	})
	return found
}

func deleteOther(c *one.Ctx, id string) error {
	if id == "" {
		return nil
	}
	return one.DispatchDelete[Path](c, id, nil, pathDelete)
}

var routes = one.Module("routes",
	one.Command[Trail]("trail::create").Allow(one.Anyone).Do(func(c *one.Ctx, t *Trail) error {
		start := &Stop{Trail: t.ID, Name: t.Title}
		if err := one.DispatchCreate(c, start, nil, stopCreate); err != nil {
			return err
		}
		t.Start = start.ID
		return nil
	}),
	// Renaming a trail renames its first stop.
	one.Command[Trail]("trail::update").Allow(one.Anyone).Do(func(c *one.Ctx, t *Trail) error {
		return one.DispatchUpdate(c, t.Start, func(s *Stop) { s.Name = t.Title }, nil, stopUpdate)
	}),
	one.Command[Stop]("stop::create").Allow(one.Anyone).Do(stopCreate),
	one.Command[Stop]("stop::update").Allow(one.Anyone).Do(stopUpdate),
	// A stop goes with the paths from it, each deleting its other side.
	one.Command[Stop]("stop::delete").Allow(one.Anyone).Do(func(c *one.Ctx, s *Stop) error {
		return one.EachIn(c, "from", s.ID, func(p *Path) error {
			return one.DispatchDelete[Path](c, p.ID, nil, pathDelete)
		})
	}),
	one.Command[Path]("path::create").Allow(one.Anyone).Do(pathCreate),
	one.Command[Path]("path::delete").Allow(one.Anyone).Do(pathDelete),
)

func init() { modules = append(modules, routes) }

func (h *harness) count(collection string) int {
	h.t.Helper()
	docs, err := h.store.Collection(collection).Documents(context.Background()).GetAll()
	if err != nil {
		h.t.Fatal(err)
	}
	return len(docs)
}

func TestADispatchedCreateRunsItsCommandsBodyInTheSameStep(t *testing.T) {
	h := start(t)
	trail := h.mustRun("routes/trail/create", "", map[string]any{"title": "ridge"})
	stored := h.stored("routes_trail", trail)
	start, _ := stored["start"].(string)
	if start == "" {
		t.Fatalf("the trail doesn't name the stop it made: %v", stored)
	}
	if stop := h.stored("routes_stop", start); stop["trail"] != trail || stop["name"] != "ridge" {
		t.Fatalf("the stop made with the trail is %v", stop)
	}

	// Its body says no, so neither is made.
	h.expect("routes/trail/create", "", map[string]any{"title": "closed"}, http.StatusBadRequest, "that stop is closed")
	if trails, stops := h.count("routes_trail"), h.count("routes_stop"); trails != 1 || stops != 1 {
		t.Fatalf("a refused dispatch left %d trails and %d stops", trails, stops)
	}
}

func TestADispatchedUpdateKnowsWhatItsOwnRecordWas(t *testing.T) {
	h := start(t)
	trail := h.mustRun("routes/trail/create", "", map[string]any{"title": "ridge"})
	h.mustRun("routes/trail/update", "", map[string]any{"id": trail, "title": "summit"})
	start := h.stored("routes_trail", trail)["start"].(string)
	if stop := h.stored("routes_stop", start); stop["name"] != "summit" || stop["note"] != "was ridge" {
		t.Fatalf("the renamed stop is %v", stop)
	}
}

func TestDeletingWhatsBeingDeletedAlreadyDoesNothing(t *testing.T) {
	h := start(t)
	trail := h.mustRun("routes/trail/create", "", map[string]any{"title": "ridge"})
	a := h.stored("routes_trail", trail)["start"].(string)
	b := h.mustRun("routes/stop/create", "", map[string]any{"trail": trail, "name": "lake"})

	// A path makes its other side, and no more.
	path := h.mustRun("routes/path/create", "", map[string]any{"from": a, "to": b})
	if paths := h.count("routes_path"); paths != 2 {
		t.Fatalf("a path and its other side are %d paths", paths)
	}

	// Deleting one deletes the other, which deletes the first back, and stops there:
	// each one's body runs once.
	pathDeletes = 0
	h.mustRun("routes/path/delete", "", map[string]any{"id": path})
	if paths := h.count("routes_path"); paths != 0 || pathDeletes != 2 {
		t.Fatalf("after deleting a path, %d are left, and its bodies ran %d times", paths, pathDeletes)
	}

	// Deleting a stop deletes the paths from it, and each one's other side.
	h.mustRun("routes/path/create", "", map[string]any{"from": a, "to": b})
	h.mustRun("routes/stop/delete", "", map[string]any{"id": a})
	if paths, stops := h.count("routes_path"), h.count("routes_stop"); paths != 0 || stops != 1 {
		t.Fatalf("after deleting a stop, %d paths and %d stops are left", paths, stops)
	}
}
