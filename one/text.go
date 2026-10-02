// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import "strings"

// Helpers for the functions a .one file declares, which work on text the way a
// reader counts it: by letters, not by bytes.

// StartsWith says whether text begins with prefix.
func StartsWith(text, prefix string) bool { return strings.HasPrefix(text, prefix) }

// Drop leaves out the first n letters of text: Drop("The Hobbit", 4) is "Hobbit".
func Drop(text string, n int) string {
	letters := []rune(text)
	if n >= len(letters) {
		return ""
	}
	if n <= 0 {
		return text
	}
	return string(letters[n:])
}
