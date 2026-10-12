// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: GPL-3.0-only

// layout_test.cpp — checks that layout() keeps its promises on a document
// (test/sample.md by default): every row fits the width, and no run's text
// carries an escape code or a newline, so any backend can draw it.
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "one/markdown.hpp"

int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : "test/sample.md";
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "layout_test: cannot open '" << path << "'\n";
        return 1;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string source = contents.str();

    int failures = 0;
    int checked = 0;
    // Fenced code never wraps, and the sample's longest code row is 49 columns
    // with its gutter, so narrower widths would overflow by design.
    for (int width : {50, 60, 80, 100, 120}) {
        for (bool hyperlinks : {true, false}) {
            one::markdown::options settings{.width = width, .hyperlinks = hyperlinks};
            std::vector<one::markdown::line> rows = one::markdown::layout(source, settings);
            if (rows.empty()) {
                std::cerr << "width " << width << ": no rows\n";
                ++failures;
            }
            for (std::size_t r = 0; r < rows.size(); ++r) {
                ++checked;
                int columns = one::markdown::display_width(rows[r]);
                if (columns > width) {
                    std::cerr << "width " << width << ", row " << r << ": "
                              << columns << " columns: "
                              << one::markdown::ansi(rows[r], false) << "\n";
                    ++failures;
                }
                for (const one::markdown::run& piece : rows[r]) {
                    if (piece.text.find_first_of("\x1b\n") != std::string::npos) {
                        std::cerr << "width " << width << ", row " << r
                                  << ": escape or newline in run text\n";
                        ++failures;
                    }
                }
            }
        }
    }

    if (failures) {
        std::cerr << "layout_test: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "layout_test: " << checked << " rows checked in " << path
              << ", all fit with no escape codes\n";
    return 0;
}
