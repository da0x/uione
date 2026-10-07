// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"context"
	"fmt"
	"time"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// OnceSpec is work the backend does once in its life, made with Once.
type OnceSpec struct {
	name string
	work func(*System) error
}

// Once does work the first time a backend with it starts, and never again: a change
// to what's stored that a deploy brings, like moving every project to a new
// workflow. It runs as the backend itself, so it may run any command, before any
// view is rebuilt. That it's done is kept in once/<name>, so it's done once however
// many times the backend starts. If it fails, the backend doesn't start, and the
// deploy says so.
func Once(name string, work func(*System) error) *OnceSpec {
	return &OnceSpec{name: name, work: work}
}

func (o *OnceSpec) register(r *registry, _ string) { r.once = append(r.once, o) }

func (a *App) doOnce(ctx context.Context) error {
	for _, o := range a.reg.once {
		done := a.store.Collection("once").Doc(o.name)
		if _, err := done.Get(ctx); err == nil {
			continue
		} else if status.Code(err) != codes.NotFound {
			return err
		}
		if err := o.work(&System{app: a, ctx: ctx}); err != nil {
			return fmt.Errorf("one: once %s: %w", o.name, err)
		}
		if _, err := done.Set(ctx, map[string]any{"name": o.name, "at": time.Now()}); err != nil {
			return err
		}
	}
	return nil
}
