// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "generators/local.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "code/stream.hpp"
#include "generators/native_names.hpp"
#include "language/names.hpp"
#include "language/written.hpp"

namespace one::generators {

    namespace {

        using stream = code::stream;
        using native_detail::identifier;

        std::string str(std::string_view text) { return native_detail::literal(text); }

        // A piece of C++ an expression became, and whether it's a bool rather than a value.
        struct piece {
            std::string code;
            bool boolean = false;
        };

        // What the package is written in: C++ on one::local for the native apps, or
        // TypeScript on @uione/react/local for the web app. Both say the same lines.
        enum class dialect { cpp, typescript };

        struct namespace_parts {
            std::string path;  // the file that declares it first
            std::vector<std::pair<const language::entity_declaration*, std::string>> entities;
            std::vector<std::pair<const language::command_declaration*, std::string>> commands;
            std::vector<std::pair<const language::view_declaration*, std::string>> views;
            std::vector<std::pair<const language::function_declaration*, std::string>> functions;
            std::map<const void*, int> lines;
        };

        class local_generator {
        public:
            local_generator(const std::vector<language::file>& files, dialect written) : files_(files), dialect_(written) {}

            generated_local generate() {
                for (const auto& f : files_) collect(f.path, "", f.declarations);
                generated_local out;
                stream packages;  // every namespace's, for the web app's one file
                stream declared;  // and every namespace's functions, before them
                std::string first_path;
                for (const auto& [ns, parts] : namespaces_) {
                    if (parts.entities.empty() && parts.commands.empty() && parts.views.empty()) continue;
                    if (ns.empty()) {
                        unsupported(parts.path, {}, "entities, commands and views outside a namespace");
                        continue;
                    }
                    ns_ = ns;
                    parts_ = &parts;
                    if (typescript()) {
                        if (first_path.empty()) first_path = parts.path;
                        functions(declared, parts);
                        package_body(packages, parts);
                        continue;
                    }
                    std::string install = "install_" + identifier(ns);
                    out.files.push_back(file("src/local/" + identifier(ns) + ".cpp", namespace_file(parts, install)));
                    out.installs.push_back(install);
                }
                if (typescript() && !first_path.empty()) out.files.push_back(file("src/local.generated.ts", typescript_file(declared, packages, first_path, helpers_)));
                out.errors = std::move(errors_);
                if (!out.errors.empty()) out.files.clear();
                return out;
            }

        private:
            const std::vector<language::file>& files_;
            dialect dialect_;
            std::map<std::string, namespace_parts> namespaces_;
            std::map<std::string, std::vector<std::string>> form_fields_;  // what each command's forms ask for
            language::diagnostics errors_;
            std::string ns_;
            const namespace_parts* parts_ = nullptr;
            std::string path_;  // the file being read, for errors
            std::set<std::string> helpers_;  // the web app's helpers the code written uses, besides those it always imports

            void unsupported(const std::string& path, language::location where, const std::string& what) {
                for (const auto& e : errors_)
                    if (e.path == path && e.where.line == where.line) return;
                errors_.push_back({path, where, "not supported yet in a project that says storage local: " + what});
            }
            void unsupported(language::location where, const std::string& what) { unsupported(path_, where, what); }

            void collect(const std::string& path, const std::string& ns, const std::vector<language::declaration>& declarations) {
                for (const auto& d : declarations) {
                    if (auto* n = std::get_if<language::namespace_declaration>(&d.node)) {
                        collect(path, language::qualified(ns, n->name), n->declarations);
                        continue;
                    }
                    auto& parts = namespaces_[ns];
                    if (parts.path.empty()) parts.path = path;
                    if (auto* e = std::get_if<language::entity_declaration>(&d.node)) {
                        parts.entities.emplace_back(e, path);
                        parts.lines[e] = d.where.line;
                    } else if (auto* c = std::get_if<language::command_declaration>(&d.node)) {
                        parts.commands.emplace_back(c, path);
                        parts.lines[c] = d.where.line;
                    } else if (auto* v = std::get_if<language::view_declaration>(&d.node)) {
                        parts.views.emplace_back(v, path);
                        parts.lines[v] = d.where.line;
                    } else if (auto* fn = std::get_if<language::function_declaration>(&d.node)) {
                        parts.functions.emplace_back(fn, path);
                        parts.lines[fn] = d.where.line;
                    } else if (auto* screen = std::get_if<language::screen_declaration>(&d.node)) {
                        collect_forms(ns, screen->items);
                    }
                }
            }

            // The fields each command's forms ask for, which is what it may be sent.
            void collect_forms(const std::string& ns, const std::vector<language::screen_item>& items) {
                for (const auto& item : items) {
                    if (auto* form = std::get_if<language::form_item>(&item.node)) {
                        for (const auto& command : form->commands) {
                            std::string full = command.parts.size() > 2 ? command.text() : language::qualified(ns, command.text());
                            auto& fields = form_fields_[full];
                            for (const auto& field : form->fields)
                                if (std::ranges::find(fields, field.name) == fields.end()) fields.push_back(field.name);
                        }
                    } else if (auto* block = std::get_if<language::content_block>(&item.node)) {
                        collect_forms(ns, block->items);
                    }
                }
            }

            // the two dialects, where they differ

            bool typescript() const { return dialect_ == dialect::typescript; }
            // A value from code that's a C++ literal or a sum; TypeScript's are values as they are.
            std::string value_of(const std::string& code) const { return typescript() ? code : "value(" + code + ")"; }
            std::string nothing_value() const { return typescript() ? "null" : "value()"; }
            std::string number_of(const std::string& code) const { return typescript() ? "number(" + code + ")" : code + ".as_double()"; }
            std::string text_of(const std::string& code) const { return typescript() ? "text(" + code + ")" : code + ".as_string()"; }
            std::string sum(const std::string& code) const { return typescript() ? "(" + code + ")" : "value(" + code + ")"; }
            std::string bracketed(const std::string& inside) const { return typescript() ? "[" + inside + "]" : "{" + inside + "}"; }
            std::string strings(const std::vector<std::string>& all) const {
                std::string inside;
                for (const auto& s : all) inside += (inside.empty() ? "" : ", ") + str(s);
                return bracketed(inside);
            }
            std::string set_field(const std::string& held, const std::string& name) const {
                return typescript() ? held + "[" + str(name) + "]" : held + ".fields[" + str(name) + "]";
            }
            std::string as_value(const piece& p) const { return p.boolean ? value_of(p.code) : p.code; }
            static std::string as_bool(const piece& p) { return p.boolean ? p.code : "truthy(" + p.code + ")"; }
            // A step a body takes that can refuse, and the body refusing with it.
            std::string or_refuse(const std::string& call, const std::string& name, bool made) const {
                if (typescript()) return "{ const " + name + " = " + call + "; if (" + (made ? "failed(" + name + ")" : name) + ") return " + name + "; }";
                return "if (auto " + name + " = " + call + "; !" + name + ") return " + (made ? "c.fail(" + name + ".error())" : name) + ";";
            }
            std::string for_each_row(const std::string& entity) const {
                return std::string(typescript() ? "for (const row of" : "for (value row :") + " c.each(all(" + str(entity) + "))) {";
            }

