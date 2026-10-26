// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"net/http"
	"reflect"
	"testing"

	"github.com/da0x/uione/one"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// A storeroom, as tracker's boards are: shelves named from their titles, bays on a
// shelf named from theirs, hops from bay to bay, and crates on a shelf in a bay,
// numbered per shelf. A shelf starts its crates in one of its bays.
type Shelf struct {
	one.Record
	Name  string `firestore:"name" one:"required,key,from=title"`
	Title string `firestore:"title" one:"required"`
	Start string `firestore:"start" one:"refers=storeroom::bay"`
}

type Bay struct {
	one.Record
	Shelf string `firestore:"shelf" one:"required,key,refers=storeroom::shelf"`
	Name  string `firestore:"name" one:"required,key,from=title"`
	Title string `firestore:"title" one:"required"`
}

type Hop struct {
	one.Record
	Shelf string `firestore:"shelf" one:"required,key,refers=storeroom::shelf"`
	From  string `firestore:"from" one:"required,key,refers=storeroom::bay"`
	To    string `firestore:"to" one:"required,key,refers=storeroom::bay"`
}

type Crate struct {
	one.Record
	one.History
	Shelf  string   `firestore:"shelf" one:"required,refers=storeroom::shelf"`
	Number float64  `firestore:"number" one:"serial=shelf"`
	Bay    string   `firestore:"bay" one:"refers=storeroom::bay"`
	Seen   []string `firestore:"seen" one:"refers=storeroom::bay"`
	Title  string   `firestore:"title"`
}

var storeroom = one.Module("storeroom",
	one.Command[Shelf]("shelf::create").Allow(one.Anyone),
	one.Command[Shelf]("shelf::update").Allow(one.Anyone),
	one.Command[Bay]("bay::create").Allow(one.Anyone),
	one.Command[Bay]("bay::update").Allow(one.Anyone),
	one.Command[Bay]("bay::retitle").Allow(one.Anyone).Fields("title"),
	one.Command[Hop]("hop::create").Allow(one.Anyone),
	one.Command[Crate]("crate::create").Allow(one.Anyone),
	one.Command[Crate]("crate::update").Allow(one.Anyone),
	one.View("shelf_page").Public().Per(one.Entity[Shelf]()).List("bays", one.Where[Bay]("shelf", one.Subject)).Fields("title"),
	one.View("crate_page").Public().Per(one.Entity[Crate]()).
		List("changes", one.Where[one.ChangeOf[Crate]]("crate", one.Subject)).Order("created_at").Fields("field", "before", "after"),
)

func init() { modules = append(modules, storeroom) }

// A shelf with two bays and a hop between them, and a crate in the first.
func stocked(t *testing.T) (h *harness, crate string) {
	h = start(t)
	h.mustRun("storeroom/shelf/create", "", map[string]any{"title": "Pantry"})
	h.mustRun("storeroom/bay/create", "", map[string]any{"shelf": "pantry", "title": "Dry goods"})
	h.mustRun("storeroom/bay/create", "", map[string]any{"shelf": "pantry", "title": "Cold"})
	h.mustRun("storeroom/shelf/update", "", map[string]any{"id": "pantry", "start": "pantry-dry_goods"})
	h.mustRun("storeroom/hop/create", "", map[string]any{"shelf": "pantry", "from": "pantry-dry_goods", "to": "pantry-cold"})
	crate = h.mustRun("storeroom/crate/create", "", map[string]any{"shelf": "pantry", "bay": "pantry-cold", "title": "Milk"})
	h.mustRun("storeroom/crate/update", "", map[string]any{"id": crate, "bay": "pantry-dry_goods", "seen": []string{"pantry-cold", "pantry-dry_goods"}})
	return h, crate
}

func (h *harness) mustBeGone(collection, id string) {
	h.t.Helper()
	if _, err := h.store.Collection(collection).Doc(id).Get(h.t.Context()); status.Code(err) != codes.NotFound {
		h.t.Fatalf("%s %s is still there: %v", collection, id, err)
	}
}

