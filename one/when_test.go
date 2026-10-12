// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"slices"
	"testing"

	"github.com/da0x/uione/one"
)

// A parcel moving through a depot's stages, each worked by one of its shifts.
type Shift struct {
	one.Record
	Depot string `firestore:"depot" one:"required,key"`
	Name  string `firestore:"name" one:"required,key"`
}

type Parcel struct {
	one.Record
	Depot string `firestore:"depot"`
	Stage string `firestore:"stage"`
}

var depots = one.Module("depots",
	one.Command[Shift]("shift::create").Allow(one.Anyone),
	one.Command[Parcel]("parcel::create").Allow(one.Anyone),
	one.Command[Parcel]("parcel::update").Allow(one.Anyone),
	// What each shift works: packers the parcels being packed, shippers those being
	// shipped, each in its own depot.
	one.View("shift_work").Public().Per(one.Entity[Shift]()).
		List("items", one.Where[Parcel]("depot", one.SubjectField("depot")).And("stage", "packing").When(one.SubjectField("name"), "packers").
			Or(one.Where[Parcel]("depot", one.SubjectField("depot")).And("stage", "shipping").When(one.SubjectField("name"), "shippers"))).
		Fields("stage"),
)

func init() { modules = append(modules, depots) }

func TestAWayKeptForSomeOfWhatAViewIsPerPicksOnlyForThem(t *testing.T) {
	h := start(t)
	packers := h.mustRun("depots/shift/create", "", map[string]any{"depot": "north", "name": "packers"})
	shippers := h.mustRun("depots/shift/create", "", map[string]any{"depot": "north", "name": "shippers"})
	southPackers := h.mustRun("depots/shift/create", "", map[string]any{"depot": "south", "name": "packers"})
	first := h.mustRun("depots/parcel/create", "", map[string]any{"depot": "north", "stage": "packing"})
	second := h.mustRun("depots/parcel/create", "", map[string]any{"depot": "north", "stage": "shipping"})
	third := h.mustRun("depots/parcel/create", "", map[string]any{"depot": "south", "stage": "packing"})
	works := func(shift string) []string {
		var ids []string
		for _, row := range list(h.view("depots::shift_work:"+shift), "items") {
			ids = append(ids, row["id"].(string))
		}
		slices.Sort(ids)
		return ids
	}
	sorted := func(ids ...string) []string {
		slices.Sort(ids)
		return ids
	}
	for shift, want := range map[string][]string{packers: {first}, shippers: {second}, southPackers: {third}} {
		if got := works(shift); !slices.Equal(got, want) {
			t.Errorf("shift %s works %v, want %v", shift, got, want)
		}
	}
	// Packed, it's the shippers' and no longer the packers'.
	h.mustRun("depots/parcel/update", "", map[string]any{"id": first, "stage": "shipping"})
	if got := works(packers); len(got) != 0 {
		t.Errorf("the packers still work %v", got)
	}
	if got, want := works(shippers), sorted(first, second); !slices.Equal(got, want) {
		t.Errorf("the shippers work %v, want %v", got, want)
	}
	if got := works(southPackers); !slices.Equal(got, []string{third}) {
		t.Errorf("the south's packers work %v, want only %s", got, third)
	}
}
