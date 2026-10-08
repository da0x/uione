// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"context"
	"net/http"
	"sort"
	"testing"

	"github.com/da0x/uione/one"
)

// Teams whose members hold roles within them: whoever makes a team leads it, a
// lead seats others as helpers, and what each may do to a team's tasks depends
// on their seat in that team, not anywhere else.

type Team struct {
	one.Record
	Slug       string `firestore:"slug" one:"required,unique,key"`
	Visibility string `firestore:"visibility" one:"choices=public|private,default=public"`
}

type Seat struct {
	one.Record
	Team   string `firestore:"team" one:"required,key,refers=team::team"`
	Person string `firestore:"person" one:"required,key,refers=user"`
	Role   string `firestore:"role" one:"choices=lead|helper,default=helper"`
}

type Chore struct {
	one.Record
	Team  string `firestore:"team" one:"required,refers=team::team"`
	Title string `firestore:"title" one:"required"`
}

// A remark is on a chore, so the team it's held within is the chore's.
type Remark struct {
	one.Record
	Chore string `firestore:"chore" one:"required,refers=team::chore"`
	Text  string `firestore:"text" one:"required"`
}

// A report is filed by anyone signed in, even outside the team, and read by the
// team and by whoever filed it and is watching it, never by anyone else.
type Report struct {
	one.Record
	Team     string   `firestore:"team" one:"required,refers=team::team"`
	Title    string   `firestore:"title" one:"required"`
	Author   string   `firestore:"author" one:"default=me,refers=user"`
	Watchers []string `firestore:"watchers" one:"refers=user"`
}

var teams = one.Module("team",
	one.Command[Team]("team::create").Allow(one.SignedIn).Do(func(c *one.Ctx, t *Team) error {
		return one.Create(c, &Seat{Team: t.ID, Person: c.Me(), Role: "lead"})
	}),
	one.Command[Seat]("seat::create"),
	one.Command[Chore]("chore::create"),
	one.Command[Chore]("chore::update"),
	one.Command[Remark]("remark::create"),
	one.Command[Team]("team::update"),
	one.Command[Report]("report::create").Allow(one.SignedIn),
	one.Role("lead", "team:update", "seat:create", "chore:create", "chore:update", "remark:create").Per(one.Entity[Team](), one.Entity[Seat]()),
	one.Role("helper", "chore:create", "remark:create").Per(one.Entity[Team](), one.Entity[Seat]()),
	one.View("board").Per(one.Entity[Team]()).Readers(one.Entity[Seat]()).PublicWhen("visibility", "public").
		Copy("slug", "slug").
		List("chores", one.Where[Chore]("team", one.Subject)).Fields("title"),
	one.View("chore_page").Per(one.Entity[Chore]()).Readers(one.Entity[Seat]()).PublicWhen("team.visibility", "public").
		Copy("title", "title").Copy("visibility", "team.visibility"),
	one.View("report_card").Per(one.Entity[Report]()).ReadersFrom("author").Copy("title", "title"),
	one.View("report_page").Per(one.Entity[Report]()).Readers(one.Entity[Seat]()).ReadersFrom("author").ReadersFrom("watchers").
		Copy("title", "title"),
)

func TestWhoeverMakesATeamLeadsIt(t *testing.T) {
	h := start(t)
	ada, token := h.signUp("ada@example.com")
	if id := h.mustRun("team/team/create", token, map[string]any{"slug": "engine"}); id != "engine" {
		t.Fatalf("the team is %s", id)
	}
	snap, err := h.store.Collection("team_seat").Doc("engine-" + ada).Get(context.Background())
	if err != nil || snap.Data()["role"] != "lead" || snap.Data()["created_by"] != ada {
		t.Fatalf("making a team seated its maker as %v (%v)", snap.Data(), err)
	}
	if _, err := h.store.Collection("roles").Doc("lead").Get(context.Background()); err == nil {
		t.Errorf("a role held within a team was written to roles/, where anyone could be given it")
	}
}

