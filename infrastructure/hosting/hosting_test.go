// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package hosting

import (
	"bytes"
	"compress/gzip"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"sync"
	"testing"
)

// fake is Firebase Hosting as far as a deploy sees it: it keeps the files it has
// been given, by hash, and checks each one against its hash.
type fake struct {
	mu       sync.Mutex
	stored   map[string][]byte // ungzipped, by hash
	calls    []string
	config   map[string]any
	files    map[string]string
	released string
	t        *testing.T
}

func (f *fake) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	f.mu.Lock()
	defer f.mu.Unlock()
	body, _ := io.ReadAll(r.Body)
	f.calls = append(f.calls, r.Method+" "+r.URL.Path)
	switch {
	case r.Method == http.MethodPost && r.URL.Path == "/sites/ui-one/versions":
		var v struct{ Config map[string]any }
		json.Unmarshal(body, &v)
		f.config = v.Config
		json.NewEncoder(w).Encode(map[string]string{"name": "sites/ui-one/versions/v1", "status": "CREATED"})
	case r.Method == http.MethodPost && r.URL.Path == "/sites/ui-one/versions/v1:populateFiles":
		var v struct{ Files map[string]string }
		json.Unmarshal(body, &v)
		f.files = v.Files
		var required []string
		for _, hash := range v.Files {
			if _, ok := f.stored[hash]; !ok {
				required = append(required, hash)
			}
		}
		json.NewEncoder(w).Encode(map[string]any{"uploadRequiredHashes": required, "uploadUrl": "http://" + r.Host + "/upload"})
	case r.Method == http.MethodPost && strings.HasPrefix(r.URL.Path, "/upload/"):
		hash := strings.TrimPrefix(r.URL.Path, "/upload/")
		sum := sha256.Sum256(body)
		if hex.EncodeToString(sum[:]) != hash {
			http.Error(w, "the file isn't what its hash says", http.StatusBadRequest)
			return
		}
		zr, err := gzip.NewReader(bytes.NewReader(body))
		if err != nil {
			http.Error(w, "not gzipped", http.StatusBadRequest)
			return
		}
		plain, _ := io.ReadAll(zr)
		f.stored[hash] = plain
	case r.Method == http.MethodPatch && r.URL.Path == "/sites/ui-one/versions/v1":
		if r.URL.Query().Get("update_mask") != "status" || !strings.Contains(string(body), "FINALIZED") {
			http.Error(w, "only the status can be changed", http.StatusBadRequest)
		}
	case r.Method == http.MethodPost && r.URL.Path == "/sites/ui-one/releases":
		f.released = r.URL.Query().Get("versionName")
	default:
		http.Error(w, "unexpected", http.StatusNotFound)
	}
}

func app(t *testing.T) string {
	t.Helper()
	dir := t.TempDir()
	for path, text := range map[string]string{
		"index.html":          "<!doctype html><div id=root></div>",
		"assets/app-1a2b.js":  "console.log('uione')",
		"assets/app-1a2b.css": "body{margin:0}",
	} {
		full := filepath.Join(dir, path)
		os.MkdirAll(filepath.Dir(full), 0o755)
		os.WriteFile(full, []byte(text), 0o644)
	}
	return dir
}

func settings(t *testing.T) Settings {
	t.Helper()
	path := filepath.Join(t.TempDir(), "firebase.json")
	os.WriteFile(path, []byte(`{"hosting": {"public": "dist",
		"headers": [{"source": "/assets/**", "headers": [{"key": "Cache-Control", "value": "immutable"}]}],
		"rewrites": [{"source": "/api/**", "run": {"serviceId": "api", "region": "us-east4"}}, {"source": "**", "destination": "/index.html"}]}}`), 0o644)
	s, err := ReadSettings(path)
	if err != nil {
		t.Fatal(err)
	}
	return s
}

