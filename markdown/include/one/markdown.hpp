// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: GPL-3.0-only

// one::markdown: lay Markdown out for a terminal, or read it as a tree for a window.
// It began as libmarkdown, whose history this directory keeps.
//
// One header, everything inline, no third-party libraries. `layout()` parses
// Markdown and lays it out as rows of styled runs: already wrapped to a width,
// code already highlighted, and no escape codes anywhere in the text. Drawing
// is left to the caller. `ansi()` is one way to draw — it produces exactly what
// the `md` tool prints — and a curses program can map each run's style onto its
// own attributes instead.
//
//     one::markdown::options settings{.width = 72};
//     for (const one::markdown::line& row : markdown::layout(source, settings))
//         for (const one::markdown::run& piece : row)
//             draw(piece.text, piece.look);  // bold, colors, link, …
#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace one::markdown {

// ---- The layout ----------------------------------------------------------

// The 16-color terminal palette. `none` means "the backend's default".
enum class color {
    none,
    black,
    red,
    green,
    yellow,
    blue,
    magenta,
    cyan,
    white,
    bright_black,
    bright_red,
    bright_green,
    bright_yellow,
    bright_blue,
    bright_magenta,
    bright_cyan,
    bright_white,
};

// How one run of text looks. A run with a `link` is a hyperlink to that URL.
struct style {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool dim = false;
    color foreground = color::none;
    color background = color::none;
    std::string link{};

    // No visual attribute is set. A link alone doesn't count: it changes where
    // the text points, not how it looks.
    bool plain() const {
        return !bold && !italic && !underline && !strike && !dim &&
               foreground == color::none && background == color::none;
    }
};

// A span of UTF-8 text in one style. The text never holds escape codes or
// newlines.
struct run {
    std::string text{};
    style look{};
    // A table cell too wide for its column was cut here; the text ends in "…".
    // Only `ansi()` cares, to reproduce md's exact bytes.
    bool truncated = false;
};

// One terminal row, already wrapped.
using line = std::vector<run>;

struct options {
    int width = 80;          // columns to wrap to
    bool hyperlinks = true;  // false: links can't be clicked, so write the URL
                             // after the link text, as "text (url)"
};

// Parse `source` and lay it out into rows no wider than `settings.width`.
// Fenced code is never wrapped and table columns never shrink below three
// columns, so those two can overflow a very narrow width.
std::vector<line> layout(std::string_view source, const options& settings);

// Draw rows as ANSI escape-coded text, rows separated by "\n" with no trailing
// newline. `colored` false gives the bare text. This is what `md` prints.
std::string ansi(const std::vector<line>& rows, bool colored);
std::string ansi(const line& row, bool colored);

// ---- Unicode -------------------------------------------------------------
//
// The olog/catcsv table code measured with byte length, so any non-ASCII cell
// misaligned. Here `display_width` counts terminal columns — wide (CJK/emoji)
// code points count as 2, combining and zero-width marks as 0.

// Decode the UTF-8 code point starting at `text[i]`, advancing `i` past it.
// Invalid bytes decode as U+FFFD and advance by one, so this never loops.
inline char32_t decode_utf8(std::string_view text, std::size_t& i) {
    auto first = static_cast<unsigned char>(text[i]);
    auto byte = [&](std::size_t k) {
        return static_cast<unsigned char>(text[k]);
    };
    auto continues = [&](std::size_t k) -> bool {
        return k < text.size() && (byte(k) & 0xC0) == 0x80;
    };
    if (first < 0x80) {
        ++i;
        return first;
    }
    if ((first & 0xE0) == 0xC0 && continues(i + 1)) {
        char32_t point = ((first & 0x1F) << 6) | (byte(i + 1) & 0x3F);
        i += 2;
        return point;
    }
    if ((first & 0xF0) == 0xE0 && continues(i + 1) && continues(i + 2)) {
        char32_t point = ((first & 0x0F) << 12) | ((byte(i + 1) & 0x3F) << 6) |
                         (byte(i + 2) & 0x3F);
        i += 3;
        return point;
    }
    if ((first & 0xF8) == 0xF0 && continues(i + 1) && continues(i + 2) &&
        continues(i + 3)) {
        char32_t point = ((first & 0x07) << 18) | ((byte(i + 1) & 0x3F) << 12) |
                         ((byte(i + 2) & 0x3F) << 6) | (byte(i + 3) & 0x3F);
        i += 4;
        return point;
    }
    ++i;
    return 0xFFFD;
}

namespace detail {

inline bool in_range(char32_t point, char32_t low, char32_t high) {
    return point >= low && point <= high;
}

// Zero-width: combining marks, ZWJ/ZWNJ, variation selectors, etc.
inline bool is_zero_width(char32_t point) {
    if (point == 0) return true;
    return in_range(point, 0x0300, 0x036F) ||  // combining diacritical marks
           in_range(point, 0x1AB0, 0x1AFF) ||
           in_range(point, 0x1DC0, 0x1DFF) ||
           in_range(point, 0x20D0, 0x20FF) ||
           in_range(point, 0xFE00, 0xFE0F) ||  // variation selectors
           in_range(point, 0xFE20, 0xFE2F) ||
           point == 0x200B || point == 0x200C || point == 0x200D ||
           point == 0xFEFF;
}

// Wide (2-column) code points: the common CJK, Hangul, and emoji blocks.
inline bool is_wide(char32_t point) {
    return in_range(point, 0x1100, 0x115F) ||   // Hangul Jamo
           in_range(point, 0x2E80, 0x303E) ||   // CJK radicals, Kangxi
           in_range(point, 0x3041, 0x33FF) ||   // Hiragana..CJK symbols
           in_range(point, 0x3400, 0x4DBF) ||   // CJK Ext A
           in_range(point, 0x4E00, 0x9FFF) ||   // CJK Unified
           in_range(point, 0xA000, 0xA4CF) ||   // Yi
           in_range(point, 0xAC00, 0xD7A3) ||   // Hangul syllables
           in_range(point, 0xF900, 0xFAFF) ||   // CJK compatibility
           in_range(point, 0xFE30, 0xFE4F) ||   // CJK compat forms
           in_range(point, 0xFF00, 0xFF60) ||   // fullwidth forms
           in_range(point, 0xFFE0, 0xFFE6) ||
           in_range(point, 0x1F300, 0x1FAFF) || // emoji & pictographs
           in_range(point, 0x20000, 0x3FFFD);   // CJK Ext B+
}

}  // namespace detail

// Terminal columns occupied by one code point.
inline int char_width(char32_t point) {
    if (detail::is_zero_width(point)) return 0;
    if (detail::is_wide(point)) return 2;
    return 1;
}

// Total terminal display width of a UTF-8 string.
inline int display_width(std::string_view text) {
    int width = 0;
    std::size_t i = 0;
    while (i < text.size()) width += char_width(decode_utf8(text, i));
    return width;
}

// Display width of a laid-out row.
inline int display_width(const line& row) {
    int width = 0;
    for (const run& piece : row) width += display_width(piece.text);
    return width;
}

namespace detail {

constexpr std::size_t not_found = std::string_view::npos;

// ---- The document tree ----------------------------------------------------
//
// Inline content is a flat-ish tree: styled spans may nest (e.g. bold inside a
// link). `text` and `code` are leaves carrying literal text; the styling kinds
// carry `children`; `link`/`image` additionally carry `url`.
enum class inline_kind {
    text,
    code,
    emphasis,         // *italic*
    strong,           // **bold**
    strong_emphasis,  // ***bold italic***
    strikethrough,    // ~~strike~~
    link,             // [text](url)
    image,            // ![alt](url)
};

struct inline_node {
    inline_kind kind;
    std::string text;                  // literal, for text/code
    std::string url;                   // for link/image
    std::vector<inline_node> children; // for styled spans, link, image
};

enum class block_kind {
    document,
    heading,
    paragraph,
    code_block,
    block_quote,
    list,
    list_item,
    thematic_break,
    table,
};

enum class alignment { left, center, right };

struct block_node {
    block_kind kind;

