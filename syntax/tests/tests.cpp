// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::syntax held to tests/languages.tsv, the table it keeps being that file but its
// comments; and to tests/cases.tsv, everything written there colored as it says.

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "one/syntax.hpp"

namespace {

	// \n, \t and \\ as C writes them; any other \ is itself.
	std::string unescaped(const std::string& s) {
		std::string out;
		for (std::size_t i = 0; i < s.size(); ++i) {
			if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == 'n' || s[i + 1] == 't' || s[i + 1] == '\\')) {
				out += s[i + 1] == 'n' ? '\n' : s[i + 1] == 't' ? '\t' : '\\';
				++i;
			} else {
				out += s[i];
			}
		}
		return out;
	}

	std::vector<std::string> fields(const std::string& line) {
		std::vector<std::string> out;
		std::stringstream in(line);
		std::string f;
		while (std::getline(in, f, '\t')) out.push_back(f);
		return out;
	}

}  // namespace

int main() {
	int count = 0, failed = 0;
	std::ifstream languages(ONE_SYNTAX_TESTS "/languages.tsv");
	std::string line, table;
	while (std::getline(languages, line)) {
		if (!line.empty() && !line.starts_with("#")) table += line + "\n";
	}
	++count;
	if (table != one::syntax::table) {
		++failed;
		std::cout << "FAIL the table syntax.hpp keeps isn't tests/languages.tsv; node syntax/write-table.mjs copies it\n";
	}
	std::ifstream cases(ONE_SYNTAX_TESTS "/cases.tsv");
	while (std::getline(cases, line)) {
		if (line.empty() || line.starts_with("#")) continue;
		auto f = fields(line);
		if (f.size() < 2) continue;
		++count;
		if (f[0] == "find") {
			const auto* found = one::syntax::find(f[1]);
			std::string got = found ? found->name : "-";
			if (f.size() < 3 || got != f[2]) {
				++failed;
				std::cout << "FAIL find " << f[1] << "\n  got  " << got << "\n";
			}
			continue;
		}
		const auto* language = one::syntax::find(f[0]);
		if (!language) {
			++failed;
			std::cout << "FAIL there's no language " << f[0] << "\n";
			continue;
		}
		std::string written = unescaped(f[1]);
		std::vector<std::string> wanted, got;
		for (std::size_t i = 2; i < f.size(); ++i) wanted.push_back(unescaped(f[i]));
		for (const auto& s : one::syntax::highlight(written, *language)) got.push_back(std::string(one::syntax::name(s.kind)) + ":" + written.substr(s.from, s.to - s.from));
		if (got != wanted) {
			++failed;
			std::cout << "FAIL " << f[0] << " " << f[1] << "\n  got ";
			for (const auto& g : got) std::cout << " [" << g << "]";
			std::cout << "\n";
		}
	}
	std::cout << count - failed << " of " << count << " colored as they should be\n";
	return failed == 0 ? 0 : 1;
}
