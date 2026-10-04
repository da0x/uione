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


## Indentation

Lines are indented with tabs, one per level, never with spaces. How wide a tab
looks is up to you, the same as how names look: your editor, or the code on this
site, draws it as deep as you like. Spaces after a line's first word, to line
things up, are fine.

## project

One per project, naming where it runs and what it is built with.

```one
project uione {
	domain    "uione.io"
	firebase  "uione-web"
	region    "us-east4"
	ui        radix
	signin    google
	icon      "assets/icon.svg"
	serve     "public"
}
```

`title "uione"` is the name at the top of every page and in the browser's tab, when
it isn't the project's own: the studio's project is `studio`, which names what it
runs on in Google Cloud, and its pages say uione. `ui` picks the component adapter
that renders every screen. `signin` picks how
people sign in: `google` or `github`, turned on for the project in the Firebase
console. `icon` is the app's icon, an `.svg` file next to the project's
`.one` files: it's the page's icon in the browser, and it's shown beside the
app's name at the top of every page. `serve` names a folder whose files are served
as they are, at the site's root: `public/install.sh` is at `/install.sh`. `redirect "/install.sh"
"https://www.uione.io/install.sh"` sends whoever asks for an address that has
moved on to where it is now, and a project can have as many as it needs.

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
	status     on_shelf | lent | withdrawn = on_shelf
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
- Types: `text`, `markdown` (text written in Markdown, shown rendered), `email`,
  `slug` (a name like `my-app`: lowercase letters and digits joined by single
  dashes, lowered as it's saved), `date`, `number`, `serial`, `boolean`, `user`, a
  list of choices
  (`on_shelf | lent | withdrawn`), a `format` declared in the file, or another
  entity (`book`), which stores its id and reads through it (`book.title`).
- `user` is a person, stored as their id. A view can show their name and picture
  (`member.name`, `member.picture`), which come from how they signed in and are
  kept up to date each time they do something. Nothing else about them, such as
  their email address, can reach a view.
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
	book.status = on_shelf
}
```

- `require` states a precondition and the message shown when it fails.
- A command may change an entity it points at, as `book.status` does here. Both
  changes are made together or not at all.
- `permission` overrides the permission the command needs. `anyone` means no
  sign-in, `signed_in` means any signed-in person, and `owner` means the person in
  the entity's `owner` field.
- `add me to assignees` and `remove me from assignees` change a list. Adding what's
  already there, or removing what isn't, changes nothing. A list is never given a
  whole new value with `=`.
- `create` makes another entity in the same step, giving its fields values worked
  out where the command runs. `id` is the id of the command's own entity, and `me`
  is the person running it. Every required field gets a value, unless it starts
  with one.

```one
command project::create {
	permission signed_in
	create member {
		project = id  person = me  role = maintainer
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
  document, and `where book == book.id` picks the rows that belong to it. A screen
  showing it reads it for the entity its address names, so its route has that
  entity as a parameter: `screen "Book" /books/:book`. `per user` makes one
  document per person, readable only by that person.
- `public` lets anyone read it, signed in or not.
- In a view per entity, `readers member` lets the people a member names read each
  document: everyone with a role in the project the entity is held within. `public
  when project.visibility == public` opens a document to everyone while the
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
	public when issue.project.visibility == public
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
  ones: `each project where visibility == public`.
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
	role     maintainer | reporter = reporter
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
- `table <view>` lists a view's rows. A line naming a command, such as `update` or
  `withdraw`, puts that command on each row.
- A command on a line of its own is a button, and `form` lists the fields it asks
  for. One form can serve several commands.
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
