// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package infrastructure

import (
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	"github.com/pulumi/pulumi-gcp/sdk/v9/go/gcp/firebase"
	"github.com/pulumi/pulumi/sdk/v3/go/common/resource"
	"github.com/pulumi/pulumi/sdk/v3/go/pulumi"
)

// mocks stands in for Google Cloud: it records every resource declared, and answers
// the two lookups the library makes.
type mocks struct {
	mu        sync.Mutex
	billing   string                          // the billing account the project is linked to
	resources map[string]resource.PropertyMap // by type and name, like gcp:firestore/database:Database::database
	imports   map[string]string               // the ID each imported resource was imported from
	waits     map[string][]string             // the resources each one waits for, by URN
}

func (m *mocks) NewResource(args pulumi.MockResourceArgs) (string, resource.PropertyMap, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	key := args.TypeToken + "::" + args.Name
	m.resources[key] = args.Inputs
	if args.RegisterRPC != nil {
		if args.RegisterRPC.ImportId != "" {
			m.imports[key] = args.RegisterRPC.ImportId
		}
		m.waits[key] = args.RegisterRPC.Dependencies
	}
	outputs := args.Inputs.Copy()
	switch args.TypeToken {
	case "gcp:serviceaccount/account:Account":
		outputs["email"] = resource.NewStringProperty(args.Inputs["accountId"].StringValue() + "@ui-one.iam.gserviceaccount.com")
	case "gcp:firebase/webApp:WebApp":
		outputs["appId"] = resource.NewStringProperty("1:783135676682:web:abc")
	case "gcp:firebaserules/ruleset:Ruleset":
		outputs["name"] = resource.NewStringProperty("r1")
	case "command:local:Command":
		outputs["stdout"] = resource.NewStringProperty("built")
	case "random:index/randomPassword:RandomPassword":
		outputs["result"] = resource.MakeSecret(resource.NewStringProperty("generated-secret"))
	case "gcp:secretmanager/secretVersion:SecretVersion":
		outputs["version"] = resource.NewStringProperty("1")
	}
	return args.Name + "-id", outputs, nil
}

func (m *mocks) Call(args pulumi.MockCallArgs) (resource.PropertyMap, error) {
	switch args.Token {
	case "gcp:firebase/getWebAppConfig:getWebAppConfig":
		return resource.PropertyMap{"apiKey": resource.NewStringProperty("key")}, nil
	case "gcp:organizations/getProject:getProject":
		return resource.PropertyMap{
			"number":         resource.NewStringProperty("783135676682"),
			"billingAccount": resource.NewStringProperty(m.billing),
		}, nil
	}
	return resource.PropertyMap{}, nil
}

