# one::markdown and md

uione's Markdown: a single header, `one/markdown.hpp`, that lays Markdown out for
terminals and reads it as a tree for windows, and `md`, a small, dependency-free command-line tool that renders Markdown `.md` files
formatted properly in your terminal — headings, lists, tables, code blocks,
blockquotes, and inline styling, all wrapped to your terminal width.

Written in modern C++ (C++23), no third-party libraries. The renderer is also
a single-header library, `one/markdown.hpp`, for programs that draw Markdown their
own way, as uione's desktop and terminal apps do (see [As a library](#as-a-library)).

## Features

- **Headings** with per-level color and an underline rule for H1/H2
- **Inline styles**: `**bold**`, `_italic_`, `***both***`, `~~strike~~`,
  `` `code` ``, backslash escapes
- **Links** as clickable [OSC 8](https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda)
  hyperlinks (falling back to `text (url)` when piped)
- **Lists**: ordered, unordered, nested, and `- [ ]` / `- [x]` task lists
- **Blockquotes**, including nested ones, with a colored bar
- **Fenced code blocks** with a gutter, language label, and **syntax
  highlighting** for C, C++, Python, JavaScript/TypeScript, Rust, Go, shell, and
  JSON (unknown languages still get string/number highlighting)
- **GFM tables** with box-drawing borders, per-column alignment, and correct
  display-width measurement for CJK/emoji — wide tables shrink to fit
- **Word-wrapping** to the terminal width, ANSI-aware so styles never leak
- Auto-disables color and wraps to 80 columns when output is piped

## Build & install

Requires a C++23 compiler and CMake. No third-party libraries.

```sh
cmake -S markdown -B markdown/build && cmake --build markdown/build
markdown/build/md README.md
markdown/build/layout_test markdown/test/sample.md
```

A CMake project inside uione uses the header through the `one::markdown` target.

## Usage

```
md [OPTIONS] [FILE]

  With no FILE (or "-"), reads Markdown from standard input.

OPTIONS:
  -w, --width N     Wrap to N columns (default: terminal width)
      --color       Force ANSI color/hyperlinks on
      --no-color    Disable ANSI color/hyperlinks
  -h, --help        Show this help
  -v, --version     Show version
```

### Examples

```sh
md README.md                 # render a file
md --width 100 notes.md      # fixed wrap width
cat CHANGELOG.md | md         # from a pipe
md test/sample.md            # a feature showcase
```

## As a library

`include/one/markdown.hpp` is the whole renderer: one header, everything `inline`,
namespace `one::markdown`. `parse()` gives the document as a tree of blocks holding
styled inline spans, for a program that lays it out itself. `layout()` parses Markdown into rows that are already
wrapped and syntax-highlighted. Each row is a list of runs — UTF-8 text in one
style, with no escape codes — so any backend can draw it. `ansi()` is the
backend `md` uses:

```cpp
#include <iostream>
#include <one/markdown.hpp>

int main() {
    one::markdown::options settings{.width = 60};
    auto rows = one::markdown::layout("# Hello\n\nSome **bold** text.", settings);
    std::cout << one::markdown::ansi(rows, true) << "\n";
}
```

A curses program walks the same rows and maps each run's style onto its own
attributes:

```cpp
for (const one::markdown::line& row : rows) {
    for (const one::markdown::run& piece : row) {
        int attributes = (piece.look.bold ? A_BOLD : 0) |
                         (piece.look.underline ? A_UNDERLINE : 0);
        attron(attributes);
        addstr(piece.text.c_str());
        attroff(attributes);
    }
    addch('\n');
}
```

A run's `look` holds `bold`, `italic`, `underline`, `strike`, `dim`, a
`foreground` and `background` from the 16-color palette, and `link`, the URL
when the run is a hyperlink. Set `hyperlinks = false` in the options when links
can't be clicked, and the layout writes each URL after its text.
`one::markdown::display_width()` measures text in terminal columns.

## Project layout

| File | Responsibility |
|------|----------------|
| `include/markdown.hpp` | The library: parsing, highlighting, tables, wrapping, ANSI |
| `src/main.cpp` | The `md` CLI: options, terminal width and TTY detection |
| `test/layout_test.cpp` | Checks rows fit the width and hold no escape codes |
| `test/sample.md` | A feature showcase |

The header reads top to bottom: the public types, Unicode width, the document
tree, the inline and block parsers, syntax highlighting, tables, layout, and
the ANSI backend.

## License

See [LICENSE](LICENSE).
