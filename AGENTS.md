# AGENTS.md

Conventions for working in this repository. They apply to people and assistants
alike.

## This repository is public-facing

Treat every file as published, whatever the repository's visibility today.

- **Everything here is original work.** Never copy code, text, identifiers or
  designs from another codebase into this one, including as a reference or a test
  fixture. Examples are written from scratch for this repository.
- **Every source file starts with its copyright, held by Daher Alfawares, and its
  license: LGPL-3.0-only in the four libraries, AGPL-3.0-only everywhere else**
  (decision 0004). `tools/check-license` enforces it, and `.githooks/pre-commit`
  runs it on every commit. Files that cannot hold a comment are listed in that
  script, with the reason.
- **Code that uione generates for someone else's project is theirs.** The generators
  must not stamp this repository's copyright onto it.

## What is here

- `examples/<feature>/main.one` is a feature written in uione. `library` uses most
  of the language, `tasks` is the smallest that needs sign-in, and `tracker` has
  roles held within a project, private projects, lists, history and GitHub's
  webhook.
- `site/` is uione.io, written in uione, and the first project the toolchain builds.
  `site/target/` holds hand-written examples of what the generators must emit. When
  the syntax or a library's API changes, the target files change with it, because
  they are the specification the generators are tested against.
- `compiler/` is `one`, the compiler, in C++23. `make -C compiler test` builds it
  and runs its tests.
  `compiler/build/one check examples/library examples/tasks examples/tracker site` checks every
  project; each path given to `one check` is one project, checked on its own.
  `compiler/build/one build site` generates the site into `site/build`: the web app
  in `web/` (a Yarn workspace, so it has to exist before `yarn install`), the Go
  backend in `api/`, `firestore.rules`, and what it takes to deploy (see
  `infrastructure/`). Anything the `one` library can't do yet stops the build with
  "not supported yet" and the line it's on; every project in the repository builds.
  `node tools/smoke-web.mjs` renders every page of the web app in Node, and
  `yarn workspace uione-rules-test test` tests the rules against the emulator.
  `compiler/build/one show <file>:<line>` (or a range, like `main.one:53-56`) prints
  the code those lines generate, in every output, like the assembly view of a block
  of C++. Every generator writes through `code::stream`, which records the `.one`
  line each output line came from, and a test holds every generated line in the
  repository to having one, or to being marked as the same in every project.
  `tools/develop` runs the whole site locally against the emulators, to try by hand,
  and `node tools/end-to-end.mjs` runs it end to end with no browser: the generated
  backend, the generated rules, and the Firebase data source, checking that views
  update live and that the rules keep each person's views to themselves. It follows
  decision 0003: snake_case, namespaces, headers under `language/`, and anything
  that touches the operating system only in `compiler/platform/`, so the compiler
  can later be built for the browser. Warnings are errors. Tests use doctest, which
  CMake downloads at a pinned version rather than keeping it in the repository.
- `packages/` holds the JavaScript libraries that generated apps are built on, as
  Yarn workspaces. `packages/react` is `@uione/react`: the runtime and the component
  contract. `yarn install`, then `yarn workspace @uione/react test` (or `typecheck`,
  `build`). `@uione/react/firebase` is the data source a generated app uses: views
  read live from Firestore, commands sent to the Go backend, and Google sign-in. Its
  rules for staying honest about a dropped connection are in `src/live.ts`, apart
  from Firebase, so the tests can break the connection on demand. A component set
  only draws; routing, data, commands and confirmations live in `@uione/react`, so
  every look behaves the same. `packages/radix` is `@uione/radix`, the styled
  component set (decision 0002). Build `@uione/react` before it, since it's used
  through its built output.
- `one/` is the Go library that generated backends are built on: commands over HTTP,
  sign-in and permission checks, validation, storage, events, and the views rebuilt
  from them. Its tests run against the Firebase emulators, which
  `tools/emulators/run` starts in Docker (Firestore on 8080, Auth on 9099, project
  `demo-uione`). Then `cd one && go test -race ./...`.
- `infrastructure/` is the Go library that generated Pulumi programs are built on:
  every resource a project runs on in Google Cloud, declared in one place (decision
  0008). Its tests use Pulumi's mocks, so `cd infrastructure && go test ./...` needs
  no cloud. `one build` writes `build/infrastructure`, `build/api/dockerfile`,
  `build/web/firebase.json` and `build/deploy` for a project whose settings name its
  `firebase` project, `region` and `domain`. Nothing is created in the cloud except
  through that program, and anything that can't be is listed in the project's README
  with the reason.
- `docs/grammar.ebnf` is the language's grammar, the specification the hand-written
  parser is held to. `compiler/tests/grammar_tests.cpp` checks it both ways: programs
  made from its rules must parse, and every `.one` file in the repository must fit
  it. `editors/test.mjs` checks that both editor grammars know exactly its words.
  A change to the syntax changes the grammar first.
- `editors/` holds the highlighting for Vim and VS Code, and its test.
- `tools/` holds the repository's own checks.
- `docs/decisions/` records the decisions that shape uione, one per file. Read them
  before changing anything they cover.

## One architecture

uione targets exactly one architecture, and the language never names it:

- Commands are the only writes. Each one publishes an event.
- Projections turn events into Firestore view documents.
- Screens subscribe to those documents and update live.
- A screen never reads an entity, never calls an endpoint, and never polls.

Anything a screen shows is therefore a view. If a screen needs data no view
provides, the fix is a new `view` block, never a query in the screen. Other
architectures may come later. Until then, do not add syntax that only makes sense
under a different one.

## Rules

- **A change to a decision carries its record.** Going against an accepted record
  in `docs/decisions/` means writing a new record that supersedes it, in the same
  commit, never quietly working around it.
- **Work lands on `main`, checked first** (decision 0007). Before a push, every step
  of the `checks` workflow passes from a clean clone. Commits are signed, `main` is
  never force-pushed, and messages carry no attribution lines.

- **Highlighting moves with the language.** A change that adds, removes or renames a
  word in a `.one` file updates both grammars under `editors/` and `expected.tsv` in
  the same commit, and `node editors/test.mjs` passes. See `editors/README.md`.
- **Every example stays true.** An example `.one` file is only changed together with
  anything that quotes it: the docs under `site/docs/`, the landing page, and
  `editors/expected.tsv`.
- **Every name in a `.one` file is snake_case**, keywords included (`signed_in`). How
  names are displayed is the reader's choice; how they're stored is not. See
  `docs/decisions/0006-names-are-snake-case.md`.
- File and folder names are lowercase kebab-case: `makefile`, `dockerfile`. Only
  README.md, AGENTS.md, SECURITY.md, LICENSE, COPYING, CMakeLists.txt (the one name CMake reads)
  and Pulumi.yaml (the one name Pulumi reads) have capitals. `tools/check-names`
  enforces it, before each commit and in CI.
- Names of folders, files, modules and namespaces are whole words, because a clipped
  word is one more thing to decode: `generators`, not `gen`; `infrastructure`, not
  `infra`; `language`, not `lang`. `tools/check-names` catches the common clipped
  words. Names a tool fixes, like `src/` or `vite.config.ts`, are left as they are.
- Commit subjects are plain sentences: a capital letter, a verb, and what it does.
  No `type(scope):` prefixes.
