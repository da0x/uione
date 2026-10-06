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

    struct binary_expression {
        token_kind op = token_kind::equal;
        expression_ptr left;
        expression_ptr right;
    };

    struct expression {
        location where;
        std::variant<literal_expression, name_expression, member_expression, call_expression,
                     where_expression, unary_expression, binary_expression>
            node;
    };

    // statements, in command and function bodies

    struct statement;

    struct require_statement {
        expression_ptr condition;
        std::string message;
    };

    struct permission_statement {  // anyone, authenticated, owner, or a permission
        qualified_name permission;
    };

    struct clear_statement {
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
    };

    struct return_statement {
        expression_ptr value;
    };

    struct if_statement {
        expression_ptr condition;
        std::vector<statement> then_body;
        std::vector<statement> else_body;
    };

    struct statement {
        location where;
        std::variant<require_statement, permission_statement, clear_statement, assign_statement,
                     create_statement, list_statement, return_statement, if_statement>
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

    struct format_declaration {
        std::string name;
        std::string pattern;  // raw, like AAA-9999
        std::string example;  // raw, like HIS-0142
    };

    struct field {
        std::string name;
        location where;
        std::optional<qualified_name> type;  // none when the field has choices, or no type
        std::vector<std::string> choices;    // on_shelf | lent | withdrawn
        std::vector<std::string> choice_labels;  // mit "MIT": how each choice is shown, or empty for its name
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

    struct function_declaration {
        std::string name;
        std::vector<std::string> parameters;
        std::vector<statement> body;
    };

    struct screen_item;

    struct content_block {
        enum class kind { hero, section, menu };  // a menu holds links, with the rest of the screen beside it
        kind type = kind::section;
        std::string title;
        std::optional<std::string> anchor;
        std::vector<screen_item> items;
    };

    struct content_text {
        enum class kind { text, code, markdown };
        kind type = kind::text;
        std::string value;
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
    };

    struct table_item {
        qualified_name view;
        std::optional<std::string> list;  // table issue_page.comments: one of the view's lists
        std::vector<table_column> columns;
        std::optional<std::string> link;  // the screen each row opens, like /books/:book
        location link_where;
    };

    struct form_field {
        location where;
        std::string name;
        expression_ptr value;             // = suggest_code(start_date)
        std::optional<std::string> hint;
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

    struct screen_item {
        location where;
        std::variant<content_block, content_text, content_link, table_item, form_item,
                     confirm_item, button_item, component_item>
            node;
    };

    struct screen_declaration {
        std::string title;
        bool title_is_name = false;  // screen docs /:page, rather than screen "Shelf" /shelf
        std::string route;
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
                     webhook_declaration, backend_declaration>
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
        if (name.parts.size() == 2 && name.parts[0] == f.name) value = &name.parts[1];
        if (!value || std::find(f.choices.begin(), f.choices.end(), *value) == f.choices.end()) return std::nullopt;
        return *value;
    }

} // namespace one::language
