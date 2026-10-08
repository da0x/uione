// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"context"
	"fmt"
	"reflect"
	"strings"
	"time"

	"cloud.google.com/go/firestore"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// OnceSpec is work the backend does once in its life, made with Once.
type OnceSpec struct {
	name string
	work []func(*System) error
}

// Once does work the first time a backend with it starts, and never again: a change
// to what's stored that a deploy brings, like moving every project to a new
// workflow. It runs as the backend itself, so it may run any command, before any
// view is rebuilt. That it's done is kept in once/<name>, so it's done once however
// many times the backend starts. If it fails, the backend doesn't start, and the
// deploy says so.
//
// Its work is done in the order it's given, each part once the one before is
// done: written by hand, or made with Each.
func Once(name string, work ...func(*System) error) *OnceSpec {
	return &OnceSpec{name: name, work: work}
}

// Each is work for a Once: body done to every stored entity of a kind, or to those
// whose field holds a value, like every role named developer with
// Each[Role]("name", "developer", ...), and an empty field for all of them. Each
// entity is changed in a transaction of its own, the way an update changes it: what
// the body makes is made with it, it's checked against its rules, and its history
// says it was updated.
func Each[E any, P entityPointer[E]](field string, value any, body func(*Ctx, *E) error) func(*System) error {
	return func(s *System) error {
		var only *string
		if field != "" {
			only = &field
		}
		all, err := fetch[E, P](s, only, value)
		if err != nil {
			return err
		}
		for _, entity := range all {
			if err := change[E, P](s, P(entity).record().ID, body); err != nil {
				return err
			}
		}
		return nil
	}
}

// change does body to one stored entity, as its Once.
func change[E any, P entityPointer[E]](s *System, id string, do func(*Ctx, *E) error) error {
	a := s.app
	schema := a.reg.schemas[reflect.TypeFor[E]()]
	now := time.Now().UTC()
	ref := a.store.Collection(schema.collection).Doc(id)
	var before, after map[string]any
	var events []event
	err := a.store.RunTransaction(s.ctx, func(ctx context.Context, tx *firestore.Transaction) error {
		entity := new(E)
		v := reflect.ValueOf(entity).Elem()
		snap, err := tx.Get(ref)
		if err != nil {
			return err
		}
		if err := snap.DataTo(entity); err != nil {
			return err
		}
		record := P(entity).record()
		record.ID = id
		before = snap.Data()
		was := schema.data(v)
		body := &Ctx{Context: ctx, me: s.me, now: now, app: a, tx: tx, counters: map[string]*counting{}, command: schema.entity + "::update", before: before}
		body.entity = &owned{schema, v}
		if err := do(body, entity); err != nil {
			return err
		}
		if err := schema.validate(v); err != nil {
			return err
		}
		if err := a.unique(tx, schema, v, id); err != nil {
			return err
		}
		// Something it only makes beside it, like a project's phases, leaves it as
		// it was, and its history without a change.
		changed := !reflect.DeepEqual(schema.data(v), was)
		if changed {
			record.UpdatedAt, record.UpdatedBy = now, ""
		}
		after = schema.data(v)
		made, err := body.save()
		if err != nil {
			return err
		}
		events = made
		if !changed {
			return nil
		}
		kept, err := a.keep(body.Context, tx, schema, id, before, after, body.command, "", now)
		if err != nil {
			return err
		}
		events = append(events, event{Type: schema.name + ".updated", Entity: schema.entity, ID: id, Version: now.UnixNano(), Before: before, After: after})
		events = append(events, kept...)
		return tx.Set(ref, after)
	})
	if err != nil {
		return fmt.Errorf("%s %s: %w", schema.name, id, err)
	}
	for _, ev := range events {
		a.publish(s.ctx, ev)
	}
	return nil
}

func (o *OnceSpec) register(r *registry, _ string) { r.once = append(r.once, o) }

