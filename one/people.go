// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"context"
	"fmt"
	"net/http"
	"reflect"
	"slices"
	"strings"
	"time"

	"cloud.google.com/go/firestore"
	"firebase.google.com/go/v4/auth"
	"google.golang.org/api/iterator"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// Everyone who has signed up, for the app's own people: who they are, when they
// joined and were last active, and what they use and keep. It's read a page at a time
// when asked, never kept as a document, since it changes whenever anyone does
// anything and only a few ever read it. Each person is their usage document, which
// says who they are and is ordered by when they were last active.

// ViewPeople is the permission a role held everywhere gives to read everyone, as
// user::view, the only permission on people.
const ViewPeople Permission = "user:view"

// How many people a page holds, unless the list says.
const peoplePage = 50

// The fields a row of people may show, besides keeps: who they are, from their
// profile; and when they joined, were last active, and what they used this month.
var peopleFields = []string{"name", "picture", "username", "service", "email", "joined", "active", "commands", "writes", "bytes"}

// PeopleSpec lists everyone, made with People.
type PeopleSpec struct {
	name   string
	full   string
	fields []string
	keeps  []kept
	sums   []summed
	order  ordering
	limit  int
}

// kept is a count of the records of a kind each person keeps, like their projects.
type kept struct {
	name string
	of   reflect.Type
}

// summed is a total, for each person, of a number the records of a kind hold, over
// those that are theirs: whose field by names them, directly, or through what it
// points at, like a payment's account.created_by.
type summed struct {
	name  string
	of    reflect.Type
	field string
	by    string
}

// People lists everyone who has signed up, for whoever holds ViewPeople, a page at a
// time, the most recently active first.
func People(name string) *PeopleSpec {
	return &PeopleSpec{name: name, order: ordering{field: "active", reverse: true}}
}

// Fields picks what each row shows, of peopleFields.
func (p *PeopleSpec) Fields(names ...string) *PeopleSpec {
	for _, name := range names {
		if !slices.Contains(peopleFields, name) {
			panic(fmt.Sprintf("one: people %s can't show %s; a row shows %v, and Keeps", p.name, name, peopleFields))
		}
	}
	p.fields = names
	return p
}

// Keeps shows how many of a kind each person keeps, as the records they made of it
// that haven't been deleted: Keeps("projects", Entity[Project]()).
func (p *PeopleSpec) Keeps(name string, kind Kind) *PeopleSpec {
	p.keeps = append(p.keeps, kept{name, kind.typ})
	return p
}

// Sums shows a total for each person of a number the records of a kind hold, over
// those whose field by names them, or names what's theirs by one field it points
// at: Sums("paid", Entity[Payment](), "amount_paid", "account.created_by"). It's added
// up as each page is read, from every record it's over.
func (p *PeopleSpec) Sums(name string, kind Kind, field, by string) *PeopleSpec {
	p.sums = append(p.sums, summed{name, kind.typ, field, by})
	return p
}

// Order sorts everyone by when they joined or were last active, with - in front for
// the latest first, as "-active" is unless it says.
func (p *PeopleSpec) Order(key string) *PeopleSpec {
	field := trimDash(key)
	if field != "active" && field != "joined" {
		panic(fmt.Sprintf("one: people %s can be ordered by active or joined, not %s", p.name, field))
	}
	p.order = ordering{field: field, reverse: field != key}
	return p
}

// Limit is how many people a page holds.
func (p *PeopleSpec) Limit(n int) *PeopleSpec {
	p.limit = n
	return p
}

func trimDash(s string) string {
	if len(s) > 0 && s[0] == '-' {
		return s[1:]
	}
	return s
}

func (p *PeopleSpec) register(r *registry, ns string) {
	p.full = join(ns, p.name)
	r.people = append(r.people, p)
}

// servePeople answers GET /api/people?view=admin::people&after=<person>, with a page
// of everyone and the person the next page starts after, for whoever may read them.
func (a *App) servePeople(w http.ResponseWriter, r *http.Request) {
	who, err := a.caller(r)
	if err != nil {
		replyFailure(w, a, "signing in", err)
		return
	}
	if who.id == "" {
		reply(w, http.StatusUnauthorized, map[string]any{"error": "sign in to see this"})
		return
	}
	if err := a.allow("asking", who.id, askingRate, "asking"); err != nil {
		replyFailure(w, a, "asking", err)
		return
	}
	var p *PeopleSpec
	for _, each := range a.reg.people {
		if each.full == r.URL.Query().Get("view") {
			p = each
		}
	}
	if p == nil {
		reply(w, http.StatusNotFound, map[string]any{"error": "there's no such list"})
		return
	}
	if who.service || !a.grants(r.Context(), who.id, string(ViewPeople)) {
		reply(w, http.StatusForbidden, map[string]any{"error": "you can't read this"})
		return
	}
	rows, next, err := a.peopleAfter(r.Context(), p, r.URL.Query().Get("after"))
	if err != nil {
		replyFailure(w, a, "listing people", err)
		return
	}
	reply(w, http.StatusOK, map[string]any{"rows": rows, "next": next})
}

