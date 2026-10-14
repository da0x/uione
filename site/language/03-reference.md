# Reference

A `.one` file is a list of declarations. Blocks use `{ }`, and a declaration with
nothing to add has no braces at all. There are no semicolons and no commas between
fields. Comments are `//` and `/* */`.

The examples below come from `examples/library`, a lending library.

## Names

Every name in a `.one` file is snake_case: lowercase words joined by underscores,
like `due_at` or `sort_title`. That's the only way to write them, and the compiler
rejects anything else.

That doesn't mean you have to read them that way. How names look on screen is the
reader's choice, so an editor can show `due_at` as `dueAt` or `DueAt` if that's what
you prefer, while the file itself stays snake_case. snake_case is the one style every
other style can be produced from without guessing, which is why it's the one that's
stored.

## Names and scope

A namespace is a module. Its entities, commands, views, roles and functions can be
spread over any of the project's `.one` files, and each sees all the others by name,
with nothing imported: the whole project is checked together, so a file is only how
the namespace is split up. A name from another namespace is always written with
that namespace, like `library::book`, so whatever comes from elsewhere says where,
right where it's used.

What a name means depends on where it's written:

- **In an entity**, a field's type is a [built-in type](#built-in-types), an entity
  of the same namespace, or another namespace's, qualified.
- **In a command**, the entity it's on is in scope: its fields by name, like
  `status`, or with the entity's name, like `report.status`, which mean the same.
  `id` is the entity's own id. A field that points at another entity reads that
  entity's fields through it: in `command report::create`,
  `project.takes_reports` is the takes_reports of the report's project. When a
  field is named for its type, as `project  project  required` is, writing
  `report.project.takes_reports` says plainly that it's the field.
- **In a view**, each row's fields are named by the entity it comes from, like
  `book.title`, and in `each book where ...` plainly. A view per entity names that
  entity, like `project.name`, and `me` is whoever is reading it.
