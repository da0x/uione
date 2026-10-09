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
- `true` and `false`.

## Keywords

The language's own words, inside what a declaration says:

- in a field: `enum`, `list of`, `serial per`;
- in a command: `require`, `by`, `dispatch`, `clear`, `changes`, `was`, `add … to`, `remove … from`,
  `input`, `each … where`;
- in a view: `per`, `public`, `each`, `change`, `in`, `where`, `order`, `ascending`,
  `descending`, `limit`,
  `readers`, `public when`;
- in a role: `per`, `from`, and in a picker, `from`;
- in a once: `each`, `where`;
- on a screen: `heading`, `subtitle`, `table`, `grid`, `diagram`, `board`, `cards`, `form`, `confirm`, `component`, `hero`, `section`, `text`,
  `code`, `link`, `menu`, `markdown`, `hint`, `reorder`, `move … along`, and `by … and … over` in a grid, `by … over` in a board;
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
	signin          google
	signin          github
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
		// The ways people sign in, one to a line.
		signin      list of signin
		// Whether it opens light or dark, rather than as the visitor's system is.
		appearance  appearance
	}
}
```

The value is checked by its type. An enum setting takes one of its choices,
written plainly, `appearance light`, since the type says which enum it is; it can
be written in full too, `appearance one::appearance::light`. A `domain` is a domain name,
a `slug` lowercase letters, digits and dashes, a `version` like "0.7.2", a
`theme` one that's declared, a `file` and a `folder` are next to the project's `.one` files,
an `address` is one of the site's own, starting with /, and a `link` is an https
address. A setting whose
type is another `settings`, like `redirect`, takes its fields in
order, `redirect "/old" "https://example.com/new"`. A setting is said once,
unless its type is a `list of`, whose lines each add one. A name the library
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
`github` or `microsoft`, one to a line. With one, the Sign in button goes straight
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
  none is given: a phase titled In review is named `in_review`, so a form asks only
  for the title. The name is made once, so a rename changes only the title, and a
  second phase whose title makes the same name is refused rather than taken for the
  first.
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
| `number` | a number, whole or not | a number box |
| `serial` | a whole number counted up as each entity is made, 1, 2, 3, and never typed; `serial per project` counts within each project | a number |
| `boolean` | true or false | a checkbox |
| `user` | a person who has signed in, by their id; a view can show their name, picture and username | their name |
| `list of permission` | the commands a project's role allows, like `issue::create` | a box to tick for each command |
| `list of …` | several of a type: `list of text`, `list of user`, `list of label` | each in turn |
| `enum` | one of the choices it names, each written with its name: `status  enum open \| closed = status::open` | a choice of cards or a list |

A field's type can also be another entity, holding its id (`book`), or a `format`.

## Field rules

What a field must be, written after its type: `shelfmark  shelfmark  required  unique  key`.

- `required`: it always has a value; a form can't leave it empty.
- `unique`: no two of the entity have the same one.
- `key`: it names the entity, so its id is made from its key fields, like a book's
  shelfmark, or a project's owner and slug.
- `after lent_at`: a date that has to come after another field's.
- `= value`: what it starts as, like `= now`, `= me` or `= status::open`.

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

- `require` states a precondition and the message shown when it fails.
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
  name: `changes phase` takes the phase an issue moves to.
- `was issue.phase` is what a field held before the command changed it.
- `exists(step where from == was issue.phase && to == issue.phase)` asks whether
  there's one, in a `require`. Inside it, the entity's own fields are named
  plainly, and the command's entity by its name. `held(roles)` there asks whether
  the person holds one of a list of the project's [roles](#define-role), so who may take
  a step is the project's to say:

```one
command issue::move {
	changes phase
	require exists(step where from == was issue.phase && to == issue.phase && held(roles))  "your role doesn't move an issue from there to there"
}
```
- `if workflow == workflow::kanban { ... }` does what's inside only when its
  condition holds, so a project can start from a preset it picks.
- In a `dispatch phase::create`, `phase::triaged` is the project's phase named
  triaged: one of what the command makes, or a role every project starts with,
  named by the project and its name. A list field is given its values whole: `roles = [role::maintainer,
  role::programmer]`, or a role's `may = [issue::create, issue::move]`.
  A long list goes on over lines until its `]`, and may end in a comma.
- `dispatch board::create { project = id  title = name }` makes a board as
  `board::create` makes one: its body runs too, like the phases of the preset a
  board starts from, so a project made with a first board gets that board's phases
  with it. Every required field gets a value, unless it starts with one, and what's
  made gets an id of its own.
- An update, a delete, or another command like a move, says which it acts on with
  `id = ...`, or goes in an `each` over its entity, where it acts on each row. It's
  sent its command's inputs by name, like `into`, and a list is changed with `add
  ... to` and `remove ... from` inside its braces. Deleting what's already being
  deleted in the same step does nothing, so two links that delete each other stop.
- A once may give what was made before a key existed its key, like the board a
  project's phases were in before projects had boards.
- `input into phase` is something a command is sent besides its entity's fields,
  like the phase a removed phase's issues move to. Its forms ask for it, picked as a
  field of its type would be, its body names it, and it's never stored.
- `each issue where phase == id { ... }` runs what's inside for each entity it
  picks, by a field's value, or by any of several, like `from == id || to == id`.
  Each is changed by a dispatch of its own command, and kept in its history as
  changed.

```one
command phase::delete {
	input into phase
	require into != id  "pick another phase for its issues"
	each issue where phase == id {
		dispatch issue::update { phase = into }
	}
	each step where from == id || to == id {
		dispatch step::delete
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
	title = book.title
	loans = each loan in book {
		order by number descending
		number  member  lent_at  returned_at
	}
}
```

- `per <entity>` makes one document per entity, like a page per book. Inside the
  view, that entity is named plainly: `title = book.title` puts its title in the
  document, `lifecycle = issue.project.lifecycle` a field of what it points at,
  which changing the project changes on every issue's page, and `each loan in
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
  condition holds; it compares one field of the entity, or of what it points at,
  like `issue.project.visibility`, with a value. When people join a project, or it
  turns private, only that project's documents are rebuilt.
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
	changes = each change in issue where (board.followers has me || assignees has me) && created_by != me {
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
	name     slug     required  key
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

A system that runs commands, not a person, like GitHub telling each project
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

A page: a title, a route, and what is on it.

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

- A column like `member.picture` shows the person's picture, small and round, so
  `member.picture ""` and `member.name "Member"` put a face beside a name.
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
  `/board`, which phase a board shows on a phone, the same part as the tab, and a
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
  that field, and the entity's `update` may set it.
- `table project_page.issues by status` sorts the rows into tabs, one for each of
  a choice's values, like Open and Closed, each with how many rows it has. The
  first choice is shown first, and the table needs the choice as a column.
- `by phase over project_page.phases` makes the tabs a list's records instead, one
  for each phase in its order, called by its title, with the rows holding which
  phase they're in.
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
  phase::update "Save" { title }` renames the phase on the row. The form isn't drawn
  on its own, and the list holds what it asks for.
- `grid project_page.steps by from and to over project_page.phases { roles.title }`
  shows what goes between two of a list's things: a row and a column for each
  phase, in its order, and in each cell what the step from the row's phase to the
  column's holds, like the roles that may take it. Pressing an empty cell opens the
  form for `step::create` on the screen, with `from` and `to` filled in; pressing a
  full one opens the form for `step::update`, started from the step, with Remove
  beside it for `step::delete`. Neither form is drawn on its own, and each is there
  only for whoever may run it.

```one
grid project_page.steps by from and to over project_page.phases {
	roles.title
}
form step::create "Allow" {
	roles "Taken by"
}
form step::update "Save" {
	roles "Taken by"
}
```

- `board project_page.issues by phase over project_page.phases` shows a list's
  rows as cards in columns, a column for each phase in its order, with how many
  cards it has, and each issue in the column its `phase` names. Its block says what
  a card shows, its title first, and `link` opens a card's page as a table's does.
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
- `screen "#{issue_page.number}" /:project/:issue under /:project/boards/:board
  "{issue_page.board_title}"` puts a screen under a page its address doesn't name:
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
  add`. The icons are `add`, `edit`, `follow`, `following` and `workflow`.
- A screen laid out in regions keeps its `heading` and `subtitle` outside them,
  under its title. Beside the page, in `side`, a `section` is a glance at
  something, with a smaller heading.
- `cards project_page.boards link /:project/boards/:board { ... }` shows a list's
  rows as large cards, three across a wide page and one on a narrow one, each with
  what its block shows, its title first. `tally project_page.issues by board and
  phase` sums up what each card holds, counting the list's rows whose `board` is the
  card by their `phase`, named and ordered by `phase.title` and `phase.position` when
  the list has them. `filter "Opened by me" author == me` is a link that opens the
  card filtered; three at most. A filter can keep the rows whose time is after or
  before one counted from when it's opened: `filter "Changed this week" updated_at
  > 7 days ago`, or `due <= 2 weeks from now`, in hours, days or weeks.
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
	number "#"  title  phase.title "Phase"
}
board project_page.issues by phase over project_page.phases link /:project/issues/:issue {
	move issue::move along project_page.steps
	title
	number "#"  priority  labels
}
```

- `diagram project_page.steps by from and to over project_page.phases { roles.title }`
  draws the same as a grid: each phase a box in a row, in order, and each step an
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
- `layout two_columns` after a screen's address lays it out in two columns: `main`,
  wide, for what the page is about, and `side`, narrow, beside it on a wide screen
  and after it on a phone. A screen puts its items in them with `main { ... }` and
  `side { ... }`, every item in one once it uses any, or leaves them all in main.
  `single`, one column, is how a screen is laid out unless it or the project block's
  `layout` setting says otherwise.
- `thread issue_page.comments` shows a list of what people wrote as a
  conversation: each entry with its author's picture and name, when it was written,
  and its body as Markdown. Its rows need `body` and `author.name`, and show
  `author.picture` and `created_at` when they have them.
- `timeline issue_page.history` shows an entity's changes, a list of `each change
  of issue`, one sentence each: "Ada created this", "Grace changed status from open
  to closed". Its rows need `field`, `before`, `after` and `created_at`, and name
  who made each change with `created_by.name`. With `action` too, a change a
  command of its own made says what that command did: "Ada closed this".
  A change of a field that points at something, like an issue's phase, names what
  it pointed at by its title or name: "Ada changed phase from Triage to Ready".
- A timeline of many things' changes, like every issue's in a project, names what
  each change was to with the list's own columns, like `issue.number` and
  `issue.title`, and `link` opens it: `timeline project_page.timeline link
  /:project/issues/:issue` reads "Ada closed #12 Copy an issue whole".
- `timeline news.changes "What's new"` says what a timeline is of above it. Like
  every timeline, it isn't shown while it has nothing in it. Its block can say
  `new since news.seen`, which marks the changes made after a value of a view, like
  when the person last looked, and counts them beside its title, and `seen
  reader::create`, which runs once there's something new, to say they've looked.
  What was new stays marked until they leave the page.
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
  ask for a key, which never changes.
- `form project::update "Save changes" { ... }` names the form's button; without
  it, the button is named after the command, like Update.
- A field can be called something other than its name: `form comment::create
  "Comment" { body "Comment" }` asks for the body, labelled Comment.
- `confirm` asks before a command runs.
- `component opening_hours` draws a hand-written React component: the default
  export of `components/opening_hours.tsx`, beside the `.one` file. It's for
  anything the language doesn't say. It reads views and runs commands with
  `@uione/react`'s hooks, like any screen, and the npm packages it needs go in
  `components/package.json`, whose dependencies are added to the app. The other
  `.ts`, `.tsx` and `.css` files in `components/` come along too, so components
  can share a module, like `import { useHistory } from "./commits"`.
- `hero`, `section`, `text`, `link`, `code` and `markdown` are for pages that are
  mostly words. `#name` is a place on the page, and `{...}` in a string shows a
  live value.
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
before workflows the phases it now needs. It's done the first time the backend
starts with it, before anything else, and never again: that it's done is kept by
its name, so a once is named for when it was written and what it does, and its
name never changes. If it fails, the backend doesn't start, and the deploy says so.

```one
once "2026-10-07 workflows" {
	each role where name == "developer" {
		dispatch role::update { title = "Programmer" }
	}
	each project {
		dispatch phase::create {
			project = id  name = "triage"  title = "Triage"  position = 1
		}
		dispatch project::update { start = phase::triage }
	}
	each issue {
		dispatch issue::update { phase = phase::triage }
	}
}
```

- Its steps are done in order, each `each` to every stored entity of its kind,
  or to those whose field holds a value, like `where name == "developer"`.
- What a step says is done to each entity it picks, by dispatching commands, as a
  command's body does: `dispatch role::update { ... }` acts on the role picked,
  and decides with `if`. Each entity is changed in a step of its own, checked
  against its rules, with its history saying it was changed.
- `phase::triage` is the project's phase named triage, from the project itself or
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
