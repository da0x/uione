// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

// Package infrastructure declares everything a uione project runs on in Google
// Cloud, so a generated Pulumi program is only the project's settings. Every
// resource is declared here: a stack that doesn't describe what's in the project
// would let pulumi preview say "no changes" while things it knows nothing about
// pile up beside it. What a deploy uploads, the backend's image and the web app's
// files, are build artifacts rather than resources, like a compiled binary.
//
// A few things can't be declared, and each project's README names them: linking
// billing, turning on Google sign-in (which creates an OAuth client no resource can
// manage), adding the site's domain to sign-in's authorized domains, and the DNS
// records at the domain's registrar.
//
// A project's first deploy expects a Firebase project without a Firestore database
// yet. Creating the database in the console first, or publishing rules there, makes
// that deploy stop with "already exists". To keep a database that's already there,
// adopt it and its rules release into the stack before the first pulumi up, from the
// Pulumi program's folder, with the Firebase project in place of ui-one:
//
//	pulumi import --protect=false gcp:firestore/database:Database database 'projects/ui-one/databases/(default)'
//	pulumi import --protect=false gcp:firebaserules/release:Release rules projects/ui-one/releases/cloud.firestore
//
// pulumi import prints code to add for each, which isn't needed, since the program
// already declares both; the next pulumi up updates them to match it.
//
// The stack's outputs are what a deploy and its maintainers read from it:
//
//   - firebase_api_key, firebase_app_id, firebase_project_id and
//     firebase_auth_domain: the web app's Firebase settings, which the web build
//     needs.
//   - backend_url: the address Cloud Run serves the backend at. The site calls it
//     through Hosting, at /api/.
//   - dns_records: the changes to make at the domain's registrar, one per line, like
//     "add A uione.io 199.36.158.100".
//   - github_webhook_url, when the project takes GitHub's webhook: the address
//     each project's webhook is pointed at.
//   - github_webhook_master_secret, when the project takes GitHub's webhook: the
//     master secret each project's webhook secret is derived from, as a Pulumi
//     secret. It's never pasted into GitHub itself; each project's maintainers see
//     their own secret in the app.
package infrastructure

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strings"

	"golang.org/x/oauth2/google"

	"github.com/pulumi/pulumi-gcp/sdk/v9/go/gcp/billing"
	"github.com/pulumi/pulumi-gcp/sdk/v9/go/gcp/cloudrunv2"
	"github.com/pulumi/pulumi-gcp/sdk/v9/go/gcp/firebase"
	"github.com/pulumi/pulumi-gcp/sdk/v9/go/gcp/firebaserules"
	"github.com/pulumi/pulumi-gcp/sdk/v9/go/gcp/firestore"
	"github.com/pulumi/pulumi-gcp/sdk/v9/go/gcp/organizations"
	"github.com/pulumi/pulumi-gcp/sdk/v9/go/gcp/projects"
	"github.com/pulumi/pulumi-gcp/sdk/v9/go/gcp/secretmanager"
	"github.com/pulumi/pulumi-gcp/sdk/v9/go/gcp/serviceaccount"
	"github.com/pulumi/pulumi-random/sdk/v4/go/random"
	"github.com/pulumi/pulumi/sdk/v3/go/pulumi"
	"github.com/pulumi/pulumi/sdk/v3/go/pulumi/config"
)

// Project is what a .one project's settings say about where it runs.
type Project struct {
	Name     string // the project's name, like uione
	Domain   string // where the site is served, like uione.io
	Firebase string // the Firebase project, like ui-one
	Region   string // where the backend and the database live, like us-east4

	// Where GitHub's webhook comes in, like /hooks/github, when the project takes
	// it. A master secret is made here, kept in Secret Manager, and given to the
	// backend, which derives each project's webhook secret from it. The master
	// secret is never pasted into GitHub; each project's maintainers see their own
	// secret in the app.
	GitHub string

	// The folder `one build` wrote, holding api/, web/ and firestore.rules. It's the
	// parent of the Pulumi program's folder unless it's set.
	Build string
}

// Service is the name the backend runs under on Cloud Run. Hosting sends /api/** to
// it, so the generated firebase.json uses the same name.
const Service = "api"

