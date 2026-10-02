// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// These tests run against the Firebase emulators. Start them with
// tools/emulators/run; the tests find them on their usual ports.

package one_test

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"testing"

	"cloud.google.com/go/firestore"
	"github.com/da0x/uione/one"
)

const project = "demo-uione"

func TestMain(m *testing.M) {
	for name, value := range map[string]string{
		"FIRESTORE_EMULATOR_HOST":     "localhost:8080",
		"FIREBASE_AUTH_EMULATOR_HOST": "localhost:9099",
	} {
		if os.Getenv(name) == "" {
			os.Setenv(name, value)
		}
	}
	if _, err := http.Get("http://" + os.Getenv("FIRESTORE_EMULATOR_HOST")); err != nil {
		fmt.Fprintln(os.Stderr, "the Firebase emulators aren't running; start them with tools/emulators/run")
		os.Exit(1)
	}
	os.Exit(m.Run())
}

// The entities and modules below are small versions of the site's, plus a few that
// exercise the rules the site doesn't use yet.

type Signup struct {
	one.Record
	Email string `firestore:"email" one:"required,key,email"`
}

type Project struct {
	one.Record
	Name  string `firestore:"name" one:"required"`
	Owner string `firestore:"owner" one:"default=me"`
}

// A handle someone claims by name, and owns.
type Handle struct {
	one.Record
	Name  string   `firestore:"name" one:"required,key"`
	Owner string   `firestore:"owner" one:"default=me"`
	Tags  []string `firestore:"tags"`
}

type Note struct {
	one.Record
	Text string `firestore:"text" one:"required"`
	Code string `firestore:"code" one:"unique"`
}

type Task struct {
	one.Record
	Title string `firestore:"title" one:"required"`
	Done  bool   `firestore:"done" one:"default=false"`
	Owner string `firestore:"owner" one:"default=me"`
}

var modules = []one.Item{
	library,
	tracker,
	forum,
	teams,
	desk,
	cases,
	code,
	one.Module("waitlist",
		one.Command[Signup]("signup::create").Allow(one.Anyone),
		one.View("signups").Public().Count("total", one.All[Signup]()),
	),
	one.Module("studio",
		one.Command[Project]("project::create").Allow(one.SignedIn),
		one.Command[Project]("project::delete").Allow(one.Owner),
		one.Command[Handle]("handle::create").Allow(one.SignedIn),
		one.View("projects").PerUser().Each(one.Where[Project]("owner", one.Viewer)).Fields("name", "created_at"),
	),
	one.Module("notes",
		one.Command[Note]("note::create"),
		one.Role("writer", "note:create"),
	),
	one.Module("tasks",
		one.Command[Task]("task::create").Allow(one.SignedIn),
		one.View("list").PerUser().Each(one.Where[Task]("owner", one.Viewer)).Order("done", "-created_at").Fields("title", "done"),
		one.Command[Task]("task::complete").Allow(one.Owner).Do(func(c *one.Ctx, t *Task) error {
			if t.Done {
				return c.Fail("that task is already done")
			}
			t.Done = true
			return nil
		}),
	),
}

// harness is a fresh backend on empty emulators, and a way to talk to it.
type harness struct {
	t      *testing.T
	server *httptest.Server
	store  *firestore.Client
}

func start(t *testing.T) *harness {
	t.Helper()
	for _, url := range []string{
		"http://" + os.Getenv("FIRESTORE_EMULATOR_HOST") + "/emulator/v1/projects/" + project + "/databases/(default)/documents",
		"http://" + os.Getenv("FIREBASE_AUTH_EMULATOR_HOST") + "/emulator/v1/projects/" + project + "/accounts",
	} {
		request, _ := http.NewRequest(http.MethodDelete, url, nil)
		if _, err := http.DefaultClient.Do(request); err != nil {
			t.Fatal(err)
		}
	}
	ctx := context.Background()
	app, err := one.New(ctx, modules...)
	if err != nil {
		t.Fatal(err)
	}
	store, err := firestore.NewClient(ctx, project)
	if err != nil {
		t.Fatal(err)
	}
	h := &harness{t: t, server: httptest.NewServer(app.Handler()), store: store}
	t.Cleanup(func() {
		h.server.Close()
		store.Close()
		app.Close()
	})
	return h
}

// signUp makes a person in the Auth emulator and returns their id and ID token.
func (h *harness) signUp(email string) (string, string) {
	h.t.Helper()
	body, _ := json.Marshal(map[string]any{"email": email, "password": "password", "returnSecureToken": true})
	url := "http://" + os.Getenv("FIREBASE_AUTH_EMULATOR_HOST") + "/identitytoolkit.googleapis.com/v1/accounts:signUp?key=any"
	response, err := http.Post(url, "application/json", bytes.NewReader(body))
	if err != nil {
		h.t.Fatal(err)
	}
	defer response.Body.Close()
	var account struct {
		LocalID string `json:"localId"`
		IDToken string `json:"idToken"`
	}
	json.NewDecoder(response.Body).Decode(&account)
	if account.IDToken == "" {
		h.t.Fatal("the Auth emulator didn't sign anyone up")
	}
	return account.LocalID, account.IDToken
}

