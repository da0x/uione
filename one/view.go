// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"cmp"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"reflect"
	"sort"
	"strings"
	"time"

	"cloud.google.com/go/firestore"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// A view is a document prepared in advance for the screen that shows it, stored at
// views/<view>, or views/<view>:<person> for a view with one document per person.
// It's rebuilt whenever an entity it reads changes, and screens read it live.

type marker struct{ name string }

// Viewer stands for the person a per-person view belongs to, as in
// Where[Project]("owner", Viewer).
var Viewer = &marker{"viewer"}

// Row stands for the id of the row a value is worked out for, as in
// First[Loan]("book", Row): the loans of this row's book.
var Row = &marker{"row"}

// Subject stands for the entity a view with one document per entity is for, as in
// Where[Loan]("book", Subject) in a view per book: that book's loans.
var Subject = &marker{"subject"}

// SubjectField stands for a field of the entity a view per entity is for, as in
// Where[Step]("board", SubjectField("board")) in a view per issue: the steps of the
// issue's board.
func SubjectField(field string) any { return &subjectField{field} }

type subjectField struct{ field string }

// What the entity a document is for holds, for the conditions that name its fields,
// carried with the request that builds the document.
type subjectFields struct{}

// Kind names an entity type, for a view that has one document per entity of it.
type Kind struct{ typ reflect.Type }

// Entity names entity type E as a Kind, as in Per(Entity[Book]()).
func Entity[E any, P entityPointer[E]]() Kind { return Kind{reflect.TypeFor[E]()} }

// A field of the entity a view is for, copied into its document under a name.
type copied struct {
	name  string
	field string
}

// A condition on one field. Except turns it around: everything but that value.
// Has asks a list instead: the entities whose list holds the value.
type condition struct {
	field  string
	value  any // nil means none
	except bool
	has    bool
}

// Query picks the entities a view reads: all of them, or those that meet every
// condition, or every condition of one of its alternatives.
//
// A condition's field can be read through what the entity points at, like
// board.followers: Has("board.followers", Viewer) picks the changes of issues on
// the boards the person reading follows.
type Query struct {
	typ        reflect.Type
	conditions []condition
	or         []Query
}

// Or adds another way to be picked, as in
// Has[Change]("board.followers", Viewer).Or(All[Change]().Has("assignees", Viewer)):
// the changes on boards the person follows, or of issues assigned to them.
func (q Query) Or(other Query) Query {
	q.or = append(append([]Query(nil), q.or...), other)
	return q
}

// ways is the query as each of its alternatives, every one on its own.
func (q Query) ways() []Query {
	first := q
	first.or = nil
	return append([]Query{first}, q.or...)
}

// All picks every entity of type E.
func All[E any, P entityPointer[E]]() Query { return Query{typ: reflect.TypeFor[E]()} }

// Where picks the entities whose field has a value. A nil value means none.
func Where[E any, P entityPointer[E]](field string, value any) Query {
	return Query{typ: reflect.TypeFor[E]()}.And(field, value)
}

// First picks entities the same way as Where. It reads better where only the
// earliest made is wanted, in a Value.
func First[E any, P entityPointer[E]](field string, value any) Query {
	return Where[E, P](field, value)
}

// And adds another field that has to have a value.
func (q Query) And(field string, value any) Query {
	q.conditions = append(append([]condition(nil), q.conditions...), condition{field: field, value: value})
	return q
}

// Has picks the entities whose list holds a value, as in
// All[Issue]().Has("assignees", Viewer): the issues assigned to the person reading.
func (q Query) Has(field string, value any) Query {
	q.conditions = append(append([]condition(nil), q.conditions...), condition{field: field, value: value, has: true})
	return q
}

// Except leaves out the entities whose field has a value. It's checked after
// reading, so no view needs an index declared in advance to ask for it.
func (q Query) Except(field string, value any) Query {
	q.conditions = append(append([]condition(nil), q.conditions...), condition{field: field, value: value, except: true})
	return q
}

type count struct {
	name  string
	query Query
}

// A value worked out for each row: a field of the first entity a query finds.
type value struct {
	name  string
	query Query
	field string
}

// An ordering key: a field as stored, or a field passed through a function, such
// as a title without its "The".
type ordering struct {
	field   string
	reverse bool
	through func(any) any
}

// By orders rows by what a function makes of a field, as in By("title", SortTitle).
func By[T any, R any](field string, f func(T) R) any {
	return ordering{field: field, through: func(stored any) any {
		given, _ := stored.(T)
		return f(given)
	}}
}

// A list in a view: its rows, stored under "rows", or one of several named lists,
// like an issue's comments.
type list struct {
	name   string
	query  Query
	fields []string // names of the entity's fields, or through a reference, like book.title
	values []value
	order  []ordering
	limit  int // how many rows it keeps, once ordered; 0 keeps them all
	// In a person's own view, the pages that say who may read each row, like an
	// issue's page for the issues assigned to them: a row they may no longer read,
	// since they left its project or it turned private, is left out.
	guards []guard
}

// guard is a page that says who may read a row of a list: the page for the row's
// own entity, or for the entity a change is of, named by the row's field.
type guard struct {
	page  *ViewSpec
	field string // the row's field naming the page's entity, or "" for the row itself
}

// ViewSpec is a view, made with View and described by the calls that follow it.
type ViewSpec struct {
	name    string
	public  bool
	perUser bool
	per     reflect.Type // the entity each document is for, in a view per entity
	counts  []count
	firsts  []value // fields of the earliest of something, like when a person last looked
	lists   []*list
	copies  []copied
	secrets []string     // values holding the document's project's GitHub webhook secret
	readers reflect.Type // the entity whose people may read each document, like member
	people  []string     // fields of the document's entity naming people who may read it, like author
	open    *condition   // when a document is public, as a field of its entity, like project.visibility

	scope *schema // what each document is held within, like a project, once it's known

	full       string             // with its namespace, like waitlist::signups
	permission string             // who may read it, when it isn't public or per person
	reads      map[string]*schema // the entities it reads, by full name
}

// View declares a view: a document prepared ahead of time for the screens that show
// it, rebuilt whenever what it reads changes.
func View(name string) *ViewSpec { return &ViewSpec{name: name} }

// Public lets anyone read the view, signed in or not.
func (v *ViewSpec) Public() *ViewSpec {
	v.public = true
	return v
}

// PerUser gives the view one document per person, readable only by that person.
func (v *ViewSpec) PerUser() *ViewSpec {
	v.perUser = true
	return v
}

