// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"os"
	"reflect"
	"strings"
	"sync"
	"time"
	"unicode"

	"cloud.google.com/go/firestore"
	firebase "firebase.google.com/go/v4"
	"firebase.google.com/go/v4/auth"
	"google.golang.org/api/option"
)

// Item is anything a module holds: commands, views, roles, or other modules.
type Item interface {
	register(r *registry, ns string)
}

// ModuleSpec is a namespace's commands and views, made with Module.
type ModuleSpec struct {
	name  string
	items []Item
}

// Module groups a namespace's commands and views. Everything in it is named inside
// the namespace, so Command("signup::create") in Module("waitlist") is
// waitlist::signup::create.
func Module(name string, items ...Item) *ModuleSpec { return &ModuleSpec{name, items} }

// Add puts more in a module from Go written by hand beside it, from an init
// function, like work done once: Module.Add(one.Once("...", migrate)).
func (m *ModuleSpec) Add(items ...Item) *ModuleSpec {
	m.items = append(m.items, items...)
	return m
}

func (m *ModuleSpec) register(r *registry, ns string) {
	for _, item := range m.items {
		item.register(r, join(ns, m.name))
	}
}

// RoleSpec is a role, made with Role.
type RoleSpec struct {
	name        string
	permissions []string
	scope       reflect.Type // for a role per entity: the entity it's held within, like a project
	member      reflect.Type // and the entity that grants it, like a project's member
}

// Role declares a role and the permissions it grants. Roles are written to
// roles/{name} when the app starts; people are given one with users/{id}.role_id.
func Role(name string, permissions ...string) *RoleSpec {
	return &RoleSpec{name: name, permissions: permissions}
}

// Per makes the role one a person holds within one entity, such as a project,
// rather than everywhere. The member entity grants it: a member that points at
// the project and the person, and names the role in its role field, gives that
// person the role's permissions on the project and on everything that points at
// it, like its issues. A role per entity is never written to roles/, so no one
// can be given it everywhere.
func (ro *RoleSpec) Per(scope, member Kind) *RoleSpec {
	ro.scope, ro.member = scope.typ, member.typ
	return ro
}

func (ro *RoleSpec) register(r *registry, ns string) {
	// A role given twice would have its second permissions replace its first, so
	// it's refused rather than half kept.
	if key := join(ns, ro.name); r.named[key] {
		r.twice = append(r.twice, key)
	} else {
		r.named[key] = true
	}
	for i, p := range ro.permissions {
		ro.permissions[i] = string(qualified(ns, Permission(p)))
	}
	if ro.scope == nil {
		r.roles[ro.name] = ro.permissions
		return
	}
	r.schema(ro.scope, ns)
	r.schema(ro.member, ns)
	r.scoped = append(r.scoped, ro)
}

type registry struct {
	commands map[string]func(*App, *call) (string, error)
	views    []*ViewSpec
	roles    map[string][]string
	scoped   []*RoleSpec  // roles held within an entity, like a project
	defined  []*RolesSpec // roles each project, or the like, defines for itself as records
	once     []*OnceSpec  // work done the first time a backend with it starts
	named    map[string]bool
	twice    []string // roles given more than once, which is a mistake
	hooks    []*GitHubSpec
	schemas  map[reflect.Type]*schema
}

// entity finds a known entity by its full name, like library::book.
func (r *registry) entity(name string) *schema {
	for _, s := range r.schemas {
		if s.entity == name {
			return s
		}
	}
	return nil
}

// historyOf is the entity whose changes a schema holds, like issue for issue_change,
// or nil for one that isn't a history.
func (r *registry) historyOf(changes *schema) *schema {
	for _, s := range r.schemas {
		if s.history == changes {
			return s
		}
	}
	return nil
}

