# 0008: Projects are deployed with Pulumi, by hand at first

- Status: Accepted
- Date: 2026-10-02
- Deciders: Daher Alfawares

## Context

Resources created by hand, or quietly by a tool, drift away from anything that
describes them. `pulumi preview` then says "no changes" while things nobody
declared sit in the project.

## Decision

Everything a project runs on is declared in Pulumi. `one build` writes a Go program
into `build/infrastructure` that holds only the project's settings; the
`infrastructure` library declares the resources. State lives in Pulumi Cloud.

- The backend's image is built by Cloud Build under its own service account, from
  a bucket the program declares, and tagged with a hash of its source so an
  unchanged backend isn't rebuilt.
- `build/deploy` runs `pulumi up`, builds the web app, and uploads it to Hosting.
  It's run by hand for now.
- Firebase makes a project's default Hosting site itself, so the program imports it
  and leaves it in place if the stack is destroyed.
- Events are handled in the same request as the command that caused them, not
  through Pub/Sub. With one small service nothing is lost: if a projection fails,
  the command fails and says so.
- A project that takes GitHub's webhook gets a random secret, made by Pulumi, kept
  in Secret Manager and readable only by the backend. One secret serves every
  repository on a site; a GitHub App can replace it when projects need their own.

What can't be declared is done by hand and listed in the project's README with the
reason: linking billing, signing in to Pulumi Cloud, turning on Google sign-in,
adding the domain to sign-in's authorized domains, and the DNS records at the
domain's host.

## Consequences

A preview shows every change before it's made, and the program is the complete
list of what a project runs on.

A deploy needs a person at a terminal with Pulumi, gcloud and Docker. Until the
libraries are published, generated `go.mod` files point into this repository, so a
project can only be deployed from a clone of it.
