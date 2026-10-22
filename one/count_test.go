// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"net/http"
	"testing"

	"github.com/da0x/uione/one"
)

// A pantry whose shelves hold two jars each, and three labelled ones in all,
// counted in each command's transaction.
type Jar struct {
	one.Record
	Shelf string `firestore:"shelf" one:"required"`
	Label string `firestore:"label"`
}

var pantry = one.Module("pantry",
	one.Command[Jar]("jar::create").Allow(one.SignedIn).Do(func(c *one.Ctx, j *Jar) error {
		held, err := one.Count[Jar](c, one.Where[Jar]("shelf", j.Shelf))
		if err != nil {
			return err
		}
		if held >= 2 {
			return c.Fail("that shelf holds two jars")
		}
		labelled, err := one.Count[Jar](c, one.All[Jar]().Except("label", nil))
		if err != nil {
			return err
		}
		if j.Label != "" && labelled >= 3 {
			return c.Fail("three jars are labelled already")
		}
		return nil
	}),
)

func init() { modules = append(modules, pantry) }

func TestACommandCountsWhatsStored(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	h.mustRun("pantry/jar/create", token, map[string]any{"shelf": "top", "label": "jam"})
	h.mustRun("pantry/jar/create", token, map[string]any{"shelf": "top"})
	if status, _ := h.run("pantry/jar/create", token, map[string]any{"shelf": "top"}); status != http.StatusBadRequest {
		t.Errorf("a third jar on a shelf of two got %d", status)
	}
	h.mustRun("pantry/jar/create", token, map[string]any{"shelf": "middle", "label": "honey"})
	h.mustRun("pantry/jar/create", token, map[string]any{"shelf": "bottom", "label": "salt"})
	if status, _ := h.run("pantry/jar/create", token, map[string]any{"shelf": "bottom", "label": "pepper"}); status != http.StatusBadRequest {
		t.Errorf("a fourth labelled jar got %d", status)
	}
	h.mustRun("pantry/jar/create", token, map[string]any{"shelf": "bottom"})
}
