// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: LGPL-3.0-only

package infrastructure

import (
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
	"golang.org/x/mod/modfile"
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
	trees := []string{api}
	if one != "" {
		trees = append(trees, one)
	}
	hash, err := hashTrees(trees...)
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
	// Left to itself, gcloud would create a bucket of its own for them, named
	// <project>_cloudbuild, so this one's name is one only this library uses, and a
	// project where gcloud already made that one still deploys. Uploads are only
	// needed while a build runs, so they're removed after a week.
	uploads, err := storage.NewBucket(ctx, "build-uploads", &storage.BucketArgs{
		Project:                  project,
		Name:                     pulumi.String(uploadsBucket(p.Firebase)),
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
	// The environment holds this machine's paths, so a deploy from another machine
	// changes the command's inputs without changing the image. That runs Update
	// rather than Create, and Update builds only when the image isn't the one the
	// last build made, so only the hash in Triggers, and in the tag, rebuilds.
	build, err := local.NewCommand(ctx, "image", &local.CommandArgs{
		Create:   pulumi.String(buildScript),
		Update:   pulumi.String(unlessBuilt + buildScript),
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

// uploadsBucket is where a project's builds upload their source.
func uploadsBucket(firebase string) string {
	return firebase + "-uione-builds"
}

// unlessBuilt ends an update early when the last build, whose stdout ends with the
// image it made, already made this image.
const unlessBuilt = `if [ "$(printf '%s\n' "${PULUMI_COMMAND_STDOUT:-}" | tail -n 1)" = "$IMAGE" ]; then
  echo "$IMAGE"
  exit 0
fi
`

// The build puts the backend and the one library side by side, as the dockerfile
// expects, and has Cloud Build turn them into an image. When the backend uses a
// published one library rather than a folder, only the backend is staged.
//
// A build account and its roles can take a minute to be seen everywhere after
// they're first made, so a build refused for permission is tried again before
// giving up. Any other failure stops the deploy at once.
const buildScript = `set -eu
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
cp -RH "$API" "$stage/api"
if [ -n "$ONE" ]; then
  cp -RH "$ONE" "$stage/one"
fi
cat > "$stage/cloudbuild.yaml" <<YAML
steps:
  - name: gcr.io/cloud-builders/docker
    args: [build, --file, api/dockerfile, --tag, "$IMAGE", .]
images: ["$IMAGE"]
options:
  logging: CLOUD_LOGGING_ONLY
YAML
exec 3>&1
for attempt in 1 2 3 4; do
  # gcloud's errors are shown as they come and kept, to tell why it failed.
  echo 1 > "$stage/status"
  { status=0
    gcloud builds submit "$stage" --quiet --project "$PROJECT" --region "$REGION" \
      --config "$stage/cloudbuild.yaml" \
      --gcs-source-staging-dir "gs://$UPLOADS/source" \
      --service-account "projects/$PROJECT/serviceAccounts/$BUILDER" 2>&1 >&3 3>&- || status=$?
    echo "$status" > "$stage/status"; } | tee "$stage/errors" >&2
  if [ "$(cat "$stage/status")" = 0 ]; then
    echo "$IMAGE"
    exit 0
  fi
  grep -qiE 'permission|forbidden|403|does not have|does not exist' "$stage/errors" || exit 1
  [ "$attempt" -lt 4 ] && sleep 30
done
exit 1
`

// libraryModule is the one library's module path.
const libraryModule = "github.com/da0x/uione/one"

// library finds the one library the backend is built on, from a replace of it with
// a folder in the backend's go.mod. Until the library is published, it's a folder
// in this repository. Without one, the backend uses a published version, and
// library returns "".
func library(api string) (string, error) {
	path := filepath.Join(api, "go.mod")
	data, err := os.ReadFile(path)
	if err != nil {
		return "", fmt.Errorf("infrastructure: the backend isn't there; run one build first: %w", err)
	}
	file, err := modfile.Parse(path, data, nil)
	if err != nil {
		return "", fmt.Errorf("infrastructure: %w", err)
	}
	for _, r := range file.Replace {
		if r.Old.Path != libraryModule || r.New.Version != "" {
			continue
		}
		if filepath.IsAbs(r.New.Path) {
			return r.New.Path, nil
		}
		return filepath.Join(api, r.New.Path), nil
	}
	return "", nil
}

// hashTrees is a hash of every file in the folders, by path and content, so it
// changes exactly when the source does, whichever machine computes it. A symbolic
// link is hashed as where it points, the way the build copies it, rather than
// followed. Anything that's neither a file nor a link, like a socket, is skipped.
func hashTrees(roots ...string) (string, error) {
	sum := sha256.New()
	for i, root := range roots {
		// The folder itself may be reached through a link, which the build follows.
		root, err := filepath.EvalSymlinks(root)
		if err != nil {
			return "", err
		}
		var paths []string
		err = filepath.WalkDir(root, func(path string, d fs.DirEntry, err error) error {
			if err != nil {
				return err
			}
			if d.Type().IsRegular() || d.Type()&fs.ModeSymlink != 0 {
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
			info, err := os.Lstat(path)
			if err != nil {
				return "", err
			}
			if info.Mode()&fs.ModeSymlink != 0 {
				target, err := os.Readlink(path)
				if err != nil {
					return "", err
				}
				fmt.Fprintf(sum, "%d/%s\x00link\x00%s\x00", i, filepath.ToSlash(rel), filepath.ToSlash(target))
				continue
			}
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
