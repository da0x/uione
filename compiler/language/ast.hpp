// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The syntax tree: what a .one file says, with every node remembering where it came
// from. The tree only records what was written. Whether it makes sense (that a
// command's entity exists, that names are snake_case) is the checker's job.

#pragma once

#include <filesystem>
#include <memory>
#include <algorithm>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "language/token.hpp"

namespace one::language {

    // A name that may reach into namespaces or an entity: book, loan::checkin,
    // waitlist::signup::create.
    struct qualified_name {
        std::vector<std::string> parts;
        location where;

        std::string text() const {
            std::string joined;
            for (const auto& part : parts) {
                if (!joined.empty()) joined += "::";
                joined += part;
            }
            return joined;
        }
    };

    // expressions

    struct expression;
    using expression_ptr = std::unique_ptr<expression>;

    struct literal_expression {
        // A time is one relative to when it's read, like 7 days ago or 2 weeks from
        // now, written as its offset: -7d, +14d.
        enum class kind { string, number, time };
        kind type = kind::string;
        std::string value;
    };

    struct name_expression {
        qualified_name name;
    };

    struct member_expression {  // book.title
        expression_ptr object;
        std::string member;
    };

    struct call_expression {    // count(signup)
        expression_ptr callee;
        std::vector<expression_ptr> arguments;
    };

    struct where_expression {   // loan where returned_at == none
        expression_ptr source;
        expression_ptr condition;
    };

    struct unary_expression {   // !done, -created_at
        token_kind op = token_kind::logical_not;
        expression_ptr operand;
    };

    // [role::maintainer, role::programmer]: a list's values, given whole, as what a
    // create statement gives a list field.
    struct list_expression {
        std::vector<expression_ptr> items;
    };

    struct binary_expression {
        token_kind op = token_kind::equal;
        expression_ptr left;
        expression_ptr right;
    };

    struct expression {
        location where;
        std::variant<literal_expression, name_expression, member_expression, call_expression,
                     where_expression, unary_expression, binary_expression, list_expression>
            node;
    };

    // statements, in command and function bodies

    struct statement;

    struct require_statement {
        expression_ptr condition;
        std::string message;
    };

    struct permission_statement {  // anyone, signed_in, owner, or a permission
        qualified_name permission;
        bool by = false;  // by anyone signed in: who runs a command no role allows, said as it reads
    };

    struct clear_statement {
        std::vector<std::string> fields;
    };

    // changes cloud_project deploy_account: fields an update may change besides
    // those its forms ask for, like the ones a hand-written component sends.
    struct changes_statement {
        std::vector<std::string> fields;
    };

    struct assign_statement {
        expression_ptr target;
        expression_ptr value;
    };

    // add me to assignees, or remove me from assignees.
    struct list_statement {
        bool adds = true;
        expression_ptr value;
        std::string list;
        location list_where;
    };

    // A value given to a field of what a create statement makes: role = maintainer.
    struct field_value {
        std::string name;
        location where;
        expression_ptr value;
    };

    // create member { project = id  person = me }: another entity, made in the same
    // step as the command.
    struct create_statement {
        std::string entity;
        location entity_where;
        std::vector<field_value> values;
        // board::create { ... }: made as that command makes it, its body run too,
        // like a board's phases made from the preset it starts from.
        std::optional<qualified_name> command;
        std::size_t head_length = 0;  // create board, or board::create: what dispatch replaces
    };

    // dispatch board::create { project = id  name = "main" }: another command run in
    // the same step as this one, as the same person, its body and requires too. An
    // update or a delete names what it acts on with id = ..., or acts on the row of an
    // each over its entity. Its permission isn't asked again: this command vouches.
    struct dispatch_statement {
        qualified_name command;
        std::vector<field_value> values;
        std::vector<statement> lists;  // add role::contributor to roles
    };

    struct return_statement {
        expression_ptr value;
    };

    struct if_statement {
        expression_ptr condition;
        std::vector<statement> then_body;
        std::vector<statement> else_body;
    };

    // input into phase: a value a command is sent besides its entity's fields, like
    // the phase a removed phase's issues move to. It's asked for by its forms, named
    // in its body, and never stored.
    struct input_statement {
        std::string name;
        location name_where;
        qualified_name type;
    };

