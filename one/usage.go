// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"context"
	"sync"
	"time"

	"cloud.google.com/go/firestore"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// What each person uses, so whoever runs the app knows what each costs it: the
// commands they run and the records they and the views they touch write, each month,
// and what they keep now, the records they made of each kind and the bytes of their
// files. It's kept in usage/<person>, added up in memory and written every few
// seconds, so someone running ten commands a second never waits on their own counter,
// and what's counted costs one write per person, not one per command.

const usageWritten = 10 * time.Second // how often what's counted is written

// usage is what's been counted since it was last written, by person.
type usage struct {
	mu      sync.Mutex
	pending map[string]*use
	stop    chan struct{}
	done    chan struct{}
	started bool
	closing sync.Once
}

type use struct {
	commands int64
	writes   int64
	held     map[string]int64 // records made, less those deleted, by collection
	bytes    int64            // of files uploaded, less those deleted
	active   time.Time        // last asked anything
}

func newUsage() *usage {
	return &usage{pending: map[string]*use{}, stop: make(chan struct{}), done: make(chan struct{})}
}

// of is a person's count since the last write. The caller holds mu.
func (u *usage) of(person string) *use {
	p := u.pending[person]
	if p == nil {
		p = &use{held: map[string]int64{}}
		u.pending[person] = p
	}
	return p
}

// ran counts a command someone ran, and what it and the views it rebuilt wrote.
func (u *usage) ran(person string, writes int) {
	if person == "" {
		return
	}
	u.mu.Lock()
	defer u.mu.Unlock()
	p := u.of(person)
	p.commands++
	p.writes += int64(writes)
}

// asked marks someone active now.
func (u *usage) asked(person string) {
	if person == "" {
		return
	}
	u.mu.Lock()
	defer u.mu.Unlock()
	u.of(person).active = time.Now().UTC()
}

// kept counts the records events made and deleted for whoever made each. A record
// whose key moved is still one record, so the event for the key it left isn't counted.
func (u *usage) kept(reg *registry, events []event) {
	u.mu.Lock()
	defer u.mu.Unlock()
	for _, ev := range events {
		if ev.moved {
			continue
		}
		s := reg.entity(ev.Entity)
		if s == nil || s.collection == "users" {
			continue
		}
		switch {
		case ev.Before == nil && ev.After != nil:
			if by, _ := ev.After["created_by"].(string); by != "" {
				u.of(by).held[s.collection]++
			}
		case ev.Before != nil && ev.After == nil:
			if by, _ := ev.Before["created_by"].(string); by != "" {
				u.of(by).held[s.collection]--
			}
		}
	}
}

// filed counts bytes of files someone uploaded, or took away when negative.
func (u *usage) filed(person string, bytes int64) {
	if person == "" || bytes == 0 {
		return
	}
	u.mu.Lock()
	defer u.mu.Unlock()
	u.of(person).bytes += bytes
}

// write adds what's been counted to each person's usage, and starts counting again.
func (u *usage) write(ctx context.Context, a *App) {
	u.mu.Lock()
	pending := u.pending
	u.pending = map[string]*use{}
	u.mu.Unlock()
	if len(pending) == 0 {
		return
	}
	month := time.Now().UTC().Format("2006-01")
	writer := a.store.BulkWriter(ctx)
	for person, p := range pending {
		data := map[string]any{"person": person}
		if p.commands > 0 || p.writes > 0 {
			data["months"] = map[string]any{month: map[string]any{
				"commands": firestore.Increment(p.commands),
				"writes":   firestore.Increment(p.writes),
			}}
		}
		held := map[string]any{}
		for collection, n := range p.held {
			if n != 0 {
				held[collection] = firestore.Increment(n)
			}
		}
		if len(held) > 0 {
			data["held"] = held
		}
		if p.bytes != 0 {
			data["bytes"] = firestore.Increment(p.bytes)
		}
		if !p.active.IsZero() {
			data["active"] = p.active
		}
		if _, err := writer.Set(a.store.Collection("usage").Doc(person), data, firestore.MergeAll); err != nil {
			a.log.Printf("one: %s's usage wasn't kept: %v", person, err)
		}
	}
	writer.End()
}

// start writes what's counted every few seconds until the app closes, then once more.
func (u *usage) start(a *App) {
	u.started = true
	go u.run(a)
}

func (u *usage) run(a *App) {
	defer close(u.done)
	tick := time.NewTicker(usageWritten)
	defer tick.Stop()
	for {
		select {
		case <-tick.C:
			u.write(context.Background(), a)
		case <-u.stop:
			u.write(context.Background(), a)
			return
		}
	}
}

func (u *usage) close() {
	u.closing.Do(func() {
		close(u.stop)
		if u.started {
			<-u.done
		}
	})
}

// usageAs is how this library counts what each person keeps, changed whenever that
// changes, so what was counted before is counted again.
const usageAs = "1"

// heldCounted counts, once, what each person keeps of what was stored before usage
// was counted: each record by who made it, and each file by who uploaded it. Each is
// read once, and never again until usageAs changes.
func (a *App) heldCounted(ctx context.Context) error {
	kept := a.store.Collection("usage_definitions").Doc("held")
	snap, err := kept.Get(ctx)
	if err != nil && status.Code(err) != codes.NotFound {
		return err
	}
	if err == nil && snap.Data()["definition"] == usageAs {
		return nil
	}
	held := map[string]map[string]any{}
	bytes := map[string]int64{}
	for _, s := range a.reg.schemas {
		if s.collection == "users" {
			continue
		}
		docs, err := a.store.Collection(s.collection).Select("created_by").Documents(ctx).GetAll()
		if err != nil {
			return err
		}
		for _, doc := range docs {
			by, _ := doc.Data()["created_by"].(string)
			if by == "" {
				continue
			}
			if held[by] == nil {
				held[by] = map[string]any{}
			}
			n, _ := held[by][s.collection].(int64)
			held[by][s.collection] = n + 1
		}
	}
	files, err := a.store.Collection("files").Select("uploaded_by", "size").Documents(ctx).GetAll()
	if err != nil {
		return err
	}
	for _, doc := range files {
		by, _ := doc.Data()["uploaded_by"].(string)
		bytes[by] += asInt(doc.Data()["size"])
	}
	people := map[string]bool{}
	for person := range held {
		people[person] = true
	}
	for person := range bytes {
		people[person] = person != ""
	}
	writer := a.store.BulkWriter(ctx)
	var jobs []*firestore.BulkWriterJob
	for person, counted := range people {
		if !counted {
			continue
		}
		// What they hold is set whole, not added to.
		data := map[string]any{"person": person, "held": held[person], "bytes": bytes[person]}
		if held[person] == nil {
			data["held"] = map[string]any{}
		}
		job, err := writer.Set(a.store.Collection("usage").Doc(person), data, firestore.Merge([]string{"person"}, []string{"held"}, []string{"bytes"}))
		if err != nil {
			writer.End()
			return err
		}
		jobs = append(jobs, job)
	}
	writer.End()
	for _, job := range jobs {
		if _, err := job.Results(); err != nil {
			return err
		}
	}
	_, err = kept.Set(ctx, map[string]any{"definition": usageAs, "people": len(jobs), "at": time.Now()})
	return err
}

// asInt is a stored whole number, as Firestore gives it back.
func asInt(stored any) int64 {
	switch n := stored.(type) {
	case int64:
		return n
	case float64:
		return int64(n)
	}
	return 0
}
