// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"context"
	"testing"

	"github.com/da0x/uione/one"
)

// Crews whose roles are their own: each crew starts with a captain and a deckhand,
// and changes what they allow, or adds others, as records, with no deploy.

type Crew struct {
	one.Record
	Slug string `firestore:"slug" one:"required,unique,key"`
}

type Rank struct {
	one.Record
	Crew  string   `firestore:"crew" one:"required,key,refers=crew::crew"`
	Name  string   `firestore:"name" one:"required,key"`
	Title string   `firestore:"title" one:"required"`
	May   []string `firestore:"may"`
}

type Hand struct {
	one.Record
	Crew   string `firestore:"crew" one:"required,key,refers=crew::crew"`
	Person string `firestore:"person" one:"required,key,refers=user"`
	Rank   string `firestore:"rank" one:"required,key,refers=crew::rank"`
}

type Job struct {
	one.Record
	Crew  string `firestore:"crew" one:"required,refers=crew::crew"`
	Title string `firestore:"title" one:"required"`
}

var crews = one.Module("crew",
	one.Command[Crew]("crew::create").Allow(one.Authenticated).Do(func(c *one.Ctx, x *Crew) error {
		return one.Create(c, &Hand{Crew: x.ID, Person: c.Me(), Rank: one.Key(x.ID, "captain")})
	}),
	one.Command[Hand]("hand::create"),
	one.Command[Rank]("rank::create"),
	one.Command[Rank]("rank::update").Fields("title", "may"),
	one.Command[Job]("job::create"),
	one.Command[Job]("job::update"),
	one.Roles(one.Entity[Rank](), one.Entity[Crew](), one.Entity[Hand](), "may").
		Default("captain", "Captain", "hand::create", "rank::create", "rank::update", "job::create", "job::update").
		Default("deckhand", "Deckhand", "job::create"),
)

func TestACrewStartsWithItsDefaultRolesAndItsMakerAsCaptain(t *testing.T) {
	h := start(t)
	_, token := h.signUp("cora@example.com")
	h.mustRun("crew/crew/create", token, map[string]any{"slug": "ark"})
	snap, err := h.store.Collection("crew_rank").Doc("ark-deckhand").Get(context.Background())
	if err != nil {
		t.Fatalf("the crew's deckhand role is a record: %v", err)
	}
	if snap.Data()["title"] != "Deckhand" {
		t.Fatalf("a default role has its title: %v", snap.Data())
	}
	h.mustRun("crew/job/create", token, map[string]any{"crew": "ark", "title": "swab the deck"})
}

func TestWhatARoleAllowsIsWhatItsRecordSaysNow(t *testing.T) {
	h := start(t)
	_, captain := h.signUp("cyd@example.com")
	deckhand, token := h.signUp("dee@example.com")
	h.mustRun("crew/crew/create", captain, map[string]any{"slug": "skiff"})
	h.mustRun("crew/hand/create", captain, map[string]any{"crew": "skiff", "person": deckhand, "rank": one.Key("skiff", "deckhand")})

	job := h.mustRun("crew/job/create", token, map[string]any{"crew": "skiff", "title": "coil the ropes"})
	h.expect("crew/job/update", token, map[string]any{"id": job, "title": "coil every rope"}, 403, "you don't have permission to do this")

	// The captain lets deckhands change jobs too, and it holds at once.
	h.mustRun("crew/rank/update", captain, map[string]any{"id": one.Key("skiff", "deckhand"), "may": []any{"job::create", "job::update"}})
	h.mustRun("crew/job/update", token, map[string]any{"id": job, "title": "coil every rope"})

	// A deckhand can't give themselves more.
	h.expect("crew/rank/update", token, map[string]any{"id": one.Key("skiff", "deckhand"), "may": []any{"rank::update"}}, 403, "you don't have permission to do this")
}

func TestARoleOfAnotherCrewAllowsNothingHere(t *testing.T) {
	h := start(t)
	_, first := h.signUp("eli@example.com")
	_, second := h.signUp("fay@example.com")
	h.mustRun("crew/crew/create", first, map[string]any{"slug": "raft"})
	h.mustRun("crew/crew/create", second, map[string]any{"slug": "barge"})
	h.expect("crew/job/create", second, map[string]any{"crew": "raft", "title": "bail"}, 403, "you don't have permission to do this")
}

func TestACrewMadeBeforeItsRolesWereRecordsGetsThemWhenTheBackendStarts(t *testing.T) {
	h := start(t)
	captain, token := h.signUp("gus@example.com")
	ctx := context.Background()
	// As an older backend left it: a crew, and its captain named, with no records.
	if _, err := h.store.Collection("crew_crew").Doc("dory").Set(ctx, map[string]any{"id": "dory", "slug": "dory"}); err != nil {
		t.Fatal(err)
	}
	if _, err := h.store.Collection("crew_hand").Doc("dory-"+captain+"-captain").Set(ctx, map[string]any{"id": "dory-" + captain + "-captain", "crew": "dory", "person": captain, "rank": "captain"}); err != nil {
		t.Fatal(err)
	}
	h.expect("crew/job/create", token, map[string]any{"crew": "dory", "title": "bail"}, 403, "you don't have permission to do this")

	app, err := one.New(ctx, modules...)
	if err != nil {
		t.Fatal(err)
	}
	defer app.Close()
	snap, err := h.store.Collection("crew_hand").Doc("dory-" + captain + "-captain").Get(ctx)
	if err != nil || snap.Data()["rank"] != one.Key("dory", "captain") {
		t.Fatalf("the captain's role points at the crew's captain record: %v %v", snap.Data(), err)
	}
	h.mustRun("crew/job/create", token, map[string]any{"crew": "dory", "title": "bail"})
}
