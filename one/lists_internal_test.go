// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"slices"
	"testing"
)

func TestListsTakeAndLeave(t *testing.T) {
	list := []string{"a", "b", "c"}
	if got := FirstOf(list, 2); !slices.Equal(got, []string{"a", "b"}) {
		t.Errorf("FirstOf 2 is %v", got)
	}
	if got := FirstOf(list, 9); !slices.Equal(got, list) {
		t.Errorf("FirstOf past the end is %v, not all of it", got)
	}
	if got := DropOf(list, 1); !slices.Equal(got, []string{"b", "c"}) {
		t.Errorf("DropOf 1 is %v", got)
	}
	if got := DropOf(list, 9); len(got) != 0 {
		t.Errorf("DropOf past the end is %v, not none", got)
	}
	if got := FirstOf(list, -1); len(got) != 0 {
		t.Errorf("FirstOf -1 is %v, not none", got)
	}
	joined := Joined(FirstOf(list, 1), list)
	joined[0] = "z"
	if !slices.Equal(joined, []string{"z", "a", "b", "c"}) || list[0] != "a" {
		t.Errorf("Joined is %v and changed what it was given: %v", joined, list)
	}
}

// Each whole number from low to high comes up, and nothing else does.
func TestRandomStaysWithin(t *testing.T) {
	seen := map[float64]int{}
	for range 2000 {
		seen[Random(-2, 3)]++
	}
	for n := -2.0; n <= 3; n++ {
		if seen[n] == 0 {
			t.Errorf("%v never came up", n)
		}
	}
	if len(seen) != 6 {
		t.Errorf("drew %v, more than -2 to 3", seen)
	}
	if got := Random(5, 5); got != 5 {
		t.Errorf("Random(5, 5) is %v", got)
	}
	if got := Random(5, 1); got != 5 {
		t.Errorf("Random(5, 1) is %v, not its low", got)
	}
}

// Drawn in an order a test knows, as another target's tests draw it, it gives the
// numbers that order says.
func TestRandomDrawsFromItsSource(t *testing.T) {
	was := draw
	defer func() { draw = was }()
	draw = func(n int64) int64 { return n - 1 }
	if got := Random(1, 6); got != 6 {
		t.Errorf("the last of six is %v", got)
	}
}
