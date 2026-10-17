// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"testing"

	"github.com/da0x/uione/one"
)

// Fleets above docks, as organizations are above projects: a fleet's admiral holds
// harbormaster in each dock of the fleet, given as a member marked with where it
// comes from, and taken away with it.

type Fleet struct {
	one.Record
	Slug string `firestore:"slug" one:"required,unique,key"`
}

type FleetRank struct {
	one.Record
	Fleet string   `firestore:"fleet" one:"required,key,refers=fleet::fleet"`
	Name  string   `firestore:"name" one:"required,key,from=title"`
	Title string   `firestore:"title" one:"required"`
	May   []string `firestore:"may"`
}

type Sailor struct {
	one.Record
	Fleet  string `firestore:"fleet" one:"required,key,refers=fleet::fleet"`
	Person string `firestore:"person" one:"required,key,refers=user"`
	Rank   string `firestore:"rank" one:"required,key,refers=fleet::fleet_rank"`
}

type Dock struct {
	one.Record
	Slug  string `firestore:"slug" one:"required,unique,key"`
	Fleet string `firestore:"fleet" one:"refers=fleet::fleet"`
}

type DockRole struct {
	one.Record
	Dock  string   `firestore:"dock" one:"required,key,refers=dock::dock"`
	Name  string   `firestore:"name" one:"required,key,from=title"`
	Title string   `firestore:"title" one:"required"`
	May   []string `firestore:"may"`
}

type Docker struct {
	one.Record
	Dock    string `firestore:"dock" one:"required,key,refers=dock::dock"`
	Person  string `firestore:"person" one:"required,key,refers=user"`
	Role    string `firestore:"role" one:"required,key,refers=dock::dock_role"`
	Through string `firestore:"through"`
}

type Berth struct {
	one.Record
	Dock  string `firestore:"dock" one:"required,refers=dock::dock"`
	Title string `firestore:"title" one:"required"`
}

var fleets = one.Module("fleet",
	one.Command[Fleet]("fleet::create").Allow(one.SignedIn).Do(func(c *one.Ctx, f *Fleet) error {
		return one.Create(c, &Sailor{Fleet: f.ID, Person: c.Me(), Rank: one.Key(f.ID, "admiral")})
	}),
	one.Command[Sailor]("sailor::create"),
	one.Command[Sailor]("sailor::delete"),
	// A fleet's docks: another module's entity, read before that module is registered.
	one.View("fleet_docks").Per(one.Entity[Fleet]()).Public().List("docks", one.Where[Dock]("fleet", one.Subject)).Fields("slug"),
	one.Roles(one.Entity[FleetRank](), one.Entity[Fleet](), one.Entity[Sailor](), "may").
		Default("admiral", "Admiral", "sailor::create", "sailor::delete").
		Default("lookout", "Lookout").
		Holds("admiral", "dock", "harbormaster", "fleet"),
)

var docks = one.Module("dock",
	// Whoever makes a dock is its harbormaster themselves, as a project's maker is.
	one.Command[Dock]("dock::create").Allow(one.SignedIn).Do(func(c *one.Ctx, d *Dock) error {
		return one.Create(c, &Docker{Dock: d.ID, Person: c.Me(), Role: one.Key(d.ID, "harbormaster")})
	}),
	one.Command[Dock]("dock::update").Allow(one.SignedIn).Fields("fleet"),
	one.Command[Docker]("docker::create"),
	one.Command[Docker]("docker::delete"),
	one.Command[Berth]("berth::create"),
	one.Roles(one.Entity[DockRole](), one.Entity[Dock](), one.Entity[Docker](), "may").
		Default("harbormaster", "Harbormaster", "berth::create", "docker::create", "docker::delete"),
)

func init() { modules = append(modules, fleets, docks) }

