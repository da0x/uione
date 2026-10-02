// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"context"
	"fmt"
)

// event says that an entity changed. Every command publishes one, and views are
// rebuilt from them.
type event struct {
	Type    string // like signup.updated
	Entity  string // like waitlist::signup
	ID      string
	Version int64          // later events have larger versions
	Before  map[string]any // the entity before, or nil if it's new
	After   map[string]any // the entity after, or nil if it was deleted
}

func (ev event) id() string { return fmt.Sprintf("%s/%s/%d", ev.Entity, ev.ID, ev.Version) }

// publish rebuilds every view that reads the changed entity, in this process.
//
// A failure here never fails the command: the write has already happened, and
// saying otherwise would be wrong. It's logged, and the view catches up on the next
// event. Delivery through Pub/Sub, with a fallback for when publishing fails, comes
// when the library runs in the cloud.
func (a *App) publish(ctx context.Context, ev event) {
	for _, v := range a.reg.views {
		if _, reads := v.reads[ev.Entity]; !reads {
			continue
		}
		if err := a.project(context.WithoutCancel(ctx), v, ev); err != nil {
			a.log.Printf("one: view %s wasn't rebuilt after %s: %v", v.full, ev.id(), err)
		}
	}
}
