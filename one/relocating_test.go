// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"context"
	"fmt"
	"net/http"
	"testing"
	"time"

	"cloud.google.com/go/firestore"
	"github.com/da0x/uione/one"
)

// Harbors, whose address their captains may change: everything named by it moves
// with it, more than one transaction writes, and its old address leads to the new.

type Harbor struct {
	one.Record
	Slug string `firestore:"slug" one:"required,unique,key"`
}

type Pennant struct {
	one.Record
	Harbor string   `firestore:"harbor" one:"required,key,refers=harbor::harbor"`
	Name   string   `firestore:"name" one:"required,key"`
	Title  string   `firestore:"title" one:"required"`
	May    []string `firestore:"may"`
}

type Mate struct {
	one.Record
	Harbor  string `firestore:"harbor" one:"required,key,refers=harbor::harbor"`
	Person  string `firestore:"person" one:"required,key,refers=user"`
	Pennant string `firestore:"pennant" one:"required,key,refers=harbor::pennant"`
}

type Boat struct {
	one.Record
	Harbor   string    `firestore:"harbor" one:"required,key,refers=harbor::harbor"`
	Name     string    `firestore:"name" one:"required,key,from=title"`
	Title    string    `firestore:"title" one:"required"`
	Pennant  string    `firestore:"pennant" one:"required,refers=harbor::pennant"`
	KeyStart string    `firestore:"key_start"`
	KeyMade  time.Time `firestore:"key_made"`
	KeyUsed  time.Time `firestore:"key_used"`
}

type Voyage struct {
	one.Record
	one.History
	Harbor string  `firestore:"harbor" one:"required,key,refers=harbor::harbor"`
	Number float64 `firestore:"number" one:"key,serial=harbor"`
	Title  string  `firestore:"title"`
	Chart  string  `firestore:"chart" one:"file"`
}

type Cargo struct {
	one.Record
	Voyage string `firestore:"voyage" one:"required,key,refers=harbor::voyage"`
	Label  string `firestore:"label" one:"required,key"`
}

// A buoy is in a harbor without being named by it.
type Buoy struct {
	one.Record
	Harbor string `firestore:"harbor" one:"required,refers=harbor::harbor"`
	Title  string `firestore:"title"`
}

var harbors = one.Module("harbor",
	one.Command[Harbor]("harbor::create").Allow(one.SignedIn).Do(func(c *one.Ctx, h *Harbor) error {
		return one.Create(c, &Mate{Harbor: h.ID, Person: c.Me(), Pennant: one.Key(h.ID, "captain")})
	}),
	// What it requires reads the address it's moving to.
	one.Command[Harbor]("harbor::move").Fields("slug").Do(func(c *one.Ctx, h *Harbor) error {
		if h.Slug == "lighthouse" {
			return c.Fail("that address is the lighthouse's")
		}
		return nil
	}),
	one.Command[Voyage]("voyage::create"),
	one.Command[Voyage]("voyage::update").Fields("title"),
	one.Command[Cargo]("cargo::create"),
	one.Command[Buoy]("buoy::create"),
	one.Command[Boat]("boat::create"),
	one.Command[Boat]("boat::key"),
	one.View("deck").Per(one.Entity[Harbor]()).Readers(one.Entity[Mate]()).
		List("voyages", one.Where[Voyage]("harbor", one.Subject)).Fields("title"),
	one.Roles(one.Entity[Pennant](), one.Entity[Harbor](), one.Entity[Mate](), "may").
		Default("captain", "Captain", "harbor::move", "voyage::create", "voyage::update", "cargo::create", "buoy::create", "boat::create", "boat::key").
		Default("deckhand", "Deckhand", "voyage::update").
		Services(one.Entity[Boat](), "deckhand"),
)

func init() { modules = append(modules, harbors) }

// stocked gives a harbor many voyages, each with a cargo, as a backend from before
// kept them, more than a transaction writes, and counts its voyages to there.
func (h *harness) stocked(harbor string, from, to int) {
	h.t.Helper()
	ctx := context.Background()
	writer := h.store.BulkWriter(ctx)
	for n := from; n <= to; n++ {
		voyage := one.Key(harbor, fmt.Sprint(n))
		writer.Set(h.store.Collection("harbor_voyage").Doc(voyage), map[string]any{"id": voyage, "harbor": harbor, "number": float64(n), "title": fmt.Sprintf("Voyage %d", n)})
		writer.Set(h.store.Collection("harbor_cargo").Doc(one.Key(voyage, "salt")), map[string]any{"id": one.Key(voyage, "salt"), "voyage": voyage, "label": "salt"})
	}
	writer.End()
	if _, err := h.store.Collection("serials").Doc("harbor_voyage.number."+harbor).Set(ctx, map[string]any{"last": float64(to)}); err != nil {
		h.t.Fatal(err)
	}
}

