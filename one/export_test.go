// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import "context"

// KeepUsage writes what each person has used now, rather than within a few seconds,
// for tests to read.
func (a *App) KeepUsage() { a.usage.write(context.Background(), a) }