// schema is the entity a Go type holds, named after the type in snake_case, inside
// the namespace it's used in: Signup in waitlist is waitlist::signup.
func (r *registry) schema(t reflect.Type, ns string) *schema {
	if s, ok := r.schemas[t]; ok {
		return s
	}
	// The changes of an entity are known through the entity: ChangeOf[Issue] is
	// what Issue keeps.
	if c, ok := reflect.Zero(t).Interface().(changer); ok {
		of := r.schema(c.changes(), ns)
		if of.history == nil {
			panic("one: " + of.name + " doesn't keep its history; embed one.History in it")
		}
		r.schemas[t] = of.history
		return of.history
	}
	s := schemaOf(t, join(ns, snake(t.Name())))
	r.schemas[t] = s
	return s
}

// qualified names a permission within its module: book:withdraw in library is
// library::book:withdraw, so a role in one module never grants another's.
func qualified(ns string, p Permission) Permission {
	switch p {
	case "", Anyone, SignedIn, Owner:
		return p
	}
	if strings.Contains(string(p), "::") {
		return p
	}
	return Permission(join(ns, string(p)))
}

func join(ns, name string) string {
	if ns == "" {
		return name
	}
	if name == "" {
		return ns
	}
	return ns + "::" + name
}

func snake(name string) string {
	var out strings.Builder
	for i, c := range name {
		if unicode.IsUpper(c) {
			if i > 0 {
				out.WriteByte('_')
			}
			c = unicode.ToLower(c)
		}
		out.WriteRune(c)
	}
	return out.String()
}

// App is a running backend: the store, sign-in, and everything its modules declare.
type App struct {
	store *firestore.Client
	auth  *auth.Client
	reg   *registry
	log   *log.Logger

	profiles sync.Map // each person's name and picture, as last saved
}

// New connects to the project's Firestore and Firebase Auth, writes its roles, and
// builds every view that doesn't depend on who's reading it.
//
// The project is GOOGLE_CLOUD_PROJECT. When FIRESTORE_EMULATOR_HOST is set, the
// emulators are used instead, with project demo-uione unless one is given.
func New(ctx context.Context, items ...Item) (*App, error) {
	project := os.Getenv("GOOGLE_CLOUD_PROJECT")
	emulated := os.Getenv("FIRESTORE_EMULATOR_HOST") != ""
	if project == "" && emulated {
		project = "demo-uione"
	}
	if project == "" {
		return nil, errors.New("one: set GOOGLE_CLOUD_PROJECT to the Firebase project's id")
	}
	store, err := firestore.NewClient(ctx, project)
	if err != nil {
		return nil, err
	}
	var options []option.ClientOption
	if os.Getenv("FIREBASE_AUTH_EMULATOR_HOST") != "" {
		options = append(options, option.WithoutAuthentication())
	}
	fb, err := firebase.NewApp(ctx, &firebase.Config{ProjectID: project}, options...)
	if err != nil {
		return nil, err
	}
	authClient, err := fb.Auth(ctx)
	if err != nil {
		return nil, err
	}

	reg := &registry{
		commands: map[string]func(*App, *call) (string, error){},
		roles:    map[string][]string{},
		named:    map[string]bool{},
		schemas:  map[reflect.Type]*schema{reflect.TypeFor[Profile](): profiles()},
	}
	for _, item := range items {
		item.register(reg, "")
	}
	if len(reg.twice) > 0 {
		return nil, fmt.Errorf("one: role %s is given more than once; give it every permission in one Role", reg.twice[0])
	}
	for _, v := range reg.views {
		if err := v.resolve(reg); err != nil {
			return nil, err
		}
	}
	for _, g := range reg.hooks {
		if _, _, err := g.numbered(reg); err != nil {
			return nil, err
		}
	}
	a := &App{store: store, auth: authClient, reg: reg, log: log.Default()}

	for name, permissions := range reg.roles {
		if _, err := store.Collection("roles").Doc(name).Set(ctx, map[string]any{"permissions": permissions}); err != nil {
			return nil, err
		}
	}
	// Views that don't depend on who's reading them exist from the start, so a screen
	// never waits for a first event to have something to show.
	version := time.Now().UnixNano()
	for _, v := range reg.views {
		if v.keyed() {
			continue
		}
		data, err := a.compose(ctx, v, "")
		if err != nil {
			return nil, err
		}
		if _, err := a.write(ctx, v, "", data, version, "start"); err != nil {
			return nil, err
		}
	}
	// Projects made before their roles were records get them before any view is
	// rebuilt, so the views show them.
	if err := a.seedEarlierRoles(ctx); err != nil {
		return nil, err
	}
	// Then what a deploy changes once, before views are rebuilt, so they show it.
	if err := a.doOnce(ctx); err != nil {
		return nil, err
	}
	// A view with a document per person or per entity is rebuilt where its
	// definition changed, so a deploy that changes it reaches every document.
	if err := a.rebuildChanged(ctx); err != nil {
		return nil, err
	}
	return a, nil
}