            // names

            std::string full(const std::string& name) const { return name.find("::") != std::string::npos ? name : language::qualified(ns_, name); }

            const language::entity_declaration* entity(const std::string& name) const {
                std::string wanted = full(name);
                for (const auto& [ns, parts] : namespaces_)
                    for (const auto& [e, _] : parts.entities)
                        if (language::qualified(ns, e->name) == wanted) return e;
                return nullptr;
            }

            static const language::field* field(const language::entity_declaration* e, const std::string& name) {
                if (!e) return nullptr;
                for (const auto& f : e->fields)
                    if (f.name == name) return &f;
                return nullptr;
            }

            // The entity a field points at, in full, or empty.
            std::string refers(const language::field& f) const {
                if (!f.type) return "";
                std::string type = f.type->text();
                return entity(type) ? full(type) : "";
            }

            // Whether a name is one of the choices any entity's fields offer.
            bool is_choice(const std::string& name) const {
                for (const auto& [ns, parts] : namespaces_)
                    for (const auto& [e, _] : parts.entities)
                        for (const auto& f : e->fields)
                            if (std::ranges::find(f.choices, name) != f.choices.end()) return true;
                return false;
            }

            // entities

            std::optional<std::string> literal_value(const language::expression& e) {
                if (auto* l = std::get_if<language::literal_expression>(&e.node)) {
                    if (l->type == language::literal_expression::kind::string) return value_of(str(l->value));
                    if (l->type == language::literal_expression::kind::number) return value_of(l->value);
                    return std::nullopt;
                }
                if (auto* n = std::get_if<language::name_expression>(&e.node)) {
                    const auto& parts = n->name.parts;
                    if (parts.size() == 2 && is_choice(parts[1])) return value_of(str(parts[1]));
                    if (parts.size() == 1 && parts[0] == "none") return nothing_value();
                    if (parts.size() == 1 && (parts[0] == "true" || parts[0] == "false")) return value_of(parts[0]);
                    if (parts.size() == 1 && is_choice(parts[0])) return value_of(str(parts[0]));
                }
                return std::nullopt;
            }

            std::vector<std::string> entity_fields(const language::entity_declaration& e) {
                std::vector<std::string> fields;
                for (const auto& f : e.fields) {
                    std::string says;
                    auto say = [&](const std::string& name, const std::string& code) {
                        says += (says.empty() ? "" : ", ") + (typescript() ? name + ": " : "." + name + " = ") + code;
                    };
                    say("name", str(f.name));
                    bool serial = f.type && f.type->text() == "serial";
                    // In the order package.hpp declares them, as designated initializers go.
                    if (f.required) say("required", "true");
                    if (f.key) say("key", "true");
                    if (f.unique) say("unique", "true");
                    if (!f.choices.empty()) say("choices", strings(f.choices));
                    if (f.initial) {
                        auto* n = std::get_if<language::name_expression>(&f.initial->node);
                        if (n && n->name.text() == "now") {
                            say("now", "true");
                        } else if (auto v = literal_value(*f.initial)) {
                            say("initial", *v);
                        } else {
                            unsupported(f.where, "a field's start that isn't a value, a choice or now");
                        }
                    }
                    if (serial) say("serial", str(f.per.value_or("")));
                    if (std::string to = refers(f); !to.empty()) say("refers", str(to));
                    if (f.after) say("after", str(*f.after));
                    if (f.list) say("list", "true");
                    if (f.range) {
                        say("range", typescript() ? "{ from: " + f.range->from + ", to: " + f.range->to + ", by: " + f.range->by + " }"
                                                  : "field::numbers{" + f.range->from + ", " + f.range->to + ", " + f.range->by + "}");
                    }
                    fields.push_back(typescript() ? "{ " + says + " }" : "{" + says + "}");
                }
                return fields;
            }

            // command bodies

            struct scope {
                const language::entity_declaration* own = nullptr;  // the command's entity, read as e
                const language::entity_declaration* row = nullptr;  // in an each, the row's, read as row
                std::set<std::string> inputs;
                std::map<std::string, const language::field*> parameters;  // a function's, each as a field
            };

            // A function the namespace declares, and what it's called in the code written,
            // with its namespace, since the web app's file holds every namespace's.
            const language::function_declaration* function(const std::string& name) const {
                for (const auto& [f, _] : parts_->functions)
                    if (f->name == name) return f;
                return nullptr;
            }
            std::string function_name(const std::string& name) const { return identifier(ns_ + "_" + name); }

            // Whether an expression is a list: a field or a parameter that holds several, a
            // list's first so many or what's after them, two joined, or a function's that
            // returns one.
            bool listed(const language::expression& e, const scope& s) {
                std::set<const language::function_declaration*> seen;
                return listed(e, s, seen);
            }

            bool listed(const language::expression& e, const scope& s, std::set<const language::function_declaration*>& seen) {
                if (std::holds_alternative<language::list_expression>(e.node)) return true;
                if (auto* b = std::get_if<language::binary_expression>(&e.node)) {
                    return b->op == language::token_kind::plus && (listed(*b->left, s, seen) || listed(*b->right, s, seen));
                }
                if (auto* n = std::get_if<language::name_expression>(&e.node); n && n->name.parts.size() == 1) {
                    const std::string& name = n->name.parts[0];
                    if (auto p = s.parameters.find(name); p != s.parameters.end()) return p->second->list;
                    if (const language::field* f = field(s.row, name)) return f->list;
                    if (const language::field* f = field(s.own, name)) return f->list;
                    return false;
                }
                if (auto* m = std::get_if<language::member_expression>(&e.node)) {
                    auto* object = std::get_if<language::name_expression>(&m->object->node);
                    if (object && s.own && object->name.text() == s.own->name) {
                        const language::field* f = field(s.own, m->member);
                        return f && f->list;
                    }
                    return false;
                }
                if (auto* call = std::get_if<language::call_expression>(&e.node)) {
                    auto* callee = std::get_if<language::name_expression>(&call->callee->node);
                    std::string named = callee ? callee->name.text() : "";
                    if ((named == "first" || named == "drop") && call->arguments.size() == 2) return listed(*call->arguments[0], s, seen);
                    const language::function_declaration* called = function(named);
                    if (!called || !seen.insert(called).second) return false;
                    // A function returns a list when any of its returns is one.
                    scope inside;
                    for (std::size_t i = 0; i < called->parameters.size() && i < called->parameter_types.size(); ++i) inside.parameters[called->parameters[i]] = &called->parameter_types[i];
                    std::function<bool(const std::vector<language::statement>&)> returns = [&](const std::vector<language::statement>& body) {
                        for (const auto& st : body) {
                            if (auto* r = std::get_if<language::return_statement>(&st.node); r && r->value) {
                                if (auto* m = std::get_if<language::match_expression>(&r->value->node)) {
                                    for (const auto& arm : m->arms)
                                        if (listed(*arm.value, inside, seen)) return true;
                                } else if (listed(*r->value, inside, seen)) {
                                    return true;
                                }
                            }
                            if (auto* i = std::get_if<language::if_statement>(&st.node); i && (returns(i->then_body) || returns(i->else_body))) return true;
                        }
                        return false;
                    };
                    return returns(called->body);
                }
                return false;
            }

