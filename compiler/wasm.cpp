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
    answer += ",\"files\":" + std::to_string(checked.files) + "}";
    return answer.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* one_build(const char* project, const char* out) {
    auto built = one::driver::build(project, out);
    answer = "{";
    problems(answer, built.problems);
    answer += ",\"refusal\":";
    text(answer, built.refusal);
    answer += ",\"note\":";
    text(answer, built.note);
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
