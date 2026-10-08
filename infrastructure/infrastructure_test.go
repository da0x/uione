// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package infrastructure

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
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
	hosting   []string                        // the projects whose Hosting API was waited for
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
	return declareChanged(t, dir, settings, billing, func(p *Project) { p.GitHub = extra.GitHub })
}

// declareChanged declares uione's project with change made to its settings first.
func declareChanged(t *testing.T, dir string, settings string, billing string, change func(*Project)) (*mocks, error) {
	return declareIn(t, dir, settings, billing, false, change)
}

// declareIn declares uione's project in a Google Cloud project that has Firebase
// added already, as ui-one does, or that's fresh.
func declareIn(t *testing.T, dir string, settings string, billing string, fresh bool, change func(*Project)) (*mocks, error) {
	t.Helper()
	t.Setenv("PULUMI_CONFIG", settings)
	m := &mocks{billing: billing, resources: map[string]resource.PropertyMap{}, imports: map[string]string{}, waits: map[string][]string{}}
	firebaseAdded = func(context.Context, string) (bool, error) { return !fresh, nil }
	siteReachable = func(context.Context, string) (bool, error) { return !fresh, nil }
	waitForHosting = func(_ context.Context, project string) error {
		m.mu.Lock()
		defer m.mu.Unlock()
		m.hosting = append(m.hosting, project)
		return nil
	}
	err := pulumi.RunErr(func(ctx *pulumi.Context) error {
		p := Project{Name: "uione", Domain: "uione.io", Firebase: "ui-one", Region: "us-east4", Build: filepath.Join(dir, "build")}
		change(&p)
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

func TestTheIndexesDeclaredAreTheGeneratedOnes(t *testing.T) {
	dir := build(t)
	index := `{ "indexes": [
	{ "collection": "projects_issue_history", "fields": [{ "field": "assignees", "array": "CONTAINS" }, { "field": "created_at", "order": "DESCENDING" }] }
] }`
	if err := os.WriteFile(filepath.Join(dir, "build", "firestore.indexes.json"), []byte(index), 0o644); err != nil {
		t.Fatal(err)
	}
	m, err := declare(t, dir, settings)
	if err != nil {
		t.Fatal(err)
	}
	got := m.get(t, "gcp:firestore/index:Index::index-projects_issue_history-assignees-contains-created_at-descending")
	if got["collection"].StringValue() != "projects_issue_history" || got["database"].StringValue() != "(default)" {
		t.Errorf("the index is on %v in %v", got["collection"], got["database"])
	}
	fields := got["fields"].ArrayValue()
	if len(fields) != 2 || fields[0].ObjectValue()["arrayConfig"].StringValue() != "CONTAINS" || fields[1].ObjectValue()["order"].StringValue() != "DESCENDING" {
		t.Errorf("the index's fields are %v", fields)
	}
	if !got["skipWait"].BoolValue() || got["deletionPolicy"].StringValue() != "DELETE" {
		t.Errorf("the index waits %v and is deleted %v", got["skipWait"], got["deletionPolicy"])
	}
}

func TestABuildWithoutIndexesDeclaresNone(t *testing.T) {
	m, err := declare(t, build(t), settings)
	if err != nil {
		t.Fatal(err)
	}
	for name := range m.resources {
		if strings.Contains(name, "firestore/index:Index") {
			t.Errorf("a build without indexes declares %s", name)
		}
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
	// gcloud makes ui-one_cloudbuild itself, so a project it already ran in would
	// have that name taken.
	if got := bucket["name"].StringValue(); got != "ui-one-uione-builds" {
		t.Errorf("uploads go to %s", got)
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
	secret := m.get(t, "gcp:secretmanager/secret:Secret::github-webhook-secret")
	if got := secret["secretId"].StringValue(); got != "github-webhook-secret" {
		t.Errorf("the secret is called %s", got)
	}
	if !secret["deletionProtection"].BoolValue() {
		t.Errorf("removing GitHub from the project would delete the master secret")
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

func TestSettingsGoogleWouldRefuseAreRefusedBeforeAnythingIsDeclared(t *testing.T) {
	for _, c := range []struct {
		change func(*Project)
		says   string
	}{
		{func(p *Project) { p.Name = "" }, "no name"},
		{func(p *Project) { p.Name = "UiOne" }, "lowercase"},
		{func(p *Project) { p.Name = "1uione" }, "starting with a letter"},
		{func(p *Project) { p.Name = "ui_one" }, "lowercase"},
		{func(p *Project) { p.Name = "u" }, "2 to 24"},
		{func(p *Project) { p.Name = strings.Repeat("a", 25) }, "2 to 24"},
		{func(p *Project) { p.Domain = "" }, "no domain"},
		{func(p *Project) { p.Firebase = "" }, "no Firebase project"},
		{func(p *Project) { p.Firebase = "ui1" }, "isn't a Firebase project id"},
		{func(p *Project) { p.Firebase = "Ui-One" }, "isn't a Firebase project id"},
		{func(p *Project) { p.Firebase = "1-ui-one" }, "isn't a Firebase project id"},
		{func(p *Project) { p.Firebase = "ui-one-" }, "isn't a Firebase project id"},
		{func(p *Project) { p.Firebase = strings.Repeat("a", 31) }, "isn't a Firebase project id"},
		{func(p *Project) { p.Region = "" }, "no region"},
		{func(p *Project) { p.GitHub = "hooks/github" }, "has to start with /"},
	} {
		m, err := declareChanged(t, build(t), settings, "000000-000000-000000", c.change)
		if err == nil || !strings.Contains(err.Error(), "infrastructure: ") || !strings.Contains(err.Error(), c.says) {
			t.Errorf("expected an error saying %q, got %v", c.says, err)
		}
		if len(m.resources) != 0 {
			t.Errorf("%d resources were declared from settings that were refused", len(m.resources))
		}
	}
	longest := strings.Repeat("a", 24)
	if _, err := declareChanged(t, build(t), settings, "000000-000000-000000", func(p *Project) {
		p.Name, p.Firebase = longest, "a"+strings.Repeat("-", 28)+"1"
	}); err != nil {
		t.Errorf("the longest settings allowed were refused: %v", err)
	}
}

func TestABudgetIsOnlyEverAWholeAmountAboveZero(t *testing.T) {
	m, err := declare(t, build(t), settings)
	if err != nil {
		t.Fatal(err)
	}
	if got := m.get(t, "gcp:billing/budget:Budget::budget")["amount"].ObjectValue()["specifiedAmount"].ObjectValue()["units"].StringValue(); got != "10" {
		t.Errorf("the budget without a setting is %s", got)
	}
	for budget, says := range map[string]string{
		"ten": "whole number",
		"9.5": "whole number",
		"0":   "more than 0",
		"-5":  "more than 0",
	} {
		_, err := declare(t, build(t), `{"uione:budget": "`+budget+`"}`)
		if err == nil || !strings.Contains(err.Error(), says) {
			t.Errorf("a budget of %s gave %v", budget, err)
		}
	}
}

func TestCloudRunCantBeDeletedByAccident(t *testing.T) {
	m, err := declare(t, build(t), settings)
	if err != nil {
		t.Fatal(err)
	}
	if !m.get(t, "gcp:cloudrunv2/service:Service::backend")["deletionProtection"].BoolValue() {
		t.Error("the backend's service can be deleted by a deploy")
	}
}

func TestTheMasterSecretIsAnOutputUnderItsName(t *testing.T) {
	var outputs map[string]pulumi.Input
	m := &mocks{billing: "000000-000000-000000", resources: map[string]resource.PropertyMap{}, imports: map[string]string{}, waits: map[string][]string{}}
	dir := build(t)
	t.Setenv("PULUMI_CONFIG", settings)
	err := pulumi.RunErr(func(ctx *pulumi.Context) error {
		p := Project{Name: "uione", Domain: "uione.io", Firebase: "ui-one", Region: "us-east4", GitHub: "/hooks/github", Build: filepath.Join(dir, "build")}
		if err := Declare(ctx, p); err != nil {
			return err
		}
		outputs = ctx.GetCurrentExportMap()
		return nil
	}, pulumi.WithMocks("uione", "prod", m))
	if err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"github_webhook_url", "github_webhook_master_secret"} {
		if _, ok := outputs[name]; !ok {
			t.Errorf("there's no %s output", name)
		}
	}
	if _, ok := outputs["github_webhook_secret"]; ok {
		t.Error("the master secret is still exported as github_webhook_secret")
	}
}

func TestAnUnchangedImageIsntBuiltAgainFromAnotherMachine(t *testing.T) {
	m, err := declare(t, build(t), settings)
	if err != nil {
		t.Fatal(err)
	}
	image := m.get(t, "command:local:Command::image")
	update := image["update"].StringValue()
	if !strings.HasPrefix(update, unlessBuilt) || image["create"].StringValue() != buildScript {
		t.Fatalf("the update is %q", update)
	}
	dir := t.TempDir()
	run := func(previous, image string) string {
		command := exec.Command("sh", "-c", unlessBuilt+"echo building\n")
		command.Dir = dir
		command.Env = append(os.Environ(), "PULUMI_COMMAND_STDOUT="+previous, "IMAGE="+image)
		out, err := command.Output()
		if err != nil {
			t.Fatal(err)
		}
		return strings.TrimSpace(string(out))
	}
	if got := run("a log\nregistry/api:abc", "registry/api:abc"); got != "registry/api:abc" {
		t.Errorf("an image that's already built was built again: %q", got)
	}
	if got := run("a log\nregistry/api:abc", "registry/api:def"); got != "building" {
		t.Errorf("a changed image wasn't built: %q", got)
	}
}

// fakeBuild runs the build script with a gcloud that fails the way it's told to,
// the given number of times, and reports how many times it ran and whether the
// build succeeded.
func fakeBuild(t *testing.T, failure string, failures int) (int, bool) {
	t.Helper()
	dir := build(t)
	bin := t.TempDir()
	count := filepath.Join(bin, "count")
	gcloud := "#!/bin/sh\necho x >> " + count + "\n" +
		"if [ $(wc -l < " + count + ") -le " + strconv.Itoa(failures) + " ]; then echo '" + failure + "' >&2; exit 1; fi\n" +
		"echo done\n"
	for name, script := range map[string]string{"gcloud": gcloud, "sleep": "#!/bin/sh\n"} {
		if err := os.WriteFile(filepath.Join(bin, name), []byte(script), 0o755); err != nil {
			t.Fatal(err)
		}
	}
	command := exec.Command("sh", "-c", buildScript)
	command.Env = append(os.Environ(), "PATH="+bin+":"+os.Getenv("PATH"),
		"API="+filepath.Join(dir, "build/api"), "ONE="+filepath.Join(dir, "one"), "IMAGE=registry/api:abc",
		"PROJECT=ui-one", "REGION=us-east4", "UPLOADS=ui-one-uione-builds", "BUILDER=uione-build@ui-one.iam.gserviceaccount.com")
	out, err := command.Output()
	data, _ := os.ReadFile(count)
	runs := strings.Count(string(data), "x")
	if err == nil && !strings.HasSuffix(strings.TrimSpace(string(out)), "registry/api:abc") {
		t.Errorf("the build's output doesn't end with its image: %q", out)
	}
	return runs, err == nil
}

func TestOnlyABuildRefusedForPermissionIsTriedAgain(t *testing.T) {
	runs, built := fakeBuild(t, "ERROR: (gcloud.builds.submit) PERMISSION_DENIED: The caller does not have permission", 2)
	if !built || runs != 3 {
		t.Errorf("a build refused twice for permission ran %d times, built: %v", runs, built)
	}
	runs, built = fakeBuild(t, "ERROR: build step 0 failed: step exited with non-zero status: 1", 1)
	if built || runs != 1 {
		t.Errorf("a build that failed on its own ran %d times, built: %v", runs, built)
	}
}

func TestTheLibraryIsFoundOnlyByItsExactModulePath(t *testing.T) {
	dir := build(t)
	api := filepath.Join(dir, "build/api")
	for gomod, want := range map[string]string{
		"module uione.io/api\n\nreplace (\n\tgithub.com/da0x/uione/oneX => ../../other\n\tgithub.com/da0x/uione/one => ../../one\n)\n": filepath.Join(dir, "one"),
		"module uione.io/api\n\nreplace github.com/da0x/uione/oneX => ../../other\n":                                                   "",
		"module uione.io/api\n\nrequire github.com/da0x/uione/one v0.1.0\n":                                                            "",
		"module uione.io/api\n\nreplace github.com/da0x/uione/one => github.com/someone/one v0.1.0\n":                                  "",
	} {
		if err := os.WriteFile(filepath.Join(api, "go.mod"), []byte(gomod), 0o644); err != nil {
			t.Fatal(err)
		}
		got, err := library(api)
		if err != nil || got != want {
			t.Errorf("from\n%s\nfound the library at %q (%v), not %q", gomod, got, err, want)
		}
	}
}

func TestAPublishedLibraryIsntStaged(t *testing.T) {
	dir := build(t)
	if err := os.WriteFile(filepath.Join(dir, "build/api/go.mod"), []byte("module uione.io/api\n\nrequire github.com/da0x/uione/one v0.1.0\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	m, err := declare(t, dir, settings)
	if err != nil {
		t.Fatal(err)
	}
	if got := m.get(t, "command:local:Command::image")["environment"].ObjectValue()["ONE"].StringValue(); got != "" {
		t.Errorf("the build stages the library from %s", got)
	}
	// The fake gcloud succeeds only if what it's given to build has no one/.
	bin := t.TempDir()
	if err := os.WriteFile(filepath.Join(bin, "gcloud"), []byte("#!/bin/sh\n[ -d \"$3/api\" ] && [ ! -e \"$3/one\" ]\n"), 0o755); err != nil {
		t.Fatal(err)
	}
	command := exec.Command("sh", "-c", buildScript)
	command.Env = append(os.Environ(), "PATH="+bin+":"+os.Getenv("PATH"), "API="+filepath.Join(dir, "build/api"), "ONE=", "IMAGE=i",
		"PROJECT=ui-one", "REGION=us-east4", "UPLOADS=ui-one-uione-builds", "BUILDER=uione-build@ui-one.iam.gserviceaccount.com")
	if out, err := command.CombinedOutput(); err != nil {
		t.Errorf("a build without a library folder failed, or staged one: %v\n%s", err, out)
	}
}

func TestSymbolicLinksAreHashedNotFollowed(t *testing.T) {
	dir := build(t)
	api, one := filepath.Join(dir, "build/api"), filepath.Join(dir, "one")
	before, err := hashTrees(api, one)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(one, filepath.Join(api, "library")); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink("main.go", filepath.Join(api, "alias.go")); err != nil {
		t.Fatal(err)
	}
	linked, err := hashTrees(api, one)
	if err != nil {
		t.Fatalf("links to a folder and a file couldn't be hashed: %v", err)
	}
	if linked == before {
		t.Error("adding links didn't change the hash")
	}
	if err := os.Remove(filepath.Join(api, "alias.go")); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink("other.go", filepath.Join(api, "alias.go")); err != nil {
		t.Fatal(err)
	}
	if moved, _ := hashTrees(api, one); moved == linked {
		t.Error("pointing a link elsewhere didn't change the hash")
	}
	through := filepath.Join(dir, "through")
	if err := os.Symlink(api, through); err != nil {
		t.Fatal(err)
	}
	direct, _ := hashTrees(api, one)
	if got, err := hashTrees(through, one); err != nil || got != direct {
		t.Errorf("the backend reached through a link hashed as %s (%v), not %s", got, err, direct)
	}
}

func TestFirebaseIsAddedToAFreshProjectAndTakenOverWhereItWasAddedAlready(t *testing.T) {
	const key = "gcp:firebase/project:Project::firebase"
	m, err := declareIn(t, build(t), settings, "000000-000000-000000", true, func(*Project) {})
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := m.resources[key]; !ok {
		t.Fatal("a fresh project isn't given Firebase")
	}
	if got, ok := m.imports[key]; ok {
		t.Errorf("a fresh project's Firebase is imported from %s rather than added", got)
	}
	m, err = declareIn(t, build(t), settings, "000000-000000-000000", false, func(*Project) {})
	if err != nil {
		t.Fatal(err)
	}
	if got := m.imports[key]; got != "projects/ui-one" {
		t.Errorf("Firebase added already is imported from %q", got)
	}
	// The database and the web app wait for Firebase.
	for _, waiting := range []string{"gcp:firestore/database:Database::database", "gcp:firebase/webApp:WebApp::web"} {
		if !strings.Contains(strings.Join(m.waits[waiting], " "), "gcp:firebase/project:Project::firebase") {
			t.Errorf("%s doesn't wait for Firebase: %v", waiting, m.waits[waiting])
		}
	}
}

func TestAFreshProjectsPreviewDoesntReadItsSiteBeforeHostingIsOn(t *testing.T) {
	const key = "gcp:firebase/hostingSite:HostingSite::site"
	t.Setenv("PULUMI_DRY_RUN", "true")
	m, err := declareIn(t, build(t), settings, "000000-000000-000000", true, func(*Project) {})
	if err != nil {
		t.Fatal(err)
	}
	if got, ok := m.imports[key]; ok {
		t.Errorf("a fresh project's preview imports its site from %s", got)
	}
	t.Setenv("PULUMI_DRY_RUN", "false")
	m, err = declareIn(t, build(t), settings, "000000-000000-000000", true, func(*Project) {})
	if err != nil {
		t.Fatal(err)
	}
	if got := m.imports[key]; got != "projects/ui-one/sites/ui-one" {
		t.Errorf("a fresh project's deploy imports its site from %q", got)
	}
}

// Turning the Hosting API on returns before it answers, so a fresh project's deploy
// waits for it before importing the site, and its preview, which reads nothing,
// doesn't.
func TestTheSiteWaitsForHostingToAnswer(t *testing.T) {
	t.Setenv("PULUMI_DRY_RUN", "false")
	m, err := declareIn(t, build(t), settings, "000000-000000-000000", true, func(*Project) {})
	if err != nil {
		t.Fatal(err)
	}
	if len(m.hosting) != 1 || m.hosting[0] != "ui-one" {
		t.Errorf("a deploy waits for Firebase Hosting in %v", m.hosting)
	}
	t.Setenv("PULUMI_DRY_RUN", "true")
	m, err = declareIn(t, build(t), settings, "000000-000000-000000", true, func(*Project) {})
	if err != nil {
		t.Fatal(err)
	}
	if len(m.hosting) != 0 {
		t.Errorf("a preview waits for Firebase Hosting in %v", m.hosting)
	}
}
