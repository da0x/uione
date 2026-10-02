// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// Package hosting puts a built web app on Firebase Hosting through its REST API,
// the way firebase deploy does, without firebase-tools. A deploy is a new version
// of the site with the app's settings, the files Hosting doesn't already have, and
// a release of that version.
package hosting

import (
	"bytes"
	"compress/gzip"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"io/fs"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// API is Firebase Hosting's address. Tests point it at a fake.
const API = "https://firebasehosting.googleapis.com/v1beta1"

// Settings are firebase.json's "hosting" block: which folder holds the app, and its
// headers and rewrites.
type Settings struct {
	Public   string    `json:"public"`
	Headers  []header  `json:"headers"`
	Rewrites []rewrite `json:"rewrites"`
}

type header struct {
	Source  string `json:"source"`
	Headers []struct {
		Key   string `json:"key"`
		Value string `json:"value"`
	} `json:"headers"`
}

type rewrite struct {
	Source      string `json:"source"`
	Destination string `json:"destination,omitempty"`
	Run         *struct {
		ServiceID string `json:"serviceId"`
		Region    string `json:"region"`
	} `json:"run,omitempty"`
}

// ReadSettings reads the hosting block of a firebase.json.
func ReadSettings(path string) (Settings, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return Settings{}, err
	}
	var file struct {
		Hosting Settings `json:"hosting"`
	}
	if err := json.Unmarshal(data, &file); err != nil {
		return Settings{}, fmt.Errorf("hosting: %s isn't firebase.json: %w", path, err)
	}
	if file.Hosting.Public == "" {
		return Settings{}, fmt.Errorf("hosting: %s doesn't say which folder holds the app", path)
	}
	return file.Hosting, nil
}

// config is the settings as a version of a site holds them.
func (s Settings) config() map[string]any {
	var headers, rewrites []map[string]any
	for _, h := range s.Headers {
		values := map[string]string{}
		for _, kv := range h.Headers {
			values[kv.Key] = kv.Value
		}
		headers = append(headers, map[string]any{"glob": h.Source, "headers": values})
	}
	for _, r := range s.Rewrites {
		entry := map[string]any{"glob": r.Source}
		switch {
		case r.Run != nil:
			entry["run"] = map[string]string{"serviceId": r.Run.ServiceID, "region": r.Run.Region}
		default:
			entry["path"] = r.Destination
		}
		rewrites = append(rewrites, entry)
	}
	return map[string]any{"headers": headers, "rewrites": rewrites}
}

// A Step is how far a deploy has got, for showing progress.
type Step struct {
	Name    string `json:"step"`    // version, files, upload, finalize, release
	Message string `json:"message"` // what happened, in a sentence
}

// Deploy uploads the app in dir (firebase.json's public folder) to site with these
// settings, and releases it. client signs its requests, with the scope
// https://www.googleapis.com/auth/firebase.hosting. Each step is reported to progress,
// which may be nil. Returns the released version's name.
func Deploy(ctx context.Context, client *http.Client, api, site, dir string, settings Settings, progress func(Step)) (string, error) {
	report := func(name, format string, args ...any) {
		if progress != nil {
			progress(Step{Name: name, Message: fmt.Sprintf(format, args...)})
		}
	}
	files, err := gather(dir)
	if err != nil {
		return "", err
	}
	if len(files) == 0 {
		return "", fmt.Errorf("hosting: %s holds no files; build the app first", dir)
	}

	var version struct {
		Name string `json:"name"`
	}
	if err := call(ctx, client, http.MethodPost, api+"/sites/"+url.PathEscape(site)+"/versions",
		map[string]any{"config": settings.config()}, &version); err != nil {
		return "", fmt.Errorf("hosting: making a version of %s: %w", site, err)
	}
	report("version", "made %s", version.Name)

	hashes := map[string]string{}
	byHash := map[string]file{}
	for _, f := range files {
		hashes[f.path] = f.hash
		byHash[f.hash] = f
	}
	var populated struct {
		Required  []string `json:"uploadRequiredHashes"`
		UploadURL string   `json:"uploadUrl"`
	}
	if err := call(ctx, client, http.MethodPost, api+"/"+version.Name+":populateFiles",
		map[string]any{"files": hashes}, &populated); err != nil {
		return "", fmt.Errorf("hosting: listing the files: %w", err)
	}
	report("files", "%d files, %d new", len(files), len(populated.Required))

	for i, hash := range populated.Required {
		f, ok := byHash[hash]
		if !ok {
			return "", fmt.Errorf("hosting: asked for a file this deploy doesn't have, %s", hash)
		}
		request, err := http.NewRequestWithContext(ctx, http.MethodPost, populated.UploadURL+"/"+hash, bytes.NewReader(f.gzipped))
		if err != nil {
			return "", err
		}
		request.Header.Set("Content-Type", "application/octet-stream")
		if err := send(client, request, nil); err != nil {
			return "", fmt.Errorf("hosting: uploading %s: %w", f.path, err)
		}
		report("upload", "%d of %d: %s", i+1, len(populated.Required), f.path)
	}

	if err := call(ctx, client, http.MethodPatch, api+"/"+version.Name+"?update_mask=status",
		map[string]any{"status": "FINALIZED"}, nil); err != nil {
		return "", fmt.Errorf("hosting: finishing the version: %w", err)
	}
	report("finalize", "finished %s", version.Name)

	if err := call(ctx, client, http.MethodPost, api+"/sites/"+url.PathEscape(site)+"/releases?versionName="+url.QueryEscape(version.Name),
		map[string]any{}, nil); err != nil {
		return "", fmt.Errorf("hosting: releasing the version: %w", err)
	}
	report("release", "released %s to %s", version.Name, site)
	return version.Name, nil
}

type file struct {
	path    string // as the site serves it, like /index.html
	hash    string // of the gzipped content, as Hosting names it
	gzipped []byte
}

// gather reads every file under dir, gzipped the same way every time, so an
// unchanged file has the same hash and isn't uploaded again.
func gather(dir string) ([]file, error) {
	var files []file
	err := filepath.WalkDir(dir, func(path string, d fs.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return err
		}
		if !d.Type().IsRegular() {
			return nil
		}
		data, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		var packed bytes.Buffer
		zw, _ := gzip.NewWriterLevel(&packed, gzip.BestCompression)
		zw.Write(data)
		zw.Close()
		sum := sha256.Sum256(packed.Bytes())
		rel, _ := filepath.Rel(dir, path)
		files = append(files, file{path: "/" + filepath.ToSlash(rel), hash: hex.EncodeToString(sum[:]), gzipped: packed.Bytes()})
		return nil
	})
	sort.Slice(files, func(i, j int) bool { return files[i].path < files[j].path })
	return files, err
}

func call(ctx context.Context, client *http.Client, method, address string, body, into any) error {
	encoded, err := json.Marshal(body)
	if err != nil {
		return err
	}
	request, err := http.NewRequestWithContext(ctx, method, address, bytes.NewReader(encoded))
	if err != nil {
		return err
	}
	request.Header.Set("Content-Type", "application/json")
	return send(client, request, into)
}

func send(client *http.Client, request *http.Request, into any) error {
	response, err := client.Do(request)
	if err != nil {
		return err
	}
	defer response.Body.Close()
	data, _ := io.ReadAll(io.LimitReader(response.Body, 1<<20))
	if response.StatusCode/100 != 2 {
		return fmt.Errorf("%s %s: %s: %s", request.Method, request.URL.Path, response.Status, strings.TrimSpace(string(data)))
	}
	if into != nil {
		return json.Unmarshal(data, into)
	}
	return nil
}