// Per gives the view one document per entity of a kind, as in a page per book. Its
// queries name that entity as Subject, and Copy puts its fields in the document.
func (v *ViewSpec) Per(kind Kind) *ViewSpec {
	v.per = kind.typ
	return v
}

// Readers lets the people a member entity names read each document: everyone with
// a role in the project the document's entity is held within, the project itself
// for a page per project, or an issue's project for a page per issue.
func (v *ViewSpec) Readers(member Kind) *ViewSpec {
	v.readers = member.typ
	return v
}

// ReadersFrom lets the person a field of the document's entity names read it, like
// a report's author, or each person a list of them names, like its assignees.
func (v *ViewSpec) ReadersFrom(field string) *ViewSpec {
	v.people = append(v.people, field)
	return v
}

// PublicWhen opens a document to everyone while a field of its entity has a
// value: PublicWhen("visibility", "public") on a page per project, or
// PublicWhen("project.visibility", "public") on a page per issue.
func (v *ViewSpec) PublicWhen(field string, value any) *ViewSpec {
	v.open = &condition{field: field, value: value}
	return v
}

// Copy puts a field of the entity the view is for into its document: Copy("title",
// "title") shows the book's title on the book's page.
func (v *ViewSpec) Copy(name, field string) *ViewSpec {
	v.copies = append(v.copies, copied{name, field})
	return v
}

// GitHubSecret puts in each document of a view per project that project's secret
// for GitHub's webhook, under a name. A view that does should be read only by the
// project's people, never public.
func (v *ViewSpec) GitHubSecret(name string) *ViewSpec {
	v.secrets = append(v.secrets, name)
	return v
}

// Count adds a value to the view: how many entities the query finds.
func (v *ViewSpec) Count(name string, q Query) *ViewSpec {
	v.counts = append(v.counts, count{name, q})
	return v
}

// Each lists the entities the query finds, as the view's rows. Order, Fields and
// Value after it are about these rows.
func (v *ViewSpec) Each(q Query) *ViewSpec { return v.List("rows", q) }

// List adds a list with a name, such as an issue's comments, beside the view's
// other values and lists. Order, Fields and Value after it are about this list.
func (v *ViewSpec) List(name string, q Query) *ViewSpec {
	v.lists = append(v.lists, &list{name: name, query: q})
	return v
}

// current is the list Order, Fields and Value describe: the last one added.
func (v *ViewSpec) current(what string) *list {
	if len(v.lists) == 0 {
		panic(fmt.Sprintf("one: view %s has %s before any Each or List", v.name, what))
	}
	return v.lists[len(v.lists)-1]
}

// Order sorts a list's rows by keys, in turn: a field name, with - in front to
// reverse it, or By(...). Order("done", "-created_at") puts the ones not done
// first, and the newest first within each.
func (v *ViewSpec) Order(keys ...any) *ViewSpec {
	l := v.current("an Order")
	l.order = nil
	for _, key := range keys {
		switch k := key.(type) {
		case string:
			l.order = append(l.order, ordering{field: strings.TrimPrefix(k, "-"), reverse: strings.HasPrefix(k, "-")})
		case ordering:
			l.order = append(l.order, k)
		default:
			panic(fmt.Sprintf("one: view %s can't be ordered by %T", v.name, key))
		}
	}
	return v
}

// Limit keeps only the first rows of a list, once ordered, so a list that keeps
// growing, like a project's timeline, stays small enough to be one document.
func (v *ViewSpec) Limit(rows int) *ViewSpec {
	v.current("a Limit").limit = rows
	return v
}

// Fields picks what each row of a list holds, besides its id: the entity's own
// fields, or a field of an entity it points at, like book.title.
func (v *ViewSpec) Fields(names ...string) *ViewSpec {
	v.current("Fields").fields = names
	return v
}

// FirstOf holds a field of the first entity the query finds, the earliest made,
// as in FirstOf("seen", Where[Reader]("person", Viewer), "seen_at"): when the
// person reading last looked. With nothing found, it holds none.
func (v *ViewSpec) FirstOf(name string, q Query, field string) *ViewSpec {
	v.firsts = append(v.firsts, value{name, q, field})
	return v
}

// Value adds to each row of a list a field of the first entity the query finds,
// the earliest made, as in Value("lent_to", First[Loan]("book",
// Row).And("returned_at", nil), "member"). With nothing found, the row holds none.
func (v *ViewSpec) Value(name string, q Query, field string) *ViewSpec {
	l := v.current("a Value")
	l.values = append(l.values, value{name, q, field})
	return v
}

// What every view document holds to say what it is and who may read it, so no
// value or list of a view may have one of these names.
var reserved = []string{"type", "public", "owner_uid", "subject", "readers", "within",
	"required_permission", "source_version", "content_hash", "last_event_id", "projected_at"}

func (v *ViewSpec) register(r *registry, ns string) {
	v.full = join(ns, v.name)
	var names []string
	for _, c := range v.copies {
		names = append(names, c.name)
	}
	for _, c := range v.counts {
		names = append(names, c.name)
	}
	for _, f := range v.firsts {
		names = append(names, f.name)
	}
	names = append(names, v.secrets...)
	for _, l := range v.lists {
		if l.name != "rows" {
			names = append(names, l.name)
		}
	}
	for _, name := range names {
		if contains(reserved, name) {
			panic("one: view " + v.full + " has something called " + name + ", which every view document uses for itself; name it something else")
		}
	}
	v.reads = map[string]*schema{}
	for _, q := range v.queries() {
		s := r.schema(q.typ, ns)
		v.reads[s.entity] = s
	}
	if v.per != nil {
		s := r.schema(v.per, ns)
		v.reads[s.entity] = s
	}
	if v.readers != nil {
		s := r.schema(v.readers, ns)
		v.reads[s.entity] = s
	}
	// Who reads a view with readers, or that's public when, is said by them alone.
	if v.readers != nil || len(v.people) > 0 || v.open != nil {
		r.views = append(r.views, v)
		return
	}
	// Who may read a view that isn't public or a person's own: the first entity it
	// lists, or is per, or counts.
	if !v.public && !v.perUser {
		switch {
		case len(v.lists) > 0:
			v.permission = r.schema(v.lists[0].query.typ, ns).entity + ":view"
		case v.per != nil:
			v.permission = r.schema(v.per, ns).entity + ":view"
		case len(v.counts) > 0:
			v.permission = r.schema(v.counts[0].query.typ, ns).entity + ":view"
		}
	}
	r.views = append(r.views, v)
}

