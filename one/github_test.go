// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package one_test

import (
	"bytes"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"reflect"
	"testing"

	"github.com/da0x/uione/one"
)

// Repositories with bugs numbered within each, and the commits and pull requests
// that mention them.

type Repo struct {
	one.Record
	Slug       string `firestore:"slug" one:"required,unique,key"`
	Repository string `firestore:"repository" one:"unique"`
}

type Bug struct {
	one.Record
	Repo   string  `firestore:"repo" one:"required,key,refers=code::repo"`
	Number float64 `firestore:"number" one:"key,serial=repo"`
	Title  string  `firestore:"title" one:"required"`
}

type Link struct {
	one.Record
	Bug    string `firestore:"bug" one:"required,key,refers=code::bug"`
	URL    string `firestore:"url" one:"required,key"`
	Kind   string `firestore:"kind" one:"choices=commit|pull_request"`
	Title  string `firestore:"title"`
	Author string `firestore:"author"`
}

var code = one.Module("code",
	one.Command[Repo]("repo::create").Allow(one.Authenticated),
	one.Command[Bug]("bug::create").Allow(one.Authenticated),
	one.GitHub("/hooks/github").For(one.Entity[Repo](), "repository").Mentions(one.Entity[Bug]()).
		OnCommit(func(c *one.Ctx, m one.Mention) error {
			return one.Create(c, &Link{Bug: m.Issue, URL: m.URL, Kind: "commit", Title: m.Message, Author: m.Author})
		}).
		OnPullRequest(func(c *one.Ctx, m one.Mention) error {
			return one.Create(c, &Link{Bug: m.Issue, URL: m.URL, Kind: "pull_request", Title: m.Title, Author: m.Author})
		}),
	one.View("settings").Per(one.Entity[Repo]()).Public().GitHubSecret("secret"),
	one.View("bug_page").Per(one.Entity[Bug]()).Public().
		List("links", one.Where[Link]("bug", one.Subject)).Order("created_at").Fields("kind", "title", "author", "created_by"),
)

const secret = "a webhook secret"

// deliver posts what GitHub would, signed with a secret, and returns the status.
func (h *harness) deliver(event string, payload any, key string) (int, map[string]any) {
	h.t.Helper()
	return h.deliverAs("", event, payload, key)
}

// deliverAs is deliver with GitHub's id for the delivery.
func (h *harness) deliverAs(id, event string, payload any, key string) (int, map[string]any) {
	h.t.Helper()
	body, _ := json.Marshal(payload)
	mac := hmac.New(sha256.New, []byte(key))
	mac.Write(body)
	request, _ := http.NewRequest(http.MethodPost, h.server.URL+"/hooks/github", bytes.NewReader(body))
	request.Header.Set("X-GitHub-Event", event)
	if id != "" {
		request.Header.Set("X-GitHub-Delivery", id)
	}
	request.Header.Set("X-Hub-Signature-256", "sha256="+hex.EncodeToString(mac.Sum(nil)))
	response, err := http.DefaultClient.Do(request)
	if err != nil {
		h.t.Fatal(err)
	}
	defer response.Body.Close()
	reply := map[string]any{}
	json.NewDecoder(response.Body).Decode(&reply)
	return response.StatusCode, reply
}

func push(repository string, messages ...string) map[string]any {
	var commits []any
	for i, m := range messages {
		commits = append(commits, map[string]any{
			"id": string(rune('a' + i)), "message": m, "url": "https://github.com/" + repository + "/commit/" + string(rune('a'+i)),
			"author": map[string]any{"name": "Ada", "username": "ada"},
		})
	}
	return map[string]any{"repository": map[string]any{"full_name": repository}, "commits": commits}
}

func TestACommitThatMentionsABugShowsOnItsPage(t *testing.T) {
	t.Setenv("GITHUB_WEBHOOK_SECRET", secret)
	h := start(t)
	_, token := h.signUp("ada@example.com")
	h.mustRun("code/repo/create", token, map[string]any{"slug": "uione", "repository": "da0x/uione"})
	h.mustRun("code/repo/create", token, map[string]any{"slug": "other", "repository": "da0x/other"})
	h.mustRun("code/bug/create", token, map[string]any{"repo": "uione", "title": "Keys"})
	h.mustRun("code/bug/create", token, map[string]any{"repo": "uione", "title": "Serials"})
	h.mustRun("code/bug/create", token, map[string]any{"repo": "other", "title": "Elsewhere"})

	status, reply := h.deliver("push", push("Da0x/UIone", "Fix keys, #1 and #2", "Tidy #1 again", "No issue here, &#1; da0x/other#1", "#99 isn't there"), one.GitHubSecret("uione"))
	if status != http.StatusOK || reply["mentions"] != 3.0 {
		t.Fatalf("the push was answered %d %v", status, reply)
	}
	var got [][]any
	for _, link := range list(h.view("code::bug_page:uione-1"), "links") {
		got = append(got, []any{link["kind"], link["title"], link["author"], link["created_by"]})
	}
	want := [][]any{{"commit", "Fix keys, #1 and #2", "ada", "github"}, {"commit", "Tidy #1 again", "ada", "github"}}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("bug 1's links are %v, want %v", got, want)
	}
	if links := list(h.view("code::bug_page:other-1"), "links"); len(links) != 0 {
		t.Errorf("another repository's bug 1 got links %v", links)
	}

	// GitHub delivers again when it isn't sure: the same commit is the same link.
	h.deliver("push", push("Da0x/UIone", "Fix keys, #1 and #2"), one.GitHubSecret("uione"))
	if links := list(h.view("code::bug_page:uione-1"), "links"); len(links) != 2 {
		t.Errorf("a delivery sent again made %d links", len(links))
	}
}

