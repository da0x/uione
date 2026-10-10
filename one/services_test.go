// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/da0x/uione/one"
)

// Yards whose work is done by people and by service accounts: programs a yard gives
// a role made for services, the welder, which signs in with a key.

type Yard struct {
	one.Record
	Slug string `firestore:"slug" one:"required,unique,key"`
}

type Grade struct {
	one.Record
	Yard  string   `firestore:"yard" one:"required,key,refers=yard::yard"`
	Name  string   `firestore:"name" one:"required,key"`
	Title string   `firestore:"title" one:"required"`
	May   []string `firestore:"may"`
}

type Worker struct {
	one.Record
	Yard   string `firestore:"yard" one:"required,key,refers=yard::yard"`
	Person string `firestore:"person" one:"required,key,refers=user"`
	Grade   string `firestore:"grade" one:"required,key,refers=yard::grade"`
}

type Bot struct {
	one.Record
	Yard     string    `firestore:"yard" one:"required,key,refers=yard::yard"`
	Name     string    `firestore:"name" one:"required,key,from=title"`
	Title    string    `firestore:"title" one:"required"`
	Grade     string    `firestore:"grade" one:"required,refers=yard::grade"`
	KeyStart string    `firestore:"key_start"`
	KeyMade  time.Time `firestore:"key_made"`
	KeyUsed  time.Time `firestore:"key_used"`
}

type Hull struct {
	one.Record
	Yard  string `firestore:"yard" one:"required,refers=yard::yard"`
	Title string `firestore:"title" one:"required"`
	Done  bool   `firestore:"done"`
}

var yards = one.Module("yard",
	one.Command[Yard]("yard::create").Allow(one.SignedIn).Do(func(c *one.Ctx, y *Yard) error {
		return one.Create(c, &Worker{Yard: y.ID, Person: c.Me(), Grade: one.Key(y.ID, "foreman")})
	}),
	one.Command[Yard]("yard::follow").Allow(one.SignedIn),
	one.Command[Worker]("worker::create"),
	one.Command[Grade]("grade::update").Fields("title", "may"),
	one.Command[Bot]("bot::create"),
	one.Command[Bot]("bot::update").Fields("title", "grade"),
	one.Command[Bot]("bot::delete"),
	one.Command[Bot]("bot::key"),
	one.Command[Bot]("bot::revoke"),
	one.Command[Hull]("hull::create"),
	one.Command[Hull]("hull::finish").Do(func(c *one.Ctx, h *Hull) error {
		h.Done = true
		return nil
	}),
	one.View("hulls").Per(one.Entity[Yard]()).Readers(one.Entity[Worker]()).
		List("hulls", one.Where[Hull]("yard", one.Subject)).Fields("title", "done"),
	one.Roles(one.Entity[Grade](), one.Entity[Yard](), one.Entity[Worker](), "may").
		Default("foreman", "Foreman", "worker::create", "grade::update", "bot::create", "bot::update", "bot::delete", "bot::key", "bot::revoke", "hull::create", "hull::finish").
		Default("welder", "Welder", "hull::finish").
		Services(one.Entity[Bot](), "welder"),
)

// keyOf makes a service in a yard and gives it a key, returned once.
func (h *harness) keyOf(foreman, yard, title, grade string) (string, string) {
	h.t.Helper()
	id := h.mustRun("yard/bot/create", foreman, map[string]any{"yard": yard, "title": title, "grade": one.Key(yard, grade)})
	status, reply := h.run("yard/bot/key", foreman, map[string]any{"id": id})
	key, _ := reply["key"].(string)
	if status != http.StatusOK || !strings.HasPrefix(key, "one_") {
		h.t.Fatalf("a new key is shown once: %d %v", status, reply)
	}
	return id, key
}

