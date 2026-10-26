// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package one

import (
	"context"
	"net/http"
	"slices"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"

	"cloud.google.com/go/firestore"
	"golang.org/x/text/runes"
	"golang.org/x/text/transform"
	"golang.org/x/text/unicode/norm"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// A text field marked searchable keeps its words on its record, worked out whenever
// the record is written, in the write that happens anyway. A search is then one query
// for the records holding a word, reading only those, however much there is: nothing
// is ever indexed again. Words are whole, lowercased and with their accents folded, so
// Café is found by cafe, and a record keeps each once.

const (
	searchWords  = "search_words" // the field a record keeps its words in
	mostWords    = 1000           // a record's most, so a long page can't fill its index
	longestWord  = 40             // in letters; longer is an address or a hash, not a word
	mostFound    = 20             // what one search answers with
	searchedMore = 3              // read for each answered, as some are left out after
)

// Searching: twice a second, and 10 at once, as a person types.
var searchingRate = rate{perSecond: 2, burst: 10}

var folding = transform.Chain(norm.NFD, runes.Remove(runes.In(unicode.Mn)), norm.NFC)

// wordsIn is each word of some text, once, in the order they first appear: two
// letters or more, and no longer than longestWord.
func wordsIn(texts ...string) []string {
	var words []string
	seen := map[string]bool{}
	for _, text := range texts {
		folded, _, err := transform.String(folding, text)
		if err != nil {
			folded = text
		}
		for _, word := range strings.FieldsFunc(strings.ToLower(folded), func(r rune) bool { return !unicode.IsLetter(r) && !unicode.IsDigit(r) }) {
			if n := utf8.RuneCountInString(word); n < 2 || n > longestWord || seen[word] {
				continue
			}
			seen[word] = true
			words = append(words, word)
		}
	}
	return words
}

// words is what a record keeps of its searchable fields, sorted, so the same text
// is stored the same way; nil when it has none.
func (s *schema) words(data map[string]any) []string {
	var texts []string
	searched := false
	for _, f := range s.fields {
		if f.searched {
			searched = true
			text, _ := data[f.name].(string)
			texts = append(texts, text)
		}
	}
	if !searched {
		return nil
	}
	words := wordsIn(texts...)
	if len(words) > mostWords {
		words = words[:mostWords]
	}
	slices.Sort(words)
	return words
}

// textsIn is a stored list of text, as Firestore gives it back.
func textsIn(stored any) []string {
	var out []string
	list, _ := stored.([]any)
	for _, item := range list {
		if text, ok := item.(string); ok {
			out = append(out, text)
		}
	}
	return out
}

func (s *schema) searched() bool {
	return slices.ContainsFunc(s.fields, func(f field) bool { return f.searched })
}

// serveSearch answers GET /api/search?view=projects::project_page&id=tracker&list=pages&words=deploy,
// with the newest of the view's list's rows holding every word, as the list shows
// them, for whoever may read the view.
func (a *App) serveSearch(w http.ResponseWriter, r *http.Request) {
	who, err := a.caller(r)
	if err != nil {
		replyFailure(w, a, "signing in", err)
		return
	}
	if err := a.allow("searching", asker(who, r), searchingRate, "searching"); err != nil {
		replyFailure(w, a, "searching", err)
		return
	}
	asked := r.URL.Query()
	var v *ViewSpec
	for _, each := range a.reg.views {
		if each.full == asked.Get("view") {
			v = each
		}
	}
	var l *list
	if v != nil {
		for _, each := range v.lists {
			if each.name == asked.Get("list") {
				l = each
			}
		}
	}
	if l == nil || !a.reg.schemas[l.query.typ].searched() {
		reply(w, http.StatusNotFound, map[string]any{"error": "there's no such list to search"})
		return
	}
	subject := asked.Get("id")
	may, err := a.mayReadView(r.Context(), v, subject, who.id)
	if err != nil {
		replyFailure(w, a, "searching", err)
		return
	}
	if !may {
		reply(w, http.StatusForbidden, map[string]any{"error": "you can't read this"})
		return
	}
	docs, err := a.search(r.Context(), l, subject, wordsIn(asked.Get("words")))
	if status.Code(err) == codes.FailedPrecondition {
		// The index it needs is still being built, as just after a deploy.
		reply(w, http.StatusServiceUnavailable, map[string]any{"error": "search is still being set up; try again in a few minutes"})
		return
	}
	if err != nil {
		replyFailure(w, a, "searching", err)
		return
	}
	rows, err := a.shaped(r.Context(), l, subject, docs)
	if err != nil {
		replyFailure(w, a, "searching", err)
		return
	}
	reply(w, http.StatusOK, map[string]any{"rows": rows})
}

// mayReadView says whether someone may read a view's document, as the store's rules
// do: one read of it.
func (a *App) mayReadView(ctx context.Context, v *ViewSpec, subject, me string) (bool, error) {
	id := v.full
	if subject != "" {
		id += ":" + subject
	}
	snap, err := a.store.Collection("views").Doc(id).Get(ctx)
	if status.Code(err) == codes.NotFound {
		return false, nil
	}
	if err != nil {
		return false, err
	}
	data := snap.Data()
	if data["public"] == true {
		return true, nil
	}
	if me == "" {
		return false, nil
	}
	readers, _ := data["readers"].([]any)
	if data["owner_uid"] == me || slices.Contains(readers, any(me)) {
		return true, nil
	}
	permission, _ := data["required_permission"].(string)
	return permission != "" && a.grants(ctx, me, permission), nil
}

// search reads the newest of a list's rows holding every word: the store is asked
// for the longest, the most telling, and the others are checked on what comes back.
func (a *App) search(ctx context.Context, l *list, subject string, words []string) ([]*firestore.DocumentSnapshot, error) {
	if len(words) == 0 {
		return nil, nil
	}
	asked := slices.MaxFunc(words, func(x, y string) int { return utf8.RuneCountInString(x) - utf8.RuneCountInString(y) })
	var found []*firestore.DocumentSnapshot
	seen := map[string]bool{}
	for _, way := range l.query.ways() {
		queries, keep, err := a.stores(ctx, way, subject, "")
		if err != nil {
			return nil, err
		}
		for _, query := range queries {
			docs, err := query.Where(searchWords, "array-contains", asked).
				OrderBy("created_at", firestore.Desc).Limit(mostFound * searchedMore).Documents(ctx).GetAll()
			if err != nil {
				return nil, err
			}
			for _, doc := range docs {
				held, _ := doc.Data()[searchWords].([]any)
				all := !slices.ContainsFunc(words, func(word string) bool { return !slices.Contains(held, any(word)) })
				if all && !seen[doc.Ref.Path] && keep(doc) && !leftOut(ctx, way, doc, subject, "") {
					seen[doc.Ref.Path] = true
					found = append(found, doc)
				}
			}
		}
	}
	if len(l.guards) > 0 {
		var err error
		if found, err = a.readable(ctx, l, subject, found); err != nil {
			return nil, err
		}
	}
	slices.SortStableFunc(found, func(x, y *firestore.DocumentSnapshot) int {
		return compare(y.Data()["created_at"], x.Data()["created_at"])
	})
	if len(found) > mostFound {
		found = found[:mostFound]
	}
	return found, nil
}

// wordsAs is how this library works out words, changed whenever wordsIn is, so a
// record kept by an older one has its words worked out again.
const wordsAs = "1"

// wordsChanged gives each record its words where the fields searched changed since
// the backend last started, as when a deploy marks one searchable: each record read
// once and written where its words differ, then never again until they change.
func (a *App) wordsChanged(ctx context.Context) error {
	for _, s := range a.reg.schemas {
		var fields []string
		for _, f := range s.fields {
			if f.searched {
				fields = append(fields, f.name)
			}
		}
		definition := strings.Join(fields, " ") + " " + wordsAs
		kept := a.store.Collection("search_definitions").Doc(s.collection)
		snap, err := kept.Get(ctx)
		if err != nil && status.Code(err) != codes.NotFound {
			return err
		}
		if err == nil && snap.Data()["definition"] == definition {
			continue
		}
		if err != nil && len(fields) == 0 {
			continue // never searched
		}
		docs, err := a.store.Collection(s.collection).Documents(ctx).GetAll()
		if err != nil {
			return err
		}
		writer := a.store.BulkWriter(ctx)
		var jobs []*firestore.BulkWriterJob
		for _, doc := range docs {
			var words any = firestore.Delete
			if len(fields) > 0 {
				words = s.words(doc.Data())
			}
			held, had := doc.Data()[searchWords]
			if len(fields) == 0 && !had {
				continue
			}
			if list, ok := words.([]string); ok && had && slices.Equal(list, textsIn(held)) {
				continue
			}
			job, err := writer.Update(doc.Ref, []firestore.Update{{Path: searchWords, Value: words}})
			if err != nil {
				writer.End()
				return err
			}
			jobs = append(jobs, job)
		}
		writer.End()
		for _, job := range jobs {
			if _, err := job.Results(); err != nil && status.Code(err) != codes.NotFound {
				return err
			}
		}
		if _, err := kept.Set(ctx, map[string]any{"definition": definition, "collection": s.collection, "changed": len(jobs), "at": time.Now()}); err != nil {
			return err
		}
	}
	return nil
}
