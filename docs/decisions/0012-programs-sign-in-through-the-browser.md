# 0012: Programs sign in as a person through their browser

- Status: Accepted
- Date: 2026-10-10
- Deciders: Daher Alfawares

## Context

A person runs an app's command, like `neotrac`, on their own computer, and it should
act as them. Asking them to paste a token, or to type a code at GitHub, is clumsy,
and GitHub's device flow needs a setting turned on by hand in each app's OAuth app,
with no API, and works for GitHub accounts only.
[0011](0011-services-with-keys.md) signs services in with keys; people have none.

## Decision

A program signs a person in through their browser, as native apps do with OAuth
(RFC 8252, with PKCE from RFC 7636), with uione's own pieces instead of an OAuth
server:

- **Every app people sign in to serves `/authorize`.** The program listens on
  `127.0.0.1` at a port the system picks and opens the page with the port, a state
  and a challenge. The person signs in there however the app lets them, and presses
  Authorize.
- **The code is bound to the challenge.** `POST /api/authorize`, as the person,
  keeps the hash of a new code with their id and the challenge for five minutes, and
  the page sends the browser to `http://127.0.0.1:<port>/` with the code and state,
  and nowhere else.
- **The program trades it once.** `POST /api/token` with the code and the verifier
  deletes the code, and, if the verifier's SHA-256 is the challenge, returns a
  Firebase custom token of the person's own, marked `command_line`. The program
  signs in with it and keeps the refresh token.
- **The backend signs it as itself**, as it does a service's, so every app people
  sign in to deploys with that grant.

## Consequences

- `neotrac login` opens the browser, and nothing has to be set up by hand.
- It works with every way an app lets people sign in, not only GitHub.
- A program on another machine, over SSH, can't be reached by the browser; it signs
  in with a service's key, or the port is forwarded.
- Signing a person out of a program is forgetting its refresh token; revoking every
  sign-in of theirs is Firebase's.
