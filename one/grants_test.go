// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"testing"

	"github.com/da0x/uione/one"
)

// Gangs, as teams are: a mooring gives a dock role to everyone in a gang, as a team
// is given a role in a project, made and taken away as people join and leave it.

type Gang struct {
	one.Record
	Slug string `firestore:"slug" one:"required,unique,key"`
}

type GangRole struct {
	one.Record
	Gang  string   `firestore:"gang" one:"required,key,refers=gang::gang"`
	Name  string   `firestore:"name" one:"required,key,from=title"`
	Title string   `firestore:"title" one:"required"`
	May   []string `firestore:"may"`
}

type Gangmate struct {
	one.Record
	Gang    string `firestore:"gang" one:"required,key,refers=gang::gang"`
	Person  string `firestore:"person" one:"required,key,refers=user"`
	Role    string `firestore:"role" one:"required,key,refers=gang::gang_role"`
	Through string `firestore:"through"`
}

// A gang's role in a dock: everyone in the gang holds it there.
type Mooring struct {
	one.Record
	Gang string `firestore:"gang" one:"required,key,refers=gang::gang"`
	Dock string `firestore:"dock" one:"required,key,refers=dock::dock"`
	Role string `firestore:"role" one:"required,refers=dock::dock_role"`
}

var gangs = one.Module("gang",
	one.Command[Gang]("gang::create").Allow(one.SignedIn).Do(func(c *one.Ctx, k *Gang) error {
		return one.Create(c, &Gangmate{Gang: k.ID, Person: c.Me(), Role: one.Key(k.ID, "boatswain")})
	}),
	one.Command[Gangmate]("gangmate::create"),
	one.Command[Gangmate]("gangmate::delete"),
	one.Roles(one.Entity[GangRole](), one.Entity[Gang](), one.Entity[Gangmate](), "may").
		Default("boatswain", "Boatswain", "gangmate::create", "gangmate::delete").
		Default("hand", "Hand"),
)

var moorings = one.Module("mooring",
	one.Command[Mooring]("mooring::create").Allow(one.SignedIn),
	one.Command[Mooring]("mooring::delete").Allow(one.SignedIn),
	one.Gives[Mooring]("dock", "gang"),
)

func init() { modules = append(modules, gangs, moorings) }

func TestAGangGivenARoleInADockGivesItToEachOfItsPeople(t *testing.T) {
	h := start(t)
	_, ann := h.signUp("ann@example.com")
	ben, benToken := h.signUp("ben@example.com")
	cat, catToken := h.signUp("cat@example.com")
	h.mustRun("dock/dock/create", ann, map[string]any{"slug": "cove"})
	h.mustRun("gang/gang/create", ann, map[string]any{"slug": "reds"})
	benInGang := h.mustRun("gang/gangmate/create", ann, map[string]any{"gang": "reds", "person": ben, "role": one.Key("reds", "hand")})
	h.expect("dock/berth/create", benToken, map[string]any{"dock": "cove", "title": "one"}, 403, "you don't have permission to do this")

	// The gang is given deckhand in the cove: Ben is a deckhand there, through it.
	mooring := h.mustRun("mooring/mooring/create", ann, map[string]any{"gang": "reds", "dock": "cove", "role": one.Key("cove", "deckhand")})
	h.mustRun("dock/berth/create", benToken, map[string]any{"dock": "cove", "title": "one"})
	given := one.Key("cove", ben, one.Key("cove", "deckhand"))
	if through := h.stored("dock_docker", given)["through"]; through != mooring {
		t.Fatalf("ben's deckhand role says it comes from %v, not the mooring %s", through, mooring)
	}
	// It's the gang's to take away, not the dock's.
	h.expect("dock/docker/delete", ann, map[string]any{"id": given}, 400, "this role comes from the gang, so it's taken away there")

	// Cat joins the gang, and is a deckhand at once.
	h.mustRun("gang/gangmate/create", ann, map[string]any{"gang": "reds", "person": cat, "role": one.Key("reds", "hand")})
	h.mustRun("dock/berth/create", catToken, map[string]any{"dock": "cove", "title": "two"})

	// Ben leaves the gang, and holds nothing in the cove.
	h.mustRun("gang/gangmate/delete", ann, map[string]any{"id": benInGang})
	h.expect("dock/berth/create", benToken, map[string]any{"dock": "cove", "title": "three"}, 403, "you don't have permission to do this")

	// The mooring goes, and so does Cat's role.
	h.mustRun("mooring/mooring/delete", ann, map[string]any{"id": mooring})
	h.expect("dock/berth/create", catToken, map[string]any{"dock": "cove", "title": "three"}, 403, "you don't have permission to do this")
}

// Given a role by two gangs, a person keeps it while either still gives it.
func TestARoleFromTwoGangsStaysWhileEitherGivesIt(t *testing.T) {
	h := start(t)
	_, ann := h.signUp("ann@example.com")
	dan, danToken := h.signUp("dan@example.com")
	h.mustRun("dock/dock/create", ann, map[string]any{"slug": "bay"})
	h.mustRun("gang/gang/create", ann, map[string]any{"slug": "blues"})
	h.mustRun("gang/gang/create", ann, map[string]any{"slug": "greys"})
	h.mustRun("gang/gangmate/create", ann, map[string]any{"gang": "blues", "person": dan, "role": one.Key("blues", "hand")})
	h.mustRun("gang/gangmate/create", ann, map[string]any{"gang": "greys", "person": dan, "role": one.Key("greys", "hand")})
	first := h.mustRun("mooring/mooring/create", ann, map[string]any{"gang": "blues", "dock": "bay", "role": one.Key("bay", "deckhand")})
	h.mustRun("mooring/mooring/create", ann, map[string]any{"gang": "greys", "dock": "bay", "role": one.Key("bay", "deckhand")})

	h.mustRun("mooring/mooring/delete", ann, map[string]any{"id": first})
	h.mustRun("dock/berth/create", danToken, map[string]any{"dock": "bay", "title": "still"})
}
