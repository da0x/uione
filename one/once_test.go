// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"context"
	"sync/atomic"
	"testing"

	"github.com/da0x/uione/one"
)

// How many times the work below has been done, across every start.
var onceDone atomic.Int32

var onceModule = one.Module("tasks", one.Once("count the starts", func(s *one.System) error {
	onceDone.Add(1)
	_, err := s.Run("tasks::task::create", map[string]any{"title": "made once"})
	return err
}))

func TestWorkDoneOnceIsDoneAtTheFirstStartAndNotAgain(t *testing.T) {
	h := start(t)
	before := onceDone.Load()
	app, err := one.New(context.Background(), append(modules, onceModule)...)
	if err != nil {
		t.Fatal(err)
	}
	app.Close()
	again, err := one.New(context.Background(), append(modules, onceModule)...)
	if err != nil {
		t.Fatal(err)
	}
	again.Close()
	if done := onceDone.Load() - before; done != 1 {
		t.Fatalf("the work was done %d times over two starts, not once", done)
	}
	docs, err := h.store.Collection("tasks_task").Documents(context.Background()).GetAll()
	if err != nil || len(docs) != 1 {
		t.Fatalf("what it made, once: %d tasks, %v", len(docs), err)
	}
}
