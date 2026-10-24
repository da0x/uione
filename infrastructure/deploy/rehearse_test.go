// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package deploy

import (
	"bytes"
	"context"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strconv"
	"testing"

	"cloud.google.com/go/firestore"
)

// The emulators tools/emulators/run starts, standing in for a project's Firestore.
const (
	storedHost    = "localhost:8080"
	storedProject = "demo-uione"
)

// needs skips the test where what it needs isn't here, and fails it in CI, where it
// always should be, so the real rehearsal is never passed over unseen.
func needs(t *testing.T, missing string) {
	t.Helper()
	if os.Getenv("CI") != "" {
		t.Fatalf("%s, which CI has to have", missing)
	}
	t.Skipf("%s", missing)
}

// The real rehearsal, on the tasks example's backend as one build wrote it: what's
// stored is copied from the emulators into the rehearsal's own, and the backend
// starts there with exactly what a deploy gives it, so anything it asks for as it
// starts that the rehearsal doesn't provide fails here and not on a deploy.
func TestTheTasksBackendStartsInARealRehearsal(t *testing.T) {
	api, err := filepath.Abs("../../examples/tasks/build/api")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(api, "go.mod")); err != nil {
		needs(t, "the tasks example isn't built; compiler/build/one build examples/tasks")
	}
	if exec.Command("docker", "info").Run() != nil {
		needs(t, "Docker isn't running")
	}
	if response, err := http.Get("http://" + storedHost); err != nil {
		needs(t, "the Firebase emulators aren't running; start them with tools/emulators/run")
	} else {
		response.Body.Close()
	}

	// As a deploy does before it rehearses.
	tidy := exec.Command("go", "mod", "tidy")
	tidy.Dir = api
	if out, err := tidy.CombinedOutput(); err != nil {
		t.Fatalf("go mod tidy: %v\n%s", err, out)
	}

	ctx := context.Background()
	stored, err := emulated(ctx, storedProject, storedHost)
	if err != nil {
		t.Fatal(err)
	}
	defer stored.Close()
	seeded := stored.Collection("rehearsal_seed").Doc("one")
	if _, err := seeded.Set(ctx, map[string]any{"title": "copied into the rehearsal"}); err != nil {
		t.Fatal(err)
	}
	defer seeded.Delete(ctx)

	was := storedIn
	storedIn = func(ctx context.Context, project string) (*firestore.Client, error) {
		return emulated(ctx, project, storedHost)
	}
	defer func() { storedIn = was }()
	// What the deploy itself reads; the rehearsal refuses to run with it set.
	t.Setenv("FIRESTORE_EMULATOR_HOST", "")

	var log bytes.Buffer
	if err := rehearse(ctx, api, storedProject, &log); err != nil {
		t.Fatalf("the rehearsal failed: %v\n%s", err, log.String())
	}
	copied := regexp.MustCompile(`rehearsal: (\d+) records of ` + storedProject + ` copied`).FindStringSubmatch(log.String())
	if copied == nil {
		t.Fatalf("the rehearsal didn't say what it copied:\n%s", log.String())
	}
	if n, _ := strconv.Atoi(copied[1]); n < 1 {
		t.Errorf("the rehearsal copied %d records, not the one seeded", n)
	}
	if !regexp.MustCompile(`rehearsal: .*one: serving on`).MatchString(log.String()) {
		t.Errorf("the backend didn't say it was serving:\n%s", log.String())
	}
}
