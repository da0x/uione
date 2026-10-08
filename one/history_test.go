// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"context"
	"reflect"
	"testing"

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
}

var cases = one.Module("log",
	one.Command[Folder]("folder::create").Allow(one.SignedIn),
	one.Command[Case]("case::create").Allow(one.SignedIn),
	one.Command[Case]("case::update").Allow(one.SignedIn),
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
