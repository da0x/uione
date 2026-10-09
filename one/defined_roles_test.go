// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"context"
	"strings"
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
	Stage string `firestore:"stage" one:"refers=crew::stage"`
}

// Where a job is, and the legs between stages, each taken by the ranks it names.
type Stage struct {
	one.Record
	Crew string `firestore:"crew" one:"required,key,refers=crew::crew"`
	Name string `firestore:"name" one:"required,key"`
}

type Leg struct {
	one.Record
	Crew  string   `firestore:"crew" one:"required,key,refers=crew::crew"`
	From  string   `firestore:"from" one:"required,key,refers=crew::stage"`
	To    string   `firestore:"to" one:"required,key,refers=crew::stage"`
	Ranks []string `firestore:"ranks" one:"refers=crew::rank"`
}

var crews = one.Module("crew",
	one.Command[Crew]("crew::create").Allow(one.SignedIn).Do(func(c *one.Ctx, x *Crew) error {
		return one.Create(c, &Hand{Crew: x.ID, Person: c.Me(), Rank: one.Key(x.ID, "captain")})
	}),
	one.Command[Hand]("hand::create"),
	one.Command[Rank]("rank::create"),
	one.Command[Rank]("rank::update").Fields("title", "may"),
	one.Command[Job]("job::create"),
	one.Command[Job]("job::update"),
	one.Command[Stage]("stage::create"),
	one.Command[Leg]("leg::create"),
	// A crew's legs, in the order of the stages they leave from.
	one.View("legs").Per(one.Entity[Crew]()).Public().
		List("legs", one.Where[Leg]("crew", one.Subject)).Order("-from.name", "to.name").Fields("from.name", "to.name"),
	// A job moves along a leg its mover's rank may take, from the stage it was in.
	one.Command[Job]("job::move").Fields("stage").Do(func(c *one.Ctx, j *Job) error {
		ok, err := one.Exists(c, one.Where[Leg]("from", c.Was("stage")).And("to", j.Stage), func(l *Leg) (bool, error) {
			return c.Held(l.Ranks)
		})
		if err != nil {
			return err
		}
		if !ok {
			return c.Fail("your rank doesn't move a job from there to there")
		}
		return nil
	}),
	one.Roles(one.Entity[Rank](), one.Entity[Crew](), one.Entity[Hand](), "may").
		Default("captain", "Captain", "hand::create", "rank::create", "rank::update", "job::create", "job::update", "job::move", "stage::create", "leg::create").
		Default("deckhand", "Deckhand", "job::create", "job::move"),
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

func TestAJobMovesOnlyAlongALegItsMoversRankMayTake(t *testing.T) {
	h := start(t)
	_, captain := h.signUp("hal@example.com")
	deckhand, token := h.signUp("ivy@example.com")
	h.mustRun("crew/crew/create", captain, map[string]any{"slug": "sloop"})
	h.mustRun("crew/hand/create", captain, map[string]any{"crew": "sloop", "person": deckhand, "rank": one.Key("sloop", "deckhand")})
	for _, stage := range []string{"todo", "doing", "done"} {
		h.mustRun("crew/stage/create", captain, map[string]any{"crew": "sloop", "name": stage})
	}
	todo, doing, done := one.Key("sloop", "todo"), one.Key("sloop", "doing"), one.Key("sloop", "done")
	h.mustRun("crew/leg/create", captain, map[string]any{"crew": "sloop", "from": todo, "to": doing, "ranks": []any{one.Key("sloop", "deckhand")}})
	h.mustRun("crew/leg/create", captain, map[string]any{"crew": "sloop", "from": doing, "to": done, "ranks": []any{one.Key("sloop", "captain")}})

	job := h.mustRun("crew/job/create", captain, map[string]any{"crew": "sloop", "title": "hoist", "stage": todo})
	h.expect("crew/job/move", token, map[string]any{"id": job, "stage": done}, 400, "your rank doesn't move a job from there to there")
	h.mustRun("crew/job/move", token, map[string]any{"id": job, "stage": doing})
	h.expect("crew/job/move", token, map[string]any{"id": job, "stage": done}, 400, "your rank doesn't move a job from there to there")
	h.expect("crew/job/move", token, map[string]any{"id": job, "title": "lower"}, 400, "Title can't be changed by job::move")
	h.mustRun("crew/job/move", captain, map[string]any{"id": job, "stage": done})
}

func TestAListOrdersByAFieldOfWhatItsRowsPointAt(t *testing.T) {
	h := start(t)
	_, captain := h.signUp("jo@example.com")
	h.mustRun("crew/crew/create", captain, map[string]any{"slug": "yawl"})
	for _, stage := range []string{"a", "b", "c"} {
		h.mustRun("crew/stage/create", captain, map[string]any{"crew": "yawl", "name": stage})
	}
	for _, leg := range [][2]string{{"a", "b"}, {"c", "a"}, {"b", "c"}, {"c", "b"}} {
		h.mustRun("crew/leg/create", captain, map[string]any{"crew": "yawl", "from": one.Key("yawl", leg[0]), "to": one.Key("yawl", leg[1])})
	}
	var got []string
	for _, row := range list(h.view("crew::legs:yawl"), "legs") {
		got = append(got, row["from.name"].(string)+row["to.name"].(string))
	}
	if strings.Join(got, " ") != "ca cb bc ab" {
		t.Errorf("legs by where they leave from, last first, then where they go: %v", got)
	}
}

// The crew module as a later deploy says it: captains may also delete stages, and
// crews start with a cook as well.
func laterCrews() one.Item {
	return one.Module("crew",
		one.Command[Crew]("crew::create").Allow(one.SignedIn),
		one.Command[Hand]("hand::create"),
		one.Command[Rank]("rank::create"),
		one.Command[Rank]("rank::update").Fields("title", "may"),
		one.Command[Job]("job::create"),
		one.Command[Job]("job::update"),
		one.Command[Stage]("stage::create"),
		one.Command[Stage]("stage::delete"),
		one.Command[Leg]("leg::create"),
		one.Roles(one.Entity[Rank](), one.Entity[Crew](), one.Entity[Hand](), "may").
			Default("captain", "Captain", "hand::create", "rank::create", "rank::update", "job::create", "job::update", "job::move", "stage::create", "leg::create", "stage::delete").
			Default("deckhand", "Deckhand", "job::create", "job::move").
			Default("cook", "Cook", "job::create"),
	)
}

func TestWhatADeployAddsToTheRolesCrewsStartWithReachesEveryCrew(t *testing.T) {
	h := start(t)
	_, token := h.signUp("hal@example.com")
	h.mustRun("crew/crew/create", token, map[string]any{"slug": "junk"})
	// The crew takes job::move away from its deckhands, its own to say.
	h.mustRun("crew/rank/update", token, map[string]any{"id": one.Key("junk", "deckhand"), "may": []any{"job::create"}})

	later, err := one.New(context.Background(), laterCrews())
	if err != nil {
		t.Fatal(err)
	}
	defer later.Close()
	captain := h.stored("crew_rank", one.Key("junk", "captain"))
	if may, _ := captain["may"].([]any); len(may) != 9 || may[8] != "stage::delete" {
		t.Fatalf("the captain after the deploy may %v", captain["may"])
	}
	// What the crew took away stays away, since the deploy didn't add it.
	if may, _ := h.stored("crew_rank", one.Key("junk", "deckhand"))["may"].([]any); len(may) != 1 {
		t.Fatalf("the deckhand after the deploy may %v", may)
	}
	if cook := h.stored("crew_rank", one.Key("junk", "cook")); cook["title"] != "Cook" {
		t.Fatalf("the crew's new cook is %v", cook)
	}
}