// The APIs a uione project uses. They're declared like anything else, so turning
// one on shows up in a preview.
var apis = []string{
	"artifactregistry.googleapis.com",
	"billingbudgets.googleapis.com",
	"cloudbilling.googleapis.com",
	"cloudbuild.googleapis.com",
	"cloudresourcemanager.googleapis.com",
	"firebase.googleapis.com",
	"firebasehosting.googleapis.com",
	"firebaserules.googleapis.com",
	"firestore.googleapis.com",
	"iam.googleapis.com",
	"identitytoolkit.googleapis.com",
	"run.googleapis.com",
	"serviceusage.googleapis.com",
	"storage.googleapis.com",
}

// Deploy is a generated program's whole main.
func Deploy(p Project) {
	pulumi.Run(func(ctx *pulumi.Context) error { return Declare(ctx, p) })
}

// Declare declares a project's resources. The one setting that isn't in the .one
// files is budget, the monthly amount in the billing account's currency, which is 10
// unless the program's config sets it.
func Declare(ctx *pulumi.Context, p Project) error {
	if err := p.check(); err != nil {
		return err
	}
	if p.Build == "" {
		p.Build = ".."
	}
	settings := config.New(ctx, "")
	project := pulumi.String(p.Firebase)
	opt := func(deps ...pulumi.Resource) pulumi.ResourceOrInvokeOption { return pulumi.DependsOn(deps) }

	needed := apis
	if p.GitHub != "" {
		needed = append(append([]string(nil), apis...), "secretmanager.googleapis.com")
	}
	var enabled []pulumi.Resource
	for _, api := range needed {
		s, err := projects.NewService(ctx, api, &projects.ServiceArgs{
			Project:          project,
			Service:          pulumi.String(api),
			DisableOnDestroy: pulumi.Bool(false),
		})
		if err != nil {
			return err
		}
		enabled = append(enabled, s)
	}
	// Firebase itself: added to the project here, or taken over where it was added
	// already, as it was in the console before uione could. It's never removed.
	firebaseOptions := []pulumi.ResourceOption{opt(enabled...), pulumi.RetainOnDelete(true)}
	added, err := firebaseAdded(ctx.Context(), p.Firebase)
	if err != nil {
		return err
	}
	if added {
		firebaseOptions = append(firebaseOptions, pulumi.Import(pulumi.ID("projects/"+p.Firebase)))
	}
	fb, err := firebase.NewProject(ctx, "firebase", &firebase.ProjectArgs{Project: project}, firebaseOptions...)
	if err != nil {
		return err
	}
	after := opt(append(enabled, fb)...)

	// The database, and the rules that decide what a browser may read.
	db, err := firestore.NewDatabase(ctx, "database", &firestore.DatabaseArgs{
		Project:               project,
		Name:                  pulumi.String("(default)"),
		LocationId:            pulumi.String(p.Region),
		Type:                  pulumi.String("FIRESTORE_NATIVE"),
		DeleteProtectionState: pulumi.String("DELETE_PROTECTION_ENABLED"),
		DeletionPolicy:        pulumi.String("ABANDON"),
	}, after)
	if err != nil {
		return err
	}
	rules, err := os.ReadFile(filepath.Join(p.Build, "firestore.rules"))
	if err != nil {
		return fmt.Errorf("infrastructure: the rules aren't there; run one build first: %w", err)
	}
	ruleset, err := firebaserules.NewRuleset(ctx, "rules", &firebaserules.RulesetArgs{
		Project: project,
		Source: &firebaserules.RulesetSourceArgs{
			Files: firebaserules.RulesetSourceFileArray{&firebaserules.RulesetSourceFileArgs{
				Name:    pulumi.String("firestore.rules"),
				Content: pulumi.String(string(rules)),
			}},
		},
	}, opt(db))
	if err != nil {
		return err
	}
	if _, err := firebaserules.NewRelease(ctx, "rules", &firebaserules.ReleaseArgs{
		Project:     project,
		Name:        pulumi.String("cloud.firestore"),
		RulesetName: pulumi.Sprintf("projects/%s/rulesets/%s", p.Firebase, ruleset.Name),
	}, opt(ruleset)); err != nil {
		return err
	}

	// The backend: its image, built by Cloud Build, and the service that runs it.
	image, err := backend(ctx, p, after)
	if err != nil {
		return err
	}
	runtime, err := serviceaccount.NewAccount(ctx, "runtime", &serviceaccount.AccountArgs{
		Project:     project,
		AccountId:   pulumi.String(p.Name + "-api"),
		DisplayName: pulumi.String(p.Name + " backend"),
	}, after)
	if err != nil {
		return err
	}
	// Commands read and write Firestore, and nothing else. Checking a sign-in token
	// needs no role, since the keys that sign tokens are public.
	if _, err := projects.NewIAMMember(ctx, "runtime-firestore", &projects.IAMMemberArgs{
		Project: project,
		Role:    pulumi.String("roles/datastore.user"),
		Member:  pulumi.Sprintf("serviceAccount:%s", runtime.Email),
	}); err != nil {
		return err
	}
	envs := cloudrunv2.ServiceTemplateContainerEnvArray{&cloudrunv2.ServiceTemplateContainerEnvArgs{
		Name:  pulumi.String("GOOGLE_CLOUD_PROJECT"),
		Value: project,
	}}
	backendNeeds := []pulumi.Resource{db}
	if p.GitHub != "" {
		env, access, err := githubSecret(ctx, p, project, runtime, after)
		if err != nil {
			return err
		}
		envs = append(envs, env)
		backendNeeds = append(backendNeeds, access)
	}
	run, err := cloudrunv2.NewService(ctx, "backend", &cloudrunv2.ServiceArgs{
		Project:  project,
		Name:     pulumi.String(Service),
		Location: pulumi.String(p.Region),
		// Destroying the stack, or changing Region or Firebase (which replaces the
		// service), needs this turned off first, in a deploy of its own.
		DeletionProtection: pulumi.Bool(true),
		// Anyone may call it, the same as locally: every command checks who's asking
		// and whether they may, so a second gate in front would only duplicate that.
		InvokerIamDisabled: pulumi.Bool(true),
		Template: &cloudrunv2.ServiceTemplateArgs{
			ServiceAccount: runtime.Email,
			Scaling: &cloudrunv2.ServiceTemplateScalingArgs{
				MinInstanceCount: pulumi.Int(0),
				MaxInstanceCount: pulumi.Int(2),
			},
			Containers: cloudrunv2.ServiceTemplateContainerArray{&cloudrunv2.ServiceTemplateContainerArgs{
				Image: image,
				Envs:  envs,
			}},
		},
	}, opt(backendNeeds...))
	if err != nil {
		return err
	}

	// The web app: its Firebase registration, the Hosting site, and the domain.
	app, err := firebase.NewWebApp(ctx, "web", &firebase.WebAppArgs{
		Project:     project,
		DisplayName: pulumi.String(p.Name),
	}, after)
	if err != nil {
		return err
	}
	web := firebase.GetWebAppConfigOutput(ctx, firebase.GetWebAppConfigOutputArgs{Project: project, WebAppId: app.AppId})
	// Firebase makes a project's default site itself, the way it makes the project,
	// so it's imported rather than created, and left in place if the stack is ever
	// destroyed. In a fresh project's preview the Hosting API isn't on yet, so there's
	// nothing to read; the deploy itself imports it once the API is on.
	siteOptions := []pulumi.ResourceOption{after}
	reachable, err := siteReachable(ctx.Context(), p.Firebase)
	if err != nil {
		return err
	}
	if reachable || !ctx.DryRun() {
		siteOptions = append(siteOptions, pulumi.Import(pulumi.ID(fmt.Sprintf("projects/%s/sites/%s", p.Firebase, p.Firebase))))
	}
	site, err := firebase.NewHostingSite(ctx, "site", &firebase.HostingSiteArgs{
		Project:        project,
		SiteId:         project,
		DeletionPolicy: pulumi.String("ABANDON"),
	}, siteOptions...)
	if err != nil {
		return err
	}
	domain, err := firebase.NewHostingCustomDomain(ctx, "domain", &firebase.HostingCustomDomainArgs{
		Project:      project,
		SiteId:       site.SiteId.Elem(),
		CustomDomain: pulumi.String(p.Domain),
		// DNS is changed by hand at the registrar, so a deploy doesn't wait for it.
		WaitDnsVerification: pulumi.Bool(false),
	})
	if err != nil {
		return err
	}
	// The domains sign-in is allowed from are left to the console. The resource that
	// holds them would turn the project's Firebase Auth into Identity Platform, which
	// is priced and run differently, and that isn't a side effect to have.

	// A budget on the billing account the project is linked to, so a surprise shows
	// up as an email rather than as a bill.
	// The lookup reads billing through an API it has to wait for.
	info := organizations.LookupProjectOutput(ctx, organizations.LookupProjectOutputArgs{ProjectId: project}, after)
	account := info.BillingAccount().ApplyT(func(account string) (string, error) {
		if account == "" {
			return "", fmt.Errorf("infrastructure: %s has no billing account; link one in the console first", p.Firebase)
		}
		return account, nil
	}).(pulumi.StringOutput)
	amount, err := settings.TryInt("budget")
	switch {
	case errors.Is(err, config.ErrMissingVar):
		amount = 10
	case err != nil:
		return fmt.Errorf("infrastructure: budget has to be a whole number: %w", err)
	case amount <= 0:
		return fmt.Errorf("infrastructure: budget is %d; it has to be more than 0", amount)
	}
	if _, err := billing.NewBudget(ctx, "budget", &billing.BudgetArgs{
		BillingAccount: account,
		DisplayName:    pulumi.String(p.Name),
		BudgetFilter: &billing.BudgetBudgetFilterArgs{
			Projects: pulumi.StringArray{pulumi.Sprintf("projects/%s", info.Number())},
		},
		Amount: &billing.BudgetAmountArgs{
			SpecifiedAmount: &billing.BudgetAmountSpecifiedAmountArgs{Units: pulumi.String(fmt.Sprint(amount))},
		},
		ThresholdRules: billing.BudgetThresholdRuleArray{
			&billing.BudgetThresholdRuleArgs{ThresholdPercent: pulumi.Float64(0.5)},
			&billing.BudgetThresholdRuleArgs{ThresholdPercent: pulumi.Float64(1)},
			&billing.BudgetThresholdRuleArgs{ThresholdPercent: pulumi.Float64(1), SpendBasis: pulumi.String("FORECASTED_SPEND")},
		},
	}, after); err != nil {
		return err
	}

	// What the web build needs, and what has to be set at the registrar.
	ctx.Export("firebase_api_key", web.ApiKey())
	ctx.Export("firebase_app_id", app.AppId)
	ctx.Export("firebase_project_id", project)
	ctx.Export("firebase_auth_domain", web.AuthDomain())
	ctx.Export("backend_url", run.Uri)
	ctx.Export("dns_records", domain.RequiredDnsUpdates.ApplyT(records))
	return nil
}