    // heading
    int level = 0;

    // list / list_item
    bool ordered = false;   // list: ordered vs bullet
    int start = 1;          // list: first ordinal
    int task_state = -1;    // list item: -1 none, 0 unchecked, 1 checked

    // code_block
    std::string info{};     // language / info string
    std::string literal{};  // raw code, newline-separated

    // table
    std::vector<alignment> alignments{};                       // per column
    std::vector<std::vector<std::vector<inline_node>>> rows{}; // row -> cell -> inlines
    // rows[0] is the header row.

    // heading / paragraph inline content
    std::vector<inline_node> inlines{};

    // Nested blocks (document, block_quote, list, list_item)
    std::vector<block_node> children{};
};

// ---- Inline parser ---------------------------------------------------------
//
// A pragmatic inline scanner, not a full CommonMark delimiter-run
// implementation: it handles the well-formed constructs that appear in real
// documents (matched emphasis, code spans, links, images, autolinks, escapes)
// and renders anything ambiguous as literal text.

inline std::vector<inline_node> parse_inlines(std::string_view text);

inline bool is_ascii_punctuation(char c) {
    static const std::string_view punctuation =
        "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
    return punctuation.find(c) != not_found;
}

inline bool is_alphanumeric(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

// Length of the run of character `c` starting at `i`.
inline std::size_t run_length(std::string_view text, std::size_t i, char c) {
    std::size_t j = i;
    while (j < text.size() && text[j] == c) ++j;
    return j - i;
}

struct inline_scanner {
    std::string_view source;
    std::string pending;              // literal text not yet emitted
    std::vector<inline_node> nodes;

    void push_text() {
        if (!pending.empty()) {
            nodes.push_back({inline_kind::text, pending, {}, {}});
            pending.clear();
        }
    }

    void emit(inline_node node) {
        push_text();
        nodes.push_back(std::move(node));
    }

    // Find a closing delimiter run of `c` at least `length` long, at or after
    // `from`. Runs shorter than `length` are skipped over (treated as literal).
    std::size_t find_closer(std::size_t from, char c, std::size_t length) {
        std::size_t j = from;
        while (j < source.size()) {
            if (source[j] == c) {
                std::size_t found = run_length(source, j, c);
                if (found >= length) return j;
                j += found;
            } else {
                ++j;
            }
        }
        return not_found;
    }

    // `_` only delimits at a word boundary so intra-word underscores in
    // identifiers (foo_bar_baz) stay literal. `*` may delimit intra-word.
    bool underscore_can_open(std::size_t i) {
        return i == 0 || !is_alphanumeric(source[i - 1]);
    }
    bool underscore_can_close(std::size_t end) {
        return end >= source.size() || !is_alphanumeric(source[end]);
    }

    void scan() {
        std::size_t i = 0;
        while (i < source.size()) {
            char c = source[i];

            // Backslash escape.
            if (c == '\\' && i + 1 < source.size() &&
                is_ascii_punctuation(source[i + 1])) {
                pending += source[i + 1];
                i += 2;
                continue;
            }

            // Code span: `code`, ``code with ` inside``.
            if (c == '`') {
                std::size_t length = run_length(source, i, '`');
                std::string fence(length, '`');
                std::size_t close = source.find(fence, i + length);
                // A closer must be exactly `length` backticks (not part of a
                // longer run); scan for one.
                while (close != not_found &&
                       run_length(source, close, '`') != length)
                    close = source.find(fence, close + 1);
                if (close != not_found) {
                    std::string code(
                        source.substr(i + length, close - (i + length)));
                    // CommonMark: strip one surrounding space if both present.
                    if (code.size() >= 2 && code.front() == ' ' &&
                        code.back() == ' ' &&
                        code.find_first_not_of(' ') != not_found) {
                        code = code.substr(1, code.size() - 2);
                    }
                    emit({inline_kind::code, code, {}, {}});
                    i = close + length;
                    continue;
                }
            }

            // Image: ![alt](url)
            if (c == '!' && i + 1 < source.size() && source[i + 1] == '[') {
                std::size_t next = parse_link(i + 1, true);
                if (next != not_found) {
                    i = next;
                    continue;
                }
            }

            // Link: [text](url)
            if (c == '[') {
                std::size_t next = parse_link(i, false);
                if (next != not_found) {
                    i = next;
                    continue;
                }
            }

            // Autolink: <https://…> or <name@host>
            if (c == '<') {
                std::size_t next = parse_autolink(i);
                if (next != not_found) {
                    i = next;
                    continue;
                }
            }

            // Emphasis / strong / strikethrough.
            if (c == '*' || c == '_' || c == '~') {
                std::size_t next = parse_emphasis(i, c);
                if (next != not_found) {
                    i = next;
                    continue;
                }
            }

            pending += c;
            ++i;
        }
        push_text();
    }

    // Returns the index past the construct, or not_found if it isn't a link.
    std::size_t parse_link(std::size_t open_bracket, bool image) {
        // Find the matching ']' accounting for nested brackets.
        int depth = 0;
        std::size_t j = open_bracket;
        std::size_t close_bracket = not_found;
        for (; j < source.size(); ++j) {
            if (source[j] == '\\') {
                ++j;
                continue;
            }
            if (source[j] == '[') ++depth;
            else if (source[j] == ']') {
                if (--depth == 0) {
                    close_bracket = j;
                    break;
                }
            }
        }
        if (close_bracket == not_found) return not_found;
        std::size_t paren = close_bracket + 1;
        if (paren >= source.size() || source[paren] != '(') return not_found;

        // URL runs until the closing paren; an optional "title" is dropped.
        std::size_t k = paren + 1;
        std::string url;
        while (k < source.size() && source[k] != ')') {
            if (source[k] == ' ') break;  // start of a title
            url += source[k];
            ++k;
        }
        std::size_t end_paren = source.find(')', k);
        if (end_paren == not_found) return not_found;

        std::string_view inner =
            source.substr(open_bracket + 1, close_bracket - open_bracket - 1);
        inline_node node;
        node.kind = image ? inline_kind::image : inline_kind::link;
        node.url = url;
        node.children = parse_inlines(inner);
        emit(std::move(node));
        return end_paren + 1;
    }

    std::size_t parse_autolink(std::size_t less_than) {
        std::size_t greater_than = source.find('>', less_than + 1);
        if (greater_than == not_found) return not_found;
        std::string_view body =
            source.substr(less_than + 1, greater_than - less_than - 1);
        if (body.empty() || body.find(' ') != not_found) return not_found;
        bool web = body.starts_with("http://") || body.starts_with("https://");
        bool email = !web && body.find('@') != not_found &&
                     body.find('.') != not_found;
        if (!web && !email) return not_found;
        inline_node node;
        node.kind = inline_kind::link;
        node.url = email ? "mailto:" + std::string(body) : std::string(body);
        node.children = {{inline_kind::text, std::string(body), {}, {}}};
        emit(std::move(node));
        return greater_than + 1;
    }

    std::size_t parse_emphasis(std::size_t i, char c) {
        std::size_t length = run_length(source, i, c);
        if (c == '~') {
            if (length < 2) return not_found;  // only ~~strike~~
            length = 2;
        } else {
            if (length > 3) length = 3;
        }
        if (c == '_' && !underscore_can_open(i)) return not_found;

        std::size_t content = i + length;
        std::size_t close = find_closer(content, c, length);
        if (close == not_found) return not_found;
        if (close == content) return not_found;  // empty span
        if (c == '_' && !underscore_can_close(close + length)) return not_found;

        std::string_view inner = source.substr(content, close - content);
        inline_node node;
        if (c == '~') node.kind = inline_kind::strikethrough;
        else if (length == 3) node.kind = inline_kind::strong_emphasis;
        else if (length == 2) node.kind = inline_kind::strong;
        else node.kind = inline_kind::emphasis;
        node.children = parse_inlines(inner);
        emit(std::move(node));
        return close + length;
    }
};

// Parse inline Markdown (emphasis, code, links, …) into a node list.
inline std::vector<inline_node> parse_inlines(std::string_view text) {
    inline_scanner scanner{text, {}, {}};
    scanner.scan();
    return std::move(scanner.nodes);
}

// ---- Block parser ----------------------------------------------------------
//
// A line-oriented block scanner. The source is split into lines once, then
// parse() walks a [begin, end) range dispatching on each line's shape.
// Container blocks (blockquotes, list items) strip their marker and recurse on
// the inner lines, which keeps nesting simple.

inline std::string strip_right(std::string_view text) {
    std::size_t end = text.size();
    while (end > 0 &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r'))
        --end;
    return std::string(text.substr(0, end));
}

inline std::string_view strip_left(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t'))
        ++begin;
    return text.substr(begin);
}

inline bool is_blank(std::string_view text) {
    return strip_left(text).empty();
}

inline int leading_spaces(std::string_view text) {
    int count = 0;
    for (char c : text) {
        if (c == ' ') ++count;
        else if (c == '\t') count += 4;
        else break;
    }
    return count;
}

inline std::vector<std::string> split_lines(std::string_view source) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= source.size(); ++i) {
        if (i == source.size() || source[i] == '\n') {
            lines.emplace_back(strip_right(source.substr(start, i - start)));
            start = i + 1;
        }
    }
    // A trailing newline yields one empty final element; drop it.
    if (!lines.empty() && lines.back().empty() && !source.empty() &&
        source.back() == '\n')
        lines.pop_back();
    return lines;
}

// -- line classifiers --

struct heading_match {
    int level;
    std::string text;
};

inline std::optional<heading_match> match_heading(std::string_view text) {
    std::string_view rest = strip_left(text);
    if (leading_spaces(text) > 3) return std::nullopt;
    int level = 0;
    while (level < static_cast<int>(rest.size()) && rest[level] == '#') ++level;
    if (level == 0 || level > 6) return std::nullopt;
    if (level < static_cast<int>(rest.size()) && rest[level] != ' ')
        return std::nullopt;
    // Strip an optional closing run of '#'.
    std::string title = strip_right(strip_left(rest.substr(level)));
    std::size_t end = title.size();
    while (end > 0 && title[end - 1] == '#') --end;
    if (end < title.size() && (end == 0 || title[end - 1] == ' '))
        title = strip_right(title.substr(0, end));
    return heading_match{level, title};
}

struct fence_match {
    char character;
    int length;
    int indent;
    std::string info;
};

inline std::optional<fence_match> match_fence(std::string_view text) {
    if (leading_spaces(text) > 3) return std::nullopt;
    std::string_view rest = strip_left(text);
    if (rest.empty() || (rest[0] != '`' && rest[0] != '~')) return std::nullopt;
    char character = rest[0];
    int length = 0;
    while (length < static_cast<int>(rest.size()) && rest[length] == character)
        ++length;
    if (length < 3) return std::nullopt;
    std::string info = strip_right(strip_left(rest.substr(length)));
    // Info strings for backtick fences may not contain a backtick.
    if (character == '`' && info.find('`') != std::string::npos)
        return std::nullopt;
    return fence_match{character, length, leading_spaces(text), info};
}

inline bool match_thematic_break(std::string_view text) {
    if (leading_spaces(text) > 3) return false;
    std::string_view rest = strip_left(text);
    if (rest.empty()) return false;
    char marker = rest[0];
    if (marker != '-' && marker != '*' && marker != '_') return false;
    int count = 0;
    for (char c : rest) {
        if (c == marker) ++count;
        else if (c != ' ' && c != '\t') return false;
    }
    return count >= 3;
}

inline bool is_blockquote(std::string_view text) {
    if (leading_spaces(text) > 3) return false;
    std::string_view rest = strip_left(text);
    return !rest.empty() && rest[0] == '>';
}

// Strip one level of "> " from a blockquote line.
inline std::string strip_blockquote(std::string_view text) {
    std::string_view rest = strip_left(text);
    rest.remove_prefix(1);  // '>'
    if (!rest.empty() && rest[0] == ' ') rest.remove_prefix(1);
    return std::string(rest);
}

struct list_marker {
    int indent;          // leading spaces before the marker
    int content_indent;  // column where the item's content begins
    bool ordered;
    int start;
    char bullet;         // '-', '*', '+' for bullets
};

inline std::optional<list_marker> match_list(std::string_view text) {
    int indent = leading_spaces(text);
    if (indent > 3 && strip_left(text).empty()) return std::nullopt;
    std::string_view rest = strip_left(text);
    if (rest.empty()) return std::nullopt;

    if (rest[0] == '-' || rest[0] == '*' || rest[0] == '+') {
        if (rest.size() < 2 || rest[1] != ' ') return std::nullopt;
        return list_marker{indent, indent + 2, false, 1, rest[0]};
    }
    // Ordered: digits then '.' or ')'.
    int digits = 0;
    while (digits < static_cast<int>(rest.size()) &&
           std::isdigit(static_cast<unsigned char>(rest[digits])))
        ++digits;
    if (digits == 0 || digits > 9) return std::nullopt;
    if (digits >= static_cast<int>(rest.size()) ||
        (rest[digits] != '.' && rest[digits] != ')'))
        return std::nullopt;
    if (digits + 1 >= static_cast<int>(rest.size()) || rest[digits + 1] != ' ')
        return std::nullopt;
    int start = std::stoi(std::string(rest.substr(0, digits)));
    return list_marker{indent, indent + digits + 2, true, start, 0};
}

// A GFM delimiter row: pipes, dashes, colons, spaces, at least one dash.
inline bool is_table_delimiter(std::string_view text) {
    std::string_view rest = strip_left(text);
    if (rest.empty()) return false;
    bool dash = false;
    for (char c : rest) {
        if (c == '-') dash = true;
        else if (c != '|' && c != ':' && c != ' ' && c != '\t') return false;
    }
    return dash && rest.find('-') != not_found;
}

// Split a table row on unescaped pipes, trimming cells and the optional
// leading/trailing empty cell from surrounding pipes.
inline std::vector<std::string> split_row(std::string_view text) {
    std::string row = strip_right(strip_left(text));
    std::vector<std::string> cells;
    std::string cell;
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (row[i] == '\\' && i + 1 < row.size() && row[i + 1] == '|') {
            cell += '|';
            ++i;
        } else if (row[i] == '|') {
            cells.push_back(cell);
            cell.clear();
        } else {
            cell += row[i];
        }
    }
    cells.push_back(cell);
    if (!cells.empty() && cells.front().find_first_not_of(" \t") == not_found)
        cells.erase(cells.begin());
    if (!cells.empty() && cells.back().find_first_not_of(" \t") == not_found)
        cells.pop_back();
    for (auto& each : cells) each = strip_right(strip_left(each));
    return cells;
}

