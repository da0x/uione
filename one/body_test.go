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

var boards = one.Module("boards",
	one.Command[Column]("column::create").Allow(one.Anyone),
	one.Command[Card]("card::create").Allow(one.Anyone),
	one.Command[Arrow]("arrow::create").Allow(one.Anyone),
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
