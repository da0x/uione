// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"context"
	"net/http"
	"reflect"
	"strings"
	"sync"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// System is the backend's own code, written by hand beside the generated code: what
// runs After a command, or answers a Route. It may run any command, since it's the
// backend itself rather than a person, and read what's stored.
type System struct {
	app *App
	ctx context.Context
}

// Context is the request the system is working for.
func (s *System) Context() context.Context { return s.ctx }

// SignedIn says who sent a request to a Route, by the sign-in its Authorization
// header carries, as commands are told: their uid, or "" when nobody signed in. A
// sign-in that isn't valid, or has expired, is an error, so a route can't mistake it
// for nobody.
func (s *System) SignedIn(r *http.Request) (string, error) {
	header := r.Header.Get("Authorization")
	if header == "" {
		return "", nil
	}
	token, err := s.app.auth.VerifyIDToken(r.Context(), strings.TrimPrefix(header, "Bearer "))
	if err != nil {
		return "", &Failure{Status: http.StatusUnauthorized, Message: "your sign-in has expired; sign in again"}
	}
	s.app.remember(r.Context(), token)
	return token.UID, nil
}

// Run runs a command as the backend itself, which may run any command: a deploy's
// build reporting that it's done, say. It returns the id of what it changed.
func (s *System) Run(command string, input map[string]any) (string, error) {
	run, ok := s.app.reg.commands[command]
	if !ok {
		return "", &Failure{Status: 404, Message: "there's no command " + command}
	}
	return run(s.app, &call{ctx: s.ctx, input: input, system: true})
}

// Fetch reads one stored entity by its id, or nil when there's none.
func Fetch[E any, P entityPointer[E]](s *System, id string) (*E, error) {
	schema, ok := s.app.reg.schemas[reflect.TypeFor[E]()]
	if !ok {
		return nil, &Failure{Status: 500, Message: "this backend has no " + reflect.TypeFor[E]().Name()}
	}
	if !validID(id) {
		return nil, nil
	}
	snap, err := s.app.store.Collection(schema.collection).Doc(id).Get(s.ctx)
	if status.Code(err) == codes.NotFound {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	entity := new(E)
	if err := snap.DataTo(entity); err != nil {
		return nil, err
	}
	P(entity).record().ID = snap.Ref.ID
	return entity, nil
}

// FetchWhere reads every stored entity whose field has a value: a project's files,
// with FetchWhere[File](s, "project", id).
func FetchWhere[E any, P entityPointer[E]](s *System, field string, value any) ([]*E, error) {
	return fetch[E, P](s, &field, value)
}

// FetchAll reads every stored entity of a kind, like every project, for work done
// Once across all of them.
func FetchAll[E any, P entityPointer[E]](s *System) ([]*E, error) {
	return fetch[E, P](s, nil, nil)
}

func fetch[E any, P entityPointer[E]](s *System, field *string, value any) ([]*E, error) {
	schema, ok := s.app.reg.schemas[reflect.TypeFor[E]()]
	if !ok {
		return nil, &Failure{Status: 500, Message: "this backend has no " + reflect.TypeFor[E]().Name()}
	}
	query := s.app.store.Collection(schema.collection).Query
	if field != nil {
		query = query.Where(*field, "==", value)
	}
	docs, err := query.Documents(s.ctx).GetAll()
	if err != nil {
		return nil, err
	}
	found := make([]*E, 0, len(docs))
	for _, doc := range docs {
		entity := new(E)
		if err := doc.DataTo(entity); err != nil {
			return nil, err
		}
		P(entity).record().ID = doc.Ref.ID
		found = append(found, entity)
	}
	return found, nil
}

type route struct {
	pattern string
	handle  func(*System, http.ResponseWriter, *http.Request)
}

var (
	routesMu sync.Mutex
	routes   []route
)

// Route answers requests of the backend's own, like a build reporting back: Route
// ("POST /hooks/deploy", ...), from an init function beside the generated code. The
// handler is given the System, to read what's stored and run commands. Whoever
// calls a route isn't signed in, so the handler checks the request itself.
func Route(pattern string, handle func(s *System, w http.ResponseWriter, r *http.Request)) {
	routesMu.Lock()
	defer routesMu.Unlock()
	routes = append(routes, route{pattern, handle})
}
