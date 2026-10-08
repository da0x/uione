// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A recursive-descent parser for .one files. Each parse_ function reads one kind of
// thing and leaves the parser just after it.
//
// When something is wrong, the parser reports it and skips to the next declaration,
// so one mistake doesn't hide the rest of the file's errors.

#pragma once

#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "language/ast.hpp"
#include "language/diagnostics.hpp"
#include "language/lexer.hpp"

namespace one::language {

    class parser {
    public:
        parser(std::string path, std::string_view source, diagnostics& out)
            : path_(std::move(path)), source_(source), out_(out),
              tokens_(lexer(path_, source_, out_).tokens()) {
            int depth = 0;
            for (const auto& t : tokens_) {
                depth_.push_back(depth);
                if (t.kind == token_kind::left_brace) ++depth;
                if (t.kind == token_kind::right_brace) depth = std::max(0, depth - 1);
            }
        }

        file parse() {
            file result;
            result.path = path_;
            result.declarations = parse_declarations(false);
            return result;
        }

    private:
        struct parse_error {};

        std::string path_;
        std::string_view source_;
        diagnostics& out_;
        std::vector<token> tokens_;
        std::vector<int> depth_;  // how many braces are open before each token
        std::size_t pos_ = 0;
        bool relative_times_ = false;  // while reading a filter, where 7 days ago is a time

        // reading tokens

        const token& peek(std::size_t ahead = 0) const {
            return tokens_[std::min(pos_ + ahead, tokens_.size() - 1)];
        }
        bool at(token_kind kind) const { return peek().kind == kind; }
        bool at_word(std::string_view word) const {
            return at(token_kind::identifier) && peek().text == word;
        }
        bool at_line_end() const {
            return at(token_kind::newline) || at(token_kind::right_brace) ||
                   at(token_kind::end_of_file);
        }

        const token& advance() {
            const token& t = tokens_[pos_];
            if (pos_ + 1 < tokens_.size()) ++pos_;
            return t;
        }

        static std::string found(const token& t) {
            if (t.kind == token_kind::identifier) return "'" + t.text + "'";
            return std::string(describe(t.kind));
        }

        [[noreturn]] void fail(location where, std::string message, std::optional<fix> resolved = std::nullopt) {
            out_.push_back({path_, where, std::move(message), std::move(resolved)});
            throw parse_error{};
        }

        [[noreturn]] void fail_expecting(std::string_view what) {
            fail(peek().where, "expected " + std::string(what) + ", found " + found(peek()));
        }

        const token& expect(token_kind kind, std::string_view what) {
            if (!at(kind)) fail_expecting(what);
            return advance();
        }

        void skip_newlines() {
            while (at(token_kind::newline)) advance();
        }

        // For a loop over the lines of a { } block: true while there's another line
        // to read, false at the closing brace. A file that ends first is an error,
        // so no loop can run past the end of the file.
        bool in_block() {
            skip_newlines();
            if (at(token_kind::right_brace)) return false;
            if (at(token_kind::end_of_file)) fail_expecting("'}'");
            return true;
        }

        // A line ends at a line break. A closing brace also ends it, which is what lets
        // a short block sit on one line: if a { return b }.
        void end_line() {
            if (at(token_kind::right_brace) || at(token_kind::end_of_file)) return;
            expect(token_kind::newline, "the end of the line");
        }

        // The source text from `from` up to the end of the line, or to a { if that
        // comes first, trimmed. Used for a format's pattern and example, which aren't
        // made of ordinary tokens. The tokens inside that text are skipped.
        std::string raw_text(std::size_t from, bool stop_at_brace) {
            std::size_t stop = from;
            while (stop < source_.size() && source_[stop] != '\n' &&
                   !(stop_at_brace && source_[stop] == '{') &&
                   !(source_[stop] == '/' && stop + 1 < source_.size() && source_[stop + 1] == '/')) {
                ++stop;
            }
            while (!at(token_kind::newline) && !at(token_kind::end_of_file) && peek().begin < stop) {
                advance();
            }
            std::string_view text = source_.substr(from, stop - from);
            auto first = text.find_first_not_of(" \t\r");
            if (first == std::string_view::npos) return "";
            auto last = text.find_last_not_of(" \t\r");
            return std::string(text.substr(first, last - first + 1));
        }

        qualified_name parse_qualified_name(std::string_view what) {
            qualified_name name;
            const token& first = expect(token_kind::identifier, what);
            name.where = first.where;
            name.parts.push_back(first.text);
            while (at(token_kind::scope)) {
                advance();
                name.parts.push_back(expect(token_kind::identifier, "a name after '::'").text);
            }
            return name;
        }

        // declarations

        std::vector<declaration> parse_declarations(bool inside_block) {
            std::vector<declaration> result;
            for (;;) {
                skip_newlines();
                if (at(token_kind::end_of_file)) break;
                if (inside_block && at(token_kind::right_brace)) break;
                std::size_t start = pos_;
                try {
                    result.push_back(parse_declaration());
                } catch (const parse_error&) {
                    recover(start, inside_block);
                }
            }
            return result;
        }

        // After an error, skip to the start of the next declaration at the same depth,
        // or to the brace that closes the enclosing block.
        void recover(std::size_t start, bool inside_block) {
            int depth = depth_[start];
            if (pos_ <= start) advance();
            while (!at(token_kind::end_of_file)) {
                if (inside_block && at(token_kind::right_brace) && depth_[pos_] == depth) return;
                bool line_start = pos_ > 0 && tokens_[pos_ - 1].kind == token_kind::newline;
                if (line_start && at(token_kind::identifier) && depth_[pos_] == depth) return;
                advance();
            }
        }

        declaration parse_declaration() {
            if (!at(token_kind::identifier)) fail_expecting("a declaration");
            location where = peek().where;
            const std::string& word = peek().text;
            if (word == "project") return {where, parse_project()};
            // import one
            if (word == "import" && peek(1).kind == token_kind::identifier) {
                advance();
                import_declaration import{advance().text, where};
                end_line();
                return {where, std::move(import)};
            }
            // settings project { ... }
            if (word == "settings" && peek(1).kind == token_kind::identifier && peek(2).kind == token_kind::left_brace) {
                advance();
                settings_declaration settings;
                settings.name = advance().text;
                expect(token_kind::left_brace, "'{'");
                while (in_block()) settings.fields.push_back(parse_field());
                expect(token_kind::right_brace, "'}'");
                end_line();
                return {where, std::move(settings)};
            }
            if (word == "namespace") return {where, parse_namespace()};
            if (word == "format") return {where, parse_format()};
            if (word == "entity") return {where, parse_entity()};
            if (word == "command") return {where, parse_command()};
            if (word == "view") return {where, parse_view()};
            if (word == "role") return {where, parse_role()};
            if (word == "roles") return {where, parse_roles()};
            if (word == "function") return {where, parse_function()};
            if (word == "screen") return {where, parse_screen()};
            if (word == "picker") return {where, parse_picker()};
            if (word == "enum") return {where, parse_enum()};
            if (word == "webhook") return {where, parse_webhook()};
            if (word == "once" && peek(1).kind == token_kind::string) return {where, parse_once()};
            if (word == "on" && peek(1).kind == token_kind::identifier && peek(1).text == "signin") {
                advance();
                return {where, parse_once(true)};
            }
            if (word == "backend") {
                advance();
                backend_declaration backend{expect(token_kind::identifier, "the backend's name, like deploy").text};
                end_line();
                return {where, std::move(backend)};
            }
            if (word == "fn") fail(where, "functions are declared with the whole word: function, not fn");
            fail(where, "'" + word + "' doesn't start a declaration; expected project, namespace, "
                        "format, enum, entity, command, view, role, roles, function, screen, picker, webhook, backend or once");
        }