func TestARoleWithinATeamCountsInThatTeamOnly(t *testing.T) {
	h := start(t)
	ada, adaToken := h.signUp("ada@example.com")
	_, graceToken := h.signUp("grace@example.com")
	h.mustRun("team/team/create", adaToken, map[string]any{"slug": "engine"})
	h.mustRun("team/team/create", graceToken, map[string]any{"slug": "loom"})

	chore := h.mustRun("team/chore/create", adaToken, map[string]any{"team": "engine", "title": "Oil the gears"})
	h.mustRun("team/chore/update", adaToken, map[string]any{"id": chore, "title": "Oil every gear"})
	h.expect("team/chore/create", adaToken, map[string]any{"team": "loom", "title": "Not hers"}, http.StatusForbidden, "you don't have permission to do this")
	h.expect("team/chore/update", graceToken, map[string]any{"id": chore, "title": "Not hers either"}, http.StatusForbidden, "you don't have permission to do this")
	h.expect("team/seat/create", graceToken, map[string]any{"team": "engine", "person": ada, "role": "helper"}, http.StatusForbidden, "you don't have permission to do this")
	h.expect("team/chore/create", "", map[string]any{"team": "engine", "title": "Anonymous"}, http.StatusUnauthorized, "sign in to do this")
}

func TestALeadSeatsHelpersWhoCanDoLess(t *testing.T) {
	h := start(t)
	_, adaToken := h.signUp("ada@example.com")
	grace, graceToken := h.signUp("grace@example.com")
	h.mustRun("team/team/create", adaToken, map[string]any{"slug": "engine"})
	h.expect("team/chore/create", graceToken, map[string]any{"team": "engine", "title": "Before"}, http.StatusForbidden, "you don't have permission to do this")

	h.mustRun("team/seat/create", adaToken, map[string]any{"team": "engine", "person": grace})
	chore := h.mustRun("team/chore/create", graceToken, map[string]any{"team": "engine", "title": "Card the wool"})
	h.expect("team/chore/update", graceToken, map[string]any{"id": chore, "title": "Changed"}, http.StatusForbidden, "you don't have permission to do this")
}

func TestAChoresPageShowsItsTeamsVisibilityAsTheTeamChangesIt(t *testing.T) {
	h := start(t)
	_, adaToken := h.signUp("ada@example.com")
	h.mustRun("team/team/create", adaToken, map[string]any{"slug": "engine"})
	chore := h.mustRun("team/chore/create", adaToken, map[string]any{"team": "engine", "title": "Oil the gears"})
	if page := h.view("team::chore_page:" + chore); page["visibility"] != "public" {
		t.Fatalf("a chore's page shows its team as %v", page["visibility"])
	}
	h.mustRun("team/team/update", adaToken, map[string]any{"id": "engine", "visibility": "private"})
	if page := h.view("team::chore_page:" + chore); page["visibility"] != "private" {
		t.Fatalf("after the team changed, a chore's page shows it as %v", page["visibility"])
	}
}

func TestARemarkIsHeldWithinItsChoresTeam(t *testing.T) {
	h := start(t)
	_, adaToken := h.signUp("ada@example.com")
	_, graceToken := h.signUp("grace@example.com")
	h.mustRun("team/team/create", adaToken, map[string]any{"slug": "engine"})
	h.mustRun("team/team/create", graceToken, map[string]any{"slug": "loom"})
	chore := h.mustRun("team/chore/create", adaToken, map[string]any{"team": "engine", "title": "Oil the gears"})

	h.mustRun("team/remark/create", adaToken, map[string]any{"chore": chore, "text": "Done twice"})
	h.expect("team/remark/create", graceToken, map[string]any{"chore": chore, "text": "Not her team"}, http.StatusForbidden, "you don't have permission to do this")
	h.expect("team/remark/create", graceToken, map[string]any{"chore": "no-such-chore", "text": "Nowhere"}, http.StatusForbidden, "you don't have permission to do this")
}

