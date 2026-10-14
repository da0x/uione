// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

package deploy

import (
	"bufio"
	"context"
	"crypto/sha256"
	"embed"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"slices"
	"strconv"
	"strings"
	"sync"
	"time"

	"cloud.google.com/go/firestore"
	"google.golang.org/api/iterator"
	"google.golang.org/api/option"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

// The emulator a rehearsal runs on, built once into an image named for what it's
// built from.
//
//go:embed rehearsal/dockerfile rehearsal/firebase.json
var rehearsalFiles embed.FS

// rehearsalProject is the emulator's project: a demo- one never touches a real one.
const rehearsalProject = "demo-rehearsal"

// mostCopied is how many records of a collection a rehearsal copies at most.
const mostCopied = 20000

// rehearse builds the backend for this machine, copies what the project stores into
// a Firestore emulator of its own, and starts the backend there once, as Cloud Run
// is about to: every once, backfill and view it builds as it starts runs against
// what's really stored, and nothing is written anywhere but the emulator, which goes
// when it's done. The views are left out of the copy, since the backend builds them.
func rehearse(ctx context.Context, api, project string, log io.Writer) error {
	if os.Getenv("FIRESTORE_EMULATOR_HOST") != "" {
		return fmt.Errorf("%w: FIRESTORE_EMULATOR_HOST is set, so what's stored couldn't be read", ErrNoRehearsal)
	}
	if _, err := exec.LookPath("docker"); err != nil {
		return fmt.Errorf("%w: there's no Docker to run the emulator in", ErrNoRehearsal)
	}
	if err := exec.CommandContext(ctx, "docker", "info").Run(); err != nil {
		return fmt.Errorf("%w: Docker isn't running", ErrNoRehearsal)
	}
	work, err := os.MkdirTemp("", "uione-rehearsal-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(work)

	backend := filepath.Join(work, "backend")
	build := exec.CommandContext(ctx, "go", "build", "-o", backend, ".")
	build.Dir, build.Stdout, build.Stderr = api, log, log
	if err := build.Run(); err != nil {
		return fmt.Errorf("building the backend: %w", err)
	}
	host, stop, err := emulator(ctx, work, log)
	if err != nil {
		return err
	}
	defer stop()
	copied, err := copyStored(ctx, project, host)
	if err != nil {
		return fmt.Errorf("copying what %s stores: %w", project, err)
	}
	fmt.Fprintf(log, "rehearsal: %d records of %s copied to the emulator\n", copied, project)
	return startOnce(ctx, backend, host, log)
}

// emulator starts a Firestore emulator in Docker, building its image the first time,
// and returns where it answers and how to stop it.
func emulator(ctx context.Context, work string, log io.Writer) (string, func(), error) {
	none := func() {}
	dir := filepath.Join(work, "emulator")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", none, err
	}
	sum := sha256.New()
	for _, name := range []string{"dockerfile", "firebase.json"} {
		data, err := rehearsalFiles.ReadFile("rehearsal/" + name)
		if err != nil {
			return "", none, err
		}
		sum.Write(data)
		if err := os.WriteFile(filepath.Join(dir, name), data, 0o644); err != nil {
			return "", none, err
		}
	}
	image := "uione-rehearsal:" + hex.EncodeToString(sum.Sum(nil))[:12]
	if exec.CommandContext(ctx, "docker", "image", "inspect", image).Run() != nil {
		fmt.Fprintf(log, "rehearsal: building the emulator's image, once\n")
		build := exec.CommandContext(ctx, "docker", "build", "--quiet", "--tag", image, dir)
		build.Stdout, build.Stderr = log, log
		if err := build.Run(); err != nil {
			return "", none, fmt.Errorf("building the emulator's image: %w", err)
		}
	}
	out, err := exec.CommandContext(ctx, "docker", "run", "--detach", "--rm", "--publish", "127.0.0.1::8080", image).Output()
	if err != nil {
		return "", none, fmt.Errorf("starting the emulator: %w", err)
	}
	id := strings.TrimSpace(string(out))
	stop := func() { exec.Command("docker", "rm", "--force", id).Run() }
	out, err = exec.CommandContext(ctx, "docker", "port", id, "8080/tcp").Output()
	if err != nil {
		stop()
		return "", none, fmt.Errorf("finding the emulator's port: %w", err)
	}
	host := strings.TrimSpace(strings.Split(string(out), "\n")[0])
	for range 120 {
		if response, err := http.Get("http://" + host); err == nil {
			response.Body.Close()
			return host, stop, nil
		}
		select {
		case <-ctx.Done():
			stop()
			return "", none, ctx.Err()
		case <-time.After(500 * time.Millisecond):
		}
	}
	stop()
	return "", none, errors.New("the emulator didn't start in a minute")
}

// owner is what the emulator takes from the Admin SDK: a caller its rules don't stop.
type owner struct{}

func (owner) GetRequestMetadata(context.Context, ...string) (map[string]string, error) {
	return map[string]string{"authorization": "Bearer owner"}, nil
}
func (owner) RequireTransportSecurity() bool { return false }

// copyStored copies every collection the project stores but its views, each up to
// mostCopied records, into the emulator, and says how many records it copied.
func copyStored(ctx context.Context, project, host string) (int, error) {
	source, err := firestore.NewClient(ctx, project)
	if err != nil {
		return 0, err
	}
	defer source.Close()
	target, err := firestore.NewClient(ctx, rehearsalProject,
		option.WithEndpoint(host),
		option.WithoutAuthentication(),
		option.WithGRPCDialOption(grpc.WithTransportCredentials(insecure.NewCredentials())),
		option.WithGRPCDialOption(grpc.WithPerRPCCredentials(owner{})))
	if err != nil {
		return 0, err
	}
	defer target.Close()
	writer := target.BulkWriter(ctx)
	copied := 0
	collections := source.Collections(ctx)
	for {
		collection, err := collections.Next()
		if errors.Is(err, iterator.Done) {
			break
		}
		if err != nil {
			return copied, err
		}
		if collection.ID == "views" {
			continue
		}
		docs := collection.Limit(mostCopied).Documents(ctx)
		for {
			doc, err := docs.Next()
			if errors.Is(err, iterator.Done) {
				break
			}
			if err != nil {
				return copied, err
			}
			if _, err := writer.Set(target.Collection(collection.ID).Doc(doc.Ref.ID), doc.Data()); err != nil {
				return copied, err
			}
			copied++
		}
	}
	writer.End()
	return copied, nil
}

// startOnce starts the backend on the emulator and waits until it serves, or stops.
// Stopped, it says what the backend said last, which says why.
func startOnce(ctx context.Context, backend, host string, log io.Writer) error {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return err
	}
	port := strconv.Itoa(listener.Addr().(*net.TCPAddr).Port)
	listener.Close()
	ctx, cancel := context.WithTimeout(ctx, 10*time.Minute)
	defer cancel()
	run := exec.CommandContext(ctx, backend)
	// Only the emulators, and nothing of this machine's that might reach a real
	// project's.
	run.Env = []string{
		"PATH=" + os.Getenv("PATH"), "HOME=" + os.Getenv("HOME"), "PORT=" + port,
		"GOOGLE_CLOUD_PROJECT=" + rehearsalProject, "FIRESTORE_EMULATOR_HOST=" + host,
		"FIREBASE_AUTH_EMULATOR_HOST=127.0.0.1:9", "STORAGE_EMULATOR_HOST=127.0.0.1:9",
	}
	output, err := run.StdoutPipe()
	if err != nil {
		return err
	}
	run.Stderr = run.Stdout
	if err := run.Start(); err != nil {
		return err
	}
	var mu sync.Mutex
	var said []string
	serving := make(chan struct{})
	go func() {
		lines := bufio.NewScanner(output)
		for lines.Scan() {
			line := lines.Text()
			fmt.Fprintln(log, "rehearsal: "+line)
			mu.Lock()
			said = append(said, line)
			if len(said) > 20 {
				said = said[1:]
			}
			mu.Unlock()
			if strings.Contains(line, "one: serving on") {
				close(serving)
				return
			}
		}
	}()
	ended := make(chan error, 1)
	go func() { ended <- run.Wait() }()
	select {
	case <-serving:
		run.Process.Kill()
		<-ended
		return nil
	case err := <-ended:
		mu.Lock()
		defer mu.Unlock()
		if ctx.Err() != nil {
			return errors.New("the backend didn't start in 10 minutes on a copy of what's stored")
		}
		return fmt.Errorf("the backend didn't start on a copy of what's stored (%v); it said:\n%s", err, strings.Join(said, "\n"))
	}
}

