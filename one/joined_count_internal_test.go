// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"context"
	"os"
	"reflect"
	"testing"
)

type pantry struct {
	Record
	Name string `firestore:"name" one:"required,key"`
	Open bool   `firestore:"open"`
}

type tag struct {
	Record
	Name  string `firestore:"name" one:"required,key"`
	Fresh bool   `firestore:"fresh"`
}

type jar struct {
	Record
	Name   string `firestore:"name" one:"required,key"`
	Pantry string `firestore:"pantry" one:"refers=shelves::pantry"`
	Label  string `firestore:"label" one:"refers=shelves::tag"`
}

// A count through what two fields point at keeps to both, like a milestone's issues
// at a stage that's a success on a public board: the jars in open pantries with fresh
// labels, not every jar in an open pantry.
func TestACountThroughTwoFieldsKeepsToBoth(t *testing.T) {
	if os.Getenv("FIRESTORE_EMULATOR_HOST") == "" {
		os.Setenv("FIRESTORE_EMULATOR_HOST", "localhost:8080")
		os.Setenv("FIREBASE_AUTH_EMULATOR_HOST", "localhost:9099")
	}
	ctx := context.Background()
	a, err := New(ctx, Module("shelves", Command[pantry]("pantry::create"), Command[tag]("tag::create"), Command[jar]("jar::create"), View("jars").Public().
		Count("both", Where[jar]("pantry.open", true).And("label.fresh", true)).
		Count("open", Where[jar]("pantry.open", true))))
	if err != nil {
		t.Fatal(err)
	}
	defer a.Close()
	collection := func(e any) string { return a.reg.schemas[reflect.TypeOf(e)].collection }
	pantries, tags, jars := collection(pantry{}), collection(tag{}), collection(jar{})
	for _, c := range []string{pantries, tags, jars} {
		docs, _ := a.store.Collection(c).Documents(ctx).GetAll()
		for _, d := range docs {
			d.Ref.Delete(ctx)
		}
	}
	put := func(collection, id string, data map[string]any) {
		if _, err := a.store.Collection(collection).Doc(id).Set(ctx, data); err != nil {
			t.Fatal(err)
		}
	}
	put(pantries, "p1", map[string]any{"name": "p1", "open": true})
	put(pantries, "p2", map[string]any{"name": "p2", "open": false})
	put(tags, "l1", map[string]any{"name": "l1", "fresh": true})
	put(tags, "l2", map[string]any{"name": "l2", "fresh": false})
	put(jars, "j1", map[string]any{"name": "j1", "pantry": "p1", "label": "l1"})
	put(jars, "j2", map[string]any{"name": "j2", "pantry": "p1", "label": "l2"})
	put(jars, "j3", map[string]any{"name": "j3", "pantry": "p2", "label": "l1"})
	v := a.reg.views[0]
	for _, c := range v.counts {
		n, err := a.count(ctx, c.query, "")
		if err != nil {
			t.Fatal(err)
		}
		want := map[string]int64{"both": 1, "open": 2}[c.name]
		if n != want {
			t.Fatalf("%s: counted %d, not %d", c.name, n, want)
		}
	}
}
