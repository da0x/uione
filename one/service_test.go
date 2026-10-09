// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"bytes"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"testing"

	"github.com/da0x/uione/one"
)

// Desks with tickets, and notes GitHub leaves on them: a note is made by GitHub's
// service alone, which no person may stand in for, not even a desk's owner who
// edits their own role to say they may.

type Desk struct {
	one.Record
	Slug       string `firestore:"slug" one:"required,unique,key"`
	Repository string `firestore:"repository" one:"unique"`
}

type DeskRole struct {
	one.Record
	Desk  string   `firestore:"desk" one:"required,key,refers=desks::desk"`
	Name  string   `firestore:"name" one:"required,key"`
	Title string   `firestore:"title" one:"required"`
	May   []string `firestore:"may"`
}

type DeskSeat struct {
	one.Record
	Desk   string `firestore:"desk" one:"required,key,refers=desks::desk"`
	Person string `firestore:"person" one:"required,key,refers=user"`
	Role   string `firestore:"role" one:"required,key,refers=desks::desk_role"`
}

type DeskTicket struct {
	one.Record
	Desk   string  `firestore:"desk" one:"required,key,refers=desks::desk"`
	Number float64 `firestore:"number" one:"key,serial=desk"`
	Title  string  `firestore:"title"`
}

type DeskNote struct {
	one.Record
	DeskTicket string `firestore:"ticket" one:"required,key,refers=desks::desk_ticket"`
	URL        string `firestore:"url" one:"required,key"`
	Title      string `firestore:"title"`
}

var desks = one.Module("desks",
	one.Command[Desk]("desk::create").Allow(one.SignedIn).Do(func(c *one.Ctx, d *Desk) error {
		return one.Create(c, &DeskSeat{Desk: d.ID, Person: c.Me(), Role: one.Key(d.ID, "owner")})
	}),
	one.Command[DeskRole]("desk_role::update").Fields("title", "may"),
	one.Command[DeskTicket]("desk_ticket::create"),
	one.Command[DeskNote]("desk_note::create"),
	one.Roles(one.Entity[DeskRole](), one.Entity[Desk](), one.Entity[DeskSeat](), "may").
		Default("owner", "Owner", "desk_role::update", "desk_ticket::create"),
	one.Service("github", "GitHub", "desk_note::create"),
	one.GitHub("/hooks/desks").For(one.Entity[Desk](), "repository").Mentions(one.Entity[DeskTicket]()).
		OnCommit(func(c *one.Ctx, m one.Mention) error {
			return one.DispatchCreate(c, &DeskNote{DeskTicket: m.Issue, URL: m.URL, Title: m.Message}, nil, nil)
		}),
)

func init() { modules = append(modules, desks) }

// deliverTo posts a push to a route, signed with a key, as GitHub would.
func (h *harness) deliverTo(route string, payload any, key string) int {
	h.t.Helper()
	body, _ := json.Marshal(payload)
	mac := hmac.New(sha256.New, []byte(key))
	mac.Write(body)
	request, _ := http.NewRequest(http.MethodPost, h.server.URL+route, bytes.NewReader(body))
	request.Header.Set("X-GitHub-Event", "push")
	request.Header.Set("X-Hub-Signature-256", "sha256="+hex.EncodeToString(mac.Sum(nil)))
	response, err := http.DefaultClient.Do(request)
	if err != nil {
		h.t.Fatal(err)
	}
	response.Body.Close()
	return response.StatusCode
}

func TestOnlyItsServiceRunsWhatOnlyAServiceIsGiven(t *testing.T) {
	h := start(t)
	t.Setenv("GITHUB_WEBHOOK_SECRET", secret)
	_, owner := h.signUp("ola@example.com")
	h.mustRun("desks/desk/create", owner, map[string]any{"slug": "front", "repository": "da0x/front"})
	h.mustRun("desks/desk/create", owner, map[string]any{"slug": "back", "repository": "da0x/back"})
	h.mustRun("desks/desk_ticket/create", owner, map[string]any{"desk": "front", "title": "Keys"})
	note := map[string]any{"ticket": one.Key("front", "1"), "url": "https://example.com/forged", "title": "forged"}

	// Nobody signed out, and not the desk's owner, may make a note.
	h.expect("desks/desk_note/create", "", note, http.StatusUnauthorized, "sign in to do this")
	h.expect("desks/desk_note/create", owner, note, http.StatusForbidden, "you don't have permission to do this")

	// The owner edits their own role to say they may, which is theirs to edit; it
	// still gives them nothing only GitHub runs.
	h.mustRun("desks/desk_role/update", owner, map[string]any{"id": one.Key("front", "owner"), "may": []any{"desk_role::update", "desk_ticket::create", "desk_note::create"}})
	h.expect("desks/desk_note/create", owner, note, http.StatusForbidden, "you don't have permission to do this")
	if notes := h.count("desks_desk_note"); notes != 0 {
		t.Fatalf("people made %d notes", notes)
	}

	// Signed with another desk's secret, GitHub's word isn't taken for this one.
	if status := h.deliverTo("/hooks/desks", push("da0x/front", "Fix #1"), one.GitHubSecret("back")); status != http.StatusUnauthorized {
		t.Fatalf("a push signed with another desk's secret was answered %d", status)
	}
	// With its own, GitHub's service makes the note.
	if status := h.deliverTo("/hooks/desks", push("da0x/front", "Fix #1"), one.GitHubSecret("front")); status != http.StatusOK {
		t.Fatalf("a push signed with the desk's secret was answered %d", status)
	}
	if notes := h.count("desks_desk_note"); notes != 1 {
		t.Fatalf("GitHub made %d notes", notes)
	}
}
