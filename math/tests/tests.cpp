// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::math held to tests/cases.tsv, every equation there read as the tree beside it or
// turned down for the reason beside it; and to tests/layouts.txt, every one that reads
// set as it was when that was written, 20 pixels to the em, as it's shown on its own
// lines. ONE_MATH_LAYOUTS=write writes it again, to be looked at before it's kept. And
// chem.hpp held to tests/chem.tsv, each chemical equation read, balanced or not, and
// weighed as it says. And diagram.hpp held to tests/diagrams.txt, each diagram there,
// after "== ", drawn as it was, or turned down as it was; ONE_MATH_LAYOUTS=write writes
// that again too.

#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "one/chem.hpp"
#include "one/diagram.hpp"
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
	std::ifstream chemical(ONE_MATH_TESTS "/chem.tsv");
	int reactions = 0, wrong = 0;
	while (std::getline(chemical, line)) {
		if (line.empty() || line.starts_with("#") || line.find('\t') == std::string::npos) continue;
		std::vector<std::string> fields;
		for (std::size_t from = 0;;) {
			auto tab = line.find('\t', from);
			fields.push_back(line.substr(from, tab == std::string::npos ? std::string::npos : tab - from));
			if (tab == std::string::npos) break;
			from = tab + 1;
		}
		++reactions;
		auto read = one::math::chem::parse(fields[0]);
		std::string got = read ? one::math::chem::written(*read) : "! " + read.error().says;
		bool same = fields[1].starts_with("! ") ? got.starts_with(fields[1]) : got == fields[1];
		if (same && read && fields.size() == 4) {
			auto mass = one::math::chem::molar_mass(*read);
			char weighed[32];
			std::snprintf(weighed, sizeof weighed, "%.2f", mass.value_or(0));
			same = one::math::chem::unbalanced(*read).value_or("balanced") == fields[2] && (mass ? std::string(weighed) : "-") == fields[3];
		}
		if (!same) {
			++wrong;
			std::cout << "FAIL " << fields[0] << "\n  want " << fields[1] << "\n  got  " << got << "\n";
		}
	}
	std::cout << reactions - wrong << " of " << reactions << " chemical equations read as they should\n";
	failed += wrong;
	std::string diagrams;
	std::ifstream drawn(ONE_MATH_TESTS "/diagrams.txt");
	while (std::getline(drawn, line)) {
		if (line.starts_with("#")) diagrams += line + "\n";
		if (!line.starts_with("== ")) continue;
		auto d = one::diagram::lay_out(line.substr(3));
		diagrams += line + "\n" + (d ? one::diagram::written(*d) : std::format("! {} at {}\n", d.error().says, d.error().at));
	}
	bool write = std::getenv("ONE_MATH_LAYOUTS") && std::string(std::getenv("ONE_MATH_LAYOUTS")) == "write";
	for (auto [name, now] : {std::pair{"layouts", &layouts}, std::pair{"diagrams", &diagrams}}) {
		std::string file = ONE_MATH_TESTS "/" + std::string(name) + ".txt";
		if (write) {
			std::ofstream(file) << *now;
			std::cout << "wrote " << file << "\n";
			continue;
		}
		std::ifstream kept(file);
		std::stringstream was;
		was << kept.rdbuf();
		if (was.str() != *now) {
			++failed;
			std::cout << "FAIL the " << name << " differ from " << file << "\n";
		} else {
			std::cout << "every one of the " << name << " as kept\n";
		}
	}
	return failed == 0 && count > 0 ? 0 : 1;
}
