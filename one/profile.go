// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"context"
	"reflect"
	"time"

	"cloud.google.com/go/firestore"
	"firebase.google.com/go/v4/auth"
)

// Profile is what a view can show about a person: the name and picture their
// sign-in gives. It's kept in users/{id}, beside the role a person is given, and
// a field of type user reads through it, as in member.name.
type Profile struct {
	Record
	Name    string `firestore:"name"`
	Picture string `firestore:"picture"`
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
// Only name and picture are written, so a person's role is left as it is.
func (a *App) remember(ctx context.Context, token *auth.Token) {
	name, _ := token.Claims["name"].(string)
	picture, _ := token.Claims["picture"].(string)
	seen := name + "\x00" + picture
	if last, ok := a.profiles.Load(token.UID); ok && last == seen {
		return
	}
	ref := a.store.Collection("users").Doc(token.UID)
	var before map[string]any
	if snap, err := ref.Get(ctx); err == nil {
		before = snap.Data()
		if before["name"] == name && before["picture"] == picture {
			a.profiles.Store(token.UID, seen)
			return
		}
	}
	now := time.Now().UTC()
	update := map[string]any{"id": token.UID, "name": name, "picture": picture, "updated_at": now, "updated_by": token.UID}
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