// resolve finds the entities a view reads through references, like the book in
// book.title, once every entity is known, so that renaming a book rebuilds it.
func (v *ViewSpec) resolve(r *registry) error {
	if err := v.held(r); err != nil {
		return err
	}
	// A value copied through what the entity points at, like an issue's
	// project.lifecycle, reads that entity too, so changing it rebuilds the view.
	for _, c := range v.copies {
		through, _, hop := strings.Cut(c.field, ".")
		if !hop {
			continue
		}
		if v.per == nil {
			return fmt.Errorf("one: view %s copies %s, which needs a document per entity", v.full, c.field)
		}
		f := r.schemas[v.per].field(through)
		if f == nil || r.entity(f.refers) == nil {
			return fmt.Errorf("one: view %s copies %s, but %s doesn't point at another entity", v.full, c.field, through)
		}
		target := r.entity(f.refers)
		v.reads[target.entity] = target
	}
	for _, q := range v.queries() {
		each := r.schemas[q.typ]
		for _, c := range q.conditions {
			through, _, ok := strings.Cut(c.field, ".")
			if !ok {
				continue
			}
			f := each.through(through)
			if f == nil || r.entity(f.refers) == nil {
				return fmt.Errorf("one: view %s picks by %s, but %s doesn't point at another entity", v.full, c.field, through)
			}
			target := r.entity(f.refers)
			v.reads[target.entity] = target
		}
	}
	if v.perUser {
		if err := v.guard(r); err != nil {
			return err
		}
	}
	for _, l := range v.lists {
		each := r.schemas[l.query.typ]
		for _, name := range l.fields {
			through, _, ok := strings.Cut(name, ".")
			if !ok {
				continue
			}
			f := each.through(through)
			if f == nil || f.refers == "" {
				return fmt.Errorf("one: view %s reads %s, but %s doesn't point at another entity", v.full, name, through)
			}
			target := r.entity(f.refers)
			if target == nil {
				return fmt.Errorf("one: view %s reads %s, but there's no entity %s", v.full, name, f.refers)
			}
			v.reads[target.entity] = target
		}
	}
	return nil
}

// guard finds, for each list of a person's own view, the pages that say who may
// read its rows: a view per the rows' entity, or per the entity a change is of,
// with readers or public when. What those pages read, like who belongs to a
// project, the view reads too, so a change to it rebuilds the documents it touches.
func (v *ViewSpec) guard(r *registry) error {
	for _, l := range v.lists {
		each := r.schemas[l.query.typ]
		target, field := each, ""
		if owner := r.historyOf(each); owner != nil {
			target, field = owner, owner.name
		}
		l.guards = nil
		for _, page := range r.views {
			if page.per == nil || r.schemas[page.per] != target || (page.readers == nil && len(page.people) == 0 && page.open == nil) {
				continue
			}
			if err := page.held(r); err != nil {
				return err
			}
			l.guards = append(l.guards, guard{page: page, field: field})
			v.reads[page.scope.entity] = page.scope
			if page.readers != nil {
				member := r.schemas[page.readers]
				v.reads[member.entity] = member
			}
		}
	}
	return nil
}

// held finds what each document of a view with readers, or that's public when, is
// held within: the project a member grants roles in, or the entity its public
// condition reads through, like an issue's project. Changes to that, or to who
// belongs to it, rebuild only the documents held within it.
func (v *ViewSpec) held(r *registry) error {
	if v.readers == nil && len(v.people) == 0 && v.open == nil {
		return nil
	}
	if v.per == nil {
		return fmt.Errorf("one: view %s has readers or is public when, so it needs a document per entity", v.full)
	}
	per := r.schemas[v.per]
	for _, name := range v.people {
		if f := per.field(name); f == nil || f.refers != "user" {
			return fmt.Errorf("one: view %s is read by the people in %s, but a %s's %s isn't a person", v.full, name, per.name, name)
		}
	}
	if v.readers != nil {
		for _, ro := range r.scoped {
			if ro.member == v.readers {
				v.scope = r.schemas[ro.scope]
			}
		}
		for _, ro := range r.defined {
			if ro.member == v.readers {
				v.scope = r.schemas[ro.scope]
			}
		}
		if v.scope == nil {
			return fmt.Errorf("one: view %s is read by %s, which grants no role", v.full, r.schemas[v.readers].name)
		}
	}
	if v.open != nil {
		through, _, hop := strings.Cut(v.open.field, ".")
		if hop {
			f := per.field(through)
			if f == nil || r.entity(f.refers) == nil {
				return fmt.Errorf("one: view %s is public when %s, but %s doesn't point at another entity", v.full, v.open.field, through)
			}
			target := r.entity(f.refers)
			v.reads[target.entity] = target
			if v.scope == nil {
				v.scope = target
			}
		}
	}
	if v.scope == nil {
		v.scope = per
	}
	v.reads[v.scope.entity] = v.scope
	return nil
}

func (v *ViewSpec) queries() []Query {
	var qs []Query
	for _, c := range v.counts {
		qs = append(qs, c.query)
	}
	for _, f := range v.firsts {
		qs = append(qs, f.query)
	}
	for _, l := range v.lists {
		qs = append(qs, l.query.ways()...)
		for _, val := range l.values {
			qs = append(qs, val.query)
		}
	}
	return qs
}

// keyed says whether the view has a document per something: a person, or an entity.
func (v *ViewSpec) keyed() bool { return v.perUser || v.per != nil }