            piece expression(const language::expression& e, const scope& s) {
                if (auto v = literal_value(e)) return {*v};
                if (auto* n = std::get_if<language::name_expression>(&e.node)) {
                    const auto& parts = n->name.parts;
                    if (parts.size() == 1 && s.parameters.contains(parts[0])) return {identifier(parts[0])};
                    // riffle, or pace::riffle: a choice of a parameter's enum.
                    for (const auto& [_, f] : s.parameters) {
                        if ((parts.size() == 1 || parts[0] == f->enum_name) && std::ranges::find(f->choices, parts.back()) != f->choices.end()) return {value_of(str(parts.back()))};
                    }
                    if (parts.size() == 1) {
                        const std::string& name = parts[0];
                        if (name == "now") return {value_of("c.now()")};
                        if (s.inputs.contains(name)) return {"c.input(" + str(name) + ")"};
                        if (s.row && (field(s.row, name) || name == "id")) return {"row[" + str(name) + "]"};
                        if (field(s.own, name) || name == "id") return {"e[" + str(name) + "]"};
                    }
                    // stage::to_do, on a board: its own stage of that name, the one keyed by
                    // the board and the name.
                    if (parts.size() == 2 && s.own) {
                        if (const language::entity_declaration* named = entity(parts[0])) {
                            bool keyed_by_own = std::ranges::any_of(named->fields, [&](const language::field& f) { return f.key && f.type && f.type->text() == s.own->name; });
                            if (keyed_by_own) {
                                if (typescript()) return {"keyOf([text(e[\"id\"]), " + str(parts[1]) + "])"};
                                return {"value(one::screens::key_of({e[\"id\"].as_string(), " + str(parts[1]) + "}))"};
                            }
                        }
                    }
                    unsupported(e.where, "the name " + n->name.text() + " here");
                    return {nothing_value()};
                }
                if (auto* m = std::get_if<language::member_expression>(&e.node)) {
                    // A field of what a field points at: book.status.
                    if (auto* object = std::get_if<language::name_expression>(&m->object->node); object && object->name.parts.size() == 1) {
                        const std::string& through = object->name.parts[0];
                        const language::entity_declaration* from = s.row && field(s.row, through) ? s.row : s.own;
                        std::string held = from == s.row ? "row" : "e";
                        if (const language::field* f = field(from, through); f && !refers(*f).empty()) {
                            if (m->member == "id") return {held + "[" + str(through) + "]"};
                            return {"c.at(" + str(refers(*f)) + ", " + held + "[" + str(through) + "])[" + str(m->member) + "]"};
                        }
                    }
                    unsupported(e.where, "reading a field through more than one step");
                    return {nothing_value()};
                }
                // was crop_y: what the command's own entity held before it, as it's kept.
                if (auto* call = std::get_if<language::call_expression>(&e.node)) {
                    auto* callee = std::get_if<language::name_expression>(&call->callee->node);
                    auto* named = call->arguments.size() == 1 ? std::get_if<language::name_expression>(&call->arguments[0]->node) : nullptr;
                    if (callee && callee->name.text() == "was" && named && named->name.parts.size() == 1 && s.own && field(s.own, named->name.parts[0])) {
                        return {"c.at(" + str(full(s.own->name)) + ", e[" + str("id") + "])[" + str(named->name.parts[0]) + "]"};
                    }
                    // min(a, b) and max(a, b), of two numbers.
                    if (callee && (callee->name.text() == "min" || callee->name.text() == "max") && call->arguments.size() == 2) {
                        std::string a = number_of(as_value(expression(*call->arguments[0], s))), b = number_of(as_value(expression(*call->arguments[1], s)));
                        std::string said = typescript() ? "Math." + callee->name.text() : "std::" + callee->name.text() + "<double>";
                        return {sum(said + "(" + a + ", " + b + ")")};
                    }
                    // random(1, 6), and a list's first so many, what's after them, and how many it holds.
                    std::string called_name = callee ? callee->name.text() : "";
                    auto helper = [&](const std::string& ts, const std::string& cpp) {
                        if (typescript()) helpers_.insert(ts);
                        return typescript() ? ts : "one::local::" + cpp;
                    };
                    if (called_name == "random" && call->arguments.size() == 2) {
                        std::string low = number_of(as_value(expression(*call->arguments[0], s))), high = number_of(as_value(expression(*call->arguments[1], s)));
                        return {sum(helper("random", "random") + "(" + low + ", " + high + ")")};
                    }
                    if ((called_name == "first" || called_name == "drop") && call->arguments.size() == 2 && listed(*call->arguments[0], s)) {
                        std::string list = as_value(expression(*call->arguments[0], s)), n = number_of(as_value(expression(*call->arguments[1], s)));
                        return {(called_name == "first" ? helper("firstOf", "first_of") : helper("dropOf", "drop_of")) + "(" + list + ", " + n + ")"};
                    }
                    if (called_name == "count" && call->arguments.size() == 1 && listed(*call->arguments[0], s)) {
                        return {sum(helper("counted", "counted") + "(" + as_value(expression(*call->arguments[0], s)) + ")")};
                    }
                    // A function the namespace declares, a choice given by its name as its parameter's enum keeps it.
                    if (const language::function_declaration* called = callee ? function(callee->name.text()) : nullptr) {
                        std::string args;
                        for (std::size_t i = 0; i < call->arguments.size(); ++i) {
                            const auto& a = *call->arguments[i];
                            const language::field* param = i < called->parameter_types.size() ? &called->parameter_types[i] : nullptr;
                            auto* given = std::get_if<language::name_expression>(&a.node);
                            bool choice = given && param && std::ranges::find(param->choices, given->name.parts.back()) != param->choices.end() &&
                                          (given->name.parts.size() == 1 ? !s.parameters.contains(given->name.parts[0]) && !s.inputs.contains(given->name.parts[0]) &&
                                                                               !field(s.own, given->name.parts[0]) && !field(s.row, given->name.parts[0])
                                                                         : given->name.parts[0] == param->enum_name);
                            args += (args.empty() ? "" : ", ") + (choice ? value_of(str(given->name.parts.back())) : as_value(expression(a, s)));
                        }
                        return {function_name(called->name) + "(" + args + ")"};
                    }
                    unsupported(e.where, "this call");
                    return {nothing_value()};
                }
                if (auto* u = std::get_if<language::unary_expression>(&e.node)) {
                    piece inner = expression(*u->operand, s);
                    if (u->op == language::token_kind::logical_not) return {"!" + paren(as_bool(inner)), true};
                    if (u->op == language::token_kind::minus) return {value_of("-" + number_of(as_value(inner)))};
                }
                // [] or [a, b]: a list.
                if (auto* list = std::get_if<language::list_expression>(&e.node)) {
                    std::string items;
                    for (const auto& item : list->items) items += (items.empty() ? "" : ", ") + as_value(expression(*item, s));
                    return {typescript() ? "[" + items + "]" : "value(std::vector<value>{" + items + "})"};
                }
                if (auto* b = std::get_if<language::binary_expression>(&e.node)) {
                    piece l = expression(*b->left, s), r = expression(*b->right, s);
                    using language::token_kind;
                    // hand + first(deck, 13): two lists joined.
                    if (b->op == token_kind::plus && (listed(*b->left, s) || listed(*b->right, s))) {
                        if (typescript()) helpers_.insert("joined");
                        return {(typescript() ? "joined(" : "one::local::joined(") + as_value(l) + ", " + as_value(r) + ")"};
                    }
                    switch (b->op) {
                        case token_kind::equal: return {"same(" + as_value(l) + ", " + as_value(r) + ")", true};
                        case token_kind::not_equal: return {"!same(" + as_value(l) + ", " + as_value(r) + ")", true};
                        case token_kind::less: return {"compare(" + as_value(l) + ", " + as_value(r) + ") < 0", true};
                        case token_kind::less_equal: return {"compare(" + as_value(l) + ", " + as_value(r) + ") <= 0", true};
                        case token_kind::greater: return {"compare(" + as_value(l) + ", " + as_value(r) + ") > 0", true};
                        case token_kind::greater_equal: return {"compare(" + as_value(l) + ", " + as_value(r) + ") >= 0", true};
                        case token_kind::logical_and: return {"(" + as_bool(l) + " && " + as_bool(r) + ")", true};
                        case token_kind::logical_or: return {"(" + as_bool(l) + " || " + as_bool(r) + ")", true};
                        case token_kind::plus: return {sum(number_of(as_value(l)) + " + " + number_of(as_value(r)))};
                        case token_kind::minus: return {sum(number_of(as_value(l)) + " - " + number_of(as_value(r)))};
                        case token_kind::star: return {sum(number_of(as_value(l)) + " * " + number_of(as_value(r)))};
                        // Divided, or what's left over, as every target works them out; in C++, said
                        // as one::local's, since the standard library has a remainder too.
                        case token_kind::slash: return {sum((typescript() ? "divided(" : "one::local::divided(") + number_of(as_value(l)) + ", " + number_of(as_value(r)) + ")")};
                        case token_kind::remainder: return {sum((typescript() ? "remainder(" : "one::local::remainder(") + number_of(as_value(l)) + ", " + number_of(as_value(r)) + ")")};
                        default: break;
                    }
                }
                unsupported(e.where, "this kind of expression");
                return {nothing_value()};
            }

