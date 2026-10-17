// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"context"
	"os"
	"testing"
)

type tray struct {
	Record
	Name string `firestore:"name" one:"required,key"`
}

type slip struct {
	Record
	Tray   string         `firestore:"tray" one:"refers=desk::tray"`
	Title  string         `firestore:"title"`
	Report map[string]any `firestore:"report"`
}

// A change rebuilds only the pages that read what changed: a slip's report, which
// its tray's list doesn't show, rebuilds the slip's own page and not the tray's, as
// an agent's progress rebuilds an issue's page and not its project's.
func TestAChangeRebuildsOnlyThePagesThatReadWhatChanged(t *testing.T) {
	if os.Getenv("FIRESTORE_EMULATOR_HOST") == "" {
		os.Setenv("FIRESTORE_EMULATOR_HOST", "localhost:8080")
		os.Setenv("FIREBASE_AUTH_EMULATOR_HOST", "localhost:9099")
	}
	ctx := context.Background()
	a, err := New(ctx, Module("desk",
		Command[tray]("tray::create"), Command[slip]("slip::create"),
		View("tray_page").Per(Entity[tray]()).Public().List("slips", Where[slip]("tray", Subject)).Fields("title"),
		View("slip_page").Per(Entity[slip]()).Public().Copy("title", "title"),
	))
	if err != nil {
		t.Fatal(err)
	}
	defer a.Close()
	slipChanged := func(before, after map[string]any) event {
		return event{Type: "slip.updated", Entity: "desk::slip", ID: "s1", Before: before, After: after}
	}
	rebuilds := func(events ...event) int64 {
		before := a.rebuilt.Load()
		a.publish(ctx, events...)
		return a.rebuilt.Load() - before
	}
	was := map[string]any{"tray": "t1", "title": "Pens", "report": map[string]any{"done": 1.0}}

	// Its report: the slip's page alone.
	if n := rebuilds(slipChanged(was, map[string]any{"tray": "t1", "title": "Pens", "report": map[string]any{"done": 2.0}})); n != 1 {
		t.Errorf("a slip's report rebuilt %d pages, not its own page alone", n)
	}
	// Its title, which the tray's list shows: both.
	if n := rebuilds(slipChanged(was, map[string]any{"tray": "t1", "title": "Pencils", "report": map[string]any{"done": 1.0}})); n != 2 {
		t.Errorf("a slip's title rebuilt %d pages, not its own and its tray's", n)
	}
	// Moved to another tray: its page, and both trays'.
	if n := rebuilds(slipChanged(was, map[string]any{"tray": "t2", "title": "Pens", "report": map[string]any{"done": 1.0}})); n != 3 {
		t.Errorf("a slip moved rebuilt %d pages, not its own and both trays'", n)
	}
	// Made, and deleted, whatever it holds: both.
	if n := rebuilds(slipChanged(nil, was)); n != 2 {
		t.Errorf("a slip made rebuilt %d pages, not 2", n)
	}
	if n := rebuilds(slipChanged(was, nil)); n != 2 {
		t.Errorf("a slip deleted rebuilt %d pages, not 2", n)
	}
}