// run posts a command and returns the status and the reply.
func (h *harness) run(command, token string, input map[string]any) (int, map[string]any) {
	h.t.Helper()
	body, _ := json.Marshal(input)
	request, _ := http.NewRequest(http.MethodPost, h.server.URL+"/api/"+command, bytes.NewReader(body))
	if token != "" {
		request.Header.Set("Authorization", "Bearer "+token)
	}
	response, err := http.DefaultClient.Do(request)
	if err != nil {
		h.t.Fatal(err)
	}
	defer response.Body.Close()
	reply := map[string]any{}
	json.NewDecoder(response.Body).Decode(&reply)
	return response.StatusCode, reply
}

func (h *harness) mustRun(command, token string, input map[string]any) string {
	h.t.Helper()
	status, reply := h.run(command, token, input)
	if status != http.StatusOK {
		h.t.Fatalf("%s: %d %v", command, status, reply)
	}
	return reply["id"].(string)
}

func (h *harness) expect(command, token string, input map[string]any, status int, message string) {
	h.t.Helper()
	got, reply := h.run(command, token, input)
	if got != status || reply["error"] != message {
		h.t.Fatalf("%s: want %d %q, got %d %v", command, status, message, got, reply)
	}
}

// view reads a view's document, or nil when it doesn't exist.
func (h *harness) view(id string) map[string]any {
	h.t.Helper()
	snap, err := h.store.Collection("views").Doc(id).Get(context.Background())
	if err != nil {
		return nil
	}
	return snap.Data()
}

func TestTheWaitlistCountsEachAddressOnce(t *testing.T) {
	h := start(t)
	if total := h.view("waitlist::signups")["total"]; total != int64(0) {
		t.Fatalf("a public view exists from the start, at zero; got %v", total)
	}
	first := h.mustRun("waitlist/signup/create", "", map[string]any{"email": "Ada@Example.com"})
	if total := h.view("waitlist::signups")["total"]; total != int64(1) {
		t.Fatalf("want 1 signup, got %v", total)
	}
	again := h.mustRun("waitlist/signup/create", "", map[string]any{"email": "  ada@example.com "})
	if again != first {
		t.Fatalf("the same address is the same signup: %q and %q", first, again)
	}
	if total := h.view("waitlist::signups")["total"]; total != int64(1) {
		t.Fatalf("joining twice counts once; got %v", total)
	}
	h.mustRun("waitlist/signup/create", "", map[string]any{"email": "grace@example.com"})
	if total := h.view("waitlist::signups")["total"]; total != int64(2) {
		t.Fatalf("want 2 signups, got %v", total)
	}
	if public := h.view("waitlist::signups")["public"]; public != true {
		t.Fatal("the count is public")
	}
}

func TestMistakesAreExplainedTheWayTheFormShowsThem(t *testing.T) {
	h := start(t)
	h.expect("waitlist/signup/create", "", map[string]any{}, 400, "Email is required")
	h.expect("waitlist/signup/create", "", map[string]any{"email": "not an address"}, 400, "Email isn't an email address")
	h.expect("waitlist/signup/create", "", map[string]any{"email": 5}, 400, "Email should be text")
	h.expect("waitlist/nothing/create", "", map[string]any{}, 404, "there's no command waitlist::nothing::create")
}

func TestSigningInIsNeededWhereItShouldBe(t *testing.T) {
	h := start(t)
	h.expect("studio/project/create", "", map[string]any{"name": "uione.io"}, 401, "sign in to do this")
	h.expect("studio/project/create", "not-a-token", map[string]any{"name": "uione.io"}, 401, "your sign-in has expired; sign in again")
}

func TestAPersonSeesOnlyTheirOwnProjects(t *testing.T) {
	h := start(t)
	alice, aliceToken := h.signUp("alice@example.com")
	bob, _ := h.signUp("bob@example.com")

	// Whoever creates a project owns it; an owner in the input is ignored.
	id := h.mustRun("studio/project/create", aliceToken, map[string]any{"name": "uione.io", "owner": bob})
	stored, err := h.store.Collection("studio_project").Doc(id).Get(context.Background())
	if err != nil || stored.Data()["owner"] != alice {
		t.Fatalf("the project belongs to whoever created it: %v %v", stored.Data()["owner"], err)
	}

	mine := h.view("studio::projects:" + alice)
	rows, _ := mine["rows"].([]any)
	if len(rows) != 1 || rows[0].(map[string]any)["name"] != "uione.io" || mine["owner_uid"] != alice {
		t.Fatalf("alice's view holds her project: %v", mine)
	}
	if h.view("studio::projects:"+bob) != nil {
		t.Fatal("bob has no view of alice's projects")
	}
}