            // What an each or a delete picks, like project == id || owner == id: each
            // left side is the row's field and each right side the command's, its own id.
            piece picking(const language::expression& e, const scope& row, const scope& outer) {
                if (auto* b = std::get_if<language::binary_expression>(&e.node)) {
                    using language::token_kind;
                    if (b->op == token_kind::logical_or) return {"(" + as_bool(picking(*b->left, row, outer)) + " || " + as_bool(picking(*b->right, row, outer)) + ")", true};
                    if (b->op == token_kind::equal) return {"same(" + as_value(expression(*b->left, row)) + ", " + as_value(expression(*b->right, outer)) + ")", true};
                }
                return expression(e, row);
            }

            // The fields given in a create or dispatch, as a value.
            std::string given(const std::vector<language::field_value>& values, const scope& s, std::string* id) {
                std::string fields;
                for (const auto& v : values) {
                    std::string code = as_value(expression(*v.value, s));
                    if (id && v.name == "id") {
                        *id = text_of(code);
                        continue;
                    }
                    fields += (fields.empty() ? "" : ", ") + (typescript() ? str(v.name) + ": " + code : "{" + str(v.name) + ", " + code + "}");
                }
                if (typescript()) return fields.empty() ? "{}" : "{ " + fields + " }";
                return "value(fields{" + fields + "})";
            }