// peopleAfter is a page of everyone, after the person named, if any, and the last
// person on it when there may be more.
func (a *App) peopleAfter(ctx context.Context, p *PeopleSpec, after string) ([]map[string]any, string, error) {
	limit := p.limit
	if limit <= 0 {
		limit = peoplePage
	}
	direction := firestore.Asc
	if p.order.reverse {
		direction = firestore.Desc
	}
	query := a.store.Collection("usage").OrderBy(p.order.field, direction).OrderBy(firestore.DocumentID, direction).Limit(limit)
	if after != "" {
		if !validID(after) {
			return nil, "", &Failure{Status: 400, Message: "that isn't a person"}
		}
		snap, err := a.store.Collection("usage").Doc(after).Get(ctx)
		if status.Code(err) == codes.NotFound {
			return nil, "", &Failure{Status: 404, Message: "that person isn't listed any more"}
		}
		if err != nil {
			return nil, "", err
		}
		query = query.StartAfter(snap)
	}
	docs, err := query.Documents(ctx).GetAll()
	if err != nil {
		return nil, "", err
	}
	refs := make([]*firestore.DocumentRef, len(docs))
	for i, doc := range docs {
		refs[i] = a.store.Collection("users").Doc(doc.Ref.ID)
	}
	profiles, err := a.store.GetAll(ctx, refs)
	if err != nil {
		return nil, "", err
	}
	people := make([]string, len(docs))
	for i, doc := range docs {
		people[i] = doc.Ref.ID
	}
	totals := make([]map[string]float64, len(p.sums))
	for i, sum := range p.sums {
		if totals[i], err = a.totalled(ctx, sum, people); err != nil {
			return nil, "", err
		}
	}
	month := time.Now().UTC().Format("2006-01")
	rows := make([]map[string]any, 0, len(docs))
	for i, doc := range docs {
		used := doc.Data()
		profile := map[string]any{}
		if profiles[i].Exists() {
			profile = profiles[i].Data()
		}
		ran, _ := used["months"].(map[string]any)
		this, _ := ran[month].(map[string]any)
		row := map[string]any{"id": doc.Ref.ID}
		for _, f := range p.fields {
			switch f {
			case "name", "picture", "username", "service":
				row[f] = profile[f]
			case "email", "joined", "active":
				row[f] = used[f]
			case "commands", "writes":
				row[f] = asInt(this[f])
			case "bytes":
				row[f] = asInt(used[f])
			}
		}
		held, _ := used["held"].(map[string]any)
		for _, k := range p.keeps {
			if s := a.reg.schemas[k.of]; s != nil {
				row[k.name] = asInt(held[s.collection])
			}
		}
		for j, sum := range p.sums {
			row[sum.name] = totals[j][doc.Ref.ID]
		}
		rows = append(rows, row)
	}
	next := ""
	if len(docs) == limit {
		next = docs[len(docs)-1].Ref.ID
	}
	return rows, next, nil
}