- **Everywhere**, `me`, `now`, `none`, `true` and `false` are the language's own
  [values](#built-in-values). A choice is named with its enum, like
  `status::open`. `by` takes one of the language's own words: `anyone`, `anyone
  signed in` or `owner`.

A name that means nothing where it's written is an error that says what's in scope
there, and suggests the nearest name that is.

In the editor, resting the pointer on a name says what it means there, like
"field project of report, a project", and Ctrl or Cmd and a click, or F12, goes to
where it's declared, or to this reference for one of the language's own words.

## Indentation

Lines are indented with tabs, one per level, never with spaces. How wide a tab
looks is up to you, the same as how names look: your editor, or the code on this
site, draws it as deep as you like. Spaces after a line's first word, to line
things up, are fine.

## Declarations

Each declaration starts a line with the word for what it declares, then its name.

| Declaration | Declares |
|---|---|
| [`import`](#import) | a library whose names the files may use |
| [`project`](#project) | where the app runs and what it's built with |
| [`namespace`](#namespace) | a group of declarations, and the address its screens are at |
| [`enum`](#enum) | choices any field of its type can hold |
| [`entity`](#entity) | something the app keeps, with its fields |
| [`format`](#format) | a pattern a text field holds to |
| [`command`](#command) | a way to change an entity |
| [`view`](#view) | a document built ahead of time for a screen |
| [`role`](#role) | permissions a person can be given |
| [`define role`](#define-role) | a role each project starts with, kept as records its people edit |
| [`define workflow`](#define-workflow) | the rules an entity moves by: its stages, who works each, and the steps between them |
| [`define agent`](#define-agent) | a program that works stages by itself: takes what arrives, runs, and moves it on |
| [`define theme`](#define-theme) | how a site looks: its colors, light and dark, fonts, corners and depth |
| [`define service`](#define-service) | a system that runs commands, like GitHub, and the commands it may run |
| [`function`](#function) | a value worked out from others |
| [`screen`](#screen) | a page, at an address |
| [`picker`](#picker) | how an entity is chosen in a form |
| [`webhook`](#webhook) | what's done when another service sends an event |
| [`footer`](#footer) | what's at the foot of every page |
| [`once`](#once) | a change to what's stored, done once, the first time the backend starts with it |
| [`backend`](#backend) | Go written by hand beside what's generated |

## Comments

`//` starts a comment that runs to the end of the line, and `/* */` holds one that
spans lines. Comments are for people: the compiler reads past them.

## Strings

Text written as it is, in double quotes: `"Shelf"`, `"that book is not on the
shelf"`. In a screen's text, `{...}` shows a live value: `"{book_page.title}, by
{book_page.author}"`. A backslash writes a quote or a backslash inside one: `\"`.

A string ends on its own line. When it ends a line, the strings on the lines right
after it that hold nothing else go on with it, joined by a space, so long words fit
in a short line:

```one
text "Tell the project's maintainers about a problem privately."
	"Only they and you can read it."
```

## Numbers and operators

Numbers are written as they are, `20` or `1.5`. Values are compared with `==`, `!=`,
`<`, `<=`, `>` and `>=`, joined with `&&` (and) and `||` (or), and turned around with
`!`. `+`, `-` and `*` work them out, `=` gives a field a value, and `has` asks
whether a list holds something: `where labels has label.id`.

## Built-in values

Words for values that aren't written out:

- `now`, the moment a command runs;
- `me`, the person running it, and `me.username`, their GitHub username;
- `none`, no value, as an empty field holds;
- `true` and `false`;
- a time counted from now, like `10 minutes from now` or `2 days ago`, in minutes,
  hours, days or weeks: from the moment a command runs, or in a filter, from when
  it's opened.

## Keywords

The language's own words, inside what a declaration says:

- in a field: `enum`, `list of`, `serial per`;
- in a command: `require`, `by`, `dispatch`, `clear`, `changes`, `was`, `add … to`, `remove … from`,
  `input`, `each … where`, `each other … where`, `match`, `else`;
- in a view: `per`, `public`, `each`, `change`, `in`, `where`, `order`, `ascending`,
  `descending`, `limit`,
  `readers`, `public when`;
- in a role: `per`, `from`, and in a define role, `in` and `for services`; in a picker, `from`;
- in a define workflow: `by`, `worked by`, `steps`, `lease`, `notify`, `done at`, `depends on`, and `->` between two stages;
- in a define agent: `for`, and `on … in … run … then … else`;
- in a once: `each`, `where`;
- on a screen: `heading`, `subtitle`, `table`, `grid`, `diagram`, `board`, `cards`, `form`, a button's `form`, `confirm`, `component`, `hero`, `section`, `text`,
  `code`, `link`, `menu`, `markdown`, `hint`, `reorder`, `move … along`, `track … over` in a table or a board, and `by … and … over` in a grid, `by … over` in a board;
- in a project: the settings `one`'s library defines, below;
- in a footer: `bar`, `columns`, and in a link, `build.release` and `build.source`.

## import

`import one` lets a file use uione's own library, `one`, which says what a project
may say: the settings a project block takes, the type each holds, and the choices
an enum setting picks from. It's the only library there is, and a file with a
project block says it, at the top:

```one
import one

project shop {
	ui     radix
	theme  papercolor
}
```

The library is written in uione, in `compiler/library/one.one`, and is part of the
compiler, so it's always the one the project's `one` version came with. Its names
are in the namespace `one`, which a project can't declare itself. An editor reads
it to offer each setting as it's typed, with what it's for, and each choice of an
enum setting.

## project

One per project, naming where it runs and what it is built with.

```one
import one

project uione {
	domain          "uione.io"
	firebase        "uione-web"
	region          "us-east4"
	ui              radix
	signin          google github
	icon            "assets/icon.svg"
	serve           "public"
}
```

Each setting is a line, its name and then its value, and each is defined in the
library's `settings project`, with its type:

```one
namespace one {
	enum appearance {
		system  "As the system is"
		light   "Light"
		dark    "Dark"
	}

	settings project {
		// Where it's served, like "neotrac.org".
		domain      domain
		// The ways people sign in, several to a line, like signin google github.
		signin      list of signin
		// Whether it opens light or dark, rather than as the visitor's system is.
		appearance  appearance
	}
}
```

The value is checked by its type. An enum setting takes one of its choices,
written plainly, `appearance light`, since the type says which enum it is; it can
be written in full too, `appearance one::appearance::light`. A `domain` is a domain name,
a `slug` lowercase letters, digits and dashes, a `version` like "0.12.0", a
`theme` one that's declared, a `file` and a `folder` are next to the project's `.one` files,
an `address` is one of the site's own, starting with /, and a `link` is an https
address. A setting whose
type is another `settings`, like `redirect`, takes its fields in
order, `redirect "/old" "https://example.com/new"`. `domain` can name a second
domain after the first, `domain "uione.io" "www.uione.io"`: the same site answers
there, and sends whoever opens it to the first, so one project serves both. A setting
is said once,
unless its type is a `list of`, whose lines each add one, or one line several,
`signin google github`. A name the library
doesn't define isn't a setting, and the checker says so with the ones there are.

`one "0.4.0"` is the compiler the project is for, the last version it was checked
clean with. Any `one` run on the project hands the work to that version, fetching
it once from its release, so a project is always built by the compiler it was
written for, by everyone and by its deploys. `one upgrade` moves it to a newer
one: it applies the fixes that come with each change to the language, checks
again, and records the new version only once the project is clean, changing
nothing otherwise. `UIONE_TOOLCHAIN=local` keeps the `one` you ran.

`title "uione"` is the name at the top of every page and in the browser's tab, when
it isn't the project's own: the studio's project is `studio`, which names what it
runs on in Google Cloud, and its pages say uione. `ui` picks the component adapter
that renders every screen. `signin` names a way people sign in: `google`,
`github` or `microsoft`, several to a line, `signin google github`. With one, the Sign in button goes straight
to it; with more, it offers each, in the order they're written. A project that names
none signs no one in. Each way is turned on for the project in Firebase, which the
studio's checklist walks through. Someone who signs in one way and later another,
with the same email, has one account: the second way is added to it the first time
they sign in the way they did before. `icon` is the app's icon, an `.svg` file next to the project's
`.one` files: it's the page's icon in the browser, and it's shown beside the
app's name at the top of every page. `serve` names a folder whose files are served
as they are, at the site's root: `public/install.sh` is at `/install.sh`. `redirect "/install.sh"
"https://www.uione.io/install.sh"` sends whoever asks for an address that has
moved on to where it is now, and a project can have as many as it needs.

`target web desktop terminal` names the apps `one build` writes from the screens:
the web site in `build/web`, and a desktop app and a terminal app drawn from the same
screens in `build/desktop` and `build/terminal`. With none named, it's the web alone.
Each target draws the screen elements its toolkit has a part for, and says so in the
compiler, so a screen using one a target doesn't draw is an error, on its line, for
that target: "a board's move … along isn't drawn by the terminal app yet; take
terminal out of target, or build with --target web,desktop". `one build --target web`
builds fewer of the targets for one run, never others. How a form asks for each
field is part of it too: a text box, a slider, radio buttons, a checkbox, a file to
attach, each a kind every target draws or is an error. The desktop and terminal apps
draw every element, a `component` from its `.cpp` file, and a landing design as a
plain screen. uione's own gallery,
`examples/gallery`, uses every element and every kind of field, built for every
target, so it's where each is seen side by side; what a target can't draw yet is in
its `gaps`, which its test holds to what's so, and a new target starts with all of it
there. Each is a CMake project of
C++26: `cmake -S build/desktop -B build/desktop/out` and `cmake --build
build/desktop/out` build it with g++ against uione's libraries, installed with the
compiler, or a checkout of uione named by `UIONE`. It reads its views live from the
project's `domain`, or `ONE_SERVER`, and
signs in through the browser the first time, keeping the sign-in for the next. The
terminal app is drawn as btop draws itself: sections and forms are panels with round
corners, titled in their border, and while it runs the terminal's own colors are the
theme's dark ones, its page behind everything and its accent, danger, warning and
success for what they mean, put back as it quits; with `NO_COLOR` set it keeps the
terminal's. [Targets](/language/targets) shows one screen in all three, and how to
build and run each.

`theme papercolor` gives the site a [theme](#define-theme): its colors, light and
dark, its fonts, its corners and its depth. It's uione's own `harbor` unless it
says, and uione's `papercolor` is a warm gray page with graphite text, and panels
that sit flat on it. A site's own theme is declared beside it, and usually starts
from one of these.

`appearance light` opens the site light, whatever the visitor's system prefers, and
`appearance dark` opens it dark; without it, or with `appearance system`, it
follows the system. Either way, a visitor who picks the other with the toggle at the
top of the page keeps their pick.

`accessibility menu` puts a menu of display settings beside light and dark, at the
top of every page, for readers who need the page drawn otherwise: more contrast,
text up to twice as large, more space between lines, letters and words, a legible
font, less motion, solid panels, every link underlined, and a thicker focus ring.
Each is kept in the reader's browser, and Back to my system's undoes them all. Every
site, with the menu or without, follows a reader's system when it asks for more
contrast or less motion, and keeps the colors a system picks for itself, like
Windows' contrast themes. More contrast is the site's own theme, worked out from it:
text black or white, quieter text, links and the rest at 7:1, and lines at 3:1. The
menu changes how the page is drawn, and nothing else: it isn't an overlay, and it
doesn't stand in for a screen reader or a magnifier.

`analytics google` counts the site's visitors with Firebase Analytics, which is
Google Analytics underneath, once its Firebase project is linked to a Google
Analytics account of the owner's. It records each screen as it's opened. A visitor
is asked once whether it may use cookies, and until they agree, they're counted
without them, as Google's consent mode does; their answer is kept in their browser.
A project without the setting counts no one and asks no one.

### environment

A project can run in more than one place, each deployed on its own: production
for everyone, staging to try a change first. Each `environment` is one of them,
with what's its own there: `domain`, `firebase` and `region`. Each is its own
Firebase project, so their data never mixes. Settings outside the environments are
shared by all of them, and one inside takes the place of a shared one. Which
settings an environment takes is the library's `settings environment`.

```one
import one

project shop {
	region  "us-east4"
	ui      radix
	environment production {
		domain    "shop.example"
		firebase  "shop-production"
	}
	environment staging {
		domain    "staging.shop.example"
		firebase  "shop-staging"
		region    "europe-west1"
	}
}
```

`one build shop --for staging` builds for staging, and its `deploy` deploys
there, as a Pulumi stack named after its Google Cloud project, `shop-staging`. An
environment is where it runs, whatever it's called, so renaming one keeps its stack
and everything it made. Without `--for`, a project is built for its first
environment, and says so. Every environment needs a domain, a Firebase project and a
region, its own or shared, and each its own Google Cloud project; anything else is
the same everywhere, so it goes outside them.

## namespace

Groups related declarations. A namespace is also the route prefix and the
navigation group of the screens inside it, so `screen "Shelf" /shelf` inside
`namespace library` is served at `/library/shelf`. A screen outside any namespace
is at the root.

`namespace studio at /` puts the namespace's screens somewhere else, here at the
site's root, so its `/:owner/:project` screen is at `/da0x/neotrac` rather than
`/studio/da0x/neotrac`. A namespace written in several files says where it is in
one of them, and every file's screens are there.

`::` reaches into a namespace or an entity: `waitlist::signup::create`,
`library::book`.

What's inside a namespace is indented, like the inside of any other block, so it's
plain at a glance which declarations belong to it:

```one
namespace library {

	entity book {
		title  text  required
	}

	command book::create

} // namespace library
```

## enum

Choices named once, each with how it's shown, for any field whose type it is.
Each choice is on a line of its own, or several to a line.

```one
enum status {
	open         "Open"
	in_progress  "In progress"
	done         "Done"
}

entity issue {
	status  status = status::open
}
```

- A choice is written with the enum's name, `status::open`, whatever the field is
  called.
- What a field starts as is the field's to say, with `=`, as for any field, so two
  fields of one enum can start differently.
- An enum used by one field alone can be written on it, in a block or, when it's
  short, on one line: `visibility  enum { public  private } = visibility::public`,
  or `visibility  enum  public | private = visibility::public`.

## entity

The data a feature owns. Each line is a field: a name, a type, then its rules.

```one
entity book {
	title      text       required
	author     text       required
	shelfmark  shelfmark  required  unique  key
	status     enum       on_shelf | lent | withdrawn = status::on_shelf
}

entity loan {
	book         book    required  key
	number       serial  per book  key
	member       user    required
	lent_at      date    = now
	due_at       date    required  after lent_at
	returned_at  date
}
```

- `readers member` and `public when visibility == visibility::public`, lines of an
  entity's own, say who may read the views of it and of what's within it: a view
  per project, or per an issue whose one field points at its project, reads as
  the project says, with `visibility` read as `issue.project.visibility`. One
  within two that say who reads, like a card on a board in a project, is read as
  both say: by the same readers, and public only while both are; two that name
  different readers are a mistake until the view says its own. A view that says
  its own readers says all of who may read it, and inherits nothing. A field
  can't be called `readers`.
- Every field has a type, so what it holds is never worked out from how it's used.
- A field's type is one of the [built-in types](#built-in-types), a `format`
  declared in the file, or another entity (`book`), which stores its id and reads
  through it (`book.title`).
- An enum's choices are always written with its name, the [enum](#enum)'s or, for
  one written on the field, the field's:
  `status::on_shelf`, never `on_shelf` alone, wherever one is used, as a starting
  value, in a command, a comparison, a view's condition or what a command creates.
  So a choice reads as what it is, and two enums that share a choice, like an
  issue's `status::open` and a report's, are never mixed up. A choice may say how
  it's shown: `license  enum mit "MIT" | apache_2_0 "Apache-2.0"`.
- `user` is a person, stored as their id. A view can show their name and picture
  (`member.name`, `member.picture`), which come from how they signed in and are
  kept up to date each time they do something, and, for someone who signed in with
  GitHub, their username (`member.username`), like da0x. Nothing else about them,
  such as their email address, can reach a view.
- `list of label`, `list of user` or `list of text` holds several: labels, the
  people assigned, tags. A view can read through each, as `assignees.name`, and
  pick by what a list holds, as `where assignees has me`. A form writes a list
  separated by commas, and a table shows it that way.
- `serial` is a number counted up as each entity is made: 1, 2, 3. `serial per book`
  counts within each book, so every book's loans start from 1. Two made at the same
  moment never get the same number, and nobody types one: a form can't ask for it
  and a command can't set it.
- Rules: `required`, `unique`, `after <field>`, and `key`.
- `key` makes the field the entity's id. A book with shelf mark HIS-0142 is
  `HIS-0142`, and its page is `/books/HIS-0142`. With several key fields their
  values are joined by dashes, so the third loan of that book is `HIS-0142-3`.
  A key is never changed once the entity is made. An address can name an entity
  by its key's parts rather than its id: a project keyed `owner slug` is at
  `/:owner/:project`, so `/da0x/neotrac`, with a parameter named after each part
  but the last, and the last named after the entity.
- Making an entity whose key is already taken updates it, which suits a waitlist
  where signing up twice is still one signup. `unique key` refuses the second one
  instead, with "Shelfmark is already taken".
- `entity issue history { ... }` keeps every change to an issue: which field, what
  it was before and after, the command that changed it, and who ran it and when.
  Making an issue is one change with no field. The changes are kept in the same
  step as the change itself, so none is ever missed or made up.
- `entity comment history of issue { ... }` keeps each change to a comment in its
  issue's history instead, through its field pointing at the issue: "Ada added a
  comment to #12". The entry holds what the issue points at, like its board, and
  what the comment points at, like the people it mentions, so a person's news can
  pick it by either.
- `= value` sets the value a new entity starts with. `me` is the person running the
  command, `me.username` is their GitHub username, `now` is the time it runs, and
  `none` is no value. A field that starts as `me` or `me.username` always does, so
  nobody can make something in another's name.
- `name  text  required  key = slug(title)` makes a name from another field when
  none is given: a stage titled In review is named `in_review`, so a form asks only
  for the title. A second stage whose title makes the same name is refused rather
  than taken for the first.
- The name follows the field it's made from. Renaming the stage to Reviewing names
  it `reviewing`, and an update form may ask for the name itself, to name it
  something else. Either way it moves to the id its new name makes, and in the same
  step:
  - everything pointing at it points at the new id, like its issues and a board's
    start;
  - what's named by it moves too, as a stage's steps do. Renaming a board moves its
    stages and their steps;
  - its history goes with it, and the changes that name it say its new title;
  - counters kept per it, like a board's issue numbers, go on from where they were.

  Its old title is free for another stage, and its old address still opens it: its
  page leads to the new one, in the web, desktop and terminal apps, and the web
  app's address changes to the new name. Roles and services keep the names they're
  made with: a role is found by its name, and a service is who it is by its own.
- `entity invitation invites member { ... }` makes an invitation to become a
  member, by email. When someone opens the app signed in with that email, a member
  is made from the invitation's fields of the same names, like its project and
  role, with the member's one person field, the user it doesn't start as a value,
  the one signing in, and the invitation is deleted, both at once. From then on
  they're a member by who they are, not by their email, so changing it later
  changes nothing. Only an email someone is known to have takes one: the one their
  sign-in vouches for, like Google's, or, for a GitHub account, any address GitHub
  has verified, asked of GitHub with the access given when signing in, which is
  checked to be that account's. The email is matched whatever its capitals. The invitation has one email
  field, and everything else the member needs, by the same name and kind.

```one
entity invitation invites member {
	project  project  required  key
	email    email    required  key
	role     role     required
}
```

- `on signin { ... }` is done each time someone opens the app signed in, as them:
  its steps are a once's, `each invitation where email == me.email { ... }`, and
  `delete each invitation where email == me.email`, with `me.email` the email their
  sign-in vouches for, and `me` the person. An invitation is the usual reason, and
  `invites` says it whole.
- `mentioned  list of user = mentions(body)` holds the people a text names with
  @username, like @da0x, worked out each time the text is written. A name nobody
  has is left out, and so is an email address.

## Built-in types

What a field can hold, written after its name: `title  text  required`.

| Type | Holds | Shown as |
|---|---|---|
| `text` | a line of text | a text box |
| `markdown` | text written in Markdown, which can't add markup or scripts to a page | a box to write in, and rendered where it's shown |
| `email` | an email address, checked as one and compared without case | an email box |
| `slug` | a name like `my-app`: lowercase letters and digits joined by single dashes, lowered as it's saved | a text box |
| `date` | a moment, to the second | a date |
| `number` | a number, whole or not; `from 0 to 100  by 5` keeps it within a range, in steps | a number box, or a slider for a number with a range |
| `serial` | a whole number counted up as each entity is made, 1, 2, 3, and never typed; `serial per project` counts within each project | a number |
| `boolean` | true or false | a checkbox |
| `user` | a person who has signed in, by their id; a view can show their name, picture and username | their name |
| `file` | a file someone uploaded, kept in the app's bucket, by its path, whose last part is its name; `list of file` holds several (see [Files](#files)) | a box to choose a file, uploaded as it's chosen; where it's shown, a link that opens it, and a small picture of an image |
| `data` | named values, each text, a number, or yes or no, up to 64; what a command sends is merged in, and a null takes a name out | not asked for in forms; a workflow keeps what its holder reports in one, which a board shows as a dot and a running time |
| `list of permission` | the commands a project's role allows, like `issue::create` | a box to tick for each command |
| `list of …` | several of a type: `list of text`, `list of user`, `list of label` | each in turn |
| `enum` | one of the choices it names, each written with its name: `status  enum open \| closed = status::open` | radio buttons for three or fewer, otherwise a list that opens |

A field's type can also be another entity, holding its id (`book`), or a `format`.

## Files

A field of type `file` holds a file, and `list of file` several:

```one
entity order {
	title     text  required
	proof     file
	pictures  list of file
}
```

- **Uploading:** a form's file box uploads a file as soon as it's chosen, showing
  how far it's come, and the form is sent once every file has arrived. The bytes
  go straight to the app's bucket, never through the backend: the backend gives
  whoever is signed in somewhere to upload one file, for ten minutes, at
  `POST /api/files`. A file holds at most 5 GB. In the desktop and terminal apps,
  the box is where the file is on that machine, typed or pasted from a file
  manager, `~/` for home and one a line for several; they're uploaded as the form
  is sent, and a list keeps what it held and adds them.
- **Attaching:** a command that puts the file's path in a field attaches it to
  that record, in the command's transaction. It has to be an upload of the person
  running the command, and not attached to anything else; otherwise the command
  says so, like "Proof is a file someone else uploaded".
- **Reading:** whoever may read the record may read its files: anyone when a page
  of it is public, or its readers, like a project's members, people and services
  alike. A record with a page of its own is read only as that page says, never
  through what it points at. A record without one, like a comment, is read by
  whoever reads every page of what it points at, so a comment on a private
  board's issue isn't read through its public project. `GET /api/files?path=…` gives
  somewhere to read it from, for ten minutes.
- **Deleting:** a file taken out of its field, or whose record is deleted, is
  deleted too, once the command is done. An upload never attached is deleted a day
  on.
- **Where:** a deploy makes a bucket for the project, `<firebase>-uione-files`, in
  its region, that only its backend reaches, and leaves it, and the files in it,
  if the stack is ever destroyed. The Storage emulator stands in for it locally.
- **From a program:** the generated C++ client's `upload(path)` uploads a file
  from disk and gives the path to put in a field, `download(path, to)` saves one,
  and `file_address(path)` says where to read it from. Each reads a file whole, so
  it has to fit in memory.
- A file can't be a key, unique, after another field, or start with a value.

## Field rules

What a field must be, written after its type: `shelfmark  shelfmark  required  unique  key`.

- `required`: it always has a value; a form can't leave it empty.
- `unique`: no two of the entity have the same one.
- `key`: it names the entity, so its id is made from its key fields, like a book's
  shelfmark, or a project's owner and slug.
- `after lent_at`: a date that has to come after another field's.
- `from 0 to 100  by 5`: a number that has to be within the range, in steps of
  `by`, 1 unless it says, counted from `from`. A form asks for it with a slider,
  and a new one starts at `from` unless it says, like `= 50`. The backend refuses
  one outside it: "Volume is from 0 to 100", "Volume goes in steps of 5 from 0".
- `= value`: what it starts as, like `= now`, `= me` or `= status::open`.
- `= board.project`: worked out when it's made, from what another of its fields
  points at, like a step's project from its board's, read in the same transaction,
  even when the board is made in the same command. It's never given: a command or
  a dispatch that sets it is an error.
- `= [role::maintainer, role::contributor]`: a list of roles starts as those every
  project starts with, each the one in the record's own project.
- `"#"`, before any `= value`: what every screen calls it, like `number  serial  per
  project  key  "#"`, unless a screen says its own.

## format

A text shape with its own ordering and display, declared once and used as a type.

```one
format shelfmark AAA-9999 {
	example HIS-0142
}
```

## command

A way to change an entity, named on it. A command with no body creates, updates or
deletes with the entity's own rules. A command is the only way anything stored is
made, changed or deleted, because a command is where everything that has to happen
with it is said.

```one
command book::create
command book::update

command loan::checkin {
	require returned_at == none  "that book is already back"
	returned_at = now
	dispatch book::update { id = book  status = status::on_shelf }
}
```

- `require` states a precondition and the message shown when it fails. A long one
  puts its message first and its conditions in a block, one a line, that all hold,
  as if each line were joined to the next with `&&`; a line can still use `||`
  within itself. `where` and `public when` take the same block, as `when` does.
- A command's body sets its own entity's fields, like `returned_at = now`. Anything
  else, like the book a loan points at, is changed by its own command, run with
  `dispatch`: `dispatch book::update { id = book  status = status::on_shelf }`.
  What it dispatches runs in the same step, as the same person, its body and its
  `require`s too, and both changes are made together or not at all. Its
  permission isn't asked again, since the command dispatching it was allowed.
- `by` says who runs a command that no role allows: `by anyone` means no
  sign-in, `by anyone signed in` means anyone signed in, any way the project
  offers, and `by owner` means the person in the entity's `owner` field.
- `add me to assignees` and `remove me from assignees` change a list. Adding what's
  already there, or removing what isn't, changes nothing. A list is never given a
  whole new value with `=`.
- An update changes only the fields its forms ask for, so whoever may edit an
  issue's title can't also set its status by sending it. `changes cloud_project
  deploy_account` adds fields it may change besides, like those a hand-written
  component sends. An update with neither may change any field but its keys.
  Another command, like a move, takes only the fields its `changes` and its forms
  name: `changes stage` takes the stage an issue moves to.
- `was issue.stage` is what a field held before the command changed it.
- `exists(step where from == was issue.stage && to == issue.stage)` asks whether
  there's one, in a `require`. Inside it, the entity's own fields are named
  plainly, and the command's entity by its name. `held(roles)` there asks whether
  the person holds one of a list of the project's [roles](#define-role), so who may take
  a step is the project's to say:

```one
command issue::move {
	changes stage
	require "your role doesn't move an issue from there to there" {
		exists(step where from == was issue.stage && to == issue.stage && held(roles))
	}
}
```
- `held(stage.worked_by)` asks the same of the roles listed on what the command's
  entity points at, read once in the command's own transaction. A list is `none`
  when it holds nothing, so taking an issue nobody else has is:

```one
command issue::claim {
	require held(stage.worked_by)  "this stage isn't worked by a role you hold"
	require assignees == none || assignees has me  "someone else has taken it"
	add me to assignees
}
```
- `if workflow == workflow::kanban { ... }` does what's inside only when its
  condition holds, so a project can start from a preset it picks.
- In a `dispatch stage::create`, `stage::triaged` is the project's stage named
  triaged: one of what the command makes, or a role every project starts with,
  named by the project and its name. A list field is given its values whole: `roles = [role::maintainer,
  role::programmer]`, or a role's `may = [issue::create, issue::move]`.
  A long list goes on over lines until its `]`, and may end in a comma.
- `dispatch board::create { project = id  title = name }` makes a board as
  `board::create` makes one: its body runs too, like the stages of the preset a
  board starts from, so a project made with a first board gets that board's stages
  with it. Every required field gets a value, unless it starts with one, and what's
  made gets an id of its own.
- An update, a delete, or another command like a move, says which it acts on with
  `id = ...`, or goes in an `each` over its entity, where it acts on each row. It's
  sent its command's inputs by name, like `into`, and a list is changed with `add
  ... to` and `remove ... from` inside its braces. Deleting what's already being
  deleted in the same step does nothing, so two links that delete each other stop.
- What's deleted takes its own history with it, and the numbers counted for it,
  like a project's issue numbers, so whatever's made again under its id starts
  afresh and never shows what came before. Deleting the entity a
  [define role](#define-role) is `in`, like a project, also deletes what the
  language made for it: its roles, the members that give its people their roles,
  and its services with their keys. A service's profile stays, so what it did
  is still named. Deleting a whole project is a command like any other, which
  dispatches the deletes of what's in it:

```one
command project::delete {
	input address text
	require address == slug  "type the project's address to delete it"
	each issue where project == id {
		dispatch issue::delete
	}
	each board where project == id {
		dispatch board::delete
	}
}
```
- A once may give what was made before a key existed its key, like the board a
  project's stages were in before projects had boards.
- `input into stage` is something a command is sent besides its entity's fields,
  like the stage a removed stage's issues move to. Its forms ask for it, picked as a
  field of its type would be, its body names it, and it's never stored.
- `each issue where stage == id { ... }` runs what's inside for each entity it
  picks, by a field's value, or by any of several, like `from == id || to == id`.
  Each is changed by a dispatch of its own command, and kept in its history as
  changed.

```one
command stage::delete {
	input into stage
	require into != id  "pick another stage for its issues"
	each issue where stage == id {
		dispatch issue::update { stage = into }
	}
	each step where from == id || to == id {
		dispatch step::delete
	}
}
```

- `each other link where other.pair == id || other.id == pair` picks rows of the
  command's own kind. They're named `other`, and their fields `other.pair`, so a
  bare `pair` or `id` is always this one's and the two sides don't read the same.
  An `each` over the command's own kind says `other`, and no other `each` does.

```one
command link::delete {
	each other link where other.pair == id || other.id == pair {
		dispatch link::delete
	}
}
```

- `match relation { … }` is a value picked by a choice of an enum: a line for each
  choice, the choice then its value, and `else` last for any it doesn't name.
  Every choice is said, or there's an `else`, so a choice added to the enum later
  is an error until each match says what it gives. Given to a field that's an
  enum, its values are that enum's choices, written bare. A match works in a
  command's body.

```one
command link::create {
	if pair == none {
		dispatch link::create {
			project = project  pair = id  from = to  to = from
			relation = match relation {
				blocks         blocked_by
				blocked_by     blocks
				duplicates     duplicated_by
				duplicated_by  duplicates
				relates        relates
			}
		}
	}
}
```

- Values given in a dispatch are worked out where the command runs. `id` is the id
  of the command's own entity, and `me` is the person running it.

```one
command project::create {
	by anyone signed in
	dispatch member::create {
		project = id  person = me  role = role::maintainer
	}
}
```

## view

A document built ahead of time for a screen. A view with `each` has one row per
entity; one without is a single set of values. A row holds exactly what its block
lists, so a view never sends a field by accident: a field of its own (`due_at`), a
field of an entity it points at (`book.title`), or a value worked out for it
(`lent_to = ...`).

```one
view desk {
	each loan where returned_at == none {
		order by due_at
		book.title  member  due_at
	}
}

