// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

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
