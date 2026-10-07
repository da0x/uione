// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one, built for the browser. The page puts a project's files in the module's
// in-memory file system, calls one_check, one_build or one_show, and reads back
// JSON: the problems found, and for a build every generated file with the .one
// line each of its lines came from. The answer stays valid until the next call.

#include <emscripten/emscripten.h>

#include <string>
#include <vector>

#include "driver.hpp"
#include "version.hpp"

namespace {

    std::string answer;

    void text(std::string& out, std::string_view value) {
        out += '"';
        for (unsigned char c : value) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        static const char hex[] = "0123456789abcdef";
                        out += "\\u00";
                        out += hex[c >> 4];
                        out += hex[c & 0xf];
                    } else {
                        out += static_cast<char>(c);
                    }
            }
        }
        out += '"';
    }

    void problems(std::string& out, const one::language::diagnostics& found) {
        out += "\"problems\":[";
        for (std::size_t i = 0; i < found.size(); ++i) {
            const auto& d = found[i];
            out += i ? ",{" : "{";
            out += "\"path\":";
            text(out, d.path);
            out += ",\"line\":" + std::to_string(d.where.line) + ",\"column\":" + std::to_string(d.where.column) + ",\"message\":";
            text(out, d.message);
            out += ",\"text\":";
            text(out, one::language::format(d));
            out += "}";
        }
        out += "]";
    }

    // A generated line's source: the .one file and line it came from, "same" for a
    // line every project gets, or null for a blank line.
    void source(std::string& out, const std::optional<one::code::source>& s) {
        if (!s) {
            out += "null";
        } else if (s->fixed()) {
            out += "\"same\"";
        } else {
            out += "{\"path\":";
            text(out, s->path);
            out += ",\"line\":" + std::to_string(s->line) + "}";
        }
    }

    void settings(std::string& out, const std::vector<one::driver::outline::setting>& list) {
        out += "[";
        for (std::size_t i = 0; i < list.size(); ++i) {
            out += i ? ",{\"key\":" : "{\"key\":";
            text(out, list[i].key);
            out += ",\"value\":";
            text(out, list[i].value);
            out += ",\"line\":" + std::to_string(list[i].line) + "}";
        }
        out += "]";
    }

    // The project block's outline, or null when there's none.
    void outline(std::string& out, const std::optional<one::driver::outline>& o) {
        if (!o) {
            out += "null";
            return;
        }
        out += "{\"path\":";
        text(out, o->path);
        out += ",\"name\":";
        text(out, o->name);
        out += ",\"line\":" + std::to_string(o->line) + ",\"end\":" + std::to_string(o->end) + ",\"settings\":";
        settings(out, o->settings);
        out += ",\"environments\":[";
        for (std::size_t i = 0; i < o->environments.size(); ++i) {
            const auto& e = o->environments[i];
            out += i ? ",{\"name\":" : "{\"name\":";
            text(out, e.name);
            out += ",\"line\":" + std::to_string(e.line) + ",\"end\":" + std::to_string(e.end) + ",\"settings\":";
            settings(out, e.settings);
            out += "}";
        }
        out += "]}";
    }

    void item(std::string& out, const one::driver::outlined_item& i) {
        out += "{\"kind\":";
        text(out, i.kind);
        out += ",\"subject\":";
        text(out, i.subject);
        out += ",\"label\":";
        text(out, i.label);
        out += ",\"line\":" + std::to_string(i.line) + ",\"items\":[";
        for (std::size_t n = 0; n < i.items.size(); ++n) {
            if (n) out += ",";
            item(out, i.items[n]);
        }
        out += "]}";
    }

    // Each screen's layout and items, by line.
    void screens(std::string& out, const std::vector<one::driver::outlined_screen>& list) {
        out += "[";
        for (std::size_t n = 0; n < list.size(); ++n) {
            const auto& s = list[n];
            out += n ? ",{\"path\":" : "{\"path\":";
            text(out, s.path);
            out += ",\"title\":";
            text(out, s.title);
            out += ",\"route\":";
            text(out, s.route);
            out += ",\"line\":" + std::to_string(s.line) + ",\"layout\":";
            text(out, s.layout);
            out += ",\"layoutLine\":" + std::to_string(s.layout_line) + ",\"items\":[";
            for (std::size_t i = 0; i < s.items.size(); ++i) {
                if (i) out += ",";
                item(out, s.items[i]);
            }
            out += "]}";
        }
        out += "]";
    }

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE const char* one_version() {
    answer = std::string(one::version);
    return answer.c_str();
}

