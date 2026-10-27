// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The kinds a paper knows, by name.

#include <map>

#include "kinds.hpp"

namespace one::desktop::runtime::paper_kinds {

	const kind& kind_of(const std::string& name) {
		static const std::map<std::string, const kind*, std::less<>> kinds{
		    {"title", &title_kind()}, {"subtitle", &subtitle_kind()}, {"heading", &heading_kind()}, {"text", &text_kind()},
		    {"quote", &quote_kind()}, {"math", &math_kind()},         {"code", &code_kind()},
		};
		auto found = kinds.find(name);
		return found == kinds.end() ? text_kind() : *found->second;
	}

	std::string family_of(const std::string& face, const desktop::theme& t) {
		if (face == "book") return "Literata";
		if (face == "lyric") return "Fraunces";
		if (face == "typewriter") return t.code_family;
		return t.text_family;
	}

}  // namespace one::desktop::runtime::paper_kinds
