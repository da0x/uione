// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"log"
	"os"
	"strings"
	"testing"
	"time"

	"firebase.google.com/go/v4/auth"
)

// Against the emulator, the Admin SDK's own check of a sign-in already asks whether
// the account is disabled, every time; in production it doesn't. So this is tested
// here, by asking turnedOff, rather than through a request.
func TestAPersonTurnedOffInFirebaseIsRefusedWithinAMinute(t *testing.T) {
	if os.Getenv("FIRESTORE_EMULATOR_HOST") == "" {
		os.Setenv("FIRESTORE_EMULATOR_HOST", "localhost:8080")
		os.Setenv("FIREBASE_AUTH_EMULATOR_HOST", "localhost:9099")
	}
	ctx := context.Background()
	a, err := New(ctx, Module("pad", Command[jotting]("jotting::create").Allow(SignedIn)))
	if err != nil {
		t.Fatal(err)
	}
	defer a.Close()
	c := &clock{at: time.Now()}
	a.disabled.now = c.now
	who := fmt.Sprint("mallory-", time.Now().UnixNano())
	if _, err := a.auth.CreateUser(ctx, (&auth.UserToCreate{}).UID(who)); err != nil {
		t.Fatal(err)
	}
	if err := a.turnedOff(ctx, who); err != nil {
		t.Fatalf("a person nobody turned off is let in: %v", err)
	}
	if _, err := a.auth.UpdateUser(ctx, who, (&auth.UserToUpdate{}).Disabled(true)); err != nil {
		t.Fatal(err)
	}
	if err := a.turnedOff(ctx, who); err != nil {
		t.Fatalf("what Firebase said is believed for a minute: %v", err)
	}
	c.pass(disabledCheckedFor + time.Second)
	var refused *Failure
	if err := a.turnedOff(ctx, who); !errors.As(err, &refused) || refused.Status != 403 || refused.Message != "this account has been turned off" {
		t.Fatalf("a minute later, they're refused: %v", err)
	}
	if _, err := a.auth.UpdateUser(ctx, who, (&auth.UserToUpdate{}).Disabled(false)); err != nil {
		t.Fatal(err)
	}
	c.pass(disabledCheckedFor + time.Second)
	if err := a.turnedOff(ctx, who); err != nil {
		t.Fatalf("turned on again, they're let in again: %v", err)
	}
	// Someone Firebase doesn't know, as when it can't be asked, is let in.
	if err := a.turnedOff(ctx, who+"-nobody"); err != nil {
		t.Fatalf("when Firebase can't say, the person is let in: %v", err)
	}
}

func TestARefusalIsLoggedOnceAMinuteForEachCaller(t *testing.T) {
	var logged bytes.Buffer
	c := &clock{at: time.Now()}
	a := &App{limits: newLimits(), log: log.New(&logged, "", 0)}
	a.limits.now = c.now
	for range 50 {
		a.allow("making", "mallory", rate{perSecond: 1, burst: 1}, "making things")
	}
	a.allow("making", "at 198.51.100.1", rate{perSecond: 1, burst: 0}, "making things")
	c.pass(time.Minute)
	a.allow("making", "mallory", rate{perSecond: 0.001, burst: 1}, "making things")
	a.allow("making", "mallory", rate{perSecond: 0.001, burst: 1}, "making things")
	want := "one: limited mallory making\none: limited at 198.51.100.1 making\none: limited mallory making\n"
	if got := logged.String(); got != want {
		t.Fatalf("logged:\n%s\nwant:\n%s", got, want)
	}
	if strings.Count(logged.String(), "mallory") != 2 {
		t.Fatal("once a minute each")
	}
}
