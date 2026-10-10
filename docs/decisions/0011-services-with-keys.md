# 0011: Services that work beside people, with keys

- Status: Accepted
- Date: 2026-10-10
- Deciders: Daher Alfawares

## Context

Programs, like an AI agent or a script, work on a project's issues beside its
people: an issue moved into a phase they work waits for one of them to take it,
look into it, say what it found and move it on. They need to read what a member
reads, run only what they're given, and sign in without a person's browser.
[0010](0010-who-may-do-and-read-what.md) gives access to people only, signed in
with Firebase; `define service` is a system declared in code, with no credential.

## Decision

A role a project's programs may hold says so: `define role agent "Agent" in project
for services`. With one, the language declares `service`, records a project makes in
its settings, each named by its title and holding one role made for services.

- **A service is a member.** Its record makes a member of its project with its role,
  and a profile, its title, marked a service. So it reads what members read, under
  the same rules, and `held(...)` and a step's roles count it as anyone else.
- **It runs only what its role allows, in its project.** Nothing that says `by
  anyone` or `by anyone signed in`, no grant given everywhere, and nothing that
  changes who has access, whatever a role's record is edited to say.
- **It signs in with a key.** `one_<id>_<secret>`: shown once, kept only as the
  secret's SHA-256 in `keys/{id}`, which no browser reads, compared in constant
  time, replaced by a new one and refused at once when revoked. The prefix lets a
  scanner find one left where it shouldn't be. Each key may make 10 requests a
  second, 30 at once, on each instance.
- **The key trades for a Firebase sign-in of the service's own.** `POST /api/token`
  returns a custom token for the service's id, with `service: true`, that the
  backend signs as its own account. Signed in with it, a program reads views live,
  as a page does, under unchanged rules. A command run with it is still a
  service's. A revoked key ends reading when the sign-in's hour runs out; removing
  the service ends it at once, as it's no longer a member.

## Consequences

- A program needs no browser, and no person's account.
- A leaked key acts as one role, in one project, until it's revoked.
- A project with a role for services deploys with its backend allowed to sign tokens
  as itself, through IAM Credentials.
- A service has no username, so no `@mention` names one.