// -- the parser --

class block_parser {
public:
    explicit block_parser(const std::vector<std::string>& lines)
        : source_lines(lines) {}

    std::vector<block_node> parse(std::size_t begin, std::size_t end) {
        const auto& lines = source_lines;
        std::vector<block_node> blocks;
        std::size_t i = begin;
        while (i < end) {
            if (is_blank(lines[i])) {
                ++i;
                continue;
            }
            if (auto fence = match_fence(lines[i])) {
                blocks.push_back(parse_code(i, end, *fence));
            } else if (auto heading = match_heading(lines[i])) {
                block_node node{.kind = block_kind::heading,
                                .level = heading->level};
                node.inlines = parse_inlines(heading->text);
                blocks.push_back(std::move(node));
                ++i;
            } else if (match_thematic_break(lines[i])) {
                blocks.push_back({.kind = block_kind::thematic_break});
                ++i;
            } else if (is_blockquote(lines[i])) {
                blocks.push_back(parse_blockquote(i, end));
            } else if (match_list(lines[i])) {
                blocks.push_back(parse_list(i, end));
            } else if (i + 1 < end && lines[i].find('|') != not_found &&
                       is_table_delimiter(lines[i + 1])) {
                blocks.push_back(parse_table(i, end));
            } else {
                blocks.push_back(parse_paragraph(i, end));
            }
        }
        return blocks;
    }

private:
    const std::vector<std::string>& source_lines;