// records lists the changes the registrar needs, one per line, like
// "add A uione.io 199.36.158.100". Records to take away, such as the ones that point
// the domain at its old host, are listed as "remove".
func records(updates []firebase.HostingCustomDomainRequiredDnsUpdate) []string {
	out := []string{}
	for _, update := range updates {
		for _, want := range update.Desireds {
			for _, r := range want.Records {
				action := map[string]string{"ADD": "add", "DELETE": "remove"}[deref(r.RequiredAction)]
				if action == "" {
					continue
				}
				out = append(out, fmt.Sprintf("%s %s %s %s", action, deref(r.Type), deref(r.DomainName), deref(r.Rdata)))
			}
		}
	}
	return out
}

func deref(s *string) string {
	if s == nil {
		return ""
	}
	return *s
}

// githubSecret makes the master secret the backend derives each project's webhook
// secret from: a random value, kept in Secret Manager, readable by the backend and
// nothing else. It's never pasted into GitHub itself; each project's maintainers
// see their own secret in the app. It's the stack output
// github_webhook_master_secret, for recovering it.
func githubSecret(ctx *pulumi.Context, p Project, project pulumi.StringInput, runtime *serviceaccount.Account,
	after pulumi.ResourceOrInvokeOption) (*cloudrunv2.ServiceTemplateContainerEnvArgs, pulumi.Resource, error) {
	value, err := random.NewRandomPassword(ctx, "github-webhook-secret", &random.RandomPasswordArgs{
		Length:  pulumi.Int(40),
		Special: pulumi.Bool(false),
	})
	if err != nil {
		return nil, nil, err
	}
	secret, err := secretmanager.NewSecret(ctx, "github-webhook-secret", &secretmanager.SecretArgs{
		Project:     project,
		SecretId:    pulumi.String("github-webhook-secret"),
		Replication: &secretmanager.SecretReplicationArgs{Auto: &secretmanager.SecretReplicationAutoArgs{}},
		// Every project's webhook secret is derived from this one, so removing GitHub
		// from the project, or destroying the stack, needs this turned off first.
		DeletionProtection: pulumi.Bool(true),
	}, after)
	if err != nil {
		return nil, nil, err
	}
	version, err := secretmanager.NewSecretVersion(ctx, "github-webhook-secret", &secretmanager.SecretVersionArgs{
		Secret:     secret.ID(),
		SecretData: value.Result,
	})
	if err != nil {
		return nil, nil, err
	}
	access, err := secretmanager.NewSecretIamMember(ctx, "runtime-github-webhook-secret", &secretmanager.SecretIamMemberArgs{
		Project:  project,
		SecretId: secret.SecretId,
		Role:     pulumi.String("roles/secretmanager.secretAccessor"),
		Member:   pulumi.Sprintf("serviceAccount:%s", runtime.Email),
	})
	if err != nil {
		return nil, nil, err
	}
	ctx.Export("github_webhook_url", pulumi.Sprintf("https://%s%s", p.Domain, p.GitHub))
	ctx.Export("github_webhook_master_secret", pulumi.ToSecret(value.Result))
	env := &cloudrunv2.ServiceTemplateContainerEnvArgs{
		Name: pulumi.String("GITHUB_WEBHOOK_SECRET"),
		ValueSource: &cloudrunv2.ServiceTemplateContainerEnvValueSourceArgs{
			SecretKeyRef: &cloudrunv2.ServiceTemplateContainerEnvValueSourceSecretKeyRefArgs{
				Secret:  secret.SecretId,
				Version: version.Version,
			},
		},
	}
	return env, pulumi.Resource(access), nil
}

