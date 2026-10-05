// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"net/http"
	"reflect"
	"testing"

	"github.com/da0x/uione/one"
)

// Tickets with labels, and people assigned to them.

type Ticket struct {
	one.Record
	Title     string   `firestore:"title" one:"required"`
	Labels    []string `firestore:"labels"`
	Assignees []string `firestore:"assignees" one:"refers=user"`
}

var desk = one.Module("desk",
	one.Command[Ticket]("ticket::create").Allow(one.Authenticated),
	one.Command[Ticket]("ticket::update").Allow(one.Authenticated),
	one.Command[Ticket]("ticket::take").Allow(one.Authenticated).Do(func(c *one.Ctx, t *Ticket) error {
		t.Assignees = one.Add(t.Assignees, c.Me())
		return nil
	}),
	one.Command[Ticket]("ticket::drop").Allow(one.Authenticated).Do(func(c *one.Ctx, t *Ticket) error {
		t.Assignees = one.Remove(t.Assignees, c.Me())
		return nil
	}),
	one.View("board").Public().Each(one.All[Ticket]()).Fields("title", "labels", "assignees.name"),
	one.View("taken").PerUser().Each(one.All[Ticket]().Has("assignees", one.Viewer)).Fields("title"),
	one.View("bugs").Public().Count("open", one.All[Ticket]().Has("labels", "bug")),
)

func TestATicketsLabelsAreAListWithEachOnce(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	h.mustRun("desk/ticket/create", token, map[string]any{"title": "Crash", "labels": []any{"bug", "ui", " bug ", ""}})
	h.mustRun("desk/ticket/create", token, map[string]any{"title": "Docs"})
	board := rows(h.view("desk::board"))
	labels := map[any]any{}
	for _, row := range board {
		labels[row["title"]] = row["labels"]
	}
	if !reflect.DeepEqual(labels["Crash"], []any{"bug", "ui"}) || !reflect.DeepEqual(labels["Docs"], []any{}) {
		t.Errorf("the board shows labels %v", labels)
	}
	if got := h.view("desk::bugs")["open"]; got != int64(1) {
		t.Errorf("%v tickets are labelled bug", got)
	}
	h.expect("desk/ticket/create", token, map[string]any{"title": "Odd", "labels": "bug"}, http.StatusBadRequest, "Labels should be a list")
}

func TestTakingATicketPutsItAmongYourOwn(t *testing.T) {
	h := start(t)
	ada, token := h.signUp("ada@example.com")
	token = h.rename(token, "ada@example.com", "Ada Lovelace", "")
	_, grace := h.signUp("grace@example.com")
	ticket := h.mustRun("desk/ticket/create", token, map[string]any{"title": "Crash"})

	h.mustRun("desk/ticket/take", token, map[string]any{"id": ticket})
	h.mustRun("desk/ticket/take", token, map[string]any{"id": ticket})
	if got := column(h.view("desk::taken:"+ada), "title"); !reflect.DeepEqual(got, []any{"Crash"}) {
		t.Errorf("Ada's tickets are %v", got)
	}
	if got := rows(h.view("desk::board"))[0]["assignees.name"]; !reflect.DeepEqual(got, []any{"Ada Lovelace"}) {
		t.Errorf("the board shows the ticket assigned to %v", got)
	}

	h.mustRun("desk/ticket/take", grace, map[string]any{"id": ticket})
	h.mustRun("desk/ticket/drop", token, map[string]any{"id": ticket})
	if got := rows(h.view("desk::taken:" + ada)); len(got) != 0 {
		t.Errorf("after dropping it, Ada's tickets are %v", got)
	}
	if got := rows(h.view("desk::board"))[0]["assignees.name"]; !reflect.DeepEqual(got, []any{""}) {
		t.Errorf("after Ada dropped it, the board shows it assigned to %v", got)
	}
}