// What an admiral holds in each dock: made with the dock, and with the admiral.
func TestAnOrganizationsOwnerHoldsTheirRoleInEachOfItsProjects(t *testing.T) {
	h := start(t)
	annID, ann := h.signUp("ann@example.com")
	ben, benToken := h.signUp("ben@example.com")
	h.mustRun("fleet/fleet/create", ann, map[string]any{"slug": "blue"})
	h.mustRun("dock/dock/create", ann, map[string]any{"slug": "pier", "fleet": "blue"})
	// Ann made it, so her harbormaster role is her own, not the fleet's to take away.
	if through := h.stored("dock_docker", one.Key("pier", annID, one.Key("pier", "harbormaster")))["through"]; through != nil && through != "" {
		t.Fatalf("the dock's maker holds it through %v", through)
	}

	// Ben isn't in the fleet, so the dock is closed to him.
	h.expect("dock/berth/create", benToken, map[string]any{"dock": "pier", "title": "one"}, 403, "you don't have permission to do this")

	// Made an admiral, he's harbormaster of the fleet's dock, marked as coming from it.
	sailor := h.mustRun("fleet/sailor/create", ann, map[string]any{"fleet": "blue", "person": ben, "rank": one.Key("blue", "admiral")})
	h.mustRun("dock/berth/create", benToken, map[string]any{"dock": "pier", "title": "one"})
	given := one.Key("pier", ben, one.Key("pier", "harbormaster"))
	if through := h.stored("dock_docker", given)["through"]; through != sailor {
		t.Fatalf("ben's harbormaster role says it comes from %v, not %s", through, sailor)
	}

	// It's the fleet's to take away, not the dock's.
	h.expect("dock/docker/delete", ann, map[string]any{"id": given}, 400, "this role comes from the fleet, so it's taken away there")

	// A dock made in the fleet afterwards is his too.
	h.mustRun("dock/dock/create", ann, map[string]any{"slug": "quay", "fleet": "blue"})
	h.mustRun("dock/berth/create", benToken, map[string]any{"dock": "quay", "title": "two"})

	// No longer an admiral, he holds nothing in its docks.
	h.mustRun("fleet/sailor/delete", ann, map[string]any{"id": sailor})
	h.expect("dock/berth/create", benToken, map[string]any{"dock": "pier", "title": "three"}, 403, "you don't have permission to do this")
	h.expect("dock/berth/create", benToken, map[string]any{"dock": "quay", "title": "three"}, 403, "you don't have permission to do this")
}

// A lookout holds nothing in the docks, and a dock moved to another fleet is that
// fleet's admirals', no longer the first's.
func TestOnlyTheHoldingRoleGivesAndAProjectMovedChangesHands(t *testing.T) {
	h := start(t)
	_, ann := h.signUp("ann@example.com")
	cat, catToken := h.signUp("cat@example.com")
	danID, dan := h.signUp("dan@example.com")
	h.mustRun("fleet/fleet/create", ann, map[string]any{"slug": "green"})
	h.mustRun("fleet/fleet/create", dan, map[string]any{"slug": "red"})
	h.mustRun("fleet/sailor/create", ann, map[string]any{"fleet": "green", "person": cat, "rank": one.Key("green", "lookout")})
	h.mustRun("dock/dock/create", ann, map[string]any{"slug": "jetty", "fleet": "green"})
	h.expect("dock/berth/create", catToken, map[string]any{"dock": "jetty", "title": "one"}, 403, "you don't have permission to do this")

	// Only an admiral of red brings a dock into it: Ann isn't one.
	h.expect("dock/dock/update", ann, map[string]any{"id": "jetty", "fleet": "red"}, 400, "only an admiral of that fleet brings a dock into it")

	// Cat is an admiral of red, and Dan, red's admiral, is made the dock's harbormaster
	// by Ann; he brings it into red.
	h.mustRun("fleet/sailor/create", dan, map[string]any{"fleet": "red", "person": cat, "rank": one.Key("red", "admiral")})
	h.mustRun("dock/docker/create", ann, map[string]any{"dock": "jetty", "person": danID, "role": one.Key("jetty", "harbormaster")})
	h.mustRun("dock/dock/update", dan, map[string]any{"id": "jetty", "fleet": "red"})
	h.mustRun("dock/berth/create", catToken, map[string]any{"dock": "jetty", "title": "two"})
}
