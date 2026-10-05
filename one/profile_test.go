// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"os"
	"testing"

	"cloud.google.com/go/firestore"

	"github.com/da0x/uione/one"
)

// A small forum: posts that show who wrote them, by name and picture.

type Post struct {
	one.Record
	Text   string `firestore:"text" one:"required"`
	Author string `firestore:"author" one:"default=me,refers=user"`
}

var forum = one.Module("forum",
	one.Command[Post]("post::create").Allow(one.Authenticated),
	one.View("posts").Public().Each(one.All[Post]()).Order("created_at").Fields("text", "author.name", "author.picture", "author.username"),
	// Whoever made something is a person too, without a field naming them.
	one.View("posted").Public().Each(one.All[Post]()).Order("created_at").Fields("text", "created_by.name", "updated_by.picture"),
)

// rename gives a person a new name and picture, and signs them in again for an ID
// token that says so, the way changing them at their sign-in provider would.
func (h *harness) rename(token, email, name, picture string) string {
	h.t.Helper()
	post := func(method string, body map[string]any) map[string]any {
		encoded, _ := json.Marshal(body)
		url := "http://" + os.Getenv("FIREBASE_AUTH_EMULATOR_HOST") + "/identitytoolkit.googleapis.com/v1/accounts:" + method + "?key=any"
		response, err := http.Post(url, "application/json", bytes.NewReader(encoded))
		if err != nil {
			h.t.Fatal(err)
		}
		defer response.Body.Close()
		reply := map[string]any{}
		json.NewDecoder(response.Body).Decode(&reply)
		return reply
	}
	post("update", map[string]any{"idToken": token, "displayName": name, "photoUrl": picture})
	fresh, _ := post("signInWithPassword", map[string]any{"email": email, "password": "password", "returnSecureToken": true})["idToken"].(string)
	if fresh == "" {
		h.t.Fatal("the Auth emulator didn't sign the renamed person in")
	}
	return fresh
}

func TestAPostShowsWhoWroteItByNameAndPicture(t *testing.T) {
	h := start(t)
	ada, token := h.signUp("ada@example.com")
	if _, err := h.store.Collection("users").Doc(ada).Set(context.Background(), map[string]any{"role_id": "writer"}); err != nil {
		t.Fatal(err)
	}
	token = h.rename(token, "ada@example.com", "Ada Lovelace", "https://example.com/ada.png")
	// Her GitHub username, which signing in with GitHub keeps; the Auth emulator
	// can't, so it's written as that would.
	if _, err := h.store.Collection("users").Doc(ada).Set(context.Background(), map[string]any{"username": "ada"}, firestore.MergeAll); err != nil {
		t.Fatal(err)
	}
	h.mustRun("forum/post/create", token, map[string]any{"text": "Hello"})

	row := rows(h.view("forum::posts"))[0]
	if row["author.name"] != "Ada Lovelace" || row["author.picture"] != "https://example.com/ada.png" || row["author.username"] != "ada" {
		t.Fatalf("the post shows its author as %v", row)
	}
	if _, has := row["author"]; has {
		t.Errorf("the post sends its author's id, which it doesn't list: %v", row)
	}
	made := rows(h.view("forum::posted"))[0]
	if made["created_by.name"] != "Ada Lovelace" || made["updated_by.picture"] != "https://example.com/ada.png" {
		t.Errorf("the post shows who made it as %v", made)
	}

	// A new name reaches every post the person wrote, the next time they do
	// anything at all, and leaves the role they were given alone.
	token = h.rename(token, "ada@example.com", "Countess of Lovelace", "https://example.com/ada.png")
	h.mustRun("tracker/tracked/create", token, map[string]any{"slug": "engine"})
	if got := rows(h.view("forum::posts"))[0]["author.name"]; got != "Countess of Lovelace" {
		t.Errorf("after renaming, the post shows its author as %v", got)
	}
	snap, err := h.store.Collection("users").Doc(ada).Get(context.Background())
	if err != nil || snap.Data()["role_id"] != "writer" {
		t.Errorf("saving the profile changed the person's role: %v %v", snap.Data(), err)
	}
}
