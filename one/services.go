// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base32"
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"reflect"
	"strings"
	"sync"
	"time"

	"cloud.google.com/go/firestore"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// Service accounts: programs, like an agent that works the issues in a phase, that
// a project gives a role as it gives people one. Each is a record of the project,
// named by its title, holding one role made for services. It's a member of the
// project like a person, so it reads what members read and runs what its role
// allows, and nothing else: no command anyone signed in may run, no other project,
// and nothing that hands out access, whatever a role record is edited to say.
//
// A service signs in with a key, shown once when it's made and kept only as a
// hash in keys/{id}, which no browser reads. The key runs commands, and trades at
// /api/token for a Firebase sign-in of the service's own id, so it reads views
// under the same rules as a person.

// Services lets each project's service accounts, records of service, hold the
// roles named, the ones made for services, like agent.
func (r *RolesSpec) Services(service Kind, roles ...string) *RolesSpec {
	r.service = service.typ
	r.forServices = roles
	return r
}

// The fields of a service: its project, its title, its role, and what's shown of
// its key: the first characters, when it was made, and when it was last used.
type servicesFields struct {
	*rolesFields
	service                    *schema
	place, title, role         *field
	keyStart, keyMade, keyUsed *field
}

func (a *App) servicesFields(r *RolesSpec) (*servicesFields, error) {
	roles, err := a.rolesFields(r)
	if err != nil {
		return nil, err
	}
	f := &servicesFields{rolesFields: roles, service: a.reg.schemas[r.service]}
	for i := range f.service.fields {
		switch g := &f.service.fields[i]; {
		case g.refers == roles.scope.entity:
			f.place = g
		case g.refers == roles.role.entity:
			f.role = g
		case g.name == "title":
			f.title = g
		case g.name == "key_start":
			f.keyStart = g
		case g.name == "key_made":
			f.keyMade = g
		case g.name == "key_used":
			f.keyUsed = g
		}
	}
	if f.place == nil || f.title == nil || f.role == nil {
		return nil, fmt.Errorf("one: services of %s are %s records, which need a field pointing at %s, a title and a role", roles.scope.name, f.service.name, roles.scope.name)
	}
	return f, nil
}

// servicesOf is the roles a service schema belongs to, or nil for any other.
func (a *App) servicesOf(s *schema) *RolesSpec {
	for _, r := range a.reg.defined {
		if r.service != nil && a.reg.schemas[r.service] == s {
			return r
		}
	}
	return nil
}

// write is a change a command's body makes outside any entity, like a key or a
// service's profile, saved with the rest once everything is read.
type write struct {
	ref    *firestore.DocumentRef
	data   map[string]any // nil deletes it
	merged bool
	event  *event
}

// serviceChanged does what a change to a service brings with it, in the command's
// transaction: on create, its profile and the member that gives it its role; on
// update, its new name or role; on delete, its member and its key; key gives it a
// new key, shown once, and revoke takes its key away.
func (a *App) serviceChanged(c *Ctx, s *schema, action, id string, v reflect.Value) error {
	r := a.servicesOf(s)
	if r == nil {
		return nil
	}
	f, err := a.servicesFields(r)
	if err != nil {
		return err
	}
	place := v.FieldByIndex(f.place.index).String()
	title := v.FieldByIndex(f.title.index).String()
	role := v.FieldByIndex(f.role.index).String()
	switch action {
	case "create":
		if err := a.serviceRole(c, r, f, place, role); err != nil {
			return err
		}
		c.writes = append(c.writes, a.serviceProfile(id, title, nil))
		return a.serviceMember(c, f, place, id, role)
	case "delete":
		if err := a.dropMembers(c, f, place, id); err != nil {
			return err
		}
		return a.dropKeys(c, id)
	case "key":
		if err := a.dropKeys(c, id); err != nil {
			return err
		}
		key, kept, err := newKey(id, place, c.me, c.now)
		if err != nil {
			return err
		}
		keyID := key[len(keyPrefix) : len(keyPrefix)+keyIDLength]
		c.writes = append(c.writes, write{ref: a.store.Collection("keys").Doc(keyID), data: kept})
		if f.keyStart != nil {
			v.FieldByIndex(f.keyStart.index).SetString(key[:len(keyPrefix)+6])
		}
		if f.keyMade != nil {
			v.FieldByIndex(f.keyMade.index).Set(reflect.ValueOf(c.now))
		}
		c.revealed = map[string]any{"key": key}
		return nil
	case "revoke":
		if f.keyStart != nil {
			v.FieldByIndex(f.keyStart.index).SetString("")
		}
		if f.keyMade != nil {
			v.FieldByIndex(f.keyMade.index).Set(reflect.ValueOf(time.Time{}))
		}
		return a.dropKeys(c, id)
	}
	// Any other change: a new title renames its profile, a new role moves its member.
	wasTitle, _ := c.before[f.title.name].(string)
	wasRole, _ := c.before[f.role.name].(string)
	if title != wasTitle {
		before := map[string]any{"id": id, "name": wasTitle, "service": true}
		c.writes = append(c.writes, a.serviceProfile(id, title, before))
	}
	if role != wasRole {
		if err := a.serviceRole(c, r, f, place, role); err != nil {
			return err
		}
		if err := a.dropMembers(c, f, place, id); err != nil {
			return err
		}
		return a.serviceMember(c, f, place, id, role)
	}
	return nil
}

