// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

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
	"io/fs"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// Options say what to deploy, from the folder `one build` wrote.
type Options struct {
	Build string // the folder `one build` wrote, holding infrastructure/, api/ and web/
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
	Upload  func(ctx context.Context, project, web string, progress func(name, message string)) error
	// Records asks Hosting what the domain still needs at its DNS host.
	Records func(ctx context.Context, project, domain string) ([]string, error)
}

// A Line is one line of progress.
type Line struct {
	Step    string    `json:"step"`   // tidy, preview, up, build, upload, done
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
	var program Program
	if err := step("tidy", func() error {
		for _, dir := range []string{filepath.Join(o.Build, "api"), infrastructure} {
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
	var outputs map[string]any
	if err := step("up", func() error {
		var err error
		outputs, err = program.Up(ctx, log)
		return err
	}); err != nil {
		return "", err
	}
	project := text(outputs, "firebase_project_id")
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
		if err := os.WriteFile(filepath.Join(web, ".env.production"), []byte(env), 0o644); err != nil {
			return err
		}
		// A machine that builds only to deploy, like a Cloud Build, hasn't installed
		// the web app's packages yet.
		if _, err := os.Stat(filepath.Join(web, "node_modules")); errors.Is(err, fs.ErrNotExist) {
			if err := tools.Run(ctx, web, log, "yarn", "install", "--non-interactive"); err != nil {
				return err
			}
		}
		return tools.Run(ctx, web, log, "yarn", "build")
	}); err != nil {
		return "", err
	}
	if err := step("upload", func() error {
		return tools.Upload(ctx, project, web, func(name, message string) { say("upload", "progress", name+": "+message) })
	}); err != nil {
		return "", err
	}
	needed := records(outputs)
	// Firebase works out what a new domain needs a while after it's made, so what
	// Pulumi was told may be nothing yet.
	if domain := text(outputs, "domain"); needed == "" && domain != "" && tools.Records != nil {
		asked, err := tools.Records(ctx, project, domain)
		if err != nil {
			say("done", "progress", "couldn't ask Hosting what "+domain+" needs: "+err.Error())
		}
		needed = strings.Join(asked, "\n")
	}
	say("done", "finished", needed)
	return needed, nil
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
