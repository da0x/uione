# Targets

One project can be three apps. `target web desktop terminal` tells `one build` to
write, from the same screens, a web site, a desktop app and a terminal app. All three
read the same views and run the same commands on the same backend, so what's saved in
one shows up in the others as it's saved, without a refresh.

```one
project neotrac {
	target  web desktop terminal
}
```

With no `target`, a project is the web alone. `one build --target web` builds fewer
of the targets for one run, never others.

## One screen, three apps

[neotrac](https://neotrac.org)'s board is one screen in its `boards.one`:

```one
screen "{board_page.title}" /:project/boards/:board {
	heading {
		board::follow "Follow" icon follow when !(board_page.followers has me)
		board::unfollow "Stop following" icon following when board_page.followers has me
		issue::create "New issue" icon add form "Open issue" {
			title  body  assignees  milestone  priority  labels  attachments
		}
		link /:project/boards/:board/closed "Closed" icon archive
		link /:project/boards/:board/workflow "Workflow" icon workflow
		board::update "Edit board" icon edit form "Save" {
			title
		}
		board::delete "Delete board" icon delete form "Delete it for good" {
			typed "Its issues are deleted with it. Type its title to confirm"
		}
	}
	table board_page.issues by stage over board_page.stages link /:project/:issue {
		track milestone over board_page.milestones {
			due "Due"
		}
		search title labels stage.title priority
		reorder position
		page 25
		tint by priority
		number
		title
		labels
		assignees
		author "Opened by"
	}
	board board_page.issues by stage over board_page.stages link /:project/:issue {
		move issue::move along board_page.steps
		reorder position
		tint by priority
		title
		number  labels  assignees
	}
	link /:project "All boards"
}
```

A table and a board of the same issues are two ways of showing them, so every app
offers both, and these pictures show the board. Each opens full size.

On the web, in a browser:

[![neotrac's Website board in a browser: its buttons beside the title, and the issues in three columns, To do, In progress and Done, each card edged in its priority's color](/targets/web.png)](/targets/web.png)

On the desktop, in a window of its own:

[![The same board in neotrac's desktop app: the same title and buttons, Table and Board to pick from, and the same three columns of cards](/targets/desktop.png)](/targets/desktop.png)

In a terminal:

[![The same board in neotrac's terminal app: the title and buttons on one line, and each stage's issues listed under it, edged in their priority's color, with the keys at the bottom](/targets/terminal.png)](/targets/terminal.png)

## What each target writes

| Target | Where | What it is | Built with |
|---|---|---|---|
| `web` | `build/web` | a React app on `@uione/react`, drawn by a component set such as `@uione/radix` | `yarn build` |
| `desktop` | `build/desktop` | a C++26 app in a Wayland window, drawn with Vulkan by `one::desktop` | CMake and g++ 16 |
| `terminal` | `build/terminal` | a C++26 app drawn with ncurses by `one::terminal` | CMake and g++ 16 |

The desktop and terminal apps are written the same way. `src/main.cpp` holds the
theme, the sign-in and the screens; `src/screens/` holds a file for each `.one` file,
each screen a function that builds it from the parts its toolkit has. They're short,
because what a table, a form or a board does is in the toolkit, as it's in
`@uione/react` on the web, and nothing in them is edited by hand.

## What each target draws

Each target says, in the compiler, which screen elements and kinds of form field it
draws, so `one check` knows before anything's built. A screen using one a target
doesn't draw is an error on its line, for that target. Were the terminal app unable
to move a board's cards, neotrac's board would be:

```
boards.one:110:3: error: a board's move … along isn't drawn by the terminal app yet; take terminal out of target, or build with --target web,desktop
```

Today every target draws every element and every kind of field. A landing design is
drawn as a plain screen on the desktop and in a terminal. A `component` is written by
hand for each kind of app: `components/welcome_note.tsx` for the web, and
`components/welcome_note.cpp` for the desktop and the terminal, a function of the page
giving a view, written with the same parts the generated screens are. A project whose
targets need a file that isn't there gets an error saying which.

uione's gallery, `examples/gallery`, uses every element and every kind of field, and
is built for every target, so it's where each is seen side by side. What a target
can't draw yet would be listed in its `gaps` file, which its test holds to what's so;
a new target starts with everything there, and is done when it's empty.
`tools/gallery` in a checkout of uione runs it on your machine, with the Firebase
emulators, someone signed in and a room of pieces to look at.

## Building and running

The installer puts uione's libraries for the desktop and terminal apps in
`~/.local/share/uione/<version>`, beside the compiler, and the generated
`CMakeLists.txt` finds them there. Building needs g++ 16, CMake and pkg-config, with,
for the desktop, Wayland, `wayland-protocols`, Vulkan's headers and loader, `shaderc`,
HarfBuzz, FreeType, fontconfig and xkbcommon, and for the terminal, ncurses. Both
need Botan for TLS. On Arch:

```sh
sudo pacman -S --needed gcc cmake pkgconf wayland wayland-protocols vulkan-headers \
  vulkan-icd-loader shaderc harfbuzz freetype2 fontconfig libxkbcommon ncurses botan
```

Then build each app with CMake:

```sh
one build .
cmake -S build/desktop -B build/desktop/out
cmake --build build/desktop/out
build/desktop/out/neotrac

cmake -S build/terminal -B build/terminal/out
cmake --build build/terminal/out
build/terminal/out/neotrac
```

`cmake --install build/desktop/out --prefix ~/.local` installs it as the project's name
and its kind, like `neotrac-desktop`, with its license, and for a desktop app what
lists it among the system's apps, with the project's icon. `cmake --build
build/desktop/out --target package` makes the same as a download,
`neotrac-desktop-<version>-linux-<machine>.tar.gz`, unpacked with `tar -xzf` into a
prefix, its version named by `-DVERSION=…` or else the commit. The project's
`LICENSE`, beside its `.one` files or above them in its repository, goes with it, and
without one it isn't installed or packaged.

The program is named after the project. It opens on the front page, or on the
address it's given: `build/desktop/out/neotrac /tints/boards/website` opens that
board. Both are built and tested on Linux.

Each app reads its views live from the project's `domain`, or from `ONE_SERVER` when
it's set. A project that signs people in signs them in through the browser the first
time and keeps the sign-in, readable only by them, in `~/.config/<project>/credentials`
for the next; a service signs in with its key in `ONE_KEY`. To run against the Firebase
emulators, `ONE_FIREBASE_PROJECT`, `ONE_FIRESTORE_HOST` and `ONE_AUTH_HOST` name
them, as `tools/gallery` does.

The desktop app is used with the mouse and the keyboard, as the web is, and draws its
focus ring once the keyboard is used. The terminal app is used with the keyboard:
Tab moves, Enter opens and q quits. It draws sections and forms as panels with round
corners, titled in their border, as btop draws itself, and bars, like a slider or a
card's tally, in btop's blocks, and while it runs the
terminal's colors are the theme's dark ones, put back as it quits. With `NO_COLOR`
set, it keeps the terminal's.

`UIONE` builds against a checkout of uione instead of the installed libraries, for
working on uione itself: `UIONE=~/uione cmake -S build/desktop -B build/desktop/out`.
