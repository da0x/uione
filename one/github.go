// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one

import (
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"reflect"
	"regexp"
	"strconv"
	"strings"
	"time"

	"cloud.google.com/go/firestore"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// Mention is an issue a commit or a pull request names, like #12 in "Fix the
// shelf, #12". Issue is its id; the rest says what mentioned it.
type Mention struct {
	Issue   string
	Message string  // a commit's message
	Title   string  // a pull request's title
	URL     string  // the commit's or pull request's page on GitHub
	Author  string  // who wrote it, by their GitHub name
	SHA     string  // a commit's id
	Number  float64 // a pull request's number
}

// GitHubSpec receives GitHub's webhook: the commits pushed to a repository, and
// its pull requests. Each project names its repository, and each #12 in a commit's
// message, or a pull request's title or description, is a mention of that
// project's issue 12. A mention of an issue that isn't there is left out.
//
// Every delivery is signed with the secret in GITHUB_WEBHOOK_SECRET, and one
// that isn't is refused. Each mention is handled in a transaction of its own, by
// "github", so a delivery GitHub sends again changes nothing twice that's keyed.
type GitHubSpec struct {
	route      string
	scope      reflect.Type // the entity a repository belongs to, like a project
	repository string       // and its field that names the repository
	issue      reflect.Type // the entity a #12 names, numbered within the project
	onCommit   func(*Ctx, Mention) error
	onPull     func(*Ctx, Mention) error
}

// GitHub receives GitHub's webhook at a route, like /hooks/github.
func GitHub(route string) *GitHubSpec { return &GitHubSpec{route: route} }

// For says which entity a repository belongs to, and the field that names it, as
// in For(Entity[Project](), "repository") for "da0x/uione".
func (g *GitHubSpec) For(scope Kind, field string) *GitHubSpec {
	g.scope, g.repository = scope.typ, field
	return g
}

// Mentions says what a #12 names: an entity whose key is the project and a number
// counted within it, like an issue.
func (g *GitHubSpec) Mentions(issue Kind) *GitHubSpec {
	g.issue = issue.typ
	return g
}

// OnCommit runs for each issue each pushed commit mentions.
func (g *GitHubSpec) OnCommit(do func(*Ctx, Mention) error) *GitHubSpec {
	g.onCommit = do
	return g
}

// OnPullRequest runs for each issue a pull request mentions, whenever it's opened,
// edited, closed or reopened.
func (g *GitHubSpec) OnPullRequest(do func(*Ctx, Mention) error) *GitHubSpec {
	g.onPull = do
	return g
}

func (g *GitHubSpec) register(r *registry, ns string) {
	if g.scope == nil || g.issue == nil {
		panic("one: a GitHub webhook needs For and Mentions")
	}
	r.schema(g.scope, ns)
	r.schema(g.issue, ns)
	r.hooks = append(r.hooks, g)
}

// numbered is how an issue's id is made from its project and number: its key
// fields, the one pointing at the project and the serial counted within it.
func (g *GitHubSpec) numbered(r *registry) (place, number *field, err error) {
	scope, issue := r.schemas[g.scope], r.schemas[g.issue]
	for _, key := range issue.keys {
		switch {
		case key.refers == scope.entity:
			place = key
		case key.serial && issue.field(key.per) != nil && issue.field(key.per).refers == scope.entity:
			number = key
		}
	}
	if place == nil || number == nil || len(issue.keys) != 2 || issue.keys[0] != place {
		return nil, nil, fmt.Errorf("one: a GitHub webhook mentions %s, so its key has to be the %s it's in and a serial per %s, in that order",
			issue.name, scope.name, scope.name)
	}
	if scope.field(g.repository) == nil {
		return nil, nil, fmt.Errorf("one: a GitHub webhook finds the %s by %s, which it doesn't have", scope.name, g.repository)
	}
	return place, number, nil
}

// mentioned finds every #12 in some text, each once, in the order they're written.
// One in a link (owner/repo#12), a URL (/#12) or a character reference (&#12;)
// isn't a mention.
var mentioned = regexp.MustCompile(`(?:^|[^\w&/#])#([0-9]{1,9})\b`)

func mentions(text string) []float64 {
	var numbers []float64
	seen := map[float64]bool{}
	for _, m := range mentioned.FindAllStringSubmatch(text, -1) {
		n, err := strconv.ParseFloat(m[1], 64)
		if err != nil || n < 1 || seen[n] {
			continue
		}
		seen[n] = true
		numbers = append(numbers, n)
	}
	return numbers
}

// What a delivery says, as far as a webhook reads it.
type delivery struct {
	Action     string `json:"action"`
	Repository struct {
		FullName string `json:"full_name"`
	} `json:"repository"`
	Commits []struct {
		ID      string `json:"id"`
		Message string `json:"message"`
		URL     string `json:"url"`
		Author  struct {
			Name     string `json:"name"`
			Username string `json:"username"`
		} `json:"author"`
	} `json:"commits"`
	PullRequest struct {
		Number  float64 `json:"number"`
		Title   string  `json:"title"`
		Body    string  `json:"body"`
		HTMLURL string  `json:"html_url"`
		User    struct {
			Login string `json:"login"`
		} `json:"user"`
	} `json:"pull_request"`
}

// signed says whether a delivery's X-Hub-Signature-256 is the secret's HMAC of
// its body, compared in constant time.
func signed(secret string, body []byte, signature string) bool {
	given, ok := strings.CutPrefix(signature, "sha256=")
	if !ok {
		return false
	}
	want, err := hex.DecodeString(given)
	if err != nil {
		return false
	}
	mac := hmac.New(sha256.New, []byte(secret))
	mac.Write(body)
	return hmac.Equal(mac.Sum(nil), want)
}

func (a *App) serveGitHub(g *GitHubSpec) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		secret := os.Getenv("GITHUB_WEBHOOK_SECRET")
		if secret == "" {
			a.log.Printf("one: a GitHub delivery came, but GITHUB_WEBHOOK_SECRET isn't set")
			reply(w, http.StatusServiceUnavailable, map[string]any{"error": "this site doesn't take GitHub's webhook yet"})
			return
		}
		body, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 25<<20))
		if err != nil {
			reply(w, http.StatusRequestEntityTooLarge, map[string]any{"error": "that delivery is too large"})
			return
		}
		if !signed(secret, body, r.Header.Get("X-Hub-Signature-256")) {
			reply(w, http.StatusUnauthorized, map[string]any{"error": "that delivery isn't signed with this site's secret"})
			return
		}
		event := r.Header.Get("X-GitHub-Event")
		if event == "ping" {
			reply(w, http.StatusOK, map[string]any{"ok": true})
			return
		}
		var d delivery
		if err := json.Unmarshal(body, &d); err != nil {
			reply(w, http.StatusBadRequest, map[string]any{"error": "that delivery isn't JSON GitHub sends"})
			return
		}
		count, err := a.deliver(r.Context(), g, event, &d)
		if err != nil {
			a.log.Printf("one: a GitHub %s delivery failed: %v", event, err)
			reply(w, http.StatusInternalServerError, map[string]any{"error": "something went wrong on our side; GitHub will try again"})
			return
		}
		reply(w, http.StatusOK, map[string]any{"mentions": count})
	}
}