        project_declaration parse_project() {
            advance();
            project_declaration project;
            project.name = expect(token_kind::identifier, "the project's name").text;
            expect(token_kind::left_brace, "'{'");
            auto one_setting = [&]() {
                setting s;
                const token& key = expect(token_kind::identifier, "a setting, like domain");
                s.key = key.text;
                s.where = key.where;
                // unread news.changes since news.seen: how many of a person's own list are
                // newer than a value of theirs, said beside the app's name.
                if (key.text == "unread") {
                    auto dotted = [&](const std::string& what) {
                        std::string view = expect(token_kind::identifier, what).text;
                        expect(token_kind::dot, "'.' after " + view);
                        return view + "." + expect(token_kind::identifier, what).text;
                    };
                    s.value = dotted("the person's own list, like news.changes");
                    if (!at_word("since")) fail_expecting("since and when they last looked, like since news.seen");
                    advance();
                    s.to = dotted("when they last looked, like news.seen");
                    end_line();
                    return s;
                }
                s.value_where = peek().where;
                if (at(token_kind::string)) {
                    s.value = advance().text;
                    s.is_string = true;
                    if (at(token_kind::string)) s.to = advance().text;  // redirect "/from" "to"
                } else {
                    // corners square, or in full, corners one::corners::square.
                    qualified_name value = parse_qualified_name("the setting's value");
                    s.value = value.parts.back();
                    if (value.parts.size() > 1) s.qualified = value.text();
                }
                end_line();
                return s;
            };
            while (in_block()) {
                // environment staging { ... }: a place the project runs, and what's its own there.
                if (at_word("environment") && peek(1).kind == token_kind::identifier && peek(2).kind == token_kind::left_brace) {
                    environment_block environment;
                    environment.where = advance().where;
                    environment.name = advance().text;
                    advance();
                    while (in_block()) environment.settings.push_back(one_setting());
                    environment.end = expect(token_kind::right_brace, "'}'").where;
                    end_line();
                    project.environments.push_back(std::move(environment));
                    continue;
                }
                project.settings.push_back(one_setting());
            }
            project.end = expect(token_kind::right_brace, "'}'").where;
            end_line();
            return project;
        }

        namespace_declaration parse_namespace() {
            advance();
            namespace_declaration ns;
            ns.name = expect(token_kind::identifier, "the namespace's name").text;
            if (at_word("at")) {
                advance();
                ns.at = expect(token_kind::route, "where the namespace's screens are, like / or /docs").text;
            }
            expect(token_kind::left_brace, "'{'");
            ns.declarations = parse_declarations(true);
            expect(token_kind::right_brace, "'}' to close namespace " + ns.name);
            end_line();
            return ns;
        }

        format_declaration parse_format() {
            advance();
            format_declaration format;
            const token& name = expect(token_kind::identifier, "the format's name");
            format.name = name.text;
            format.pattern = raw_text(name.end, true);
            if (format.pattern.empty()) fail(name.where, "format " + format.name + " needs a pattern, like AAA-9999");
            if (at(token_kind::left_brace)) {
                advance();
                while (in_block()) {
                    if (!at_word("example")) fail_expecting("'example'");
                    const token& word = advance();
                    format.example = raw_text(word.end, false);
                    end_line();
                }
                expect(token_kind::right_brace, "'}'");
            }
            end_line();
            return format;
        }

        static bool is_field_rule(std::string_view word) {
            return word == "required" || word == "unique" || word == "key" || word == "after";
        }

        entity_declaration parse_entity() {
            advance();
            entity_declaration entity;
            entity.name = expect(token_kind::identifier, "the entity's name").text;
            if (at_word("invites")) {
                advance();
                entity.invites_where = peek().where;
                entity.invites = expect(token_kind::identifier, "what it invites to be, like member").text;
            }
            if (at_word("history")) {
                advance();
                if (at_word("of")) {
                    advance();
                    entity.history_of_where = peek().where;
                    entity.history_of = expect(token_kind::identifier, "the entity whose history keeps this one's changes, like issue").text;
                } else {
                    entity.history = true;
                }
            }
            expect(token_kind::left_brace, "'{'");
            while (in_block()) {
                entity.fields.push_back(parse_field());
            }
            expect(token_kind::right_brace, "'}'");
            end_line();
            return entity;
        }

        field parse_field() {
            field f;
            const token& name = expect(token_kind::identifier, "a field's name");
            f.name = name.text;
            f.where = name.where;
            if (at(token_kind::identifier) && !is_field_rule(peek().text)) {
                if (peek(1).kind == token_kind::pipe || (peek(1).kind == token_kind::string && peek(2).kind == token_kind::pipe)) {
                    // Choices are an enum, and say so: status enum open | closed.
                    fail(peek().where, "write enum before a field's choices, like " + f.name + " enum " + peek().text + " | ...",
                         fix{peek().where, 0, "enum "});
                }
                if (at_word("enum")) {
                    advance();
                    // Each choice may say how it's shown: mit "MIT" | apache_2_0 "Apache-2.0".
                    auto choice = [&](const token& name) {
                        f.choices.push_back(name.text);
                        f.choice_labels.push_back(at(token_kind::string) ? advance().text : "");
                    };
                    if (at(token_kind::left_brace)) {
                        // Or in a block, each on its own line or several to a line:
                        // visibility enum { public  private } = visibility::public.
                        advance();
                        while (in_block()) {
                            while (!at_line_end()) choice(expect(token_kind::identifier, "a choice, like open"));
                            end_line();
                        }
                        expect(token_kind::right_brace, "'}'");
                    } else {
                        choice(expect(token_kind::identifier, "the enum's first choice, like open"));
                        while (at(token_kind::pipe)) {
                            advance();
                            choice(expect(token_kind::identifier, "a choice after '|'"));
                        }
                    }
                } else {
                    f.type = parse_qualified_name("the field's type");
                    if (f.type->text() == "list" && at_word("of")) {
                        advance();
                        f.list = true;
                        f.type = parse_qualified_name("what the list holds, like label or user");
                    }
                    if (f.type->text() == "serial" && at_word("per")) {
                        advance();
                        f.per = expect(token_kind::identifier, "what it's counted per, like book").text;
                    }
                }
            }
            while (at(token_kind::identifier)) {
                const token& rule = peek();
                if (rule.text == "required") {
                    advance();
                    f.required = true;
                } else if (rule.text == "unique") {
                    advance();
                    f.unique = true;
                } else if (rule.text == "key") {
                    advance();
                    f.key = true;
                } else if (rule.text == "after") {
                    advance();
                    f.after = expect(token_kind::identifier, "the field it has to come after").text;
                } else {
                    fail(rule.where, "'" + rule.text + "' isn't a field rule; expected required, "
                                     "unique, key or after");
                }
            }
            if (at(token_kind::assign)) {
                advance();
                f.initial = parse_expression();
            }
            end_line();
            return f;
        }

        command_declaration parse_command() {
            advance();
            command_declaration command;
            command.name = parse_qualified_name("the command's name, like book::create");
            if (at(token_kind::left_brace)) {
                command.has_body = true;
                command.body = parse_statement_block();
            }
            end_line();
            return command;
        }

        once_declaration parse_once(bool signin = false) {
            advance();
            once_declaration once;
            once.signin = signin;
            if (!signin) once.name = advance().text;
            expect(token_kind::left_brace, "'{'");
            while (in_block()) {
                // delete each invitation where email == me.email
                once_step step;
                if (at_word("delete") && peek(1).kind == token_kind::identifier && peek(1).text == "each") {
                    advance();
                    step.remove = true;
                }
                if (!at_word("each")) fail_expecting("each and what it changes, like each project { ... }");
                advance();
                step.entity_where = peek().where;
                step.entity = expect(token_kind::identifier, "the entity it changes, like project").text;
                if (at_word("where")) {
                    advance();
                    step.where = parse_expression();
                }
                if (!step.remove) step.body = parse_statement_block();
                end_line();
                once.steps.push_back(std::move(step));
            }
            expect(token_kind::right_brace, "'}'");
            end_line();
            return once;
        }