// totalled adds up a sum for each of some people, reading each record it's over, 30
// people at a time: what's theirs is first found by the field it's through, if any,
// then the records naming it.
func (a *App) totalled(ctx context.Context, sum summed, people []string) (map[string]float64, error) {
	of := a.reg.schemas[sum.of]
	if of == nil || of.field(sum.field) == nil {
		return nil, fmt.Errorf("one: there's no %s to add up", sum.field)
	}
	whose := map[string]string{}
	for _, person := range people {
		whose[person] = person
	}
	by, last, through := strings.Cut(sum.by, ".")
	if through {
		f := of.field(by)
		var theirs *schema
		if f != nil {
			theirs = a.reg.entity(f.refers)
		}
		if theirs == nil || !holds(theirs, last) {
			return nil, fmt.Errorf("one: %s.%s doesn't name a person", of.entity, sum.by)
		}
		owned := map[string]string{}
		for start := 0; start < len(people); start += 30 {
			docs, err := a.store.Collection(theirs.collection).Where(last, "in", people[start:min(start+30, len(people))]).Select(last).Documents(ctx).GetAll()
			if err != nil {
				return nil, err
			}
			for _, doc := range docs {
				owned[doc.Ref.ID], _ = doc.Data()[last].(string)
			}
		}
		whose = owned
	} else if !holds(of, by) {
		return nil, fmt.Errorf("one: %s.%s doesn't name a person", of.entity, sum.by)
	}
	named := make([]string, 0, len(whose))
	for id := range whose {
		named = append(named, id)
	}
	totals := map[string]float64{}
	for start := 0; start < len(named); start += 30 {
		docs, err := a.store.Collection(of.collection).Where(by, "in", named[start:min(start+30, len(named))]).Select(by, sum.field).Documents(ctx).GetAll()
		if err != nil {
			return nil, err
		}
		for _, doc := range docs {
			id, _ := doc.Data()[by].(string)
			switch n := doc.Data()[sum.field].(type) {
			case int64:
				totals[whose[id]] += float64(n)
			case float64:
				totals[whose[id]] += n
			}
		}
	}
	return totals, nil
}

// holds says whether an entity's records hold a field, as each holds who made it.
func holds(s *schema, name string) bool {
	return name == "created_by" || s.field(name) != nil
}

// joined keeps who a person is, and when they joined, the first time they're seen,
// which is when they were last active too, until their usage is next kept.
func (a *App) joined(ctx context.Context, person, email string, at time.Time) {
	data := map[string]any{"person": person, "joined": at, "active": at}
	if email != "" {
		data["email"] = email
	}
	if _, err := a.store.Collection("usage").Doc(person).Set(ctx, data, firestore.MergeAll); err != nil {
		a.log.Printf("one: when %s joined wasn't kept: %v", person, err)
	}
}

// everyoneListed lists, once, everyone who signed up before people were listed:
// each account Firebase knows, with its email, when it was made, and when it last
// signed in, unless they've been seen since.
func (a *App) everyoneListed(ctx context.Context) error {
	kept := a.store.Collection("usage_definitions").Doc("people")
	snap, err := kept.Get(ctx)
	if err != nil && status.Code(err) != codes.NotFound {
		return err
	}
	if err == nil && snap.Data()["definition"] == usageAs {
		return nil
	}
	seen := map[string]bool{}
	docs, err := a.store.Collection("usage").Select("active").Documents(ctx).GetAll()
	if err != nil {
		return err
	}
	for _, doc := range docs {
		_, active := doc.Data()["active"]
		seen[doc.Ref.ID] = active
	}
	writer := a.store.BulkWriter(ctx)
	var jobs []*firestore.BulkWriterJob
	accounts := a.auth.Users(ctx, "")
	for {
		account, err := accounts.Next()
		if err == iterator.Done {
			break
		}
		if err != nil {
			writer.End()
			return err
		}
		data, fields := everyoneOf(account, seen[account.UID])
		job, err := writer.Set(a.store.Collection("usage").Doc(account.UID), data, firestore.Merge(fields...))
		if err != nil {
			writer.End()
			return err
		}
		jobs = append(jobs, job)
	}
	writer.End()
	for _, job := range jobs {
		if _, err := job.Results(); err != nil {
			return err
		}
	}
	_, err = kept.Set(ctx, map[string]any{"definition": usageAs, "people": len(jobs), "at": time.Now()})
	return err
}

// everyoneOf is what an account says of a person, and the fields to keep of it:
// when they were last active only when it isn't known already.
func everyoneOf(account *auth.ExportedUserRecord, active bool) (map[string]any, []firestore.FieldPath) {
	data := map[string]any{"person": account.UID, "email": account.Email}
	fields := []firestore.FieldPath{{"person"}, {"email"}}
	if m := account.UserMetadata; m != nil {
		if m.CreationTimestamp > 0 {
			data["joined"] = time.UnixMilli(m.CreationTimestamp).UTC()
			fields = append(fields, firestore.FieldPath{"joined"})
		}
		last := max(m.LastLogInTimestamp, m.LastRefreshTimestamp)
		if !active && last > 0 {
			data["active"] = time.UnixMilli(last).UTC()
			fields = append(fields, firestore.FieldPath{"active"})
		}
	}
	if _, ok := data["active"]; !ok && !active {
		// Ordered by when they were last active, someone never active is still listed.
		data["active"] = data["joined"]
		if data["active"] != nil {
			fields = append(fields, firestore.FieldPath{"active"})
		}
	}
	return data, fields
}