// The documents an event changes, by key: the people of a per-person view, or the
// entities of a view per entity.
//
// A change to the entity a view is per changes its own document. A change to an
// entity the view lists changes the documents of whoever or whatever it pointed at,
// before and after. A change to another entity the view reads, such as the book
// behind a loan's book.title, or to a list that isn't picked by whom or what it's
// for, changes every document of the view, since any of them may show it.
func (v *ViewSpec) subjects(ctx context.Context, a *App, ev event) ([]string, error) {
	if !v.keyed() {
		return []string{""}, nil
	}
	seen := map[string]bool{}
	var subjects []string
	add := func(key string) {
		if key != "" && !seen[key] {
			seen[key] = true
			subjects = append(subjects, key)
		}
	}
	if v.per != nil && a.reg.schemas[v.per].entity == ev.Entity {
		add(ev.ID)
		return subjects, nil
	}
	// A change to what the documents copy a value through, like a project whose
	// issues' pages show its lifecycle, changes every one that points at it.
	if v.per != nil {
		per := a.reg.schemas[v.per]
		for _, c := range v.copies {
			through, _, hop := strings.Cut(c.field, ".")
			if !hop {
				continue
			}
			if f := per.field(through); f == nil || a.reg.entity(f.refers) == nil || a.reg.entity(f.refers).entity != ev.Entity {
				continue
			}
			refs, err := a.store.Collection(per.collection).Where(through, "==", ev.ID).Documents(ctx).GetAll()
			if err != nil {
				return nil, err
			}
			for _, ref := range refs {
				add(ref.Ref.ID)
			}
		}
	}
	// Who may read a person's own list changed: someone joined or left a project,
	// so their own documents change, or a project turned public or private, so
	// anyone's may.
	for _, l := range v.lists {
		for _, g := range l.guards {
			if g.page.readers == nil || a.reg.schemas[g.page.readers].entity != ev.Entity {
				continue
			}
			for _, f := range a.reg.schemas[g.page.readers].fields {
				if f.refers != "user" {
					continue
				}
				for _, data := range []map[string]any{ev.Before, ev.After} {
					person, _ := data[f.name].(string)
					add(person)
				}
			}
			return subjects, nil
		}
	}
	listed, everywhere := false, false
	picked := []Query{}
	for _, l := range v.lists {
		picked = append(picked, l.query)
	}
	for _, f := range v.firsts {
		picked = append(picked, f.query)
	}
	for _, q := range picked {
		for _, way := range q.ways() {
			each := a.reg.schemas[way.typ]
			// Picked through what it points at, like the changes on the boards a person
			// follows: a change to a board changes the documents of its followers, before
			// and after.
			for _, c := range way.conditions {
				through, field, hop := strings.Cut(c.field, ".")
				if !hop || (c.value != Viewer && c.value != Subject) {
					continue
				}
				target := a.reg.entity(each.through(through).refers)
				if target.entity == ev.Entity {
					listed = true
					for _, data := range []map[string]any{ev.Before, ev.After} {
						addAll(add, data[field])
					}
				}
			}
			if each.entity != ev.Entity {
				continue
			}
			listed = true
			var field string
			var through *subjectField
			for _, c := range way.conditions {
				// Leaving the person out, like their own changes, doesn't say whose it is.
				if (c.value == Viewer || c.value == Subject) && !c.except {
					field = c.field
				}
				if sf, ok := c.value.(*subjectField); ok && v.per != nil {
					field, through = c.field, sf
				}
			}
			if field == "" {
				everywhere = true
			}
			// Through what it points at: everyone the change's board names now, like
			// its followers when an issue on it changes.
			if pointer, inside, hop := strings.Cut(field, "."); hop && through == nil {
				target := a.reg.entity(each.through(pointer).refers)
				for _, data := range []map[string]any{ev.Before, ev.After} {
					id, _ := data[pointer].(string)
					if id == "" {
						continue
					}
					held, err := a.stored(ctx, target, id)
					if err != nil {
						return nil, err
					}
					addAll(add, held[inside])
				}
				continue
			}
			// Picked by a field of the entity each document is for, like the steps of an
			// issue's board: the documents of every entity holding what it was and is.
			if through != nil {
				per := a.reg.schemas[v.per]
				for _, data := range []map[string]any{ev.Before, ev.After} {
					value, _ := data[field].(string)
					if value == "" {
						continue
					}
					refs, err := a.store.Collection(per.collection).Where(through.field, "==", value).Documents(ctx).GetAll()
					if err != nil {
						return nil, err
					}
					for _, ref := range refs {
						add(ref.Ref.ID)
					}
				}
				continue
			}
			for _, data := range []map[string]any{ev.Before, ev.After} {
				addAll(add, data[field])
			}
		}
	}
	if listed && !everywhere {
		return subjects, nil
	}
	// A change to who belongs to a project, or to the project itself, changes the
	// documents held within it.
	if v.scope != nil {
		var within []string
		if ev.Entity == v.scope.entity {
			within = append(within, ev.ID)
		}
		if v.readers != nil && a.reg.schemas[v.readers].entity == ev.Entity {
			for _, f := range a.reg.schemas[v.readers].fields {
				if f.refers != v.scope.entity {
					continue
				}
				for _, data := range []map[string]any{ev.Before, ev.After} {
					if id, _ := data[f.name].(string); id != "" {
						within = append(within, id)
					}
				}
			}
		}
		if len(within) > 0 {
			docs, err := a.store.Collection("views").Where("type", "==", v.full).Where("within", "in", within).Documents(ctx).GetAll()
			if err != nil {
				return nil, err
			}
			for _, doc := range docs {
				key, _ := doc.Data()["subject"].(string)
				add(key)
			}
			return subjects, nil
		}
	}
	docs, err := a.store.Collection("views").Where("type", "==", v.full).Documents(ctx).GetAll()
	if err != nil {
		return nil, err
	}
	for _, doc := range docs {
		key, _ := doc.Data()["subject"].(string)
		add(key)
	}
	return subjects, nil
}

// addAll adds each key a stored value names: one, or everyone in a list, like an
// issue's assignees.
func addAll(add func(string), value any) {
	switch key := value.(type) {
	case string:
		add(key)
	case []any:
		for _, item := range key {
			text, _ := item.(string)
			add(text)
		}
	case []string:
		for _, text := range key {
			add(text)
		}
	}
}

// find reads the entities a query picks. subject is the person a per-person view
// is for, and row the id of the row a value is worked out for.
func (a *App) find(ctx context.Context, q Query, subject, row string) ([]*firestore.DocumentSnapshot, error) {
	var all []*firestore.DocumentSnapshot
	seen := map[string]bool{}
	// Each way on its own, and what any of them picks, once.
	for _, way := range q.ways() {
		queries, err := a.stores(ctx, way, subject, row)
		if err != nil {
			return nil, err
		}
		for _, query := range queries {
			docs, err := query.Documents(ctx).GetAll()
			if err != nil {
				return nil, err
			}
			for _, doc := range docs {
				if !seen[doc.Ref.Path] && !excepted(ctx, way, doc, subject, row) {
					seen[doc.Ref.Path] = true
					all = append(all, doc)
				}
			}
		}
	}
	return all, nil
}

