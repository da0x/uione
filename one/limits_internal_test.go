// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"context"
	"errors"
	"fmt"
	"os"
	"strings"
	"testing"
	"time"
)

// clock is a time a test moves by hand.
type clock struct{ at time.Time }

func (c *clock) now() time.Time                { return c.at }
func (c *clock) pass(d time.Duration)          { c.at = c.at.Add(d) }
func limitsAt(c *clock) *limits                { l := newLimits(); l.now = c.now; return l }
func takes(l *limits, key string, r rate) bool { ok, _ := l.take(key, r); return ok }

func TestACallerMayDoItsBurstAtOnceThenItsRate(t *testing.T) {
	c := &clock{at: time.Unix(1_000_000, 0)}
	l := limitsAt(c)
	r := rate{perSecond: 2, burst: 5}
	for i := range 5 {
		if !takes(l, "ada", r) {
			t.Fatalf("the burst allows 5 at once, refused the %dth", i+1)
		}
	}
	ok, wait := l.take("ada", r)
	if ok || wait != 500*time.Millisecond {
		t.Fatalf("the 6th waits half a second at 2 a second: %v %v", ok, wait)
	}
	c.pass(500 * time.Millisecond)
	if !takes(l, "ada", r) {
		t.Fatal("half a second later there's one more")
	}
	if takes(l, "ada", r) {
		t.Fatal("and only one")
	}
}

func TestOneCallerPastItsLimitLeavesOthersAlone(t *testing.T) {
	c := &clock{at: time.Unix(1_000_000, 0)}
	l := limitsAt(c)
	r := rate{perSecond: 1, burst: 1}
	takes(l, "ada", r)
	if takes(l, "ada", r) {
		t.Fatal("ada has used hers")
	}
	if !takes(l, "grace", r) {
		t.Fatal("grace has her own")
	}
}

func TestACallerWhoHasGoneTakesNoMemory(t *testing.T) {
	c := &clock{at: time.Unix(1_000_000, 0)}
	l := limitsAt(c)
	takes(l, "ada", askingRate)
	c.pass(10 * time.Second)
	takes(l, "grace", askingRate)
	c.pass(time.Minute)
	takes(l, "linus", askingRate)
	if _, kept := l.buckets["ada"]; kept {
		t.Fatal("ada's bucket is forgotten once it has filled again")
	}
	if _, kept := l.buckets["grace"]; kept {
		t.Fatal("so is grace's")
	}
	if _, kept := l.buckets["linus"]; !kept {
		t.Fatal("linus's is still in use")
	}
}

func TestARefusalSaysHowLongToWait(t *testing.T) {
	f := slowDown("making things", 1200*time.Millisecond)
	if f.Status != 429 || f.RetryAfter != 2*time.Second || f.Message != "you're making things too fast; wait 2 seconds and try again" {
		t.Fatalf("got %d %v %q", f.Status, f.RetryAfter, f.Message)
	}
	if f := slowDown("asking", time.Millisecond); f.Message != "you're asking too fast; wait 1 second and try again" {
		t.Fatalf("a moment is said as a second: %q", f.Message)
	}
}

type jotting struct {
	Record
	Title string `firestore:"title" one:"required"`
}

func TestMakingMoreThanADayAllowsIsRefusedUntilTomorrow(t *testing.T) {
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
	a.madePerDay = 3
	who := fmt.Sprint("ada-", time.Now().UnixNano())
	jot := func(by string) error {
		_, err := a.reg.commands["pad::jotting::create"](a, &call{ctx: ctx, me: by, input: map[string]any{"title": "a note"}})
		return err
	}
	for i := range 3 {
		if err := jot(who); err != nil {
			t.Fatalf("the %dth note of the day is allowed: %v", i+1, err)
		}
	}
	err = jot(who)
	var refused *Failure
	if !errors.As(err, &refused) || refused.Status != 429 || refused.RetryAfter <= 0 || refused.RetryAfter > 24*time.Hour ||
		!strings.HasPrefix(refused.Message, "you've made 3 of these today, the most a day allows; you can make more in ") {
		t.Fatalf("the 4th is refused until tomorrow: %v", err)
	}
	if err := jot(who + "-grace"); err != nil {
		t.Fatalf("someone else's count is their own: %v", err)
	}
	// The backend's own code isn't counted.
	if _, err := a.reg.commands["pad::jotting::create"](a, &call{ctx: ctx, me: who, system: true, input: map[string]any{"title": "by the system"}}); err != nil {
		t.Fatalf("the system makes what it must: %v", err)
	}
	// Each day's count is forgotten after it.
	day := time.Now().UTC().Truncate(24 * time.Hour)
	snap, err := a.store.Collection("made_per_day").Doc("pad_jotting." + who + "." + day.Format("2006-01-02")).Get(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if expires, _ := snap.Data()["expires"].(time.Time); !expires.Equal(day.Add(72 * time.Hour)) {
		t.Fatalf("the count expires at %v", snap.Data()["expires"])
	}
}

func TestAWaitIsSaidInHoursOrMinutes(t *testing.T) {
	for wait, said := range map[time.Duration]string{
		30 * time.Second:             "a minute",
		90 * time.Second:             "2 minutes",
		5*time.Hour + 10*time.Minute: "6 hours",
		time.Hour:                    "an hour",
	} {
		if got := hoursSaid(wait); got != said {
			t.Errorf("%v is said %q, not %q", wait, got, said)
		}
	}
}
