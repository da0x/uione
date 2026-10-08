// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package deploy

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"reflect"
	"sort"
	"strings"
	"testing"

	"github.com/pulumi/pulumi/sdk/v3/go/auto"
	"github.com/pulumi/pulumi/sdk/v3/go/common/apitype"
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
		Program: func(dir, stack, project string) (Program, error) {
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

// Only the stack holding a Google Cloud project's Firebase project manages it, so
// only that one is taken over by a renamed environment's new stack.
func TestAStackManagesTheProjectWhoseFirebaseItHolds(t *testing.T) {
	state := []byte(`{"resources":[{"type":"pulumi:pulumi:Stack","id":""},` +
		`{"type":"gcp:firebase/project:Project","id":"projects/neotrac"},` +
		`{"type":"gcp:firestore/database:Database","id":"projects/neotrac-staging/databases/(default)"}]}`)
	if !manages(state, "neotrac") {
		t.Error("a stack holding projects/neotrac doesn't manage neotrac")
	}
	if manages(state, "neotrac-staging") {
		t.Error("a stack is said to manage a project it only mentions")
	}
	if manages([]byte(`not json`), "neotrac") {
		t.Error("a state that can't be read manages a project")
	}
}

// A renamed environment's new stack takes over the stack that manages its Google
// Cloud project, with what it holds, on a real Pulumi with a file backend.
func TestANewStackTakesOverTheOneManagingItsProject(t *testing.T) {
	if _, err := exec.LookPath("pulumi"); err != nil {
		t.Skip("pulumi isn't installed")
	}
	dir := t.TempDir()
	t.Setenv("PULUMI_BACKEND_URL", "file://"+t.TempDir())
	t.Setenv("PULUMI_CONFIG_PASSPHRASE", "test")
	t.Setenv("PULUMI_SKIP_UPDATE_CHECK", "true")
	if err := os.WriteFile(filepath.Join(dir, "Pulumi.yaml"), []byte("name: shop\nruntime: go\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	ctx := context.Background()
	made := func(name, project string) {
		t.Helper()
		ws, err := auto.NewLocalWorkspace(ctx, auto.WorkDir(dir))
		if err != nil {
			t.Fatal(err)
		}
		if err := ws.CreateStack(ctx, name); err != nil {
			t.Fatal(err)
		}
		urn := "urn:pulumi:" + name + "::shop::"
		deployment := fmt.Sprintf(`{"manifest":{"time":"2026-10-05T00:00:00Z","magic":"","version":""},"resources":[`+
			`{"urn":"%spulumi:pulumi:Stack::shop-%s","custom":false,"type":"pulumi:pulumi:Stack"},`+
			`{"urn":"%sgcp:firebase/project:Project::firebase","custom":true,"id":"projects/%s","type":"gcp:firebase/project:Project"}]}`,
			urn, name, urn, project)
		if err := ws.ImportStack(ctx, name, apitype.UntypedDeployment{Version: 3, Deployment: json.RawMessage(deployment)}); err != nil {
			t.Fatal(err)
		}
	}
	made("neotrac", "neotrac")
	made("staging", "neotrac-staging")

	if err := adopt(ctx, dir, "production", "neotrac"); err != nil {
		t.Fatal(err)
	}
	ws, _ := auto.NewLocalWorkspace(ctx, auto.WorkDir(dir))
	stacks, err := ws.ListStacks(ctx)
	if err != nil {
		t.Fatal(err)
	}
	var names []string
	for _, s := range stacks {
		names = append(names, s.Name)
	}
	sort.Strings(names)
	if strings.Join(names, " ") != "production staging" {
		t.Errorf("after taking over neotrac's stack, the stacks are %v", names)
	}
	state, err := ws.ExportStack(ctx, "production")
	if err != nil || !manages(state.Deployment, "neotrac") {
		t.Errorf("the new stack doesn't hold what the old one did: %v", err)
	}

	// A stack that's there already is used as it is, and a second claim is refused.
	if err := adopt(ctx, dir, "production", "neotrac"); err != nil {
		t.Errorf("deploying an existing stack again: %v", err)
	}
	made("copy", "neotrac")
	if err := adopt(ctx, dir, "live", "neotrac"); err == nil || !strings.Contains(err.Error(), "can't be told which") {
		t.Errorf("two stacks managing one project gave %v", err)
	}
}

// A project linked to Google Analytics has a measurement ID, which its web app is
// built with to count visitors; one that isn't, or doesn't count them, has none.
func TestTheWebAppIsBuiltWithItsMeasurementIDOnlyWhenThereIsOne(t *testing.T) {
	outputs := map[string]any{"firebase_api_key": "key", "firebase_app_id": "1:2:web:3", "firebase_project_id": "ui-one",
		"firebase_auth_domain": "ui-one.firebaseapp.com"}
	without, err := settings(outputs)
	if err != nil || strings.Contains(without, "MEASUREMENT") {
		t.Errorf("without a measurement ID, the settings are %q, %v", without, err)
	}
	outputs["firebase_measurement_id"] = "G-ABC123"
	with, err := settings(outputs)
	if err != nil || !strings.HasSuffix(with, "VITE_FIREBASE_MEASUREMENT_ID=G-ABC123\n") {
		t.Errorf("with a measurement ID, the settings are %q, %v", with, err)
	}
	outputs["firebase_measurement_id"] = ""
	if empty, _ := settings(outputs); strings.Contains(empty, "MEASUREMENT") {
		t.Errorf("an empty measurement ID is written: %q", empty)
	}
}

// The commit a build is of, for the foot of every page: as the deploy names it, or
// as git says of the folder, or none outside git.
func TestTheCommitIsTheDeploysOrGits(t *testing.T) {
	t.Setenv("UIONE_COMMIT", "d9d95fd0aaaa")
	if got := commitOf(t.TempDir()); got != "d9d95fd0aaaa" {
		t.Errorf("named by the deploy, the commit is %q", got)
	}
	t.Setenv("UIONE_COMMIT", "")
	if got := commitOf(t.TempDir()); got != "" {
		t.Errorf("outside git, the commit is %q", got)
	}
	if got := commitOf("."); len(got) != 40 {
		t.Errorf("in this repository, the commit is %q", got)
	}
}

// Where a build's commits can be read: as the deploy names it, or the folder's
// origin, written as its GitHub page; a remote elsewhere isn't linked.
func TestTheRepositoryIsItsGitHubPage(t *testing.T) {
	for remote, want := range map[string]string{
		"git@github.com:da0x/neotrac.git":       "https://github.com/da0x/neotrac",
		"https://github.com/da0x/neotrac.git":   "https://github.com/da0x/neotrac",
		"https://github.com/da0x/neotrac/":      "https://github.com/da0x/neotrac",
		"ssh://git@github.com/da0x/neotrac.git": "https://github.com/da0x/neotrac",
		"https://gitlab.com/da0x/neotrac.git":   "",
		"https://github.com/da0x":               "",
		"/home/da/neotrac":                      "",
	} {
		if got := webAddress(remote); got != want {
			t.Errorf("%s is %q, not %q", remote, got, want)
		}
	}
	t.Setenv("UIONE_REPOSITORY", "git@github.com:da0x/neotrac.git")
	if got := repositoryOf(t.TempDir()); got != "https://github.com/da0x/neotrac" {
		t.Errorf("named by the deploy, the repository is %q", got)
	}
	t.Setenv("UIONE_REPOSITORY", "")
	if got := repositoryOf(t.TempDir()); got != "" {
		t.Errorf("outside git, the repository is %q", got)
	}
}
