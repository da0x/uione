# site

uione.io's pages, written in uione: the language, the releases, installing, the
mission and the waitlist. They're written here, beside the compiler they describe,
and the studio at uione.io copies them when it moves to a release, so the site
always says the version it was built with.

It's also the first real project the toolchain builds, so it uses every layer:
pages, a public command, a public live view, sign-in, and a private per-person view.
`tools/develop` and the end-to-end test run it.

| File | What it is |
|---|---|
| `site.one` | the project: domain, Firebase project, region, component adapter, sign-in |
| `home.one` | the front page: the studio first, then the compiler, installing it, and the waitlist |
| `waitlist.one` | the waitlist: an entity, a public command, and a public count |
| `language.one` | `/language/:page`, one page per file in `language/`: starting, the overview, the reference |
| `releases.one` | `/releases/:page`, one page per release in `releases/`, newest first |
| `mission.one` | `/mission`: why uione exists |
| `install.one` | `/install/:page`, one page per way to install the compiler, in `install/` |
| `studio.one` | a person's projects, private to them, kept to test sign-in end to end; the studio itself runs at uione.io |
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

## Deploying

This folder isn't deployed by itself any more. uione.io is the studio's, and
www.uione.io sends people there; the studio's `tools/pages-from-uione` copies these
pages, `public/install.sh` and the library example into its own `site/` folder.