view signups public {
	total = count(signup)
}

view book_page per book {
	title  author  shelf.name
	loans = each loan in book {
		order by number descending
		number  member  lent_at  returned_at
	}
}
```

- `per <entity>` makes one document per entity, like a page per book. Its fields
  are said bare, as a list's rows are: `title  author` puts its title and author
  in the document, and `shelf.name` the name of what it points at, which renaming
  the shelf changes on every book's page; a screen reads it as
  `{book_page.shelf.name}`. A value named otherwise says what it is, like
  `lifecycle = issue.project.lifecycle`. Inside the view, that entity is named
  plainly, and `each loan in
  book` picks the rows that belong to it: those whose one field pointing at a book
  holds this one. An entity that points at a book by two fields, like a link's
  `from` and `to`, says which with `where from == book.id`. A screen
  showing it reads it for the entity its address names, so its route has that
  entity as a parameter: `screen "Book" /books/:book`. `per user` makes one
  document per person, readable only by that person.
- `total = count(signup)` counts what a view's query picks, and `seen =
  first(reader where person == me).seen_at` holds a field of the earliest it
  picks, or none, like when the person reading last looked at their news.
- `unread = count(changes where created_at > seen)` counts the rows of the view's
  own list `changes` that pass: each test compares a field its rows hold with
  another of the view's values, joined by `&&`. Nothing comes before everything, so
  before someone has ever looked, all of it counts. It counts what the list holds,
  after its `limit`.
- `public` lets anyone read it, signed in or not.
- In a view per entity, `readers member` lets the people a member names read each
  document: everyone with a role in the project the entity is held within. `public
  when project.visibility == visibility::public` opens a document to everyone while the
  condition holds; it compares a field of the entity, or of what it points at,
  through as many pointers as it takes, like `issue.board.project.visibility`, with
  a value. In a block, one a line, every one has to hold, so a private board in a
  public project stays closed. When people join a project, or something a
  condition reads through changes, like the board turning private, only the
  documents that read through it are rebuilt.
- Another `readers` line names people in a field of the view's entity:
  `readers report.author` lets whoever filed a report read it, even outside the
  project, and `readers report.watchers` lets each person a list of them names.
  A view can have these without `readers member`, and then only those people read
  it.

```one
view issue_page per issue {
	readers member
	public when issue.project.visibility == visibility::public
	title = issue.title
}
```

- `each change in issue` lists the changes issues keep. Each holds the issue,
  under `issue`, and what it points at, so in a view per issue it lists that
  issue's own changes, and `each change in issue in project` every change to a
  project's issues. A change's rows can show `field`, `before`, `after`, `action`,
  `created_at` and `created_by.name`, and `created_by != me` leaves out the
  person's own.
- A condition can read through what each row points at: `board.followers has
  me` picks the changes to issues on the boards the person follows. `||` joins
  ways to be picked, and a row any of them picks is listed once.
- A list in a view `per user` holds only what the person may read: a row whose own
  page, like an issue's, is private to a project they aren't in, or no longer in,
  is left out, and comes back when they join it or it turns public.
- `limit 50` keeps only the first rows of a list, once ordered, so a list that keeps
  growing, like a project's timeline, stays small enough to be one document. A list
  kept to its newest rows, ordered by `created_at descending` or not at all, reads
  only those from the database, however long its history, with an index for each
  way it's picked by that `one build` writes to `firestore.indexes.json` and the
  deploy creates. Until an index is built, the list is read whole.

```one
view news per user {
	changes = each change in issue where {
		board.followers has me || assignees has me
		created_by != me
	} {
		order by created_at descending
		limit 30
		project  issue  issue.number  issue.title  field  before  after  created_by.name  created_at
	}
}

