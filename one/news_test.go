// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"slices"
	"testing"
	"time"

	"github.com/da0x/uione/one"
)

// Clubs with racks of tricks, each trick keeping its history. A person's news is
// every change of a trick on a rack they follow, or that's dealt to them, and only
// while they may read the trick's page: a private club's tricks are its members'.

type Club struct {
	one.Record
	Slug       string `firestore:"slug" one:"required,unique,key"`
	Visibility string `firestore:"visibility" one:"choices=public|private,default=public"`
}

type Seating struct {
	one.Record
	Club   string `firestore:"club" one:"required,key,refers=club::club"`
	Person string `firestore:"person" one:"required,key,refers=user"`
	Role   string `firestore:"role" one:"choices=player,default=player"`
}

type Rack struct {
	one.Record
	Club      string   `firestore:"club" one:"required,refers=club::club"`
	Name      string   `firestore:"name" one:"required"`
	Followers []string `firestore:"followers" one:"refers=user"`
}

type Trick struct {
	one.Record
	one.History
	Club  string   `firestore:"club" one:"required,refers=club::club"`
	Rack  string   `firestore:"rack" one:"required,refers=club::rack"`
	Title string   `firestore:"title" one:"required"`
	Dealt []string `firestore:"dealt" one:"refers=user"`
}

// When a person last looked at their news, one for each person.
type Glance struct {
	one.Record
	Person string    `firestore:"person" one:"key,default=me,refers=user"`
	SeenAt time.Time `firestore:"seen_at"`
}

var clubs = one.Module("club",
	one.Command[Glance]("glance::create").Allow(one.SignedIn).Do(func(c *one.Ctx, g *Glance) error {
		g.SeenAt = time.Now()
		return nil
	}),
	one.Command[Club]("club::create").Allow(one.SignedIn).Do(func(c *one.Ctx, k *Club) error {
		return one.Create(c, &Seating{Club: k.ID, Person: c.Me()})
	}),
	one.Command[Club]("club::update"),
	one.Command[Seating]("seating::create"),
	one.Command[Seating]("seating::delete"),
	one.Command[Rack]("rack::create"),
	one.Command[Rack]("rack::follow").Allow(one.SignedIn).Do(func(c *one.Ctx, r *Rack) error {
		r.Followers = append(r.Followers, c.Me())
		return nil
	}),
	one.Command[Trick]("trick::create"),
	one.Command[Trick]("trick::update"),
	one.Role("player", "club:update", "seating:create", "seating:delete", "rack:create", "trick:create", "trick:update").
		Per(one.Entity[Club](), one.Entity[Seating]()),
	one.View("trick_page").Per(one.Entity[Trick]()).Readers(one.Entity[Seating]()).PublicWhen("club.visibility", "public").
		Copy("title", "title"),
	one.View("news").PerUser().
		List("news", one.All[one.ChangeOf[Trick]]().Except("created_by", one.Viewer).Has("rack.followers", one.Viewer).
			Or(one.All[one.ChangeOf[Trick]]().Has("dealt", one.Viewer).Except("created_by", one.Viewer))).
		Fields("trick", "field", "after").
		FirstOf("seen", one.Where[Glance]("person", one.Viewer), "seen_at"),
	one.View("dealt").PerUser().Each(one.All[Trick]().Has("dealt", one.Viewer)).Fields("title"),
)

func init() { modules = append(modules, clubs) }

// news is what a person's news says: each change's field and what it became, in
// the order they were made.
func (h *harness) news(person string) []string {
	h.t.Helper()
	var said []string
	for _, row := range list(h.view("club::news:"+person), "news") {
		after, _ := row["after"].(string)
		said = append(said, row["field"].(string)+":"+after)
	}
	slices.Sort(said)
	return said
}

