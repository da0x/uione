# Start

uione is a language for writing a whole application feature in one short file, and
`one` is its compiler. This page builds a first project with it. The current release
is [0.7.1](/releases/v0-6-30).

## Install the compiler

```sh
curl -fsSL https://www.uione.io/install.sh | sh
```

[Install](/install) has the other ways: macOS, Docker, and building it from source.

## Your first project

A project is a folder of `.one` files. Make a folder for one:

```sh
mkdir notes
```

Then, in your editor, save this as `notes/main.one`. It's a whole feature: notes
that each person writes and only they can read.

```one
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
			order by created_at descending
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
go get github.com/da0x/uione/one@v0.7.1
go get github.com/da0x/uione/infrastructure@v0.7.1

# the compiler in WebAssembly, and the editor the studio is built from
npm install @uione/compiler @uione/editor
```
