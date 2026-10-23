// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "driver/upgrade.hpp"

#include <algorithm>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "platform/files.hpp"

namespace one::driver {

    // Applies fixes to a project's files: each once, the later ones in a file first,
    // so the earlier ones' places stay where they were.
    inline std::size_t apply_fixes(sources& files, const language::diagnostics& found) {
        std::map<std::string, std::vector<const language::diagnostic*>> by_file;
        for (const auto& d : found) {
            if (d.fix && files.contains(d.path)) by_file[d.path].push_back(&d);
        }
        std::size_t applied = 0;
        for (auto& [path, list] : by_file) {
            std::sort(list.begin(), list.end(), [](auto* a, auto* b) {
                return std::tie(b->fix->where.line, b->fix->where.column) < std::tie(a->fix->where.line, a->fix->where.column);
            });
            std::string& text = files[path];
            std::pair<std::size_t, std::size_t> last{0, 0};
            // Applied from the end back, each before the last one applied: one that runs
            // into it waits for the next round, when it's worked out again.
            std::size_t before = std::string::npos;
            for (auto* d : list) {
                std::pair<std::size_t, std::size_t> at{d->fix->where.line, d->fix->where.column};
                if (at == last) continue;
                last = at;
                // From line and column, both counted from 1, to a place in the text.
                std::size_t offset = 0;
                for (std::size_t line = 1; line < at.first && offset != std::string::npos; ++line) {
                    offset = text.find('\n', offset);
                    if (offset != std::string::npos) ++offset;
                }
                if (offset == std::string::npos) continue;
                offset += at.second - 1;
                if (offset + d->fix->length > text.size()) continue;
                if (before != std::string::npos && offset + d->fix->length > before) continue;
                text.replace(offset, d->fix->length, d->fix->text);
                before = offset;
                ++applied;
            }
        }
        return applied;
    }