func (h *harness) mustHave(collection, id string) map[string]any {
	h.t.Helper()
	stored := h.stored(collection, id)
	if stored == nil {
		h.t.Fatalf("%s %s isn't there", collection, id)
	}
	return stored
}

func TestRenamingABayMovesItAndWhatPointsAtIt(t *testing.T) {
	h, crate := stocked(t)
	if id := h.mustRun("storeroom/bay/update", "", map[string]any{"id": "pantry-dry_goods", "title": "Pasta"}); id != "pantry-pasta" {
		t.Fatalf("the renamed bay is %s, not pantry-pasta", id)
	}
	h.mustBeGone("storeroom_bay", "pantry-dry_goods")
	if bay := h.mustHave("storeroom_bay", "pantry-pasta"); bay["name"] != "pasta" || bay["id"] != "pantry-pasta" || bay["title"] != "Pasta" {
		t.Fatalf("the bay is stored as %v", bay)
	}
	// What points at it points at the new one: a crate, in a field and in a list,
	// the shelf it starts crates in, and the hop named by it, which moves too.
	stored := h.mustHave("storeroom_crate", crate)
	if stored["bay"] != "pantry-pasta" || !reflect.DeepEqual(stored["seen"], []any{"pantry-cold", "pantry-pasta"}) {
		t.Fatalf("the crate points at %v and %v", stored["bay"], stored["seen"])
	}
	if start := h.mustHave("storeroom_shelf", "pantry")["start"]; start != "pantry-pasta" {
		t.Fatalf("the shelf starts in %v", start)
	}
	h.mustBeGone("storeroom_hop", "pantry-pantry%2Ddry_goods-pantry%2Dcold")
	if hop := h.mustHave("storeroom_hop", "pantry-pantry%2Dpasta-pantry%2Dcold"); hop["from"] != "pantry-pasta" {
		t.Fatalf("the hop is from %v", hop["from"])
	}
	// The crate's history says where it was by the bay's title now.
	var got [][]any
	for _, change := range list(h.view("storeroom::crate_page:"+crate), "changes") {
		if change["field"] == "bay" {
			got = append(got, []any{change["before"], change["after"]})
		}
	}
	if want := [][]any{{"Cold", "Pasta"}}; !reflect.DeepEqual(got, want) {
		t.Fatalf("the crate's history of its bay is %v, want %v", got, want)
	}
	// The old title is free for another bay, and the old id isn't an address any
	// more once something else has it.
	if moved := h.mustHave("moved", "storeroom_bay:pantry-dry_goods"); moved["to"] != "pantry-pasta" {
		t.Fatalf("the old id leads to %v", moved["to"])
	}
	h.mustRun("storeroom/bay/create", "", map[string]any{"shelf": "pantry", "title": "Dry goods"})
	h.mustBeGone("moved", "storeroom_bay:pantry-dry_goods")
	// Nor does a bay move onto another's name.
	h.expect("storeroom/bay/update", "", map[string]any{"id": "pantry-pasta", "title": "Cold"}, http.StatusBadRequest, "Title is already taken")
	h.mustHave("storeroom_bay", "pantry-pasta")
}