            void statements(stream& out, const std::string& path, const std::vector<language::statement>& body, scope& s) {
                for (const auto& st : body) {
                    auto from = out.from(path, st.where.line);
                    std::visit(
                        [&](const auto& n) {
                            using kind = std::decay_t<decltype(n)>;
                            if constexpr (std::is_same_v<kind, language::require_statement>) {
                                out.line("if (!" + paren(as_bool(expression(*n.condition, s))) + ") return c.fail(" + str(n.message) + ");");
                            } else if constexpr (std::is_same_v<kind, language::assign_statement>) {
                                auto* target = std::get_if<language::name_expression>(&n.target->node);
                                if (!target || target->name.parts.size() != 1) {
                                    unsupported(st.where, "setting anything but a field");
                                    return;
                                }
                                std::string held = s.row && field(s.row, target->name.parts[0]) ? "row" : "e";
                                out.line(set_field(held, target->name.parts[0]) + " = " + as_value(expression(*n.value, s)) + ";");
                            } else if constexpr (std::is_same_v<kind, language::clear_statement>) {
                                for (const auto& f : n.fields) out.line(set_field("e", f) + " = " + nothing_value() + ";");
                            } else if constexpr (std::is_same_v<kind, language::if_statement>) {
                                out.open("if (" + as_bool(expression(*n.condition, s)) + ") {");
                                statements(out, path, n.then_body, s);
                                if (!n.else_body.empty()) {
                                    out.dedent();
                                    out.open("} else {");
                                    statements(out, path, n.else_body, s);
                                }
                                out.close("}");
                            } else if constexpr (std::is_same_v<kind, language::create_statement>) {
                                if (n.command) {
                                    unsupported(st.where, "making something as another command makes it");
                                    return;
                                }
                                out.line(or_refuse("c.create(" + str(full(n.entity)) + ", " + given(n.values, s, nullptr) + ")", "made", true));
                            } else if constexpr (std::is_same_v<kind, language::dispatch_statement>) {
                                const auto& parts = n.command.parts;
                                std::string verb = parts.empty() ? "" : parts.back();
                                if ((verb != "update" && verb != "create" && verb != "delete") || !n.lists.empty()) {
                                    unsupported(st.where, "running a command other than an update, a create or a delete from another");
                                    return;
                                }
                                std::string target = n.command.text();
                                target = target.substr(0, target.rfind("::"));
                                // A create or a delete that does no more than make or delete, as
                                // the store does it; one with a body of its own would need it run too.
                                if (verb != "update") {
                                    const language::command_declaration* run = nullptr;
                                    for (const auto& [c, _] : parts_->commands) {
                                        if (c->name.text() == n.command.text() || c->name.text() == ns_ + "::" + n.command.text()) run = c;
                                    }
                                    if (!run || !run->body.empty()) {
                                        unsupported(st.where, "running a " + verb + " that does more than " + (verb == "create" ? "make" : "delete") + " from another");
                                        return;
                                    }
                                }
                                if (verb == "create") {
                                    out.line(or_refuse("c.create(" + str(full(target)) + ", " + given(n.values, s, nullptr) + ")", "made", true));
                                    return;
                                }
                                std::string id;
                                std::string fields = given(n.values, s, &id);
                                if (id.empty() && s.row) id = text_of("row[\"id\"]");  // the row an each picked
                                if (id.empty()) {
                                    unsupported(st.where, (verb == "update" ? "an update" : "a delete") + std::string(" that doesn't say which, with id = ..."));
                                    return;
                                }
                                if (verb == "delete") out.line(or_refuse("c.remove(" + str(full(target)) + ", " + id + ")", "done", false));
                                else out.line(or_refuse("c.update(" + str(full(target)) + ", " + id + ", " + fields + ")", "done", false));
                            } else if constexpr (std::is_same_v<kind, language::each_statement>) {
                                const language::entity_declaration* over = entity(n.entity);
                                if (!over || n.other) {
                                    unsupported(st.where, "going through rows of another kind");
                                    return;
                                }
                                scope inner = s;
                                inner.row = over;
                                out.open(for_each_row(full(n.entity)));
                                if (n.where) out.line("if (!" + paren(as_bool(picking(*n.where, inner, s))) + ") continue;");
                                statements(out, path, n.body, inner);
                                out.close("}");
                            } else if constexpr (std::is_same_v<kind, language::delete_statement>) {
                                const language::entity_declaration* over = entity(n.entity);
                                if (!over) {
                                    unsupported(st.where, "deleting rows of another kind");
                                    return;
                                }
                                scope inner = s;
                                inner.row = over;
                                out.open(for_each_row(full(n.entity)));
                                if (n.where) out.line("if (!" + paren(as_bool(picking(*n.where, inner, s))) + ") continue;");
                                out.line(or_refuse("c.remove(" + str(full(n.entity)) + ", " + text_of("row[\"id\"]") + ")", "done", false));
                                out.close("}");
                            } else if constexpr (std::is_same_v<kind, language::input_statement> || std::is_same_v<kind, language::permission_statement> ||
                                                 std::is_same_v<kind, language::changes_statement>) {
                                // Said in the command's line: what it takes, and what it may be sent.
                            } else {
                                unsupported(st.where, "this statement");
                            }
                        },
                        st.node);
                }
            }

            static std::string paren(const std::string& code) { return code.starts_with("(") || code.starts_with("same(") || code.starts_with("truthy(") ? code : "(" + code + ")"; }

            void command(stream& out, const language::command_declaration& c, const std::string& path, int line, bool last) {
                path_ = path;
                auto from = out.from(path, line);
                const auto& parts = c.name.parts;
                if (parts.size() != 2) {
                    unsupported(c.name.where, "a command not named for its entity, like layer::place");
                    return;
                }
                const language::entity_declaration* own = entity(parts[0]);
                std::string on = full(parts[0]), action = parts[1];
                std::string name = language::qualified(ns_, c.name.text());
                std::string head = "command(" + str(name) + ")";
                head += action == "create" ? ".creates(" + str(on) + ")" : action == "update" ? ".updates(" + str(on) + ")"
                        : action == "delete" ? ".deletes(" + str(on) + ")" : ".on(" + str(on) + ")";
                // What it may be sent: what its forms ask for and its changes name; a
                // create takes every field, and an update with neither all but its keys.
                std::vector<std::string> allowed;
                if (auto forms = form_fields_.find(name); forms != form_fields_.end()) allowed = forms->second;
                scope s{own, nullptr, {}, {}};
                for (const auto& st : c.body) {
                    if (auto* changes = std::get_if<language::changes_statement>(&st.node)) {
                        for (const auto& name : changes->fields)
                            if (std::ranges::find(allowed, name) == allowed.end()) allowed.push_back(name);
                    }
                    if (auto* input = std::get_if<language::input_statement>(&st.node)) s.inputs.insert(input->name);
                }
                if (action != "create" && action != "delete" && !allowed.empty()) head += ".fields(" + strings(allowed) + ")";
                if (!s.inputs.empty()) head += ".takes(" + strings(std::vector<std::string>(s.inputs.begin(), s.inputs.end())) + ")";
                if (c.apart) head += ".apart()";
                bool has_body = std::ranges::any_of(c.body, [](const language::statement& st) {
                    return !std::holds_alternative<language::input_statement>(st.node) && !std::holds_alternative<language::permission_statement>(st.node) &&
                           !std::holds_alternative<language::changes_statement>(st.node);
                });
                if (!has_body) {
                    out.line(head + (last ? "" : ","));
                    return;
                }
                out.open(head + (typescript() ? ".does((c, e) => {" : ".does([](context& c, value& e) -> std::expected<void, std::string> {"));
                // An input of an enum's, refused unless it's one of its choices.
                for (const auto& st : c.body) {
                    auto* input = std::get_if<language::input_statement>(&st.node);
                    if (!input || input->choices.empty()) continue;
                    auto from_input = out.from(path, st.where.line);
                    std::string said;
                    for (std::size_t i = 0; i < input->choices.size(); ++i) said += (i == 0 ? "" : i + 1 == input->choices.size() ? " or " : ", ") + input->choices[i];
                    std::string given = text_of("c.input(" + str(input->name) + ")");
                    std::string known = typescript() ? strings(input->choices) + ".includes(" + given + ")"
                                                     : "std::ranges::contains(std::vector<std::string>" + strings(input->choices) + ", " + given + ")";
                    out.line("if (!" + known + ") return c.fail(" + str(input->name + " is one of " + said) + ");");
                }
                statements(out, path, c.body, s);
                out.line("return c.done();");
                out.close(std::string("})") + (last ? "" : ","));
            }

            // views

            struct view_scope {
                std::string per;   // the entity the view is per, by its own name
                std::string each;  // in a value worked out for a row, the row's entity
            };

            // What a condition's side compares with, as package.hpp's compared.
            std::optional<std::string> wanted(const language::expression& e, const view_scope& v) {
                if (auto lit = literal_value(e)) return *lit;
                if (auto* m = std::get_if<language::member_expression>(&e.node)) {
                    if (auto* object = std::get_if<language::name_expression>(&m->object->node); object && object->name.parts.size() == 1) {
                        const std::string& name = object->name.parts[0];
                        if (!v.each.empty() && name == v.each && m->member == "id") return "row";
                        if (!v.per.empty() && name == v.per) {
                            if (m->member == "id") return "subject";
                            return typescript() ? "subjectField(" + str(m->member) + ")" : "subject_field{" + str(m->member) + "}";
                        }
                    }
                }
                return std::nullopt;
            }