    // each issue where phase == id { phase = into }: other entities changed with the
    // command, in the same step. The where picks them by a field's value, worked out
    // where the command runs; the body names the picked one's fields plainly.
    struct each_statement {
        std::string entity;
        location entity_where;
        expression_ptr where;
        std::vector<statement> body;
    };

    // delete each step where from == id || to == id: other entities deleted with the
    // command, in the same step, picked by a field's value, or by any of several.
    struct delete_statement {
        std::string entity;
        location entity_where;
        expression_ptr where;
    };

    struct statement {
        location where;
        std::variant<require_statement, permission_statement, clear_statement, changes_statement, assign_statement,
                     create_statement, list_statement, return_statement, if_statement, input_statement, each_statement,
                     delete_statement, dispatch_statement>
            node;
        std::string text = {};  // as written, for a statement on one line, which a fix rewrites
    };

    // declarations

    struct setting {
        std::string key;
        std::string value;
        bool is_string = false;
        location where;
        std::string to;  // redirect "/install.sh" "https://www.uione.io/install.sh": where it goes
        // corners one::corners::square: the value written in full, whose last part is
        // value; empty when it's written plainly.
        std::string qualified;
        location value_where;
    };

    // import one: a library a file uses, like uione's own, which says what a project
    // block may say.
    struct import_declaration {
        std::string name;
        location where;
    };


    // A place the project runs, with what's its own there: environment staging {
    // domain "staging.neotrac.org" ... }.
    struct environment_block {
        std::string name;
        location where;
        location end;  // its closing brace
        std::vector<setting> settings;
    };

    struct project_declaration {
        std::string name;
        std::vector<setting> settings;          // shared by every environment
        std::vector<environment_block> environments;
        std::string environment;                // the one being built, once chosen; empty with none
        location end;                           // its closing brace
    };

    // enum status { open "Open"  closed "Closed" }: choices named once, for any field
    // of this type, each written on its own line or several to a line.
    struct enum_declaration {
        std::string name;
        std::vector<std::string> choices;
        std::vector<std::string> choice_labels;  // how each is shown, or empty for its name
        std::vector<location> choice_where;
    };

    struct format_declaration {
        std::string name;
        std::string pattern;  // raw, like AAA-9999
        std::string example;  // raw, like HIS-0142
    };

    struct field {
        std::string name;
        location where;
        // A field whose type is an enum declared on its own, like status  status, is
        // given that enum's choices once the project is checked, as if they were
        // written on it, so everything after the checker sees choices either way.
        mutable std::optional<qualified_name> type;  // none when the field has choices, or no type
        mutable std::vector<std::string> choices;    // on_shelf | lent | withdrawn
        mutable std::vector<std::string> choice_labels;  // mit "MIT": how each choice is shown, or empty for its name
        mutable std::string enum_name;       // the enum its choices are from, which names them, like status
        bool list = false;                   // labels  list of label: several, each a label
        bool required = false;
        bool unique = false;
        bool key = false;
        std::optional<std::string> per;       // serial per book: counted within each book
        std::optional<std::string> after;
        expression_ptr initial;               // = on_shelf
    };

    struct entity_declaration {
        std::string name;
        bool history = false;  // entity issue history: every change is kept
        // entity comment history of issue: each change to a comment is kept in its
        // issue's history, through its field pointing at the issue.
        std::optional<std::string> history_of;
        location history_of_where;
        // entity invitation invites member: when someone signs in with its email, a
        // member is made from its fields of the same names, with the member's person
        // the one signing in, and it's deleted, both at once.
        std::optional<std::string> invites;
        location invites_where;
        std::vector<field> fields;
    };

    // settings project { domain domain ... }: what a block of settings may say, each
    // line one of these fields, with a value of its type. Written in a library.
    struct settings_declaration {
        std::string name;
        std::vector<field> fields;
    };

    struct command_declaration {
        qualified_name name;
        bool has_body = false;
        std::vector<statement> body;
    };

    // One value in a view: either something shown as it is (book.title), or a name
    // given a computed value (total = count(signup)).
    struct view_value {
        location where;
        std::optional<std::string> name;
        expression_ptr value;
    };

