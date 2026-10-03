// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from site/ by one. Do not edit.

package main

import (
	"github.com/da0x/uione/one"

	"www.uione.io/api/studio"
	"www.uione.io/api/waitlist"
)

func main() {
	one.Serve(studio.Module, waitlist.Module)
}
