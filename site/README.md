# site

uione.io, written in uione. It is the first real project the toolchain builds, so it
uses every layer: pages, a public command, a public live view, sign-in, and a
private per-person view.

| File | What it is |
|---|---|
| `site.one` | the project: domain, Firebase project, region, component adapter, sign-in |
| `home.one` | the front page at `/`: the studio first, then the compiler, installing it, and the waitlist |
| `waitlist.one` | the waitlist: an entity, a public command, and a public count |
| `language.one` | `/language/:page`, one page per file in `language/`: starting, the overview, the reference |
| `releases.one` | `/releases/:page`, one page per release in `releases/`, newest first |
| `mission.one` | `/mission`: why uione exists |
| `install.one` | `/install/:page`, one page per way to install the compiler, in `install/` |
| `studio.one` | `/studio`: your projects, private to you |
| `language/`, `releases/`, `install/` | those pages, in markdown |
| `public/` | files served as they are, like `install.sh` at `/install.sh` |
| `target/` | hand-written examples of the code `one build` should generate, for review |

`one build site` writes `site/build/`, which is never committed and never edited.

## Trying it

`tools/develop` runs the site on your machine against the Firebase emulators: the
generated Go backend on port 8081, and Vite, which forwards `/api` to it. Open the
address Vite prints, join the waitlist in one tab, and watch the count change in
another. Sign-in goes to the Auth emulator, which offers a made-up Google account,
so no real account is involved.

## Before the first deploy

A few steps happen outside Pulumi, because no resource can do them or because
they belong to another service:

1. **Link billing to the `ui-one` project.** A project can't pay for itself, and
   Cloud Run and Cloud Build don't run without billing.
2. **Sign in to Pulumi Cloud** with `pulumi login`, where the stack's state is
   kept (decision 0009).
3. **Turn on Google as a sign-in provider** in the Firebase console. Doing so
   creates an OAuth client, and no Pulumi resource manages that client.
4. **Add `uione.io` to sign-in's authorized domains**, in the same place, if it
   isn't there already. The Pulumi resource that holds that list would also turn
   the project's Firebase Auth into Identity Platform, which is priced and run
   differently, so it's left to the console.
5. **Change the DNS records at Squarespace** to the ones the deploy prints. This
   replaces the Squarespace site the domain serves today.
6. **Create the `uione` organization on npm**, before `@uione/react` and
   `@uione/radix` are published.

Everything else is declared by the generated Pulumi program.

## Deploying

With the compiler built and `yarn install` done:

```
compiler/build/one build site
yarn workspace @uione/react build && yarn workspace @uione/radix build
site/build/deploy
```

The deploy shows Pulumi's preview and asks before changing anything. It needs
Pulumi and gcloud (signed in, with application default credentials). `--yes`
deploys without asking, and `--json` reports each step as a line of JSON.
