# Linux

## Install one

One command installs the latest release of `one`, the uione compiler, for x86_64 or
ARM:

```sh
curl -fsSL https://uione.io/install.sh | sh
```

The installer picks the build for your machine, checks it against the release's
checksums, and puts it in `~/.local/bin`. It changes nothing else: no profile is
edited, and nothing runs as root. [Read it first](/install.sh) if you like, and
set `UIONE_VERSION=0.3.0` for a particular release or `UIONE_INSTALL` for another
folder.

The build is a single static binary, so it runs on any distribution. Check it:

```sh
one --version
```

Every release is also on [GitHub](https://github.com/da0x/uione/releases), with
its `SHA256SUMS`, and in a [container](/install/docker).

## Select an editor

Highlighting for Vim, Neovim and VS Code is in the repository's
[`editors/`](https://github.com/da0x/uione/tree/main/editors) folder. Or write
uione in [the studio](/), in your browser, with every line checked as you type.

## Build a project

```sh
mkdir tasks && cd tasks
curl -fsSL https://raw.githubusercontent.com/da0x/uione/v0.3.0/examples/tasks/main.one -o main.one
one check .
one build .
```

`one build` writes `build/`: the React app in `web/`, the Go service in `api/`, and
the Firestore rules. [Start](/language/start) walks through it, and the
[reference](/language/reference) covers every part of the language.

## Development builds

The `main` branch can be [built from source](/install/source). It isn't a release,
and the language can change on it.
