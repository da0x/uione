// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"testing"
	"time"

	"cloud.google.com/go/firestore"
	"github.com/da0x/uione/one"
)

// The fleet's logbook entries, oldest first.
func (h *harness) logbook(fleet string) []map[string]any {
	h.t.Helper()
	docs, err := h.store.Collection("fleet_logbook").Where("fleet", "==", fleet).OrderBy("created_at", firestore.Asc).Documents(context.Background()).GetAll()
	if err != nil {
		h.t.Fatal(err)
	}
	var out []map[string]any
	for _, d := range docs {
		out = append(out, d.Data())
	}
	return out
}

// Every command in a fleet and in its docks is in its logbook: who ran it, from
// where, on what, and what it changed. One refused isn't, since nothing was done.
func TestEveryCommandInAPlaceAndWhatsWithinItIsAudited(t *testing.T) {
	h := start(t)
	ann, annToken := h.signUp("ann@example.com")
	ben, benToken := h.signUp("ben@example.com")
	h.mustRun("fleet/fleet/create", annToken, map[string]any{"slug": "navy"})
	h.mustRun("dock/dock/create", annToken, map[string]any{"slug": "pier", "fleet": "navy"})
	// Run from an address, as Cloud Run says it.
	body, _ := json.Marshal(map[string]any{"dock": "pier", "title": "first"})
	request, _ := http.NewRequest(http.MethodPost, h.server.URL+"/api/dock/berth/create", bytes.NewReader(body))
	request.Header.Set("Authorization", "Bearer "+annToken)
	request.Header.Set("X-Forwarded-For", "203.0.113.7, 10.0.0.1")
	response, err := http.DefaultClient.Do(request)
	if err != nil || response.StatusCode != http.StatusOK {
		t.Fatalf("berth::create: %v %v", response.StatusCode, err)
	}
	var made struct{ ID string }
	json.NewDecoder(response.Body).Decode(&made)
	response.Body.Close()

	entries := h.logbook("navy")
	var commands []any
	for _, e := range entries {
		commands = append(commands, e["command"])
	}
	// The fleet made, which made Ann its admiral; the dock made in it; the berth.
	if len(entries) != 3 || commands[0] != "fleet::fleet::create" || commands[1] != "dock::dock::create" || commands[2] != "dock::berth::create" {
		t.Fatalf("the logbook holds %v", commands)
	}
	last := entries[2]
	if last["person"] != ann || last["address"] != "203.0.113.7" || last["kind"] != "dock::berth" || last["item"] != made.ID {
		t.Fatalf("the berth's entry says %v", last)
	}
	if changed, _ := last["changed"].([]any); len(changed) != 2 || changed[0] != "dock" || changed[1] != "title" {
		t.Fatalf("the berth's entry says it changed %v", last["changed"])
	}
	if title, _ := last["changes"].(map[string]any)["title"].(map[string]any); title["before"] != nil || title["after"] != "first" {
		t.Fatalf("the berth's title changed %v", title)
	}
	if until := last["until"].(time.Time); until.Before(time.Now().Add(364*24*time.Hour)) || until.After(time.Now().Add(366*24*time.Hour)) {
		t.Fatalf("an entry is kept a year, until %v", until)
	}

	// Refused, nothing was done, and nothing's written.
	h.expect("dock/berth/create", benToken, map[string]any{"dock": "pier", "title": "no"}, 403, "you don't have permission to do this")
	if n := len(h.logbook("navy")); n != 3 {
		t.Fatalf("a refused command wrote to the logbook: %d entries", n)
	}

	// Ben joins as a lookout; the log is the admirals' to read, not his.
	h.mustRun("fleet/sailor/create", annToken, map[string]any{"fleet": "navy", "person": ben, "rank": one.Key("navy", "lookout")})
	if readers, _ := h.view("fleet::fleet_log:navy")["readers"].([]any); len(readers) != 1 || readers[0] != ann {
		t.Fatalf("the fleet's log is read by %v, not its admiral alone", readers)
	}
	h.mustRun("fleet/sailor/create", annToken, map[string]any{"fleet": "navy", "person": ben, "rank": one.Key("navy", "admiral")})
	if readers, _ := h.view("fleet::fleet_log:navy")["readers"].([]any); len(readers) != 2 {
		t.Fatalf("made an admiral, Ben reads the log too: %v", readers)
	}
}

// No one changes or deletes an entry, not even an admiral with a command for it.
func TestAnAuditCantBeChanged(t *testing.T) {
	h := start(t)
	_, annToken := h.signUp("ann@example.com")
	h.mustRun("fleet/fleet/create", annToken, map[string]any{"slug": "navy"})
	entry := h.logbook("navy")[0]["id"].(string)
	h.expect("fleet/logbook/delete", annToken, map[string]any{"id": entry}, 403, "an audit can't be changed")
}
