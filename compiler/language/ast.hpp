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
        enum class kind { string, number };
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
                     delete_statement>
            node;
    };

    // declarations

    struct setting {
        std::string key;
        std::string value;
        bool is_string = false;
        location where;
        std::string to;  // redirect "/install.sh" "https://www.uione.io/install.sh": where it goes
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
        bool changes = false;               // each change of issue: the changes an entity keeps
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
    };

    struct table_column {
        location where;
        expression_ptr value;
        std::optional<std::string> label;
        // delete "Remove" when person != me: a row's button shown only on the rows
        // where it holds, read from the row's own fields, and its text as written.
        expression_ptr when;
        std::string when_written;
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
        location reorder_where;
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
    };

    // cards project_page.boards link /:project/boards/:board { ... }: a list's rows as
    // large cards, each with what its block shows, its title first; a tally of what
    // it holds, like its issues by phase; and filters, links that open it filtered.
    struct cards_filter {
        std::string label;
        location where;
        expression_ptr condition;  // author == me
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
    };

    struct screen_item {
        location where;
        std::variant<content_block, content_text, content_link, table_item, form_item,
                     confirm_item, button_item, component_item, thread_item, timeline_item, copy_item, details_item, grid_item, board_item, cards_item>
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
    };

    // once "2026-10-07 workflows" { each project { ... } }: a change to what's stored
    // that a deploy brings, done the first time the backend starts with it and never
    // again, its steps in order.
    struct once_declaration {
        std::string name;
        std::vector<once_step> steps;
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

    struct declaration {
        location where;
        std::variant<project_declaration, namespace_declaration, format_declaration,
                     entity_declaration, command_declaration, view_declaration,
                     role_declaration, function_declaration, screen_declaration, picker_declaration,
                     webhook_declaration, backend_declaration, enum_declaration, roles_declaration, once_declaration>
            node;
    };

    struct file {
        std::string path;
        std::vector<declaration> declarations;
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

} // namespace one::language