func (h *harness) within(collection, field, value string) int {
	h.t.Helper()
	docs, err := h.store.Collection(collection).Where(field, "==", value).Documents(context.Background()).GetAll()
	if err != nil {
		h.t.Fatal(err)
	}
	return len(docs)
}

func TestAHarborMovesToItsNewAddressWithEverythingInIt(t *testing.T) {
	h := start(t)
	ctx := context.Background()
	cora, captain := h.signUp("cora@example.com")
	h.mustRun("harbor/harbor/create", captain, map[string]any{"slug": "old"})
	first := h.mustRun("harbor/voyage/create", captain, map[string]any{"harbor": "old", "title": "Maiden"})
	h.mustRun("harbor/voyage/update", captain, map[string]any{"id": first, "title": "Maiden voyage"})
	h.mustRun("harbor/cargo/create", captain, map[string]any{"voyage": first, "label": "tea"})
	chart := h.upload(captain, "chart.txt", "north by northwest")
	charted := h.mustRun("harbor/voyage/create", captain, map[string]any{"harbor": "old", "title": "Charted", "chart": chart})
	buoy := h.mustRun("harbor/buoy/create", captain, map[string]any{"harbor": "old", "title": "Red"})
	boat := h.mustRun("harbor/boat/create", captain, map[string]any{"harbor": "old", "title": "Tender", "pennant": one.Key("old", "deckhand")})
	_, reply := h.run("harbor/boat/key", captain, map[string]any{"id": boat})
	key := reply["key"].(string)
	h.stocked("old", 3, 300)

	if to := h.mustRun("harbor/harbor/move", captain, map[string]any{"id": "old", "slug": "new"}); to != "new" {
		t.Fatalf("the harbor moved to %q", to)
	}

	if h.within("harbor_voyage", "harbor", "new") != 300 || h.within("harbor_voyage", "harbor", "old") != 0 {
		t.Errorf("its voyages are %d at the new address and %d at the old", h.within("harbor_voyage", "harbor", "new"), h.within("harbor_voyage", "harbor", "old"))
	}
	if _, err := h.store.Collection("harbor_harbor").Doc("old").Get(ctx); err == nil {
		t.Error("the harbor is still at its old address")
	}
	if got := h.mustHave("harbor_harbor", "new")["slug"]; got != "new" {
		t.Errorf("its address is %v", got)
	}
	// What's named by what moved moves too, all the way down.
	if h.mustHave("harbor_cargo", one.Key("new", "1", "tea"))["voyage"] != one.Key("new", "1") || h.within("harbor_cargo", "label", "salt") != 298 {
		t.Error("the cargo didn't move with its voyages")
	}
	if h.within("harbor_cargo", "voyage", one.Key("old", "7")) != 0 {
		t.Error("a cargo was left at a voyage's old address")
	}
	// What's in it without being named by it stays, pointing at the new address.
	if h.mustHave("harbor_buoy", buoy)["harbor"] != "new" {
		t.Error("the buoy still points at the old address")
	}
	// Its roles and people move; its service keeps its id, and its key still works.
	h.mustHave("harbor_pennant", one.Key("new", "captain"))
	if b := h.mustHave("harbor_boat", boat); b["harbor"] != "new" || b["pennant"] != one.Key("new", "deckhand") {
		t.Errorf("the boat is %v", b)
	}
	h.mustRun("harbor/voyage/update", key, map[string]any{"id": one.Key("new", "5"), "title": "By the tender"})
	// A file attached to what moved is still read through it.
	if charted != one.Key("old", "2") {
		t.Fatalf("the charted voyage is %s", charted)
	}
	if status, body := h.download(captain, chart); status != http.StatusOK || body != "north by northwest" {
		t.Errorf("the chart of the moved voyage reads %d %q", status, body)
	}
	// Its history goes with it, and its counter: the next voyage is the 301st.
	changes, _ := h.store.Collection("harbor_voyage_history").Where("voyage", "==", one.Key("new", "1")).Documents(ctx).GetAll()
	if len(changes) == 0 {
		t.Error("the first voyage's history didn't move with it")
	}
	if next := h.mustRun("harbor/voyage/create", captain, map[string]any{"harbor": "new", "title": "Onward"}); next != one.Key("new", "301") {
		t.Errorf("the next voyage is %s", next)
	}
	// What the captain keeps is what they made, wherever it is.
	h.app.KeepUsage()
	if held := h.mustHave("usage", cora)["held"].(map[string]any); held["harbor_voyage"] != int64(3) || held["harbor_harbor"] != int64(1) {
		t.Errorf("the captain keeps %v", held)
	}
	// Its page is at its new address, and the old leads there.
	if voyages, _ := h.view("harbor::deck:new")["voyages"].([]any); len(voyages) != 301 {
		t.Errorf("its page lists %d voyages", len(voyages))
	}
	if old := h.view("harbor::deck:old"); old["moved"] != "new" || old["voyages"] != nil {
		t.Errorf("its page at the old address is %v, not where it went", old)
	}
	if h.mustHave("moved", "harbor_harbor:old")["to"] != "new" || h.mustHave("moved", "harbor_voyage:"+one.Key("old", "9"))["to"] != one.Key("new", "9") {
		t.Error("the old addresses don't lead to the new")
	}
	if left, _ := h.store.Collection("relocations").Documents(ctx).GetAll(); len(left) != 0 {
		t.Errorf("the move is still asked for: %d", len(left))
	}
}