            // The field of the rows a query picks that a side names: status, or loan.book.
            // Every row has these, kept by the store as it's made and changed.
            static bool kept_by_store(const std::string& name) { return name == "id" || name == "created_at" || name == "updated_at"; }

            std::optional<std::string> picked_field(const language::expression& e, const std::string& rows) {
                if (auto* n = std::get_if<language::name_expression>(&e.node); n && n->name.parts.size() == 1) {
                    if (field(entity(rows), n->name.parts[0]) || kept_by_store(n->name.parts[0])) return n->name.parts[0];
                }
                if (auto* m = std::get_if<language::member_expression>(&e.node)) {
                    auto* object = std::get_if<language::name_expression>(&m->object->node);
                    if (object && object->name.text() == rows.substr(rows.rfind(':') == std::string::npos ? 0 : rows.rfind(':') + 1)) return m->member;
                    if (object && object->name.parts.size() == 1 && field(entity(rows), object->name.parts[0])) return object->name.parts[0] + "." + m->member;
                }
                return std::nullopt;
            }

            // A where condition as a query's conditions: comparisons joined by &&, and
            // ways joined by ||.
            std::string query(const std::string& rows, const language::expression* condition, const view_scope& v) {
                std::string q = "all(" + str(full(rows)) + ")";
                if (!condition) return q;
                if (auto* b = std::get_if<language::binary_expression>(&condition->node)) {
                    if (b->op == language::token_kind::logical_or) return query(rows, b->left.get(), v) + ".either(" + query(rows, b->right.get(), v) + ")";
                }
                return q + conditions(rows, *condition, v);
            }

            std::string conditions(const std::string& rows, const language::expression& e, const view_scope& v) {
                auto* b = std::get_if<language::binary_expression>(&e.node);
                if (b && b->op == language::token_kind::logical_and) return conditions(rows, *b->left, v) + conditions(rows, *b->right, v);
                if (b && (b->op == language::token_kind::equal || b->op == language::token_kind::not_equal)) {
                    auto f = picked_field(*b->left, rows);
                    auto w = wanted(*b->right, v);
                    if (!f || !w) {
                        f = picked_field(*b->right, rows);
                        w = wanted(*b->left, v);
                    }
                    if (f && w) return std::string(b->op == language::token_kind::equal ? ".where(" : ".except(") + str(*f) + ", " + *w + ")";
                }
                unsupported(e.where, "this condition in a view");
                return "";
            }

            // A path from what a view is per, like board.stage.title, as the field it
            // reads: stage.title. None when it doesn't start there.
            static std::optional<std::string> rooted(const language::expression& e, const std::string& per) {
                if (per.empty()) return std::nullopt;
                auto* m = std::get_if<language::member_expression>(&e.node);
                if (!m) return std::nullopt;
                if (auto* n = std::get_if<language::name_expression>(&m->object->node))
                    return n->name.parts.size() == 1 && n->name.parts[0] == per ? std::optional<std::string>(m->member) : std::nullopt;
                if (auto inner = rooted(*m->object, per)) return *inner + "." + m->member;
                return std::nullopt;
            }

            // What an each's source names: the entity its rows are.
            std::optional<std::string> source(const language::view_each& each) {
                if (auto* n = std::get_if<language::name_expression>(&each.source->node); n && entity(n->name.text())) return n->name.text();
                return std::nullopt;
            }

            // What a list or a first is ordered by: a field of its own, said bare or, for
            // a first, through what it takes, as photo.taken; -field for the other way round.
            static std::optional<std::string> order_key(const language::expression& o, const std::string& own = "") {
                const language::expression* key = &o;
                auto* u = std::get_if<language::unary_expression>(&o.node);
                bool reverse = u && u->op == language::token_kind::minus;
                if (reverse) key = u->operand.get();
                std::optional<std::string> field;
                if (auto* n = std::get_if<language::name_expression>(&key->node); n && n->name.parts.size() == 1) field = n->name.parts[0];
                if (auto* m = std::get_if<language::member_expression>(&key->node); m && !own.empty()) {
                    auto* object = std::get_if<language::name_expression>(&m->object->node);
                    if (object && object->name.parts.size() == 1 && object->name.parts[0] == own) field = m->member;
                }
                if (!field) return std::nullopt;
                return (reverse ? "-" : "") + *field;
            }

            // A view's chain, a line each, with where each came from.
            using chain = std::vector<std::pair<std::string, int>>;

            void list(chain& out, const language::view_each& each, const view_scope& v) {
                int at = each.where.line;
                auto rows = source(each);
                if (!rows || !each.within.empty() || each.changes || !each.lists.empty()) {
                    unsupported(each.where, "this list in a view");
                    return;
                }
                std::string q = query(*rows, each.condition.get(), v);
                out.emplace_back(each.name ? ".list(" + str(*each.name) + ", " + q + ")" : ".each(" + q + ")", at);
                std::vector<std::string> order;
                for (const auto& o : each.order) {
                    if (auto key = order_key(*o)) order.push_back(str(*key));
                    else unsupported(o->where, "ordering by this");
                }
                if (!order.empty()) {
                    std::string keys;
                    for (const auto& k : order) keys += (keys.empty() ? "" : ", ") + std::string("by(") + k + ")";
                    out.emplace_back(".order(" + bracketed(keys) + ")", at);
                }
                if (each.limit) out.emplace_back(".limit(" + std::to_string(*each.limit) + ")", at);
                std::vector<std::string> fields;
                std::vector<std::string> values;
                for (const auto& r : each.rows) {
                    if (!r.name) {
                        if (auto f = picked_field(*r.value, *rows)) fields.push_back(*f);
                        else unsupported(r.where, "this in a row");
                        continue;
                    }
                    // name = first(other where ... order by ...).field: a field of the first
                    // that's found, or, as first(other where ..., 4).field, of the first so
                    // many, a list; count(other where ...): how many; sum(other where
                    // ...).field: what a number of theirs adds up to.
                    auto* m = std::get_if<language::member_expression>(&r.value->node);
                    auto* call = std::get_if<language::call_expression>(m ? &m->object->node : &r.value->node);
                    auto* callee = call ? std::get_if<language::name_expression>(&call->callee->node) : nullptr;
                    std::string called = callee ? callee->name.text() : "";
                    const language::expression* argument = call && (call->arguments.size() == 1 || (called == "first" && call->arguments.size() == 2)) ? call->arguments[0].get() : nullptr;
                    auto* where = argument ? std::get_if<language::where_expression>(&argument->node) : nullptr;
                    auto* other = argument ? std::get_if<language::name_expression>(&(where ? *where->source : *argument).node) : nullptr;
                    bool known = other && entity(other->name.text()) && (m ? called == "first" || called == "sum" : called == "count");
                    std::string many;
                    if (known && call->arguments.size() == 2) {
                        auto* n = std::get_if<language::literal_expression>(&call->arguments[1]->node);
                        if (n && n->type == language::literal_expression::kind::number) many = n->value;
                        else known = false;
                    }
                    std::vector<std::string> keys;
                    if (where && known) {
                        for (const auto& k : where->order) {
                            auto key = order_key(*k, other->name.parts.back());
                            if (key) keys.push_back((keys.empty() ? "by(" : ", by(") + str(*key) + ")");
                            else known = false;
                        }
                    }
                    if (!known) {
                        unsupported(r.where, "this value in a row");
                        continue;
                    }
                    view_scope inner = v;
                    inner.each = *rows;
                    std::string q = query(other->name.text(), where ? where->condition.get() : nullptr, inner);
                    if (called == "count") values.push_back(".counted(" + str(*r.name) + ", " + q + ")");
                    else if (called == "sum") values.push_back(".summed(" + str(*r.name) + ", " + q + ", " + str(m->member) + ")");
                    else values.push_back(".value(" + str(*r.name) + ", " + q + ", " + str(m->member) + (keys.empty() && many.empty() ? "" : ", " + bracketed(std::accumulate(keys.begin(), keys.end(), std::string()))) +
                                          (many.empty() ? "" : ", " + many) + ")");
                }
                if (!fields.empty()) out.emplace_back(".fields(" + strings(fields) + ")", at);
                for (const auto& line : values) out.emplace_back(line, at);
            }