// stores is one way of a query as what the store is asked: one query, or, for a
// condition read through what the entity points at, like board.followers, one for
// each 30 of what it points at, the boards the person follows, as Firestore allows.
// An Except isn't asked; excepted checks it on what comes back.
func (a *App) stores(ctx context.Context, way Query, subject, row string) ([]firestore.Query, error) {
	s := a.reg.schemas[way.typ]
	query := a.store.Collection(s.collection).Query
	var pointing string
	var among []any
	for _, c := range way.conditions {
		through, field, hop := strings.Cut(c.field, ".")
		switch {
		case hop:
			target := a.reg.entity(s.through(through).refers)
			op := "=="
			if c.has {
				op = "array-contains"
			}
			found, err := a.store.Collection(target.collection).Where(field, op, givenIn(ctx, c.value, subject, row)).Documents(ctx).GetAll()
			if err != nil {
				return nil, err
			}
			if len(found) == 0 {
				return nil, nil
			}
			pointing, among = through, nil
			for _, doc := range found {
				among = append(among, doc.Ref.ID)
			}
		case c.has:
			query = query.Where(c.field, "array-contains", givenIn(ctx, c.value, subject, row))
		case !c.except:
			query = query.Where(c.field, "==", givenIn(ctx, c.value, subject, row))
		}
	}
	if pointing == "" {
		return []firestore.Query{query}, nil
	}
	var queries []firestore.Query
	for start := 0; start < len(among); start += 30 {
		queries = append(queries, query.Where(pointing, "in", among[start:min(start+30, len(among))]))
	}
	return queries, nil
}

// excepted says whether one of a way's Excepts leaves a document out.
func excepted(ctx context.Context, way Query, doc *firestore.DocumentSnapshot, subject, row string) bool {
	for _, c := range way.conditions {
		if c.except && same(doc.Data()[c.field], givenIn(ctx, c.value, subject, row)) {
			return true
		}
	}
	return false
}

// newest reads, for a list kept to its newest rows, only those: each way it's
// picked by asked of the store newest first, a page at a time, until it has the
// limit's worth the person may read, and every one made at the same moment as the
// last of them, so cutting the rows to the limit keeps the same ones reading every
// row would. done is false when it can't, because the list is ordered some other
// way, or the store hasn't the index it needs yet, as just after a deploy; the list
// is then read whole.
func (a *App) newest(ctx context.Context, l *list, subject string) (docs []*firestore.DocumentSnapshot, done bool, err error) {
	newestFirst := len(l.order) == 0 || (len(l.order) == 1 && l.order[0].field == "created_at" && l.order[0].reverse && l.order[0].through == nil)
	if l.limit <= 0 || !newestFirst {
		return nil, false, nil
	}
	page := l.limit + 10
	seen := map[string]bool{}
	for _, way := range l.query.ways() {
		queries, err := a.stores(ctx, way, subject, "")
		if err != nil {
			return nil, false, err
		}
		for _, query := range queries {
			var kept []*firestore.DocumentSnapshot
			var after *firestore.DocumentSnapshot
			for more := true; more; {
				asked := query.OrderBy("created_at", firestore.Desc).Limit(page)
				if after != nil {
					asked = asked.StartAfter(after)
				}
				got, err := asked.Documents(ctx).GetAll()
				if status.Code(err) == codes.FailedPrecondition {
					return nil, false, nil
				}
				if err != nil {
					return nil, false, err
				}
				more = len(got) == page
				if len(got) > 0 {
					after = got[len(got)-1]
				}
				var fresh []*firestore.DocumentSnapshot
				for _, doc := range got {
					if !excepted(ctx, way, doc, subject, "") {
						fresh = append(fresh, doc)
					}
				}
				if len(l.guards) > 0 {
					if fresh, err = a.readable(ctx, l, subject, fresh); err != nil {
						return nil, false, err
					}
				}
				kept = append(kept, fresh...)
				// Enough once the limit's worth is here and the page ends after the
				// moment the last of them was made.
				if len(kept) >= l.limit && len(got) > 0 && compare(got[len(got)-1].Data()["created_at"], kept[l.limit-1].Data()["created_at"]) < 0 {
					more = false
				}
			}
			for _, doc := range kept {
				if !seen[doc.Ref.Path] {
					seen[doc.Ref.Path] = true
					docs = append(docs, doc)
				}
			}
		}
	}
	return docs, true, nil
}

// count says how many entities a query picks. Firestore counts without reading them
// unless an Except has to be checked on each one.
func (a *App) count(ctx context.Context, q Query, subject string) (int64, error) {
	for _, c := range q.conditions {
		if c.except || strings.Contains(c.field, ".") || len(q.or) > 0 {
			docs, err := a.find(ctx, q, subject, "")
			return int64(len(docs)), err
		}
	}
	query := a.store.Collection(a.reg.schemas[q.typ].collection).Query
	for _, c := range q.conditions {
		op := "=="
		if c.has {
			op = "array-contains"
		}
		query = query.Where(c.field, op, givenIn(ctx, c.value, subject, ""))
	}
	result, err := query.NewAggregationQuery().WithCount("n").Get(ctx)
	if err != nil {
		return 0, err
	}
	n, ok := result["n"].(interface{ GetIntegerValue() int64 })
	if !ok {
		return 0, fmt.Errorf("one: counting %s gave %T, not a number", a.reg.schemas[q.typ].entity, result["n"])
	}
	return n.GetIntegerValue(), nil
}

// givenIn is given, with a field of the entity the document is for, which the
// request building it carries.
func givenIn(ctx context.Context, v any, subject, row string) any {
	if sf, ok := v.(*subjectField); ok {
		fields, _ := ctx.Value(subjectFields{}).(map[string]any)
		return fields[sf.field]
	}
	return given(v, subject, row)
}

// given is the value a condition compares with, once Viewer and Row are known.
func given(v any, subject, row string) any {
	switch v {
	case Viewer, Subject:
		return subject
	case Row:
		return row
	}
	return v
}

// same says whether a stored value is the one a condition names. A choice such as
// StatusWithdrawn is a named string, so it's compared as the text it's stored as.
func same(stored, wanted any) bool {
	if wanted == nil {
		return stored == nil
	}
	w := reflect.ValueOf(wanted)
	if w.Kind() == reflect.String {
		text, ok := stored.(string)
		return ok && text == w.String()
	}
	return reflect.DeepEqual(stored, wanted)
}

// called is what an id, or each in a list of them, names something by: its title, or
// its name, or the id itself when it has neither or is gone.
func (a *App) called(ctx context.Context, target *schema, value any, read map[string]map[string]any) (any, error) {
	one := func(id string) (any, error) {
		key := target.collection + "/" + id
		if _, done := read[key]; !done {
			fields, err := a.stored(ctx, target, id)
			if err != nil {
				return nil, err
			}
			read[key] = fields
		}
		for _, name := range []string{"title", "name"} {
			if text, ok := read[key][name].(string); ok && text != "" {
				return text, nil
			}
		}
		return id, nil
	}
	switch ids := value.(type) {
	case string:
		if ids == "" {
			return ids, nil
		}
		return one(ids)
	case []any:
		called := make([]any, 0, len(ids))
		for _, item := range ids {
			id, _ := item.(string)
			name, err := one(id)
			if err != nil {
				return nil, err
			}
			called = append(called, name)
		}
		return called, nil
	}
	return value, nil
}

