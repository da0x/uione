// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"fmt"
	"math"
	"sync"
	"time"
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
