// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// main.cpp — the `md` command-line entry point, a thin user of markdown.hpp.
//
// The library lays Markdown out; this file only reads input, probes the
// terminal, and prints the layout as ANSI.
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "one/markdown.hpp"

namespace {

constexpr const char* kVersion = "md 0.1.0";

void print_help() {
    std::cout <<
        R"(md — render Markdown in your terminal

USAGE:
    md [OPTIONS] [FILE]

    With no FILE (or "-"), reads Markdown from standard input.

OPTIONS:
    -w, --width N     Wrap to N columns (default: terminal width)
        --color       Force ANSI color/hyperlinks on
        --no-color    Disable ANSI color/hyperlinks
    -h, --help        Show this help
    -v, --version     Show version

EXAMPLES:
    md README.md
    md --width 100 notes.md
    cat CHANGELOG.md | md -
)";
}

std::string read_stream(std::istream& input) {
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

// Columns available in the terminal: TIOCGWINSZ, then $COLUMNS, then 80.
int terminal_width() {
    struct winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0)
        return size.ws_col;
    if (const char* columns = std::getenv("COLUMNS")) {
        try {
            int count = std::stoi(columns);
            if (count > 0) return count;
        } catch (...) {
        }
    }
    return 80;
}

// Is standard output connected to a terminal?
bool output_is_terminal() {
    return isatty(STDOUT_FILENO) == 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string file;
    int width = 0;         // 0 → auto
    int color_mode = 0;    // 0 auto, 1 force on, -1 force off

    for (int i = 1; i < argc; ++i) {
        std::string_view argument = argv[i];
        if (argument == "-h" || argument == "--help") {
            print_help();
            return 0;
        }
        if (argument == "-v" || argument == "--version") {
            std::cout << kVersion << "\n";
            return 0;
        }
        if (argument == "--color") {
            color_mode = 1;
        } else if (argument == "--no-color") {
            color_mode = -1;
        } else if (argument == "-w" || argument == "--width") {
            if (i + 1 >= argc) {
                std::cerr << "md: " << argument << " requires an argument\n";
                return 2;
            }
            try {
                width = std::stoi(argv[++i]);
            } catch (...) {
                std::cerr << "md: invalid width\n";
                return 2;
            }
        } else if (argument.starts_with("--width=")) {
            try {
                width = std::stoi(std::string(argument.substr(8)));
            } catch (...) {
                std::cerr << "md: invalid width\n";
                return 2;
            }
        } else if (argument == "-") {
            file.clear();
        } else if (!argument.empty() && argument[0] == '-') {
            std::cerr << "md: unknown option '" << argument << "' (try --help)\n";
            return 2;
        } else {
            file = argument;
        }
    }

    std::string source;
    if (file.empty()) {
        source = read_stream(std::cin);
    } else {
        std::ifstream input(file, std::ios::binary);
        if (!input) {
            std::cerr << "md: cannot open '" << file << "': "
                      << std::strerror(errno) << "\n";
            return 1;
        }
        source = read_stream(input);
    }

    bool terminal = output_is_terminal();
    bool colored = color_mode == 1 || (color_mode == 0 && terminal);
    if (width <= 0) width = terminal ? terminal_width() : 80;

    // Without color there are no clickable links, so the layout spells out
    // each URL after its text.
    one::markdown::options settings{.width = width, .hyperlinks = colored};
    std::string output = one::markdown::ansi(one::markdown::layout(source, settings), colored);
    if (!output.empty()) std::cout << output << "\n";
    return 0;
}