view project_page per project {
	timeline = each change in issue in project {
		order by created_at descending
		limit 50
		issue  field  after  created_by.name  created_at
	}
}
```

- A list of every project would name private ones too, so it picks the public
  ones: `each project where visibility == visibility::public`.
- `order by` goes inside the list it sorts. Each key is `ascending`, which it is
  unless it says, or `descending`, newest or largest first:
  `order by done  created_at descending` puts the ones not done first, and the newest
  first within each. A key can be a field of what each row points at, like
  `order by from.position`, when the list shows it too.
- A view has at most one list without a name, its rows. Any others have names,
  like `loans = each loan ...`, so one page can hold a book's details and several
  lists that belong to it. Each item in a list is its own entity, so a long list
  never makes one item too big to store. A table names the list it shows:
  `table book_page.loans`.
- In a view per entity, a list can be picked by a field of that entity:
  `steps = each step where board == issue.board` in a view per issue lists the
  steps of the issue's board, and is rebuilt when any of them changes.
- A view is rebuilt whenever an entity it reads changes: `desk` reads books through
  `book.title`, so renaming a book updates the desk.

## role

A default role and the permissions it holds. A person is given a role everywhere,
in `users/{id}.role_id`.

```one
role librarian  book::view  book::create  book::update  book::withdraw
```

- A role given everywhere is never held within something; a role each project has
  its own of is a [define role](#define-role).

## define role

A role each project starts with, and the commands it allows, one a line. The
project keeps its roles as records its people edit, rather than roles written in
code: it changes what a role allows, or adds a role of its own, in its settings,
with no deploy.

```one
// Runs the project and its settings.
define role maintainer "Maintainer" in project {
	project::update
	member::create
	role::create
	role::update
	issue::create
	issue::close
}

// Works on the project's issues.
define role reporter "Reporter" in project {
	issue::create
	comment::create
}

command project::create {
	by anyone signed in
	dispatch member::create {
		project = id  person = me  role = role::maintainer
	}
}
```

- `in project` says what each has its own roles of. The language keeps them as
  records of an entity it declares, `role`, named by the project and its name,
  with a title and the commands it allows, `may`, and gives them to people with
  records of another, `member`:

```one
entity role {
	project  project  required  key
	name     text     required  key = slug(title)
	title    text     required
	may      list of permission
}

entity member {
	project  project  required  key
	person   user     required  key
	role     role     required  key
}
```

- A member gives its person the role it points at, in its project, and a person
  may hold several. What the role allows now is what they may do there, on the
  project and on everything held within it: what points at it, like an issue, and
  what points at that, like an issue's comment. A role allows only commands on
  those. Changing a role changes what everyone who holds it may do, at once, and
  their pages show it.
- Screens, views and commands name `role` and `member` like any entity of the
  project's own: `each member in project`, `dispatch member::create`, `readers
  member`. Their commands, like `member::create` and `role::update`, are declared
  as any command is.
- Every command is allowed by a role, or says who runs it with `by`, like
  `project::create`, which no role in a project can allow before there is one.
  One left out is a mistake, and so is a role allowing one that says `by`.
- These are the roles each project starts with, made with it. `role::maintainer`
  is the project's maintainer role, as in what `project::create` makes. A command
  added to a role later is added to that role in every project at the next deploy.
- A form for a role ticks the commands it allows; a form for a member picks its
  role from a list of the project's roles that a view on the screen holds, like
  `roles = each role in project { name  title }`.

### for services

A role a project's services may hold as well as its people: programs, like an AI
agent or a script, that work beside them.

```one
// Works the issues in the stages its role works: takes one, comments, moves it on.
define role agent "Agent" in project for services {
	issue::claim
	issue::release
	issue::move
	comment::create
}