// deliver handles what a delivery mentions, and says how many it handled.
func (a *App) deliver(ctx context.Context, g *GitHubSpec, event string, d *delivery) (int, error) {
	type found struct {
		number  float64
		mention Mention
		do      func(*Ctx, Mention) error
		command string
	}
	var all []found
	switch event {
	case "push":
		if g.onCommit == nil {
			return 0, nil
		}
		for _, c := range d.Commits {
			author := c.Author.Username
			if author == "" {
				author = c.Author.Name
			}
			for _, n := range mentions(c.Message) {
				all = append(all, found{n, Mention{Message: c.Message, URL: c.URL, Author: author, SHA: c.ID}, g.onCommit, "github::commit"})
			}
		}
	case "pull_request":
		if g.onPull == nil {
			return 0, nil
		}
		switch d.Action {
		case "opened", "edited", "closed", "reopened":
		default:
			return 0, nil
		}
		p := d.PullRequest
		for _, n := range mentions(p.Title + "\n" + p.Body) {
			all = append(all, found{n, Mention{Title: p.Title, URL: p.HTMLURL, Author: p.User.Login, Number: p.Number}, g.onPull, "github::pull_request"})
		}
	}
	if len(all) == 0 {
		return 0, nil
	}

	// The project the repository belongs to, by its name as written or lowercased.
	scope := a.reg.schemas[g.scope]
	name := d.Repository.FullName
	names := []string{name}
	if lower := strings.ToLower(name); lower != name {
		names = append(names, lower)
	}
	projects, err := a.store.Collection(scope.collection).Where(g.repository, "in", names).Limit(1).Documents(ctx).GetAll()
	if err != nil || len(projects) == 0 {
		return 0, err
	}
	project := projects[0].Ref.ID

	place, number, err := g.numbered(a.reg)
	if err != nil {
		return 0, err
	}
	issue := a.reg.schemas[g.issue]
	handled := 0
	for _, f := range all {
		v := reflect.New(issue.typ).Elem()
		v.FieldByIndex(place.index).SetString(project)
		v.FieldByIndex(number.index).SetFloat(f.number)
		id, _ := issue.id(v)
		f.mention.Issue = id
		ok, err := a.mention(ctx, issue, id, f.mention, f.do, f.command)
		if err != nil {
			return handled, err
		}
		if ok {
			handled++
		}
	}
	return handled, nil
}

// mention runs a webhook's handler for one mention, in its own transaction, unless
// the issue it names isn't there.
func (a *App) mention(ctx context.Context, issue *schema, id string, m Mention, do func(*Ctx, Mention) error, command string) (bool, error) {
	now := time.Now().UTC()
	var events []Event
	err := a.store.RunTransaction(ctx, func(ctx context.Context, tx *firestore.Transaction) error {
		events = nil
		if _, err := tx.Get(a.store.Collection(issue.collection).Doc(id)); err != nil {
			return err
		}
		body := &Ctx{Context: ctx, me: "github", now: now, app: a, tx: tx, counters: map[string]*counting{}, command: command}
		if err := do(body, m); err != nil {
			return err
		}
		var err error
		events, err = body.save()
		return err
	})
	if status.Code(err) == codes.NotFound {
		return false, nil
	}
	var failure *Failure
	if errors.As(err, &failure) {
		// What the handler refused, like a mention it can't store, isn't GitHub's to retry.
		a.log.Printf("one: a GitHub mention of %s was refused: %s", id, failure.Message)
		return false, nil
	}
	if err != nil {
		return false, err
	}
	for _, ev := range events {
		a.publish(ctx, ev)
	}
	return true, nil
}