var (
	projectNames = regexp.MustCompile(`^[a-z][a-z0-9-]*$`)
	firebaseIds  = regexp.MustCompile(`^[a-z][a-z0-9-]{4,28}[a-z0-9]$`)
)

// check refuses settings Google Cloud would refuse partway through a deploy, so
// nothing is declared from them.
func (p Project) check() error {
	switch {
	case p.Name == "":
		return errors.New("infrastructure: the project has no name")
	case !projectNames.MatchString(p.Name):
		return fmt.Errorf("infrastructure: the name %q has to be lowercase letters, digits and hyphens, starting with a letter", p.Name)
	case len(p.Name) < 2 || len(p.Name) > 24:
		// Its service accounts are the name and -api or -build, which Google
		// allows from 6 to 30 characters.
		return fmt.Errorf("infrastructure: the name %q has to be 2 to 24 characters long", p.Name)
	case p.Domain == "":
		return errors.New("infrastructure: the project has no domain")
	case p.Firebase == "":
		return errors.New("infrastructure: the project has no Firebase project")
	case !firebaseIds.MatchString(p.Firebase):
		return fmt.Errorf("infrastructure: %q isn't a Firebase project id, which is 6 to 30 lowercase letters, digits "+
			"and hyphens, starting with a letter and not ending with a hyphen", p.Firebase)
	case p.Region == "":
		return errors.New("infrastructure: the project has no region")
	case p.GitHub != "" && !strings.HasPrefix(p.GitHub, "/"):
		return fmt.Errorf("infrastructure: GitHub's webhook route %q has to start with /", p.GitHub)
	}
	return nil
}

