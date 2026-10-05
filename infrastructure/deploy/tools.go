// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package deploy

import (
	"context"
	"encoding/json"
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
	return Tools{Program: automation, Run: command, Upload: upload, Records: domainRecords}
}

type stack struct{ s auto.Stack }

func automation(dir, name, project string) (Program, error) {
	if err := adopt(context.Background(), dir, name, project); err != nil {
		return nil, err
	}
	s, err := auto.UpsertStackLocalSource(context.Background(), name, dir)
	if err != nil {
		return nil, fmt.Errorf("opening the stack %s in %s: %w", name, dir, err)
	}
	return stack{s}, nil
}

// adopt gives the stack its name when it's new but another stack already manages
// the same Google Cloud project, as one does once its environment is renamed: that
// stack is renamed to it, so the deploy carries on with everything it made, rather
// than making it all again. Two that manage the project can't be told apart, so
// that's refused, naming them.
func adopt(ctx context.Context, dir, name, project string) error {
	if project == "" {
		return nil
	}
	ws, err := auto.NewLocalWorkspace(ctx, auto.WorkDir(dir))
	if err != nil {
		return fmt.Errorf("opening the Pulumi project in %s: %w", dir, err)
	}
	stacks, err := ws.ListStacks(ctx)
	if err != nil {
		return fmt.Errorf("listing the stacks: %w", err)
	}
	var managing []string
	for _, s := range stacks {
		if s.Name == name {
			return nil // it's there already
		}
		state, err := ws.ExportStack(ctx, s.Name)
		if err != nil {
			return fmt.Errorf("reading the stack %s: %w", s.Name, err)
		}
		if manages(state.Deployment, project) {
			managing = append(managing, s.Name)
		}
	}
	switch len(managing) {
	case 0:
		return nil
	case 1:
		// The Automation API's own rename looks for the stack's history under its
		// old name afterwards, and fails, so the CLI does it.
		rename := exec.CommandContext(ctx, "pulumi", "stack", "rename", name, "--stack", managing[0], "--non-interactive")
		rename.Dir = dir
		if out, err := rename.CombinedOutput(); err != nil {
			return fmt.Errorf("renaming the stack %s to %s: %w: %s", managing[0], name, err, out)
		}
		return nil
	default:
		return fmt.Errorf("the stacks %v all manage the Google Cloud project %s, so it can't be told which is %s; "+
			"move what they hold into one with pulumi state move, and remove the others", managing, project, name)
	}
}

// manages says whether a stack's state holds the Firebase project of a Google Cloud
// project, which only the stack that deploys there does.
func manages(deployment json.RawMessage, project string) bool {
	var state struct {
		Resources []struct {
			Type string `json:"type"`
			ID   string `json:"id"`
		} `json:"resources"`
	}
	if err := json.Unmarshal(deployment, &state); err != nil {
		return false
	}
	for _, r := range state.Resources {
		if r.Type == "gcp:firebase/project:Project" && r.ID == "projects/"+project {
			return true
		}
	}
	return false
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

func domainRecords(ctx context.Context, project, domain string) ([]string, error) {
	client, err := google.DefaultClient(ctx, "https://www.googleapis.com/auth/firebase.hosting", "https://www.googleapis.com/auth/cloud-platform")
	if err != nil {
		return nil, err
	}
	client.Transport = billed{project: project, next: client.Transport}
	return hosting.Records(ctx, client, hosting.API, project, project, domain)
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
