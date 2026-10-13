// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"context"
	"os"
	"reflect"
	"testing"
)

type thing struct {
	Record
	Name string `firestore:"name"`
}

// Events can arrive late or twice. A view only moves forward, and a view whose data
// hasn't changed isn't written again.
func TestAViewNeverGoesBackwards(t *testing.T) {
	if os.Getenv("FIRESTORE_EMULATOR_HOST") == "" {
		os.Setenv("FIRESTORE_EMULATOR_HOST", "localhost:8080")
		os.Setenv("FIREBASE_AUTH_EMULATOR_HOST", "localhost:9099")
	}
	ctx := context.Background()
	a, err := New(ctx, Module("versions", View("things").Public().Count("total", All[thing]())))
	if err != nil {
		t.Fatal(err)
	}
	defer a.Close()
	v := a.reg.views[0]
	read := func() map[string]any {
		snap, err := a.store.Collection("views").Doc("versions::things").Get(ctx)
		if err != nil {
			t.Fatal(err)
		}
		return snap.Data()
	}
	// New wrote the view at the current time; start again from a known version.
	a.store.Collection("views").Doc("versions::things").Delete(ctx)

	steps := []struct {
		version int64
		total   int64
		writes  bool
		why     string
	}{
		{10, 1, true, "the first version is written"},
		{5, 2, false, "an older event doesn't overwrite a newer view"},
		{10, 2, false, "the same event twice is applied once"},
		{20, 1, false, "a newer event with the same data doesn't write again"},
		{30, 3, true, "a newer event with new data is written"},
	}
	for _, step := range steps {
		wrote, err := a.write(ctx, v, "", map[string]any{"total": step.total}, step.version, "test")
		if err != nil {
			t.Fatal(err)
		}
		if wrote != step.writes {
			t.Fatalf("%s: wrote %v", step.why, wrote)
		}
	}
	if doc := read(); doc["total"] != int64(3) || doc["source_version"] != int64(30) {
		t.Fatalf("the view ends at the newest data: %v", doc)
	}
}

type club struct {
	Record
	Slug string `firestore:"slug" one:"required,key"`
}

type place struct {
	Record
	Club   string `firestore:"club" one:"required,key,refers=held::club"`
	Person string `firestore:"person" one:"required,key,refers=user"`
	Role   string `firestore:"role" one:"choices=chair"`
}

// A change to who belongs to a club rebuilds that club's pages, not every club's.
func TestAChangeOfMembersRebuildsOnlyThatClubsPages(t *testing.T) {
	if os.Getenv("FIRESTORE_EMULATOR_HOST") == "" {
		os.Setenv("FIRESTORE_EMULATOR_HOST", "localhost:8080")
		os.Setenv("FIREBASE_AUTH_EMULATOR_HOST", "localhost:9099")
	}
	ctx := context.Background()
	a, err := New(ctx, Module("held",
		Role("chair").Per(Entity[club](), Entity[place]()),
		View("page").Per(Entity[club]()).Readers(Entity[place]()).Copy("slug", "slug"),
	))
	if err != nil {
		t.Fatal(err)
	}
	defer a.Close()
	var page *ViewSpec
	for _, v := range a.reg.views {
		page = v
	}
	for _, slug := range []string{"chess", "rowing"} {
		if _, err := a.write(ctx, page, slug, map[string]any{"slug": slug, "within": slug}, 1, "start"); err != nil {
			t.Fatal(err)
		}
	}
	subjects, err := page.subjects(ctx, a, event{Entity: "held::place", ID: "chess-ada", After: map[string]any{"club": "chess", "person": "ada"}})
	if err != nil {
		t.Fatal(err)
	}
	if len(subjects) != 1 || subjects[0] != "chess" {
		t.Errorf("seating someone in chess rebuilds %v", subjects)
	}
}

type shelf struct {
	Record
	Title string `firestore:"title"`
}

type note struct {
	Record
	Shelf string `firestore:"shelf" one:"refers=narrow::shelf"`
	Title string `firestore:"title"`
}

type remark struct {
	Record
	Note   string `firestore:"note" one:"refers=narrow::note"`
	Author string `firestore:"author" one:"refers=user"`
}