            void view(stream& out, const language::view_declaration& v, const std::string& path, int line, bool last) {
                path_ = path;
                view_scope s;
                chain lines{{"view(" + str(full(v.name)) + ")", line}};
                if (v.per) {
                    if (!entity(*v.per)) unsupported(language::location{line, 1}, "a view per something that isn't an entity");
                    s.per = *v.per;
                    lines.emplace_back(".per(" + str(full(*v.per)) + ")", line);
                }
                for (const auto& value : v.values) {
                    if (!value.name) {
                        // A field of what the view is per, copied as it's named.
                        std::string named;
                        if (auto* n = std::get_if<language::name_expression>(&value.value->node); n && n->name.parts.size() == 1) named = n->name.parts[0];
                        if (auto* m = std::get_if<language::member_expression>(&value.value->node)) {
                            if (auto* o = std::get_if<language::name_expression>(&m->object->node); o && o->name.parts.size() == 1) named = o->name.parts[0] + "." + m->member;
                        }
                        if (named.empty() || s.per.empty()) unsupported(value.where, "this value in a view");
                        else lines.emplace_back(".copy(" + str(named) + ", " + str(named) + ")", value.where.line);
                        continue;
                    }
                    // A field of what it's per, said bare: name is board.name, stage.title
                    // board.stage.title.
                    if (auto through = rooted(*value.value, s.per)) {
                        lines.emplace_back(".copy(" + str(*value.name) + ", " + str(*through) + ")", value.where.line);
                        continue;
                    }
                    // How many of its own list's rows pass, or what a number of theirs adds up
                    // to: total = count(clippings), size = sum(clippings).picture_bytes.
                    auto counted = language::counted_rows(v, value);
                    auto summed = language::summed_rows(v, value);
                    if (const language::row_count& over = counted.list ? counted : summed; over.list) {
                        std::string tests;
                        std::function<void(const language::expression&)> add = [&](const language::expression& e) {
                            auto& b = std::get<language::binary_expression>(e.node);
                            if (b.op == language::token_kind::logical_and) {
                                add(*b.left);
                                add(*b.right);
                                return;
                            }
                            static const std::map<language::token_kind, std::string> ops{
                                {language::token_kind::equal, "=="}, {language::token_kind::not_equal, "!="}, {language::token_kind::less, "<"},
                                {language::token_kind::less_equal, "<="}, {language::token_kind::greater, ">"}, {language::token_kind::greater_equal, ">="}};
                            tests += (tests.empty() ? "" : ", ") + strings({language::text_of(*b.left), ops.at(b.op), language::text_of(*b.right)});
                        };
                        if (over.where) add(*over.where->condition);
                        std::string line = counted.list ? ".tally(" + str(*value.name) + ", " + str(*over.list->name)
                                                        : ".total(" + str(*value.name) + ", " + str(*over.list->name) + ", " + str(summed.field);
                        lines.emplace_back(line + (tests.empty() ? "" : ", " + bracketed(tests)) + ")", value.where.line);
                        continue;
                    }
                    auto* call = std::get_if<language::call_expression>(&value.value->node);
                    auto* callee = call ? std::get_if<language::name_expression>(&call->callee->node) : nullptr;
                    if (callee && callee->name.text() == "count" && call->arguments.size() == 1) {
                        const auto& argument = *call->arguments[0];
                        if (auto* n = std::get_if<language::name_expression>(&argument.node); n && entity(n->name.text())) {
                            lines.emplace_back(".count(" + str(*value.name) + ", " + query(n->name.text(), nullptr, s) + ")", value.where.line);
                            continue;
                        }
                        if (auto* w = std::get_if<language::where_expression>(&argument.node)) {
                            if (auto* n = std::get_if<language::name_expression>(&w->source->node); n && entity(n->name.text())) {
                                lines.emplace_back(".count(" + str(*value.name) + ", " + query(n->name.text(), w->condition.get(), s) + ")", value.where.line);
                                continue;
                            }
                        }
                    }
                    unsupported(value.where, "this value in a view");
                }
                for (const auto& each : v.each) list(lines, each, s);
                if (!last) lines.back().first += ",";
                for (std::size_t i = 0; i < lines.size(); ++i) {
                    auto from = out.from(path, lines[i].second);
                    if (i == 0 && lines.size() > 1) out.open(lines[i].first);
                    else out.line(lines[i].first);
                }
                if (lines.size() > 1) out.dedent();
            }

            // functions

            // A function, on values as rows keep them, as every target's are.
            void function(stream& out, const language::function_declaration& f, const std::string& path, int line) {
                path_ = path;
                auto from = out.from(path, line);
                scope s;
                std::string list;
                for (std::size_t i = 0; i < f.parameters.size(); ++i) {
                    if (i < f.parameter_types.size()) s.parameters[f.parameters[i]] = &f.parameter_types[i];
                    list += (list.empty() ? "" : ", ") + (typescript() ? identifier(f.parameters[i]) + ": Value" : "value " + identifier(f.parameters[i]));
                }
                out.open(typescript() ? "function " + function_name(f.name) + "(" + list + "): Value {" : "static value " + function_name(f.name) + "(" + list + ") {");
                function_statements(out, path, f.body, s);
                if (!always_returns(f.body)) out.line("return " + nothing_value() + ";");
                out.close("}");
            }

