// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Generated from studio.one by one. Do not edit.

package studio

import "github.com/da0x/uione/one"

type Project struct {
	one.Record
	Name  string `firestore:"name" one:"required"`
	Owner string `firestore:"owner" one:"refers=user,default=me"`
}

var Create = one.Command[Project]("project::create").Allow(one.SignedIn)

var Projects = one.View("projects").PerUser().
	Each(one.Where[Project]("owner", one.Viewer)).
	Fields("name", "created_at")

var Module = one.Module("studio", Create, Projects)
