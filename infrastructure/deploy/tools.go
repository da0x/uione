// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package deploy

import (
	"context"
	"fmt"
	"io"
	"net/http"
	"os/exec"
	"path/filepath"

	"github.com/da0x/uione/infrastructure/hosting"
	"github.com/pulumi/pulumi/sdk/v3/go/auto"
	"github.com/pulumi/pulumi/sdk/v3/go/auto/optpreview"
	"github.com/pulumi/pulumi/sdk/v3/go/auto/optup"
	"golang.org/x/oauth2/google"
)

// Real are the tools a deploy uses outside a test: Pulumi's Automation API, the
// commands on this machine, and Firebase Hosting with the Google sign-in that gcloud
// set up (application default credentials).
func Real() Tools {
	return Tools{Program: automation, Run: command, Upload: upload}
}

type stack struct{ s auto.Stack }

func automation(dir, name string) (Program, error) {
	s, err := auto.UpsertStackLocalSource(context.Background(), name, dir)
	if err != nil {
		return nil, fmt.Errorf("opening the stack %s in %s: %w", name, dir, err)
	}
	return stack{s}, nil
}

func (s stack) Preview(ctx context.Context, log io.Writer) (string, error) {
	result, err := s.s.Preview(ctx, optpreview.ProgressStreams(log))
	if err != nil {
		return "", err
	}
	summary := ""
	for kind, count := range result.ChangeSummary {
		summary += fmt.Sprintf("%s: %d\n", kind, count)
	}
	return summary, nil
}

func (s stack) Up(ctx context.Context, log io.Writer) (map[string]any, error) {
	result, err := s.s.Up(ctx, optup.ProgressStreams(log))
	if err != nil {
		return nil, err
	}
	outputs := map[string]any{}
	for name, value := range result.Outputs {
		outputs[name] = value.Value
	}
	return outputs, nil
}

func command(ctx context.Context, dir string, log io.Writer, name string, args ...string) error {
	c := exec.CommandContext(ctx, name, args...)
	c.Dir = dir
	c.Stdout, c.Stderr = log, log
	if err := c.Run(); err != nil {
		return fmt.Errorf("%s in %s: %w", name, dir, err)
	}
	return nil
}

// upload puts the built app on the project's default Hosting site. Calls are billed
// to the project, which Hosting asks of a person's own sign-in.
func upload(ctx context.Context, project, web string, progress func(name, message string)) error {
	settings, err := hosting.ReadSettings(filepath.Join(web, "firebase.json"))
	if err != nil {
		return err
	}
	client, err := google.DefaultClient(ctx, "https://www.googleapis.com/auth/firebase.hosting", "https://www.googleapis.com/auth/cloud-platform")
	if err != nil {
		return fmt.Errorf("signing in to Google: %w; run gcloud auth application-default login", err)
	}
	client.Transport = billed{project: project, next: client.Transport}
	_, err = hosting.Deploy(ctx, client, hosting.API, project, filepath.Join(web, settings.Public), settings, func(s hosting.Step) {
		progress(s.Name, s.Message)
	})
	return err
}

type billed struct {
	project string
	next    http.RoundTripper
}

func (b billed) RoundTrip(r *http.Request) (*http.Response, error) {
	r = r.Clone(r.Context())
	r.Header.Set("X-Goog-User-Project", b.project)
	return b.next.RoundTrip(r)
}
