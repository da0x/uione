// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package deploy

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"io"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

// world is a deploy's surroundings, recorded: what ran, in what order.
type world struct {
	did     []string
	outputs map[string]any
	failAt  string
}

func (w *world) Preview(context.Context, io.Writer) (string, error) {
	w.did = append(w.did, "preview")
	return "create: 3\n", w.fails("preview")
}

func (w *world) Up(context.Context, io.Writer) (map[string]any, error) {
	w.did = append(w.did, "up")
	return w.outputs, w.fails("up")
}

func (w *world) fails(step string) error {
	if w.failAt == step {
		return errors.New(step + " went wrong")
	}
	return nil
}

func (w *world) tools() Tools {
	return Tools{
		Program: func(dir, stack string) (Program, error) {
			w.did = append(w.did, "open "+filepath.Base(dir)+" "+stack)
			return w, nil
		},
		Run: func(_ context.Context, dir string, _ io.Writer, name string, args ...string) error {
			w.did = append(w.did, filepath.Base(dir)+": "+name+" "+strings.Join(args, " "))
			if name == "yarn" && args[0] == "build" {
				return w.fails("build")
			}
			return nil
		},
		Upload: func(_ context.Context, project, web string, progress func(string, string)) error {
			w.did = append(w.did, "upload "+project+" from "+filepath.Base(web))
			progress("release", "released")
			return w.fails("upload")
		},
	}
}

func built(t *testing.T) string {
	t.Helper()
	dir := t.TempDir()
	for _, sub := range []string{"api", "infrastructure", "web"} {
		os.MkdirAll(filepath.Join(dir, sub), 0o755)
	}
	return dir
}

var outputs = map[string]any{
	"firebase_api_key": "key", "firebase_app_id": "1:2:web:3", "firebase_project_id": "ui-one",
	"firebase_auth_domain": "ui-one.firebaseapp.com", "dns_records": []any{"uione.io A 199.36.158.100"},
}

func lines(t *testing.T, progress *bytes.Buffer) []string {
	t.Helper()
	var out []string
	for _, text := range strings.Split(strings.TrimSpace(progress.String()), "\n") {
		var l Line
		if err := json.Unmarshal([]byte(text), &l); err != nil {
			t.Fatalf("a progress line isn't JSON: %q", text)
		}
		out = append(out, l.Step+" "+l.Status)
	}
	return out
}

func TestADeployRunsEveryStepInOrderAndReportsEach(t *testing.T) {
	dir := built(t)
	w := &world{outputs: outputs}
	var progress bytes.Buffer
	var shown string
	needed, err := Run(context.Background(), Options{Build: dir, Stack: "production", Progress: &progress,
		Confirm: func(preview string) bool { shown = preview; return true }}, w.tools())
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"api: go mod tidy", "infrastructure: go mod tidy", "open infrastructure production", "preview", "up",
		"web: yarn install --non-interactive", "web: yarn build", "upload ui-one from web"}
	if !reflect.DeepEqual(w.did, want) {
		t.Errorf("the deploy did\n%v\nwant\n%v", w.did, want)
	}
	if shown != "create: 3\n" {
		t.Errorf("the person was shown %q", shown)
	}
	env, _ := os.ReadFile(filepath.Join(dir, "web", ".env.production"))
	if string(env) != "VITE_FIREBASE_API_KEY=key\nVITE_FIREBASE_APP_ID=1:2:web:3\nVITE_FIREBASE_PROJECT_ID=ui-one\nVITE_FIREBASE_AUTH_DOMAIN=ui-one.firebaseapp.com\n" {
		t.Errorf("the web app was built with\n%s", env)
	}
	if needed != "uione.io A 199.36.158.100" {
		t.Errorf("the domain needs %q", needed)
	}
	wantLines := []string{"tidy started", "tidy finished", "preview started", "preview finished", "up started", "up finished",
		"build started", "build finished", "upload started", "upload progress", "upload finished", "done finished"}
	if got := lines(t, &progress); !reflect.DeepEqual(got, wantLines) {
		t.Errorf("progress was\n%v", got)
	}
}

func TestSayingNoChangesNothing(t *testing.T) {
	w := &world{outputs: outputs}
	var progress bytes.Buffer
	_, err := Run(context.Background(), Options{Build: built(t), Stack: "production", Progress: &progress,
		Confirm: func(string) bool { return false }}, w.tools())
	if !errors.Is(err, ErrRefused) {
		t.Fatalf("saying no gave %v", err)
	}
	for _, did := range w.did {
		if did == "up" || strings.HasPrefix(did, "upload") || strings.HasPrefix(did, "web:") {
			t.Errorf("after saying no, the deploy still did %q", did)
		}
	}
	if got := lines(t, &progress); got[len(got)-1] != "preview refused" {
		t.Errorf("progress ended %v", got)
	}
}

func TestAFailedStepStopsTheDeployAndSaysWhich(t *testing.T) {
	for _, step := range []string{"preview", "up", "build", "upload"} {
		w := &world{outputs: outputs, failAt: step}
		var progress bytes.Buffer
		_, err := Run(context.Background(), Options{Build: built(t), Stack: "production", Progress: &progress}, w.tools())
		if err == nil || !strings.Contains(err.Error(), step+" went wrong") {
			t.Errorf("a failed %s gave %v", step, err)
		}
		got := lines(t, &progress)
		if got[len(got)-1] != step+" failed" {
			t.Errorf("a failed %s ended the progress with %v", step, got[len(got)-1])
		}
	}
}

func TestAStackWithoutTheWebAppsSettingsIsntBuilt(t *testing.T) {
	w := &world{outputs: map[string]any{"firebase_project_id": "ui-one"}}
	_, err := Run(context.Background(), Options{Build: built(t), Stack: "production"}, w.tools())
	if err == nil || !strings.Contains(err.Error(), "firebase_api_key") {
		t.Errorf("a stack missing settings gave %v", err)
	}
	for _, did := range w.did {
		if strings.HasPrefix(did, "web:") {
			t.Errorf("the app was built anyway")
		}
	}
}

func TestAWebAppWithItsPackagesInstalledIsntInstalledAgain(t *testing.T) {
	dir := built(t)
	os.MkdirAll(filepath.Join(dir, "web", "node_modules"), 0o755)
	w := &world{outputs: outputs}
	if _, err := Run(context.Background(), Options{Build: dir, Stack: "production", Progress: io.Discard,
		Confirm: func(string) bool { return true }}, w.tools()); err != nil {
		t.Fatal(err)
	}
	for _, did := range w.did {
		if strings.Contains(did, "yarn install") {
			t.Errorf("installed again: %v", w.did)
		}
	}
}

func TestADomainFirebaseHasntWorkedOutYetIsAskedAboutAgain(t *testing.T) {
	dir := built(t)
	w := &world{outputs: map[string]any{
		"firebase_api_key": "key", "firebase_app_id": "1:2:web:3", "firebase_project_id": "uione-cloud",
		"firebase_auth_domain": "uione-cloud.firebaseapp.com", "dns_records": []any{}, "domain": "studio.uione.io",
	}}
	tools := w.tools()
	tools.Records = func(_ context.Context, project, domain string) ([]string, error) {
		return []string{"add CNAME " + domain + " " + project + ".web.app"}, nil
	}
	needed, err := Run(context.Background(), Options{Build: dir, Stack: "production", Progress: io.Discard,
		Confirm: func(string) bool { return true }}, tools)
	if err != nil {
		t.Fatal(err)
	}
	if needed != "add CNAME studio.uione.io uione-cloud.web.app" {
		t.Errorf("the domain needs %q", needed)
	}
}