func (a *App) compose(ctx context.Context, v *ViewSpec, subject string) (map[string]any, error) {
	data := map[string]any{}
	// What the entity it's for holds, for lists picked by its fields.
	picksByItsFields := false
	for _, q := range v.queries() {
		for _, c := range q.conditions {
			_, ok := c.value.(*subjectField)
			picksByItsFields = picksByItsFields || ok
		}
	}
	if picksByItsFields && v.per != nil {
		fields, err := a.stored(ctx, a.reg.schemas[v.per], subject)
		if err != nil {
			return nil, err
		}
		ctx = context.WithValue(ctx, subjectFields{}, fields)
	}
	if len(v.copies) > 0 || v.scope != nil || len(v.people) > 0 {
		// The entity the view is for. Once it's gone, its fields read as none.
		fields, err := a.stored(ctx, a.reg.schemas[v.per], subject)
		if err != nil {
			return nil, err
		}
		for _, c := range v.copies {
			through, field, hop := strings.Cut(c.field, ".")
			if !hop {
				data[c.name] = fields[c.field]
				continue
			}
			// Through each in a list it holds, like the names of an issue's assignees.
			if ids, ok := fields[through].([]any); ok {
				target := a.reg.entity(a.reg.schemas[v.per].field(through).refers)
				values := []any{}
				for _, item := range ids {
					id, _ := item.(string)
					if id == "" || target == nil {
						continue
					}
					held, err := a.stored(ctx, target, id)
					if err != nil {
						return nil, err
					}
					values = append(values, held[field])
				}
				data[c.name] = values
				continue
			}
			// Through what the entity points at, like its project's lifecycle.
			id, _ := fields[through].(string)
			if id == "" {
				data[c.name] = nil
				continue
			}
			held, err := a.stored(ctx, a.reg.entity(a.reg.schemas[v.per].field(through).refers), id)
			if err != nil {
				return nil, err
			}
			data[c.name] = held[field]
		}
		if v.scope != nil || len(v.people) > 0 {
			if err := a.access(ctx, v, subject, fields, data); err != nil {
				return nil, err
			}
		}
	}
	for _, name := range v.secrets {
		data[name] = GitHubSecret(subject)
	}
	for _, c := range v.counts {
		n, err := a.count(ctx, c.query, subject)
		if err != nil {
			return nil, err
		}
		data[c.name] = n
	}
	for _, f := range v.firsts {
		found, err := a.find(ctx, f.query, subject, "")
		if err != nil {
			return nil, err
		}
		data[f.name] = nil
		if len(found) > 0 {
			sort.SliceStable(found, func(i, j int) bool {
				return compare(found[i].Data()["created_at"], found[j].Data()["created_at"]) < 0
			})
			data[f.name] = found[0].Data()[f.field]
		}
	}
	for _, l := range v.lists {
		rows, err := a.rows(ctx, l, subject)
		if err != nil {
			return nil, err
		}
		data[l.name] = rows
	}
	return data, nil
}

func (a *App) rows(ctx context.Context, l *list, subject string) ([]any, error) {
	docs, done, err := a.newest(ctx, l, subject)
	if err != nil {
		return nil, err
	}
	if !done {
		if docs, err = a.find(ctx, l.query, subject, ""); err != nil {
			return nil, err
		}
		if len(l.guards) > 0 {
			if docs, err = a.readable(ctx, l, subject, docs); err != nil {
				return nil, err
			}
		}
	}
	// Newest first, by when each was made, so the order never depends on the store.
	sort.SliceStable(docs, func(i, j int) bool {
		if c := compare(docs[i].Data()["created_at"], docs[j].Data()["created_at"]); c != 0 {
			return c > 0
		}
		return docs[i].Ref.ID < docs[j].Ref.ID
	})
	// An order by a field of what each row points at, like from.position, sorts the
	// rows once they're read through, so the limit comes after.
	throughRows := false
	for _, o := range l.order {
		throughRows = throughRows || strings.Contains(o.field, ".")
	}
	if len(l.order) > 0 && !throughRows {
		key := func(doc *firestore.DocumentSnapshot, o ordering) any {
			stored := doc.Data()[o.field]
			if o.through != nil {
				return o.through(stored)
			}
			return stored
		}
		sort.SliceStable(docs, func(i, j int) bool {
			for _, o := range l.order {
				if c := compare(key(docs[i], o), key(docs[j], o)); c != 0 {
					return (c < 0) != o.reverse
				}
			}
			return false
		})
	}
	if l.limit > 0 && len(docs) > l.limit && !throughRows {
		docs = docs[:l.limit]
	}
	each := a.reg.schemas[l.query.typ]
	pointed := map[string]map[string]any{} // what each reference points at, read once
	rows := make([]any, 0, len(docs))
	for _, doc := range docs {
		row := map[string]any{"id": doc.Ref.ID}
		for _, name := range l.fields {
			through, field, ok := strings.Cut(name, ".")
			if !ok {
				row[name] = doc.Data()[name]
				continue
			}
			target := a.reg.entity(each.through(through).refers)
			read := func(id string) (any, error) {
				key := target.collection + "/" + id
				if _, read := pointed[key]; !read {
					fields, err := a.stored(ctx, target, id)
					if err != nil {
						return nil, err
					}
					pointed[key] = fields
				}
				return pointed[key][field], nil
			}
			switch ids := doc.Data()[through].(type) {
			case string:
				if row[name], err = read(ids); err != nil {
					return nil, err
				}
			case []any: // through each in a list: every assignee's name
				values := []any{}
				for _, item := range ids {
					id, _ := item.(string)
					value, err := read(id)
					if err != nil {
						return nil, err
					}
					values = append(values, value)
				}
				row[name] = values
			default:
				row[name] = nil
			}
		}
		// A change of a field that points at something, like an issue's phase, says what
		// it pointed at before and after by its title or name, rather than its id.
		if owner := a.reg.historyOf(each); owner != nil {
			if f := owner.field(fmt.Sprint(doc.Data()["field"])); f != nil && f.refers != "" {
				if target := a.reg.entity(f.refers); target != nil {
					for _, side := range []string{"before", "after"} {
						if _, shown := row[side]; !shown {
							continue
						}
						called, err := a.called(ctx, target, doc.Data()[side], pointed)
						if err != nil {
							return nil, err
						}
						row[side] = called
					}
				}
			}
		}
		for _, val := range l.values {
			found, err := a.find(ctx, val.query, subject, doc.Ref.ID)
			if err != nil {
				return nil, err
			}
			row[val.name] = nil
			if len(found) > 0 {
				sort.SliceStable(found, func(i, j int) bool {
					return compare(found[i].Data()["created_at"], found[j].Data()["created_at"]) < 0
				})
				row[val.name] = found[0].Data()[val.field]
			}
		}
		rows = append(rows, row)
	}
	if throughRows {
		sort.SliceStable(rows, func(i, j int) bool {
			for _, o := range l.order {
				x, y := rows[i].(map[string]any)[o.field], rows[j].(map[string]any)[o.field]
				if o.through != nil {
					x, y = o.through(x), o.through(y)
				}
				if c := compare(x, y); c != 0 {
					return (c < 0) != o.reverse
				}
			}
			return false
		})
		if l.limit > 0 && len(rows) > l.limit {
			rows = rows[:l.limit]
		}
	}
	return rows, nil
}

