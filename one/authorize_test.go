// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one_test

import (
	"crypto/sha256"
	"encoding/base64"
	"net/http"
	"testing"
)

// A program on someone's computer signs in as them through their browser: the page
// gives a code for the program's challenge, and the program trades the code, with
// its verifier, for a sign-in of the person's own.

const verifier = "a-verifier-only-the-program-knows-0123456789abcdef"

func challengeOf(verifier string) string {
	sum := sha256.Sum256([]byte(verifier))
	return base64.RawURLEncoding.EncodeToString(sum[:])
}

func (h *harness) codeFor(token, challenge string) string {
	h.t.Helper()
	status, reply := h.run("authorize", token, map[string]any{"challenge": challenge})
	code, _ := reply["code"].(string)
	if status != http.StatusOK || code == "" {
		h.t.Fatalf("a person signed in gets a code for a program: %d %v", status, reply)
	}
	return code
}

func TestAProgramSignsInAsThePersonWhoAllowedIt(t *testing.T) {
	h := start(t)
	uid, token := h.signUp("kit@example.com")
	token = h.rename(token, "kit@example.com", "Kit", "")
	h.mustRun("yard/yard/create", token, map[string]any{"slug": "dry"})

	code := h.codeFor(token, challengeOf(verifier))
	status, reply := h.run("token", "", map[string]any{"code": code, "verifier": verifier})
	custom, _ := reply["token"].(string)
	if status != http.StatusOK || custom == "" {
		t.Fatalf("a code and its verifier trade for a custom token: %d %v", status, reply)
	}
	program := h.signInWithCustomToken(custom)
	if me := h.get("/api/me", program); me["id"] != uid || me["service"] != false {
		t.Fatalf("the program is the person: %v", me)
	}
	// It acts as them, and their name stays as it was.
	h.mustRun("yard/yard/create", program, map[string]any{"slug": "wet"})
	if name := h.stored("users", uid)["name"]; name != "Kit" {
		t.Fatalf("the person's name is kept: %v", name)
	}
	// A code is good once.
	if status, _ := h.run("token", "", map[string]any{"code": code, "verifier": verifier}); status != http.StatusBadRequest {
		t.Fatalf("a code used once is refused: %d", status)
	}
}

func TestACodeIsGoodOnlyWithItsOwnVerifier(t *testing.T) {
	h := start(t)
	_, token := h.signUp("kit@example.com")
	code := h.codeFor(token, challengeOf(verifier))
	h.expectToken(code, "another-verifier-of-someone-who-saw-the-code-0123456789", http.StatusBadRequest)
	// A wrong verifier spends it, so it can't be tried again.
	h.expectToken(code, verifier, http.StatusBadRequest)
}

func TestOnlyAPersonSignedInAuthorizesAProgram(t *testing.T) {
	h := start(t)
	_, foreman := h.signUp("kit@example.com")
	h.mustRun("yard/yard/create", foreman, map[string]any{"slug": "dry"})
	_, key := h.keyOf(foreman, "dry", "Painter", "welder")
	h.expect("authorize", "", map[string]any{"challenge": challengeOf(verifier)}, 401, "sign in first, then allow it")
	h.expect("authorize", key, map[string]any{"challenge": challengeOf(verifier)}, 403, "a service signs in with its key")
	h.expect("authorize", foreman, map[string]any{"challenge": "short"}, 400, "this sign-in link isn't complete; run the program's login again")
	h.expectToken("", verifier, http.StatusUnauthorized)
}

func (h *harness) expectToken(code, verifier string, want int) {
	h.t.Helper()
	if status, reply := h.run("token", "", map[string]any{"code": code, "verifier": verifier}); status != want {
		h.t.Fatalf("trading a code: want %d, got %d %v", want, status, reply)
	}
}
