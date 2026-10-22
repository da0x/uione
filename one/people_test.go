// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"context"
	"encoding/json"
	"net/http"
	"net/url"
	"reflect"
	"testing"
	"time"

	"cloud.google.com/go/firestore"
	"github.com/da0x/uione/one"
)

// An office whose clerks see everyone who has signed up, two to a page, with how
// many guides each keeps.
var office = one.Module("office",
	one.People("people").Fields("name", "email", "joined", "active", "commands").Keeps("guides", one.Entity[Guide]()).Limit(2),
	one.Role("clerk", string(one.ViewPeople)),
)

func init() { modules = append(modules, office) }

// people asks for a page of everyone, and gives the status, the rows and where the
// next page starts.
func (h *harness) people(token, after string) (int, []map[string]any, string) {
	h.t.Helper()
	asked := url.Values{"view": {"office::people"}, "after": {after}}
	request, _ := http.NewRequest(http.MethodGet, h.server.URL+"/api/people?"+asked.Encode(), nil)
	if token != "" {
		request.Header.Set("Authorization", "Bearer "+token)
	}
	response, err := http.DefaultClient.Do(request)
	if err != nil {
		h.t.Fatal(err)
	}
	defer response.Body.Close()
	var reply struct {
		Rows []map[string]any `json:"rows"`
		Next string           `json:"next"`
	}
	json.NewDecoder(response.Body).Decode(&reply)
	return response.StatusCode, reply.Rows, reply.Next
}

func TestAClerkSeesEveryoneAPageAtATime(t *testing.T) {
	h := start(t)
	ada, adaToken := h.signUp("ada@example.com")
	grace, graceToken := h.signUp("grace@example.com")
	bob, bobToken := h.signUp("bob@example.com")
	h.mustRun("guide/guide/create", adaToken, map[string]any{"slug": "kitchen"})
	h.mustRun("guide/guide/create", adaToken, map[string]any{"slug": "cellar"})
	time.Sleep(10 * time.Millisecond)
	h.mustRun("guide/guide/create", graceToken, map[string]any{"slug": "garden"})
	time.Sleep(10 * time.Millisecond)
	h.mustRun("guide/guide/create", bobToken, map[string]any{"slug": "shed"})
	h.app.KeepUsage()
	if _, err := h.store.Collection("users").Doc(ada).Set(context.Background(), map[string]any{"role_id": "clerk"}, firestore.MergeAll); err != nil {
		t.Fatal(err)
	}

	for who, token := range map[string]string{"someone signed out": "", "someone without the role": graceToken} {
		if status, rows, _ := h.people(token, ""); status == http.StatusOK || len(rows) != 0 {
			t.Errorf("%s listing everyone got %d %v", who, status, rows)
		}
	}

	status, first, next := h.people(adaToken, "")
	if status != http.StatusOK || len(first) != 2 || next == "" {
		t.Fatalf("the first page is %d %v, next %q", status, first, next)
	}
	status, second, last := h.people(adaToken, next)
	if status != http.StatusOK || len(second) != 1 || last != "" {
		t.Fatalf("the second page is %d %v, next %q", status, second, last)
	}
	everyone := append(first, second...)
	var order []any
	for _, row := range everyone {
		order = append(order, row["id"])
	}
	// The last to do anything first.
	if !reflect.DeepEqual(order, []any{bob, grace, ada}) {
		t.Errorf("everyone is listed as %v", order)
	}
	row := everyone[2]
	if row["email"] != "ada@example.com" || row["guides"] != float64(2) || row["commands"] != float64(2) || row["joined"] == nil || row["active"] == nil {
		t.Errorf("ada's row is %v", row)
	}
	if _, shown := row["writes"]; shown {
		t.Errorf("a row shows what the list doesn't: %v", row)
	}
	if everyone[0]["guides"] != float64(1) {
		t.Errorf("bob's row is %v", everyone[0])
	}
}

func TestEveryoneWhoSignedUpBeforeIsListed(t *testing.T) {
	h := start(t)
	ctx := context.Background()
	ada, adaToken := h.signUp("ada@example.com")
	h.mustRun("guide/guide/create", adaToken, map[string]any{"slug": "kitchen"})
	h.app.KeepUsage()
	// Grace signed up before people were listed, and never came back.
	grace, _ := h.signUp("grace@example.com")
	if _, err := h.store.Collection("usage_definitions").Doc("people").Delete(ctx); err != nil {
		t.Fatal(err)
	}
	if _, err := h.store.Collection("users").Doc(ada).Set(ctx, map[string]any{"role_id": "clerk"}, firestore.MergeAll); err != nil {
		t.Fatal(err)
	}
	app, err := one.New(ctx, modules...)
	if err != nil {
		t.Fatal(err)
	}
	app.Close()
	listed := h.mustHave("usage", grace)
	if listed["email"] != "grace@example.com" || listed["joined"] == nil || listed["active"] == nil {
		t.Errorf("grace is listed as %v", listed)
	}
	// What was known of ada is kept.
	if held := h.mustHave("usage", ada)["held"]; held == nil {
		t.Error("listing everyone forgot what ada keeps")
	}
	if status, rows, _ := h.people(adaToken, ""); status != http.StatusOK || len(rows) != 2 {
		t.Errorf("everyone is %d %v", status, rows)
	}
}
