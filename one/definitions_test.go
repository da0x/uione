// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"context"
	"testing"

	"github.com/da0x/uione/one"
)

// tasks, as the next deploy has them: the list shows each task's owner too.
func tasksShowingOwners() one.Item {
	return one.Module("tasks",
		one.Command[Task]("task::create").Allow(one.SignedIn),
		one.View("list").PerUser().Each(one.Where[Task]("owner", one.Viewer)).Order("done", "-created_at").Fields("title", "done", "owner"),
	)
}

func TestAViewWhoseDefinitionChangedIsRebuiltWhenTheBackendStarts(t *testing.T) {
	h := start(t)
	ada, token := h.signUp("ada@example.com")
	h.mustRun("tasks/task/create", token, map[string]any{"title": "write the docs"})
	before := rows(h.view("tasks::list:" + ada))
	if len(before) != 1 || before[0]["owner"] != nil {
		t.Fatalf("before the change, the list is %v", before)
	}

	// The next deploy's backend starts with the list showing owners: the stored
	// document gains them without anything it shows changing.
	ctx := context.Background()
	next, err := one.New(ctx, tasksShowingOwners())
	if err != nil {
		t.Fatal(err)
	}
	next.Close()
	after := rows(h.view("tasks::list:" + ada))
	if len(after) != 1 || after[0]["owner"] != ada || after[0]["title"] != "write the docs" {
		t.Fatalf("after the definition changed, the list is %v", after)
	}

	// Starting again with the same definition leaves the documents alone.
	kept, err := h.store.Collection("view_definitions").Doc("tasks::list").Get(ctx)
	if err != nil {
		t.Fatal(err)
	}
	again, err := one.New(ctx, tasksShowingOwners())
	if err != nil {
		t.Fatal(err)
	}
	again.Close()
	still, err := h.store.Collection("view_definitions").Doc("tasks::list").Get(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if !still.UpdateTime.Equal(kept.UpdateTime) {
		t.Errorf("an unchanged view was rebuilt: %v then %v", kept.Data(), still.Data())
	}
	if kept.Data()["rebuilt"] != int64(1) {
		t.Errorf("the changed view rebuilt %v documents, not 1", kept.Data()["rebuilt"])
	}
}
