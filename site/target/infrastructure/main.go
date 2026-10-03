// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from site/ by one. Do not edit.

// Everything uione runs on. The deploy script next to infrastructure/ runs it.
package main

import "github.com/da0x/uione/infrastructure"

func main() {
	infrastructure.Deploy(infrastructure.Project{
		Name:     "uione",
		Domain:   "www.uione.io",
		Firebase: "uione-web",
		Region:   "us-east4",
	})
}