command service::create
command service::key
command service::revoke
```

- A role for services brings the project's services, records of an entity the
  language declares, `service`, named by its project and its title, holding one
  role made for services, with what's shown of its key:

```one
entity service history {
	project    project  required  key
	name       text     required  key = slug(title)
	title      text     required
	role       role     required
	key_start  text
	key_made   date
	key_used   date
}
```

- A service is a member of its project, as a person is: it reads what members
  read, and runs what its role allows, there. It runs nothing else: no command that
  says `by anyone` or `by anyone signed in`, nothing in another project, and nothing
  that changes who has access, roles, members, services or invitations, whatever a
  role's record is edited to say. A role for services that allows one of those is a
  mistake.
- `service::key` gives it a new key, which replaces any it had and is shown once,
  to copy; only its first characters are kept to show. `service::revoke` takes it
  away. Both are declared as any command is, with no body.
- A service sends its key as `Authorization: Bearer one_…` to run commands, at
  `POST /api/<namespace>/<entity>/<action>` as a page does, and asks who it is at
  `GET /api/me`. At `POST /api/token` the key trades for a Firebase sign-in of the
  service's own, which reads views live under the same rules as a person's.
  A revoked key is refused at once.
- A service's profile is its title, marked as a service: `created_by.service` and
  `author.service` say so in a view. It has no username, so no `@mention` names it.
- A project with a role for services lets its backend sign their sign-ins as
  itself, which its deploy sets up.

### A program signing in as a person

An app people sign in to also lets a program on their computer, like its command,
sign in as them, through their browser, the way native apps do with OAuth:

1. The program listens on `127.0.0.1` and opens the app's `/authorize` page with
   the port, a state, and a PKCE challenge.
2. The person signs in there as they always do, and presses **Authorize**. The page
   asks the backend at `POST /api/authorize` for a code bound to the challenge, and
   sends the browser back to the program with it. **Cancel** sends it back with
   `error=denied`.
3. The program trades the code, with the verifier only it knows, at `POST
   /api/token` for a Firebase sign-in of the person's own. A code is good once, for
   five minutes, and only its hash is kept.

The generated C++ client does all of it with `sign_in_with_browser(open)`, where
`open` shows the address, and keeps the refresh token the program stores.

## define workflow

The rules an entity moves by: the stages it goes through, which roles work each,
and the steps from one to another. Whoever works a stage takes what's in it for a
while, its lease, so two never work the same thing at once. Its stages are records,
so each project makes its own, or [written in the code](#stages-written-in-the-code)
when every project works the same ones. Here a print shop's orders move from
proofing to press to shipping:

```one
entity stage {
	shop       shop  required  key
	name       text  required  key = slug(title)
	title      text  required
	// The roles whose people and services work its orders.
	worked_by  list of role
}

// From one stage to another, for the roles it names.
entity step {
	shop   shop   required  key
	from   stage  required  key
	to     stage  required  key
	roles  list of role
}

entity order {
	shop   shop   required
	title  text   required
	stage  stage
}

define workflow order by stage {
	worked by worked_by
	steps step
	lease 30 minutes
}
```

- `by` names the field it moves along, pointing at its stages; `worked by` names
  the stage's field listing the roles that work it; `steps` names the entity whose
  records go `from` one stage `to` another for the `roles` they list. `lease` says
  how long a claim lasts, in minutes or hours, unless it's renewed; it's 10 minutes
  when it isn't said.
- The workflow adds to the entity who holds it and until when, what its holder
  says of it, and the commands that work it, said in full as a project could write
  them:

```one
	held_by     user
	held_until  date
	data        data

command order::claim {
	require "this stage isn't worked by a role you hold" {
		held(stage.worked_by) || exists(step where from == order.stage && held(roles))
	}
	require held_by == none || held_until < now  "it's taken already"
	held_by = me
	held_until = 30 minutes from now
}
command order::renew {
	require held_by == me  "you don't hold it"
	held_until = 30 minutes from now
}
command order::release {
	require held_by == me  "you don't hold it"
	held_by = none
	held_until = none
}
command order::report {
	changes data
	require held_by == me  "you don't hold it"
}
command order::move {
	changes stage
	require "your role doesn't move an order from there to there" {
		exists(step where from == was order.stage && to == order.stage && held(roles))
	}
	require held_by == none || held_by == me || held_until < now  "someone else holds it"
	if held_by != me {
		clear data
	}
	held_by = none
	held_until = none
}
```

- A claim is taken only while no lease holds, even its own holder's: two programs
  signed in with the same key never take the same order, and one that holds it
  keeps it with `renew`.
- `data` is named values, each text, a number, or yes or no, up to 64 of them:
  what whoever holds it says of it with `report`, like `status`, `since` and
  `runner`, which a board shows as a dot, a running time and who's on it. What's
  reported is merged into what's there, and a null takes a name out. A move by its
  holder keeps it; anyone else's move clears it, since it no longer says how it's
  going. A role that may claim may report: no role names `report` itself.

- Whoever may take a step out of a stage works it, so the roles a step lists may
  claim what's in the stage it leaves, as the stage's own roles may. A role's queue
  holds the stages it's listed to work.
- A claim lasts until its lease runs out, so a worker that stops, like a laptop
  that sleeps, lets go by itself; one still working renews it. Moving it on ends
  the claim. Each command is allowed by a role, as any is: `order::claim`,
  `order::renew`, `order::release` and `order::move`.
- Each role's queue is a view the workflow adds, `order_work` per role, read by the
  people of the role's project: what's in the stages the role works, with every
  field, the stage's title and roles, and who holds it until when. A worker listens
  to it, and takes what's free, or held past its lease.
- The roles are a [define role](#define-role)'s, and the entity is held in what
  they're in, like a shop. Writing `held_by`, `held_until` or any of the four
  commands as well is a mistake.

### Stages written in the code

When every shop works the same stages, they're choices, and the workflow says who
works each and the steps between them. Nothing is kept as records, and nothing is
made for each new shop:

```one
enum stage {
	proofing  "Proofing"
	press     "Press"
	shipping  "Shipping"
	shipped   "Shipped"
}

entity order {
	shop   shop   required
	title  text   required
	stage  stage = stage::proofing
}

define workflow order by stage {
	proofing  worked by proofreader
	press     worked by press_operator
	shipping  worked by shipper
	proofing -> press
	press -> shipping
	press -> proofing
	shipping -> shipped
	lease 30 minutes
}
```

- `by` names a field of choices, an enum's or its own, and its choices are the
  stages. An order starts where the field's default says.
- `proofing worked by proofreader` names the roles that work a stage, separated by
  commas when there are several. A stage nobody works, like `shipped`, is an end:
  it's in no queue, nobody claims it, and no step leaves it.
- `proofing -> press` is a step, taken by the roles that work `proofing`. A step
  can name its own instead: `press -> proofing by foreman`. Whoever may take a step
  out of a stage works it, so foreman works `press` too: claims orders there, and
  has them in its queue.
- It adds the same lease, commands and queue as stages kept as records, with who
  works what checked from the code:

```one
command order::claim {
	require "this stage isn't worked by a role you hold" {
		stage == stage::proofing && held([role::proofreader]) || stage == stage::press && held([role::press_operator]) || ...
	}
	...
}
command order::move {
	changes stage
	require "your role doesn't move an order from there to there" {
		was stage == stage::proofing && stage == stage::press && held([role::proofreader]) || ...
	}
	...
}
```

- `held([role::proofreader])` asks whether the person holds one of the roles named,
  the ones every shop starts with, and can be written in any command.
- `order_work` per role lists the shop's orders in the stages that role works.
- These are mistakes:
  - a stage that isn't one of the choices;
  - a stage said twice;
  - a role that isn't defined;
  - a step to the stage it leaves, a step said twice, or a step out of a stage
    nobody works;
  - a choice nobody works and no step reaches;
  - no steps at all;
  - `worked by` or `steps` with stages written in the code.

### One flow waiting on another

Each kind of work can have a flow of its own, with its own stages, and still wait
on another's. Here a shop etches the plates an order is printed from, and an order
doesn't go to the press until its plates are etched:

```one
enum plate_stage {
	to_do    "To do"
	etching  "Etching"
	etched   "Etched"
}

enum order_stage {
	to_do     "To do"
	printing  "Printing"
	printed   "Printed"
}

entity plate {
	shop   shop  required
	name   text  required
	stage  plate_stage = plate_stage::to_do
}

entity order {
	shop    shop  required
	title   text  required
	plates  list of plate
	stage   order_stage = order_stage::to_do
}

define workflow plate by stage {
	to_do    worked by foreman
	etching  worked by etcher
	to_do -> etching
	etching -> etched
	etched  notify chime
	done at etched
}

define workflow order by stage {
	to_do     worked by foreman
	printing  worked by press_operator
	to_do -> printing
	printing -> printed
	done at printed
	depends on plates
}
```

- `done at etched` says where a flow's work is finished.
- `depends on plates` names a list of what an order waits on, of its own entity or
  another whose workflow says where it's done. An order leaves its first stage only
  once every plate it lists is etched; until then the move is refused, naming them:
  "waiting on Cover, Back". Moving on from later stages, or back, doesn't wait. It
  adds this to `order::move`, as a project could write it:

```one
	require waiting(plates) == none || was order.stage != stage::to_do  "waiting on {waiting(plates)}"
```

- `waiting(plates)` is the plates not done yet, by their name or title, read in the
  command, and a require's message says a field of the entity, or `waiting(...)`,
  in braces.
- `etched  notify chime` tells whoever has the board open when one arrives in that
  stage: its column has a bell, ♪ in the desktop and terminal apps, which chime, with
  [chime](https://github.com/da0x/chime) when it's installed or the terminal's bell
  when it isn't; the web plays a tone. A chime is the only way so far; `email` and
  `text` are words kept for later.
- These are mistakes: `done at` or `notify` with a stage it doesn't have, `depends
  on` a field that isn't a list, or a list of what has no workflow saying `done
  at`, and any of the three with stages kept as records.

## define agent

A program that works stages by itself, as a role for services: when a record
arrives in a stage it works, whether a person moved it there or a step did, it
takes it, runs a program, and moves it on. A person drags an order to Printing,
and the press does the rest:

```one
define role press_operator "Press operator" in shop for services {
	order::claim
	order::renew
	order::release
	order::move
}