func TestAnOwnerOnlyCommandRefusesEveryoneElse(t *testing.T) {
	h := start(t)
	alice, aliceToken := h.signUp("alice@example.com")
	_, bobToken := h.signUp("bob@example.com")
	id := h.mustRun("studio/project/create", aliceToken, map[string]any{"name": "uione.io"})

	h.expect("studio/project/delete", bobToken, map[string]any{"id": id}, 403, "only its owner can do this")
	h.mustRun("studio/project/delete", aliceToken, map[string]any{"id": id})
	rows, _ := h.view("studio::projects:" + alice)["rows"].([]any)
	if len(rows) != 0 {
		t.Fatalf("a deleted project leaves the view: %v", rows)
	}
	h.expect("studio/project/delete", aliceToken, map[string]any{"id": id}, 404, "that doesn't exist any more")
}

func TestARolesPermissionsLetSomeoneRunACommand(t *testing.T) {
	h := start(t)
	carol, carolToken := h.signUp("carol@example.com")
	h.expect("notes/note/create", carolToken, map[string]any{"text": "hello"}, 403, "you don't have permission to do this")

	if _, err := h.store.Collection("users").Doc(carol).Set(context.Background(), map[string]any{"role_id": "writer"}); err != nil {
		t.Fatal(err)
	}
	h.mustRun("notes/note/create", carolToken, map[string]any{"text": "hello"})
}

func TestAUniqueValueCantBeUsedTwice(t *testing.T) {
	h := start(t)
	carol, carolToken := h.signUp("carol@example.com")
	h.store.Collection("users").Doc(carol).Set(context.Background(), map[string]any{"role_id": "writer"})
	h.mustRun("notes/note/create", carolToken, map[string]any{"text": "one", "code": "A1"})
	h.expect("notes/note/create", carolToken, map[string]any{"text": "two", "code": "A1"}, 400, "Code is already taken")
}

func TestACommandsBodyRunsOnTheEntity(t *testing.T) {
	h := start(t)
	_, token := h.signUp("dana@example.com")
	id := h.mustRun("tasks/task/create", token, map[string]any{"title": "write the docs"})
	h.mustRun("tasks/task/complete", token, map[string]any{"id": id})
	h.expect("tasks/task/complete", token, map[string]any{"id": id}, 400, "that task is already done")
}

func TestAViewsRowsFollowItsOrder(t *testing.T) {
	h := start(t)
	me, token := h.signUp("erin@example.com")
	first := h.mustRun("tasks/task/create", token, map[string]any{"title": "first"})
	h.mustRun("tasks/task/create", token, map[string]any{"title": "second"})
	h.mustRun("tasks/task/create", token, map[string]any{"title": "third"})
	h.mustRun("tasks/task/complete", token, map[string]any{"id": first})
	var titles []string
	for _, row := range h.view("tasks::list:" + me)["rows"].([]any) {
		titles = append(titles, row.(map[string]any)["title"].(string))
	}
	// Not done first, newest first among those, then the done one.
	if fmt.Sprint(titles) != "[third second first]" {
		t.Fatalf("rows out of order: %v", titles)
	}
}

func TestTheServiceAnswersAtHealth(t *testing.T) {
	h := start(t)
	response, err := http.Get(h.server.URL + "/health")
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		t.Errorf("GET /health gave %d", response.StatusCode)
	}
}

func TestMakingAgainSomethingSomeoneElseOwnsIsRefused(t *testing.T) {
	h := start(t)
	_, ada := h.signUp("ada@example.com")
	_, bob := h.signUp("bob@example.com")
	h.mustRun("studio/handle/create", ada, map[string]any{"name": "ada"})
	h.expect("studio/handle/create", bob, map[string]any{"name": "ada"}, http.StatusConflict, "that belongs to someone else")
	h.mustRun("studio/handle/create", ada, map[string]any{"name": "ada", "tags": []any{"mine"}})
}

func TestARequestIsBounded(t *testing.T) {
	h := start(t)
	_, ada := h.signUp("ada@example.com")
	many := make([]any, 1001)
	for i := range many {
		many[i] = fmt.Sprint(i)
	}
	h.expect("studio/handle/create", ada, map[string]any{"name": "ada", "tags": many}, http.StatusBadRequest, "Tags can hold at most 1000")
	h.expect("studio/project/delete", ada, map[string]any{"id": "a/b"}, http.StatusNotFound, "that doesn't exist")
	h.expect("studio/project/delete", ada, map[string]any{"id": "__name__"}, http.StatusNotFound, "that doesn't exist")
}