        view_declaration parse_view() {
            advance();
            view_declaration view;
            view.name = expect(token_kind::identifier, "the view's name").text;
            for (;;) {
                if (at_word("per")) {
                    advance();
                    view.per = expect(token_kind::identifier, "what the view is per, like user").text;
                } else if (at_word("public")) {
                    advance();
                    view.is_public = true;
                } else {
                    break;
                }
            }
            expect(token_kind::left_brace, "'{'");
            while (in_block()) {
                if (at_word("order")) {
                    fail(peek().where, "order goes inside the list it sorts: each book { order title ... }");
                } else if (at_word("readers") && peek(1).kind == token_kind::identifier) {
                    location where = advance().where;
                    expression_ptr readers = parse_postfix();
                    auto* name = std::get_if<name_expression>(&readers->node);
                    if (name && name->name.parts.size() == 1) {
                        if (view.readers) fail(where, "a view's readers come from one entity, like readers member; another readers line names people, like readers report.author");
                        view.readers_where = where;
                        view.readers = name->name.parts[0];
                    } else {
                        view.reader_people.push_back(std::move(readers));
                    }
                    end_line();
                } else if (at_word("public") && peek(1).kind == token_kind::identifier && peek(1).text == "when") {
                    advance();
                    advance();
                    view.public_when = parse_expression();
                    end_line();
                } else if (at_word("each")) {
                    view.each.push_back(parse_each());
                } else {
                    location where = peek().where;
                    view_value value;
                    value.where = where;
                    value.name = expect(token_kind::identifier, "'each', or a value like total = count(...)").text;
                    expect(token_kind::assign, "'='");
                    if (at_word("each")) {
                        view.each.push_back(parse_each());
                        view.each.back().name = value.name;
                        view.each.back().where = where;
                        continue;
                    }
                    value.value = parse_expression();
                    end_line();
                    view.values.push_back(std::move(value));
                }
            }
            expect(token_kind::right_brace, "'}'");
            end_line();
            return view;
        }

        // A sort key: a value, or a value with - in front of it to sort in reverse.
        // It stops before any operator, because order done -created_at is two keys,
        // not a subtraction.
        // A key, then ascending, which it is unless it says, or descending: newest or
        // largest first. Descending is kept as the key reversed.
        expression_ptr parse_order_key() {
            if (at(token_kind::minus)) {
                const token& minus = advance();
                std::size_t begin = peek().begin;
                auto key = parse_postfix();
                std::string written(source_.substr(begin, tokens_[pos_ - 1].end - begin));
                fail(minus.where, "write " + written + " descending; a - in front isn't said in words",
                     fix{minus.where, tokens_[pos_ - 1].end - minus.begin, written + " descending"});
            }
            auto key = parse_postfix();
            if (at_word("ascending")) {
                advance();
            } else if (at_word("descending")) {
                location where = advance().where;
                auto e = std::make_unique<expression>();
                e->where = where;
                e->node = unary_expression{token_kind::minus, std::move(key)};
                return e;
            }
            return key;
        }

        view_each parse_each() {
            view_each each;
            each.where = advance().where;
            if (at_word("change") && peek(1).kind == token_kind::identifier && peek(1).text == "of") {
                advance();
                advance();
                each.changes = true;
            }
            each.source = parse_postfix();
            if (at_word("where")) {
                advance();
                each.condition = parse_expression();
            }
            if (at(token_kind::left_brace)) {
                advance();
                while (in_block()) {
                    if (at_word("order")) {
                        advance();
                        // order by created_at descending: said the way it reads.
                        if (at(token_kind::minus)) {
                            // order -created_at, as it was written: both fixed at once.
                            const token& minus = advance();
                            std::size_t begin = peek().begin;
                            parse_postfix();
                            std::string key(source_.substr(begin, tokens_[pos_ - 1].end - begin));
                            fail(minus.where, "write order by " + key + " descending, saying in words what rows are ordered by",
                                 fix{minus.where, tokens_[pos_ - 1].end - minus.begin, "by " + key + " descending"});
                        }
                        if (!at_word("by") || at_line_end()) {
                            const token& key = peek();
                            fail(key.where, "write order by " + key.text + ", saying what rows are ordered by", fix{key.where, 0, "by "});
                        }
                        advance();
                        do {
                            each.order.push_back(parse_order_key());
                        } while (!at_line_end());
                        end_line();
                        continue;
                    }
                    if (at_word("limit") && peek(1).kind == token_kind::number) {
                        advance();
                        const token& n = advance();
                        int limit = 0;
                        auto [end, failed] = std::from_chars(n.text.data(), n.text.data() + n.text.size(), limit);
                        if (failed != std::errc{} || end != n.text.data() + n.text.size() || limit < 1) {
                            fail(n.where, "limit is a whole number of rows, like limit 50");
                        }
                        each.limit = limit;
                        end_line();
                        continue;
                    }
                    parse_view_row_line(each.rows);
                }
                expect(token_kind::right_brace, "'}'");
            }
            end_line();
            return each;
        }

        // A line of a view's rows: several values shown as they are (book.title
        // due_at), or one name given a computed value (lent_to = first(...)).
        void parse_view_row_line(std::vector<view_value>& rows) {
            while (!at_line_end()) {
                view_value row;
                row.where = peek().where;
                row.value = parse_postfix();
                if (at(token_kind::assign)) {
                    auto* name = std::get_if<name_expression>(&row.value->node);
                    if (!name || name->name.parts.size() != 1) {
                        fail(row.where, "only a plain name can be given a value here");
                    }
                    row.name = name->name.parts.front();
                    advance();
                    row.value = parse_expression();
                    rows.push_back(std::move(row));
                    break;
                }
                rows.push_back(std::move(row));
            }
            end_line();
        }

        role_declaration parse_role() {
            advance();
            role_declaration role;
            role.name = expect(token_kind::identifier, "the role's name").text;
            if (at_word("per")) {
                role.per_where = advance().where;
                role.per = expect(token_kind::identifier, "what the role is held within, like project").text;
                if (!at_word("from")) fail_expecting("'from' and the entity that grants the role, like from member");
                advance();
                role.from = expect(token_kind::identifier, "the entity that grants the role, like member").text;
            }
            while (at(token_kind::identifier)) {
                role.permissions.push_back(parse_qualified_name("a permission, like book::view"));
            }
            // A role with many permissions lists them in a block, as many to a line as
            // reads well: role maintainer per project from member { issue::create ... }.
            if (at(token_kind::left_brace)) {
                advance();
                while (in_block()) {
                    if (!at(token_kind::identifier)) fail_expecting("a permission, like issue::create");
                    while (at(token_kind::identifier)) {
                        role.permissions.push_back(parse_qualified_name("a permission, like issue::create"));
                    }
                    end_line();
                }
                expect(token_kind::right_brace, "'}'");
            }
            end_line();
            return role;
        }

        roles_declaration parse_roles() {
            advance();
            roles_declaration roles;
            roles.entity_where = peek().where;
            roles.entity = expect(token_kind::identifier, "the entity a project's roles are records of, like role").text;
            if (!at_word("per")) fail_expecting("'per' and what each set of roles belongs to, like per project");
            roles.per_where = advance().where;
            roles.per = expect(token_kind::identifier, "what each set of roles belongs to, like project").text;
            if (!at_word("from")) fail_expecting("'from' and the entity that gives a person a role, like from member");
            advance();
            roles.from = expect(token_kind::identifier, "the entity that gives a person a role, like member").text;
            // The roles each project starts with, one to a line, with a block for one
            // that allows a lot: maintainer "Maintainer" { issue::create ... }.
            if (at(token_kind::left_brace)) {
                advance();
                while (in_block()) {
                    default_role role;
                    role.where = peek().where;
                    role.name = expect(token_kind::identifier, "a role each project starts with, like maintainer").text;
                    role.title = expect(token_kind::string, "how it's shown, like \"Maintainer\"").text;
                    while (at(token_kind::identifier)) role.permissions.push_back(parse_qualified_name("a command it allows, like issue::create"));
                    if (at(token_kind::left_brace)) {
                        advance();
                        while (in_block()) {
                            while (at(token_kind::identifier)) role.permissions.push_back(parse_qualified_name("a command it allows, like issue::create"));
                            end_line();
                        }
                        expect(token_kind::right_brace, "'}'");
                    }
                    end_line();
                    roles.defaults.push_back(std::move(role));
                }
                expect(token_kind::right_brace, "'}'");
            }
            end_line();
            return roles;
        }