func TestAPullRequestThatMentionsABugShowsOnItsPage(t *testing.T) {
	t.Setenv("GITHUB_WEBHOOK_SECRET", secret)
	h := start(t)
	_, token := h.signUp("ada@example.com")
	h.mustRun("code/repo/create", token, map[string]any{"slug": "uione", "repository": "da0x/uione"})
	h.mustRun("code/bug/create", token, map[string]any{"repo": "uione", "title": "Keys"})
	pull := map[string]any{"action": "opened", "repository": map[string]any{"full_name": "da0x/uione"},
		"pull_request": map[string]any{"number": 7, "title": "Keys", "body": "Closes #1.", "html_url": "https://github.com/da0x/uione/pull/7",
			"user": map[string]any{"login": "grace"}}}
	if status, reply := h.deliver("pull_request", pull, one.GitHubSecret("uione")); status != http.StatusOK || reply["mentions"] != 1.0 {
		t.Fatalf("the pull request was answered %d %v", status, reply)
	}
	pull["action"] = "labeled"
	if _, reply := h.deliver("pull_request", pull, one.GitHubSecret("uione")); reply["mentions"] != 0.0 {
		t.Errorf("a label on the pull request was handled as %v", reply)
	}
	links := list(h.view("code::bug_page:uione-1"), "links")
	if len(links) != 1 || links[0]["kind"] != "pull_request" || links[0]["author"] != "grace" {
		t.Errorf("bug 1's links are %v", links)
	}
}

func TestADeliveryIsRefusedUnlessItsProjectsOwnSecretSignedIt(t *testing.T) {
	h := start(t)
	_, token := h.signUp("ada@example.com")
	h.mustRun("code/repo/create", token, map[string]any{"slug": "uione", "repository": "da0x/uione"})
	h.mustRun("code/repo/create", token, map[string]any{"slug": "other", "repository": "da0x/other"})
	h.mustRun("code/bug/create", token, map[string]any{"repo": "uione", "title": "Keys"})

	t.Setenv("GITHUB_WEBHOOK_SECRET", "")
	if status, _ := h.deliver("push", push("da0x/uione", "#1"), one.GitHubSecret("uione")); status != http.StatusServiceUnavailable {
		t.Errorf("without a master secret, a delivery was answered %d", status)
	}
	t.Setenv("GITHUB_WEBHOOK_SECRET", secret)
	if one.GitHubSecret("uione") == one.GitHubSecret("other") || one.GitHubSecret("uione") == secret {
		t.Fatal("projects share a secret, or a project's secret is the master")
	}
	for name, key := range map[string]string{
		"another project's": one.GitHubSecret("other"),
		"the master":        secret,
		"a made-up":         "guess",
	} {
		if status, _ := h.deliver("push", push("da0x/uione", "#1"), key); status != http.StatusUnauthorized {
			t.Errorf("a delivery signed with %s secret was answered %d", name, status)
		}
	}
	if status, _ := h.deliver("push", push("", "#1"), one.GitHubSecret("uione")); status != http.StatusUnauthorized {
		t.Errorf("a delivery naming no repository was answered %d", status)
	}
	if links := list(h.view("code::bug_page:uione-1"), "links"); len(links) != 0 {
		t.Errorf("a refused delivery made links %v", links)
	}
	if status, _ := h.deliver("ping", map[string]any{"repository": map[string]any{"full_name": "da0x/uione"}}, one.GitHubSecret("uione")); status != http.StatusOK {
		t.Errorf("GitHub's ping was answered %d", status)
	}
}

func TestAProjectsSecretIsShownOnItsOwnPage(t *testing.T) {
	t.Setenv("GITHUB_WEBHOOK_SECRET", secret)
	h := start(t)
	_, token := h.signUp("ada@example.com")
	h.mustRun("code/repo/create", token, map[string]any{"slug": "uione", "repository": "da0x/uione"})
	if got := h.view("code::settings:uione")["secret"]; got != one.GitHubSecret("uione") || got == "" {
		t.Errorf("uione's settings show the secret %v", got)
	}
}

func TestARepositoryBelongsToOneProjectWhateverItsCapitals(t *testing.T) {
	t.Setenv("GITHUB_WEBHOOK_SECRET", secret)
	h := start(t)
	_, token := h.signUp("ada@example.com")
	h.mustRun("code/repo/create", token, map[string]any{"slug": "uione", "repository": "Da0x/UIone"})
	h.expect("code/repo/create", token, map[string]any{"slug": "aaa", "repository": "da0x/uione"}, http.StatusBadRequest, "Repository is already taken")
	h.mustRun("code/repo/create", token, map[string]any{"slug": "first"})
	h.mustRun("code/repo/create", token, map[string]any{"slug": "second"})
}

func TestADeliverySentAgainIsHandledOnce(t *testing.T) {
	t.Setenv("GITHUB_WEBHOOK_SECRET", secret)
	h := start(t)
	_, token := h.signUp("ada@example.com")
	h.mustRun("code/repo/create", token, map[string]any{"slug": "uione", "repository": "da0x/uione"})
	h.mustRun("code/bug/create", token, map[string]any{"repo": "uione", "title": "Keys"})
	if _, reply := h.deliverAs("d-1", "push", push("da0x/uione", "#1"), one.GitHubSecret("uione")); reply["mentions"] != 1.0 {
		t.Fatalf("the first delivery was answered %v", reply)
	}
	if _, reply := h.deliverAs("d-1", "push", push("da0x/uione", "#1"), one.GitHubSecret("uione")); reply["mentions"] != 0.0 || reply["already"] != true {
		t.Errorf("the same delivery sent again was answered %v", reply)
	}
}
