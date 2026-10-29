// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Package deploy deploys a project that `one build` wrote: everything in Google
// Cloud through its Pulumi program, then the web app, built with what Pulumi made,
// onto Firebase Hosting. The command line and uione's hosting service both use it,
// so a deploy is the same wherever it's started.
//
// Each step is reported as it starts and ends, as a line of JSON, so whoever started
// the deploy can show where it is.
package deploy

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// Options say what to deploy, from the folder `one build` wrote.
type Options struct {
	Build string // the folder `one build` wrote, holding infrastructure/, web/ and, for a project with a backend, api/
	Stack string // the Pulumi stack, like production
	// The Google Cloud project it deploys to. A new stack takes over the stack that
	// already manages it, as after its environment is renamed.
	Project string

	// Confirm is shown what the deploy will change and says whether to go ahead.
	// Without it, nothing is asked.
	Confirm func(preview string) bool

	Progress io.Writer // one JSON line per step; may be nil
	Log      io.Writer // what Pulumi and the build print; may be nil
}

// Program is the project's Pulumi program, run through Pulumi's Automation API, or
// a stand-in in a test.
type Program interface {
	Preview(ctx context.Context, log io.Writer) (string, error) // what would change
	Up(ctx context.Context, log io.Writer) (map[string]any, error)
}

// Tools are what a deploy runs besides Pulumi.
type Tools struct {
	Program func(dir, stack, project string) (Program, error)
	Run     func(ctx context.Context, dir string, log io.Writer, name string, args ...string) error // a command, like yarn build
	Upload  func(ctx context.Context, project, site, web string, progress func(name, message string)) error
	// Records asks Hosting what the domain still needs at its DNS host.
	Records func(ctx context.Context, project, site, domain string) ([]string, error)
	// Rehearse starts the new backend on a copy of what the project stores, before
	// anything is built or changed, and says why it didn't start, if it didn't. It
	// returns ErrNoRehearsal where it can't be done, like on a machine without
	// Docker. May be nil.
	Rehearse func(ctx context.Context, api, project string, log io.Writer) error
	// Explain says why Cloud Run turned the new backend down, from its revision's
	// log, given what the deploy failed with; "" when it can't tell. May be nil.
	Explain func(ctx context.Context, project, failure string) string
}

// ErrNoRehearsal is what Rehearse returns where it can't be done: the deploy goes on
// without it, and says so.
var ErrNoRehearsal = errors.New("deploy: the backend can't be started here before it's deployed")

// A Line is one line of progress.
type Line struct {
	Step    string    `json:"step"`   // tidy, preview, rehearse, up, build, upload, done
	Status  string    `json:"status"` // started, finished, failed, refused, or progress
	Message string    `json:"message,omitempty"`
	At      time.Time `json:"at"`
}

// ErrRefused is returned when Confirm says not to go ahead. Nothing was changed.
var ErrRefused = errors.New("deploy: not confirmed, so nothing was changed")