func TestAServiceIsAMemberWithItsRoleAndAProfileMarkedAService(t *testing.T) {
	h := start(t)
	_, foreman := h.signUp("fern@example.com")
	h.mustRun("yard/yard/create", foreman, map[string]any{"slug": "dock"})
	id, key := h.keyOf(foreman, "dock", "Spot Welder", "welder")
	if id != "dock-spot_welder" {
		t.Fatalf("a service is named by its yard and its title: %s", id)
	}
	ctx := context.Background()
	profile, err := h.store.Collection("users").Doc(id).Get(ctx)
	if err != nil || profile.Data()["name"] != "Spot Welder" || profile.Data()["service"] != true {
		t.Fatalf("a service has a profile, named by its title and marked a service: %v %v", profile.Data(), err)
	}
	if _, err := h.store.Collection("yard_worker").Doc(one.Key("dock", id, one.Key("dock", "welder"))).Get(ctx); err != nil {
		t.Fatalf("a service holds its role as a member: %v", err)
	}
	// Only the key's hash is kept.
	docs, _ := h.store.Collection("keys").Where("service", "==", id).Documents(ctx).GetAll()
	if len(docs) != 1 || strings.Contains(key, docs[0].Data()["hash"].(string)) {
		t.Fatalf("one key is kept, as a hash: %v", docs)
	}
	bot, _ := h.store.Collection("yard_bot").Doc(id).Get(ctx)
	if start, _ := bot.Data()["key_start"].(string); !strings.HasPrefix(key, start) || len(start) < 8 {
		t.Fatalf("the service shows its key's first characters: %v", bot.Data())
	}
	// It runs what its role allows, and reads what members read.
	hull := h.mustRun("yard/hull/create", foreman, map[string]any{"yard": "dock", "title": "tug"})
	h.mustRun("yard/hull/finish", key, map[string]any{"id": hull})
	readers, _ := h.view("yard::hulls:dock")["readers"].([]any)
	if !contains(readers, id) {
		t.Fatalf("a service reads what members read: %v", readers)
	}
}

func TestAServiceDoesOnlyWhatItsRoleAllowsInItsYard(t *testing.T) {
	h := start(t)
	_, foreman := h.signUp("gil@example.com")
	h.mustRun("yard/yard/create", foreman, map[string]any{"slug": "slip"})
	h.mustRun("yard/yard/create", foreman, map[string]any{"slug": "quay"})
	_, key := h.keyOf(foreman, "slip", "Riveter", "welder")
	// Not what its role doesn't allow, nor what anyone signed in may run.
	h.expect("yard/hull/create", key, map[string]any{"yard": "slip", "title": "barge"}, 403, "you don't have permission to do this")
	h.expect("yard/yard/create", key, map[string]any{"slug": "mine"}, 403, "a service acts only through its role in its project")
	// Not in another yard.
	other := h.mustRun("yard/hull/create", foreman, map[string]any{"yard": "quay", "title": "dinghy"})
	h.expect("yard/hull/finish", key, map[string]any{"id": other}, 403, "you don't have permission to do this")
	// Never what hands out access, even when its role is edited to allow it.
	h.mustRun("yard/grade/update", foreman, map[string]any{"id": one.Key("slip", "welder"), "may": []any{"hull::finish", "worker::create", "bot::key"}})
	h.expect("yard/worker/create", key, map[string]any{"yard": "slip", "person": "someone", "grade": one.Key("slip", "foreman")}, 403, "a service can't change who has access")
	h.expect("yard/bot/key", key, map[string]any{"id": one.Key("slip", "riveter")}, 403, "a service can't change who has access")
}

func TestAServiceHoldsOnlyARoleMadeForServices(t *testing.T) {
	h := start(t)
	_, foreman := h.signUp("hal@example.com")
	h.mustRun("yard/yard/create", foreman, map[string]any{"slug": "berth"})
	h.expect("yard/bot/create", foreman, map[string]any{"yard": "berth", "title": "Boss", "grade": one.Key("berth", "foreman")}, 400,
		"a service can only hold a role made for services, like welder")
}

func TestARevokedOrReplacedKeyIsRefusedAtOnce(t *testing.T) {
	h := start(t)
	_, foreman := h.signUp("ida@example.com")
	h.mustRun("yard/yard/create", foreman, map[string]any{"slug": "pier"})
	id, first := h.keyOf(foreman, "pier", "Grinder", "welder")
	hull := h.mustRun("yard/hull/create", foreman, map[string]any{"yard": "pier", "title": "ferry"})
	refused := "that key isn't one this app gave, or it was revoked"
	// A new key replaces the old one.
	_, reply := h.run("yard/bot/key", foreman, map[string]any{"id": id})
	second := reply["key"].(string)
	h.expect("yard/hull/finish", first, map[string]any{"id": hull}, 401, refused)
	h.mustRun("yard/hull/finish", second, map[string]any{"id": hull})
	// A wrong secret with a right id is refused too.
	h.expect("yard/hull/finish", second[:len(second)-4]+"AAAA", map[string]any{"id": hull}, 401, refused)
	// Revoked, it's refused.
	h.mustRun("yard/bot/revoke", foreman, map[string]any{"id": id})
	h.expect("yard/hull/finish", second, map[string]any{"id": hull}, 401, refused)
	// Deleted, it's no member, and its profile stays so history still names it.
	h.mustRun("yard/bot/delete", foreman, map[string]any{"id": id})
	if _, err := h.store.Collection("users").Doc(id).Get(context.Background()); err != nil {
		t.Fatalf("a deleted service's profile stays: %v", err)
	}
	workers, _ := h.store.Collection("yard_worker").Where("person", "==", id).Documents(context.Background()).GetAll()
	if len(workers) != 0 {
		t.Fatalf("a deleted service holds no role: %v", workers)
	}
}

