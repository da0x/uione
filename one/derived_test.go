// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"context"
	"testing"

	"github.com/da0x/uione/one"
)

// A print shop's boards, whose steps each keep the shop their board is in, worked
// out when they're made, and start worked by the roles every shop starts with.

type PressShop struct {
	one.Record
	Slug string `firestore:"slug" one:"required,key"`
}

type PressBoard struct {
	one.Record
	Shop string `firestore:"shop" one:"required,refers=printing::press_shop"`
	Name string `firestore:"name" one:"required"`
}

type PressStep struct {
	one.Record
	Board string   `firestore:"board" one:"required,refers=printing::press_board"`
	Name  string   `firestore:"name" one:"required"`
	Shop  string   `firestore:"shop" one:"refers=printing::press_shop,through=board.shop"`
	Roles []string `firestore:"roles" one:"refers=printing::press_role,default=clerk|printer,within=shop"`
}

var printing = one.Module("printing",
	one.Command[PressShop]("press_shop::create").Allow(one.Anyone),
	// A board starts with a step, made with it, before either is stored.
	one.Command[PressBoard]("press_board::create").Allow(one.Anyone).Do(func(c *one.Ctx, b *PressBoard) error {
		return one.DispatchCreate[PressStep](c, &PressStep{Board: b.ID, Name: "intake"}, nil, nil)
	}),
	one.Command[PressStep]("press_step::create").Allow(one.Anyone),
)

func init() { modules = append(modules, printing) }

func TestAFieldIsWorkedOutFromWhatAnotherPointsAtWhenItsMade(t *testing.T) {
	h := start(t)
	shop := h.mustRun("printing/press_shop/create", "", map[string]any{"slug": "north"})
	board := h.mustRun("printing/press_board/create", "", map[string]any{"shop": shop, "name": "orders"})

	// Not given, it's read from the board; given, it's still the board's.
	step := h.mustRun("printing/press_step/create", "", map[string]any{"board": board, "name": "proof"})
	other := h.mustRun("printing/press_step/create", "", map[string]any{"board": board, "name": "press", "shop": "elsewhere"})
	for _, id := range []string{step, other} {
		stored := h.stored("printing_press_step", id)
		if stored["shop"] != shop {
			t.Fatalf("a step on a board of %s is in shop %v", shop, stored["shop"])
		}
		roles, _ := stored["roles"].([]any)
		if len(roles) != 2 || roles[0] != one.Key(shop, "clerk") || roles[1] != one.Key(shop, "printer") {
			t.Fatalf("a step starts worked by %v, not the shop's clerk and printer", stored["roles"])
		}
	}

	// The step its board started with, made in the same command as the board.
	docs, err := h.store.Collection("printing_press_step").Where("name", "==", "intake").Documents(context.Background()).GetAll()
	if err != nil || len(docs) != 1 {
		t.Fatalf("the board's first step: %v, %d of them", err, len(docs))
	}
	if first := docs[0].Data(); first["shop"] != shop || len(first["roles"].([]any)) != 2 {
		t.Fatalf("the step made with its board is %v", first)
	}

	// A list given says its own.
	given := h.mustRun("printing/press_step/create", "", map[string]any{"board": board, "name": "ship", "roles": []string{one.Key(shop, "clerk")}})
	if roles, _ := h.stored("printing_press_step", given)["roles"].([]any); len(roles) != 1 {
		t.Fatalf("a step given one role starts with %v", roles)
	}

	// A board that isn't there is said so.
	h.expect("printing/press_step/create", "", map[string]any{"board": "missing", "name": "x"}, 400, "there's no such board")
}