    // Does this line begin a block other than a paragraph continuation?
    bool starts_block(std::size_t i, std::size_t end) {
        const std::string& text = source_lines[i];
        if (is_blank(text)) return true;
        if (match_fence(text) || match_heading(text) ||
            match_thematic_break(text) || is_blockquote(text) ||
            match_list(text))
            return true;
        if (i + 1 < end && text.find('|') != not_found &&
            is_table_delimiter(source_lines[i + 1]))
            return true;
        return false;
    }

    block_node parse_code(std::size_t& i, std::size_t end,
                          const fence_match& fence) {
        block_node node{.kind = block_kind::code_block, .info = fence.info};
        ++i;  // past opening fence
        std::string body;
        bool first = true;
        while (i < end) {
            std::string_view rest = strip_left(source_lines[i]);
            bool closing = false;
            if (!rest.empty() && rest[0] == fence.character) {
                int length = 0;
                while (length < static_cast<int>(rest.size()) &&
                       rest[length] == fence.character)
                    ++length;
                if (length >= fence.length && strip_right(rest.substr(length)).empty())
                    closing = true;
            }
            if (closing) {
                ++i;
                break;
            }
            // Strip up to the fence's own indentation from each content line.
            std::string_view text = source_lines[i];
            int strip = 0;
            while (strip < fence.indent && strip < static_cast<int>(text.size()) &&
                   text[strip] == ' ')
                ++strip;
            if (!first) body += '\n';
            body += text.substr(strip);
            first = false;
            ++i;
        }
        node.literal = body;
        return node;
    }

    block_node parse_blockquote(std::size_t& i, std::size_t end) {
        std::vector<std::string> inner;
        while (i < end && is_blockquote(source_lines[i])) {
            inner.push_back(strip_blockquote(source_lines[i]));
            ++i;
        }
        block_parser nested(inner);
        block_node node{.kind = block_kind::block_quote};
        node.children = nested.parse(0, inner.size());
        return node;
    }

    block_node parse_list(std::size_t& i, std::size_t end) {
        list_marker first = *match_list(source_lines[i]);
        block_node list{.kind = block_kind::list,
                        .ordered = first.ordered,
                        .start = first.start};

        while (i < end) {
            auto marker = match_list(source_lines[i]);
            if (!marker) break;
            // A different bullet character or ordered/unordered switch starts a
            // new list, not another item of this one.
            if (marker->ordered != first.ordered ||
                (!marker->ordered && marker->bullet != first.bullet) ||
                marker->indent != first.indent)
                break;

            block_node item = parse_item(i, end, *marker);
            list.children.push_back(std::move(item));

            // Skip a single blank separator between items (loose list).
            std::size_t saved = i;
            while (i < end && is_blank(source_lines[i])) ++i;
            if (i < end) {
                auto next = match_list(source_lines[i]);
                if (!next || next->indent != first.indent ||
                    next->ordered != first.ordered ||
                    (!next->ordered && next->bullet != first.bullet)) {
                    i = saved;  // not a continuation of this list
                    break;
                }
            } else {
                break;
            }
        }
        return list;
    }

    block_node parse_item(std::size_t& i, std::size_t end,
                          const list_marker& marker) {
        // First line: text after the marker.
        std::vector<std::string> inner;
        std::string_view first_line = source_lines[i];
        inner.emplace_back(first_line.substr(
            std::min<std::size_t>(marker.content_indent, first_line.size())));
        ++i;

        // Continuation lines: blanks, or lines indented into the content column.
        while (i < end) {
            if (is_blank(source_lines[i])) {
                // Peek: does the item continue after the blank line?
                std::size_t j = i + 1;
                while (j < end && is_blank(source_lines[j])) ++j;
                if (j < end && leading_spaces(source_lines[j]) >= marker.content_indent) {
                    inner.emplace_back("");
                    i = j;
                    continue;
                }
                break;
            }
            if (leading_spaces(source_lines[i]) >= marker.content_indent) {
                inner.emplace_back(source_lines[i].substr(marker.content_indent));
                ++i;
            } else if (!starts_block(i, end)) {
                // Lazy paragraph continuation.
                inner.emplace_back(source_lines[i]);
                ++i;
            } else {
                break;
            }
        }

        block_parser nested(inner);
        block_node item{.kind = block_kind::list_item};
        item.children = nested.parse(0, inner.size());
        detect_task(item);
        return item;
    }

    // Promote a leading "[ ] "/"[x] " on the item's first paragraph to a task
    // checkbox state.
    static void detect_task(block_node& item) {
        if (item.children.empty() ||
            item.children.front().kind != block_kind::paragraph)
            return;
        auto& paragraph = item.children.front();
        if (paragraph.inlines.empty() ||
            paragraph.inlines.front().kind != inline_kind::text)
            return;
        std::string& text = paragraph.inlines.front().text;
        if (text.size() >= 4 && text[0] == '[' && text[2] == ']' && text[3] == ' ' &&
            (text[1] == ' ' || text[1] == 'x' || text[1] == 'X')) {
            item.task_state = (text[1] == ' ') ? 0 : 1;
            text.erase(0, 4);
        }
    }

    block_node parse_table(std::size_t& i, std::size_t end) {
        block_node node{.kind = block_kind::table};
        std::vector<std::string> header = split_row(source_lines[i]);
        std::vector<std::string> delimiters = split_row(source_lines[i + 1]);
        std::size_t columns = header.size();

        node.alignments.resize(columns, alignment::left);
        for (std::size_t c = 0; c < columns && c < delimiters.size(); ++c) {
            const std::string& delimiter = delimiters[c];
            bool left = !delimiter.empty() && delimiter.front() == ':';
            bool right = !delimiter.empty() && delimiter.back() == ':';
            if (left && right) node.alignments[c] = alignment::center;
            else if (right) node.alignments[c] = alignment::right;
            else node.alignments[c] = alignment::left;
        }

        auto add_row = [&](std::vector<std::string> cells) {
            cells.resize(columns);  // pad/truncate to the header's column count
            std::vector<std::vector<inline_node>> row;
            for (auto& cell : cells) row.push_back(parse_inlines(cell));
            node.rows.push_back(std::move(row));
        };

        add_row(header);
        i += 2;
        while (i < end && !is_blank(source_lines[i]) &&
               source_lines[i].find('|') != not_found) {
            add_row(split_row(source_lines[i]));
            ++i;
        }
        return node;
    }

    block_node parse_paragraph(std::size_t& i, std::size_t end) {
        std::string text;
        bool first = true;
        while (i < end && !starts_block(i, end)) {
            if (!first) text += '\n';
            text += strip_right(strip_left(source_lines[i]));
            first = false;
            ++i;
        }
        block_node node{.kind = block_kind::paragraph};
        node.inlines = parse_inlines(text);
        return node;
    }
};

// Parse a whole document. Returns the document's top-level blocks.
inline std::vector<block_node> parse_document(std::string_view source) {
    std::vector<std::string> lines = split_lines(source);
    block_parser parser(lines);
    return parser.parse(0, lines.size());
}

}  // namespace detail

// ---- The document as a tree -----------------------------------------------
//
// For a program that lays Markdown out itself, as a window does by pixels: the
// blocks a document holds, each with its inline content, styled spans nesting.
// It's the tree layout() is made from.
using detail::alignment;
using detail::block_kind;
using detail::block_node;
using detail::inline_kind;
using detail::inline_node;

// The top-level blocks of a document.
inline std::vector<block_node> parse(std::string_view source) { return detail::parse_document(source); }

namespace detail {

// ---- Building rows ---------------------------------------------------------

// Add `text` in `look` to the end of `row`. Plain text joins a plain run before
// it, since drawing it is the same either way; each styled piece stays its own
// run, as each was its own escape-coded span in md, even when empty.
inline void append(line& row, std::string_view text, const style& look = {}) {
    bool simple = look.plain() && look.link.empty();
    if (simple) {
        if (text.empty()) return;
        if (!row.empty() && row.back().look.plain() &&
            row.back().look.link.empty() && !row.back().truncated) {
            row.back().text += text;
            return;
        }
    }
    run piece;
    piece.text = std::string(text);
    piece.look = look;
    row.push_back(std::move(piece));
}

// Add a whole row's runs to the end of `row`, unchanged.
inline void extend(line& row, const line& more) {
    row.insert(row.end(), more.begin(), more.end());
}

inline std::string repeat(std::string_view glyph, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) out += glyph;
    return out;
}

