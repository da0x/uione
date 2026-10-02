# Start

uione is a language for writing a whole application feature in one short file, and
`one` is its compiler. This page installs it and builds a first project. The
current release is [0.2.0](/releases/v0-2-0).

## Install the compiler

The compiler is built from source for now. It needs Git, CMake 3.25 or newer, and a
C++23 compiler such as GCC 14.

```sh
git clone https://github.com/da0x/uione.git
cd uione
git checkout v0.2.0
make -C compiler
```

That writes the compiler to `compiler/build/one`. Put it on your path:

```sh
sudo install compiler/build/one /usr/local/bin/one
one --version
```

## Your first project

A project is a folder of `.one` files. Make one with a single feature:

```sh
mkdir notes
cat > notes/main.one <<'END'
namespace notes {

	entity note {
		text   text  required
		owner  user  = me
	}

	command note::create {
		permission signed_in
	}

	view mine per user {
		each note where owner == user.id {
			order -created_at
			text  created_at
		}
	}

	screen "Notes" / {
		table mine {
			text
			created_at "Written"
		}
		form note::create {
			text
		}
	}

} // namespace notes
END
```

Check it, then build it:

```sh
one check notes
one build notes
```

`one build` writes `notes/build/`: the React app in `web/`, the Go service in `api/`,
and the Firestore rules. The [overview](/language/overview) says what each part is,
and the [reference](/language/reference) covers every part of the language.

To deploy, add a `project` block saying where the project runs (its domain, its
Firebase project and its region), and `one build` also writes the Pulumi program and
`build/deploy`, which deploys everything to your Google Cloud project.

## The libraries

Generated projects are built on uione's libraries, and `one build` already names
them. To use them in code of your own:

```sh
# the React app's runtime, and the components it draws with
npm install @uione/react @uione/radix

# the Go backend's library, and the one its Pulumi program is built on
go get github.com/da0x/uione/one@v0.2.0
go get github.com/da0x/uione/infrastructure@v0.2.0

# the compiler in WebAssembly, and the editor the studio is built from
npm install @uione/compiler @uione/editor
```

## Your editor

Highlighting for Vim, Neovim and VS Code is in the repository's `editors/` folder.
For Vim:

```sh
mkdir -p ~/.vim/pack/uione/start
ln -s "$PWD/editors/vim" ~/.vim/pack/uione/start/uione
```

For VS Code:

```sh
editors/vscode/build
code --install-extension editors/vscode/uione-*.vsix
```