func TestAHarborIsntMovedOntoAnotherAndIsClosedWhileItMoves(t *testing.T) {
	h := start(t)
	ctx := context.Background()
	_, captain := h.signUp("dov@example.com")
	h.mustRun("harbor/harbor/create", captain, map[string]any{"slug": "east"})
	h.mustRun("harbor/harbor/create", captain, map[string]any{"slug": "west"})
	h.expect("harbor/harbor/move", captain, map[string]any{"id": "east", "slug": "west"}, http.StatusBadRequest, "Slug is already taken")
	h.expect("harbor/harbor/move", captain, map[string]any{"id": "east", "slug": "lighthouse"}, http.StatusBadRequest, "that address is the lighthouse's")
	if h.mustHave("harbor_harbor", "east")["slug"] != "east" {
		t.Fatal("a move refused changed the address anyway")
	}

	// As if a move were under way on another backend.
	moving := h.store.Collection("relocations").Doc("harbor_harbor:east")
	if _, err := moving.Set(ctx, map[string]any{"collection": "harbor_harbor", "entity": "harbor::harbor", "from": "east", "to": "north",
		"fields": map[string]any{"slug": "north"}, "until": time.Now().Add(time.Hour)}); err != nil {
		t.Fatal(err)
	}
	h.expect("harbor/voyage/create", captain, map[string]any{"harbor": "east", "title": "Late"}, http.StatusConflict, "this harbor is moving to its new address; try again in a minute")
	h.expect("harbor/harbor/move", captain, map[string]any{"id": "east", "slug": "south"}, http.StatusConflict, "this harbor is moving to its new address; try again in a minute")
	// Another harbor isn't.
	h.mustRun("harbor/voyage/create", captain, map[string]any{"harbor": "west", "title": "On time"})
	// Nor can another move onto where it's going.
	h.expect("harbor/harbor/move", captain, map[string]any{"id": "west", "slug": "north"}, http.StatusBadRequest, "Slug is already taken")

	// The backend that was moving it stopped; the next to start finishes it.
	h.stocked("east", 1, 40)
	if _, err := moving.Update(ctx, []firestore.Update{{Path: "until", Value: time.Time{}}}); err != nil {
		t.Fatal(err)
	}
	app, err := one.New(ctx, modules...)
	if err != nil {
		t.Fatal(err)
	}
	defer app.Close()
	deadline := time.Now().Add(20 * time.Second)
	for {
		if _, err := moving.Get(ctx); err != nil {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("the move wasn't finished")
		}
		time.Sleep(100 * time.Millisecond)
	}
	if h.within("harbor_voyage", "harbor", "north") != 40 || h.within("harbor_voyage", "harbor", "east") != 0 {
		t.Error("the voyages didn't all move")
	}
	h.mustRun("harbor/voyage/create", captain, map[string]any{"harbor": "north", "title": "Open again"})
}

func TestADeskThatMovedKeepsItsGitHubSecret(t *testing.T) {
	h := start(t)
	t.Setenv("GITHUB_WEBHOOK_SECRET", secret)
	_, owner := h.signUp("eli@example.com")
	h.mustRun("desks/desk/create", owner, map[string]any{"slug": "aft", "repository": "da0x/aft"})
	h.mustRun("desks/desk_ticket/create", owner, map[string]any{"desk": "aft", "title": "Rudder"})
	h.mustRun("desks/desk/move", owner, map[string]any{"id": "aft", "slug": "stern"})
	h.mustRun("desks/desk/move", owner, map[string]any{"id": "stern", "slug": "poop"})
	// GitHub was given the secret when it was aft, and still signs with it.
	if status := h.deliverTo("/hooks/desks", push("da0x/aft", "Fix #1"), one.GitHubSecret("aft")); status != http.StatusOK {
		t.Fatalf("a push signed with the secret it had was answered %d", status)
	}
	if status := h.deliverTo("/hooks/desks", push("da0x/aft", "Fix #1"), one.GitHubSecret("poop")); status != http.StatusUnauthorized {
		t.Fatalf("a push signed with a secret made from its new address was answered %d", status)
	}
	if notes := h.within("desks_desk_note", "ticket", one.Key("poop", "1")); notes != 1 {
		t.Fatalf("the push left %d notes on the moved ticket", notes)
	}
}