func (a *App) doOnce(ctx context.Context) error {
	for _, o := range a.reg.once {
		done := a.store.Collection("once").Doc(o.name)
		if _, err := done.Get(ctx); err == nil {
			continue
		} else if status.Code(err) != codes.NotFound {
			return err
		}
		for _, work := range o.work {
			if err := work(&System{app: a, ctx: ctx}); err != nil {
				return fmt.Errorf("one: once %s: %w", o.name, err)
			}
		}
		if _, err := done.Set(ctx, map[string]any{"name": o.name, "at": time.Now()}); err != nil {
			return err
		}
	}
	return nil
}

// MyEmail is the email of the person signing in, in work done OnSignIn, as in
// Each[Invitation]("email", MyEmail, ...): the invitations sent to them.
var MyEmail = &marker{"my email"}

// SignInSpec is work done each time someone opens the app signed in, made with
// OnSignIn.
type SignInSpec struct {
	work []func(*System) error
}

// OnSignIn is work done each time someone opens the app signed in, as them: what
// it makes, it makes in their name, and MyEmail is the email their sign-in vouches
// for, like joining the projects invitations to that email asked them to. Someone
// whose sign-in vouches for no email, like a GitHub account Firebase doesn't
// verify, brings nothing about. It's done in the order it's given, made with Each
// and DeleteEach.
func OnSignIn(work ...func(*System) error) *SignInSpec { return &SignInSpec{work: work} }

func (o *SignInSpec) register(r *registry, _ string) { r.signin = append(r.signin, o) }

// DeleteEach is work that deletes every stored entity of a kind whose field holds a
// value, each in a transaction of its own and kept in its history, as a delete
// command would: like the invitations to someone who's now joined.
func DeleteEach[E any, P entityPointer[E]](field string, value any) func(*System) error {
	return func(s *System) error {
		all, err := fetch[E, P](s, &field, value)
		if err != nil {
			return err
		}
		for _, entity := range all {
			id := P(entity).record().ID
			if err := change[E, P](s, id, func(c *Ctx, _ *E) error { return DeleteWhere[E, P](c, "id", id) }); err != nil {
				return err
			}
		}
		return nil
	}
}

// signedIn does each OnSignIn's work for someone who's opened the app, when their
// sign-in vouches for an email.
func (a *App) signedIn(ctx context.Context, me, email string) error {
	if email == "" {
		return nil
	}
	s := &System{app: a, ctx: ctx, me: me, email: strings.ToLower(email)}
	for _, o := range a.reg.signin {
		for _, work := range o.work {
			if err := work(s); err != nil {
				return err
			}
		}
	}
	return nil
}

// Invites makes an entity an invitation to become another, like an invitation to be
// a project's member: when someone signs in with its email, the other is made from
// its fields of the same name, like the project and the role, with its person
// field the one signing in, and the invitation is deleted, both in one transaction,
// so neither happens without the other. Who joins is then known by who they are,
// not by their email, so changing it later changes nothing. Only a sign-in that
// vouches for its email takes one; the email is matched whatever its capitals.
func Invites[I any, M any, PI entityPointer[I], PM entityPointer[M]](email, person string) *SignInSpec {
	return OnSignIn(func(s *System) error {
		all, err := fetch[I, PI](s, &email, MyEmail)
		if err != nil {
			return err
		}
		for _, invitation := range all {
			id := PI(invitation).record().ID
			err := change[I, PI](s, id, func(c *Ctx, i *I) error {
				made := new(M)
				from, to := reflect.ValueOf(i).Elem(), reflect.ValueOf(made).Elem()
				ownSchema, madeSchema := c.app.reg.schemas[reflect.TypeFor[I]()], c.app.reg.schemas[reflect.TypeFor[M]()]
				for _, f := range madeSchema.fields {
					if f.name == person {
						to.FieldByIndex(f.index).SetString(c.Me())
						continue
					}
					if same := ownSchema.field(f.name); same != nil && same.typ == f.typ {
						to.FieldByIndex(f.index).Set(from.FieldByIndex(same.index))
					}
				}
				if err := Create[M, PM](c, made); err != nil {
					return err
				}
				return DeleteWhere[I, PI](c, "id", id)
			})
			if err != nil {
				return err
			}
		}
		return nil
	})
}
