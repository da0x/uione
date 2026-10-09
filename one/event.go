// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"context"
	"fmt"
	"sync"
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

// publish rebuilds every view that reads what changed, in this process: all of a
// command's changes together, so a page that several of them touch, like a project's
// when it's made with its board, phases and steps, is rebuilt once rather than once
// for each, and different pages are rebuilt at the same time. A rebuild's version
// is when it read, not which change caused it, so one rebuild after all of them
// holds what each would have.
//
// A failure here never fails the command: the write has already happened, and
// saying otherwise would be wrong. It's logged, and the view catches up on the next
// event. Delivery through Pub/Sub, with a fallback for when publishing fails, comes
// when the library runs in the cloud.
func (a *App) publish(ctx context.Context, events ...event) {
	ctx = context.WithoutCancel(ctx)
	type page struct {
		view    *ViewSpec
		subject string
	}
	var pages []page
	cause := map[page]string{} // the last change that touched it, kept with it
	for _, v := range a.reg.views {
		for _, ev := range events {
			if _, reads := v.reads[ev.Entity]; !reads {
				continue
			}
			subjects, err := v.subjects(ctx, a, ev)
			if err != nil {
				a.log.Printf("one: view %s wasn't rebuilt after %s: %v", v.full, ev.id(), err)
				continue
			}
			for _, subject := range subjects {
				p := page{v, subject}
				if _, listed := cause[p]; !listed {
					pages = append(pages, p)
				}
				cause[p] = ev.id()
			}
		}
	}
	var wg sync.WaitGroup
	at := make(chan struct{}, rebuildsAtOnce)
	for _, p := range pages {
		wg.Add(1)
		at <- struct{}{}
		go func() {
			defer func() { <-at; wg.Done() }()
			if err := a.rebuild(ctx, p.view, p.subject, cause[p]); err != nil {
				a.log.Printf("one: view %s wasn't rebuilt after %s: %v", p.view.full, cause[p], err)
			}
		}()
	}
	wg.Wait()
}

// How many pages are rebuilt at the same time after a command.
const rebuildsAtOnce = 8