// build lays out what one build writes: the rules, and a backend whose go.mod points
// at the one library.
func build(t *testing.T) string {
	t.Helper()
	dir := t.TempDir()
	write := func(path, text string) {
		full := filepath.Join(dir, path)
		if err := os.MkdirAll(filepath.Dir(full), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(full, []byte(text), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write("build/firestore.rules", "rules_version = '2';\n")
	write("build/api/go.mod", "module uione.io/api\n\nreplace github.com/da0x/uione/one => ../../one\n")
	write("build/api/main.go", "package main\n")
	write("one/one.go", "package one\n")
	return dir
}

func declare(t *testing.T, dir string, settings string) (*mocks, error) {
	return declareBilled(t, dir, settings, "000000-000000-000000")
}

func declareBilled(t *testing.T, dir string, settings string, billing string) (*mocks, error) {
	return declareProject(t, dir, settings, billing, Project{})
}

func declareProject(t *testing.T, dir string, settings string, billing string, extra Project) (*mocks, error) {
	t.Helper()
	t.Setenv("PULUMI_CONFIG", settings)
	m := &mocks{billing: billing, resources: map[string]resource.PropertyMap{}, imports: map[string]string{}, waits: map[string][]string{}}
	err := pulumi.RunErr(func(ctx *pulumi.Context) error {
		p := Project{Name: "uione", Domain: "uione.io", Firebase: "ui-one", Region: "us-east4", Build: filepath.Join(dir, "build")}
		p.GitHub = extra.GitHub
		return Declare(ctx, p)
	}, pulumi.WithMocks("uione", "prod", m))
	return m, err
}

const settings = `{}`

func (m *mocks) get(t *testing.T, key string) resource.PropertyMap {
	t.Helper()
	r, ok := m.resources[key]
	if !ok {
		t.Fatalf("nothing declared as %s", key)
	}
	return r
}

func TestTheDatabaseIsInTheProjectsRegionAndCantBeDeletedByAccident(t *testing.T) {
	m, err := declare(t, build(t), settings)
	if err != nil {
		t.Fatal(err)
	}
	db := m.get(t, "gcp:firestore/database:Database::database")
	if got := db["locationId"].StringValue(); got != "us-east4" {
		t.Errorf("the database is in %s", got)
	}
	if got := db["deleteProtectionState"].StringValue(); got != "DELETE_PROTECTION_ENABLED" {
		t.Errorf("delete protection is %s", got)
	}
	run := m.get(t, "gcp:cloudrunv2/service:Service::backend")
	if got := run["location"].StringValue(); got != "us-east4" {
		t.Errorf("the backend runs in %s", got)
	}
}

func TestTheRulesDeployedAreTheGeneratedOnes(t *testing.T) {
	dir := build(t)
	m, err := declare(t, dir, settings)
	if err != nil {
		t.Fatal(err)
	}
	files := m.get(t, "gcp:firebaserules/ruleset:Ruleset::rules")["source"].ObjectValue()["files"].ArrayValue()
	if len(files) != 1 || files[0].ObjectValue()["content"].StringValue() != "rules_version = '2';\n" {
		t.Errorf("the ruleset holds %v", files)
	}
	release := m.get(t, "gcp:firebaserules/release:Release::rules")
	if got := release["name"].StringValue(); got != "cloud.firestore" {
		t.Errorf("the rules are released as %s, which isn't Firestore's", got)
	}
	if got := release["rulesetName"].StringValue(); got != "projects/ui-one/rulesets/r1" {
		t.Errorf("the release points at %s, not the ruleset's full name", got)
	}
}

func TestTheBackendCanTouchFirestoreAndNothingElse(t *testing.T) {
	m, err := declare(t, build(t), settings)
	if err != nil {
		t.Fatal(err)
	}
	runtime := "serviceAccount:uione-api@ui-one.iam.gserviceaccount.com"
	var roles []string
	for key, r := range m.resources {
		if strings.Contains(strings.ToLower(key), "iammember") && r["member"].StringValue() == runtime {
			roles = append(roles, r["role"].StringValue())
		}
	}
	if len(roles) != 1 || roles[0] != "roles/datastore.user" {
		t.Errorf("the backend's account has %v", roles)
	}
	template := m.get(t, "gcp:cloudrunv2/service:Service::backend")["template"].ObjectValue()
	if got := template["serviceAccount"].StringValue(); "serviceAccount:"+got != runtime {
		t.Errorf("the backend runs as %s", got)
	}
	if got := template["scaling"].ObjectValue()["maxInstanceCount"].NumberValue(); got != 2 {
		t.Errorf("the backend scales to %v instances", got)
	}
}

func TestTheBuildUsesItsOwnUploadBucketAndAccount(t *testing.T) {
	m, err := declare(t, build(t), settings)
	if err != nil {
		t.Fatal(err)
	}
	bucket := m.get(t, "gcp:storage/bucket:Bucket::build-uploads")
	if got := bucket["name"].StringValue(); got != "ui-one_cloudbuild" {
		t.Errorf("uploads go to %s, so gcloud would create a bucket of its own", got)
	}
	env := m.get(t, "command:local:Command::image")["environment"].ObjectValue()
	if got := env["BUILDER"].StringValue(); got != "uione-build@ui-one.iam.gserviceaccount.com" {
		t.Errorf("the build runs as %s", got)
	}
	if got := env["IMAGE"].StringValue(); !strings.HasPrefix(got, "us-east4-docker.pkg.dev/ui-one/uione/api:") {
		t.Errorf("the image is %s", got)
	}
}

func TestTheImageIsRebuiltExactlyWhenItsSourceChanges(t *testing.T) {
	dir := build(t)
	api, one := filepath.Join(dir, "build/api"), filepath.Join(dir, "one")
	first, err := hashTrees(api, one)
	if err != nil {
		t.Fatal(err)
	}
	again, _ := hashTrees(api, one)
	if first != again {
		t.Error("the same source hashed differently")
	}
	must := func(err error) {
		if err != nil {
			t.Fatal(err)
		}
	}
	must(os.WriteFile(filepath.Join(one, "one.go"), []byte("package one // changed\n"), 0o644))
	changed, _ := hashTrees(api, one)
	if changed == first {
		t.Error("a change to the library didn't change the hash")
	}
	must(os.WriteFile(filepath.Join(one, "one.go"), []byte("package one\n"), 0o644))
	must(os.Rename(filepath.Join(api, "main.go"), filepath.Join(api, "other.go")))
	renamed, _ := hashTrees(api, one)
	if renamed == first {
		t.Error("renaming a file didn't change the hash")
	}
}

func TestTheLibraryIsFoundAtAnAbsolutePathToo(t *testing.T) {
	dir := build(t)
	gomod := "module uione.io/api\n\nreplace github.com/da0x/uione/one => " + filepath.Join(dir, "one") + "\n"
	if err := os.WriteFile(filepath.Join(dir, "build/api/go.mod"), []byte(gomod), 0o644); err != nil {
		t.Fatal(err)
	}
	got, err := library(filepath.Join(dir, "build/api"))
	if err != nil || got != filepath.Join(dir, "one") {
		t.Errorf("found the library at %s (%v)", got, err)
	}
}

func TestTheLibraryIsFoundFromTheBackendsGoMod(t *testing.T) {
	dir := build(t)
	got, err := library(filepath.Join(dir, "build/api"))
	if err != nil {
		t.Fatal(err)
	}
	if got != filepath.Join(dir, "one") {
		t.Errorf("found the library at %s", got)
	}
}

func TestABudgetWatchesTheProject(t *testing.T) {
	m, err := declare(t, build(t), `{"uione:budget": "25"}`)
	if err != nil {
		t.Fatal(err)
	}
	budget := m.get(t, "gcp:billing/budget:Budget::budget")
	if got := budget["amount"].ObjectValue()["specifiedAmount"].ObjectValue()["units"].StringValue(); got != "25" {
		t.Errorf("the budget is %s", got)
	}
	if got := budget["billingAccount"].StringValue(); got != "000000-000000-000000" {
		t.Errorf("the budget is on %s, not the project's billing account", got)
	}
	projects := budget["budgetFilter"].ObjectValue()["projects"].ArrayValue()
	if len(projects) != 1 || projects[0].StringValue() != "projects/783135676682" {
		t.Errorf("the budget watches %v", projects)
	}
}

func TestADeployWithoutBillingStopsAndSaysWhy(t *testing.T) {
	_, err := declareBilled(t, build(t), settings, "")
	if err == nil || !strings.Contains(err.Error(), "no billing account") {
		t.Errorf("got %v", err)
	}
}

func TestTheDefaultSiteIsImportedNotCreated(t *testing.T) {
	m, err := declare(t, build(t), settings)
	if err != nil {
		t.Fatal(err)
	}
	key := "gcp:firebase/hostingSite:HostingSite::site"
	if got := m.imports[key]; got != "projects/ui-one/sites/ui-one" {
		t.Errorf("the default site is imported from %q", got)
	}
	if got := m.get(t, key)["deletionPolicy"].StringValue(); got != "ABANDON" {
		t.Errorf("destroying the stack would %s the default site", got)
	}
}

func TestNothingTurnsSignInIntoIdentityPlatform(t *testing.T) {
	m, err := declare(t, build(t), settings)
	if err != nil {
		t.Fatal(err)
	}
	for key := range m.resources {
		if strings.HasPrefix(key, "gcp:identityplatform/") {
			t.Errorf("%s would move the project from Firebase Auth to Identity Platform", key)
		}
	}
}

func TestTheImageIsBuiltOnlyOnceItsAccountCanBuild(t *testing.T) {
	m, err := declare(t, build(t), settings)
	if err != nil {
		t.Fatal(err)
	}
	waits := m.waits["command:local:Command::image"]
	for _, grant := range []string{"builder-uploads", "builder-images", "builder-logs"} {
		found := false
		for _, urn := range waits {
			found = found || strings.HasSuffix(urn, "::"+grant)
		}
		if !found {
			t.Errorf("the build doesn't wait for %s; it waits for %v", grant, waits)
		}
	}
}

func TestDNSChangesAreListedPlainly(t *testing.T) {
	s := func(v string) *string { return &v }
	record := func(action, kind, rdata string) firebase.HostingCustomDomainRequiredDnsUpdateDesiredRecord {
		return firebase.HostingCustomDomainRequiredDnsUpdateDesiredRecord{
			RequiredAction: s(action), Type: s(kind), DomainName: s("uione.io"), Rdata: s(rdata),
		}
	}
	got := records([]firebase.HostingCustomDomainRequiredDnsUpdate{{
		Desireds: []firebase.HostingCustomDomainRequiredDnsUpdateDesired{{Records: []firebase.HostingCustomDomainRequiredDnsUpdateDesiredRecord{
			record("ADD", "A", "199.36.158.100"),
			record("DELETE", "A", "198.185.159.144"),
			record("NONE", "TXT", "already there"),
		}}},
	}})
	want := []string{"add A uione.io 199.36.158.100", "remove A uione.io 198.185.159.144"}
	if strings.Join(got, "\n") != strings.Join(want, "\n") {
		t.Errorf("got %q", got)
	}
}

func TestGitHubsSecretIsMadeKeptAndReadableByTheBackendAlone(t *testing.T) {
	m, err := declareProject(t, build(t), settings, "000000-000000-000000", Project{GitHub: "/hooks/github"})
	if err != nil {
		t.Fatal(err)
	}
	m.get(t, "gcp:projects/service:Service::secretmanager.googleapis.com")
	if got := m.get(t, "gcp:secretmanager/secret:Secret::github-webhook-secret")["secretId"].StringValue(); got != "github-webhook-secret" {
		t.Errorf("the secret is called %s", got)
	}
	if got := m.get(t, "random:index/randomPassword:RandomPassword::github-webhook-secret")["length"].NumberValue(); got < 32 {
		t.Errorf("the secret is %v characters long", got)
	}
	access := m.get(t, "gcp:secretmanager/secretIamMember:SecretIamMember::runtime-github-webhook-secret")
	if access["role"].StringValue() != "roles/secretmanager.secretAccessor" || access["member"].StringValue() != "serviceAccount:uione-api@ui-one.iam.gserviceaccount.com" {
		t.Errorf("the secret can be read by %v as %v", access["member"], access["role"])
	}
	container := m.get(t, "gcp:cloudrunv2/service:Service::backend")["template"].ObjectValue()["containers"].ArrayValue()[0].ObjectValue()
	found := false
	for _, env := range container["envs"].ArrayValue() {
		e := env.ObjectValue()
		if e["name"].StringValue() != "GITHUB_WEBHOOK_SECRET" {
			continue
		}
		found = true
		if _, plain := e["value"]; plain {
			t.Errorf("the secret is given to the backend as plain text")
		}
		ref := e["valueSource"].ObjectValue()["secretKeyRef"].ObjectValue()
		if ref["secret"].StringValue() != "github-webhook-secret" {
			t.Errorf("the backend reads the secret from %v", ref)
		}
	}
	if !found {
		t.Errorf("the backend isn't given the secret")
	}
}

func TestWithoutGitHubThereIsNoSecret(t *testing.T) {
	m, err := declare(t, build(t), settings)
	if err != nil {
		t.Fatal(err)
	}
	for key := range m.resources {
		if strings.Contains(key, "secretmanager") || strings.Contains(key, "random:") {
			t.Errorf("a project without GitHub's webhook declares %s", key)
		}
	}
}