func TestADeployMakesAVersionUploadsItsFilesAndReleasesIt(t *testing.T) {
	f := &fake{stored: map[string][]byte{}, t: t}
	server := httptest.NewServer(f)
	defer server.Close()
	var steps []string
	version, err := Deploy(context.Background(), server.Client(), server.URL, "ui-one", app(t), settings(t), func(s Step) { steps = append(steps, s.Name) })
	if err != nil {
		t.Fatal(err)
	}
	if version != "sites/ui-one/versions/v1" || f.released != version {
		t.Errorf("released %q, deployed %q", f.released, version)
	}
	if want := []string{"version", "files", "upload", "upload", "upload", "finalize", "release"}; !reflect.DeepEqual(steps, want) {
		t.Errorf("the steps were %v", steps)
	}
	paths := []string{}
	for path := range f.files {
		paths = append(paths, path)
	}
	if len(paths) != 3 || f.files["/index.html"] == "" {
		t.Errorf("the version lists %v", f.files)
	}
	for hash, plain := range f.stored {
		if string(plain) == "<!doctype html><div id=root></div>" && f.files["/index.html"] != hash {
			t.Errorf("index.html was stored under another hash")
		}
	}
	rewrites, _ := json.Marshal(f.config["rewrites"])
	headers, _ := json.Marshal(f.config["headers"])
	if string(rewrites) != `[{"glob":"/api/**","run":{"region":"us-east4","serviceId":"api"}},{"glob":"**","path":"/index.html"}]` {
		t.Errorf("the rewrites are %s", rewrites)
	}
	if string(headers) != `[{"glob":"/assets/**","headers":{"Cache-Control":"immutable"}}]` {
		t.Errorf("the headers are %s", headers)
	}
}

func TestAFileHostingAlreadyHasIsntUploadedAgain(t *testing.T) {
	f := &fake{stored: map[string][]byte{}, t: t}
	server := httptest.NewServer(f)
	defer server.Close()
	dir := app(t)
	if _, err := Deploy(context.Background(), server.Client(), server.URL, "ui-one", dir, settings(t), nil); err != nil {
		t.Fatal(err)
	}
	os.WriteFile(filepath.Join(dir, "index.html"), []byte("<!doctype html><p>changed"), 0o644)
	f.calls = nil
	var uploads int
	if _, err := Deploy(context.Background(), server.Client(), server.URL, "ui-one", dir, settings(t), func(s Step) {
		if s.Name == "upload" {
			uploads++
		}
	}); err != nil {
		t.Fatal(err)
	}
	if uploads != 1 {
		t.Errorf("a deploy with one changed file uploaded %d", uploads)
	}
}

func TestAnErrorFromHostingStopsTheDeployAndSaysWhere(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, `{"error":{"message":"The caller does not have permission"}}`, http.StatusForbidden)
	}))
	defer server.Close()
	_, err := Deploy(context.Background(), server.Client(), server.URL, "ui-one", app(t), settings(t), nil)
	if err == nil || !strings.Contains(err.Error(), "making a version of ui-one") || !strings.Contains(err.Error(), "does not have permission") {
		t.Errorf("the deploy said %v", err)
	}
	if _, err := Deploy(context.Background(), server.Client(), server.URL, "ui-one", t.TempDir(), settings(t), nil); err == nil || !strings.Contains(err.Error(), "build the app first") {
		t.Errorf("an empty folder said %v", err)
	}
}

func TestADomainsRecordsAreReadFromHosting(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/projects/uione-cloud/sites/uione-cloud/customDomains/studio.uione.io" {
			http.NotFound(w, r)
			return
		}
		w.Write([]byte(`{"requiredDnsUpdates":{"desired":[{"records":[
			{"domainName":"studio.uione.io","type":"CNAME","rdata":"uione-cloud.web.app","requiredAction":"ADD"},
			{"domainName":"studio.uione.io","type":"TXT","rdata":"kept","requiredAction":"NONE"}]}]}}`))
	}))
	defer server.Close()
	got, err := Records(context.Background(), server.Client(), server.URL, "uione-cloud", "uione-cloud", "studio.uione.io")
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 1 || got[0] != "add CNAME studio.uione.io uione-cloud.web.app" {
		t.Errorf("the domain needs %v", got)
	}
}

func TestARedirectReachesHostingAsAMovedAddress(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "firebase.json")
	os.WriteFile(path, []byte(`{"hosting":{"public":"dist","redirects":[{"source":"/install.sh","destination":"https://www.uione.io/install.sh","type":301}],"rewrites":[{"source":"**","destination":"/index.html"}]}}`), 0o644)
	settings, err := ReadSettings(path)
	if err != nil {
		t.Fatal(err)
	}
	got, _ := json.Marshal(settings.config()["redirects"])
	if string(got) != `[{"glob":"/install.sh","location":"https://www.uione.io/install.sh","statusCode":301}]` {
		t.Errorf("Hosting is sent %s", got)
	}
	settings.Redirects = nil
	if _, has := settings.config()["redirects"]; has {
		t.Error("a site without redirects sends an empty list of them")
	}
}
