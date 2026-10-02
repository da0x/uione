// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Writes code a line at a time, keeping track of indentation, so a generator reads
// like the code it produces:
//
//     out.open("export const home = screen(info, () => (")
//        .line("<Text>hello</Text>")
//        .close("));");

#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace one::code {

    // Where a line of generated code came from: a line of a .one file, or, with line
    // 0, nowhere in particular, like an import every such file has.
    struct source {
        std::string path;
        int line = 0;

        bool fixed() const { return line == 0; }
    };

    class stream {
    public:
        explicit stream(std::string indent = "  ") : indent_(std::move(indent)) {}

        // A line at the current indentation. An empty line stays empty.
        stream& line(std::string_view text = "") { return write(text, current_); }

        stream& open(std::string_view text) {
            line(text);
            ++depth_;
            return *this;
        }

        stream& close(std::string_view text) {
            if (depth_ > 0) --depth_;
            return line(text);
        }

        // Steps back one indentation level without writing anything, for code whose
        // last line is at a deeper level than what follows it.
        stream& dedent() {
            if (depth_ > 0) --depth_;
            return *this;
        }

        // The .one line what's written next comes from, until the guard it returns
        // goes out of scope, when the source before it comes back. So a section's
        // closing tag belongs to the section, not to the last line inside it.
        class from_guard {
        public:
            from_guard(stream& s, std::optional<source> previous) : stream_(&s), previous_(std::move(previous)) {}
            from_guard(const from_guard&) = delete;
            from_guard& operator=(const from_guard&) = delete;
            ~from_guard() { stream_->current_ = previous_; }

        private:
            stream* stream_;
            std::optional<source> previous_;
        };

        [[nodiscard]] from_guard from(const std::string& path, int line) {
            std::optional<source> previous = current_;
            current_ = source{path, line};
            return from_guard(*this, std::move(previous));
        }

        // What's written next belongs to no particular line, like imports.
        [[nodiscard]] from_guard fixed() { return from("", 0); }

        // Writes another stream's lines here, one step deeper for each step this one
        // is in, keeping where each came from.
        stream& embed(const stream& other) {
            std::size_t at = 0, n = 0;
            while (at < other.out_.size()) {
                std::size_t end = other.out_.find('\n', at);
                std::string_view text = std::string_view(other.out_).substr(at, end - at);
                write(text, n < other.sources_.size() ? other.sources_[n] : std::nullopt);
                at = end + 1;
                ++n;
            }
            return *this;
        }

        // Takes back a blank last line, so a file doesn't end with two line breaks.
        stream& drop_trailing_blank() {
            if (out_.ends_with("\n\n")) {
                out_.pop_back();
                sources_.pop_back();
            }
            return *this;
        }

                // Where each line came from, in order; none for a line nobody said.
        const std::vector<std::optional<source>>& sources() const { return sources_; }

        // The header every generated file starts with. Generated code belongs to
        // whoever generated it, so it carries no copyright of ours.
        stream& generated_from(std::string_view from) {
            auto guard = fixed();
            return line("// Generated from " + std::string(from) + " by one. Do not edit.").line();
        }

        const std::string& str() const { return out_; }

    private:
        stream& write(std::string_view text, const std::optional<source>& from) {
            if (!text.empty()) {
                for (int i = 0; i < depth_; ++i) out_ += indent_;
                out_ += text;
            }
            out_ += '\n';
            // A line given with line breaks in it is several lines, each from the same place.
            std::size_t lines = 1 + static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
            sources_.insert(sources_.end(), lines, from);
            return *this;
        }

        std::string indent_;
        std::string out_;
        int depth_ = 0;
        std::optional<source> current_;
        std::vector<std::optional<source>> sources_;
    };

} // namespace one::code