func TestNewsHoldsTheChangesOnRacksFollowedAndTricksDealt(t *testing.T) {
	h := start(t)
	ada, adaToken := h.signUp("ada@example.com")
	grace, graceToken := h.signUp("grace@example.com")
	h.mustRun("club/club/create", adaToken, map[string]any{"slug": "bridge"})
	followed := h.mustRun("club/rack/create", adaToken, map[string]any{"club": "bridge", "name": "Openings"})
	other := h.mustRun("club/rack/create", adaToken, map[string]any{"club": "bridge", "name": "Endgames"})
	early := h.mustRun("club/trick/create", adaToken, map[string]any{"club": "bridge", "rack": followed, "title": "Early"})
	h.mustRun("club/trick/create", adaToken, map[string]any{"club": "bridge", "rack": other, "title": "Elsewhere"})

	// Following a rack brings in what already happened on it.
	h.mustRun("club/rack/follow", graceToken, map[string]any{"id": followed})
	if got := h.news(grace); !slices.Equal(got, []string{":"}) {
		t.Fatalf("after following, Grace's news is %v, want the early trick's making", got)
	}
	// A change on it later, by someone else, is in it too.
	h.mustRun("club/trick/update", adaToken, map[string]any{"id": early, "title": "Early, renamed"})
	// And a trick dealt to her on a rack she doesn't follow.
	h.mustRun("club/trick/create", adaToken, map[string]any{"club": "bridge", "rack": other, "title": "Hers", "dealt": []string{grace}})
	want := []string{":", ":", "title:Early, renamed"}
	if got := h.news(grace); !slices.Equal(got, want) {
		t.Errorf("Grace's news is %v, want %v", got, want)
	}

	// What a person did themselves isn't news to them.
	h.mustRun("club/rack/follow", adaToken, map[string]any{"id": followed})
	if got := h.news(ada); len(got) != 0 {
		t.Errorf("Ada's news holds her own changes: %v", got)
	}
	h.mustRun("club/seating/create", adaToken, map[string]any{"club": "bridge", "person": grace})
	h.mustRun("club/trick/update", graceToken, map[string]any{"id": early, "title": "Early, by Grace"})
	if got := h.news(ada); !slices.Equal(got, []string{"title:Early, by Grace"}) {
		t.Errorf("Ada's news is %v, want Grace's change", got)
	}
}

func TestNewsAndWhatsDealtLeaveOutWhatAPersonMayNoLongerRead(t *testing.T) {
	h := start(t)
	_, adaToken := h.signUp("ada@example.com")
	grace, graceToken := h.signUp("grace@example.com")
	h.mustRun("club/club/create", adaToken, map[string]any{"slug": "poker", "visibility": "private"})
	rack := h.mustRun("club/rack/create", adaToken, map[string]any{"club": "poker", "name": "Hands"})
	h.mustRun("club/trick/create", adaToken, map[string]any{"club": "poker", "rack": rack, "title": "Aces", "dealt": []string{grace}})

	// Grace isn't in the private club, so following its rack, or being dealt one of
	// its tricks, shows her nothing.
	h.mustRun("club/rack/follow", graceToken, map[string]any{"id": rack})
	if got := h.news(grace); len(got) != 0 {
		t.Errorf("outside the private club, Grace's news is %v", got)
	}
	if got := list(h.view("club::dealt:"+grace), "rows"); len(got) != 0 {
		t.Errorf("outside the private club, Grace is shown the tricks %v", got)
	}

	// Seated, she reads them.
	seat := h.mustRun("club/seating/create", adaToken, map[string]any{"club": "poker", "person": grace})
	if got := h.news(grace); len(got) != 1 {
		t.Errorf("seated, Grace's news is %v", got)
	}
	if got := list(h.view("club::dealt:"+grace), "rows"); len(got) != 1 {
		t.Errorf("seated, Grace is shown the tricks %v", got)
	}

	// Unseated, she doesn't any more.
	h.mustRun("club/seating/delete", adaToken, map[string]any{"id": seat})
	if got := h.news(grace); len(got) != 0 {
		t.Errorf("unseated, Grace's news is %v", got)
	}

	// Made public, the club's tricks are anyone's to read.
	h.mustRun("club/club/update", adaToken, map[string]any{"id": "poker", "visibility": "public"})
	if got := h.news(grace); len(got) != 1 {
		t.Errorf("once the club is public, Grace's news is %v", got)
	}
	if got := list(h.view("club::dealt:"+grace), "rows"); len(got) != 1 {
		t.Errorf("once the club is public, Grace is shown the tricks %v", got)
	}
}

func TestNewsSaysWhenThePersonLastLookedAtIt(t *testing.T) {
	h := start(t)
	grace, graceToken := h.signUp("grace@example.com")
	ada, adaToken := h.signUp("ada@example.com")
	h.mustRun("club/club/create", adaToken, map[string]any{"slug": "chess"})
	rack := h.mustRun("club/rack/create", adaToken, map[string]any{"club": "chess", "name": "Openings"})
	h.mustRun("club/rack/follow", graceToken, map[string]any{"id": rack})
	if seen := h.view("club::news:" + grace)["seen"]; seen != nil {
		t.Fatalf("before she's looked, Grace's news says she looked at %v", seen)
	}
	h.mustRun("club/glance/create", graceToken, map[string]any{})
	first, ok := h.view("club::news:" + grace)["seen"].(time.Time)
	if !ok {
		t.Fatalf("after looking, Grace's news says she looked at %v", h.view("club::news:" + grace)["seen"])
	}
	h.mustRun("club/glance/create", graceToken, map[string]any{})
	if again, _ := h.view("club::news:" + grace)["seen"].(time.Time); !again.After(first) {
		t.Errorf("looking again moved when she looked from %v to %v", first, again)
	}
	if seen := h.view("club::news:" + ada)["seen"]; seen != nil {
		t.Errorf("Ada's news says she looked, when only Grace did: %v", seen)
	}
}
