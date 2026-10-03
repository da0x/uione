// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"context"
	"net/http"
	"testing"

	"github.com/da0x/uione/one"
)

// A repository keyed by its owner's username and its name, the way GitHub names
// them, so its id reads da0x-neotrac and nobody can make one in another's name.
type Repository struct {
	one.Record
	Owner string `firestore:"owner" one:"key,default=me.username"`
	Slug  string `firestore:"slug" one:"required,key"`
}

var repositories = one.Module("repos",
	one.Command[Repository]("repository::create").Allow(one.SignedIn),
)

func TestAnEntityKeyedByItsOwnersUsernameIsMadeInTheirNameOnly(t *testing.T) {
	h := start(t)
	ada, token := h.signUp("ada@example.com")
	h.expect("repos/repository/create", token, map[string]any{"slug": "neotrac"}, http.StatusBadRequest,
		"this needs your GitHub username; sign in with GitHub")

	if _, err := h.store.Collection("users").Doc(ada).Set(context.Background(), map[string]any{"username": "da0x"}); err != nil {
		t.Fatal(err)
	}
	id := h.mustRun("repos/repository/create", token, map[string]any{"slug": "neotrac", "owner": "someone-else"})
	if id != "da0x-neotrac" {
		t.Fatalf("the repository is %s", id)
	}
	if stored := h.stored("repos_repository", id); stored["owner"] != "da0x" {
		t.Errorf("the repository is owned by %v", stored["owner"])
	}
}
