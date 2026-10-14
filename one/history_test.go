// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"context"
	"reflect"
	"testing"
	"time"

	"cloud.google.com/go/firestore"
	"github.com/da0x/uione/one"
)

// Cases in folders, each keeping its history: a case's page lists its changes,
// and a folder's page every change of every case in it.

type Folder struct {
	one.Record
	Name string `firestore:"name" one:"required,key"`
}

type Case struct {
	one.Record
	one.History
	Folder string `firestore:"folder" one:"required,refers=log::folder"`
	Title  string `firestore:"title" one:"required"`
	Status string `firestore:"status" one:"choices=open|closed,default=open"`
	Note   string `firestore:"note"`
	// Who's working it and what they say of it, as a workflow's lease keeps them.
	HeldBy    string    `firestore:"held_by"`
	HeldUntil time.Time `firestore:"held_until"`
	Data      one.Data  `firestore:"data"`
}

var cases = one.Module("log",
	one.Command[Folder]("folder::create").Allow(one.SignedIn),
	one.Command[Case]("case::create").Allow(one.SignedIn),
	one.Command[Case]("case::update").Allow(one.SignedIn),
	one.Command[Folder]("folder::delete").Allow(one.SignedIn).Do(func(c *one.Ctx, f *Folder) error {
		return one.EachIn(c, "folder", f.ID, func(k *Case) error {
			return one.DispatchDelete[Case](c, k.ID, nil, nil)
		})
	}),
	one.Command[Case]("case::claim").Allow(one.SignedIn).Do(func(c *one.Ctx, k *Case) error {
		k.HeldBy = c.Me()
		k.HeldUntil = time.Now().Add(10 * time.Minute)
		k.Data = one.Data{"status": "working"}
		return nil
	}),
	one.Command[Case]("case::release").Allow(one.SignedIn).Do(func(c *one.Ctx, k *Case) error {
		k.HeldBy, k.HeldUntil, k.Data = "", time.Time{}, nil
		return nil
	}),
	one.Command[Case]("case::close").Allow(one.SignedIn).Do(func(c *one.Ctx, k *Case) error {
		k.Status = "closed"
		return nil
	}),
	one.View("case_page").Per(one.Entity[Case]()).Public().
		List("changes", one.Where[one.ChangeOf[Case]]("case", one.Subject)).Order("created_at").
		Fields("field", "before", "after", "action", "created_by"),
	one.View("folder_page").Per(one.Entity[Folder]()).Public().
		List("timeline", one.Where[one.ChangeOf[Case]]("folder", one.Subject)).Order("created_at").
		Fields("case", "field", "after").
		List("latest", one.Where[one.ChangeOf[Case]]("folder", one.Subject)).Order("-created_at").Limit(1).Fields("field"),
)

func TestACaseKeepsEveryChangeWithWhoAndWhat(t *testing.T) {
	h := start(t)
	ada, token := h.signUp("ada@example.com")
	h.mustRun("log/folder/create", token, map[string]any{"name": "cold"})
	k := h.mustRun("log/case/create", token, map[string]any{"folder": "cold", "title": "Lost lens"})
	h.mustRun("log/case/update", token, map[string]any{"id": k, "title": "Lost lens, Lyon"})
	h.mustRun("log/case/update", token, map[string]any{"id": k, "title": "Lost lens, Lyon"})
	h.mustRun("log/case/close", token, map[string]any{"id": k})

	var got [][]any
	for _, change := range list(h.view("log::case_page:"+k), "changes") {
		got = append(got, []any{change["field"], change["before"], change["after"], change["action"], change["created_by"]})
	}
	want := [][]any{
		{"", nil, nil, "log::case::create", ada},
		{"title", "Lost lens", "Lost lens, Lyon", "log::case::update", ada},
		{"status", "open", "closed", "log::case::close", ada},
	}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("the case's history is\n%v\nwant\n%v", got, want)
	}
}

func TestAFoldersTimelineHoldsTheChangesOfItsCasesOnly(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	h.mustRun("log/folder/create", token, map[string]any{"name": "cold"})
	h.mustRun("log/folder/create", token, map[string]any{"name": "open"})
	first := h.mustRun("log/case/create", token, map[string]any{"folder": "cold", "title": "Lost lens"})
	second := h.mustRun("log/case/create", token, map[string]any{"folder": "cold", "title": "Broken seal"})
	h.mustRun("log/case/create", token, map[string]any{"folder": "open", "title": "Elsewhere"})
	h.mustRun("log/case/close", token, map[string]any{"id": first})

	var got [][]any
	for _, change := range list(h.view("log::folder_page:cold"), "timeline") {
		got = append(got, []any{change["case"], change["field"], change["after"]})
	}
	want := [][]any{{first, "", nil}, {second, "", nil}, {first, "status", "closed"}}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("the cold folder's timeline is %v, want %v", got, want)
	}
	if latest := list(h.view("log::folder_page:cold"), "latest"); len(latest) != 1 || latest[0]["field"] != "status" {
		t.Errorf("the cold folder's latest change is %v", latest)
	}
}

