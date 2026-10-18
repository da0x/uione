// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"context"
	"testing"
	"time"

	"cloud.google.com/go/firestore"
	"github.com/da0x/uione/one"
)

// A sailor suspended holds nothing in the fleet or its docks, what the fleet gave
// them and what they held there themselves, until they're restored, when they hold
// it all again.
func TestASuspendedPersonHoldsNothingUntilRestored(t *testing.T) {
	h := start(t)
	ann, annToken := h.signUp("ann@example.com")
	ben, benToken := h.signUp("ben@example.com")
	h.mustRun("fleet/fleet/create", annToken, map[string]any{"slug": "navy"})
	h.mustRun("dock/dock/create", annToken, map[string]any{"slug": "pier", "fleet": "navy"})
	h.mustRun("dock/dock/create", annToken, map[string]any{"slug": "quay", "fleet": "navy"})
	// Ben is an admiral, so a harbormaster of the pier through the fleet, and a
	// deckhand on the quay himself.
	benInFleet := h.mustRun("fleet/sailor/create", annToken, map[string]any{"fleet": "navy", "person": ben, "rank": one.Key("navy", "admiral")})
	h.mustRun("dock/docker/create", annToken, map[string]any{"dock": "quay", "person": ben, "role": one.Key("quay", "deckhand")})
	h.mustRun("dock/berth/create", benToken, map[string]any{"dock": "pier", "title": "one"})
	h.mustRun("dock/berth/create", benToken, map[string]any{"dock": "quay", "title": "one"})

	annInFleet := one.Key("navy", ann, one.Key("navy", "admiral"))
	h.expect("fleet/sailor/suspend", annToken, map[string]any{"id": annInFleet}, 400, "you can't suspend yourself")
	h.mustRun("fleet/sailor/suspend", annToken, map[string]any{"id": benInFleet})
	furlough := one.Key("navy", ben) // the person's, in the fleet

	// Suspended, he holds nothing, in the fleet or in either dock.
	h.expect("dock/berth/create", benToken, map[string]any{"dock": "pier", "title": "two"}, 403, "you don't have permission to do this")
	h.expect("dock/berth/create", benToken, map[string]any{"dock": "quay", "title": "two"}, 403, "you don't have permission to do this")
	h.expect("fleet/sailor/create", benToken, map[string]any{"fleet": "navy", "person": ann, "rank": one.Key("navy", "lookout")}, 403, "you don't have permission to do this")
	h.mustBeGone("fleet_sailor", benInFleet)
	h.mustBeGone("dock_docker", one.Key("quay", ben, one.Key("quay", "deckhand")))
	kept := h.stored("fleet_furlough", furlough)
	if len(kept["held"].([]any)) != 2 {
		t.Fatalf("the suspension keeps his own two roles, the fleet's and the quay's: %v", kept["held"])
	}
	if until := kept["until"].(time.Time); until.Before(time.Now().Add(89*24*time.Hour)) || until.After(time.Now().Add(91*24*time.Hour)) {
		t.Fatalf("a suspension lasts 90 days, until %v", until)
	}
	// Nor is he given anything again while he's suspended, here or in what's here.
	h.expect("fleet/sailor/create", annToken, map[string]any{"fleet": "navy", "person": ben, "rank": one.Key("navy", "lookout")}, 400, "they're suspended here; restore them instead")
	h.expect("dock/docker/create", annToken, map[string]any{"dock": "pier", "person": ben, "role": one.Key("pier", "deckhand")}, 400, "they're suspended here; restore them instead")

	// Restored, he holds it all again: the fleet's, the pier's through it, and his own.
	h.mustRun("fleet/furlough/restore", annToken, map[string]any{"id": furlough})
	h.mustBeGone("fleet_furlough", furlough)
	h.mustRun("dock/berth/create", benToken, map[string]any{"dock": "pier", "title": "three"})
	h.mustRun("dock/berth/create", benToken, map[string]any{"dock": "quay", "title": "three"})
	if through := h.stored("dock_docker", one.Key("pier", ben, one.Key("pier", "harbormaster")))["through"]; through != benInFleet {
		t.Fatalf("his pier role comes from his place in the fleet again, not %v", through)
	}
	if through := h.stored("dock_docker", one.Key("quay", ben, one.Key("quay", "deckhand")))["through"]; through != "" {
		t.Fatalf("his quay role is his own again, not from %v", through)
	}
}

// After 90 days a suspension is final: there's nothing to restore, and the person
// can be given a role again as anyone could, which clears what was kept.
func TestASuspensionIsFinalAfter90Days(t *testing.T) {
	h := start(t)
	_, annToken := h.signUp("ann@example.com")
	ben, _ := h.signUp("ben@example.com")
	h.mustRun("fleet/fleet/create", annToken, map[string]any{"slug": "navy"})
	benInFleet := h.mustRun("fleet/sailor/create", annToken, map[string]any{"fleet": "navy", "person": ben, "rank": one.Key("navy", "lookout")})
	h.mustRun("fleet/sailor/suspend", annToken, map[string]any{"id": benInFleet})
	furlough := one.Key("navy", ben)
	if _, err := h.store.Collection("fleet_furlough").Doc(furlough).Update(context.Background(), []firestore.Update{{Path: "until", Value: time.Now().Add(-time.Hour)}}); err != nil {
		t.Fatal(err)
	}
	h.expect("fleet/furlough/restore", annToken, map[string]any{"id": furlough}, 400, "they were suspended more than 90 days ago, so it's final")
	h.mustRun("fleet/sailor/create", annToken, map[string]any{"fleet": "navy", "person": ben, "rank": one.Key("navy", "lookout")})
	h.mustBeGone("fleet_furlough", furlough)
}