func TestABaysNameCanBeGivenAndAChangeElsewhereLeavesIt(t *testing.T) {
	h, _ := stocked(t)
	// The name itself, made a name as a title is.
	if id := h.mustRun("storeroom/bay/update", "", map[string]any{"id": "pantry-cold", "name": "Fridge"}); id != "pantry-fridge" {
		t.Fatalf("the bay named Fridge is %s, not pantry-fridge", id)
	}
	if title := h.mustHave("storeroom_bay", "pantry-fridge")["title"]; title != "Cold" {
		t.Fatalf("naming the bay changed its title to %v", title)
	}
	// A command that changes its title moves it too, and one that changes nothing
	// it's named by leaves it.
	if id := h.mustRun("storeroom/bay/retitle", "", map[string]any{"id": "pantry-fridge", "title": "Chiller"}); id != "pantry-chiller" {
		t.Fatalf("the retitled bay is %s, not pantry-chiller", id)
	}
	if id := h.mustRun("storeroom/bay/update", "", map[string]any{"id": "pantry-chiller", "title": "Chiller"}); id != "pantry-chiller" {
		t.Fatalf("the same title moved the bay to %s", id)
	}
	// A form that shows the name sends it back as it was: the title still names it,
	// and only a name changed there does.
	if id := h.mustRun("storeroom/bay/update", "", map[string]any{"id": "pantry-chiller", "title": "Freezer", "name": "chiller"}); id != "pantry-freezer" {
		t.Fatalf("a name sent as it was kept the bay at %s, not pantry-freezer", id)
	}
	if id := h.mustRun("storeroom/bay/update", "", map[string]any{"id": "pantry-freezer", "title": "Ice", "name": "deep_freeze"}); id != "pantry-deep_freeze" {
		t.Fatalf("a name changed with the title named the bay %s, not pantry-deep_freeze", id)
	}
}

func TestRenamingAShelfMovesItsBaysAndTheirHops(t *testing.T) {
	h, crate := stocked(t)
	if id := h.mustRun("storeroom/shelf/update", "", map[string]any{"id": "pantry", "title": "Larder"}); id != "larder" {
		t.Fatalf("the renamed shelf is %s, not larder", id)
	}
	h.mustBeGone("storeroom_shelf", "pantry")
	if start := h.mustHave("storeroom_shelf", "larder")["start"]; start != "larder-dry_goods" {
		t.Fatalf("the shelf starts in %v", start)
	}
	h.mustBeGone("storeroom_bay", "pantry-dry_goods")
	h.mustHave("storeroom_bay", "larder-dry_goods")
	h.mustHave("storeroom_bay", "larder-cold")
	if hop := h.mustHave("storeroom_hop", "larder-larder%2Ddry_goods-larder%2Dcold"); hop["shelf"] != "larder" || hop["to"] != "larder-cold" {
		t.Fatalf("the hop is %v", hop)
	}
	stored := h.mustHave("storeroom_crate", crate)
	if stored["shelf"] != "larder" || stored["bay"] != "larder-dry_goods" || !reflect.DeepEqual(stored["seen"], []any{"larder-cold", "larder-dry_goods"}) {
		t.Fatalf("the crate is %v", stored)
	}
	// Its crates' numbers go on from where they were.
	next := h.mustRun("storeroom/crate/create", "", map[string]any{"shelf": "larder", "title": "Eggs"})
	if n := h.mustHave("storeroom_crate", next)["number"]; n != 2.0 {
		t.Fatalf("the next crate is number %v, not 2", n)
	}
	// Its old page says where it went, and its new one is its page.
	if page := h.view("storeroom::shelf_page:pantry"); page["moved"] != "larder" || page["bays"] != nil {
		t.Fatalf("the shelf's old page is %v", page)
	}
	if bays := list(h.view("storeroom::shelf_page:larder"), "bays"); len(bays) != 2 {
		t.Fatalf("the shelf's new page lists %d bays, not 2", len(bays))
	}
	// Renamed again, the first old id leads to the newest.
	h.mustRun("storeroom/shelf/update", "", map[string]any{"id": "larder", "title": "Cellar"})
	for _, old := range []string{"pantry", "larder"} {
		if to := h.mustHave("moved", "storeroom_shelf:"+old)["to"]; to != "cellar" {
			t.Fatalf("%s leads to %v, not cellar", old, to)
		}
	}
	if page := h.view("storeroom::shelf_page:larder"); page["moved"] != "cellar" {
		t.Fatalf("the shelf's page as larder is %v", page)
	}
}