    // A list in a view: the view's rows, or, with a name, one of several lists, like
    // an issue's comments.
    struct view_each {
        location where;
        std::optional<std::string> name;    // comments = each comment ...
        bool changes = false;               // each change in issue: the changes an entity keeps
        // each issue in project: the rows whose field pointing at the view's project
        // holds it; each change in issue in project, the changes of a project's issues.
        std::vector<std::pair<std::string, location>> within;
        location where_word;              // where its where starts, and how long the
        std::size_t where_length = 0;     // condition after it runs, for a fix to say it as in
        std::vector<expression_ptr> order;  // -due_at sorts in reverse
        std::optional<int> limit;           // limit 50: only the first, once ordered
        expression_ptr source;
        expression_ptr condition;
        std::vector<view_value> rows;
    };

    struct view_declaration {
        std::string name;
        std::optional<std::string> per;
        bool is_public = false;
        std::optional<std::string> readers;  // readers member: the people a member names may read it
        location readers_where;
        std::vector<expression_ptr> reader_people;  // readers report.author: who a field of the entity names
        expression_ptr public_when;          // public when project.visibility == public
        std::vector<view_value> values;
        std::vector<view_each> each;
    };

    struct role_declaration {
        std::string name;
        std::optional<std::string> per;   // role maintainer per project from member: held within a project
        std::optional<std::string> from;  // the entity that grants it
        location per_where;
        std::vector<qualified_name> permissions;
    };

    // A role each project starts with: maintainer "Maintainer"  issue::create ...
    struct default_role {
        std::string name;
        location where;
        std::string title;
        std::vector<qualified_name> permissions;
    };

    // roles role per project from member { ... }: the roles each project defines for
    // itself, as records of role that its people edit, each listing the commands it
    // allows; a member gives its person the role it points at. The block holds the
    // roles each project starts with.
    struct roles_declaration {
        std::string entity;  // role
        location entity_where;
        std::string per;     // project
        std::string from;    // member
        location per_where;
        std::vector<default_role> defaults;
        bool defined = false;  // made from define role, with the role and member the language declares
    };

    // define role maintainer "Maintainer" in project { project::update ... }: a role
    // each project starts with, and the commands it allows, one a line. The language
    // keeps a project's roles as role records, and who holds them as member records.
    struct define_role_declaration {
        std::string name;
        location where;
        std::string title;
        std::string in;  // project
        location in_where;
        std::vector<qualified_name> permissions;
    };

    struct function_declaration {
        std::string name;
        std::vector<std::string> parameters;
        std::vector<statement> body;
    };

    struct screen_item;

    struct content_block {
        // A menu holds links, with the rest of the screen beside it; a region is one
        // part of the screen's layout, like main or side, named in title.
        // A heading's items sit on the screen's title row, at its end.
        enum class kind { hero, section, menu, region, heading };
        kind type = kind::section;
        std::string title;
        std::optional<std::string> anchor;
        std::string hue;  // section "Wiki" color violet: one of the library's hues, or none
        location hue_where;
        std::vector<screen_item> items;
    };

    struct content_text {
        enum class kind { text, code, markdown };
        kind type = kind::text;
        std::string value;
        expression_ptr when;  // text "Implemented by {issue_page.implementer}" when issue_page.implementer != none
        bool subtitle = false;  // subtitle "{project_page.summary}": under the screen's title, as running words
    };

    struct content_link {
        std::string label;
        std::string target;                            // a route inside the link's namespace, #anchor, or https:// address
        std::optional<qualified_name> namespace_name;  // link namespace projects "See the projects"
        std::optional<std::string> icon;               // icon workflow: drawn as that, its words still naming it
        location icon_where;
        std::string built;                             // link build.release: what the site was built from, release or source
        location built_where;
    };

    struct table_column {
        location where;
        expression_ptr value;
        std::optional<std::string> label;
        // delete "Remove" when person != me: a row's button shown only on the rows
        // where it holds, read from the row's own fields, and its text as written.
        expression_ptr when;
        std::string when_written;
        std::optional<std::string> icon;  // issue::create "New issue" icon add: a toolbar's button drawn as that
        location icon_where;
    };