// A change to what pages only show, like the title of the shelf a note is on or the
// name of who wrote a remark on it, rebuilds the pages showing it, not every note's;
// and someone who's just signed up is shown by none yet.
func TestAChangeToWhatPagesShowRebuildsOnlyThosePages(t *testing.T) {
	if os.Getenv("FIRESTORE_EMULATOR_HOST") == "" {
		os.Setenv("FIRESTORE_EMULATOR_HOST", "localhost:8080")
		os.Setenv("FIREBASE_AUTH_EMULATOR_HOST", "localhost:9099")
	}
	ctx := context.Background()
	a, err := New(ctx, Module("narrow",
		Command[shelf]("shelf::update").Allow(SignedIn),
		Command[note]("note::update").Allow(SignedIn),
		Command[remark]("remark::create").Allow(SignedIn),
		View("note_page").Per(Entity[note]()).Copy("shelf.title", "shelf.title").
			List("remarks", Where[remark]("note", Subject)).Fields("author.name"),
	))
	if err != nil {
		t.Fatal(err)
	}
	defer a.Close()
	page := a.reg.views[0]
	put := func(kind reflect.Type, id string, data map[string]any) {
		if _, err := a.store.Collection(a.reg.schemas[kind].collection).Doc(id).Set(ctx, data); err != nil {
			t.Fatal(err)
		}
	}
	put(reflect.TypeFor[shelf](), "front", map[string]any{"title": "Front"})
	put(reflect.TypeFor[shelf](), "back", map[string]any{"title": "Back"})
	put(reflect.TypeFor[note](), "milk", map[string]any{"shelf": "front", "title": "Milk"})
	put(reflect.TypeFor[note](), "eggs", map[string]any{"shelf": "back", "title": "Eggs"})
	put(reflect.TypeFor[remark](), "milk-1", map[string]any{"note": "milk", "author": "ada-narrow"})
	put(reflect.TypeFor[remark](), "eggs-1", map[string]any{"note": "eggs", "author": "bo-narrow"})
	for _, id := range []string{"milk", "eggs"} {
		if _, err := a.write(ctx, page, id, map[string]any{"title": id}, 1, "start"); err != nil {
			t.Fatal(err)
		}
	}
	cases := []struct {
		ev   event
		want []string
		why  string
	}{
		{event{Entity: "narrow::shelf", ID: "front", Before: map[string]any{"title": "Front"}, After: map[string]any{"title": "Fridge door"}}, []string{"milk"},
			"renaming the front shelf rebuilds the page of the note on it"},
		{event{Entity: "user", ID: "ada-narrow", Before: map[string]any{"name": "Ada"}, After: map[string]any{"name": "Ada L."}}, []string{"milk"},
			"renaming Ada rebuilds the page of the note she remarked on"},
		{event{Entity: "user", ID: "cy-narrow", After: map[string]any{"name": "Cy"}}, nil,
			"someone signing up rebuilds no note's page"},
	}
	for _, c := range cases {
		subjects, err := page.subjects(ctx, a, c.ev)
		if err != nil {
			t.Fatal(err)
		}
		if !reflect.DeepEqual(subjects, c.want) {
			t.Errorf("%s: rebuilds %v", c.why, subjects)
		}
	}
}

func TestAMentionIsAHashAndANumberStandingAlone(t *testing.T) {
	cases := map[string][]float64{
		"Fix #12":                  {12},
		"#1, #2 and #1 again":      {1, 2},
		"(#7)":                     {7},
		"see da0x/other#3":         nil,
		"https://example.com/#4":   nil,
		"&#5;":                     nil,
		"##6":                      nil,
		"#0 and #12a":              nil,
		"line one\n#8 on line two": {8},
	}
	for text, want := range cases {
		if got := mentions(text); !reflect.DeepEqual(got, want) {
			t.Errorf("mentions(%q) = %v, want %v", text, got, want)
		}
	}
}

func TestMistakesInDeclaringAModuleStopItStarting(t *testing.T) {
	refused := func(name string, item Item) {
		t.Helper()
		defer func() {
			if recover() == nil {
				t.Errorf("%s was accepted", name)
			}
		}()
		item.register(&registry{commands: map[string]func(*App, *call) (string, error){}, roles: map[string][]string{}, schemas: map[reflect.Type]*schema{}}, "checks")
	}
	refused("a command named on another entity", Command[thing]("other::create"))
	refused("a view with a value called public", View("page").Public().Count("public", All[thing]()))
}

// A command's changes rebuild each page they touch once, however many touch it: a
// project made with its board, stages and steps rebuilds its page once, not once
// for each of them.
func TestACommandsChangesRebuildEachPageOnce(t *testing.T) {
	if os.Getenv("FIRESTORE_EMULATOR_HOST") == "" {
		os.Setenv("FIRESTORE_EMULATOR_HOST", "localhost:8080")
		os.Setenv("FIREBASE_AUTH_EMULATOR_HOST", "localhost:9099")
	}
	ctx := context.Background()
	a, err := New(ctx, Module("held",
		Role("chair").Per(Entity[club](), Entity[place]()),
		View("page").Per(Entity[club]()).Readers(Entity[place]()).Copy("slug", "slug"),
	))
	if err != nil {
		t.Fatal(err)
	}
	defer a.Close()
	// Five people seated, three in chess and two in rowing.
	seated := func(club, person string) event {
		return event{Type: "place.updated", Entity: "held::place", ID: club + "-" + person, After: map[string]any{"club": club, "person": person}}
	}
	before := a.rebuilt.Load()
	a.publish(ctx, seated("chess", "ada"), seated("chess", "bo"), seated("rowing", "cy"), seated("chess", "di"), seated("rowing", "ed"))
	if rebuilt := a.rebuilt.Load() - before; rebuilt != 2 {
		t.Fatalf("five changes to two clubs rebuilt their pages %d times", rebuilt)
	}
}
