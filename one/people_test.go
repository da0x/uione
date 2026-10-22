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

// What a guide was charged, in cents, by whoever charged it.
type Fee struct {
	one.Record
	Guide  string  `firestore:"guide" one:"required,refers=guide::guide"`
	Amount float64 `firestore:"amount"`
}

// An office whose clerks see everyone who has signed up, two to a page, with how
// many guides each keeps, what their guides were charged, and what each charged;
// and on a person's own page, the fees their guides were charged, and a guide.
var office = one.Module("office",
	one.People("people").Fields("name", "email", "joined", "active", "commands").Keeps("guides", one.Entity[Guide]()).
		Sums("paid", one.Entity[Fee](), "amount", "guide.created_by").
		Sums("charged", one.Entity[Fee](), "amount", "created_by").
		Limit(2).
		List("fees", one.Entity[Fee](), "guide.created_by").Order("-amount").Fields("guide", "amount", "guide.visibility").
		List("made", one.Entity[Guide](), "created_by").Fields("slug").Limit(1),
	one.Role("clerk", string(one.ViewPeople)),
	one.Command[Fee]("fee::create").Allow(one.SignedIn),
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

// person asks for one person's row, and gives the status and the row.
func (h *harness) person(token, id string) (int, map[string]any) {
	h.t.Helper()
	asked := url.Values{"view": {"office::people"}, "person": {id}}
	request, _ := http.NewRequest(http.MethodGet, h.server.URL+"/api/people?"+asked.Encode(), nil)
	request.Header.Set("Authorization", "Bearer "+token)
	response, err := http.DefaultClient.Do(request)
	if err != nil {
		h.t.Fatal(err)
	}
	defer response.Body.Close()
	var reply struct {
		Person map[string]any `json:"person"`
	}
	json.NewDecoder(response.Body).Decode(&reply)
	return response.StatusCode, reply.Person
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

func TestAClerkSeesWhatEachPersonPaid(t *testing.T) {
	h := start(t)
	ada, adaToken := h.signUp("ada@example.com")
	grace, graceToken := h.signUp("grace@example.com")
	h.mustRun("guide/guide/create", adaToken, map[string]any{"slug": "kitchen"})
	h.mustRun("guide/guide/create", adaToken, map[string]any{"slug": "cellar"})
	h.mustRun("guide/guide/create", graceToken, map[string]any{"slug": "garden"})
	h.mustRun("office/fee/create", graceToken, map[string]any{"guide": "kitchen", "amount": 1200})
	h.mustRun("office/fee/create", graceToken, map[string]any{"guide": "cellar", "amount": 300})
	h.mustRun("office/fee/create", adaToken, map[string]any{"guide": "garden", "amount": 50})
	h.app.KeepUsage()
	if _, err := h.store.Collection("users").Doc(ada).Set(context.Background(), map[string]any{"role_id": "clerk"}, firestore.MergeAll); err != nil {
		t.Fatal(err)
	}
	_, rows, _ := h.people(adaToken, "")
	sums := map[any][2]any{}
	for _, row := range rows {
		sums[row["id"]] = [2]any{row["paid"], row["charged"]}
	}
	// Ada's guides were charged 1500 and ada charged 50; grace the other way round.
	if want := map[any][2]any{ada: {float64(1500), float64(50)}, grace: {float64(50), float64(1500)}}; !reflect.DeepEqual(sums, want) {
		t.Errorf("what each paid and charged is %v, not %v", sums, want)
	}
}

func TestAClerkSeesOnePersonWithWhatsTheirs(t *testing.T) {
	h := start(t)
	ada, adaToken := h.signUp("ada@example.com")
	grace, graceToken := h.signUp("grace@example.com")
	h.mustRun("guide/guide/create", adaToken, map[string]any{"slug": "kitchen"})
	h.mustRun("guide/guide/create", adaToken, map[string]any{"slug": "cellar"})
	h.mustRun("guide/guide/create", graceToken, map[string]any{"slug": "garden"})
	h.mustRun("office/fee/create", graceToken, map[string]any{"guide": "kitchen", "amount": 1200})
	h.mustRun("office/fee/create", graceToken, map[string]any{"guide": "cellar", "amount": 300})
	h.mustRun("office/fee/create", graceToken, map[string]any{"guide": "garden", "amount": 50})
	h.app.KeepUsage()
	if _, err := h.store.Collection("users").Doc(ada).Set(context.Background(), map[string]any{"role_id": "clerk"}, firestore.MergeAll); err != nil {
		t.Fatal(err)
	}
	status, row := h.person(adaToken, ada)
	if status != http.StatusOK || row["email"] != "ada@example.com" || row["paid"] != float64(1500) || row["guides"] != float64(2) {
		t.Fatalf("ada is %d %v", status, row)
	}
	// Hers alone, the most first, with what each points at read through.
	fees, _ := row["fees"].([]any)
	var shown []any
	for _, fee := range fees {
		f := fee.(map[string]any)
		shown = append(shown, []any{f["guide"], f["amount"], f["guide.visibility"]})
	}
	if want := []any{[]any{"kitchen", float64(1200), "public"}, []any{"cellar", float64(300), "public"}}; !reflect.DeepEqual(shown, want) {
		t.Errorf("ada's fees are %v, not %v", shown, want)
	}
	if made, _ := row["made"].([]any); len(made) != 1 {
		t.Errorf("ada's guides, one at most, are %v", row["made"])
	}
	if status, _ := h.person(graceToken, ada); status != http.StatusForbidden {
		t.Errorf("someone without the role reading ada got %d", status)
	}
	if status, _ := h.person(adaToken, "nobody"); status != http.StatusNotFound {
		t.Errorf("reading someone who isn't there got %d", status)
	}
	if _, row := h.person(adaToken, grace); len(row["fees"].([]any)) != 1 {
		t.Errorf("grace's fees are %v", row["fees"])
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
