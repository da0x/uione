// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"fmt"
	"strings"
)

// Listed says a list in a command's message, as "waiting on {waiting(plates)}"
// fills it in: its values between commas.
func Listed[T any](values []T) string {
	said := make([]string, len(values))
	for i, v := range values {
		said[i] = fmt.Sprint(v)
	}
	return strings.Join(said, ", ")
}

// Said says a value in a command's message, as "{title}" fills it in.
func Said(value any) string {
	return fmt.Sprint(value)
}