// siteReachable asks Firebase Hosting whether a project's default site can be read
// yet, which it can't before the Hosting API is on. Tests replace it.
var siteReachable = func(ctx context.Context, project string) (bool, error) {
	return answers(ctx, project, "https://firebasehosting.googleapis.com/v1beta1/projects/"+url.PathEscape(project)+"/sites/"+url.PathEscape(project))
}

// firebaseAdded asks Firebase whether it has been added to a Google Cloud project.
// A project whose Firebase API isn't on yet hasn't had it added. Tests replace it.
var firebaseAdded = func(ctx context.Context, project string) (bool, error) {
	return answers(ctx, project, "https://firebase.googleapis.com/v1beta1/projects/"+url.PathEscape(project))
}

// answers reads something of a project's from a Google API: true when it's there,
// false when it isn't, or when the API isn't on for the project yet.
func answers(ctx context.Context, project, address string) (bool, error) {
	client, err := google.DefaultClient(ctx, "https://www.googleapis.com/auth/cloud-platform")
	if err != nil {
		return false, err
	}
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, address, nil)
	if err != nil {
		return false, err
	}
	request.Header.Set("X-Goog-User-Project", project)
	response, err := client.Do(request)
	if err != nil {
		return false, err
	}
	defer response.Body.Close()
	switch {
	case response.StatusCode == http.StatusOK:
		return true, nil
	case response.StatusCode == http.StatusNotFound, response.StatusCode == http.StatusForbidden:
		return false, nil
	default:
		body, _ := io.ReadAll(io.LimitReader(response.Body, 2048))
		return false, fmt.Errorf("infrastructure: asking about %s at %s: %s %s", project, address, response.Status, body)
	}
}
