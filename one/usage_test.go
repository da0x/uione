// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"context"
	"reflect"
	"testing"
	"time"

	"github.com/da0x/uione/one"
)

// What each person uses is kept for whoever runs the app: the commands they run, what
// those write, and what they keep now.

func TestEachPersonsCommandsAndWhatTheyKeepAreCounted(t *testing.T) {
	h := start(t)
	ada, adaToken := h.signUp("ada@example.com")
	grace, graceToken := h.signUp("grace@example.com")
	h.mustRun("guide/guide/create", adaToken, map[string]any{"slug": "kitchen"})
	h.mustRun("guide/leaf/create", adaToken, map[string]any{"guide": "kitchen", "title": "Coffee"})
	tea := h.mustRun("guide/leaf/create", adaToken, map[string]any{"guide": "kitchen", "title": "Tea"})
	h.mustRun("guide/leaf/delete", adaToken, map[string]any{"id": tea})
	h.mustRun("guide/guide/create", graceToken, map[string]any{"slug": "garden"})
	h.app.KeepUsage()

	month := time.Now().UTC().Format("2006-01")
	used := h.mustHave("usage", ada)
	// The guide and the writer it made, and one leaf of the two.
	if held := used["held"]; !reflect.DeepEqual(held, map[string]any{"guide_guide": int64(1), "guide_writer": int64(1), "guide_leaf": int64(1)}) {
		t.Errorf("ada keeps %v", held)
	}
	ran := used["months"].(map[string]any)[month].(map[string]any)
	if ran["commands"] != int64(4) {
		t.Errorf("ada ran %v commands this month, not 4", ran["commands"])
	}
	// At least each record, and the pages rebuilt for them.
	if writes, _ := ran["writes"].(int64); writes < 5 {
		t.Errorf("ada's commands wrote %d times", writes)
	}
	if active, _ := used["active"].(time.Time); time.Since(active) > time.Minute {
		t.Errorf("ada was last active %v", used["active"])
	}
	if held := h.mustHave("usage", grace)["held"]; !reflect.DeepEqual(held, map[string]any{"guide_guide": int64(1), "guide_writer": int64(1)}) {
		t.Errorf("grace keeps %v", held)
	}

	// Counted again, it adds to what's there.
	h.mustRun("guide/leaf/create", adaToken, map[string]any{"guide": "kitchen", "title": "Bread"})
	h.app.KeepUsage()
	used = h.mustHave("usage", ada)
	if n := used["held"].(map[string]any)["guide_leaf"]; n != int64(2) {
		t.Errorf("ada keeps %v leaves, not 2", n)
	}
	if n := used["months"].(map[string]any)[month].(map[string]any)["commands"]; n != int64(5) {
		t.Errorf("ada ran %v commands, not 5", n)
	}
}

func TestWhatWasKeptBeforeUsageWasCountedIsCountedOnce(t *testing.T) {
	h := start(t)
	ctx := context.Background()
	ada, adaToken := h.signUp("ada@example.com")
	h.mustRun("guide/guide/create", adaToken, map[string]any{"slug": "kitchen"})
	h.app.KeepUsage()
	// As a backend from before kept them: two leaves, and an upload.
	for _, id := range []string{"soup", "stew"} {
		if _, err := h.store.Collection("guide_leaf").Doc(id).Set(ctx, map[string]any{"guide": "kitchen", "title": id, "created_by": ada}); err != nil {
			t.Fatal(err)
		}
	}
	if _, err := h.store.Collection("files").Doc("menu").Set(ctx, map[string]any{"path": "files/menu.pdf", "uploaded_by": ada, "size": int64(2048)}); err != nil {
		t.Fatal(err)
	}
	if _, err := h.store.Collection("usage_definitions").Doc("held").Delete(ctx); err != nil {
		t.Fatal(err)
	}
	app, err := one.New(ctx, modules...)
	if err != nil {
		t.Fatal(err)
	}
	app.Close()
	used := h.mustHave("usage", ada)
	// What's stored, set whole, not added to what was counted before.
	if held := used["held"]; !reflect.DeepEqual(held, map[string]any{"guide_guide": int64(1), "guide_writer": int64(1), "guide_leaf": int64(2)}) {
		t.Errorf("ada keeps %v", held)
	}
	if used["bytes"] != int64(2048) {
		t.Errorf("ada's files hold %v bytes", used["bytes"])
	}
	if used["months"] == nil {
		t.Error("counting what's kept forgot the commands ada ran")
	}
}

func TestARecordWhoseKeyMovedIsStillKeptOnce(t *testing.T) {
	h := start(t)
	ada, token := h.signUp("ada@example.com")
	h.mustRun("storeroom/shelf/create", token, map[string]any{"title": "Pantry"})
	h.mustRun("storeroom/bay/create", token, map[string]any{"shelf": "pantry", "title": "Dry goods"})
	h.mustRun("storeroom/bay/retitle", token, map[string]any{"id": "pantry-dry_goods", "title": "Grains"})
	h.mustHave("storeroom_bay", "pantry-grains")
	h.app.KeepUsage()
	if held := h.mustHave("usage", ada)["held"]; !reflect.DeepEqual(held, map[string]any{"storeroom_shelf": int64(1), "storeroom_bay": int64(1)}) {
		t.Errorf("ada keeps %v", held)
	}
}
