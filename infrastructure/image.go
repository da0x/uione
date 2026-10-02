// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package infrastructure

import (
	"bufio"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/pulumi/pulumi-command/sdk/go/command/local"
	"github.com/pulumi/pulumi-gcp/sdk/v9/go/gcp/artifactregistry"
	"github.com/pulumi/pulumi-gcp/sdk/v9/go/gcp/projects"
	"github.com/pulumi/pulumi-gcp/sdk/v9/go/gcp/serviceaccount"
	"github.com/pulumi/pulumi-gcp/sdk/v9/go/gcp/storage"
	"github.com/pulumi/pulumi/sdk/v3/go/pulumi"
)

// backend declares where the backend's image is built and kept, builds it, and
// returns its address. The image is tagged with a hash of its source, so it's built
// again only when the source changes, and Cloud Run moves to it only then.
//
// The image itself is a build artifact, like a compiled binary, so it isn't managed
// as a resource. Everything that makes it is.
func backend(ctx *pulumi.Context, p Project, after pulumi.ResourceOption) (pulumi.StringOutput, error) {
	project := pulumi.String(p.Firebase)
	api := filepath.Join(p.Build, "api")
	one, err := library(api)
	if err != nil {
		return pulumi.StringOutput{}, err
	}
	hash, err := hashTrees(api, one)
	if err != nil {
		return pulumi.StringOutput{}, err
	}

	repo, err := artifactregistry.NewRepository(ctx, "images", &artifactregistry.RepositoryArgs{
		Project:      project,
		Location:     pulumi.String(p.Region),
		RepositoryId: pulumi.String(p.Name),
		Format:       pulumi.String("DOCKER"),
		// Keep the last few images to roll back to, and nothing older.
		CleanupPolicies: artifactregistry.RepositoryCleanupPolicyArray{&artifactregistry.RepositoryCleanupPolicyArgs{
			Id:     pulumi.String("keep-recent"),
			Action: pulumi.String("KEEP"),
			MostRecentVersions: &artifactregistry.RepositoryCleanupPolicyMostRecentVersionsArgs{
				KeepCount: pulumi.Int(5),
			},
		}, &artifactregistry.RepositoryCleanupPolicyArgs{
			Id:     pulumi.String("delete-old"),
			Action: pulumi.String("DELETE"),
			Condition: &artifactregistry.RepositoryCleanupPolicyConditionArgs{
				OlderThan: pulumi.String("2592000s"),
			},
		}},
	}, after)
	if err != nil {
		return pulumi.StringOutput{}, err
	}

	// The build uploads its source to this bucket, which the build script names.
	// Left to itself, gcloud would create a bucket of its own for them. Uploads are
	// only needed while a build runs, so they're removed after a week.
	uploads, err := storage.NewBucket(ctx, "build-uploads", &storage.BucketArgs{
		Project:                  project,
		Name:                     pulumi.String(p.Firebase + "_cloudbuild"),
		Location:                 pulumi.String(strings.ToUpper(p.Region)),
		UniformBucketLevelAccess: pulumi.Bool(true),
		ForceDestroy:             pulumi.Bool(true),
		LifecycleRules: storage.BucketLifecycleRuleArray{&storage.BucketLifecycleRuleArgs{
			Action:    &storage.BucketLifecycleRuleActionArgs{Type: pulumi.String("Delete")},
			Condition: &storage.BucketLifecycleRuleConditionArgs{Age: pulumi.Int(7)},
		}},
	}, after)
	if err != nil {
		return pulumi.StringOutput{}, err
	}

	// Builds run as an account of their own that can read the uploads, push images
	// and write logs, and nothing more. Projects differ in what the default build
	// account may do, so relying on it would make a deploy work in one project and
	// not the next.
	builder, err := serviceaccount.NewAccount(ctx, "builder", &serviceaccount.AccountArgs{
		Project:     project,
		AccountId:   pulumi.String(p.Name + "-build"),
		DisplayName: pulumi.String(p.Name + " image builds"),
	}, after)
	if err != nil {
		return pulumi.StringOutput{}, err
	}
	member := pulumi.Sprintf("serviceAccount:%s", builder.Email)
	grants := []pulumi.Resource{}
	g1, err := storage.NewBucketIAMMember(ctx, "builder-uploads", &storage.BucketIAMMemberArgs{
		Bucket: uploads.Name, Role: pulumi.String("roles/storage.objectViewer"), Member: member,
	})
	if err != nil {
		return pulumi.StringOutput{}, err
	}
	g2, err := artifactregistry.NewRepositoryIamMember(ctx, "builder-images", &artifactregistry.RepositoryIamMemberArgs{
		Project: project, Location: repo.Location, Repository: repo.Name,
		Role: pulumi.String("roles/artifactregistry.writer"), Member: member,
	})
	if err != nil {
		return pulumi.StringOutput{}, err
	}
	g3, err := projects.NewIAMMember(ctx, "builder-logs", &projects.IAMMemberArgs{
		Project: project, Role: pulumi.String("roles/logging.logWriter"), Member: member,
	})
	if err != nil {
		return pulumi.StringOutput{}, err
	}
	grants = append(grants, g1, g2, g3)

	image := pulumi.Sprintf("%s-docker.pkg.dev/%s/%s/%s:%s", p.Region, p.Firebase, repo.RepositoryId, Service, hash[:12])
	build, err := local.NewCommand(ctx, "image", &local.CommandArgs{
		Create:   pulumi.String(buildScript),
		Triggers: pulumi.Array{pulumi.String(hash)},
		Environment: pulumi.StringMap{
			"API":     pulumi.String(api),
			"ONE":     pulumi.String(one),
			"IMAGE":   image,
			"PROJECT": project,
			"REGION":  pulumi.String(p.Region),
			"UPLOADS": uploads.Name,
			"BUILDER": builder.Email,
		},
	}, pulumi.DependsOn(grants))
	if err != nil {
		return pulumi.StringOutput{}, err
	}
	// Cloud Run takes the image only once it's built.
	return pulumi.All(image, build.Stdout).ApplyT(func(v []any) string { return v[0].(string) }).(pulumi.StringOutput), nil
}