        function_declaration parse_function() {
            advance();
            function_declaration function;
            function.name = expect(token_kind::identifier, "the function's name").text;
            expect(token_kind::left_paren, "'('");
            if (!at(token_kind::right_paren)) {
                function.parameters.push_back(expect(token_kind::identifier, "a parameter's name").text);
                while (at(token_kind::comma)) {
                    advance();
                    function.parameters.push_back(expect(token_kind::identifier, "a parameter's name").text);
                }
            }
            expect(token_kind::right_paren, "')'");
            function.body = parse_statement_block();
            end_line();
            return function;
        }

        screen_declaration parse_screen() {
            advance();
            screen_declaration screen;
            if (at(token_kind::string)) {
                screen.title = advance().text;
            } else if (at(token_kind::identifier)) {
                screen.title = advance().text;
                screen.title_is_name = true;
            } else {
                fail_expecting("the screen's title, like \"Shelf\"");
            }
            screen.route = expect(token_kind::route, "the screen's route, like /shelf").text;
            if (at_word("under")) {
                advance();
                screen.under_where = peek().where;
                screen.under = expect(token_kind::route, "the page above it, like /:project/boards/:board").text;
                screen.under_title = expect(token_kind::string, "what that page is called from here, like \"{issue_page.board_title}\"").text;
            }
            if (at_word("layout")) {
                advance();
                screen.layout_where = peek().where;
                screen.layout = expect(token_kind::identifier, "a layout, like single or two_columns").text;
            }
            screen.items = parse_screen_block();
            end_line();
            return screen;
        }

        std::vector<screen_item> parse_screen_block() {
            expect(token_kind::left_brace, "'{'");
            std::vector<screen_item> items;
            while (in_block()) {
                items.push_back(parse_screen_item());
            }
            expect(token_kind::right_brace, "'}'");
            return items;
        }