func TestAFieldStoredAsNothingIsntChangedByBeingWrittenEmpty(t *testing.T) {
	h := start(t)
	_, token := h.signUp("bo@example.com")
	h.mustRun("log/folder/create", token, map[string]any{"name": "warm"})
	k := h.mustRun("log/case/create", token, map[string]any{"folder": "warm", "title": "Odd noise"})
	// As a case made before it had a note is stored: without one.
	if _, err := h.store.Collection("log_case").Doc(k).Update(context.Background(), []firestore.Update{{Path: "note", Value: firestore.Delete}}); err != nil {
		t.Fatal(err)
	}
	h.mustRun("log/case/update", token, map[string]any{"id": k, "title": "Odd noise, at night"})
	var fields []any
	for _, change := range list(h.view("log::case_page:"+k), "changes") {
		fields = append(fields, change["field"])
	}
	if !reflect.DeepEqual(fields, []any{"", "title"}) {
		t.Errorf("only the title changed, not the note it had nothing in: %v", fields)
	}
}

func TestADeletedCaseTakesItsHistoryWithIt(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	ctx := context.Background()
	h.mustRun("log/folder/create", token, map[string]any{"name": "sealed"})
	k := h.mustRun("log/case/create", token, map[string]any{"folder": "sealed", "title": "Ledger"})
	h.mustRun("log/case/update", token, map[string]any{"id": k, "title": "Ledger, 1931"})
	h.mustRun("log/folder/delete", token, map[string]any{"id": "sealed"})
	left, _ := h.store.Collection("log_case_history").Where("folder", "==", "sealed").Documents(ctx).GetAll()
	if len(left) != 0 {
		t.Fatalf("a deleted case's history goes with it: %d left", len(left))
	}
	// A folder made again under its name starts with nothing of the old one's.
	h.mustRun("log/folder/create", token, map[string]any{"name": "sealed"})
	h.mustRun("log/case/create", token, map[string]any{"folder": "sealed", "title": "Diary"})
	if timeline := list(h.view("log::folder_page:sealed"), "timeline"); len(timeline) != 1 {
		t.Fatalf("a folder made again shows only its own: %v", timeline)
	}
}

// Who holds a case and what they report of it is how it's going now, shown live on
// its card: taken and let go again and again as it waits, it isn't kept as history.
func TestHoldingACaseIsntKeptInItsHistory(t *testing.T) {
	h := start(t)
	_, token := h.signUp("cy@example.com")
	h.mustRun("log/folder/create", token, map[string]any{"name": "slow"})
	k := h.mustRun("log/case/create", token, map[string]any{"folder": "slow", "title": "Late post"})
	for range 3 {
		h.mustRun("log/case/claim", token, map[string]any{"id": k})
		h.mustRun("log/case/release", token, map[string]any{"id": k})
	}
	h.mustRun("log/case/close", token, map[string]any{"id": k})
	var fields []any
	for _, change := range list(h.view("log::case_page:"+k), "changes") {
		fields = append(fields, change["field"])
	}
	if !reflect.DeepEqual(fields, []any{"", "status"}) {
		t.Errorf("the case's history is its making and its closing, not its holding: %v", fields)
	}
}

// Kept before holding a case was left out of its history: each time it was taken
// and let go is cleared, once, when the backend starts, and the rest stays.
func TestHoldingKeptBeforeIsClearedFromHistoryOnce(t *testing.T) {
	h := start(t)
	ada, token := h.signUp("di@example.com")
	h.mustRun("log/folder/create", token, map[string]any{"name": "old"})
	k := h.mustRun("log/case/create", token, map[string]any{"folder": "old", "title": "Stale lock"})
	ctx := context.Background()
	changes := h.store.Collection("log_case_history")
	for _, field := range []string{"held_by", "held_until", "data", "held_by"} {
		if _, _, err := changes.Add(ctx, map[string]any{"case": k, "folder": "old", "field": field, "action": "log::case::claim", "before": nil, "after": map[string]any{"status": "working"}, "created_at": time.Now(), "created_by": ada}); err != nil {
			t.Fatal(err)
		}
	}
	if _, err := h.store.Collection("once").Doc("history without working log_case_history").Delete(ctx); err != nil {
		t.Fatal(err)
	}
	again, err := one.New(ctx, modules...)
	if err != nil {
		t.Fatal(err)
	}
	defer again.Close()
	kept, err := changes.Where("case", "==", k).Documents(ctx).GetAll()
	if err != nil {
		t.Fatal(err)
	}
	var fields []any
	for _, change := range kept {
		fields = append(fields, change.Data()["field"])
	}
	if !reflect.DeepEqual(fields, []any{""}) {
		t.Errorf("the case's history still holds %v", fields)
	}
}