    struct table_item {
        qualified_name view;
        std::optional<std::string> list;  // table issue_page.comments: one of the view's lists
        std::vector<table_column> columns;
        std::optional<std::string> link;  // the screen each row opens, like /books/:book
        location link_where;
        std::optional<std::string> by;    // table project_page.issues by status: a tab for each of its choices
        location by_where;
        // by phase over project_page.phases: a tab for each of a list's records, in its order.
        std::optional<qualified_name> by_over;
        std::string by_over_list;
        std::vector<std::string> search;  // search title labels: a box that finds rows by these
        location search_where;
        std::optional<std::string> sort;  // sort -number: rows in this order, - for largest or latest first
        bool sort_descending = false;
        location sort_where;
        std::optional<int> page;          // page 25: this many rows at a time
        location page_where;
        std::optional<std::string> reorder;  // reorder position: rows put in order by dragging, which sets this
        bool hide_empty = false;             // hide when empty: not there at all while it has no rows
        location reorder_where;
        // only due <= 2 weeks from now: the rows it keeps, worked out when it's shown,
        // like a card's filter.
        expression_ptr only;
        location only_where;
        // tint by priority: each row colored by how urgent its choice is, the first of
        // the field's choices the most, as Trac colored a ticket's priority.
        std::string hue;  // color violet: its rows' links in one of the library's hues
        location hue_where;
        std::optional<std::string> tint;
        location tint_where;
    };

    // grid project_page.steps by from and to over project_page.phases { roles.title }:
    // what goes between two of a list's things, a row and a column for each, and in a
    // cell what it holds, like the roles that may take a move.
    struct grid_item {
        qualified_name view;
        std::optional<std::string> list;
        std::string from, to;  // the fields naming an entry's row and column
        location from_where, to_where;
        qualified_name over;
        std::string over_list;
        expression_ptr cell;   // what a cell shows, or none for a mark
        // diagram ...: the same drawn, each thing a box and each entry an arrow.
        bool drawn = false;
    };

    struct form_field {
        location where;
        std::string name;
        expression_ptr value;             // = suggest_code(start_date)
        std::optional<std::string> hint;
        std::optional<std::string> label; // body "Comment": what the field is called, rather than its name
    };

    struct form_item {
        std::vector<qualified_name> commands;
        std::vector<form_field> fields;
        std::optional<std::string> submit;  // form project::update "Save changes": what its button says
    };

    struct confirm_item {
        qualified_name command;
        std::string message;
    };

    // issue::close "Close issue" when issue_page.status == status::open: a command's
    // button, named for what it does, and shown only while the page says it applies.
    struct button_item {
        qualified_name command;
        std::optional<std::string> label;
        expression_ptr when;
        // issue::move along project_page.steps: a button for each step from where the
        // entity is now that the person's roles may take, moving it to the step's to.
        std::optional<qualified_name> along;  // the view, like project_page
        std::string along_list;               // its list of steps, like steps
        std::optional<std::string> icon;      // icon edit: drawn as that, its words still naming it
        location icon_where;
    };

    // board project_page.issues by phase over project_page.phases { ... }: a list's
    // rows as cards in columns, a column for each of the over list's things, in its
    // order; with move issue::move along project_page.steps, a card is dragged to
    // another column along a step the person's roles may take.
    struct board_item {
        qualified_name view;
        std::optional<std::string> list;
        std::string by;  // the field naming a card's column, like phase
        location by_where;
        qualified_name over;
        std::string over_list;
        std::optional<std::string> link;  // the screen a card opens, like /:project/issues/:issue
        location link_where;
        std::optional<button_item> move;
        std::vector<table_column> columns;  // what a card shows, its title first
        std::vector<std::string> search;    // search title labels: a box that finds cards by these
        std::optional<std::string> tint;    // tint by priority: each card colored as a table's row
        location tint_where;
    };

    // cards project_page.boards link /:project/boards/:board { ... }: a list's rows as
    // large cards, each with what its block shows, its title first; a tally of what
    // it holds, like its issues by phase; and filters, links that open it filtered.
    struct cards_filter {
        std::string label;
        location where;
        expression_ptr condition;  // author == me
        std::string hue;           // color red: one of the library's hues, or none
        location hue_where;
    };

