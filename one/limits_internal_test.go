// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
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
