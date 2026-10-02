// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"context"
	"net/http"
	"slices"
	"sort"
	"sync"
	"testing"

	"github.com/da0x/uione/one"
)

// A small tracker: projects named by a short name, and issues numbered within
// each project, so issue 3 of uione is uione-3.

type Tracked struct {
	one.Record
	Slug string `firestore:"slug" one:"required,unique,key"`
	Name string `firestore:"name"`
}

type Issue struct {
	one.Record
	Project string  `firestore:"project" one:"required,key,refers=tracker::tracked"`
	Number  float64 `firestore:"number" one:"key,serial=project"`
	Title   string  `firestore:"title" one:"required"`
}

var tracker = one.Module("tracker",
	one.Command[Tracked]("tracked::create").Allow(one.SignedIn),
	one.Command[Issue]("issue::create").Allow(one.SignedIn),
	one.Command[Issue]("issue::update").Allow(one.SignedIn),
)

func (h *harness) issue(id string) map[string]any {
	h.t.Helper()
	snap, err := h.store.Collection("tracker_issue").Doc(id).Get(context.Background())
	if err != nil {
		h.t.Fatalf("issue %s: %v", id, err)
	}
	return snap.Data()
}

func TestIssuesAreNumberedWithinTheirProject(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	ids := []string{
		h.mustRun("tracker/issue/create", token, map[string]any{"project": "uione", "title": "Keys"}),
		h.mustRun("tracker/issue/create", token, map[string]any{"project": "uione", "title": "Serials", "number": 99.0}),
		h.mustRun("tracker/issue/create", token, map[string]any{"project": "neotrac", "title": "Skeleton"}),
	}
	if want := []string{"uione-1", "uione-2", "neotrac-1"}; !slices.Equal(ids, want) {
		t.Fatalf("the issues are %v, want %v", ids, want)
	}
	if got := h.issue("uione-2")["number"]; got != 2.0 {
		t.Errorf("a number that was sent was kept: uione-2 is number %v", got)
	}
}

func TestIssuesMadeAtOnceNeverShareANumber(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	var wg sync.WaitGroup
	var mu sync.Mutex
	var ids []string
	for range 8 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			status, reply := h.run("tracker/issue/create", token, map[string]any{"project": "uione", "title": "Busy"})
			if status != http.StatusOK {
				t.Errorf("an issue made alongside others failed: %d %v", status, reply)
				return
			}
			mu.Lock()
			ids = append(ids, reply["id"].(string))
			mu.Unlock()
		}()
	}
	wg.Wait()
	sort.Strings(ids)
	if want := []string{"uione-1", "uione-2", "uione-3", "uione-4", "uione-5", "uione-6", "uione-7", "uione-8"}; !slices.Equal(ids, want) {
		t.Errorf("eight issues made at once are %v", ids)
	}
}

func TestALargeNumberIsWrittenOutInTheId(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	if _, err := h.store.Collection("serials").Doc("tracker_issue.number.uione").Set(context.Background(), map[string]any{"last": 999999}); err != nil {
		t.Fatal(err)
	}
	if id := h.mustRun("tracker/issue/create", token, map[string]any{"project": "uione", "title": "A million"}); id != "uione-1000000" {
		t.Errorf("the millionth issue is %s", id)
	}
}

func TestAnUpdateChangesNeitherTheKeyNorTheNumber(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	id := h.mustRun("tracker/issue/create", token, map[string]any{"project": "uione", "title": "Keys"})
	h.mustRun("tracker/issue/update", token, map[string]any{"id": id, "project": "neotrac", "number": 7.0, "title": "Keys, joined"})
	issue := h.issue(id)
	if issue["project"] != "uione" || issue["number"] != 1.0 || issue["title"] != "Keys, joined" {
		t.Errorf("after the update the issue is %v", issue)
	}
}

func TestAUniqueKeyCantBeMadeTwice(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	if id := h.mustRun("tracker/tracked/create", token, map[string]any{"slug": "uione", "name": "uione"}); id != "uione" {
		t.Fatalf("the project is %s", id)
	}
	h.expect("tracker/tracked/create", token, map[string]any{"slug": "uione", "name": "Someone else's"}, http.StatusBadRequest, "Slug is already taken")
	snap, err := h.store.Collection("tracker_tracked").Doc("uione").Get(context.Background())
	if err != nil || snap.Data()["name"] != "uione" {
		t.Errorf("a second create changed the first: %v %v", snap.Data(), err)
	}
}