// Close lets go of the app's connection to Firestore.
func (a *App) Close() error { return a.store.Close() }

// Handler serves commands at POST /api/<namespace>/<entity>/<action>, with a JSON
// object as the body, and GET /health. (Cloud Run keeps paths ending in z, like
// /healthz, for itself.)
func (a *App) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /health", func(w http.ResponseWriter, _ *http.Request) {
		w.Write([]byte("ok\n"))
	})
	mux.HandleFunc("POST /api/", a.serveCommand)
	for _, g := range a.reg.hooks {
		mux.HandleFunc("POST "+g.route, a.serveGitHub(g))
	}
	routesMu.Lock()
	defer routesMu.Unlock()
	for _, r := range routes {
		mux.HandleFunc(r.pattern, func(w http.ResponseWriter, req *http.Request) {
			r.handle(&System{app: a, ctx: req.Context()}, w, req)
		})
	}
	return mux
}

func (a *App) serveCommand(w http.ResponseWriter, r *http.Request) {
	name := strings.ReplaceAll(strings.Trim(strings.TrimPrefix(r.URL.Path, "/api/"), "/"), "/", "::")
	command, ok := a.reg.commands[name]
	if !ok {
		reply(w, http.StatusNotFound, map[string]any{"error": "there's no command " + name})
		return
	}

	me := ""
	if header := r.Header.Get("Authorization"); header != "" {
		token, err := a.auth.VerifyIDToken(r.Context(), strings.TrimPrefix(header, "Bearer "))
		if err != nil {
			reply(w, http.StatusUnauthorized, map[string]any{"error": "your sign-in has expired; sign in again"})
			return
		}
		me = token.UID
		a.remember(r.Context(), token)
	}

	input := map[string]any{}
	if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<20)).Decode(&input); err != nil {
		reply(w, http.StatusBadRequest, map[string]any{"error": "the command needs a JSON object"})
		return
	}

	id, err := command(a, &call{ctx: r.Context(), me: me, input: input})
	var failure *Failure
	switch {
	case errors.As(err, &failure):
		reply(w, failure.Status, map[string]any{"error": failure.Message})
	case err != nil:
		a.log.Printf("one: %s failed: %v", name, err)
		reply(w, http.StatusInternalServerError, map[string]any{"error": "something went wrong on our side; try again"})
	default:
		reply(w, http.StatusOK, map[string]any{"id": id})
	}
}

func reply(w http.ResponseWriter, status int, body map[string]any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(body)
}

// Serve runs the backend on PORT, or 8081 when that isn't set.
func Serve(items ...Item) {
	ctx := context.Background()
	app, err := New(ctx, items...)
	if err != nil {
		log.Fatal(err)
	}
	defer app.Close()
	port := os.Getenv("PORT")
	if port == "" {
		port = "8081"
	}
	log.Printf("one: serving on :%s", port)
	server := &http.Server{
		Addr:              ":" + port,
		Handler:           app.Handler(),
		ReadHeaderTimeout: 10 * time.Second,
		ReadTimeout:       time.Minute,
		WriteTimeout:      2 * time.Minute,
		IdleTimeout:       2 * time.Minute,
	}
	log.Fatal(server.ListenAndServe())
}
