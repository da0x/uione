// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"testing"

	"github.com/da0x/uione/one"
)

// What's left over, as a clock counts: never below none, and none over none.
func TestRemainder(t *testing.T) {
	for _, c := range []struct{ x, y, want float64 }{{5, 4, 1}, {-1, 4, 3}, {2.5, 1, 0.5}, {3, 0, 0}} {
		if got := one.Remainder(c.x, c.y); got != c.want {
			t.Errorf("Remainder(%v, %v) is %v, not %v", c.x, c.y, got, c.want)
		}
	}
}