    bool record_version(sources& files, std::string_view version) {
        static const std::regex block(R"re((^|\n)[ \t]*project[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*\{[^\n]*\n)re");
        static const std::regex existing(R"re((^|\n)([ \t]*one[ \t]+)"[^"\n]*")re");
        static const std::regex setting(R"re(^([ \t]+)([a-z_]+)([ \t]+)\S)re");
        for (auto& [path, text] : files) {
            std::smatch found;
            if (!std::regex_search(text, found, block)) continue;
            std::size_t start = static_cast<std::size_t>(found.position(0) + found.length(0));
            std::size_t end = text.find("\n}", start);
            std::string inside = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            std::smatch line;
            if (std::regex_search(inside, line, existing)) {
                std::size_t at = start + static_cast<std::size_t>(line.position(2) + line.length(2));
                std::size_t close = text.find('"', at + 1);
                text.replace(at, close + 1 - at, "\"" + std::string(version) + "\"");
                return true;
            }
            std::string first = inside.substr(0, inside.find('\n'));
            std::smatch beside;
            std::string indent = "\t", gap = "  ";
            if (std::regex_search(first, beside, setting)) {
                indent = beside[1];
                std::size_t width = beside[2].length() + beside[3].length();
                gap = std::string(width > 3 ? width - 3 : 2, ' ');
            }
            text.insert(start, indent + "one" + gap + "\"" + std::string(version) + "\"\n");
            return true;
        }
        return false;
    }

    // A project block whose settings' values were lined up stays lined up when an
    // upgrade renames a setting, like authentication to signin: its values stay in
    // their column, or move out to the one the longest name needs, and no further.
    inline std::string align_settings(const std::string& before, const std::string& after) {
        static const std::regex opens(R"(^[ \t]*project[ \t]+\w+[ \t]*\{[ \t]*$)");
        static const std::regex setting(R"(^([ \t]+)([a-z_]+)([ \t]+)([^ \t{][^{]*)$)");
        auto lines_of = [](const std::string& text) {
            std::vector<std::string> lines;
            std::size_t at = 0;
            for (std::size_t end; (end = text.find('\n', at)) != std::string::npos; at = end + 1) lines.push_back(text.substr(at, end - at));
            lines.push_back(text.substr(at));
            return lines;
        };
        // The settings directly in the project block, by line, as indent, name and value.
        struct line_setting {
            std::size_t line;
            std::string indent, key, value;
            std::size_t column;
        };
        auto settings_of = [&](const std::vector<std::string>& lines) {
            std::vector<line_setting> found;
            std::size_t i = 0;
            while (i < lines.size() && !std::regex_match(lines[i], opens)) ++i;
            std::string indent;
            for (++i; i < lines.size() && !lines[i].starts_with("}"); ++i) {
                std::smatch m;
                if (!std::regex_match(lines[i], m, setting)) continue;
                if (indent.empty()) indent = m[1];
                if (m[1] != indent) continue;  // inside an environment
                found.push_back({i, m[1], m[2], m[4], static_cast<std::size_t>(m[1].length() + m[2].length() + m[3].length())});
            }
            return found;
        };
        auto lines = lines_of(after);
        auto was = settings_of(lines_of(before));
        auto now = settings_of(lines);
        if (was.size() < 2 || was.size() != now.size()) return after;
        std::size_t column = was[0].column;
        for (const auto& s : was) {
            if (s.column != column) return after;  // not lined up to begin with
        }
        // A name made longer moves the column out; one made shorter stays in it.
        std::size_t needed = column;
        for (const auto& s : now) needed = std::max(needed, s.indent.size() + s.key.size() + 2);
        if (std::all_of(now.begin(), now.end(), [&](const line_setting& s) { return s.column == needed; })) return after;
        for (const auto& s : now) lines[s.line] = s.indent + s.key + std::string(needed - s.indent.size() - s.key.size(), ' ') + s.value;
        std::string out;
        for (std::size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
        return out;
    }

    // An entity's choices written as an enum by an upgrade start in the column the
    // block's rules do, as they'd have been written, so the line still reads as name,
    // type and rules:
    //     person   user     required  key
    //     role     enum     maintainer | reporter = role::reporter
    // Only lines that became enums are moved; the rules' column is the one most fields
    // whose type starts where the enum's does use.
    inline std::string align_choices(const std::string& before, const std::string& after) {
        auto split = [](const std::string& text) {
            std::vector<std::string> lines;
            std::size_t at = 0;
            for (std::size_t end; (end = text.find('\n', at)) != std::string::npos; at = end + 1) lines.push_back(text.substr(at, end - at));
            lines.push_back(text.substr(at));
            return lines;
        };
        auto was = split(before);
        auto lines = split(after);
        if (was.size() != lines.size()) return after;  // fixes never add lines
        static const std::regex enum_field(R"(^(\s+\w+\s+)enum\s+(.*)$)");
        static const std::regex field(R"(^(\s+\w+\s+)(list of \w+|[\w:]+)(\s+)\S.*$)");
        static const std::regex opens(R"(^\s*entity\s+\w+.*\{\s*$)");
        static const std::regex closes(R"(^\s*\}.*$)");
        for (std::size_t i = 0; i < lines.size(); ++i) {
            std::smatch m;
            if (!std::regex_match(lines[i], m, enum_field) || std::regex_search(was[i], std::regex(R"(\benum\b)"))) continue;
            std::string head = m[1], rest = m[2];
            std::size_t first = i, last = i;
            while (first > 0 && !std::regex_match(lines[first], opens)) --first;
            while (last + 1 < lines.size() && !std::regex_match(lines[last], closes)) ++last;
            std::map<std::size_t, int> rules;  // where rules start, among fields typed in the enum's column
            for (std::size_t k = first + 1; k < last; ++k) {
                std::smatch f;
                if (k == i || !std::regex_match(lines[k], f, field) || f[2] == "enum" || static_cast<std::size_t>(f[1].length()) != head.size()) continue;
                ++rules[head.size() + static_cast<std::size_t>(f[2].length() + f[3].length())];
            }
            std::size_t column = head.size() + 6;  // "enum" and two spaces, with nothing to line up with
            int most = 0;
            for (const auto& [at, count] : rules) {
                if (count > most) most = count, column = std::max(at, head.size() + 6);
            }
            lines[i] = head + "enum" + std::string(column - head.size() - 4, ' ') + rest;
        }
        std::string out;
        for (std::size_t i = 0; i < lines.size(); ++i) out += (i ? "\n" : "") + lines[i];
        return out;
    }

    // Moves who a site is by, once a setting, copyright "Ada" "https://...", into a
    // footer below the project block, with what the site was built from, as the
    // setting showed. How many it moved.
    inline std::size_t move_copyright(sources& files) {
        static const std::regex setting(R"re((^|\n)[ \t]+copyright[ \t]+"([^"\n]*)"(?:[ \t]+"([^"\n]*)")?[ \t]*(?=\n|$))re");
        static const std::regex block(R"re((^|\n)project[ \t]+\w+[ \t]*\{)re");
        std::size_t moved = 0;
        for (auto& [path, text] : files) {
            std::smatch opens;
            if (!std::regex_search(text, opens, block)) continue;
            std::size_t start = static_cast<std::size_t>(opens.position(0) + opens.length(0));
            std::size_t end = text.find("\n}", start);
            if (end == std::string::npos) continue;
            std::string inside = text.substr(start, end - start);
            std::smatch found;
            if (!std::regex_search(inside, found, setting)) continue;
            std::string name = found[2], link = found[3];
            std::string by = link.empty() ? name : "[" + name + "](" + link + ")";
            std::string footer = "\n\nfooter {\n\ttext \"© {year} " + by + "\"\n\tlink build.release \"uione {build.version}\"\n\tlink build.source \"{build.commit}\"\n}";
            std::size_t close = end + 2;  // just past the block's }
            text.insert(close, footer);
            text.erase(start + static_cast<std::size_t>(found.position(0)) + found[1].length(),
                       static_cast<std::size_t>(found.length(0) - found[1].length()) + 1);
            ++moved;
        }
        return moved;
    }

    // Joins a form to the button right above it that opens it, issue::update "Edit"
    // and form issue::update "Save" { … }, as issue::update "Edit" form "Save" { … }.
    // A form several buttons open, or one apart from its button, stays as it is. How
    // many it joined.
    inline std::size_t join_forms(sources& files) {
        static const std::regex pair(
            R"re((^|\n)([ \t]+)(\w+(?:::\w+)+)((?:[ \t]+"[^"\n]*")?(?:[ \t]+icon[ \t]+\w+)?)[ \t]*\n\2form[ \t]+\3((?:[ \t]+"[^"\n]*")?)[ \t]*\{)re");
        std::size_t joined = 0;
        for (auto& [path, text] : files) {
            std::string out;
            auto from = text.cbegin();
            for (std::sregex_iterator it(text.begin(), text.end(), pair), end; it != end; ++it) {
                const auto& m = *it;
                out.append(from, m[0].first);
                out += m[1].str() + m[2].str() + m[3].str() + m[4].str() + " form" + m[5].str() + " {";
                from = m[0].second;
                ++joined;
            }
            out.append(from, text.cend());
            text = std::move(out);
        }
        return joined;
    }

    // Moves what's new to a person, once a setting, unread news.changes since
    // news.seen, into a value of their view, unread = count(changes where created_at >
    // seen), and a header below the project block that shows it, header { badge
    // news.unread }. How many it moved.
    inline std::size_t move_unread(sources& files) {
        static const std::regex setting(R"re((^|\n)[ \t]+unread[ \t]+(\w+)\.(\w+)[ \t]+since[ \t]+(\w+)\.(\w+)[ \t]*(?=\n|$))re");
        static const std::regex block(R"re((^|\n)project[ \t]+\w+[ \t]*\{)re");
        std::size_t moved = 0;
        for (auto& [path, text] : files) {
            std::smatch opens;
            if (!std::regex_search(text, opens, block)) continue;
            std::size_t start = static_cast<std::size_t>(opens.position(0) + opens.length(0));
            std::size_t end = text.find("\n}", start);
            if (end == std::string::npos) continue;
            std::string inside = text.substr(start, end - start);
            std::smatch found;
            if (!std::regex_search(inside, found, setting)) continue;
            std::string view = found[2], list = found[3], since = found[5];
            // The view's closing brace, past its strings and comments, in whichever file
            // declares it.
            const std::regex declared("(^|\n)([ \t]*)view[ \t]+" + view + "\\b[^\n{]*\\{");
            std::string* home = nullptr;
            std::size_t close = std::string::npos;
            std::string indent;
            for (auto& [other, source] : files) {
                std::smatch v;
                if (!std::regex_search(source, v, declared)) continue;
                int depth = 1;
                for (std::size_t i = static_cast<std::size_t>(v.position(0) + v.length(0)); i < source.size(); ++i) {
                    char c = source[i];
                    if (c == '"') {
                        while (++i < source.size() && source[i] != '"' && source[i] != '\n') {
                            if (source[i] == '\\') ++i;
                        }
                    } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
                        i = source.find('\n', i);
                        if (i == std::string::npos) break;
                    } else if (c == '{') {
                        ++depth;
                    } else if (c == '}' && --depth == 0) {
                        close = i;
                        break;
                    }
                }
                if (close != std::string::npos) {
                    home = &source;
                    indent = v[2];
                }
                break;
            }
            if (!home) continue;
            // Its own line, after what the view says, at the start of the brace's line.
            std::size_t line = home->rfind('\n', close);
            line = line == std::string::npos ? 0 : line + 1;
            home->insert(line, indent + "\tunread = count(" + list + " where created_at > " + since + ")\n");
            // text may be the same file as the view's, so it's found again.
            std::regex_search(text, opens, block);
            start = static_cast<std::size_t>(opens.position(0) + opens.length(0));
            end = text.find("\n}", start);
            inside = text.substr(start, end - start);
            std::regex_search(inside, found, setting);
            text.insert(end + 2, "\n\nheader {\n\tbadge " + view + ".unread\n}");
            text.erase(start + static_cast<std::size_t>(found.position(0)) + found[1].length(),
                       static_cast<std::size_t>(found.length(0) - found[1].length()) + 1);
            ++moved;
        }
        return moved;
    }

    // A person's picture with no label, then their name, as a table, a board or cards
    // shows them, is one column of theirs: author.picture "" author.name "Opened by"
    // is author "Opened by", on one line or two. A view's own rows say both still.
    inline std::size_t join_people(sources& files) {
        static const std::regex lines(R"re(([ \t]*)(\w+)\.picture[ \t]+""[ \t]*\n\1\2\.name\b)re");
        static const std::regex beside(R"re(\b(\w+)\.picture[ \t]+""[ \t]+\1\.name\b)re");
        std::size_t joined = 0;
        for (auto& [path, text] : files) {
            for (const auto* pattern : {&lines, &beside}) {
                std::string out;
                auto from = text.cbegin();
                for (std::sregex_iterator it(text.begin(), text.end(), *pattern), end; it != end; ++it) {
                    const auto& m = *it;
                    out.append(from, m[0].first);
                    out += pattern == &lines ? m[1].str() + m[2].str() : m[1].str();
                    from = m[0].second;
                    ++joined;
                }
                out.append(from, text.cend());
                text = std::move(out);
            }
        }
        return joined;
    }

    // A screen titled only with the site's own title, which an untitled screen shows
    // anyway, says nothing: screen "uione" /:owner/:project/theme is screen
    // /:owner/:project/theme.
    inline std::size_t drop_site_titles(sources& files) {
        static const std::regex project(R"re((^|\n)[ \t]*project[ \t]+\w+[ \t]*\{[^}]*?\n[ \t]+title[ \t]+"([^"\n]*)")re");
        std::string site;
        for (const auto& [path, text] : files) {
            std::smatch m;
            if (std::regex_search(text, m, project)) site = m[2];
        }
        if (site.empty()) return 0;
        const std::regex titled("(^|\n)([ \t]*)screen[ \t]+\"" + std::regex_replace(site, std::regex(R"([.^$|()\[\]{}*+?\\])"), R"(\$&)") + "\"[ \t]+/");
        std::size_t dropped = 0;
        for (auto& [path, text] : files) {
            std::string out;
            auto from = text.cbegin();
            for (std::sregex_iterator it(text.begin(), text.end(), titled), end; it != end; ++it) {
                const auto& m = *it;
                out.append(from, m[0].first);
                out += m[1].str() + m[2].str() + "screen /";
                from = m[0].second;
                ++dropped;
            }
            out.append(from, text.cend());
            text = std::move(out);
        }
        return dropped;
    }

    // Screens in one file that say the same, titled alike with the same items, are one
    // screen at all their addresses: the first keeps its place, the others' addresses
    // join it on lines of their own, and their comments go above it after its own.
    inline std::size_t join_screens(sources& files) {
        static const std::regex opens(R"re(^([ \t]*)screen((?:[ \t]+"[^"\n]*"|[ \t]+\w+)?)((?:[ \t]+/\S*)+)((?:[ \t]+landing[ \t]+\w+)?)[ \t]*\{[ \t]*$)re");
        std::size_t joined = 0;
        for (auto& [path, text] : files) {
            std::vector<std::string> lines;
            std::size_t at = 0;
            for (std::size_t end; (end = text.find('\n', at)) != std::string::npos; at = end + 1) lines.push_back(text.substr(at, end - at));
            lines.push_back(text.substr(at));
            struct block {
                std::size_t comments, open, close;
                std::string indent, title, routes, landing, body;
            };
            std::vector<block> blocks;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                std::smatch m;
                if (!std::regex_match(lines[i], m, opens)) continue;
                block b{i, i, i, m[1], m[2], m[3], m[4], ""};
                std::size_t j = i + 1;
                while (j < lines.size() && lines[j] != b.indent + "}") b.body += lines[j++] + "\n";
                if (j == lines.size()) continue;
                b.close = j;
                while (b.comments > 0 && lines[b.comments - 1].starts_with(b.indent + "//")) --b.comments;
                blocks.push_back(std::move(b));
                i = j;
            }
            std::vector<bool> gone(lines.size(), false);
            std::map<std::size_t, std::vector<std::string>> more_routes, more_comments;
            for (std::size_t a = 0; a < blocks.size(); ++a) {
                if (gone[blocks[a].open]) continue;
                for (std::size_t b = a + 1; b < blocks.size(); ++b) {
                    const auto &x = blocks[a], &y = blocks[b];
                    if (gone[y.open] || x.indent != y.indent || x.title != y.title || x.landing != y.landing || x.body != y.body) continue;
                    std::istringstream routes(y.routes);
                    for (std::string route; routes >> route;) more_routes[a].push_back(route);
                    for (std::size_t c = y.comments; c < y.open; ++c) more_comments[a].push_back(lines[c]);
                    for (std::size_t c = y.comments; c <= y.close; ++c) gone[c] = true;
                    if (y.comments > 0 && lines[y.comments - 1].find_first_not_of(" \t") == std::string::npos) gone[y.comments - 1] = true;
                    ++joined;
                }
            }
            if (more_routes.empty()) continue;
            std::string out;
            auto put = [&](const std::string& line) { out += line + "\n"; };
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (gone[i]) continue;
                auto owner = std::find_if(blocks.begin(), blocks.end(), [&](const block& b) { return b.open == i; });
                if (owner != blocks.end() && more_routes.contains(static_cast<std::size_t>(owner - blocks.begin()))) {
                    std::size_t index = static_cast<std::size_t>(owner - blocks.begin());
                    for (const auto& comment : more_comments[index]) put(comment);
                    std::string head = lines[i].substr(0, lines[i].rfind('{'));
                    while (!head.empty() && (head.back() == ' ' || head.back() == '\t')) head.pop_back();
                    // The landing, if it has one, stays after the last address.
                    std::string landing = owner->landing;
                    if (!landing.empty()) head = head.substr(0, head.size() - landing.size());
                    out += head;
                    for (const auto& route : more_routes[index]) out += "\n" + owner->indent + "\t" + route;
                    out += landing + " {\n";
                    continue;
                }
                put(lines[i]);
            }
            out.pop_back();  // the last line had no newline after it
            text = std::move(out);
        }
        return joined;
    }

    upgraded upgrade(const std::string& root, std::string_view version) {
        upgraded out;
        auto files = read_sources(root, out.problems);
        if (!out.problems.empty()) return out;
        auto original = files;
        out.fixes += move_copyright(files);
        out.fixes += move_unread(files);
        out.fixes += join_forms(files);
        out.fixes += join_people(files);
        for (int round = 0; round < 100; ++round) {
            language::diagnostics found;
            check_sources(files, found);
            std::size_t applied = apply_fixes(files, found);
            out.fixes += applied;
            if (applied == 0) {
                out.problems = std::move(found);
                break;
            }
        }
        if (!out.problems.empty()) return out;
        out.fixes += drop_site_titles(files);
        out.fixes += join_screens(files);
        for (auto& [path, text] : files) {
            // Lined up against the file as it was, with the import its fix added, so the
            // lines still pair up.
            std::string before = original[path];
            const std::string import = "import one\n\n";
            if (std::size_t at = text.starts_with(import) ? 0 : text.find("\n" + import);
                at != std::string::npos && before.find(import) == std::string::npos) {
                if (at != 0) ++at;
                std::size_t line = static_cast<std::size_t>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(at), '\n'));
                std::size_t offset = 0;
                for (std::size_t n = 0; n < line && offset != std::string::npos; ++n) {
                    offset = before.find('\n', offset);
                    if (offset != std::string::npos) ++offset;
                }
                if (offset != std::string::npos) before.insert(offset, import);
            }
            text = align_settings(before, text);
            text = align_choices(before, text);
        }
        // Only its own files: those of a project it extends are that project's, and
        // say the version that one is for.
        auto own = platform::find_one_files({root});
        sources mine;
        for (const auto& path : own) {
            if (files.contains(path)) mine[path] = files[path];
        }
        out.recorded = record_version(mine, version);
        for (auto& [path, text] : mine) files[path] = std::move(text);
        language::diagnostics after;
        check_sources(files, after);
        if (!after.empty()) {
            out.problems = std::move(after);
            out.recorded = false;
            return out;
        }
        for (const auto& [path, text] : files) {
            if (original[path] != text && std::find(own.begin(), own.end(), path) != own.end()) out.changed[path] = text;
        }
        return out;
    }

} // namespace one::driver