// readable keeps the rows of a person's own list that they may read: those whose
// page, by any of the list's guards, is public or names them as a reader.
func (a *App) readable(ctx context.Context, l *list, subject string, docs []*firestore.DocumentSnapshot) ([]*firestore.DocumentSnapshot, error) {
	ctx = context.WithValue(ctx, readersRead{}, map[string][]string{})
	decided := map[string]bool{} // by the page's entity's id, read once
	kept := docs[:0]
	for _, doc := range docs {
		ok := false
		for _, g := range l.guards {
			id := doc.Ref.ID
			if g.field != "" {
				id, _ = doc.Data()[g.field].(string)
			}
			key := g.page.full + ":" + id
			may, known := decided[key]
			if !known && id != "" {
				fields, err := a.stored(ctx, a.reg.schemas[g.page.per], id)
				if err != nil {
					return nil, err
				}
				data := map[string]any{}
				if len(fields) > 0 {
					if err := a.access(ctx, g.page, id, fields, data); err != nil {
						return nil, err
					}
				}
				readers, _ := data["readers"].([]string)
				may = data["public"] == true || contains(readers, subject)
				decided[key] = may
			}
			ok = ok || may
		}
		if ok {
			kept = append(kept, doc)
		}
	}
	return kept, nil
}

// readersRead holds, while a list's rows are checked, the readers already found
// for each place, so a project's members are read once however many rows are in it.
type readersRead struct{}

func (a *App) stored(ctx context.Context, s *schema, id string) (map[string]any, error) {
	if id == "" {
		return map[string]any{}, nil
	}
	snap, err := a.store.Collection(s.collection).Doc(id).Get(ctx)
	if status.Code(err) == codes.NotFound {
		return map[string]any{}, nil
	}
	if err != nil {
		return nil, err
	}
	return snap.Data(), nil
}

// access puts in a document who may read it: what it's held within, the people
// with a role there as its readers, and whether it's public now.
func (a *App) access(ctx context.Context, v *ViewSpec, subject string, fields, data map[string]any) error {
	per := a.reg.schemas[v.per]
	within := ""
	switch {
	case v.scope == nil:
	case v.scope == per:
		within = subject
	default:
		for _, f := range per.fields {
			if f.refers == v.scope.entity {
				within, _ = fields[f.name].(string)
			}
		}
		if within == "" {
			// One step further: a comment's issue's project.
			for _, f := range per.fields {
				through := a.reg.entity(f.refers)
				if through == nil {
					continue
				}
				for _, g := range through.fields {
					if g.refers == v.scope.entity {
						id, _ := fields[f.name].(string)
						held, err := a.stored(ctx, through, id)
						if err != nil {
							return err
						}
						within, _ = held[g.name].(string)
					}
				}
			}
		}
	}
	data["within"] = within

	readers := []string{}
	if v.readers != nil {
		member := a.reg.schemas[v.readers]
		if within != "" {
			var place, person string
			for _, f := range member.fields {
				switch f.refers {
				case v.scope.entity:
					place = f.name
				case "user":
					person = f.name
				}
			}
			memo, _ := ctx.Value(readersRead{}).(map[string][]string)
			if known, ok := memo[member.entity+":"+within]; ok && memo != nil {
				readers = append(readers, known...)
			} else {
				docs, err := a.store.Collection(member.collection).Where(place, "==", within).Documents(ctx).GetAll()
				if err != nil {
					return err
				}
				for _, doc := range docs {
					if id, _ := doc.Data()[person].(string); id != "" && !contains(readers, id) {
						readers = append(readers, id)
					}
				}
				if memo != nil {
					memo[member.entity+":"+within] = append([]string(nil), readers...)
				}
			}
		}
	}
	for _, name := range v.people {
		var named []string
		switch value := fields[name].(type) {
		case string:
			named = []string{value}
		case []string:
			named = value
		case []any:
			for _, item := range value {
				if id, ok := item.(string); ok {
					named = append(named, id)
				}
			}
		}
		for _, id := range named {
			if id != "" && !contains(readers, id) {
				readers = append(readers, id)
			}
		}
	}
	if v.readers != nil || len(v.people) > 0 {
		sort.Strings(readers)
		data["readers"] = readers
	}

	if v.open != nil {
		value := fields[v.open.field]
		if through, field, hop := strings.Cut(v.open.field, "."); hop {
			id, _ := fields[through].(string)
			held, err := a.stored(ctx, a.reg.entity(per.field(through).refers), id)
			if err != nil {
				return err
			}
			value = held[field]
		}
		data["public"] = same(value, v.open.value)
	}
	return nil
}

// compare orders two stored values of the same kind: false before true, earlier
// times first, and numbers and text in their usual order. Missing values come first.
func compare(a, b any) int {
	switch x := a.(type) {
	case bool:
		y, _ := b.(bool)
		if x == y {
			return 0
		}
		if !x {
			return -1
		}
		return 1
	case time.Time:
		y, _ := b.(time.Time)
		return x.Compare(y)
	case int64:
		y, _ := b.(int64)
		return cmp.Compare(x, y)
	case float64:
		y, _ := b.(float64)
		return cmp.Compare(x, y)
	case string:
		y, _ := b.(string)
		return strings.Compare(x, y)
	case nil:
		if b == nil {
			return 0
		}
		return -1
	}
	return 0
}