// The build puts the backend and the one library side by side, as the dockerfile
// expects, and has Cloud Build turn them into an image.
const buildScript = `set -eu
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
cp -R "$API" "$stage/api"
cp -R "$ONE" "$stage/one"
cat > "$stage/cloudbuild.yaml" <<YAML
steps:
  - name: gcr.io/cloud-builders/docker
    args: [build, --file, api/dockerfile, --tag, "$IMAGE", .]
images: ["$IMAGE"]
options:
  logging: CLOUD_LOGGING_ONLY
YAML
# A build account and its roles can take a minute to be seen everywhere after
# they're first made, so a refused first build is tried again before giving up.
for attempt in 1 2 3 4; do
  if gcloud builds submit "$stage" --quiet --project "$PROJECT" --region "$REGION" \
    --config "$stage/cloudbuild.yaml" \
    --gcs-source-staging-dir "gs://$UPLOADS/source" \
    --service-account "projects/$PROJECT/serviceAccounts/$BUILDER"; then
    echo "$IMAGE"
    exit 0
  fi
  [ "$attempt" -lt 4 ] && sleep 30
done
exit 1
`

// library finds the one library the backend is built on, from the replace line
// in its go.mod. Until the library is published, it's a folder in this repository.
func library(api string) (string, error) {
	f, err := os.Open(filepath.Join(api, "go.mod"))
	if err != nil {
		return "", fmt.Errorf("infrastructure: the backend isn't there; run one build first: %w", err)
	}
	defer f.Close()
	lines := bufio.NewScanner(f)
	for lines.Scan() {
		line := strings.TrimSpace(lines.Text())
		if !strings.HasPrefix(line, "replace github.com/da0x/uione/one") {
			continue
		}
		_, path, ok := strings.Cut(line, "=>")
		if !ok {
			break
		}
		path = strings.TrimSpace(path)
		if filepath.IsAbs(path) {
			return path, nil
		}
		return filepath.Join(api, path), nil
	}
	return "", fmt.Errorf("infrastructure: %s/go.mod doesn't say where the one library is", api)
}

// hashTrees is a hash of every file in the folders, by path and content, so it
// changes exactly when the source does, whichever machine computes it.
func hashTrees(roots ...string) (string, error) {
	sum := sha256.New()
	for i, root := range roots {
		var paths []string
		err := filepath.WalkDir(root, func(path string, d fs.DirEntry, err error) error {
			if err != nil {
				return err
			}
			if !d.IsDir() {
				paths = append(paths, path)
			}
			return nil
		})
		if err != nil {
			return "", err
		}
		sort.Strings(paths)
		for _, path := range paths {
			rel, _ := filepath.Rel(root, path)
			fmt.Fprintf(sum, "%d/%s\x00", i, filepath.ToSlash(rel))
			f, err := os.Open(path)
			if err != nil {
				return "", err
			}
			_, err = io.Copy(sum, f)
			f.Close()
			if err != nil {
				return "", err
			}
			sum.Write([]byte{0})
		}
	}
	return hex.EncodeToString(sum.Sum(nil)), nil
}