// Run deploys, and returns what the site's domain needs at its DNS host, if
// anything. It stops at the first step that fails, and says which.
func Run(ctx context.Context, o Options, tools Tools) (string, error) {
	say := func(step, status, message string) {
		if o.Progress == nil {
			return
		}
		line, _ := json.Marshal(Line{Step: step, Status: status, Message: message, At: time.Now().UTC()})
		o.Progress.Write(append(line, '\n'))
	}
	log := o.Log
	if log == nil {
		log = io.Discard
	}
	step := func(name string, do func() error) error {
		say(name, "started", "")
		if err := do(); err != nil {
			status := "failed"
			if errors.Is(err, ErrRefused) {
				status = "refused"
			}
			say(name, status, err.Error())
			return err
		}
		say(name, "finished", "")
		return nil
	}

	infrastructure := filepath.Join(o.Build, "infrastructure")
	web := filepath.Join(o.Build, "web")
	// A project with no backend, like a company's front page, has no api/ to build.
	api := filepath.Join(o.Build, "api")
	dirs := []string{api, infrastructure}
	backend := true
	if _, err := os.Stat(api); errors.Is(err, os.ErrNotExist) {
		dirs, backend = dirs[1:], false
	}
	var program Program
	if err := step("tidy", func() error {
		for _, dir := range dirs {
			if err := tools.Run(ctx, dir, log, "go", "mod", "tidy"); err != nil {
				return fmt.Errorf("go mod tidy in %s: %w", dir, err)
			}
		}
		var err error
		program, err = tools.Program(infrastructure, o.Stack, o.Project)
		return err
	}); err != nil {
		return "", err
	}
	if err := step("preview", func() error {
		preview, err := program.Preview(ctx, log)
		if err != nil {
			return err
		}
		if o.Confirm != nil && !o.Confirm(preview) {
			return ErrRefused
		}
		return nil
	}); err != nil {
		return "", err
	}
	// The new backend, started on a copy of what's stored before anything is built:
	// what it does when it starts, like a once moving records along, fails here in
	// seconds, saying why, rather than after its image is built, in Cloud Run.
	project := o.Project
	if project == "" {
		project = configuredProject(infrastructure)
	}
	if tools.Rehearse != nil && project != "" && backend {
		if err := step("rehearse", func() error {
			err := tools.Rehearse(ctx, api, project, log)
			if errors.Is(err, ErrNoRehearsal) {
				say("rehearse", "progress", err.Error())
				return nil
			}
			return err
		}); err != nil {
			return "", err
		}
	}
	var outputs map[string]any
	if err := step("up", func() error {
		var err error
		outputs, err = program.Up(ctx, log)
		if err != nil && tools.Explain != nil && project != "" {
			if why := tools.Explain(ctx, project, err.Error()); why != "" {
				err = fmt.Errorf("%w\n%s", err, why)
			}
		}
		return err
	}); err != nil {
		return "", err
	}
	project = text(outputs, "firebase_project_id")
	// The site it's served from: its own, or the one Firebase named for the project.
	site := text(outputs, "site")
	if site == "" {
		site = project
	}
	if err := step("build", func() error {
		env, err := settings(outputs)
		if err != nil {
			return err
		}
		// The commit it's built from, at the foot of every page, so what's deployed
		// can be told: as the deploy says, or as git does where it's built.
		if commit := commitOf(o.Build); commit != "" {
			env += "VITE_UIONE_COMMIT=" + commit + "\n"
		}
		// And where that commit can be read, so the foot links to it.
		if repository := repositoryOf(o.Build); repository != "" {
			env += "VITE_UIONE_REPOSITORY=" + repository + "\n"
		}
		if err := os.WriteFile(filepath.Join(web, ".env.production"), []byte(env), 0o644); err != nil {
			return err
		}
		// The web app's packages, as its package.json names them: installed on a
		// machine that builds only to deploy, and brought up to date where an earlier
		// build left others, which would otherwise ship in their place. Every one is
		// installed again, since a package copied from a folder, as uione's are while
		// it's worked on, keeps its version as it changes, and yarn would keep the copy
		// it made before.
		if err := tools.Run(ctx, web, log, "yarn", "install", "--non-interactive", "--force"); err != nil {
			return err
		}
		return tools.Run(ctx, web, log, "yarn", "build")
	}); err != nil {
		return "", err
	}
	if err := step("upload", func() error {
		return tools.Upload(ctx, project, site, web, func(name, message string) { say("upload", "progress", name+": "+message) })
	}); err != nil {
		return "", err
	}
	needed := records(outputs)
	// Firebase works out what a new domain needs a while after it's made, so what
	// Pulumi was told may be nothing yet.
	if domain := text(outputs, "domain"); needed == "" && domain != "" && tools.Records != nil {
		asked, err := tools.Records(ctx, project, site, domain)
		if err != nil {
			say("done", "progress", "couldn't ask Hosting what "+domain+" needs: "+err.Error())
		}
		needed = strings.Join(asked, "\n")
	}
	say("done", "finished", needed)
	return needed, nil
}

// Rehearse only rehearses: the backend, tidied as a deploy tidies it, started on a
// copy of what the project stores, with nothing previewed, built or changed. A
// release is tried this way on each project built on it before it's tagged, so
// where it can't be done, as without Docker, that's an error and not passed over.
// A project with no backend has nothing to rehearse.
func Rehearse(ctx context.Context, o Options, tools Tools) error {
	log := o.Log
	if log == nil {
		log = io.Discard
	}
	api := filepath.Join(o.Build, "api")
	if _, err := os.Stat(api); errors.Is(err, os.ErrNotExist) {
		fmt.Fprintln(log, "rehearsal: there's no backend to rehearse")
		return nil
	}
	project := o.Project
	if project == "" {
		project = configuredProject(filepath.Join(o.Build, "infrastructure"))
	}
	if project == "" {
		return errors.New("deploy: there's no Google Cloud project to copy what's stored from")
	}
	if err := tools.Run(ctx, api, log, "go", "mod", "tidy"); err != nil {
		return fmt.Errorf("go mod tidy in %s: %w", api, err)
	}
	return tools.Rehearse(ctx, api, project, log)
}