func TestAPrivateTeamsPagesAreReadByItsPeopleUntilItsMadePublic(t *testing.T) {
	h := start(t)
	ada, adaToken := h.signUp("ada@example.com")
	grace, _ := h.signUp("grace@example.com")
	h.mustRun("team/team/create", adaToken, map[string]any{"slug": "engine", "visibility": "private"})
	chore := h.mustRun("team/chore/create", adaToken, map[string]any{"team": "engine", "title": "Oil the gears"})

	access := func(view string) (bool, []any) {
		t.Helper()
		page := h.view(view)
		readers, _ := page["readers"].([]any)
		return page["public"] == true, readers
	}
	for _, view := range []string{"team::board:engine", "team::chore_page:" + chore} {
		if public, readers := access(view); public || len(readers) != 1 || readers[0] != ada {
			t.Errorf("%s is public %v, read by %v", view, public, readers)
		}
	}

	// Seating someone lets them read every page held within the team.
	h.mustRun("team/seat/create", adaToken, map[string]any{"team": "engine", "person": grace})
	for _, view := range []string{"team::board:engine", "team::chore_page:" + chore} {
		if _, readers := access(view); len(readers) != 2 {
			t.Errorf("after seating Grace, %s is read by %v", view, readers)
		}
	}

	// Making the team public opens its pages, the chores' included.
	h.mustRun("team/team/update", adaToken, map[string]any{"id": "engine", "visibility": "public"})
	for _, view := range []string{"team::board:engine", "team::chore_page:" + chore} {
		if public, _ := access(view); !public {
			t.Errorf("after making the team public, %s is still private", view)
		}
	}
}

func TestTwoSeatsInTwoTeamsNeverShareAnId(t *testing.T) {
	h := start(t)
	_, adaToken := h.signUp("ada@example.com")
	grace, graceToken := h.signUp("grace@example.com")
	h.mustRun("team/team/create", adaToken, map[string]any{"slug": "engine"})
	h.mustRun("team/team/create", graceToken, map[string]any{"slug": "engine-x"})

	// Seating "x-<grace>" in engine would once have been engine-x-<grace>: Grace's
	// own seat as lead of engine-x.
	h.mustRun("team/seat/create", adaToken, map[string]any{"team": "engine", "person": "x-" + grace})
	h.mustRun("team/chore/create", graceToken, map[string]any{"team": "engine-x", "title": "Still hers"})
}

func TestAnUpdateCantMoveAChoreIntoAnotherTeam(t *testing.T) {
	h := start(t)
	_, adaToken := h.signUp("ada@example.com")
	_, graceToken := h.signUp("grace@example.com")
	h.mustRun("team/team/create", adaToken, map[string]any{"slug": "engine"})
	h.mustRun("team/team/create", graceToken, map[string]any{"slug": "loom"})
	chore := h.mustRun("team/chore/create", adaToken, map[string]any{"team": "engine", "title": "Oil the gears"})
	h.expect("team/chore/update", adaToken, map[string]any{"id": chore, "team": "loom"}, http.StatusForbidden, "you don't have permission to do this")
	h.mustRun("team/chore/update", adaToken, map[string]any{"id": chore, "title": "Oil every gear"})
}

func TestAReportIsReadByTheTeamAndWhoeverFiledItAndNobodyElse(t *testing.T) {
	h := start(t)
	ada, adaToken := h.signUp("ada@example.com")
	grace, graceToken := h.signUp("grace@example.com")
	linus, _ := h.signUp("linus@example.com")
	alan, _ := h.signUp("alan@example.com")
	h.mustRun("team/team/create", adaToken, map[string]any{"slug": "engine", "visibility": "private"})

	// Grace isn't in the team, and Linus watches what she files.
	report := h.mustRun("team/report/create", graceToken, map[string]any{"team": "engine", "title": "The gears slip", "watchers": []any{linus, grace}})
	page := h.view("team::report_page:" + report)
	readers, _ := page["readers"].([]any)
	want := []string{ada, grace, linus}
	sort.Strings(want)
	if len(readers) != len(want) {
		t.Fatalf("the report is read by %v, not %v", readers, want)
	}
	for i, id := range want {
		if readers[i] != id {
			t.Fatalf("the report is read by %v, not %v", readers, want)
		}
	}
	if page["public"] == true || contains(readers, alan) {
		t.Errorf("the report is open to someone who's neither in the team nor named on it: %v", page)
	}

	// A view read only by who filed it has nobody else, not even the team.
	card := h.view("team::report_card:" + report)
	if readers, _ := card["readers"].([]any); len(readers) != 1 || readers[0] != grace || card["title"] != "The gears slip" {
		t.Errorf("the report's card is %v", card)
	}
}

func contains(list []any, item string) bool {
	for _, each := range list {
		if each == item {
			return true
		}
	}
	return false
}