    struct cards_item {
        qualified_name view;
        std::optional<std::string> list;
        std::optional<std::string> link;
        location link_where;
        std::vector<table_column> columns;
        // tally project_page.issues by board and phase
        std::optional<qualified_name> tally;
        std::string tally_list, tally_by, tally_and;
        location tally_where;
        std::vector<cards_filter> filters;
    };

    // component workbench: a hand-written React component, components/workbench.tsx
    // beside the .one file, drawn where the item is.
    struct component_item {
        std::string name;
    };

    // The file a component is written in, beside the .one file that draws it.
    inline std::string component_file(const std::string& one_file, const std::string& name) {
        auto folder = std::filesystem::path(one_file).parent_path();
        return (folder / "components" / (name + ".tsx")).string();
    }

    // thread issue_page.comments: a list of what people wrote, each with who wrote it
    // and when. timeline issue_page.history: an entity's changes, each said as what
    // happened, like "Ada closed this". Both read one of a view's lists.
    struct thread_item {
        qualified_name view;
        std::string list;
    };

    struct timeline_item {
        qualified_name view;
        std::string list;
        std::optional<std::string> link;  // timeline project_page.timeline link /:project/issues/:issue: what each change was to
        location link_where;
        std::optional<std::string> title;  // timeline news.changes "What's new": said above it, shown while it holds something
        // new since news.seen: the changes after a time a view holds are marked new, and
        // seen reader::create runs once they've been looked at, to say so.
        std::optional<qualified_name> since;
        std::string since_field;
        location since_where;
        std::optional<qualified_name> seen;
        location seen_where;
    };

    // copy issue_page "Copy issue": a button that copies everything a view holds, as
    // Markdown, to be pasted somewhere else whole.
    struct copy_item {
        qualified_name view;
        std::optional<std::string> label;
    };

    // details issue_page { status "Status"  implementer "Implemented by" }: a view's
    // values, each beside what it is, the ones with nothing in them left out.
    struct details_item {
        qualified_name view;
        std::vector<table_column> fields;
        std::optional<std::string> tint;  // tint by priority: the box colored as a table's row
        location tint_where;
    };

    // find "Search this project" { project_page.issues "Issues" link /:project/:issue by
    // number title labels }: a box that finds rows of several lists as it's typed in,
    // each list's under its own label, each row a link.
    struct find_source {
        location where;
        qualified_name view;
        std::string list;
        std::string label;
        std::optional<std::string> link;
        location link_where;
        std::vector<std::string> by;  // the fields it's found by, the first shown, a number before it
    };

    struct find_item {
        std::string label;
        std::vector<find_source> sources;
    };

    struct screen_item {
        location where;
        std::variant<content_block, content_text, content_link, table_item, form_item,
                     confirm_item, button_item, component_item, thread_item, timeline_item, copy_item, details_item, grid_item, board_item, cards_item,
                     find_item>
            node;
    };

    struct screen_declaration {
        std::string title;
        bool title_is_name = false;  // screen docs /:page, rather than screen "Shelf" /shelf
        std::string route;
        std::optional<std::string> layout;  // layout two_columns: how its regions are laid out
        location layout_where;
        // under /:project/boards/:board "{issue_page.board_title}": the page above it,
        // though its address doesn't say so, and what that page is called from here.
        std::optional<std::string> under;
        std::string under_title;
        location under_where;
        std::vector<screen_item> items;
    };

    struct picker_declaration {
        std::string entity;
        std::string view;
    };

    struct webhook_handler {
        std::string event;  // commit or pull_request
        location where;
        std::vector<statement> body;
    };

    // webhook github /hooks/github { for project by repository  on commit { ... } }
    struct webhook_declaration {
        std::string provider;
        location provider_where;
        std::string route;
        std::string scope;       // project
        std::string repository;  // the field naming its repository
        location scope_where;
        std::vector<webhook_handler> handlers;
    };

    // backend deploy: Go written by hand, backend/deploy.go beside the .one file, built
    // into the namespace's package with the generated code.
    struct backend_declaration {
        std::string name;
    };