        screen_item parse_screen_item() {
            location where = peek().where;
            bool string_follows = peek(1).kind == token_kind::string;

            // menu { link /settings "General" ... }: links down the side of the screen.
            // main { ... }: one region of the screen's layout.
            if ((at_word("main") || at_word("side")) && peek(1).kind == token_kind::left_brace) {
                content_block region;
                region.type = content_block::kind::region;
                region.title = advance().text;
                region.items = parse_screen_block();
                end_line();
                return {where, std::move(region)};
            }
            // heading { project::update "Edit project" }: beside the screen's title.
            if (at_word("heading") && peek(1).kind == token_kind::left_brace) {
                advance();
                content_block block;
                block.type = content_block::kind::heading;
                block.items = parse_screen_block();
                end_line();
                return {where, std::move(block)};
            }
            if (at_word("menu") && peek(1).kind == token_kind::left_brace) {
                advance();
                content_block block;
                block.type = content_block::kind::menu;
                block.items = parse_screen_block();
                end_line();
                return {where, std::move(block)};
            }
            if (at_word("hero") || at_word("section")) {
                content_block block;
                block.type = peek().text == "hero" ? content_block::kind::hero : content_block::kind::section;
                advance();
                block.title = expect(token_kind::string, "a title").text;
                if (at(token_kind::anchor)) block.anchor = advance().text;
                if (at_word("color")) {
                    advance();
                    block.hue_where = peek().where;
                    block.hue = expect(token_kind::identifier, "one of the library's hues, like color violet").text;
                }
                block.items = parse_screen_block();
                end_line();
                return {where, std::move(block)};
            }
            // subtitle "{project_page.summary}": under the title, as running words.
            if (string_follows && at_word("subtitle")) {
                advance();
                content_text text;
                text.type = content_text::kind::markdown;
                text.subtitle = true;
                text.value = advance().text;
                end_line();
                return {where, std::move(text)};
            }
            if (string_follows && (at_word("text") || at_word("code") || at_word("markdown"))) {
                content_text text;
                const std::string& word = advance().text;
                text.type = word == "text"   ? content_text::kind::text
                          : word == "code"   ? content_text::kind::code
                                             : content_text::kind::markdown;
                text.value = advance().text;
                if (text.type == content_text::kind::text && at_word("when")) {
                    advance();
                    text.when = parse_when();
                }
                end_line();
                return {where, std::move(text)};
            }
            if (at_word("link") && peek(1).kind != token_kind::scope) {
                advance();
                content_link link;
                if (at(token_kind::route)) {
                    link.target = advance().text;
                } else if (at(token_kind::anchor)) {
                    link.target = "#" + advance().text;
                } else if (at_word("namespace")) {
                    advance();
                    link.namespace_name = parse_qualified_name("the namespace the link opens, like namespace projects");
                } else if (at(token_kind::string)) {
                    link.target = advance().text;  // another site: link "https://uione.io/studio" "Open the studio"
                } else {
                    fail_expecting("where the link goes, like /docs, #waitlist, namespace projects or \"https://example.com\"");
                }
                link.label = expect(token_kind::string, "the link's text").text;
                if (at_word("icon")) {
                    advance();
                    link.icon_where = peek().where;
                    link.icon = expect(token_kind::identifier, "the icon it's drawn as, like workflow").text;
                }
                end_line();
                return {where, std::move(link)};
            }
            if (at_word("component") && peek(1).kind == token_kind::identifier) {
                advance();
                component_item component{advance().text};
                end_line();
                return {where, std::move(component)};
            }
            if (at_word("table") && peek(1).kind != token_kind::scope) {
                advance();
                table_item table;
                table.view = parse_qualified_name("the view to list");
                if (at(token_kind::dot)) {
                    advance();
                    table.list = expect(token_kind::identifier, "which of the view's lists, like comments").text;
                }
                auto by_over = [&]() {
                    if (!at_word("over")) return;
                    advance();
                    table.by_over = parse_qualified_name("the view whose list its tabs are, like project_page");
                    expect(token_kind::dot, "'.' and the view's list, like project_page.phases");
                    table.by_over_list = expect(token_kind::identifier, "the view's list, like phases").text;
                };
                if (at_word("by")) {
                    advance();
                    table.by_where = peek().where;
                    table.by = expect(token_kind::identifier, "the choice its rows are sorted into tabs by, like status").text;
                    by_over();
                }
                if (at_word("link")) {
                    advance();
                    table.link_where = peek().where;
                    table.link = expect(token_kind::route, "the screen each row opens, like /books/:book").text;
                }
                if (at(token_kind::left_brace)) {
                    advance();
                    while (in_block()) {
                        // A table's own settings, a word starting a line: by, search, sort, page.
                        if (at_word("by") && peek(1).kind == token_kind::identifier) {
                            advance();
                            table.by_where = peek().where;
                            table.by = advance().text;
                            by_over();
                            end_line();
                            continue;
                        }
                        // hide when empty: not shown while it has no rows, like a person's reports.
                        if (at_word("hide") && peek(1).kind == token_kind::identifier && peek(1).text == "when") {
                            advance();
                            advance();
                            if (!at_word("empty")) fail_expecting("empty, as in hide when empty");
                            advance();
                            table.hide_empty = true;
                            end_line();
                            continue;
                        }
                        if (at_word("reorder") && peek(1).kind == token_kind::identifier) {
                            advance();
                            table.reorder_where = peek().where;
                            table.reorder = advance().text;
                            end_line();
                            continue;
                        }
                        if (at_word("search") && peek(1).kind == token_kind::identifier) {
                            advance();
                            table.search_where = peek().where;
                            while (at(token_kind::identifier)) {
                                std::string name = advance().text;
                                while (at(token_kind::dot)) {
                                    advance();
                                    name += "." + expect(token_kind::identifier, "a field").text;
                                }
                                table.search.push_back(name);
                            }
                            end_line();
                            continue;
                        }
                        if (at_word("sort") && (peek(1).kind == token_kind::identifier || peek(1).kind == token_kind::minus)) {
                            advance();
                            table.sort_where = peek().where;
                            if (at(token_kind::minus)) {
                                const token& minus = advance();
                                const token& field = expect(token_kind::identifier, "the field rows are sorted by");
                                fail(minus.where, "write by " + field.text + " descending; a - in front isn't said in words",
                                     fix{minus.where, field.end - minus.begin, "by " + field.text + " descending"});
                            }
                            // sort by number: said the way it reads.
                            if (!at_word("by") || peek(1).kind != token_kind::identifier) {
                                const token& field = peek();
                                fail(field.where, "write sort by " + field.text + ", saying what rows are sorted by", fix{field.where, 0, "by "});
                            }
                            advance();
                            table.sort = expect(token_kind::identifier, "the field rows are sorted by").text;
                            if (at_word("ascending")) {
                                advance();
                            } else if (at_word("descending")) {
                                advance();
                                table.sort_descending = true;
                            }
                            end_line();
                            continue;
                        }
                        // tint by priority
                        if (at_word("tint") && peek(1).kind == token_kind::identifier && peek(1).text == "by") {
                            advance();
                            advance();
                            table.tint_where = peek().where;
                            table.tint = expect(token_kind::identifier, "the field whose choice colors it, like priority").text;
                            end_line();
                            continue;
                        }
                        // only due <= 2 weeks from now
                        if (at_word("only")) {
                            advance();
                            table.only_where = peek().where;
                            relative_times_ = true;
                            table.only = parse_expression();
                            relative_times_ = false;
                            end_line();
                            continue;
                        }
                        if (at_word("page") && peek(1).kind == token_kind::number) {
                            advance();
                            table.page_where = peek().where;
                            table.page = std::stoi(advance().text);
                            end_line();
                            continue;
                        }
                        while (!at_line_end()) {
                            table_column column;
                            column.where = peek().where;
                            column.value = parse_postfix();
                            if (at(token_kind::string)) column.label = advance().text;
                            if (at_word("icon")) {
                                advance();
                                column.icon_where = peek().where;
                                column.icon = expect(token_kind::identifier, "the icon it's drawn as, like add").text;
                            }
                            if (at_word("when")) {
                                advance();
                                std::size_t begin = peek().begin;
                                column.when = parse_when();
                                column.when_written = std::string(source_.substr(begin, tokens_[pos_ - 1].end - begin));
                                // What follows is the condition's, so it ends the line.
                                if (!at_line_end()) fail_expecting("the end of the line after a row's when");
                            }
                            table.columns.push_back(std::move(column));
                        }
                        end_line();
                    }
                    expect(token_kind::right_brace, "'}'");
                }
                end_line();
                return {where, std::move(table)};
            }
            if (at_word("board") && peek(1).kind != token_kind::scope) {
                advance();
                board_item board;
                board.view = parse_qualified_name("the view whose list it shows, like project_page");
                expect(token_kind::dot, "'.' and the view's list, like project_page.issues");
                board.list = expect(token_kind::identifier, "the view's list, like issues").text;
                if (!at_word("by")) fail_expecting("by and the field naming each card's column, like by phase");
                advance();
                board.by_where = peek().where;
                board.by = expect(token_kind::identifier, "the field naming each card's column, like phase").text;
                if (!at_word("over")) fail_expecting("over and the list of its columns, like over project_page.phases");
                advance();
                board.over = parse_qualified_name("the view whose list its columns are, like project_page");
                expect(token_kind::dot, "'.' and the view's list, like project_page.phases");
                board.over_list = expect(token_kind::identifier, "the view's list, like phases").text;
                if (at_word("link")) {
                    advance();
                    board.link_where = peek().where;
                    board.link = expect(token_kind::route, "the screen each card opens, like /:project/issues/:issue").text;
                }
                if (at(token_kind::left_brace)) {
                    advance();
                    while (in_block()) {
                        // move issue::move along project_page.steps: how a card is moved.
                        if (at_word("move") && peek(1).kind == token_kind::identifier && peek(2).kind == token_kind::scope) {
                            advance();
                            button_item move{parse_qualified_name("the command that moves a card, like issue::move"), std::nullopt, nullptr, std::nullopt, "", std::nullopt, {}};
                            if (!at_word("along")) fail_expecting("along and the view's list of steps, like along project_page.steps");
                            advance();
                            move.along = parse_qualified_name("the view whose list of steps it goes along, like project_page");
                            expect(token_kind::dot, "'.' and the view's list of steps, like project_page.steps");
                            move.along_list = expect(token_kind::identifier, "the view's list of steps, like steps").text;
                            board.move = std::move(move);
                            end_line();
                            continue;
                        }
                        // tint by priority
                        if (at_word("tint") && peek(1).kind == token_kind::identifier && peek(1).text == "by") {
                            advance();
                            advance();
                            board.tint_where = peek().where;
                            board.tint = expect(token_kind::identifier, "the field whose choice colors it, like priority").text;
                            end_line();
                            continue;
                        }
                        if (at_word("search") && peek(1).kind == token_kind::identifier) {
                            advance();
                            while (at(token_kind::identifier)) {
                                std::string name = advance().text;
                                while (at(token_kind::dot)) {
                                    advance();
                                    name += "." + expect(token_kind::identifier, "a field").text;
                                }
                                board.search.push_back(name);
                            }
                            end_line();
                            continue;
                        }
                        while (!at_line_end()) {
                            table_column column;
                            column.where = peek().where;
                            column.value = parse_postfix();
                            if (at(token_kind::string)) column.label = advance().text;
                            if (at_word("icon")) {
                                advance();
                                column.icon_where = peek().where;
                                column.icon = expect(token_kind::identifier, "the icon it's drawn as, like add").text;
                            }
                            board.columns.push_back(std::move(column));
                        }
                        end_line();
                    }
                    expect(token_kind::right_brace, "'}'");
                }
                end_line();
                return {where, std::move(board)};
            }
            if (at_word("cards") && peek(1).kind != token_kind::scope) {
                advance();
                cards_item cards;
                cards.view = parse_qualified_name("the view whose list it shows, like project_page");
                expect(token_kind::dot, "'.' and the view's list, like project_page.boards");
                cards.list = expect(token_kind::identifier, "the view's list, like boards").text;
                if (at_word("link")) {
                    advance();
                    cards.link_where = peek().where;
                    cards.link = expect(token_kind::route, "the screen each card opens, like /:project/boards/:board").text;
                }
                if (at(token_kind::left_brace)) {
                    advance();
                    while (in_block()) {
                        // tally project_page.issues by board and phase
                        if (at_word("tally") && peek(1).kind == token_kind::identifier) {
                            advance();
                            cards.tally_where = peek().where;
                            cards.tally = parse_qualified_name("the view whose list it counts, like project_page");
                            expect(token_kind::dot, "'.' and the view's list, like project_page.issues");
                            cards.tally_list = expect(token_kind::identifier, "the view's list, like issues").text;
                            if (!at_word("by")) fail_expecting("by and the field naming each one's card, like by board");
                            advance();
                            cards.tally_by = expect(token_kind::identifier, "the field naming each one's card, like board").text;
                            if (!at_word("and")) fail_expecting("and the field they're counted by, like and phase");
                            advance();
                            cards.tally_and = expect(token_kind::identifier, "the field they're counted by, like phase").text;
                            end_line();
                            continue;
                        }
                        // filter "Opened by me" author == me
                        if (at_word("filter") && peek(1).kind == token_kind::string) {
                            advance();
                            cards_filter f;
                            f.where = peek().where;
                            f.label = advance().text;
                            relative_times_ = true;
                            f.condition = parse_expression();
                            relative_times_ = false;
                            if (at_word("color")) {
                                advance();
                                f.hue_where = peek().where;
                                f.hue = expect(token_kind::identifier, "one of the library's hues, like color red").text;
                            }
                            cards.filters.push_back(std::move(f));
                            end_line();
                            continue;
                        }
                        while (!at_line_end()) {
                            table_column column;
                            column.where = peek().where;
                            column.value = parse_postfix();
                            if (at(token_kind::string)) column.label = advance().text;
                            cards.columns.push_back(std::move(column));
                        }
                        end_line();
                    }
                    expect(token_kind::right_brace, "'}'");
                }
                end_line();
                return {where, std::move(cards)};
            }
            if ((at_word("grid") || at_word("diagram")) && peek(1).kind != token_kind::scope) {
                grid_item grid;
                grid.drawn = advance().text == "diagram";
                grid.view = parse_qualified_name("the view whose list it shows, like project_page");
                expect(token_kind::dot, "'.' and the view's list, like project_page.steps");
                grid.list = expect(token_kind::identifier, "the view's list, like steps").text;
                if (!at_word("by")) fail_expecting("by and the fields naming each one's row and column, like by from and to");
                advance();
                grid.from_where = peek().where;
                grid.from = expect(token_kind::identifier, "the field naming each one's row, like from").text;
                if (!at_word("and")) fail_expecting("and the field naming each one's column, like and to");
                advance();
                grid.to_where = peek().where;
                grid.to = expect(token_kind::identifier, "the field naming each one's column, like to").text;
                if (!at_word("over")) fail_expecting("over and the list of what its rows and columns are, like over project_page.phases");
                advance();
                grid.over = parse_qualified_name("the view whose list its rows and columns are, like project_page");
                expect(token_kind::dot, "'.' and the view's list, like project_page.phases");
                grid.over_list = expect(token_kind::identifier, "the view's list, like phases").text;
                if (at(token_kind::left_brace)) {
                    advance();
                    while (in_block()) {
                        if (grid.cell) fail_expecting("'}'; a grid's cell shows one thing, like roles.title");
                        grid.cell = parse_postfix();
                        end_line();
                    }
                    expect(token_kind::right_brace, "'}'");
                }
                end_line();
                return {where, std::move(grid)};
            }
            if (at_word("details") && peek(1).kind != token_kind::scope) {
                advance();
                details_item details;
                details.view = parse_qualified_name("the view whose values it shows");
                expect(token_kind::left_brace, "'{'");
                while (in_block()) {
                    // tint by priority
                    if (at_word("tint") && peek(1).kind == token_kind::identifier && peek(1).text == "by") {
                        advance();
                        advance();
                        details.tint_where = peek().where;
                        details.tint = expect(token_kind::identifier, "the field whose choice colors it, like priority").text;
                        end_line();
                        continue;
                    }

                    while (!at_line_end()) {
                        table_column field;
                        field.where = peek().where;
                        field.value = parse_postfix();
                        if (at(token_kind::string)) field.label = advance().text;
                        details.fields.push_back(std::move(field));
                    }
                    end_line();
                }
                expect(token_kind::right_brace, "'}'");
                end_line();
                return {where, std::move(details)};
            }
            // find "Search this project" { project_page.issues "Issues" link /:project/:issue by number title }
            if (at_word("find") && peek(1).kind == token_kind::string) {
                advance();
                find_item find;
                find.label = advance().text;
                expect(token_kind::left_brace, "'{' and the lists it finds in, one a line");
                while (in_block()) {
                    find_source source;
                    source.where = peek().where;
                    source.view = parse_qualified_name("the view whose list it finds in, like project_page");
                    expect(token_kind::dot, "'.' and the view's list, like project_page.issues");
                    source.list = expect(token_kind::identifier, "the view's list, like issues").text;
                    source.label = expect(token_kind::string, "what its results are called, like \"Issues\"").text;
                    if (at_word("link")) {
                        advance();
                        source.link_where = peek().where;
                        source.link = expect(token_kind::route, "the screen each result opens, like /:project/:issue").text;
                    }
                    if (!at_word("by")) fail_expecting("by and the fields it's found by, like by number title");
                    advance();
                    while (at(token_kind::identifier)) source.by.push_back(advance().text);
                    if (source.by.empty()) fail_expecting("the fields it's found by, like by number title");
                    find.sources.push_back(std::move(source));
                    end_line();
                }
                expect(token_kind::right_brace, "'}'");
                end_line();
                return {where, std::move(find)};
            }
            if (at_word("copy") && peek(1).kind != token_kind::scope) {
                advance();
                copy_item copy{parse_qualified_name("the view to copy"), std::nullopt};
                if (at(token_kind::string)) copy.label = advance().text;
                end_line();
                return {where, std::move(copy)};
            }
            if ((at_word("thread") || at_word("timeline")) && peek(1).kind != token_kind::scope) {
                bool thread = advance().text == "thread";
                qualified_name view = parse_qualified_name("the view whose list it shows");
                expect(token_kind::dot, thread ? "'.' and the view's list, like issue_page.comments" : "'.' and the view's list, like issue_page.history");
                std::string list = expect(token_kind::identifier, "which of the view's lists").text;
                if (thread) {
                    end_line();
                    return {where, thread_item{std::move(view), std::move(list)}};
                }
                timeline_item timeline{std::move(view), std::move(list), std::nullopt, {}, std::nullopt, std::nullopt, {}, {}, std::nullopt, {}};
                if (at(token_kind::string)) timeline.title = advance().text;
                if (at_word("link")) {
                    advance();
                    timeline.link_where = peek().where;
                    timeline.link = expect(token_kind::route, "the screen each change opens, like /:project/issues/:issue").text;
                }
                if (at(token_kind::left_brace)) {
                    advance();
                    while (in_block()) {
                        // new since news.seen
                        if (at_word("new")) {
                            advance();
                            if (!at_word("since")) fail_expecting("since and when the person last looked, like new since news.seen");
                            advance();
                            timeline.since_where = peek().where;
                            timeline.since = parse_qualified_name("the view holding when the person last looked, like news");
                            expect(token_kind::dot, "'.' and its value, like news.seen");
                            timeline.since_field = expect(token_kind::identifier, "the view's value, like seen").text;
                        } else if (at_word("seen")) {
                            // seen reader::create
                            advance();
                            timeline.seen_where = peek().where;
                            timeline.seen = parse_qualified_name("the command that says they've looked, like reader::create");
                        } else {
                            fail_expecting("new since or seen");
                        }
                        end_line();
                    }
                    expect(token_kind::right_brace, "'}'");
                }
                end_line();
                return {where, std::move(timeline)};
            }
            if (at_word("form") && peek(1).kind != token_kind::scope) {
                advance();
                form_item form;
                do {
                    form.commands.push_back(parse_qualified_name("a command, like book::create"));
                } while (at(token_kind::identifier));
                if (at(token_kind::string)) form.submit = advance().text;
                expect(token_kind::left_brace, "'{'");
                while (in_block()) {
                    parse_form_line(form.fields);
                }
                expect(token_kind::right_brace, "'}'");
                end_line();
                return {where, std::move(form)};
            }
            if (at_word("confirm") && peek(1).kind != token_kind::scope) {
                advance();
                confirm_item confirm;
                confirm.command = parse_qualified_name("the command to confirm");
                confirm.message = expect(token_kind::string, "the question to ask").text;
                end_line();
                return {where, std::move(confirm)};
            }
            if (at(token_kind::identifier)) {
                qualified_name name = parse_qualified_name("a screen element");
                if (name.parts.size() < 2) {
                    fail(where, "'" + name.text() + "' isn't a screen element; a button names its "
                                "command in full, like book::create");
                }
                button_item button{std::move(name), std::nullopt, nullptr, std::nullopt, "", std::nullopt, {}};
                if (at_word("along")) {
                    advance();
                    button.along = parse_qualified_name("the view whose list of steps it goes along, like project_page");
                    expect(token_kind::dot, "'.' and the view's list of steps, like project_page.steps");
                    button.along_list = expect(token_kind::identifier, "the view's list of steps, like steps").text;
                    end_line();
                    return {where, std::move(button)};
                }
                if (at(token_kind::string)) button.label = expect(token_kind::string, "what the button says").text;
                if (at_word("icon")) {
                    advance();
                    button.icon_where = peek().where;
                    button.icon = expect(token_kind::identifier, "the icon it's drawn as, like edit").text;
                }
                if (at_word("when")) {
                    advance();
                    button.when = parse_when();
                }
                end_line();
                return {where, std::move(button)};
            }
            fail_expecting("a screen element");
        }