// serviceRole checks a service is given a role of its own project made for services.
func (a *App) serviceRole(c *Ctx, r *RolesSpec, f *servicesFields, place, role string) error {
	if role == "" || !validID(role) {
		return invalid("a service needs a role")
	}
	snap, err := c.tx.Get(a.store.Collection(f.rolesFields.role.collection).Doc(role))
	if status.Code(err) == codes.NotFound {
		return invalid("that role doesn't exist")
	}
	if err != nil {
		return err
	}
	if where, _ := snap.Data()[f.rolesFields.place.name].(string); where != place {
		return invalid("a service's role is one of its own %s's", f.scope.name)
	}
	name, _ := snap.Data()[f.name.name].(string)
	if !contains(r.forServices, name) {
		return invalid("a service can only hold a role made for services, like %s", strings.Join(r.forServices, ", "))
	}
	return nil
}

// serviceProfile is the profile a service is shown by, as a person is by theirs: its
// title as its name, marked a service, with no picture and no username, so no
// @mention can name it.
func (a *App) serviceProfile(id, title string, before map[string]any) write {
	data := map[string]any{"id": id, "name": title, "service": true}
	return write{ref: a.store.Collection("users").Doc(id), data: data, merged: true,
		event: &event{Type: "user.updated", Entity: "user", ID: id, Before: before, After: data}}
}

// serviceMember makes the member that gives a service its role in its project.
func (a *App) serviceMember(c *Ctx, f *servicesFields, place, id, role string) error {
	m := reflect.New(f.member.typ).Elem()
	m.FieldByIndex(f.memberPlace.index).SetString(place)
	m.FieldByIndex(f.memberPerson.index).SetString(id)
	m.FieldByIndex(f.memberRole.index).SetString(role)
	return create(c, f.member, m, nil)
}

// dropMembers deletes the members that give a service its roles.
func (a *App) dropMembers(c *Ctx, f *servicesFields, place, id string) error {
	query := a.store.Collection(f.member.collection).Where(f.memberPlace.name, "==", place).Where(f.memberPerson.name, "==", id)
	docs, err := c.tx.Documents(query).GetAll()
	if err != nil {
		return err
	}
	for _, d := range docs {
		c.gone = append(c.gone, &gone{schema: f.member, ref: d.Ref, before: d.Data()})
	}
	return nil
}

// dropKeys deletes a service's key, so it signs in no more.
func (a *App) dropKeys(c *Ctx, id string) error {
	docs, err := c.tx.Documents(a.store.Collection("keys").Where("service", "==", id)).GetAll()
	if err != nil {
		return err
	}
	for _, d := range docs {
		c.writes = append(c.writes, write{ref: d.Ref})
	}
	return nil
}

// A key is one_, then the id of its record in keys, then _ and its secret:
// one_<16 letters and digits>_<43 characters of base64url>. Its prefix lets a scanner
// find one left somewhere it shouldn't be.
const (
	keyPrefix   = "one_"
	keyIDLength = 16
)

func newKey(service, place, by string, now time.Time) (string, map[string]any, error) {
	raw := make([]byte, 10+32)
	if _, err := rand.Read(raw); err != nil {
		return "", nil, err
	}
	id := strings.ToLower(base32.StdEncoding.WithPadding(base32.NoPadding).EncodeToString(raw[:10]))[:keyIDLength]
	secret := base64.RawURLEncoding.EncodeToString(raw[10:])
	hash := sha256.Sum256([]byte(secret))
	kept := map[string]any{"hash": hex.EncodeToString(hash[:]), "service": service, "place": place, "made_at": now, "made_by": by}
	return keyPrefix + id + "_" + secret, kept, nil
}

