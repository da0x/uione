// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"net/http"
	"testing"

	"github.com/da0x/uione/one"
)

// A board's columns, the cards in them, and the arrows between them: removing a
// column moves its cards to another and takes its arrows with it.
type Column struct {
	one.Record
	Title string `firestore:"title" one:"required"`
}

type Card struct {
	one.Record
	Column string `firestore:"column" one:"refers=boards::column"`
	Title  string `firestore:"title"`
}

type Arrow struct {
	one.Record
	From string `firestore:"from" one:"refers=boards::column"`
	To   string `firestore:"to" one:"refers=boards::column"`
}

// A lane of a board, keyed by its board and a name made from its title.
type Lane struct {
	one.Record
	Board string `firestore:"board" one:"required,key"`
	Name  string `firestore:"name" one:"required,key,from=title"`
	Title string `firestore:"title" one:"required"`
}

var boards = one.Module("boards",
	one.Command[Lane]("lane::create").Allow(one.Anyone),
	one.Command[Column]("column::create").Allow(one.Anyone),
	one.Command[Card]("card::create").Allow(one.Anyone),
	one.Command[Arrow]("arrow::create").Allow(one.Anyone),
	// A card's page lists the arrows out of its column.
	one.View("card_page").Public().Per(one.Entity[Card]()).List("arrows", one.Where[Arrow]("from", one.SubjectField("column"))).Fields("to"),
	one.Command[Column]("column::delete").Allow(one.Anyone).Inputs("into").Do(func(c *one.Ctx, x *Column) error {
		into, _ := c.Input("into").(string)
		if into == x.ID {
			return c.Fail("pick another column for its cards")
		}
		if err := one.EachIn(c, "column", x.ID, func(card *Card) error {
			card.Column = into
			return nil
		}); err != nil {
			return err
		}
		if err := one.DeleteWhere[Arrow](c, "from", x.ID); err != nil {
			return err
		}
		return one.DeleteWhere[Arrow](c, "to", x.ID)
	}),
)

func init() { modules = append(modules, boards) }

func TestADeleteMovesWhatsInItAndTakesWhatPointsAtItInTheSameStep(t *testing.T) {
	h := start(t)
	todo := h.mustRun("boards/column/create", "", map[string]any{"title": "To do"})
	doing := h.mustRun("boards/column/create", "", map[string]any{"title": "Doing"})
	done := h.mustRun("boards/column/create", "", map[string]any{"title": "Done"})
	first := h.mustRun("boards/card/create", "", map[string]any{"column": doing, "title": "first"})
	second := h.mustRun("boards/card/create", "", map[string]any{"column": doing, "title": "second"})
	other := h.mustRun("boards/card/create", "", map[string]any{"column": todo, "title": "other"})
	in := h.mustRun("boards/arrow/create", "", map[string]any{"from": todo, "to": doing})
	out := h.mustRun("boards/arrow/create", "", map[string]any{"from": doing, "to": done})
	kept := h.mustRun("boards/arrow/create", "", map[string]any{"from": todo, "to": done})

	// What it's sent besides its fields reaches its body, which can refuse.
	h.expect("boards/column/delete", "", map[string]any{"id": doing, "into": doing}, http.StatusBadRequest, "pick another column for its cards")
	if h.stored("boards_column", doing) == nil {
		t.Fatal("a refused delete deleted the column")
	}

	h.mustRun("boards/column/delete", "", map[string]any{"id": doing, "into": done})
	for card, want := range map[string]string{first: done, second: done, other: todo} {
		if got := h.stored("boards_card", card)["column"]; got != want {
			t.Errorf("card %s is in %v, want %s", card, got, want)
		}
	}
	for arrow, stays := range map[string]bool{in: false, out: false, kept: true} {
		docs, err := h.store.Collection("boards_arrow").Where("id", "==", arrow).Documents(t.Context()).GetAll()
		if err != nil {
			t.Fatal(err)
		}
		if (len(docs) == 1) != stays {
			t.Errorf("arrow %s is there: %v, want %v", arrow, len(docs) == 1, stays)
		}
	}
	docs, err := h.store.Collection("boards_column").Documents(t.Context()).GetAll()
	if err != nil || len(docs) != 2 {
		t.Fatalf("the columns left: %d, %v", len(docs), err)
	}
}

func TestAKeyMadeFromATitleNamesANewOneAndIsntTakenTwice(t *testing.T) {
	h := start(t)
	if id := h.mustRun("boards/lane/create", "", map[string]any{"board": "b1", "title": "In review"}); id != "b1-in_review" {
		t.Fatalf("the lane is %s, not b1-in_review", id)
	}
	// The same words, however they're written, aren't a second lane, nor this one again.
	h.expect("boards/lane/create", "", map[string]any{"board": "b1", "title": "In  Review!"}, http.StatusBadRequest, "Title is already taken")
	if title := h.stored("boards_lane", "b1-in_review")["title"]; title != "In review" {
		t.Fatalf("the lane's title became %v", title)
	}
	// A name that's given is used as it is.
	if id := h.mustRun("boards/lane/create", "", map[string]any{"board": "b1", "name": "done", "title": "Finished"}); id != "b1-done" {
		t.Fatalf("the lane is %s, not b1-done", id)
	}
}

func TestAViewPerEntityListsWhatAFieldOfItsEntityPicks(t *testing.T) {
	h := start(t)
	todo := h.mustRun("boards/column/create", "", map[string]any{"title": "To do"})
	done := h.mustRun("boards/column/create", "", map[string]any{"title": "Done"})
	card := h.mustRun("boards/card/create", "", map[string]any{"column": todo, "title": "first"})
	h.mustRun("boards/arrow/create", "", map[string]any{"from": todo, "to": done})
	h.mustRun("boards/arrow/create", "", map[string]any{"from": done, "to": todo})
	arrows := func() int {
		rows, _ := h.view("boards::card_page:" + card)["arrows"].([]any)
		return len(rows)
	}
	if n := arrows(); n != 1 {
		t.Fatalf("the card's page lists %d arrows out of its column, not 1", n)
	}
	// Another arrow out of its column is on its page once it's made.
	h.mustRun("boards/arrow/create", "", map[string]any{"from": todo, "to": todo})
	if n := arrows(); n != 2 {
		t.Fatalf("after another arrow out of its column, the card's page lists %d, not 2", n)
	}
}