    // each project { start = phase::triage }: a command's body done to every stored
    // entity of a kind, or to those its where names, like each role where name ==
    // "developer".
    struct once_step {
        std::string entity;
        location entity_where;
        expression_ptr where;  // name == "developer", or none for all of them
        std::vector<statement> body;
        bool remove = false;  // delete each invitation where email == me.email
    };

    // once "2026-10-07 workflows" { each project { ... } }: a change to what's stored
    // that a deploy brings, done the first time the backend starts with it and never
    // again, its steps in order.
    struct once_declaration {
        std::string name;
        std::vector<once_step> steps;
        // on signin { ... }: done each time someone opens the app signed in, as them,
        // with me.email the email their sign-in vouches for, rather than once.
        bool signin = false;
    };

    // The file a backend is written in, beside the .one file that names it.
    inline std::string backend_file(const std::string& one_file, const std::string& name) {
        auto folder = std::filesystem::path(one_file).parent_path();
        return (folder / "backend" / (name + ".go")).string();
    }

    struct declaration;

    struct namespace_declaration {
        std::string name;
        std::vector<declaration> declarations;
        std::optional<std::string> at;  // namespace studio at /: where its screens are, rather than /studio
    };

    // footer { text "© {year} Ada" }: what's at the foot of every page, drawn with a
    // screen's items, in one line (bar) or in columns, each a section.
    struct footer_declaration {
        std::string layout = "bar";
        location layout_where;
        std::vector<screen_item> items;
    };

    // header { badge news.unread }: what's beside the site's name on every page. A
    // badge shows a number a view holds, like what's new to the person reading,
    // when it's more than none.
    struct header_badge {
        location where;
        std::string view;   // news
        std::string value;  // unread
    };

    struct header_declaration {
        std::vector<header_badge> badges;
    };

    struct declaration {
        location where;
        std::variant<project_declaration, namespace_declaration, format_declaration,
                     entity_declaration, command_declaration, view_declaration,
                     role_declaration, function_declaration, screen_declaration, picker_declaration,
                     webhook_declaration, backend_declaration, enum_declaration, roles_declaration, once_declaration,
                     import_declaration, settings_declaration, footer_declaration, header_declaration, define_role_declaration>
            node;
    };

    struct file {
        std::string path;
        std::vector<declaration> declarations;
        // Where an import goes: before the first declaration, and the comment that's
        // its own, below the comments that open the file, like its license.
        location imports_at{1, 1};
    };

    // The choice a name stands for, when it's one of a field's: written with the
    // field's enum, as it has to be, like visibility::public, or plainly, as public.
    inline std::optional<std::string> choice_of(const qualified_name& name, const field& f) {
        if (f.choices.empty()) return std::nullopt;
        const std::string* value = nullptr;
        if (name.parts.size() == 1) value = &name.parts[0];
        if (name.parts.size() == 2 && (name.parts[0] == f.name || name.parts[0] == f.enum_name)) value = &name.parts[1];
        if (!value || std::find(f.choices.begin(), f.choices.end(), *value) == f.choices.end()) return std::nullopt;
        return *value;
    }

    // unread = count(changes where created_at > seen): a count of a view's own list,
    // of the rows that pass, compared with its other values. The list it counts, and
    // its where, when a value is one.
    struct row_count {
        const view_each* list = nullptr;
        const where_expression* where = nullptr;  // none when it counts every row
    };

    inline row_count counted_rows(const view_declaration& v, const view_value& value) {
        auto* call = std::get_if<call_expression>(&value.value->node);
        auto* callee = call ? std::get_if<name_expression>(&call->callee->node) : nullptr;
        if (!callee || callee->name.text() != "count" || call->arguments.size() != 1) return {};
        const expression& argument = *call->arguments[0];
        auto* w = std::get_if<where_expression>(&argument.node);
        auto* source = std::get_if<name_expression>(&(w ? *w->source : argument).node);
        if (!source || source->name.parts.size() != 1) return {};
        for (const auto& each : v.each) {
            if (each.name && *each.name == source->name.parts[0]) return {&each, w};
        }
        return {};
    }

} // namespace one::language