        // A line of a form: several fields (title author), or one field with a starting
        // value, which can carry a hint: code = suggest(start) { hint "..." }.
        void parse_form_line(std::vector<form_field>& fields) {
            while (!at_line_end()) {
                form_field f;
                const token& name = expect(token_kind::identifier, "a field to ask for");
                f.name = name.text;
                f.where = name.where;
                if (at(token_kind::string)) f.label = advance().text;
                if (at(token_kind::assign)) {
                    advance();
                    f.value = parse_expression();
                    if (at(token_kind::left_brace)) {
                        advance();
                        skip_newlines();
                        if (!at_word("hint")) fail_expecting("'hint'");
                        advance();
                        f.hint = expect(token_kind::string, "the hint's text").text;
                        end_line();
                        skip_newlines();
                        expect(token_kind::right_brace, "'}'");
                    }
                    fields.push_back(std::move(f));
                    break;
                }
                fields.push_back(std::move(f));
            }
            end_line();
        }

        enum_declaration parse_enum() {
            advance();
            enum_declaration e;
            e.name = expect(token_kind::identifier, "the enum's name, like status").text;
            expect(token_kind::left_brace, "'{' and its choices, each with how it's shown, like open \"Open\"");
            while (in_block()) {
                while (!at_line_end()) {
                    const token& choice = expect(token_kind::identifier, "a choice, like open");
                    e.choices.push_back(choice.text);
                    e.choice_where.push_back(choice.where);
                    e.choice_labels.push_back(at(token_kind::string) ? advance().text : "");
                }
                end_line();
            }
            expect(token_kind::right_brace, "'}'");
            end_line();
            return e;
        }