func TestRenamingAServiceRenamesItsProfile(t *testing.T) {
	h := start(t)
	_, foreman := h.signUp("jo@example.com")
	h.mustRun("yard/yard/create", foreman, map[string]any{"slug": "wharf"})
	id, _ := h.keyOf(foreman, "wharf", "Sander", "welder")
	h.mustRun("yard/bot/update", foreman, map[string]any{"id": id, "title": "Polisher"})
	profile, _ := h.store.Collection("users").Doc(id).Get(context.Background())
	if profile.Data()["name"] != "Polisher" {
		t.Fatalf("a renamed service's profile has its new name: %v", profile.Data())
	}
}

func TestAKeyTradesForASignInOfTheServiceAndSaysWhoItIs(t *testing.T) {
	h := start(t)
	_, foreman := h.signUp("kit@example.com")
	h.mustRun("yard/yard/create", foreman, map[string]any{"slug": "dry"})
	id, key := h.keyOf(foreman, "dry", "Painter", "welder")

	me := h.get("/api/me", key)
	if me["id"] != id || me["service"] != true || me["yard"] != "dry" || me["role"] != one.Key("dry", "welder") || me["name"] != "Painter" {
		t.Fatalf("a key says which service it is, where, and its role: %v", me)
	}
	status, reply := h.grade("/api/token", key)
	token, _ := reply["token"].(string)
	if status != http.StatusOK || token == "" {
		t.Fatalf("a key trades for a custom token: %d %v", status, reply)
	}
	// Signed in with it, the service is still a service.
	idToken := h.signInWithCustomToken(token)
	hull := h.mustRun("yard/hull/create", foreman, map[string]any{"yard": "dry", "title": "yacht"})
	h.mustRun("yard/hull/finish", idToken, map[string]any{"id": hull})
	h.expect("yard/yard/create", idToken, map[string]any{"slug": "theirs"}, 403, "a service acts only through its role in its project")
	// A person's sign-in gets no custom token, and a key doesn't sign in.
	if status, _ := h.grade("/api/token", foreman); status != http.StatusUnauthorized {
		t.Fatalf("only a key trades for a token: %d", status)
	}
	if status, _ := h.grade("/api/signin", key); status != http.StatusBadRequest {
		t.Fatalf("a key doesn't sign in: %d", status)
	}
}

func (h *harness) get(path, credential string) map[string]any {
	h.t.Helper()
	request, _ := http.NewRequest(http.MethodGet, h.server.URL+path, nil)
	request.Header.Set("Authorization", "Bearer "+credential)
	response, err := http.DefaultClient.Do(request)
	if err != nil {
		h.t.Fatal(err)
	}
	defer response.Body.Close()
	answer := map[string]any{}
	json.NewDecoder(response.Body).Decode(&answer)
	return answer
}

func (h *harness) grade(path, credential string) (int, map[string]any) {
	h.t.Helper()
	request, _ := http.NewRequest(http.MethodPost, h.server.URL+path, strings.NewReader("{}"))
	request.Header.Set("Authorization", "Bearer "+credential)
	response, err := http.DefaultClient.Do(request)
	if err != nil {
		h.t.Fatal(err)
	}
	defer response.Body.Close()
	answer := map[string]any{}
	json.NewDecoder(response.Body).Decode(&answer)
	return response.StatusCode, answer
}

// signInWithCustomToken trades a custom token at the Auth emulator for an ID token.
func (h *harness) signInWithCustomToken(token string) string {
	h.t.Helper()
	body, _ := json.Marshal(map[string]any{"token": token, "returnSecureToken": true})
	url := "http://" + os.Getenv("FIREBASE_AUTH_EMULATOR_HOST") + "/identitytoolkit.googleapis.com/v1/accounts:signInWithCustomToken?key=any"
	response, err := http.Post(url, "application/json", bytes.NewReader(body))
	if err != nil {
		h.t.Fatal(err)
	}
	defer response.Body.Close()
	var signedIn struct {
		IDToken string `json:"idToken"`
	}
	json.NewDecoder(response.Body).Decode(&signedIn)
	if signedIn.IDToken == "" {
		h.t.Fatal("the Auth emulator didn't take the custom token")
	}
	return signedIn.IDToken
}