define agent press for press_operator {
	on order in printing  run "tools/print {order.id}"  then printed  else to_do
}
```

- `on order in printing` is a stage of a [workflow](#define-workflow) the role
  works. `run` is the program, from the project's folder, with a field of what it
  works in braces, like `{order.id}` or `{order.title}`. It's given the record as
  JSON on its input, and a folder of its own to work in.
- When the program succeeds, the record moves to `then`; when it fails, to `else`,
  or back to whoever may take it when there's no `else`. Each is a step of the
  workflow out of that stage. A stage after it that the agent works too is taken
  at once, so one drag can start several stages' work.
- Its lease is renewed while the program runs, so a record isn't taken from it
  midway, and one it stops working, like a machine that sleeps, is let go when its
  lease runs out, for another to take.
- `one build` writes it as a program in `build/agents/press`, which signs in with
  a key of the role's service, `ONE_KEY`, against the server the apps use. These
  are mistakes: a role that isn't for services, a stage the role doesn't work, a
  `then` or `else` that isn't a step out of the stage, a field in braces the
  entity doesn't have, and a stage said twice.

## define theme

How a site looks: a color for what each is for, light and dark, its fonts, its
corners and its depth. Everything else it's drawn with is worked out from these:
text on a button, hover, the soft color behind a chosen row, the edge of a field,
grid lines, the tints of how urgent something is, and shadows. So a theme stays
short, and nothing in it can drift from the rest.

```one
define theme sea "Sea" from harbor {
	page     #f7fafc  dark #0b1220
	accent   #0b5cad  dark #8cc4ff
	text     "Inter"
	heading  "Fraunces"
	code     "IBM Plex Mono"  ground #f4f7fb  dark #0d1524
	corners  4
	depth    flat
}

project shop {
	theme  sea
}
```

- The colors are for `page`, `surface` (a panel on the page), `sunken` (a well in
  one, like a table's head), `ink` (text), `muted` (quieter text), `line`
  (borders), `accent` (links and buttons), `danger`, `success` and `warning`, each
  `#rrggbb`, with its color when dark after `dark`.
- `from harbor` starts from another theme, uione's own or the project's, and
  changes only what it says. A theme that starts from none says every color.
- `text`, `heading` and `code` are fonts, from Google Fonts unless they're the IBM
  Plex the component set brings. Headings are in the text's font unless it says.
  `ground` is the background code sits on, light and dark, in place of a code
  theme's own.
- `corners` is how round, in pixels, 0 for square; `depth` is `flat`, panels flat
  on the page, or `raised`, with a shadow.
- Text has to read on what it sits on, 4.5:1 as WCAG asks, light and dark: ink,
  muted and the accent on the page and on panels, and danger, success and warning
  on panels. A color that doesn't is a mistake, and its fix is the nearest color
  that does, its hue kept and only its lightness moved. So is an accent no text
  reads on, white or the page's own.
- A section's color, like `color violet`, is drawn in the shades of uione's theme
  a site's theme starts from.

## define service