// keyed is the service a key signs in as, and the project it's held in, or why not.
// The key's record is read every time, so a revoked key is refused at once.
func (a *App) keyed(ctx context.Context, key string) (service, place string, err error) {
	refused := &Failure{Status: 401, Message: "that key isn't one this app gave, or it was revoked"}
	if !strings.HasPrefix(key, keyPrefix) || len(key) < len(keyPrefix)+keyIDLength+2 || key[len(keyPrefix)+keyIDLength] != '_' {
		return "", "", refused
	}
	id, secret := key[len(keyPrefix):len(keyPrefix)+keyIDLength], key[len(keyPrefix)+keyIDLength+1:]
	if !validID(id) {
		return "", "", refused
	}
	snap, err := a.store.Collection("keys").Doc(id).Get(ctx)
	if status.Code(err) == codes.NotFound {
		return "", "", refused
	}
	if err != nil {
		return "", "", err
	}
	stored, _ := snap.Data()["hash"].(string)
	hash := sha256.Sum256([]byte(secret))
	if subtle.ConstantTimeCompare([]byte(stored), []byte(hex.EncodeToString(hash[:]))) != 1 {
		return "", "", refused
	}
	service, _ = snap.Data()["service"].(string)
	place, _ = snap.Data()["place"].(string)
	if !a.allowKey(id) {
		return "", "", &Failure{Status: 429, Message: "too many requests with this key; slow down"}
	}
	a.keyUsed(ctx, service)
	return service, place, nil
}

// A key may make 10 requests a second, and 30 at once, on each instance of the
// backend; past that it's told to slow down.
const (
	keyRate  = 10.0
	keyBurst = 30.0
)

type bucket struct {
	tokens float64
	last   time.Time
}

var (
	bucketsMu sync.Mutex
	buckets   = map[string]*bucket{}
)

func (a *App) allowKey(id string) bool {
	bucketsMu.Lock()
	defer bucketsMu.Unlock()
	now := time.Now()
	b := buckets[id]
	if b == nil {
		b = &bucket{tokens: keyBurst, last: now}
		buckets[id] = b
	}
	b.tokens = min(keyBurst, b.tokens+now.Sub(b.last).Seconds()*keyRate)
	b.last = now
	if b.tokens < 1 {
		return false
	}
	b.tokens--
	return true
}

// keyUsed notes when a service last used its key, at most once an hour, so its
// project sees whether it's still in use.
func (a *App) keyUsed(ctx context.Context, service string) {
	if last, ok := a.used.Load(service); ok && time.Since(last.(time.Time)) < time.Hour {
		return
	}
	now := time.Now().UTC()
	a.used.Store(service, now)
	for _, r := range a.reg.defined {
		if r.service == nil {
			continue
		}
		f, err := a.servicesFields(r)
		if err != nil || f.keyUsed == nil {
			continue
		}
		ref := a.store.Collection(f.service.collection).Doc(service)
		snap, err := ref.Get(ctx)
		if err != nil {
			continue
		}
		before := snap.Data()
		if _, err := ref.Update(ctx, []firestore.Update{{Path: f.keyUsed.name, Value: now}}); err != nil {
			a.log.Printf("one: when %s last used its key wasn't kept: %v", service, err)
			continue
		}
		after := map[string]any{}
		for k, v := range before {
			after[k] = v
		}
		after[f.keyUsed.name] = now
		a.publish(ctx, event{Type: f.service.name + ".updated", Entity: f.service.entity, ID: service, Version: now.UnixNano(), Before: before, After: after})
	}
}

// managesAccess says whether a permission is for a command on what gives people and
// services their access: roles, members, services, and anything that hands out a
// role, like an invitation. No service may run one.
func (a *App) managesAccess(p Permission) bool {
	written := string(p)
	at := strings.LastIndex(written, ":")
	if at < 0 {
		return false
	}
	s := a.reg.entity(written[:at])
	if s == nil {
		return false
	}
	for _, r := range a.reg.defined {
		role := a.reg.schemas[r.role]
		if s == role || s == a.reg.schemas[r.member] || (r.service != nil && s == a.reg.schemas[r.service]) {
			return true
		}
		for _, f := range s.fields {
			if f.refers == role.entity {
				return true
			}
		}
	}
	return false
}

// permittedAs says whether the one running a command may: a person as permitted
// says, and a service only through the role its project gives it there, never as
// anyone, anyone signed in, or an owner.
func (a *App) permittedAs(ctx context.Context, tx *firestore.Transaction, me string, service bool, p Permission, entity *owned) error {
	if !service {
		return a.permitted(ctx, tx, me, p, entity)
	}
	if a.managesAccess(p) {
		return &Failure{Status: 403, Message: "a service can't change who has access"}
	}
	if p == Anyone || p == SignedIn || p == Owner || tx == nil || entity == nil {
		return &Failure{Status: 403, Message: "a service acts only through its role in its project"}
	}
	return a.permittedWithin(tx, me, p, entity)
}

// refusedOutright says whether a service is refused a permission before anything's
// read: one no role gives, or one that hands out access.
func (a *App) refusedOutright(p Permission) bool {
	return p == Anyone || p == SignedIn || p == Owner || a.managesAccess(p)
}