        picker_declaration parse_picker() {
            advance();
            picker_declaration picker;
            picker.entity = expect(token_kind::identifier, "the entity to pick").text;
            if (!at_word("from")) fail_expecting("'from'");
            advance();
            picker.view = expect(token_kind::identifier, "the view to pick from").text;
            end_line();
            return picker;
        }

        webhook_declaration parse_webhook() {
            advance();
            webhook_declaration hook;
            hook.provider_where = peek().where;
            hook.provider = expect(token_kind::identifier, "who sends it, like github").text;
            hook.route = expect(token_kind::route, "where it's received, like /hooks/github").text;
            expect(token_kind::left_brace, "'{'");
            while (in_block()) {
                if (at_word("for")) {
                    advance();
                    hook.scope_where = peek().where;
                    hook.scope = expect(token_kind::identifier, "what a repository belongs to, like project").text;
                    if (!at_word("by")) fail_expecting("'by' and the field naming the repository, like by repository");
                    advance();
                    hook.repository = expect(token_kind::identifier, "the field naming the repository").text;
                    end_line();
                } else if (at_word("on")) {
                    advance();
                    webhook_handler handler;
                    handler.where = peek().where;
                    handler.event = expect(token_kind::identifier, "an event, commit or pull_request").text;
                    handler.body = parse_statement_block();
                    end_line();
                    hook.handlers.push_back(std::move(handler));
                } else {
                    fail_expecting("'for' or 'on'");
                }
            }
            expect(token_kind::right_brace, "'}'");
            end_line();
            return hook;
        }

        // statements

        std::vector<statement> parse_statement_block() {
            expect(token_kind::left_brace, "'{'");
            std::vector<statement> body;
            while (in_block()) {
                body.push_back(parse_statement());
            }
            expect(token_kind::right_brace, "'}'");
            return body;
        }

        statement parse_statement() {
            location where = peek().where;
            if (at_word("require")) {
                advance();
                require_statement s;
                s.condition = parse_expression();
                s.message = expect(token_kind::string, "the message shown when it isn't met").text;
                end_line();
                return {where, std::move(s)};
            }
            if (at_word("permission")) {
                advance();
                permission_statement s{parse_qualified_name("who may run it: anyone, signed_in, owner or a permission")};
                end_line();
                return {where, std::move(s)};
            }
            if (at_word("clear")) {
                advance();
                clear_statement s;
                do {
                    s.fields.push_back(expect(token_kind::identifier, "a field to clear").text);
                } while (at(token_kind::identifier));
                end_line();
                return {where, std::move(s)};
            }
            if (at_word("changes") && peek(1).kind == token_kind::identifier) {
                advance();
                changes_statement s;
                do {
                    s.fields.push_back(expect(token_kind::identifier, "a field it changes").text);
                } while (at(token_kind::identifier));
                end_line();
                return {where, std::move(s)};
            }
            if (at_word("input") && peek(1).kind == token_kind::identifier && peek(2).kind == token_kind::identifier) {
                advance();
                input_statement s;
                s.name_where = peek().where;
                s.name = advance().text;
                s.type = parse_qualified_name("what it is, like phase or text");
                end_line();
                return {where, std::move(s)};
            }
            if (at_word("each") && peek(1).kind == token_kind::identifier) {
                advance();
                each_statement s;
                s.entity_where = peek().where;
                s.entity = advance().text;
                if (!at_word("where")) fail_expecting("where and what it picks, like each issue where phase == id { ... }");
                advance();
                s.where = parse_expression();
                s.body = parse_statement_block();
                end_line();
                return {where, std::move(s)};
            }
            if (at_word("delete") && peek(1).kind == token_kind::identifier && peek(1).text == "each") {
                advance();
                advance();
                delete_statement s;
                s.entity_where = peek().where;
                s.entity = expect(token_kind::identifier, "what it deletes, like step").text;
                if (!at_word("where")) fail_expecting("where and what it picks, like delete each step where from == id");
                advance();
                s.where = parse_expression();
                end_line();
                return {where, std::move(s)};
            }
            if (at_word("return")) {
                advance();
                return_statement s{parse_expression()};
                end_line();
                return {where, std::move(s)};
            }
            if (at_word("if")) {
                statement s{where, parse_if()};
                end_line();
                return s;
            }
            if ((at_word("add") || at_word("remove")) && peek(1).kind != token_kind::assign && peek(1).kind != token_kind::dot) {
                list_statement s;
                s.adds = advance().text == "add";
                s.value = parse_postfix();
                const char* joiner = s.adds ? "to" : "from";
                if (!at_word(joiner)) fail_expecting(std::string("'") + joiner + "' and the list, like " + (s.adds ? "add me to assignees" : "remove me from assignees"));
                advance();
                s.list_where = peek().where;
                s.list = expect(token_kind::identifier, "the list, like assignees").text;
                end_line();
                return {where, std::move(s)};
            }
            bool runs = at(token_kind::identifier) && peek(1).kind == token_kind::scope && peek(2).kind == token_kind::identifier &&
                        peek(2).text == "create" && peek(3).kind == token_kind::left_brace;
            if (runs || (at_word("create") && peek(1).kind == token_kind::identifier && peek(2).kind == token_kind::left_brace)) {
                create_statement s;
                if (runs) {
                    s.entity_where = peek().where;
                    s.command = parse_qualified_name("the command, like board::create");
                    s.entity = s.command->parts.front();
                } else {
                    advance();
                    s.entity_where = peek().where;
                    s.entity = advance().text;
                }
                advance();
                while (in_block()) {
                    while (!at_line_end()) {
                        field_value value;
                        value.where = peek().where;
                        value.name = expect(token_kind::identifier, "a field of what's made, like role").text;
                        expect(token_kind::assign, "'='");
                        value.value = parse_postfix();
                        s.values.push_back(std::move(value));
                    }
                    end_line();
                }
                expect(token_kind::right_brace, "'}'");
                end_line();
                return {where, std::move(s)};
            }
            assign_statement s;
            s.target = parse_postfix();
            expect(token_kind::assign, "'='");
            s.value = parse_expression();
            end_line();
            return {where, std::move(s)};
        }