A system declared in code that runs commands, not a person, and not one of the
programs a project gives a role [for services](#for-services), like GitHub telling each project
about the commits that mention its issues, and the commands it may run, one a
line. A [webhook](#webhook) runs as one.

```one
define service github "GitHub" in project {
	mention::create
}
```

- `in project` says each project connects its own, as each project has its own
  webhook secret, so one project's GitHub runs commands only on what's in that
  project. A service runs commands on what's in its project, as a role does.
- A command a service runs counts as allowed, as one a role allows does.
- No person may run a command that only services run and no
  [define role](#define-role) allows: not even by editing a project's role to say
  so, since a role's record is its people's to edit, and what a service runs isn't
  theirs to give.

## function

A function, for the logic that is really yours.

```one
function sort_title(title) {
	if starts_with(title, "The ") { return drop(title, 4) }
	if starts_with(title, "An ") { return drop(title, 3) }
	if starts_with(title, "A ") { return drop(title, 2) }
	return title
}
```

## screen

A page: a title, a route, and what is on it. A screen without a title, `screen
/:project/settings { ... }`, shows the site's own, and isn't in the menu. A screen
at several addresses says each, the others on lines of their own under the first,
and each is the same page:

```one
screen /:owner/:project/deployments
	/:owner/:project/deployments/:place/setup {
	component deployments
}
```

```one
screen "Shelf" /shelf {
	table shelf link /books/:book {
		shelfmark
		title
		status
		withdraw
	}
	book::create
	form book::create {
		title  author  shelfmark
	}
	confirm book::withdraw "Withdraw {title}? It will not be lent again."
}

screen "Book" /books/:book {
	text "{book_page.title}, by {book_page.author}"
	book::update
	form book::update {
		title  author
	}
}
```

- A column that's a person, like `member` or `assignees`, shows each one's name
  with their picture, small and round, beside it, when the rows hold
  `member.name` and `member.picture`. A column like `member.picture` alone shows
  just the picture.
- A route's last parameter can take the rest of the address, slashes and all:
  `screen "Code" /:project/code/:file*` is at `/neotrac/code/components/chart.tsx`,
  where `useParam("file")` is `components/chart.tsx`. Going from one such address to
  another keeps the page as it is, the way an editor keeps its place moving between
  files.
- `table <view> link /books/:book` makes each row open that screen, with the row's
  id as the parameter, or the row's own `book` when it holds one, like a change of
  a book or a membership of a project. Parameters before the last, like the
  project in `/projects/:project/issues/:issue`, come from the screen's own address,
  or from the row when it holds them, like a list of one person's issues across
  projects. The last can be a field of the rows that points at what the screen is
  for: `table issue_page.links link /:project/:to` opens the issue each link is to.
- A table's block can start with its own settings: `search title labels` gives it a
  box that finds rows by those fields, `sort by number descending` puts its rows in
  order, largest or latest first, and `page 25` shows them 25 at a time. `by status`
  can go there too. `only due <= 2 weeks from now` keeps only the rows a filter
  picks, worked out as it's shown, and `tint by priority` colors each row by its
  choice, as Trac colored a ticket's priority: the field's first choice red, then
  yellow, plain, cyan and blue. A board's block and a details block can say `tint by`
  too.
- What a page shows is in its path, after its own, so a link shows the same: a
  table's tab, like `/neotrac/boards/main/in_progress`, a table and board's switch,
  `/board`, which stage a board shows on a phone, the same part as the tab, and a
  page of rows, `/page/2`, in any order, a default left out. Only what's typed to
  find something is a query: a table's search, `?search=old`, and a `find` box's,
  `?find=date`. A card's filters are queries too, as they pick rows.
- A required choice, like `priority  priority  required = priority::normal`, is
  always one of its choices: a form offers no empty one.
- `find "Search this project" { project_page.issues "Issues" link /:project/:issue by
  number title labels }` is a box that finds rows of several lists, one a line, as
  it's typed in: each list's first eight under its label, each opening its page.
  `#12` or `12` finds the twelfth by number. What's typed is in the address, as
  `?find=date`.
- A form's field holding a person, or a list of people, picks them from a list on
  the screen whose rows hold a person and their name, like a project's members, each
  person once; a form making one of those rows, like adding a member, doesn't.
- A view reads a person's `name`, `picture` or `username` through a list of people
  too, like `assigned = issue.assignees.name`.
- `hide when empty` leaves a table out while it has no rows, like a person's
  reports on a front page they may never have used.
- `reorder position` lets the rows be dragged into order, or moved with Alt+↑ and
  Alt+↓ on a row's handle, for whoever may update them. A drop gives the row a
  position between its new neighbors', so only it changes. The list is ordered by
  that field, and the entity's `update` may set it. It works within a tab or a page
  too, where a row's neighbors are still its neighbors in the whole list, but not
  while the table is sorted by a column or searched. In a board, `reorder position`
  puts each column's cards in order the same way: a card is dragged up or down its
  column, or moved with Alt+↑ and Alt+↓.
- `table project_page.issues by status` sorts the rows into tabs, one for each of
  a choice's values, like Open and Closed, each with how many rows it has. The
  first choice is shown first, and the table needs the choice as a column.
- `by stage over project_page.stages` makes the tabs a list's records instead, one
  for each stage in its order, called by its title, with the rows holding which
  stage they're in.
- `track milestone over board_page.milestones { due }` puts a list's records above
  the table as large cards in a row, in the list's order, an arrow from each to the
  next, like a project's milestones, sprints or versions as a timeline. Each card
  shows its title, what the block names, like its date, and how many rows it holds.
  Picking one keeps the rows whose `milestone` is it, and picking it again shows
  them all. The one picked is in the path, `/milestone/beta`. A board beside the
  table takes its track, and a board's own block can say `track` too.

```one
table board_page.issues by stage over board_page.stages link /:project/:issue {
	track milestone over board_page.milestones {
		due
	}
	number "#"
	title
}
```
- A command in a table's block, like `issue::create "New issue"`, is a button in
  its toolbar, beside its search, opening its form when the screen has one; the form
  isn't drawn anywhere else. A board's block takes one too, and `search` as well.
- A column holding a list of words, like an issue's `labels`, shows each on its
  own.
- `table <view>` lists a view's rows. A line naming a command, such as `update` or
  `withdraw`, puts that command on each row, and `delete "Remove"` names its
  button. Like any button, it's there only for someone whose role lets them run
  it. `delete "Remove" when person != me` puts it only on the rows where its when
  holds, read from the row's own fields, so the list needs `person` among them.
  The when ends its line, or its block does.
- A row's button for a command with a `form` on the screen opens that form, started
  from the row, and sends it for that row: `update "Rename"` with `form
  stage::update "Save" { title }` renames the stage on the row. The form isn't drawn
  on its own, and the list holds what it asks for.
- `grid project_page.steps by from and to over project_page.stages { roles.title }`
  shows what goes between two of a list's things: a row and a column for each
  stage, in its order, and in each cell what the step from the row's stage to the
  column's holds, like the roles that may take it. Pressing an empty cell opens the
  form for `step::create` on the screen, with `from` and `to` filled in; pressing a
  full one opens the form for `step::update`, started from the step, with Remove
  beside it for `step::delete`. Neither form is drawn on its own, and each is there
  only for whoever may run it.

```one
grid project_page.steps by from and to over project_page.stages {
	roles.title
}
form step::create "Allow" {
	roles "Taken by"
}
form step::update "Save" {
	roles "Taken by"
}
```

- `board project_page.issues by stage over project_page.stages` shows a list's
  rows as cards in columns, a column for each stage in its order, with how many
  cards it has, and each issue in the column its `stage` names. Its block says what
  a card shows, its title first, and `link` opens a card's page as a table's does.
- A board of stages written in the code, like `board shop_page.orders by stage`,
  says no `over`: its columns are the field's choices, in their order, each shown
  as its enum says. `move order::move` with no `along` goes along the steps its
  workflow writes, those out of the stages no agent works, since an agent moves on
  what it works itself. A stage that says `notify chime` has a bell on its column.
- A board over stage records says which of their fields make a column chime and
  give its cards' dot, since each board's stages are its people's to change:
  `chime when notify has chime` chimes for a column whose stage's `notify` list
  holds `chime`, and `status by status` takes a stage's `status`, one of
  `inactive`, `waiting`, `working`, `done` and `failed`, as its cards' dot unless
  a card's holder says otherwise.
- `move issue::move along project_page.steps` in its block lets a card be dragged,
  or moved with Alt+← and Alt+→, to a column a step from its own leads to, for one
  of the person's roles; while it's dragged, the columns it can't go to are dimmed.
  The card shows in its new column at once, and goes back if the move fails.
- A table and a board of the same list, one right after the other, are one: the
  person picks which they see with a switch in the toolbar, and their pick is
  remembered. The board takes the table's search and toolbar when it has none.
- A screen whose address goes on from another's, like `/:project/boards/:board`
  from `/:project`, shows the trail of pages above it, each by its title and
  linked, above its own title: Projects › neotrac › Product.
- `under /:project/boards/:board "{issue_page.board_title}"`, a line of a screen's
  own, puts it under a page its address doesn't name:
  its trail is that page's, then that page, called as it says, with what its
  address needs taken from the screen's views, like `board = issue.board`.
- A link that names something keyed by several parts, like a board in
  `/:project/boards/:board`, is filled from a row's id of it by its own part.
- `subtitle "{project_page.summary}"` puts a screen's words about itself under its
  title, as running words rather than a document in a frame.
- A button line can say `icon edit`, like `project::update "Edit project" icon edit`:
  it's drawn as the icon, and its words still name it, shown when it's pointed at.
  So can a link, `link /:project/boards/:board/workflow "Workflow" icon workflow`,
  and a button in a table's or a board's toolbar, `issue::create "New issue" icon
  add`. The icons are `add`, `archive`, `delete`, `edit`, `follow`, `following`,
  `settings` and `workflow`.
- A screen laid out in regions keeps its `heading` and `subtitle` outside them,
  under its title. Beside the page, in `side`, a `section` is a glance at
  something, with a smaller heading.
- `cards project_page.boards link /:project/boards/:board { ... }` shows a list's
  rows as large cards, three across a wide page and one on a narrow one, each with
  what its block shows, its title first. `tally project_page.issues by board and
  stage` sums up what each card holds, counting the list's rows whose `board` is the
  card by their `stage`, named and ordered by `stage.title` and `stage.position` when
  the list has them. `filter "Opened by me" author == me` is a link that opens the
  card filtered; three at most. A filter can keep the rows whose time is after or
  before one counted from when it's opened: `filter "Changed this week" updated_at
  > 7 days ago`, or `due <= 2 weeks from now`, in minutes, hours, days or weeks.
- A filter and a section can each have a color, so the parts of a page can be told
  apart at a glance: `filter "High priority" priority == priority::high color red`,
  `section "Wiki" color violet { ... }`. A section's heading, links and tables
  take its color in place of the site's own. A table can have one of its own,
  `color violet` in its block, for its rows' links alone. The colors are the library's `hue`:
  `blue`, `teal`, `green`, `amber`, `red` and `violet`, each drawn in the
  component set's own shade of it, light and dark.
- A table's toolbar has a button for compact rows, which every table on the site
  follows once a reader picks it, in their browser.
- A table or a board opened with a filter in its address, as a card's filter opens
  it, keeps only the rows it picks, and says so, with a way to clear it.
- `heading { project::update "Edit project" }` puts its buttons on the screen's
  title row, at its end, with their forms.

```one
table project_page.issues link /:project/issues/:issue {
	number "#"  title  stage.title "Stage"
}
board project_page.issues by stage over project_page.stages link /:project/issues/:issue {
	move issue::move along project_page.steps
	title
	number "#"  priority  labels
}
```

- `diagram project_page.steps by from and to over project_page.stages { roles.title }`
  draws the same as a grid: each stage a box in a row, in order, and each step an
  arrow, forward above and back below, labelled with what its block shows. An arrow
  is pressed to change or remove it, and dragged from one box to another, a new
  one is added, by the same forms as a grid's.
- A command on a line of its own is a button, and `form` lists the fields it asks
  for. One form can serve several commands.
- `details issue_page { status "Status"  implementer "Implemented by" }` shows a
  view's values, each beside what it is, a choice as it's shown and a list of words
  each on its own, and leaves out the ones with nothing in them.
- `text "Taken by {issue_page.owner_name}" when issue_page.owner != none` shows a
  text only while its when holds, as a button's does.
- A view can read a person's `name`, `picture` or `username` through a field
  holding them: `implementer = issue.implemented_by.name`.
- `copy issue_page "Copy issue"` is a button that copies everything the view
  holds, as Markdown, to paste somewhere else whole: the page's title, each value
  but the ones the title shows, a markdown value as it was written, then each list
  the screen shows, a conversation as who wrote what and when, changes as what
  happened, and other rows as their values. As the view gains fields, so does what
  it copies. The ids of people are left out, since they mean nothing pasted
  elsewhere, and so is a list only buttons go by, like the steps of an issue's Move
  buttons.
- `layout two_columns`, a line of a screen's own, lays it out in two columns: `main`,
  wide, for what the page is about, and `side`, narrow, beside it on a wide screen
  and after it on a phone. A screen puts its items in them with `main { ... }` and
  `side { ... }`, every item in one once it uses any, or leaves them all in main.
  `single`, one column, is how a screen is laid out unless it or the project block's
  `layout` setting says otherwise.
- `landing grid` after a screen's address draws it as a front page, in one of
  uione's designs: `waveform`, a wave of bars under the hero; `studio`, the hero
  beside a player and its timeline; `aurora`, drifting light behind glass; `grid`,
  a neon floor under the site's name, and a band of what it offers; or `relay`, a
  board of work under the hero, what the site offers on its cards, where an
  agent's card moves on a stage. The design takes the screen's `hero`, with its
  words and links, the first link the one it's for; its sections of words alone, as
  what the site offers; and its other sections, like one with a form, as what it
  asks. Anything else, like a table, comes before the design, or, in `relay`, right
  under its board. Each is drawn in the theme's colors, and its motion stops for
  whoever asks for less. A landing page has no `layout`; its design lays it out.

```one
screen "Press" / landing grid {
	hero "Print that arrives on time." {
		text "Orders, proofs and shipping for a print shop, in one place."
		link "/signin" "Sign in"
	}
	section "You approve every proof" {
		text "Nothing goes to press until you've seen it."
	}
}
```
- `thread issue_page.comments` shows a list of what people wrote as a
  conversation: each entry with its author's picture and name, when it was written,
  and its body as Markdown. Its rows need `body` and `author.name`, and show
  `author.picture` and `created_at` when they have them.
- `timeline issue_page.history` shows an entity's changes, a list of `each change
  of issue`, one sentence each: "Ada created this", "Grace changed status from open
  to closed". Its rows need `field`, `before`, `after` and `created_at`, and name
  who made each change with `created_by.name`. With `action` too, a change a
  command of its own made says what that command did: "Ada closed this".
  A change of a field that points at something, like an issue's stage, names what
  it pointed at by its title or name: "Ada changed stage from Triage to Ready".
- A timeline of many things' changes, like every issue's in a project, names what
  each change was to with the list's own columns, like `issue.number` and
  `issue.title`, and `link` opens it: `timeline project_page.timeline link
  /:project/issues/:issue` reads "Ada closed #12 Copy an issue whole".
- `timeline news.changes "What's new"` says what a timeline is of above it. Like
  every timeline, it isn't shown while it has nothing in it. Its block can say
  `new when created_at > news.seen`, which marks the changes a condition holds for
  and counts them beside its title. A bare name is a column of the timeline's rows,
  and `news.seen` a value of another view, read as it was when the page opened, so
  what was new stays marked until they leave the page. Times compare as times, and
  none, like a person who has never looked, is before everything. `on open dispatch
  reader::create` runs a command once the timeline opens with something new in it,
  like one saying they've looked; nothing runs when nothing is new.
- Buttons one after another on a screen sit together in a row, a command's own and
  the ones that open forms alike, and so do links one after another, outside a
  hero or a section, which lay out their own.
- A button says what it does when its line names it: `issue::create "New issue"`
  opens the form, and `issue::close "Close issue"` runs the command. Without a
  name, it's named after its command, like Close.
- `when` shows a button only while it applies, as the page's views say:
  `issue::close "Close issue" when issue_page.status == status::open`. It compares
  a view's fields with values, choices like `status::open`, `true`, `false` and
  `none`, and whoever is reading as `me`, as in `issue_page.assignees has me`,
  joined with `&&` and `||`, and holds nothing until the views it reads have
  arrived.
- A when with several conditions can be a block of them, one a line, that all
  hold, the same as joining them with `&&`. It reads better than a long line, and
  a line is added or taken out without touching the others. A line can still use
  `||`, and keeps it to itself. Texts and rows' buttons take a block too.

```one
issue::verify "Verify" when {
	issue_page.status == status::implemented || issue_page.status == status::verified
	issue_page.implemented_by != me
}
```
- A button is there only for someone who may press it. A command a role grants,
  like `issue::close` granted to maintainers per project, shows on a page whose
  address names the project only to the people who hold one of those roles there;
  someone signed out is asked to sign in instead. The roles a person holds are a
  view of their own the build adds, read only by them.
- A title can show what the page does: `screen "#{issue_page.number}
  {issue_page.title}" /:project/issues/:issue`. The page has no title until those
  values arrive, and the browser's tab names it too.
- A `create` form on a screen whose address names what the entity points at, like
  `form issue::create` on `/projects/:project`, sends that project without asking
  for it.
- A form for `update`, or any command but `create`, changes one entity that's
  already there. It goes on that entity's page, a screen like `/books/:book`, and
  starts from what's stored: a view per book has to hold every field it asks for
  (`title = book.title`), so no field starts empty and gets saved empty. It can't
  ask for a key, which never changes. What its command is sent besides, an
  `input`, starts empty, like the address typed to delete a project, and a form
  of only those needs no view.
- A form that deletes what its page's address names goes, once it's done, to the
  address above it: deleting a project from `/:project/settings` goes to `/`.
- `form project::update "Save changes" { ... }` names the form's button; without
  it, the button is named after the command, like Update.
- A button and the form it opens go on one line: `issue::update "Edit" form "Save" {
  title  body }` is the button, then its form, as `issue::update "Edit"` with `form
  issue::update "Save" { title  body }` below it. A form several buttons open is
  written on its own.
- A field can be called something other than its name: `form comment::create
  "Comment" { body "Comment" }` asks for the body, labelled Comment.
- A field can say what it means under it: `takes_reports "Public reports" { hint
  "Anyone may report a problem, signed in or not" }`.
- `confirm` asks before a command runs.
- `themes` tries themes made at random from a palette on the whole app at once:
  pick a scheme, complementary, analogous, triadic, split complementary or
  monochrome, and press New theme. Every role comes from the palette, light and
  dark, and danger, warning and success stay red, amber and green, leaning to its
  hue, each reading on what it sits on as a theme has to. It shows the theme's
  colors and the theme as a project says it, with Copy, to paste into a `.one`
  file and name in the project block. The web draws the app in it at once, the
  desktop its window, and the terminal sets its own colors to it, put back as the
  app quits. A seed is the same theme in every app, and `one theme --seed 48213
  --scheme triadic` writes it too; `one theme` alone picks both at random.
- `component opening_hours` draws a hand-written React component: the default
  export of `components/opening_hours.tsx`, beside the `.one` file. It's for
  anything the language doesn't say. It reads views and runs commands with
  `@uione/react`'s hooks, like any screen, and the npm packages it needs go in
  `components/package.json`, whose dependencies are added to the app. The other
  `.ts`, `.tsx` and `.css` files in `components/` come along too, so components
  can share a module, like `import { useHistory } from "./commits"`. A project
  with a desktop or terminal target draws it there from
  `components/opening_hours.cpp`: a function of the page giving a view, written
  with the parts the generated screens are, and the other `.cpp` and `.hpp` files
  in `components/` come along too.
- `hero`, `section`, `text`, `link`, `code` and `markdown` are for pages that are
  mostly words. `#name` is a place on the page, and `{...}` in a string shows a
  live value.
- `code "examples/main.one"` shows a file, highlighted, and `markdown
  "docs/*.md"` makes a page of each file. Both are found beside the `.one` file
  that names them, as components are, so a folder of pages can sit in a folder
  of its own.
- `link` says where it goes first and its text after: `link #waitlist "Join the
  waitlist"`, `link /language/reference "Read the reference"`. `link namespace projects "See
  the projects"` opens a namespace's own screen, at `/projects`, and
  `projects::archive` would be at `/projects/archive`. An address is inside the
  link's namespace, like a screen's: on the page `/:project` in `namespace
  projects`, `link /:project/reports "Reports"` goes to `/projects/:project/reports`,
  with the project filled in from the page's own address. A link's `:parameter`
  always comes from the page it's on, so the page needs it too. To reach another
  namespace, name it: `link namespace docs "Read the docs"`, and another site, give
  its address: `link "https://uione.io/signin" "Open the studio"`. A link to an address or a
  namespace has to reach a screen that's there, so a renamed namespace or a typo
  is an error rather than a page that isn't found.
- `menu { link /:project/settings "General"  link /:project/settings/deployments
  "Deployments" }` puts its links down the side of the page, with everything after
  it on the screen beside them, and marks the link to the page it's on. On a phone
  the links are a row above the page. Each page the menu opens has the same menu,
  like a project's settings, General and Deployments, each a screen of its own.
- `markdown "{book_page.summary}"` shows a view's Markdown field, rendered. What
  people write is drawn safely: it can't add markup or scripts to the page, and
  images in it are shown as links.

## picker

How other features choose one of these entities. A form field whose type is an
entity uses its picker.

```one
picker book from shelf
```

## webhook

Takes GitHub's webhook. A commit or pull request that mentions `#12` makes
something on issue 12 of the project whose `repository` is that repository, as
the project's GitHub [service](#define-service), which allows what it makes.

```one
define service github "GitHub" in project {
	mention::create
}

command mention::create

webhook github /hooks/github as github {
	for project by repository
	on commit {
		dispatch mention::create {
			issue = mentioned  url = url  kind = kind::commit
			title = message  author = author
		}
	}
	on pull_request {
		dispatch mention::create {
			issue = mentioned  url = url  kind = kind::pull_request
			title = title  author = author
		}
	}
}
```

- `as github` names the service it runs as. What it dispatches is a create its
  service runs, and nothing else.

- `for project by repository` finds the project whose `repository` field names
  the repository, like `da0x/uione`.
- An issue here has the project and a serial per project as its key, so `#12`
  is that project's issue 12. A mention of an issue that doesn't exist is skipped.
- A commit gives `mentioned`, `message`, `url`, `author` and `sha`. A pull
  request gives `mentioned`, `title`, `url`, `author` and `number`, and is
  handled when it's opened, edited, closed or reopened.
- A handler only makes things. Give what it makes a key, like the issue and the
  address, so a delivery GitHub sends twice is stored once.
- Every delivery has to be signed with its project's own secret, and one signed
  with another project's is refused. A project's repository is stored in
  lowercase, so two projects can't claim the same one. A delivery GitHub sends
  twice is handled once.
- A view per project shows its secret with `github_secret(project.id)`. Such a
  view has `readers` and is never public:

```one
view project_settings per project {
	readers member
	webhook_secret = github_secret(project.id)
}
```

- The deploy makes the master secret that every project's secret is derived
  from, keeps it in Secret Manager, and gives it only to the backend. It never
  goes into GitHub itself.

## header

What's beside the site's name on every page. A `badge` shows a value of a view,
like what's new to the person reading, once they're signed in and while it's more
than none. A site has one header, or none.

`header /:owner/:project { ... }` is what's above every screen whose address goes
on from that path, like a project's own name and tabs, before the screen's own
items. It takes what a screen does, checked as on a screen at that path. A `menu`
in it goes down the side of each of those screens, with what the screen shows beside
it, so a project's settings are one menu over a page for each part:

```one
header /:project/settings {
	menu {
		link /:project/settings "General"
		link /:project/settings/people "People"
	}
}
```

```one
header {
	badge news.unread
}

view news per user {
	changes = each change in issue where assignees has me {
		order by created_at descending
		limit 30
		issue  field  created_at
	}
	seen = first(reader where person == me).seen_at
	unread = count(changes where created_at > seen)
}
```

## footer

What's at the foot of every page, drawn with a screen's own items: text, links, and
sections of them. A site has one, or none.

```one
footer {
	text "© {year} [Daher Alfawares](https://www.linkedin.com/in/daheralfawares)"
	link build.release "uione {build.version}"
	link build.source "{build.commit}"
}
```

`footer` alone, or `footer bar`, puts its items in one quiet line, apart; `footer
columns` puts each `section` in a column of its own, its title above it, like a
map of the site.

Text anywhere can say `{year}`, the year it's read in, and link words as Markdown
does, `[Ada Lovelace](https://...)`. What the site was built from can be said too:
`{build.version}` is the uione release, and `{build.commit}` the commit, as git
says where it's built, or as `UIONE_COMMIT` does when a deploy names it. `link
build.release` goes to that release's notes, and `link build.source` to that
commit in the site's repository, when it's on GitHub: as git's `origin` says, or
`UIONE_REPOSITORY`. Where the repository isn't known, its words are shown
unlinked, and without a commit, not at all.

## once

A change to what's stored that a deploy brings, like giving every project made
before workflows the stages it now needs. It's done the first time the backend
starts with it, before anything else, and never again: that it's done is kept by
its name, so a once is named for when it was written and what it does, and its
name never changes. If it fails, the backend doesn't start, and the deploy says so.

```one
once "2026-10-07 workflows" {
	each role where name == "developer" {
		dispatch role::update { title = "Programmer" }
	}
	each project {
		dispatch stage::create {
			project = id  name = "triage"  title = "Triage"  position = 1
		}
		dispatch project::update { start = stage::triage }
	}
	each issue {
		dispatch issue::update { stage = stage::triage }
	}
}
```

- Its steps are done in order, each `each` to every stored entity of its kind,
  or to those whose field holds a value, like `where name == "developer"`.
- What a step says is done to each entity it picks, by dispatching commands, as a
  command's body does: `dispatch role::update { ... }` acts on the role picked,
  and decides with `if`. Each entity is changed in a step of its own, checked
  against its rules, with its history saying it was changed.
- `stage::triage` is the project's stage named triage, from the project itself or
  from what's in it, like an issue. A once may name what's already stored, like
  `role::developer`, as well as what it makes.
- It runs as the backend itself, so no role is needed.

## backend

Go written by hand, for what the language doesn't say, the way `component` is for
React. `backend deploy` in a namespace names `backend/deploy.go` beside the `.one`
file. It's built into the namespace's Go package with the generated code, so it
starts with that package's clause and can name the generated entities and commands.

```one
namespace studio {
	backend deploy
}
```

It uses four things from the `one` library:

- `After`, on a command, runs a function once the command's change is saved, with
  the entity as it was saved: `DeploymentCreate.After(start)`, from an `init`
  function. It runs before the command answers, so it starts slow work elsewhere
  rather than doing it.
- `one.Route("POST /hooks/deploy", handle)` answers requests of its own, like a
  build reporting back. A route checks the request itself: `s.SignedIn(r)` says
  who sent it, by the sign-in it carries, or `""` for nobody.
- `one.Once("name", work)` is a [`once`](#once) with work written in Go, for what
  a once can't say. It's added to the namespace from an `init` function:
  `Module.Add(one.Once(...))`.
- They're given a `System`, the backend itself. `Run` runs any command, even one
  no role grants, and `one.Fetch` and `one.FetchWhere` read what's stored.