            void function_statements(stream& out, const std::string& path, const std::vector<language::statement>& body, const scope& s) {
                for (const auto& st : body) {
                    auto from = out.from(path, st.where.line);
                    auto* r = std::get_if<language::return_statement>(&st.node);
                    if (r && r->value && std::holds_alternative<language::match_expression>(r->value->node)) {
                        // return match way { … }: only the arm chosen worked out.
                        const auto& m = std::get<language::match_expression>(r->value->node);
                        piece on = expression(*m.subject, s);
                        for (const auto& arm : m.arms) out.line("if (" + text_of(as_value(on)) + (typescript() ? " === " : " == ") + str(arm.choice) + ") return " + as_value(expression(*arm.value, s)) + ";");
                        out.line("return " + (m.otherwise ? as_value(expression(*m.otherwise, s)) : nothing_value()) + ";");
                    } else if (r && r->value) {
                        out.line("return " + as_value(expression(*r->value, s)) + ";");
                    } else if (auto* i = std::get_if<language::if_statement>(&st.node)) {
                        out.open("if (" + as_bool(expression(*i->condition, s)) + ") {");
                        function_statements(out, path, i->then_body, s);
                        if (!i->else_body.empty()) {
                            out.dedent();
                            out.open("} else {");
                            function_statements(out, path, i->else_body, s);
                        }
                        out.close("}");
                    } else {
                        unsupported(st.where, "this statement in a function; a function can use if and return");
                    }
                }
            }

            static bool always_returns(const std::vector<language::statement>& body) {
                if (body.empty()) return false;
                if (auto* r = std::get_if<language::return_statement>(&body.back().node)) return r->value != nullptr;
                auto* i = std::get_if<language::if_statement>(&body.back().node);
                return i && always_returns(i->then_body) && always_returns(i->else_body);
            }

            // Each of a namespace's functions; in C++, said first, so one may call another
            // declared after it, or itself.
            void functions(stream& out, const namespace_parts& parts) {
                if (!typescript()) {
                    for (const auto& [f, path] : parts.functions) {
                        auto from = out.from(path, parts.lines.at(f));
                        std::string list;
                        for (std::size_t i = 0; i < f->parameters.size(); ++i) list += i ? ", value" : "value";
                        out.line("static value " + function_name(f->name) + "(" + list + ");");
                    }
                    if (!parts.functions.empty()) out.line();
                }
                for (const auto& [f, path] : parts.functions) {
                    function(out, *f, path, parts.lines.at(f));
                    out.line();
                }
            }

            // A namespace's package: its entities, commands and views, in three lists.
            void package_body(stream& body, const namespace_parts& parts) {
                std::string open = typescript() ? "[" : "{", close = typescript() ? "]" : "}";
                body.open(typescript() ? "localPackage(" + str(ns_) + "," : "static const package " + identifier(ns_) + "(" + str(ns_) + ",");
                body.open(open);
                for (std::size_t i = 0; i < parts.entities.size(); ++i) {
                    auto [e, path] = parts.entities[i];
                    auto from = body.from(path, parts.lines.at(e));
                    path_ = path;
                    body.open("entity(" + str(full(e->name)) + ", " + open);
                    for (const auto& f : entity_fields(*e)) body.line(f + ",");
                    body.close(close + ")" + (i + 1 < parts.entities.size() ? "," : ""));
                }
                body.close(close + ",");
                body.open(open);
                for (std::size_t i = 0; i < parts.commands.size(); ++i) {
                    auto [c, path] = parts.commands[i];
                    command(body, *c, path, parts.lines.at(c), i + 1 == parts.commands.size());
                }
                body.close(close + ",");
                body.open(open);
                for (std::size_t i = 0; i < parts.views.size(); ++i) {
                    auto [v, path] = parts.views[i];
                    view(body, *v, path, parts.lines.at(v), i + 1 == parts.views.size());
                }
                body.close(close + (typescript() ? ")," : ");"));
                body.dedent();
            }

            stream namespace_file(const namespace_parts& parts, const std::string& install) {
                stream declared("\t");
                functions(declared, parts);
                stream body("\t");
                package_body(body, parts);
                body.line(identifier(ns_) + ".install(source);");

                stream out("\t");
                out.generated_from(std::filesystem::path(parts.path).filename().string());
                {
                    auto fixed = out.fixed();
                    out.line("#include <one/local/package.hpp>");
                    out.line("#include <one/screens/screens.hpp>");
                    out.line();
                    out.open("namespace app {");
                    out.line();
                    out.line("using namespace one::local;");
                    out.line("using fields = std::map<std::string, value>;");
                    out.line();
                }
                out.embed(declared);
                {
                    auto fixed = out.fixed();
                    out.line("// " + ns_ + "'s entities, commands and views, kept on this machine.");
                    out.open("void " + install + "(local_source& source) {");
                }
                out.embed(body);
                {
                    auto fixed = out.fixed();
                    out.close("}");
                    out.line();
                    out.close("}  // namespace app");
                }
                return out;
            }

            // The web app's one file: every namespace's package, for its localSource.
            static stream typescript_file(const stream& declared, const stream& packages, const std::string& path, std::set<std::string> helpers) {
                stream out;
                out.generated_from(std::filesystem::path(path).filename().string());
                {
                    auto fixed = out.fixed();
                    // What every app's code may use, and what this one's uses besides, in order.
                    helpers.insert({"all", "by", "command", "compare", "divided", "entity", "failed", "keyOf", "localPackage", "number", "remainder", "row", "same", "subject",
                                    "subjectField", "text", "truthy", "view"});
                    std::vector<std::string> names(helpers.begin(), helpers.end());
                    std::ranges::sort(names, [](const std::string& a, const std::string& b) {
                        return std::ranges::lexicographical_compare(a, b, [](char x, char y) { return std::tolower(x) < std::tolower(y); });
                    });
                    if (!declared.str().empty()) names.insert(std::ranges::find(names, "view"), "type Value");
                    std::string imported;
                    for (const auto& n : names) imported += (imported.empty() ? "" : ", ") + n;
                    out.line("import { " + imported + " } from \"@uione/react/local\";");
                    out.line();
                }
                out.embed(declared);
                {
                    auto fixed = out.fixed();
                    out.line("// Each namespace's entities, commands and views, kept in this browser.");
                    out.open("export const packages = [");
                }
                out.embed(packages);
                {
                    auto fixed = out.fixed();
                    out.close("];");
                }
                return out;
            }
        };

    }  // namespace

    generated_local generate_local(const std::vector<language::file>& files) {
        generated_local out = local_generator(files, dialect::cpp).generate();
        if (out.errors.empty()) out.web = local_generator(files, dialect::typescript).generate().files;
        return out;
    }

} // namespace one::generators