        if_statement parse_if() {
            advance();
            if_statement s;
            s.condition = parse_expression();
            s.then_body = parse_statement_block();
            if (at_word("else")) {
                advance();
                if (at_word("if")) {
                    location where = peek().where;
                    s.else_body.push_back({where, parse_if()});
                } else {
                    s.else_body = parse_statement_block();
                }
            }
            return s;
        }

        // expressions, loosest first: || then && then == != then < > <= >= then + -
        // then *, then ! and unary -, then calls and members, then plain values.

        static int precedence(token_kind kind) {
            switch (kind) {
                case token_kind::logical_or:    return 1;
                case token_kind::logical_and:   return 2;
                case token_kind::equal:
                case token_kind::not_equal:     return 3;
                case token_kind::less:
                case token_kind::greater:
                case token_kind::less_equal:
                case token_kind::greater_equal: return 4;
                case token_kind::plus:
                case token_kind::minus:         return 5;
                case token_kind::star:          return 6;
                default:                        return 0;
            }
        }

        expression_ptr parse_expression() { return parse_binary(0); }

        // What follows a when: a condition, or a block of them, one a line, that all
        // hold, as if each line were joined to the next with &&:
        //
        //     when {
        //         issue_page.status == status::implemented
        //         issue_page.implemented_by != me
        //     }
        expression_ptr parse_when() {
            if (!at(token_kind::left_brace)) return parse_expression();
            location where = advance().where;
            expression_ptr all;
            while (in_block()) {
                auto line = parse_expression();
                if (!at_line_end()) fail_expecting("the end of the line; a when block has one condition a line");
                if (!all) {
                    all = std::move(line);
                    continue;
                }
                auto both = std::make_unique<expression>();
                both->where = line->where;
                both->node = binary_expression{token_kind::logical_and, std::move(all), std::move(line)};
                all = std::move(both);
            }
            expect(token_kind::right_brace, "'}'");
            if (!all) fail(where, "a when block holds its conditions, one a line");
            return all;
        }

        expression_ptr parse_binary(int loosest) {
            auto left = parse_unary();
            for (;;) {
                // labels has bug compares as tightly as ==.
                bool has = at_word("has");
                int p = has ? precedence(token_kind::equal) : precedence(peek().kind);
                if (p == 0 || p <= loosest) return left;
                const token& op = advance();
                auto e = std::make_unique<expression>();
                e->where = op.where;
                e->node = binary_expression{has ? token_kind::has : op.kind, std::move(left), parse_binary(p)};
                left = std::move(e);
            }
        }

        expression_ptr parse_unary() {
            // was issue.phase: what a field held before the command, kept as a call.
            if (at_word("was") && peek(1).kind == token_kind::identifier) {
                const token& was = advance();
                auto callee = std::make_unique<expression>();
                callee->where = was.where;
                callee->node = name_expression{qualified_name{{"was"}, was.where}};
                call_expression call{std::move(callee), {}};
                call.arguments.push_back(parse_postfix());
                auto e = std::make_unique<expression>();
                e->where = was.where;
                e->node = std::move(call);
                return e;
            }
            if (at(token_kind::logical_not) || at(token_kind::minus)) {
                const token& op = advance();
                auto e = std::make_unique<expression>();
                e->where = op.where;
                e->node = unary_expression{op.kind, parse_unary()};
                return e;
            }
            return parse_postfix();
        }

        expression_ptr parse_postfix() {
            auto e = parse_primary();
            for (;;) {
                if (at(token_kind::left_paren)) {
                    location where = advance().where;
                    call_expression call;
                    call.callee = std::move(e);
                    if (!at(token_kind::right_paren)) {
                        call.arguments.push_back(parse_argument());
                        while (at(token_kind::comma)) {
                            advance();
                            call.arguments.push_back(parse_argument());
                        }
                    }
                    expect(token_kind::right_paren, "')'");
                    e = std::make_unique<expression>();
                    e->where = where;
                    e->node = std::move(call);
                } else if (at(token_kind::dot)) {
                    location where = advance().where;
                    member_expression member;
                    member.object = std::move(e);
                    member.member = expect(token_kind::identifier, "a name after '.'").text;
                    e = std::make_unique<expression>();
                    e->where = where;
                    e->node = std::move(member);
                } else {
                    return e;
                }
            }
        }

        // A call's argument can filter what it's given: count(loan where returned_at == none).
        expression_ptr parse_argument() {
            auto value = parse_expression();
            if (!at_word("where")) return value;
            location where = advance().where;
            auto e = std::make_unique<expression>();
            e->where = where;
            e->node = where_expression{std::move(value), parse_expression()};
            return e;
        }

        // The units a time is counted in, as its offset writes them.
        static const char* unit_of(std::string_view word) {
            if (word == "hour" || word == "hours") return "h";
            if (word == "day" || word == "days") return "d";
            if (word == "week" || word == "weeks") return "w";
            return nullptr;
        }

        expression_ptr parse_primary() {
            auto e = std::make_unique<expression>();
            e->where = peek().where;
            if (at(token_kind::string)) {
                e->node = literal_expression{literal_expression::kind::string, advance().text};
            } else if (at(token_kind::number) && relative_times_ && peek(1).kind == token_kind::identifier && unit_of(peek(1).text)) {
                // 7 days ago, 2 weeks from now: a time counted from when it's read.
                std::string amount = advance().text;
                const token& unit = advance();
                std::string sign;
                if (at_word("ago")) {
                    advance();
                    sign = "-";
                } else if (at_word("from") && peek(1).kind == token_kind::identifier && peek(1).text == "now") {
                    advance();
                    advance();
                    sign = "+";
                } else {
                    fail_expecting("ago or from now, like " + amount + " " + unit.text + " ago");
                }
                e->node = literal_expression{literal_expression::kind::time, sign + amount + unit_of(unit.text)};
            } else if (at(token_kind::number)) {
                e->node = literal_expression{literal_expression::kind::number, advance().text};
            } else if (at(token_kind::identifier)) {
                e->node = name_expression{parse_qualified_name("a name")};
            } else if (at(token_kind::left_paren)) {
                advance();
                e = parse_expression();
                expect(token_kind::right_paren, "')'");
            } else if (at(token_kind::left_bracket)) {
                // A long list goes on over lines, until its ].
                advance();
                skip_newlines();
                list_expression list;
                while (!at(token_kind::right_bracket)) {
                    list.items.push_back(parse_expression());
                    skip_newlines();
                    if (!at(token_kind::comma)) break;
                    advance();
                    skip_newlines();
                }
                expect(token_kind::right_bracket, "']' after the list's values");
                e->node = std::move(list);
            } else {
                fail_expecting("a value");
            }
            return e;
        }
    };

    // Reads one .one file. Anything wrong is added to `out`; the tree holds whatever
    // could be read.
    inline file parse(std::string path, std::string_view source, diagnostics& out) {
        file parsed = parser(std::move(path), source, out).parse();
        // The first line that isn't a comment or blank, moved up over the comment just
        // above it, when nothing parts them.
        std::vector<std::string_view> lines;
        for (std::size_t at = 0; at <= source.size();) {
            std::size_t end = source.find('\n', at);
            if (end == std::string_view::npos) end = source.size();
            lines.push_back(source.substr(at, end - at));
            at = end + 1;
        }
        auto kind = [&](std::size_t i) {
            std::size_t start = lines[i].find_first_not_of(" \t\r");
            if (start == std::string_view::npos) return 'b';
            return lines[i].substr(start).starts_with("//") ? 'c' : 'd';
        };
        std::size_t first = 0;
        while (first < lines.size() && kind(first) != 'd') ++first;
        if (first == lines.size()) return parsed;
        while (first > 0 && kind(first - 1) == 'c') --first;
        parsed.imports_at = location{static_cast<int>(first) + 1, 1};
        return parsed;
    }

} // namespace one::language
