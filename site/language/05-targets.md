# Targets

One project can be three apps. `target web desktop terminal` tells `one build` to
write, from the same screens, a web site, a desktop app and a terminal app. All three
read the same views and run the same commands on the same backend, so what's saved in
one shows up in the others as it's saved, without a refresh.

```one
project tracker {
	target  web desktop terminal
}
```

With no `target`, a project is the web alone. `one build --target web` builds fewer
of the targets for one run, never others.

## One screen, three apps

The gallery's board, in `examples/gallery/boards.one`, is one screen:

```one
screen "Boards" /rooms/:room/boards {
	board room_page.pieces by stage over room_page.stages link /rooms/:room/pieces/:piece {
		move piece::move along room_page.steps
		search title labels
		reorder position
		tint by condition
		track milestone over room_page.milestones {
			due "Opens"
		}
		title
		number  labels  assignees
	}
}
```

A room's pieces are cards in a column for each stage, moved along the room's steps,
found by their title and labels, edged in their condition's color, and picked by
milestone. Each picture opens full size.

On the web, in a browser:

[![The gallery's board in a browser: the room's pages beside it, its milestone, a search, and the pieces in three columns, Arrived, Hung and Stored, each card edged in its condition's color](/targets/web.png)](/targets/web.png)

On the desktop, in a window of its own:

[![The same board in the gallery's desktop app: the same pages, milestone and search, and the same three columns of cards](/targets/desktop.png)](/targets/desktop.png)

In a terminal:

[![The same board in the gallery's terminal app: the pages as links on one line, and each stage's pieces listed under it, edged in their condition's color, with the keys at the bottom](/targets/terminal.png)](/targets/terminal.png)

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
to move a board's cards, the gallery's board would be:

```
boards.one:13:4: error: a board's move … along isn't drawn by the terminal app yet; take terminal out of target, or build with --target web,desktop
```

The web and the desktop draw every element and every kind of field. A terminal draws
no pictures and has no screen of its own, so it can't show a picture on a card or a
table's row, crop a collage's picture, copy, save or keep a picture of a collage, or
take a screenshot, and it isn't written on in place, so it has no paper; everything
else it draws in its own way, a collage as a list of its layers. A landing design is drawn as a plain screen on the desktop and in a
terminal.

A drawing app's whole window is a screen like any other: `layout workspace` fills it
with a bar, a toolbar, the work, what's beside it and a status line, on every target.
A `component` is for what the language can't say yet, written by hand for each kind
of app: `components/welcome_note.tsx` for the web, and `components/welcome_note.cpp`
for the desktop and the terminal, a function of the page giving a view, written with
the same parts the generated screens are. One that's the whole app can fill the
window, with nothing of the site's bar, title or margins around it: `p.fill()` on the
desktop, and `useFill()` from `@uione/react` on the web. A project whose targets need
a file that isn't there gets an error saying which.

uione's gallery, `examples/gallery`, uses every element and every kind of field, and
is built for every target, so it's where each is seen side by side. What a target
can't draw yet is listed in its `gaps` file, which its test holds to what's so;
a new target starts with everything there, and is done when it's empty.
`tools/gallery` in a checkout of uione runs it on your machine, with the Firebase
emulators, someone signed in and a room of pieces to look at.

## Building and running

The installer puts uione's libraries for the desktop and terminal apps in
`~/.local/share/uione/<version>`, beside the compiler, and the generated
`CMakeLists.txt` finds them there. Building needs g++ 16, CMake and pkg-config, with,
for the desktop, Wayland, `wayland-protocols`, Vulkan's headers and loader, `shaderc`,
HarfBuzz, FreeType, fontconfig, xkbcommon and D-Bus, and for the terminal, ncurses. Both
need Botan for TLS. On Arch:

```sh
sudo pacman -S --needed gcc cmake pkgconf wayland wayland-protocols vulkan-headers \
  vulkan-icd-loader shaderc harfbuzz freetype2 fontconfig libxkbcommon dbus ncurses botan
```

Then build each app with CMake:

```sh
one build .
cmake -S build/desktop -B build/desktop/out
cmake --build build/desktop/out
build/desktop/out/tracker

cmake -S build/terminal -B build/terminal/out
cmake --build build/terminal/out
build/terminal/out/tracker
```

`cmake --install build/desktop/out --prefix ~/.local` installs it as the project's name
and its kind, like `tracker-desktop`, with its license, and for a desktop app what
lists it among the system's apps, with the project's icon. `cmake --build
build/desktop/out --target package` makes the same as a download,
`tracker-desktop-<version>-linux-<machine>.tar.gz`, unpacked with `tar -xzf` into a
prefix, its version named by `-DVERSION=…` or else the commit. The project's
`LICENSE`, beside its `.one` files or above them in its repository, goes with it, and
without one it isn't installed or packaged.

`one run .` takes these steps for you, and says each before it's taken: it builds
the project, then builds the first app the project targets and starts it, or the
one `--target` names. A desktop or terminal app is configured and built with CMake,
then started; the web app has its packages installed, with yarn, or npm when there's
no yarn, and is served by Vite at the address it prints, with Vite's own copy of the
packages made again each time, so a uione library changed since the last run is the
one served. Libraries taken from a checkout of uione, which yarn copies in, are
copied in again each time, `yarn install --force`, for the same reason. Closing the
app, or
Ctrl+C, stops it, as it would if it had been started by hand.

```sh
one run .                  # the first target, like the desktop app
one run . --target web     # the web app, at http://localhost:5173
```

The program is named after the project. It opens on the front page, or on the
address it's given: `build/desktop/out/tracker /tints/boards/website` opens that
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
