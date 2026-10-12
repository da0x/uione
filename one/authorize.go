// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"net/http"
	"regexp"
	"strings"
	"time"

	"cloud.google.com/go/firestore"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// A program on someone's computer, like an app's command, signs in as them through
// the browser, the way a native app does with OAuth (RFC 8252 and 7636): it listens
// on 127.0.0.1, opens the app's /authorize page with a challenge, the person signs
// in there as usual and allows it, and the page sends a code back to the program.
// The program trades the code, with the verifier only it knows, at /api/token for a
// Firebase custom token of the person's own, which it signs in with.
//
// A code is good once, for five minutes. Only its hash is kept, in authorizations/,
// which the catch-all rule keeps browsers out of.

const authorizationLife = 5 * time.Minute

// claimCommandLine marks a Firebase sign-in a program on someone's computer made as
// a person, so it can be told from their browser's.
const claimCommandLine = "command_line"

// A PKCE challenge or verifier: base64url without padding, 43 to 128 characters.
var pkce = regexp.MustCompile(`^[A-Za-z0-9_-]{43,128}$`)

func hashed(code string) string {
	sum := sha256.Sum256([]byte(code))
	return hex.EncodeToString(sum[:])
}

// serveAuthorize gives a person signed in on the /authorize page a code for the
// program that asked, bound to the program's challenge.
func (a *App) serveAuthorize(w http.ResponseWriter, r *http.Request) {
	who, err := a.caller(r)
	if err != nil {
		replyFailure(w, a, "authorizing a program", err)
		return
	}
	if who.id == "" {
		reply(w, http.StatusUnauthorized, map[string]any{"error": "sign in first, then allow it"})
		return
	}
	if who.service {
		reply(w, http.StatusForbidden, map[string]any{"error": "a service signs in with its key"})
		return
	}
	var sent struct {
		Challenge string `json:"challenge"`
	}
	if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<12)).Decode(&sent); err != nil || !pkce.MatchString(sent.Challenge) {
		reply(w, http.StatusBadRequest, map[string]any{"error": "this sign-in link isn't complete; run the program's login again"})
		return
	}
	random := make([]byte, 32)
	if _, err := rand.Read(random); err != nil {
		replyFailure(w, a, "authorizing a program", err)
		return
	}
	code := base64.RawURLEncoding.EncodeToString(random)
	kept := map[string]any{"person": who.id, "challenge": sent.Challenge, "expires": time.Now().Add(authorizationLife)}
	if _, err := a.store.Collection("authorizations").Doc(hashed(code)).Set(r.Context(), kept); err != nil {
		replyFailure(w, a, "authorizing a program", err)
		return
	}
	reply(w, http.StatusOK, map[string]any{"code": code})
}

// authorized trades a code and its verifier for the person it was given to, once.
func (a *App) authorized(ctx context.Context, code, verifier string) (string, error) {
	refused := &Failure{Status: http.StatusBadRequest, Message: "that sign-in was already used, took too long, or isn't this program's; run login again"}
	if code == "" || !pkce.MatchString(verifier) {
		return "", refused
	}
	ref := a.store.Collection("authorizations").Doc(hashed(code))
	var person string
	err := a.store.RunTransaction(ctx, func(ctx context.Context, tx *firestore.Transaction) error {
		snap, err := tx.Get(ref)
		if status.Code(err) == codes.NotFound {
			return refused
		}
		if err != nil {
			return err
		}
		if err := tx.Delete(ref); err != nil {
			return err
		}
		kept := snap.Data()
		expires, _ := kept["expires"].(time.Time)
		challenge, _ := kept["challenge"].(string)
		sum := sha256.Sum256([]byte(verifier))
		if time.Now().After(expires) || base64.RawURLEncoding.EncodeToString(sum[:]) != challenge {
			return nil // deleted all the same: a wrong verifier spends the code
		}
		person, _ = kept["person"].(string)
		return nil
	})
	var failure *Failure
	if errors.As(err, &failure) {
		return "", failure
	}
	if err != nil {
		return "", err
	}
	if person == "" {
		return "", refused
	}
	return person, nil
}

// tokenForCode answers /api/token for a program trading its code.
func (a *App) tokenForCode(w http.ResponseWriter, r *http.Request) {
	var sent struct {
		Code     string `json:"code"`
		Verifier string `json:"verifier"`
	}
	_ = json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<12)).Decode(&sent)
	if sent.Code == "" {
		reply(w, http.StatusUnauthorized, map[string]any{"error": "a token is given for a service's key, or for a sign-in's code"})
		return
	}
	person, err := a.authorized(r.Context(), sent.Code, strings.TrimSpace(sent.Verifier))
	if err != nil {
		replyFailure(w, a, "a program's sign-in", err)
		return
	}
	token, err := a.auth.CustomTokenWithClaims(r.Context(), person, map[string]any{claimCommandLine: true})
	if err != nil {
		replyFailure(w, a, "a program's sign-in", err)
		return
	}
	reply(w, http.StatusOK, map[string]any{"token": token})
}
