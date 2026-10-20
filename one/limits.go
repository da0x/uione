// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"fmt"
	"math"
	"sync"
	"time"

	"cloud.google.com/go/firestore"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// Every person and every service is limited in how fast it may ask, so nobody, and
// no script, can flood a site or fill its database. An app gets this without saying
// anything.
//
// The counts are kept in each instance's memory. A site's backend runs on at most two
// instances, so a caller gets at most twice a limit, and no command costs a read or a
// write to count it.

// rate is how many a caller may do each second, and how many at once.
type rate struct {
	perSecond float64
	burst     float64
}

var (
	// Any command, an upload or a download: 10 a second, and 30 at once.
	askingRate = rate{perSecond: 10, burst: 30}
	// Making something, on top of that: one a second, and 20 at once.
	makingRate = rate{perSecond: 1, burst: 20}
	// Signing in: once every 5 seconds, and 5 at once.
	signingInRate = rate{perSecond: 0.2, burst: 5}
	// A service's key, wherever it's used: 10 a second, and 30 at once.
	keyRate = rate{perSecond: 10, burst: 30}
)

type bucket struct {
	tokens float64
	last   time.Time
}

// limits holds a bucket for each caller and each kind of asking. A bucket that has
// filled up again is forgotten, so callers who have gone take no memory.
type limits struct {
	mu      sync.Mutex
	buckets map[string]*bucket
	now     func() time.Time
	swept   time.Time
}

func newLimits() *limits {
	return &limits{buckets: map[string]*bucket{}, now: time.Now}
}

// take spends one of what the caller may do, or, when there's none left, says how
// long until there is.
func (l *limits) take(key string, r rate) (bool, time.Duration) {
	l.mu.Lock()
	defer l.mu.Unlock()
	now := l.now()
	if now.Sub(l.swept) > time.Minute {
		l.sweep(now)
	}
	b := l.buckets[key]
	if b == nil {
		b = &bucket{tokens: r.burst, last: now}
		l.buckets[key] = b
	}
	b.tokens = min(r.burst, b.tokens+now.Sub(b.last).Seconds()*r.perSecond)
	b.last = now
	if b.tokens < 1 {
		return false, time.Duration((1 - b.tokens) / r.perSecond * float64(time.Second))
	}
	b.tokens--
	return true, 0
}

// sweep forgets every bucket idle long enough to have filled again. The longest
// any rate takes to fill is its burst over its rate, at most half a minute.
func (l *limits) sweep(now time.Time) {
	for key, b := range l.buckets {
		if now.Sub(b.last) > 30*time.Second {
			delete(l.buckets, key)
		}
	}
	l.swept = now
}

// slowDown is the refusal for a caller past a limit: how long to wait, in the
// answer's words and in its Retry-After.
func slowDown(doing string, wait time.Duration) *Failure {
	seconds := int(math.Ceil(wait.Seconds()))
	if seconds < 1 {
		seconds = 1
	}
	unit := "seconds"
	if seconds == 1 {
		unit = "second"
	}
	return &Failure{
		Status:     429,
		Message:    fmt.Sprintf("you're %s too fast; wait %d %s and try again", doing, seconds, unit),
		RetryAfter: time.Duration(seconds) * time.Second,
	}
}

// allow spends one of what the caller may do at this rate, or refuses.
func (a *App) allow(kind, who string, r rate, doing string) error {
	if ok, wait := a.limits.take(kind+":"+who, r); !ok {
		return slowDown(doing, wait)
	}
	return nil
}

// How many of a kind one person or service may make in a day, by default. A script
// that gets past the rates above still can't fill the database.
const madePerDay = 1000

// madeToday counts what the caller has made of this kind today, in UTC, in the
// command's own transaction, and refuses one past the most a day allows. Each day's
// count is its own document, forgotten two days later by Firestore's time to live.
func (a *App) madeToday(tx *firestore.Transaction, s *schema, who string, now time.Time, counters map[string]*counting) error {
	if who == "" {
		return nil
	}
	day := now.UTC().Truncate(24 * time.Hour)
	ref := a.store.Collection("made_per_day").Doc(s.collection + "." + who + "." + day.Format("2006-01-02"))
	counter, read := counters[ref.Path]
	if !read {
		counter = &counting{ref: ref, expires: day.Add(72 * time.Hour)}
		snap, err := tx.Get(ref)
		if err == nil {
			switch last := snap.Data()["last"].(type) {
			case float64:
				counter.value = last
			case int64:
				counter.value = float64(last)
			}
		} else if status.Code(err) != codes.NotFound {
			return err
		}
		counters[ref.Path] = counter
	}
	most := a.madePerDay
	if most == 0 {
		most = madePerDay
	}
	if counter.value >= float64(most) {
		tomorrow := day.Add(24 * time.Hour)
		return &Failure{
			Status:     429,
			Message:    fmt.Sprintf("you've made %d of these today, the most a day allows; you can make more in %s", most, hoursSaid(tomorrow.Sub(now))),
			RetryAfter: tomorrow.Sub(now).Round(time.Second),
		}
	}
	counter.value++
	return nil
}

// hoursSaid is a wait of up to a day, in words.
func hoursSaid(d time.Duration) string {
	if d < time.Hour {
		minutes := max(1, int(math.Ceil(d.Minutes())))
		if minutes == 1 {
			return "a minute"
		}
		return fmt.Sprintf("%d minutes", minutes)
	}
	hours := int(math.Ceil(d.Hours()))
	if hours == 1 {
		return "an hour"
	}
	return fmt.Sprintf("%d hours", hours)
}