// The web app's settings, from what Pulumi made. A missing one stops the deploy
// rather than build an app without it.
func settings(outputs map[string]any) (string, error) {
	var env strings.Builder
	for _, pair := range [][2]string{
		{"VITE_FIREBASE_API_KEY", "firebase_api_key"},
		{"VITE_FIREBASE_APP_ID", "firebase_app_id"},
		{"VITE_FIREBASE_PROJECT_ID", "firebase_project_id"},
		{"VITE_FIREBASE_AUTH_DOMAIN", "firebase_auth_domain"},
	} {
		value := text(outputs, pair[1])
		if value == "" {
			return "", fmt.Errorf("the stack has no %s output, which the web app needs", pair[1])
		}
		fmt.Fprintf(&env, "%s=%s\n", pair[0], value)
	}
	// Counting visitors, for an app that does, once the project is linked to Google
	// Analytics; until then there's none, and the app counts no one.
	if id := text(outputs, "firebase_measurement_id"); id != "" {
		fmt.Fprintf(&env, "VITE_FIREBASE_MEASUREMENT_ID=%s\n", id)
	}
	return env.String(), nil
}

// records says what the site's domain needs at its DNS host, if anything.
func records(outputs map[string]any) string {
	value, ok := outputs["dns_records"]
	if !ok {
		return ""
	}
	switch v := value.(type) {
	case string:
		return v
	case []any:
		lines := make([]string, 0, len(v))
		for _, item := range v {
			lines = append(lines, fmt.Sprint(item))
		}
		sort.Strings(lines)
		return strings.Join(lines, "\n")
	}
	return fmt.Sprint(value)
}

func text(outputs map[string]any, name string) string {
	value, _ := outputs[name].(string)
	return value
}

// commitOf is the commit a build is of: UIONE_COMMIT when the deploy names it, as a
// build from a snapshot without git does, or what git says of the folder, or none.
func commitOf(dir string) string {
	if commit := os.Getenv("UIONE_COMMIT"); commit != "" {
		return commit
	}
	out, err := exec.Command("git", "-C", dir, "rev-parse", "HEAD").Output()
	if err != nil {
		return ""
	}
	return strings.TrimSpace(string(out))
}

// repositoryOf is where a build's commits can be read on the web, like
// https://github.com/someone/tracker: UIONE_REPOSITORY when the deploy names it, or the
// folder's origin as git says, written as a web address, or none.
func repositoryOf(dir string) string {
	repository := os.Getenv("UIONE_REPOSITORY")
	if repository == "" {
		out, err := exec.Command("git", "-C", dir, "remote", "get-url", "origin").Output()
		if err != nil {
			return ""
		}
		repository = strings.TrimSpace(string(out))
	}
	return webAddress(repository)
}

// webAddress writes a GitHub remote, git@github.com:someone/tracker.git or its https
// form, as the page it's at, https://github.com/someone/tracker; another isn't linked.
func webAddress(remote string) string {
	remote = strings.TrimSuffix(strings.TrimSuffix(remote, "/"), ".git")
	if path, ok := strings.CutPrefix(remote, "git@github.com:"); ok {
		remote = "https://github.com/" + path
	}
	if path, ok := strings.CutPrefix(remote, "ssh://git@github.com/"); ok {
		remote = "https://github.com/" + path
	}
	path, ok := strings.CutPrefix(remote, "https://github.com/")
	if !ok || strings.Count(path, "/") != 1 || strings.ContainsAny(path, " \"\n") {
		return ""
	}
	return remote
}

// configuredProject is the Google Cloud project the Pulumi program deploys to, as its
// Pulumi.yaml says, gcp:project; "" when it doesn't say.
func configuredProject(infrastructure string) string {
	data, err := os.ReadFile(filepath.Join(infrastructure, "Pulumi.yaml"))
	if err != nil {
		return ""
	}
	for _, line := range strings.Split(string(data), "\n") {
		if value, found := strings.CutPrefix(strings.TrimSpace(line), "gcp:project:"); found {
			return strings.Trim(strings.TrimSpace(value), `"'`)
		}
	}
	return ""
}
