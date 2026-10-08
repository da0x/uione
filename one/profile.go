// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"reflect"
	"strings"
	"time"

	"cloud.google.com/go/firestore"
	"firebase.google.com/go/v4/auth"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// Profile is what a view can show about a person: the name and picture their
// sign-in gives. It's kept in users/{id}, beside the role a person is given, and
// a field of type user reads through it, as in member.name.
type Profile struct {
	Record
	Name    string `firestore:"name"`
	Picture string `firestore:"picture"`
	// Username is the person's GitHub login when they signed in with GitHub, like
	// da0x, so an address can name them the way GitHub does.
	Username string `firestore:"username"`
}

// githubLogin asks GitHub for the login of the account with a numeric id, which is
// all a sign-in token carries. Tests replace it.
var githubLogin = func(ctx context.Context, id string) (string, error) {
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, "https://api.github.com/user/"+id, nil)
	if err != nil {
		return "", err
	}
	request.Header.Set("Accept", "application/vnd.github+json")
	response, err := (&http.Client{Timeout: 10 * time.Second}).Do(request)
	if err != nil {
		return "", err
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		return "", fmt.Errorf("GitHub answered %s for account %s", response.Status, id)
	}
	var account struct {
		Login string `json:"login"`
	}
	if err := json.NewDecoder(response.Body).Decode(&account); err != nil {
		return "", err
	}
	return account.Login, nil
}

// githubEmails asks GitHub, with the access a person gave it when signing in, which
// account that access is for, by its numeric id, and the email addresses it has
// verified. The access is used for this and kept nowhere. Tests replace it.
var githubEmails = func(ctx context.Context, access string) (string, []string, error) {
	ask := func(path string, into any) error {
		request, err := http.NewRequestWithContext(ctx, http.MethodGet, "https://api.github.com"+path, nil)
		if err != nil {
			return err
		}
		request.Header.Set("Accept", "application/vnd.github+json")
		request.Header.Set("Authorization", "Bearer "+access)
		response, err := (&http.Client{Timeout: 10 * time.Second}).Do(request)
		if err != nil {
			return err
		}
		defer response.Body.Close()
		if response.StatusCode != http.StatusOK {
			return fmt.Errorf("GitHub answered %s for %s", response.Status, path)
		}
		return json.NewDecoder(response.Body).Decode(into)
	}
	var account struct {
		ID int64 `json:"id"`
	}
	if err := ask("/user", &account); err != nil {
		return "", nil, err
	}
	var listed []struct {
		Email    string `json:"email"`
		Verified bool   `json:"verified"`
	}
	if err := ask("/user/emails", &listed); err != nil {
		return "", nil, err
	}
	var verified []string
	for _, e := range listed {
		if e.Verified && e.Email != "" {
			verified = append(verified, strings.ToLower(e.Email))
		}
	}
	return fmt.Sprint(account.ID), verified, nil
}

// usernameOf reads a person's username from their profile. Something that starts
// with it, like a project named for its owner, can't be made without one.
func (a *App) usernameOf(ctx context.Context, person string) (string, error) {
	if person == "" {
		return "", &Failure{Status: 401, Message: "sign in to do this"}
	}
	snap, err := a.store.Collection("users").Doc(person).Get(ctx)
	if err != nil && status.Code(err) != codes.NotFound {
		return "", err
	}
	username := ""
	if snap != nil && snap.Exists() {
		username, _ = snap.Data()["username"].(string)
	}
	if username == "" {
		return "", &Failure{Status: 400, Message: "this needs your GitHub username; sign in with GitHub"}
	}
	return username, nil
}

// githubID is the numeric GitHub account a sign-in came from, if it came from GitHub.
func githubID(token *auth.Token) string {
	ids, _ := token.Firebase.Identities["github.com"].([]any)
	if len(ids) == 0 {
		return ""
	}
	return fmt.Sprint(ids[0])
}

// profiles is the built-in entity people are, named user in every module.
func profiles() *schema {
	s := schemaOf(reflect.TypeFor[Profile](), "user")
	s.collection = "users"
	return s
}

// remember keeps a person's profile up to date with what their sign-in says,
// whenever they run a command. It writes only when something changed, and then
// every view that shows the person is rebuilt, so a new name appears everywhere.
// Only name, picture and username are written, so a person's role is left as it is.
// A GitHub username is asked of GitHub once, and kept.
func (a *App) remember(ctx context.Context, token *auth.Token) {
	name, _ := token.Claims["name"].(string)
	picture, _ := token.Claims["picture"].(string)
	github := githubID(token)
	seen := name + "\x00" + picture + "\x00" + github
	if last, ok := a.profiles.Load(token.UID); ok && last == seen {
		return
	}
	ref := a.store.Collection("users").Doc(token.UID)
	var before map[string]any
	username := ""
	if snap, err := ref.Get(ctx); err == nil {
		before = snap.Data()
		username, _ = before["username"].(string)
		if before["name"] == name && before["picture"] == picture && (github == "" || username != "") {
			a.profiles.Store(token.UID, seen)
			return
		}
	}
	if github != "" && username == "" {
		login, err := githubLogin(ctx, github)
		if err != nil {
			a.log.Printf("one: the GitHub username of %s wasn't found: %v", token.UID, err)
		}
		username = login
	}
	now := time.Now().UTC()
	update := map[string]any{"id": token.UID, "name": name, "picture": picture, "updated_at": now, "updated_by": token.UID}
	if username != "" {
		update["username"] = username
	}
	if _, err := ref.Set(ctx, update, firestore.MergeAll); err != nil {
		a.log.Printf("one: the profile of %s wasn't saved: %v", token.UID, err)
		return
	}
	a.profiles.Store(token.UID, seen)
	after := map[string]any{}
	for k, v := range before {
		after[k] = v
	}
	for k, v := range update {
		after[k] = v
	}
	a.publish(ctx, event{Type: "user.updated", Entity: "user", ID: token.UID, Version: now.UnixNano(), Before: before, After: after})
}
