// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from waitlist.one by one. Do not edit.

package waitlist

import "github.com/da0x/uione/one"

type Signup struct {
	one.Record
	Email string `firestore:"email" one:"required,key,email"`
}

var Create = one.Command[Signup]("signup::create").Allow(one.Anyone)

var Signups = one.View("signups").Public().
	Count("total", one.All[Signup]())

var Module = one.Module("waitlist", Create, Signups)
