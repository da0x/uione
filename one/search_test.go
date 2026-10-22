// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"context"
	"encoding/json"
	"net/http"
	"net/url"
	"reflect"
	"testing"

	"github.com/da0x/uione/one"
)

// Guides of leaves, each written by its guide's writers, and found by the words in
// its body, which no page lists.

type Guide struct {
	one.Record
	Slug       string `firestore:"slug" one:"required,unique,key"`
	Visibility string `firestore:"visibility" one:"choices=public|private,default=public"`
}

type Writer struct {
	one.Record
	Guide  string `firestore:"guide" one:"required,key,refers=guide::guide"`
	Person string `firestore:"person" one:"required,key,refers=user"`
	Role   string `firestore:"role" one:"choices=writer,default=writer"`
}

type Leaf struct {
	one.Record
	Guide string `firestore:"guide" one:"required,refers=guide::guide"`
	Title string `firestore:"title" one:"required"`
	Body  string `firestore:"body" one:"searchable"`
}

var guides = one.Module("guide",
	one.Command[Guide]("guide::create").Allow(one.SignedIn).Do(func(c *one.Ctx, g *Guide) error {
		return one.Create(c, &Writer{Guide: g.ID, Person: c.Me()})
	}),
	one.Command[Leaf]("leaf::create"),
	one.Command[Leaf]("leaf::update"),
	one.Command[Leaf]("leaf::delete"),
	one.Role("writer", "leaf:create", "leaf:update", "leaf:delete").Per(one.Entity[Guide](), one.Entity[Writer]()),
	one.View("guide_page").Per(one.Entity[Guide]()).Readers(one.Entity[Writer]()).PublicWhen("visibility", "public").
		List("leaves", one.Where[Leaf]("guide", one.Subject)).Fields("title"),
)

func init() { modules = append(modules, guides) }

// searched asks for a guide's leaves holding some words, and gives the status and the
// titles found.
func (h *harness) searched(token, guide, list, words string) (int, []string) {
	h.t.Helper()
	asked := url.Values{"view": {"guide::guide_page"}, "id": {guide}, "list": {list}, "words": {words}}
	request, _ := http.NewRequest(http.MethodGet, h.server.URL+"/api/search?"+asked.Encode(), nil)
	if token != "" {
		request.Header.Set("Authorization", "Bearer "+token)
	}
	response, err := http.DefaultClient.Do(request)
	if err != nil {
		h.t.Fatal(err)
	}
	defer response.Body.Close()
	var reply struct {
		Rows []map[string]any `json:"rows"`
	}
	json.NewDecoder(response.Body).Decode(&reply)
	titles := []string{}
	for _, row := range reply.Rows {
		if _, shown := row["body"]; shown {
			h.t.Errorf("a row found shows its body, which the list doesn't: %v", row)
		}
		titles = append(titles, row["title"].(string))
	}
	return response.StatusCode, titles
}

func TestALeafKeepsTheWordsOfItsBodyAndIsFoundByThem(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	h.mustRun("guide/guide/create", token, map[string]any{"slug": "kitchen"})
	cafe := h.mustRun("guide/leaf/create", token, map[string]any{"guide": "kitchen", "title": "Coffee", "body": "The Café's **espresso**, a déjà vu: espresso again."})
	h.mustRun("guide/leaf/create", token, map[string]any{"guide": "kitchen", "title": "Tea", "body": "Green tea, brewed at 80 degrees, never espresso."})
	h.mustRun("guide/leaf/create", token, map[string]any{"guide": "kitchen", "title": "Bread", "body": "Flour, water and salt."})

	// Once each, lowercased, accents folded, and a single letter left out.
	if words := h.mustHave("guide_leaf", cafe)["search_words"]; !reflect.DeepEqual(words, []any{"again", "cafe", "deja", "espresso", "the", "vu"}) {
		t.Fatalf("the leaf keeps the words %v", words)
	}
	for words, want := range map[string][]string{
		"espresso":       {"Tea", "Coffee"},
		"ESPRESSO café":  {"Coffee"},
		"espresso salt":  {},
		"flour":          {"Bread"},
		"espress":        {}, // whole words only
		"a":              {},
		"":               {},
		"  80 degrees  ": {"Tea"},
	} {
		if status, titles := h.searched("", "kitchen", "leaves", words); status != http.StatusOK || !reflect.DeepEqual(titles, want) {
			t.Errorf("searching %q found %d %v, want %v", words, status, titles, want)
		}
	}

	// A body changed is found by its new words, and not its old.
	h.mustRun("guide/leaf/update", token, map[string]any{"id": cafe, "body": "Cold brew."})
	if _, titles := h.searched("", "kitchen", "leaves", "cafe"); len(titles) != 0 {
		t.Errorf("the leaf is still found by a word it lost: %v", titles)
	}
	if _, titles := h.searched("", "kitchen", "leaves", "brew"); !reflect.DeepEqual(titles, []string{"Coffee"}) {
		t.Errorf("the leaf isn't found by its new words: %v", titles)
	}
	if status, _ := h.searched(token, "kitchen", "nothing", "brew"); status != http.StatusNotFound {
		t.Errorf("a list the view hasn't was searched: %d", status)
	}
}

func TestAPrivateGuideIsSearchedByItsWritersAlone(t *testing.T) {
	h := start(t)
	_, adaToken := h.signUp("ada@example.com")
	_, graceToken := h.signUp("grace@example.com")
	h.mustRun("guide/guide/create", adaToken, map[string]any{"slug": "diary", "visibility": "private"})
	h.mustRun("guide/leaf/create", adaToken, map[string]any{"guide": "diary", "title": "Monday", "body": "A secret."})
	for who, token := range map[string]string{"someone signed out": "", "someone else": graceToken} {
		if status, titles := h.searched(token, "diary", "leaves", "secret"); status != http.StatusForbidden || len(titles) != 0 {
			t.Errorf("%s searching a private guide got %d %v", who, status, titles)
		}
	}
	if status, titles := h.searched(adaToken, "diary", "leaves", "secret"); status != http.StatusOK || !reflect.DeepEqual(titles, []string{"Monday"}) {
		t.Errorf("its writer searching it got %d %v", status, titles)
	}
}

func TestLeavesStoredBeforeTheirBodyWasSearchedGetTheirWordsOnce(t *testing.T) {
	h := start(t)
	ctx := context.Background()
	_, token := h.signUp("ada@example.com")
	h.mustRun("guide/guide/create", token, map[string]any{"slug": "kitchen"})
	// As a backend from before kept it.
	if _, err := h.store.Collection("guide_leaf").Doc("old").Set(ctx, map[string]any{"guide": "kitchen", "title": "Soup", "body": "Leeks and potatoes."}); err != nil {
		t.Fatal(err)
	}
	if _, err := h.store.Collection("search_definitions").Doc("guide_leaf").Delete(ctx); err != nil {
		t.Fatal(err)
	}
	app, err := one.New(ctx, modules...)
	if err != nil {
		t.Fatal(err)
	}
	app.Close()
	if words := h.mustHave("guide_leaf", "old")["search_words"]; !reflect.DeepEqual(words, []any{"and", "leeks", "potatoes"}) {
		t.Fatalf("the leaf stored before was given the words %v", words)
	}
}