inline std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == delimiter) {
            parts.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

// ---- Syntax highlighting ---------------------------------------------------
//
// A small state-machine lexer, driven by a per-language description (comment
// markers, string delimiters, keyword sets). It is deliberately approximate:
// good enough to make code readable, not a compiler front-end. Unknown
// languages still get generic string/number highlighting.

// -- token palette --
inline const style kComment{.italic = true, .foreground = color::bright_black};
inline const style kString{.foreground = color::bright_green};
inline const style kNumber{.foreground = color::bright_cyan};
inline const style kKeyword{.bold = true, .foreground = color::bright_magenta};
inline const style kType{.foreground = color::bright_yellow};
inline const style kFunction{.foreground = color::bright_blue};

// -- language description --
using text_list = std::vector<std::string>;
using word_set = std::unordered_set<std::string>;

struct language_rules {
    text_list line_comments;            // e.g. {"//", "#"}
    std::string block_open, block_close;  // "" when unsupported
    text_list multiline_strings;        // e.g. {"\"\"\"", "'''"}
    std::string string_delimiters;      // single characters, e.g. "\"'`"
    word_set keywords;
    word_set types;                     // builtins / type names
    bool functions = false;             // color name-before-'(' calls
};

inline const language_rules& rules_default() {
    static const language_rules rules{{}, "", "", {}, "\"'", {}, {}, false};
    return rules;
}

inline const language_rules& rules_cpp() {
    static const language_rules rules{
        {"//"}, "/*", "*/", {}, "\"'",
        word_set{"alignas","alignof","and","asm","auto","bool","break","case","catch",
           "char","char8_t","char16_t","char32_t","class","concept","const",
           "consteval","constexpr","constinit","const_cast","continue","co_await",
           "co_return","co_yield","decltype","default","delete","do","double",
           "dynamic_cast","else","enum","explicit","export","extern","false",
           "float","for","friend","goto","if","inline","int","long","mutable",
           "namespace","new","noexcept","nullptr","operator","private","protected",
           "public","register","reinterpret_cast","requires","return","short",
           "signed","sizeof","static","static_assert","static_cast","struct",
           "switch","template","this","thread_local","throw","true","try",
           "typedef","typeid","typename","union","unsigned","using","virtual",
           "void","volatile","wchar_t","while"},
        word_set{"size_t","ssize_t","ptrdiff_t","int8_t","int16_t","int32_t","int64_t",
           "uint8_t","uint16_t","uint32_t","uint64_t","string","string_view",
           "wstring","vector","map","unordered_map","set","unordered_set","array",
           "pair","tuple","optional","variant","span","std"},
        true};
    return rules;
}

inline const language_rules& rules_c() {
    static const language_rules rules{
        {"//"}, "/*", "*/", {}, "\"'",
        word_set{"auto","break","case","char","const","continue","default","do",
           "double","else","enum","extern","float","for","goto","if","inline",
           "int","long","register","restrict","return","short","signed","sizeof",
           "static","struct","switch","typedef","union","unsigned","void",
           "volatile","while","_Bool","bool","true","false","NULL"},
        word_set{"size_t","int8_t","int16_t","int32_t","int64_t","uint8_t","uint16_t",
           "uint32_t","uint64_t","FILE"},
        true};
    return rules;
}

inline const language_rules& rules_python() {
    static const language_rules rules{
        {"#"}, "", "", text_list{"\"\"\"", "'''"}, "\"'",
        word_set{"False","None","True","and","as","assert","async","await","break",
           "class","continue","def","del","elif","else","except","finally","for",
           "from","global","if","import","in","is","lambda","nonlocal","not","or",
           "pass","raise","return","try","while","with","yield","match","case"},
        word_set{"print","len","range","int","float","str","list","dict","tuple","set",
           "bytes","bool","object","super","isinstance","enumerate","zip","map",
           "filter","open","self","cls","abs","min","max","sum","sorted"},
        true};
    return rules;
}

inline const language_rules& rules_javascript() {
    static const language_rules rules{
        {"//"}, "/*", "*/", {}, "\"'`",
        word_set{"async","await","break","case","catch","class","const","continue",
           "debugger","default","delete","do","else","export","extends","false",
           "finally","for","from","function","if","import","in","instanceof",
           "let","new","null","of","return","static","super","switch","this",
           "throw","true","try","typeof","undefined","var","void","while","yield",
           "interface","type","enum","implements","public","private","protected",
           "readonly","namespace","declare","abstract","as","is","keyof"},
        word_set{"console","document","window","Math","JSON","Object","Array","String",
           "Number","Boolean","Promise","Map","Set","Symbol","number","string",
           "boolean","any","unknown","never","void"},
        true};
    return rules;
}

inline const language_rules& rules_rust() {
    // No single-quote strings: '\'' would swallow lifetimes like 'a.
    static const language_rules rules{
        {"//"}, "/*", "*/", {}, "\"",
        word_set{"as","async","await","break","const","continue","crate","dyn","else",
           "enum","extern","false","fn","for","if","impl","in","let","loop",
           "match","mod","move","mut","pub","ref","return","self","Self","static",
           "struct","super","trait","true","type","unsafe","use","where","while"},
        word_set{"i8","i16","i32","i64","i128","isize","u8","u16","u32","u64","u128",
           "usize","f32","f64","bool","char","str","String","Vec","Option","Result",
           "Box","Some","None","Ok","Err","Rc","Arc","HashMap","HashSet"},
        true};
    return rules;
}

inline const language_rules& rules_go() {
    static const language_rules rules{
        {"//"}, "/*", "*/", {}, "\"'`",
        word_set{"break","case","chan","const","continue","default","defer","else",
           "fallthrough","for","func","go","goto","if","import","interface","map",
           "package","range","return","select","struct","switch","type","var"},
        word_set{"bool","byte","complex64","complex128","error","float32","float64",
           "int","int8","int16","int32","int64","rune","string","uint","uint8",
           "uint16","uint32","uint64","uintptr","true","false","nil","iota",
           "append","len","cap","make","new","panic","recover","print","println"},
        true};
    return rules;
}

inline const language_rules& rules_shell() {
    static const language_rules rules{
        {"#"}, "", "", {}, "\"'",
        word_set{"if","then","else","elif","fi","case","esac","for","while","until",
           "do","done","in","function","select","time","return","break",
           "continue","local","export","readonly","declare"},
        word_set{"echo","cd","pwd","ls","cat","grep","sed","awk","cp","mv","rm","mkdir",
           "make","git","sudo","source","read","set","unset","exit"},
        false};
    return rules;
}

inline const language_rules& rules_json() {
    static const language_rules rules{
        {}, "", "", {}, "\"",
        word_set{"true","false","null"}, {}, false};
    return rules;
}

inline const language_rules& rules_for(std::string_view language) {
    static const std::unordered_map<std::string, const language_rules& (*)()>
        registry = {
            {"cpp", rules_cpp}, {"c++", rules_cpp}, {"cxx", rules_cpp},
            {"cc", rules_cpp}, {"hpp", rules_cpp}, {"hxx", rules_cpp},
            {"h", rules_cpp},
            {"c", rules_c},
            {"py", rules_python}, {"python", rules_python},
            {"js", rules_javascript}, {"javascript", rules_javascript},
            {"jsx", rules_javascript}, {"ts", rules_javascript},
            {"typescript", rules_javascript}, {"tsx", rules_javascript},
            {"rs", rules_rust}, {"rust", rules_rust},
            {"go", rules_go}, {"golang", rules_go},
            {"sh", rules_shell}, {"bash", rules_shell}, {"shell", rules_shell},
            {"zsh", rules_shell},
            {"json", rules_json},
        };
    auto found = registry.find(std::string(language));
    return found == registry.end() ? rules_default() : found->second();
}

// -- the lexer --
enum class lexer_mode { normal, block_comment, multiline_string };
struct lexer_state {
    lexer_mode mode = lexer_mode::normal;
    std::string closer;  // block_close, or the multiline-string delimiter
};

inline bool is_identifier_start(unsigned char c) {
    return std::isalpha(c) || c == '_' || c == '$' || c >= 0x80;
}
inline bool is_identifier_part(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '$' || c >= 0x80;
}

inline bool matches_at(const std::string& text, std::size_t i,
                       const std::string& token) {
    return !token.empty() && text.compare(i, token.size(), token) == 0;
}

inline line highlight_line(const std::string& text, const language_rules& rules,
                           lexer_state& state) {
    line row;
    std::size_t i = 0, size = text.size();
    auto put = [&](std::size_t from, std::size_t to, const style& look) {
        append(row, text.substr(from, to - from), look);
    };

    // Continuation of a multi-line construct started on a previous line.
    if (state.mode != lexer_mode::normal) {
        const style& look =
            state.mode == lexer_mode::block_comment ? kComment : kString;
        std::size_t position = text.find(state.closer);
        if (position == not_found) {
            append(row, text, look);
            return row;
        }
        put(0, position + state.closer.size(), look);
        i = position + state.closer.size();
        state.mode = lexer_mode::normal;
    }

    while (i < size) {
        char c = text[i];

        // Line comment: rest of the line.
        bool done = false;
        for (const auto& marker : rules.line_comments) {
            if (matches_at(text, i, marker)) {
                put(i, size, kComment);
                i = size;
                done = true;
                break;
            }
        }
        if (done) break;

        // Block comment (may span lines).
        if (matches_at(text, i, rules.block_open)) {
            std::size_t position =
                text.find(rules.block_close, i + rules.block_open.size());
            if (position == not_found) {
                put(i, size, kComment);
                state.mode = lexer_mode::block_comment;
                state.closer = rules.block_close;
                i = size;
            } else {
                put(i, position + rules.block_close.size(), kComment);
                i = position + rules.block_close.size();
            }
            continue;
        }

        // Multi-line string (triple-quote and friends).
        done = false;
        for (const auto& delimiter : rules.multiline_strings) {
            if (matches_at(text, i, delimiter)) {
                std::size_t position = text.find(delimiter, i + delimiter.size());
                if (position == not_found) {
                    put(i, size, kString);
                    state.mode = lexer_mode::multiline_string;
                    state.closer = delimiter;
                    i = size;
                } else {
                    put(i, position + delimiter.size(), kString);
                    i = position + delimiter.size();
                }
                done = true;
                break;
            }
        }
        if (done) continue;

        // Single-line string with backslash escapes.
        if (rules.string_delimiters.find(c) != not_found) {
            std::size_t j = i + 1;
            while (j < size) {
                if (text[j] == '\\') {
                    j += 2;
                    continue;
                }
                if (text[j] == c) {
                    ++j;
                    break;
                }
                ++j;
            }
            put(i, j, kString);
            i = j;
            continue;
        }

        auto digit = [&](std::size_t k) {
            return std::isdigit(static_cast<unsigned char>(text[k])) != 0;
        };

        // Number literal.
        if (digit(i) || (c == '.' && i + 1 < size && digit(i + 1))) {
            std::size_t j = i;
            if (c == '0' && i + 1 < size && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
                j = i + 2;
                while (j < size && std::isxdigit(static_cast<unsigned char>(text[j])))
                    ++j;
            } else {
                while (j < size && digit(j)) ++j;
                if (j < size && text[j] == '.') {
                    ++j;
                    while (j < size && digit(j)) ++j;
                }
                if (j < size && (text[j] == 'e' || text[j] == 'E')) {
                    ++j;
                    if (j < size && (text[j] == '+' || text[j] == '-')) ++j;
                    while (j < size && digit(j)) ++j;
                }
            }
            while (j < size && (std::isalpha(static_cast<unsigned char>(text[j])) ||
                                text[j] == '_'))
                ++j;  // suffixes: 10u, 1.5f, 3px
            put(i, j, kNumber);
            i = j;
            continue;
        }

        // Identifier / keyword / type / function call.
        if (is_identifier_start(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            while (j < size && is_identifier_part(static_cast<unsigned char>(text[j])))
                ++j;
            std::string word = text.substr(i, j - i);
            if (rules.keywords.count(word)) {
                append(row, word, kKeyword);
            } else if (rules.types.count(word)) {
                append(row, word, kType);
            } else if (rules.functions) {
                std::size_t k = j;
                while (k < size && text[k] == ' ') ++k;
                if (k < size && text[k] == '(')
                    append(row, word, kFunction);
                else
                    append(row, word);
            } else {
                append(row, word);
            }
            i = j;
            continue;
        }

        append(row, std::string_view(&text[i], 1));
        ++i;
    }
    return row;
}

// Highlight already-split code `lines` for `language` (the fence info word),
// one row per input line.
inline std::vector<line> highlight_code(const std::vector<std::string>& lines,
                                        std::string_view language) {
    const language_rules& rules = rules_for(language);
    lexer_state state;
    std::vector<line> rows;
    rows.reserve(lines.size());
    for (const auto& text : lines) rows.push_back(highlight_line(text, rules, state));
    return rows;
}

// ---- Tables ----------------------------------------------------------------
//
// Box-drawn tables, following catcsv/olog's "style is data" idea: a border
// style is just a bag of glyphs, and one generator emits each line type. Unlike
// olog we measure with display_width, align per the GFM delimiter row, and
// shrink columns to fit the width.

struct border_style {
    // Corner/junction glyphs for the three horizontal rules.
    std::string top_left, top_junction, top_right;           // ╭ ┬ ╮
    std::string middle_left, middle_junction, middle_right;  // ├ ┼ ┤
    std::string bottom_left, bottom_junction, bottom_right;  // ╰ ┴ ╯
    std::string horizontal;                                  // ─
    std::string vertical;                                    // │
};

inline const border_style kRounded{
    "╭", "┬", "╮", "├", "┼", "┤", "╰", "┴", "╯", "─", "│",
};

constexpr int kMinimumColumn = 3;  // never shrink a column below this many columns

// A horizontal rule spanning all columns: left + fill·(width+2) + junction + … + right.
inline line horizontal_rule(const border_style& border, const std::string& left,
                            const std::string& junction, const std::string& right,
                            const std::vector<int>& widths) {
    std::string out = left;
    for (std::size_t c = 0; c < widths.size(); ++c) {
        out += repeat(border.horizontal, widths[c] + 2);
        out += (c + 1 == widths.size()) ? right : junction;
    }
    line row;
    append(row, out);
    return row;
}

// Pad one cell to `width` columns per its alignment.
inline line pad_cell(const line& cell, int width, alignment align) {
    int fill = width - display_width(cell);
    if (fill <= 0) return cell;
    line row;
    switch (align) {
        case alignment::right:
            append(row, std::string(fill, ' '));
            extend(row, cell);
            return row;
        case alignment::center: {
            int left = fill / 2, right = fill - left;
            append(row, std::string(left, ' '));
            extend(row, cell);
            append(row, std::string(right, ' '));
            return row;
        }
        case alignment::left:
        default:
            row = cell;
            append(row, std::string(fill, ' '));
            return row;
    }
}

// Truncate a cell to at most `max_width` columns, ending in an ellipsis when
// anything was cut. The ellipsis takes the style of the run it cut into.
inline line truncate_cell(const line& cell, int max_width) {
    if (display_width(cell) <= max_width) return cell;
    if (max_width <= 0) return {};
    int budget = max_width - 1;  // leave a column for the ellipsis
    line out;
    int width = 0;
    for (const run& piece : cell) {
        std::size_t i = 0;
        while (i < piece.text.size()) {
            std::size_t j = i;
            int point_width = char_width(decode_utf8(piece.text, j));
            if (width + point_width > budget) {
                run cut = piece;
                cut.text = piece.text.substr(0, i) + "…";
                cut.truncated = true;
                out.push_back(std::move(cut));
                return out;
            }
            width += point_width;
            i = j;
        }
        out.push_back(piece);
    }
    return out;  // unreachable: the cell was wider than max_width
}

// Lay out a table. `cells[0]` is the (already bold) header row. `max_width`
// bounds the whole table's width.
inline std::vector<line> render_table(const std::vector<std::vector<line>>& cells,
                                      const std::vector<alignment>& alignments,
                                      int max_width,
                                      const border_style& border = kRounded) {
    if (cells.empty()) return {line{}};
    std::size_t columns = 0;
    for (const auto& row : cells) columns = std::max(columns, row.size());
    if (columns == 0) return {line{}};

    std::vector<alignment> aligned = alignments;
    aligned.resize(columns, alignment::left);

    // Natural column widths = widest cell in each column.
    std::vector<int> widths(columns, kMinimumColumn);
    for (const auto& row : cells)
        for (std::size_t c = 0; c < row.size(); ++c)
            widths[c] = std::max(widths[c], display_width(row[c]));

    // Total = content + 2 pad spaces per column + one border per column edge.
    auto total_width = [&] {
        int sum = static_cast<int>(columns) + 1;  // vertical borders
        for (int width : widths) sum += width + 2;
        return sum;
    };

    // Shrink the widest column repeatedly until it fits (or all hit the floor).
    while (total_width() > max_width) {
        auto widest = std::max_element(widths.begin(), widths.end());
        if (*widest <= kMinimumColumn) break;
        --*widest;
    }

    // Truncate any cell that overflows its (possibly shrunk) column.
    auto build_row = [&](const std::vector<line>& cells_in_row) {
        line row;
        append(row, border.vertical);
        for (std::size_t c = 0; c < columns; ++c) {
            line cell = c < cells_in_row.size() ? cells_in_row[c] : line{};
            cell = truncate_cell(cell, widths[c]);
            append(row, " ");
            extend(row, pad_cell(cell, widths[c], aligned[c]));
            append(row, " ");
            append(row, border.vertical);
        }
        return row;
    };

    std::vector<line> rows;
    rows.push_back(horizontal_rule(border, border.top_left, border.top_junction,
                                   border.top_right, widths));
    rows.push_back(build_row(cells[0]));
    rows.push_back(horizontal_rule(border, border.middle_left,
                                   border.middle_junction, border.middle_right,
                                   widths));
    for (std::size_t r = 1; r < cells.size(); ++r) rows.push_back(build_row(cells[r]));
    rows.push_back(horizontal_rule(border, border.bottom_left,
                                   border.bottom_junction, border.bottom_right,
                                   widths));
    return rows;
}

// ---- Layout ----------------------------------------------------------------
//
// Inline content is flattened into styled spans, then word-wrapped by display
// width. Block layouts compose their children's rows and prefix each row (list
// markers, blockquote bars, heading hashes). Every block lays out to at least
// one row, an empty one when it has nothing to show.

inline color heading_color(int level) {
    switch (level) {
        case 1: return color::bright_magenta;
        case 2: return color::bright_blue;
        case 3: return color::bright_cyan;
        case 4: return color::bright_green;
        case 5: return color::bright_yellow;
        default: return color::white;
    }
}

// Flatten inline nodes into styled spans, inheriting `base` (and its link).
inline void flatten(const std::vector<inline_node>& nodes, const style& base,
                    const options& settings, std::vector<run>& spans) {
    auto push = [&](std::string text, const style& look) {
        run span;
        span.text = std::move(text);
        span.look = look;
        spans.push_back(std::move(span));
    };
    // Without a way to click the link, show where it goes.
    auto spell_out = [&](const std::string& url) {
        if (settings.hyperlinks || url.empty()) return;
        style unlinked = base;
        unlinked.link.clear();
        push(" (" + url + ")", unlinked);
    };
    for (const auto& node : nodes) {
        style look = base;
        switch (node.kind) {
            case inline_kind::text:
                push(node.text, base);
                break;
            case inline_kind::code:
                look.bold = false;
                look.italic = false;
                look.foreground = color::bright_yellow;
                push(node.text, look);
                break;
            case inline_kind::emphasis:
                look.italic = true;
                flatten(node.children, look, settings, spans);
                break;
            case inline_kind::strong:
                look.bold = true;
                flatten(node.children, look, settings, spans);
                break;
            case inline_kind::strong_emphasis:
                look.bold = true;
                look.italic = true;
                flatten(node.children, look, settings, spans);
                break;
            case inline_kind::strikethrough:
                look.strike = true;
                flatten(node.children, look, settings, spans);
                break;
            case inline_kind::link:
                look.underline = true;
                look.foreground = color::bright_blue;
                look.link = node.url;
                flatten(node.children, look, settings, spans);
                spell_out(node.url);
                break;
            case inline_kind::image:
                look.foreground = color::bright_magenta;
                look.link = node.url;
                push("🖼 ", look);
                flatten(node.children, look, settings, spans);
                spell_out(node.url);
                break;
        }
    }
}

inline bool is_space(char c) {
    return c == ' ' || c == '\n' || c == '\t';
}

// Word-wrap styled spans to `available` columns. Words are joined by one plain
// space; a word may mix styles (e.g. "**bold**,").
inline std::vector<line> wrap(const std::vector<run>& spans, int available) {
    if (available < 1) available = 1;
    struct word {
        line pieces;
        int width = 0;
    };
    std::vector<word> words;
    word current;
    bool started = false;
    auto flush = [&] {
        if (started) {
            words.push_back(current);
            current = word{};
            started = false;
        }
    };
    for (const auto& span : spans) {
        const std::string& text = span.text;
        std::size_t i = 0;
        while (i < text.size()) {
            if (is_space(text[i])) {
                flush();
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < text.size() && !is_space(text[j])) ++j;
            std::string_view segment(text.data() + i, j - i);
            append(current.pieces, segment, span.look);
            current.width += display_width(segment);
            started = true;
            i = j;
        }
    }
    flush();

    std::vector<line> rows;
    line row;
    int row_width = 0;
    bool row_started = false;
    for (const auto& each : words) {
        if (row_started && row_width + 1 + each.width > available) {
            rows.push_back(std::move(row));
            row.clear();
            row_width = 0;
            row_started = false;
        }
        if (!row_started) {
            row = each.pieces;
            row_width = each.width;
            row_started = true;
        } else {
            append(row, " ");
            for (const auto& piece : each.pieces) append(row, piece.text, piece.look);
            row_width += 1 + each.width;
        }
    }
    if (row_started) rows.push_back(std::move(row));
    return rows;
}

// Lay out inline nodes as a single unwrapped row (for table cells).
inline line inline_row(const std::vector<inline_node>& nodes, const style& base,
                       const options& settings) {
    std::vector<run> spans;
    flatten(nodes, base, settings, spans);
    line row;
    for (const auto& span : spans) append(row, span.text, span.look);
    return row;
}

// Prefix the first row with `first` and every later row with `rest`.
inline std::vector<line> prefix_rows(std::vector<line> rows, const line& first,
                                     const line& rest) {
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const line& prefix = i == 0 ? first : rest;
        rows[i].insert(rows[i].begin(), prefix.begin(), prefix.end());
    }
    return rows;
}

inline line single_run(std::string_view text, const style& look = {}) {
    line row;
    append(row, text, look);
    return row;
}

inline int digit_count(int number) {
    int count = 1;
    for (number = number < 0 ? -number : number; number >= 10; number /= 10)
        ++count;
    return count;
}

inline std::vector<line> render_block(const block_node& block, int width,
                                      const options& settings);

// Lay out blocks one after another, with an empty row between them when
// `separated`.
inline std::vector<line> render_blocks(const std::vector<block_node>& blocks,
                                       int width, bool separated,
                                       const options& settings) {
    std::vector<line> rows;
    bool first = true;
    for (const auto& block : blocks) {
        std::vector<line> block_rows = render_block(block, width, settings);
        if (!first && separated) rows.emplace_back();
        rows.insert(rows.end(), block_rows.begin(), block_rows.end());
        first = false;
    }
    if (rows.empty()) rows.emplace_back();
    return rows;
}

inline std::vector<line> render_heading(const block_node& block, int width,
                                        const options& settings) {
    int prefix_width = block.level + 1;
    std::string hashes = std::string(block.level, '#') + " ";

    style look{.bold = true, .foreground = heading_color(block.level)};
    std::vector<run> spans;
    flatten(block.inlines, look, settings, spans);
    std::vector<line> rows = wrap(spans, std::max(1, width - prefix_width));
    if (rows.empty()) rows.emplace_back();

    int widest = 0;
    for (const auto& row : rows) widest = std::max(widest, display_width(row));

    rows = prefix_rows(std::move(rows), single_run(hashes, style{.dim = true}),
                       single_run(std::string(prefix_width, ' ')));

    if (block.level <= 2) {
        int rule_length = std::min(width, prefix_width + widest);
        rows.push_back(single_run(repeat("─", rule_length), style{.dim = true}));
    }
    return rows;
}

inline std::vector<line> render_code_block(const block_node& block) {
    line bar = single_run("│ ", style{.foreground = color::bright_black});
    std::vector<line> rows;
    if (!block.info.empty()) {
        line row = bar;
        append(row, block.info, style{.italic = true, .dim = true});
        rows.push_back(std::move(row));
    }
    std::vector<std::string> lines = split(block.literal, '\n');
    // Drop a lone trailing empty line that a final newline would produce.
    if (lines.size() > 1 && lines.back().empty()) lines.pop_back();

    // The info string's first word is the language; highlight against it.
    std::string language;
    for (char c : block.info) {
        if (c == ' ' || c == '\t') break;
        language += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const auto& highlighted : highlight_code(lines, language)) {
        line row = bar;
        extend(row, highlighted);
        rows.push_back(std::move(row));
    }
    return rows;
}

inline std::vector<line> render_list(const block_node& block, int width,
                                     const options& settings) {
    int count = static_cast<int>(block.children.size());
    int number_width =
        block.ordered ? digit_count(block.start + std::max(0, count - 1)) : 1;
    std::vector<line> rows;
    for (int index = 0; index < count; ++index) {
        const block_node& item = block.children[index];
        std::string marker;
        style marker_look;
        if (item.task_state >= 0) {
            marker = item.task_state ? "☑ " : "☐ ";
            marker_look.foreground =
                item.task_state ? color::bright_green : color::none;
        } else if (block.ordered) {
            std::string number = std::to_string(block.start + index);
            number = std::string(number_width - static_cast<int>(number.size()), ' ') +
                     number;
            marker = number + ". ";
            marker_look.foreground = color::bright_cyan;
        } else {
            marker = "• ";
            marker_look.foreground = color::bright_cyan;
        }
        int marker_width = display_width(marker);
        std::vector<line> content = render_blocks(
            item.children, std::max(1, width - marker_width), false, settings);
        std::vector<line> item_rows =
            prefix_rows(std::move(content), single_run(marker, marker_look),
                        single_run(std::string(marker_width, ' ')));
        rows.insert(rows.end(), item_rows.begin(), item_rows.end());
    }
    return rows;
}

inline std::vector<line> render_table_block(const block_node& block, int width,
                                            const options& settings) {
    std::vector<std::vector<line>> cells;
    for (std::size_t r = 0; r < block.rows.size(); ++r) {
        style base;
        if (r == 0) base.bold = true;
        std::vector<line> row;
        for (const auto& cell : block.rows[r])
            row.push_back(inline_row(cell, base, settings));
        cells.push_back(std::move(row));
    }
    return render_table(cells, block.alignments, width);
}

inline std::vector<line> render_block(const block_node& block, int width,
                                      const options& settings) {
    switch (block.kind) {
        case block_kind::document:
            return render_blocks(block.children, width, true, settings);
        case block_kind::paragraph: {
            std::vector<run> spans;
            flatten(block.inlines, style{}, settings, spans);
            std::vector<line> rows = wrap(spans, width);
            if (rows.empty()) rows.emplace_back();
            return rows;
        }
        case block_kind::heading:
            return render_heading(block, width, settings);
        case block_kind::code_block:
            return render_code_block(block);
        case block_kind::block_quote: {
            std::vector<line> inner =
                render_blocks(block.children, std::max(1, width - 2), true, settings);
            line bar = single_run("▌ ", style{.foreground = color::bright_black});
            return prefix_rows(std::move(inner), bar, bar);
        }
        case block_kind::list:
            return render_list(block, width, settings);
        case block_kind::thematic_break:
            return {single_run(repeat("─", width), style{.dim = true})};
        case block_kind::table:
            return render_table_block(block, width, settings);
        case block_kind::list_item:
            return render_blocks(block.children, width, false, settings);
    }
    return {line{}};
}

// ---- ANSI ------------------------------------------------------------------

inline constexpr std::string_view kReset = "\x1b[0m";
inline constexpr std::string_view kHyperlinkClose = "\x1b]8;;\x1b\\";

// SGR color code for the foreground; the background is this plus ten.
inline int color_code(color hue) {
    switch (hue) {
        case color::none: return -1;
        case color::black: return 30;
        case color::red: return 31;
        case color::green: return 32;
        case color::yellow: return 33;
        case color::blue: return 34;
        case color::magenta: return 35;
        case color::cyan: return 36;
        case color::white: return 37;
        case color::bright_black: return 90;
        case color::bright_red: return 91;
        case color::bright_green: return 92;
        case color::bright_yellow: return 93;
        case color::bright_blue: return 94;
        case color::bright_magenta: return 95;
        case color::bright_cyan: return 96;
        case color::bright_white: return 97;
    }
    return -1;
}

// The SGR escape sequence that turns `look` on, or "" for a plain look.
inline std::string graphic_rendition(const style& look) {
    if (look.plain()) return {};
    std::string out = "\x1b[";
    bool first = true;
    auto add = [&](int code) {
        if (!first) out += ';';
        out += std::to_string(code);
        first = false;
    };
    if (look.bold) add(1);
    if (look.dim) add(2);
    if (look.italic) add(3);
    if (look.underline) add(4);
    if (look.strike) add(9);
    if (look.foreground != color::none) add(color_code(look.foreground));
    if (look.background != color::none) add(color_code(look.background) + 10);
    out += 'm';
    return out;
}

// Open an OSC 8 clickable hyperlink. Terminals that don't understand OSC 8
// silently drop the sequences and show the text only.
inline std::string hyperlink_open(const std::string& url) {
    return "\x1b]8;;" + url + "\x1b\\";
}

}  // namespace detail

