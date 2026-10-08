// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"strings"
	"sync"
	"testing"

	"github.com/da0x/uione/one"
)

// Code written by hand beside the generated code: asking for a build starts one
// elsewhere (After), and the build reports back (a Route), finishing it with a
// command nobody may run but the backend itself.

type Build struct {
	one.Record
	Project string `firestore:"project" one:"required"`
	Status  string `firestore:"status" one:"choices=queued|building|done|failed,default=queued"`
	Token   string `firestore:"token"`
}

type Step struct {
	one.Record
	Build string `firestore:"build" one:"required,refers=builds::build"`
	Name  string `firestore:"name"`
}

var started struct {
	sync.Mutex
	builds []Build
}

var builds = one.Module("builds",
	one.Command[Build]("build::create").Allow(one.SignedIn).After(func(s *one.System, b *Build) error {
		started.Lock()
		defer started.Unlock()
		started.builds = append(started.builds, *b)
		_, err := s.Run("builds::build::start", map[string]any{"id": b.ID})
		return err
	}),
	one.Command[Build]("build::start").Do(func(c *one.Ctx, b *Build) error {
		b.Status = "building"
		b.Token = "secret-" + b.ID
		return nil
	}),
	one.Command[Build]("build::finish").Do(func(c *one.Ctx, b *Build) error {
		b.Status = "done"
		return nil
	}),
	one.Command[Step]("step::create"),
)

func init() {
	one.Route("POST /hooks/build", func(s *one.System, w http.ResponseWriter, r *http.Request) {
		var report struct{ Build, Token string }
		if err := json.NewDecoder(r.Body).Decode(&report); err != nil {
			http.Error(w, "unreadable", http.StatusBadRequest)
			return
		}
		build, err := one.Fetch[Build](s, report.Build)
		if err != nil || build == nil || build.Token == "" || build.Token != report.Token {
			http.Error(w, "unknown build", http.StatusForbidden)
			return
		}
		if _, err := s.Run("builds::build::finish", map[string]any{"id": build.ID}); err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		if _, err := s.Run("builds::step::create", map[string]any{"build": build.ID, "name": "reported"}); err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		steps, err := one.FetchWhere[Step](s, "build", build.ID)
		if err != nil || len(steps) != 1 || steps[0].Name != "reported" || steps[0].ID == "" {
			http.Error(w, "the step wasn't read back", http.StatusInternalServerError)
			return
		}
		w.WriteHeader(http.StatusNoContent)
	})
}

func init() {
	one.Route("GET /hooks/who", func(s *one.System, w http.ResponseWriter, r *http.Request) {
		uid, err := s.SignedIn(r)
		if err != nil {
			http.Error(w, err.Error(), http.StatusUnauthorized)
			return
		}
		w.Write([]byte("uid:" + uid))
	})
}

func TestARouteKnowsWhoSentIt(t *testing.T) {
	h := start(t)
	ada, token := h.signUp("ada@example.com")
	ask := func(header string) (int, string) {
		t.Helper()
		request, _ := http.NewRequest(http.MethodGet, h.server.URL+"/hooks/who", nil)
		if header != "" {
			request.Header.Set("Authorization", header)
		}
		response, err := http.DefaultClient.Do(request)
		if err != nil {
			t.Fatal(err)
		}
		defer response.Body.Close()
		body, _ := io.ReadAll(response.Body)
		return response.StatusCode, string(body)
	}
	if status, body := ask("Bearer " + token); status != http.StatusOK || body != "uid:"+ada {
		t.Errorf("signed in, the route heard %d %q", status, body)
	}
	if status, body := ask(""); status != http.StatusOK || body != "uid:" {
		t.Errorf("signed out, the route heard %d %q", status, body)
	}
	if status, _ := ask("Bearer not-a-token"); status != http.StatusUnauthorized {
		t.Errorf("with a made-up sign-in, the route answered %d", status)
	}
}

func TestARoleGivenTwiceIsRefused(t *testing.T) {
	_, err := one.New(context.Background(), one.Module("twice",
		one.Command[Note]("note::create"),
		one.Role("writer", "note:create"),
		one.Role("writer", "note:create"),
	))
	if err == nil || !strings.Contains(err.Error(), "role twice::writer is given more than once") {
		t.Fatalf("a role given twice was taken: %v", err)
	}
}

// stored reads an entity as it's stored.
func (h *harness) stored(collection, id string) map[string]any {
	h.t.Helper()
	snap, err := h.store.Collection(collection).Doc(id).Get(context.Background())
	if err != nil {
		h.t.Fatal(err)
	}
	return snap.Data()
}

func TestHandWrittenCodeRunsAfterACommandAndAnswersARoute(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	id := h.mustRun("builds/build/create", token, map[string]any{"project": "neotrac"})

	// After ran with the saved build, and started it as the backend itself.
	started.Lock()
	last := started.builds[len(started.builds)-1]
	started.Unlock()
	if last.ID != id || last.Project != "neotrac" || last.Status != "queued" {
		t.Fatalf("After was given %+v", last)
	}
	stored := h.stored("builds_build", id)
	if stored["status"] != "building" || stored["token"] != "secret-"+id {
		t.Fatalf("the build after starting is %v", stored)
	}

	// Nobody signed in may run what only the backend runs.
	h.expect("builds/build/finish", token, map[string]any{"id": id}, http.StatusForbidden, "you don't have permission to do this")

	// A report with the wrong token is turned away; the right one finishes the build.
	post := func(body string) int {
		t.Helper()
		response, err := http.Post(h.server.URL+"/hooks/build", "application/json", strings.NewReader(body))
		if err != nil {
			t.Fatal(err)
		}
		response.Body.Close()
		return response.StatusCode
	}
	if status := post(`{"build":"` + id + `","token":"guess"}`); status != http.StatusForbidden {
		t.Fatalf("a wrong token got %d", status)
	}
	if status := post(`{"build":"` + id + `","token":"secret-` + id + `"}`); status != http.StatusNoContent {
		t.Fatalf("the right token got %d", status)
	}
	if stored := h.stored("builds_build", id); stored["status"] != "done" {
		t.Fatalf("the build after its report is %v", stored)
	}
}