// write stores a view's document, unless a newer event already has, or the data
// hasn't changed. Events can arrive late or twice, and neither may take a view
// backwards. Returns whether it wrote.
func (a *App) write(ctx context.Context, v *ViewSpec, subject string, data map[string]any, version int64, cause string) (bool, error) {
	id := v.full
	if subject != "" {
		id += ":" + subject
	}
	encoded, err := json.Marshal(data)
	if err != nil {
		return false, err
	}
	sum := sha256.Sum256(encoded)
	hash := hex.EncodeToString(sum[:])

	written := false
	ref := a.store.Collection("views").Doc(id)
	err = a.store.RunTransaction(ctx, func(ctx context.Context, tx *firestore.Transaction) error {
		written = false
		snap, err := tx.Get(ref)
		if err == nil {
			current := snap.Data()
			if applied, _ := current["source_version"].(int64); applied >= version {
				return nil
			}
			if current["content_hash"] == hash {
				return nil
			}
		} else if status.Code(err) != codes.NotFound {
			return err
		}
		doc := map[string]any{}
		for k, value := range data {
			doc[k] = value
		}
		doc["type"] = v.full
		if _, decided := data["public"]; !decided {
			doc["public"] = v.public
		}
		// Only a person's own document names them as its owner: the rules let an
		// owner read it, and a view per entity is keyed by the entity instead.
		doc["owner_uid"] = ""
		if v.perUser {
			doc["owner_uid"] = subject
		}
		doc["subject"] = subject
		doc["required_permission"] = v.permission
		doc["source_version"] = version
		doc["last_event_id"] = cause
		doc["projected_at"] = time.Now().UTC()
		doc["content_hash"] = hash
		written = true
		return tx.Set(ref, doc)
	})
	return written, err
}

// definition writes out what a view reads and how, the same from one start of the
// backend to the next, so a view whose definition changed can be told from one that
// didn't: entities by name, and a function, which can't be compared, by its place
// in the view.
func (v *ViewSpec) definition() string {
	var out strings.Builder
	typeName := func(t reflect.Type) string {
		if t == nil {
			return ""
		}
		return t.PkgPath() + "." + t.Name()
	}
	val := func(x any) string {
		switch m := x.(type) {
		case *marker:
			return "<" + m.name + ">"
		case nil:
			return "<none>"
		}
		return fmt.Sprintf("%T:%v", x, x)
	}
	query := func(q Query) string {
		var ways []string
		for _, way := range q.ways() {
			s := typeName(way.typ) + "("
			for _, c := range way.conditions {
				s += fmt.Sprintf("%s=%s,except=%t,has=%t;", c.field, val(c.value), c.except, c.has)
			}
			ways = append(ways, s+")")
		}
		return strings.Join(ways, "|")
	}
	fmt.Fprintf(&out, "view %s public=%t per_user=%t per=%s readers=%s people=%q permission=%q secrets=%q\n",
		v.full, v.public, v.perUser, typeName(v.per), typeName(v.readers), v.people, v.permission, v.secrets)
	if v.open != nil {
		fmt.Fprintf(&out, "open %s=%s,except=%t\n", v.open.field, val(v.open.value), v.open.except)
	}
	for _, c := range v.counts {
		fmt.Fprintf(&out, "count %s %s\n", c.name, query(c.query))
	}
	for _, c := range v.copies {
		fmt.Fprintf(&out, "copy %s %s\n", c.name, c.field)
	}
	for _, f := range v.firsts {
		fmt.Fprintf(&out, "first %s %s %s\n", f.name, query(f.query), f.field)
	}
	for _, l := range v.lists {
		fmt.Fprintf(&out, "list %s %s fields=%q limit=%d\n", l.name, query(l.query), l.fields, l.limit)
		for _, x := range l.values {
			fmt.Fprintf(&out, "  value %s %s %s\n", x.name, query(x.query), x.field)
		}
		for i, o := range l.order {
			fmt.Fprintf(&out, "  order %s reverse=%t through=%t #%d\n", o.field, o.reverse, o.through != nil, i)
		}
	}
	sum := sha256.Sum256([]byte(out.String()))
	return hex.EncodeToString(sum[:])
}

// composing changes whenever how views are put together changes in a way their
// stored documents need rebuilding for, like building a new view per entity for the
// entities already stored.
const composing = "3"

// rebuildChanged rebuilds the stored documents of every view whose definition
// changed since the backend last started, as when a deploy adds a field to a view,
// so they hold what it shows now rather than waiting for something they show to
// change. Each view's definition is kept in view_definitions once its documents are
// rebuilt, so later starts leave it alone.
func (a *App) rebuildChanged(ctx context.Context) error {
	for _, v := range a.reg.views {
		if !v.keyed() {
			continue // composed afresh at every start already
		}
		// What's recorded is the view's definition and how this library puts views
		// together, so a fix to that rebuilds every view once too.
		definition := v.definition() + " " + composing
		kept := a.store.Collection("view_definitions").Doc(strings.ReplaceAll(v.full, "/", "_"))
		snap, err := kept.Get(ctx)
		if err != nil && status.Code(err) != codes.NotFound {
			return err
		}
		if err == nil && snap.Data()["definition"] == definition {
			continue
		}
		docs, err := a.store.Collection("views").Where("type", "==", v.full).Documents(ctx).GetAll()
		if err != nil {
			return err
		}
		// Each document there is, and for a view per entity, one for every entity of
		// that kind already stored: a view that's new, or newly reads something, has
		// a document for each from the start, not only once one changes.
		subjects := map[string]bool{}
		for _, doc := range docs {
			subjects[strings.TrimPrefix(doc.Ref.ID, v.full+":")] = true
		}
		if v.per != nil {
			if schema, ok := a.reg.schemas[v.per]; ok {
				refs, err := a.store.Collection(schema.collection).DocumentRefs(ctx).GetAll()
				if err != nil {
					return err
				}
				for _, ref := range refs {
					subjects[ref.ID] = true
				}
			}
		}
		version := time.Now().UnixNano()
		for subject := range subjects {
			data, err := a.compose(ctx, v, subject)
			if err != nil {
				return err
			}
			if _, err := a.write(ctx, v, subject, data, version, "definition"); err != nil {
				return err
			}
		}
		if _, err := kept.Set(ctx, map[string]any{"definition": definition, "view": v.full, "rebuilt": len(subjects), "at": time.Now()}); err != nil {
			return err
		}
	}
	return nil
}

// project rebuilds the documents of one view that an event touches.
func (a *App) project(ctx context.Context, v *ViewSpec, ev event) error {
	subjects, err := v.subjects(ctx, a, ev)
	if err != nil {
		return err
	}
	for _, subject := range subjects {
		// A document's version is when its data was read, not when the command that
		// caused it began: of two rebuilds, the one that read later holds every
		// change the other did, whichever command committed first.
		version := time.Now().UnixNano()
		data, err := a.compose(ctx, v, subject)
		if err != nil {
			return err
		}
		if _, err := a.write(ctx, v, subject, data, version, ev.id()); err != nil {
			return err
		}
	}
	return nil
}
