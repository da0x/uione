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
  entity, like `project.name`, and `user.id` is whoever is reading it.
- **Everywhere**, `me`, `now`, `none`, `true` and `false` are the language's own
  [values](#built-in-values). A choice is named with its enum, like
  `status::open`. `permission` takes one of the language's own words: `anyone`,
  `authenticated` or `owner`.

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
| [`project`](#project) | where the app runs and what it's built with |
| [`namespace`](#namespace) | a group of declarations, and the address its screens are at |
| [`entity`](#entity) | something the app keeps, with its fields |
| [`format`](#format) | a pattern a text field holds to |
| [`command`](#command) | a way to change an entity |
| [`view`](#view) | a document built ahead of time for a screen |
| [`role`](#role) | permissions a person can be given |
| [`function`](#function) | a value worked out from others |
| [`screen`](#screen) | a page, at an address |
| [`picker`](#picker) | how an entity is chosen in a form |
| [`webhook`](#webhook) | what's done when another service sends an event |
| [`backend`](#backend) | Go written by hand beside what's generated |

## Comments

`//` starts a comment that runs to the end of the line, and `/* */` holds one that
spans lines. Comments are for people: the compiler reads past them.

## Strings

Text written as it is, in double quotes: `"Shelf"`, `"that book is not on the
shelf"`. In a screen's text, `{...}` shows a live value: `"{book_page.title}, by
{book_page.author}"`. A backslash writes a quote or a backslash inside one: `\"`.

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
- in a command: `require`, `permission`, `create`, `clear`, `add … to`, `remove … from`;
- in a view: `per`, `public`, `each`, `change of`, `where`, `order`, `limit`,
  `readers`, `public when`;
- in a role: `per`, `from`, and in a picker, `from`;
- on a screen: `table`, `form`, `confirm`, `component`, `hero`, `section`, `text`,
  `code`, `link`, `menu`, `markdown`, `hint`;
- in a project: `one`, `title`, `domain`, `firebase`, `region`, `ui`,
  `authentication`, `icon`, `serve`, `redirect`.

## project

One per project, naming where it runs and what it is built with.

```one
project uione {
	domain          "uione.io"
	firebase        "uione-web"
	region          "us-east4"
	ui              radix
	authentication  google
	authentication  github
	icon            "assets/icon.svg"
	serve           "public"
}
```

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
that renders every screen. `authentication` names a way people sign in: `google`,
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

`color "#0f766e"` is the site's own color, for its buttons, links and focus rings, in
place of the component set's, a lighter one of it on a dark page. It's written
#rrggbb, and dark enough to read as a link on a white page.

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
shared by all of them, and one inside takes the place of a shared one.

```one
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
`/studio/da0x/neotrac`.

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
- An enum's choices are always written with its name, the field's:
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
  pick by what a list holds, as `where assignees has user.id`. A form writes a list
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
- `= value` sets the value a new entity starts with. `me` is the person running the
  command, `me.username` is their GitHub username, `now` is the time it runs, and
  `none` is no value. A field that starts as `me` or `me.username` always does, so
  nobody can make something in another's name.

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
deletes with the entity's own rules.

```one
command book::create

command loan::checkin {
	require returned_at == none  "that book is already back"
	returned_at = now
	book.status = status::on_shelf
}
```

- `require` states a precondition and the message shown when it fails.
- A command may change an entity it points at, as `book.status` does here. Both
  changes are made together or not at all.
- `permission` overrides the permission the command needs. `anyone` means no
  sign-in, `authenticated` means anyone signed in, any way the project offers, and
  `owner` means the person in the entity's `owner` field.
- `add me to assignees` and `remove me from assignees` change a list. Adding what's
  already there, or removing what isn't, changes nothing. A list is never given a
  whole new value with `=`.
- `create` makes another entity in the same step, giving its fields values worked
  out where the command runs. `id` is the id of the command's own entity, and `me`
  is the person running it. Every required field gets a value, unless it starts
  with one.

```one
command project::create {
	permission authenticated
	create member {
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
		order due_at
		book.title  member  due_at
	}
}

view signups public {
	total = count(signup)
}

view book_page per book {
	title = book.title
	loans = each loan where book == book.id {
		order -number
		number  member  lent_at  returned_at
	}
}
```

- `per <entity>` makes one document per entity, like a page per book. Inside the
  view, that entity is named plainly: `title = book.title` puts its title in the
  document, `lifecycle = issue.project.lifecycle` a field of what it points at,
  which changing the project changes on every issue's page, and `where book ==
  book.id` picks the rows that belong to it. A screen
  showing it reads it for the entity its address names, so its route has that
  entity as a parameter: `screen "Book" /books/:book`. `per user` makes one
  document per person, readable only by that person.
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

- `each change of issue` lists the changes an issue keeps. Each holds the issue,
  under `issue`, and what it points at, so a view can list one issue's changes,
  `where issue == issue.id`, or every change in a project, `where project ==
  project.id`. A change's rows can show `field`, `before`, `after`, `action`,
  `created_at` and `created_by.name`.
- `limit 50` keeps only the first rows of a list, once ordered, so a list that keeps
  growing, like a project's timeline, stays small enough to be one document.

```one
view project_page per project {
	timeline = each change of issue where project == project.id {
		order -created_at
		limit 50
		issue  field  after  created_by.name  created_at
	}
}
```

- A list of every project would name private ones too, so it picks the public
  ones: `each project where visibility == visibility::public`.
- `order` goes inside the list it sorts, and `-` sorts in reverse.
- A view has at most one list without a name, its rows. Any others have names,
  like `loans = each loan ...`, so one page can hold a book's details and several
  lists that belong to it. Each item in a list is its own entity, so a long list
  never makes one item too big to store. A table names the list it shows:
  `table book_page.loans`.
- A view is rebuilt whenever an entity it reads changes: `desk` reads books through
  `book.title`, so renaming a book updates the desk.

## role

A default role and the permissions it holds. A person is given a role everywhere,
in `users/{id}.role_id`.

```one
role librarian  book::view  book::create  book::update  book::withdraw
```

A role can instead be held within something, such as a project, and come from an
entity that grants it:

```one
entity member {
	project  project  required  key
	person   user     required  key
	role     enum     maintainer | reporter = role::reporter
}

role maintainer per project from member {
	project::update  member::create
	issue::create  issue::update  issue::close  issue::reopen
	comment::create
}

role reporter per project from member  issue::create  comment::create
```

- A role with many permissions lists them in a block, as many to a line as reads
  well; a short one keeps them on its line. Either way a role is declared once,
  with all its permissions, so none is ever lost to a second declaration.

- A member grants its role to its person, within its project. Its id is the
  project and the person, so each person has one role in each project.
- The role's permissions count for the project itself, for anything that points at
  it, like an issue, and for anything that points at that, like an issue's
  comment. A maintainer of one project can do nothing in another.
- A role held within something is never given everywhere.

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
  projects.
- A table's block can start with its own settings: `search title labels` gives it a
  box that finds rows by those fields, `sort -number` puts its rows in order, a `-`
  for largest or latest first, and `page 25` shows them 25 at a time. `by status`
  can go there too.
- `table project_page.issues by status` sorts the rows into tabs, one for each of
  a choice's values, like Open and Closed, each with how many rows it has. The
  first choice is shown first, and the table needs the choice as a column.
- A column holding a list of words, like an issue's `labels`, shows each on its
  own.
- `table <view>` lists a view's rows. A line naming a command, such as `update` or
  `withdraw`, puts that command on each row.
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
  but the ones the title shows, a markdown value as it was written, then each list,
  a conversation as who wrote what and when, changes as what happened, and other
  rows as their values. As the view gains fields, so does what it copies. The ids of
  people are left out, since they mean nothing pasted elsewhere.
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
- A timeline of many things' changes, like every issue's in a project, names what
  each change was to with the list's own columns, like `issue.number` and
  `issue.title`, and `link` opens it: `timeline project_page.timeline link
  /:project/issues/:issue` reads "Ada closed #12 Copy an issue whole".
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
something on issue 12 of the project whose `repository` is that repository.

```one
webhook github /hooks/github {
	for project by repository
	on commit {
		create mention {
			issue = mentioned  url = url  kind = commit
			title = message  author = author
		}
	}
	on pull_request {
		create mention {
			issue = mentioned  url = url  kind = pull_request
			title = title  author = author
		}
	}
}
```

- `for project by repository` finds the project whose `repository` field names
  the repository, like `da0x/uione`.
- An issue here has the project and a serial per project as its key, so `#12`
  is that project's issue 12. A mention of an issue that doesn't exist is skipped.
- A commit gives `mentioned`, `message`, `url`, `author` and `sha`. A pull
  request gives `mentioned`, `title`, `url`, `author` and `number`, and is
  handled when it's opened, edited, closed or reopened.
- A handler only creates. Give what it makes a key, like the issue and the
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

It uses three things from the `one` library:

- `After`, on a command, runs a function once the command's change is saved, with
  the entity as it was saved: `DeploymentCreate.After(start)`, from an `init`
  function. It runs before the command answers, so it starts slow work elsewhere
  rather than doing it.
- `one.Route("POST /hooks/deploy", handle)` answers requests of its own, like a
  build reporting back. A route checks the request itself: `s.SignedIn(r)` says
  who sent it, by the sign-in it carries, or `""` for nobody.
- Both are given a `System`, the backend itself. `Run` runs any command, even one
  no role grants, and `one.Fetch` and `one.FetchWhere` read what's stored.