// project is a folder in the module's file system, like /project.
EMSCRIPTEN_KEEPALIVE const char* one_check(const char* project) {
    auto checked = one::driver::check({project});
    answer = "{";
    problems(answer, checked.problems);
    answer += ",\"files\":" + std::to_string(checked.files) + ",\"project\":";
    outline(answer, checked.project);
    answer += ",\"screens\":";
    screens(answer, checked.screens);
    answer += "}";
    return answer.c_str();
}

// environment names the one to build for, or is empty for the first.
EMSCRIPTEN_KEEPALIVE const char* one_build(const char* project, const char* out, const char* environment) {
    auto built = one::driver::build(project, out, environment);
    answer = "{";
    problems(answer, built.problems);
    answer += ",\"refusal\":";
    text(answer, built.refusal);
    answer += ",\"note\":";
    text(answer, built.note);
    answer += ",\"environment\":";
    text(answer, built.environment);
    answer += ",\"files\":[";
    for (std::size_t i = 0; i < built.files.size(); ++i) {
        const auto& f = built.files[i];
        answer += i ? ",{" : "{";
        answer += "\"path\":";
        text(answer, f.path);
        answer += ",\"content\":";
        text(answer, f.content);
        answer += f.executable ? ",\"executable\":true" : ",\"executable\":false";
        answer += ",\"sources\":[";
        for (std::size_t n = 0; n < f.sources.size(); ++n) {
            if (n) answer += ",";
            source(answer, f.sources[n]);
        }
        answer += "]}";
    }
    answer += "]}";
    return answer.c_str();
}

// What the name at a line and column of a file means: {"found":false}, or what it
// says, and where it's declared, or the reference's section for the language's own.
EMSCRIPTEN_KEEPALIVE const char* one_define(const char* project, const char* path, int line, int column) {
    auto d = one::driver::define(std::string(project), std::string(path), line, column);
    if (!d.found) {
        answer = "{\"found\":false}";
        return answer.c_str();
    }
    answer = "{\"found\":true,\"says\":";
    text(answer, d.says);
    answer += ",\"path\":";
    text(answer, d.path);
    answer += ",\"line\":" + std::to_string(d.line) + ",\"column\":" + std::to_string(d.column) + ",\"section\":";
    text(answer, d.section);
    answer += ",\"from\":" + std::to_string(d.from) + ",\"to\":" + std::to_string(d.to) + "}";
    return answer.c_str();
}

// where is file:line or file:line-line, the way `one show` takes it.
EMSCRIPTEN_KEEPALIVE const char* one_show(const char* where) {
    auto shown = one::driver::show(where);
    answer = "{";
    problems(answer, shown.problems);
    answer += shown.understood ? ",\"understood\":true" : ",\"understood\":false";
    answer += ",\"files\":[";
    for (std::size_t i = 0; i < shown.files.size(); ++i) {
        answer += i ? ",{" : "{";
        answer += "\"path\":";
        text(answer, shown.files[i].path);
        answer += ",\"lines\":[";
        for (std::size_t n = 0; n < shown.files[i].lines.size(); ++n) {
            const auto& [number, line] = shown.files[i].lines[n];
            answer += n ? ",[" : "[";
            answer += std::to_string(number) + ",";
            text(answer, line);
            answer += "]";
        }
        answer += "]}";
    }
    answer += "]}";
    return answer.c_str();
}

}
