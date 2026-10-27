// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::math held to tests/cases.tsv, every equation there read as the tree beside it or
// turned down for the reason beside it; and to tests/layouts.txt, every one that reads
// set as it was when that was written, 20 pixels to the em, as it's shown on its own
// lines. ONE_MATH_LAYOUTS=write writes it again, to be looked at before it's kept.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "one/math.hpp"
#include "one/math_layout.hpp"

int main() {
	std::ifstream in(ONE_MATH_TESTS "/cases.tsv");
	std::string line, layouts;
	int count = 0, failed = 0;
	while (std::getline(in, line)) {
		if (line.empty() || line.starts_with("#")) continue;
		auto tab = line.find('\t');
		if (tab == std::string::npos) continue;
		std::string written = line.substr(0, tab), wanted = line.substr(tab + 1);
		++count;
		auto read = one::math::parse(written);
		std::string got = read ? one::math::written(*read) : "! " + read.error().says;
		bool same = wanted.starts_with("! ") ? got.starts_with(wanted) : got == wanted;
		if (!same) {
			++failed;
			std::cout << "FAIL " << written << "\n  want " << wanted << "\n  got  " << got << "\n";
		}
		if (read) layouts += "== " + written + "\n" + one::math::written(one::math::lay_out(*read, 20, true));
	}
	std::cout << count - failed << " of " << count << " read as they should\n";
	std::string file = ONE_MATH_TESTS "/layouts.txt";
	if (const char* mode = std::getenv("ONE_MATH_LAYOUTS"); mode && std::string(mode) == "write") {
		std::ofstream(file) << layouts;
		std::cout << "wrote " << file << "\n";
	} else {
		std::ifstream kept(file);
		std::stringstream was;
		was << kept.rdbuf();
		if (was.str() != layouts) {
			++failed;
			std::cout << "FAIL the layouts differ from " << file << "\n";
		} else {
			std::cout << "every layout as kept\n";
		}
	}
	return failed == 0 && count > 0 ? 0 : 1;
}
