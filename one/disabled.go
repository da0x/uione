// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"context"
	"sync"
	"time"
)

// Whoever runs a site turns a person off everywhere by disabling their account in
// Firebase Authentication. Firebase then stops them signing in, but a sign-in they
// already have stays good until it expires, up to an hour later; so the backend asks
// Firebase too, at most once a minute for each person, and refuses them within a
// minute. What they read straight from Firestore, they can read until it expires.

// How long what Firebase said about a person is believed.
const disabledCheckedFor = time.Minute

type disabledSeen struct {
	disabled bool
	at       time.Time
}

// disabledAccounts remembers, for a minute, whether each person was turned off.
type disabledAccounts struct {
	mu    sync.Mutex
	seen  map[string]disabledSeen
	now   func() time.Time
	swept time.Time
}

func newDisabledAccounts() *disabledAccounts {
	return &disabledAccounts{seen: map[string]disabledSeen{}, now: time.Now}
}

// turnedOff refuses a person whose account is disabled. When Firebase can't be asked,
// they're let in: an outage of Firebase's isn't made into one of the site's.
func (a *App) turnedOff(ctx context.Context, uid string) error {
	d := a.disabled
	d.mu.Lock()
	now := d.now()
	if now.Sub(d.swept) > disabledCheckedFor {
		for id, s := range d.seen {
			if now.Sub(s.at) > disabledCheckedFor {
				delete(d.seen, id)
			}
		}
		d.swept = now
	}
	s, ok := d.seen[uid]
	d.mu.Unlock()
	if !ok || now.Sub(s.at) > disabledCheckedFor {
		s = disabledSeen{at: now}
		if user, err := a.auth.GetUser(ctx, uid); err == nil {
			s.disabled = user.Disabled
		} else {
			a.log.Printf("one: whether %s is turned off couldn't be asked: %v", uid, err)
		}
		d.mu.Lock()
		d.seen[uid] = s
		d.mu.Unlock()
	}
	if s.disabled {
		return &Failure{Status: 403, Message: "this account has been turned off"}
	}
	return nil
}