// ---- Public entry points ---------------------------------------------------

inline std::vector<line> layout(std::string_view source, const options& settings) {
    std::vector<detail::block_node> blocks = detail::parse_document(source);
    if (blocks.empty()) return {};
    return detail::render_blocks(blocks, settings.width, true, settings);
}

inline std::string ansi(const line& row, bool colored) {
    std::string out;
    for (const run& piece : row) {
        if (!colored) {
            out += piece.text;
            continue;
        }
        std::string on = detail::graphic_rendition(piece.look);
        bool linked = !piece.look.link.empty();
        if (linked) out += detail::hyperlink_open(piece.look.link);
        if (piece.truncated) {
            // md cuts a table cell after its escape codes were written: the
            // cut keeps the run's opening codes, adds a reset even to plain
            // text, and leaves a hyperlink open.
            out += on;
            out += piece.text;
            out += detail::kReset;
            continue;
        }
        if (on.empty()) {
            out += piece.text;
        } else {
            out += on;
            out += piece.text;
            out += detail::kReset;
        }
        if (linked) out += detail::kHyperlinkClose;
    }
    return out;
}

inline std::string ansi(const std::vector<line>& rows, bool colored) {
    std::string out;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (i) out += '\n';
        out += ansi(rows[i], colored);
    }
    return out;
}

}  // namespace one::markdown
