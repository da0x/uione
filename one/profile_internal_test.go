// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"context"
	"net/http"
	"os"
	"testing"

	"firebase.google.com/go/v4/auth"
)

func TestSigningInWithGitHubKeepsTheUsername(t *testing.T) {
	if os.Getenv("FIRESTORE_EMULATOR_HOST") == "" {
		t.Skip("needs the Firestore emulator")
	}
	request, _ := http.NewRequest(http.MethodDelete, "http://"+os.Getenv("FIRESTORE_EMULATOR_HOST")+"/emulator/v1/projects/demo-uione/databases/(default)/documents", nil)
	http.DefaultClient.Do(request)
	ctx := context.Background()
	app, err := New(ctx)
	if err != nil {
		t.Fatal(err)
	}
	defer app.Close()
	asked := 0
	githubLogin = func(_ context.Context, id string) (string, error) {
		asked++
		if id != "583231" {
			t.Errorf("GitHub was asked about account %s", id)
		}
		return "octocat", nil
	}
	token := &auth.Token{UID: "u1", Claims: map[string]any{"name": "The Octocat", "picture": "https://example.com/o.png"},
		Firebase: auth.FirebaseInfo{Identities: map[string]any{"github.com": []any{"583231"}}}}
	app.remember(ctx, token)
	snap, err := app.store.Collection("users").Doc("u1").Get(ctx)
	if err != nil || snap.Data()["username"] != "octocat" || snap.Data()["name"] != "The Octocat" {
		t.Fatalf("the profile is %v (%v)", snap.Data(), err)
	}
	// Signing in again doesn't ask GitHub again, even on a fresh start.
	app.profiles.Delete("u1")
	app.remember(ctx, token)
	if asked != 1 {
		t.Errorf("GitHub was asked %d times", asked)
	}
	// Someone who signed in with Google has no username, and GitHub isn't asked.
	app.remember(ctx, &auth.Token{UID: "u2", Claims: map[string]any{"name": "Ada"}})
	snap, _ = app.store.Collection("users").Doc("u2").Get(ctx)
	if _, has := snap.Data()["username"]; has || asked != 1 {
		t.Errorf("a Google sign-in gave %v, and GitHub was asked %d times", snap.Data(), asked)
	}
}
