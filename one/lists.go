// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"crypto/rand"
	"math"
	"math/big"
)

// Helpers for a list a .one file works on, each one's id or a piece of text, kept in
// its order. Each gives a list of its own, so the one it's given is never changed.

// FirstOf is the first n of a list, or all of it when it holds fewer.
func FirstOf(list []string, n int) []string {
	n = max(0, min(n, len(list)))
	return append([]string{}, list[:n]...)
}

// DropOf is all of a list after its first n, or none when it holds no more.
func DropOf(list []string, n int) []string {
	n = max(0, min(n, len(list)))
	return append([]string{}, list[n:]...)
}

// ItemOf is a list's nth, counting from 1, or nothing when it holds fewer.
func ItemOf(list []string, n int) string {
	if n < 1 || n > len(list) {
		return ""
	}
	return list[n-1]
}

// Joined is a, then b.
func Joined(a, b []string) []string {
	return append(append(make([]string, 0, len(a)+len(b)), a...), b...)
}

// Random is a whole number from low to high, both included, drawn where no one
// running the app can see or steer it: the operating system's secure source. With
// high below low, it's low.
func Random(low, high float64) float64 {
	from, to := math.Ceil(low), math.Floor(high)
	if to <= from {
		return from
	}
	return from + float64(draw(int64(to-from)+1))
}

// draw is a number from 0 up to n, not included; tests draw in an order they know.
var draw = func(n int64) int64 {
	got, err := rand.Int(rand.Reader, big.NewInt(n))
	if err != nil {
		panic("one: the operating system gave no random number: " + err.Error())
	}
	return got.Int64()
}