// revisionNamed finds the Cloud Run revision a failure's log address names.
var revisionNamed = regexp.MustCompile(`revision_name/([a-z0-9-]+)`)

// explain reads why a revision Cloud Run turned down didn't start, from its log:
// what the backend said, without Cloud Run's own lines about probes and instances.
// Its log can take a few seconds to arrive, so it's read a few times.
func explain(ctx context.Context, project, failure string) string {
	match := revisionNamed.FindStringSubmatch(failure)
	if match == nil {
		return ""
	}
	filter := fmt.Sprintf(`resource.type="cloud_run_revision" AND resource.labels.revision_name="%s"`, match[1])
	for attempt := range 4 {
		if attempt > 0 {
			select {
			case <-ctx.Done():
				return ""
			case <-time.After(5 * time.Second):
			}
		}
		out, err := exec.CommandContext(ctx, "gcloud", "logging", "read", filter, "--project", project,
			"--limit", "30", "--freshness", "1h", "--format", "value(textPayload)").Output()
		if err != nil {
			return ""
		}
		var said []string
		for _, line := range strings.Split(string(out), "\n") {
			line = strings.TrimSpace(line)
			if line == "" || cloudRunSays(line) {
				continue
			}
			said = append(said, "  "+line)
		}
		if len(said) > 0 {
			slices.Reverse(said) // newest first, as it's read
			return "The backend's log says:\n" + strings.Join(said, "\n")
		}
	}
	return ""
}

// cloudRunSays is whether a line of a revision's log is Cloud Run's own.
func cloudRunSays(line string) bool {
	for _, its := range []string{"probe failed", "Starting new instance", "Connection failed", "Container called exit", "Shutting down"} {
		if strings.Contains(line, its) {
			return true
		}
	}
	return false
}
