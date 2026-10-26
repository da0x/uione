// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A workspace in a terminal, as the desktop's: its bar, its toolbar of named groups,
// the work beside what's at its side, and a status line with its messages; toggles,
// undo and redo, and the keys that run what's on a line.

#include <algorithm>
#include <cctype>
#include <map>

#include "one/terminal/runtime/components.hpp"

namespace one::terminal::runtime {

	namespace {

		std::string step_words(const std::string& command) {
			std::string last = command.substr(command.rfind("::") == std::string::npos ? 0 : command.rfind("::") + 2);
			std::ranges::replace(last, '_', ' ');
			return last.empty() ? "the last change" : last;
		}

	}  // namespace

	bool pressed(const key_press& e, std::string_view chord) {
		std::vector<std::string> parts;
		for (std::size_t from = 0; from <= chord.size();) {
			std::size_t plus = std::min(chord.find('+', from), chord.size());
			parts.emplace_back(chord.substr(from, plus - from));
			from = plus + 1;
		}
		std::string key = parts.back();
		parts.pop_back();
		bool ctrl = std::ranges::find(parts, std::string("ctrl")) != parts.end(), alt = std::ranges::find(parts, std::string("alt")) != parts.end();
		static const std::map<std::string, int, std::less<>> named{{"escape", keys::escape},       {"enter", keys::enter},         {"tab", terminal::keys::tab},
		                                                           {"backspace", keys::backspace}, {"delete", terminal::keys::delete_key}, {"home", keys::home},
		                                                           {"end", keys::end},             {"page_up", keys::page_up},     {"page_down", keys::page_down},
		                                                           {"left", keys::left},           {"right", keys::right},         {"up", keys::up},
		                                                           {"down", keys::down}};
		if (auto found = named.find(key); found != named.end()) return !ctrl && e.alt == alt && (e.code == found->second || (key == "enter" && e.code == keys::newline));
		if (key.size() > 1 && key[0] == 'f') return !ctrl && e.code == 0410 + std::stoi(key.substr(1));  // ncurses' function keys
		static const std::map<std::string, char, std::less<>> characters{{"space", ' '}, {"minus", '-'}, {"equal", '='}, {"plus", '+'},
		                                                                 {"comma", ','}, {"period", '.'}, {"slash", '/'}};
		char wanted = key.size() == 1 ? key[0] : characters.contains(key) ? characters.at(key) : '\0';
		if (wanted == '\0' || e.alt != alt) return false;
		// Control and a letter comes as the letter's control code, or with control said.
		if (ctrl) return std::isalpha(static_cast<unsigned char>(wanted)) && (e.code == std::tolower(wanted) - 'a' + 1 || (e.control && e.text.size() == 1 && std::tolower(e.text[0]) == wanted));
		return !e.control && e.text.size() == 1 && std::tolower(static_cast<unsigned char>(e.text[0])) == wanted;
	}

	terminal::view along(std::vector<terminal::view> all) {
		std::erase_if(all, [](const terminal::view& v) { return !v.get(); });
		if (all.empty()) return {};
		return terminal::flow(std::move(all)).spacing(2);
	}

	terminal::view workspace(page&, std::vector<region> regions) {
		std::map<std::string, terminal::view> by;
		for (auto& r : regions) {
			if (r.inside.get()) by[r.name] = std::move(r.inside);
		}
		auto taken = [&](const std::string& name) -> terminal::view {
			auto found = by.find(name);
			return found == by.end() ? terminal::view() : std::move(found->second);
		};
		std::vector<terminal::view> parts;
		for (const char* across : {"bar", "toolbar"}) {
			if (auto v = taken(across); v.get()) {
				parts.push_back(std::move(v));
				parts.push_back(terminal::divider());
			}
		}
		auto main = taken("main"), side = taken("side");
		if (main.get() && side.get()) parts.push_back(terminal::beside(std::move(main), std::move(side)));
		else if (main.get()) parts.push_back(std::move(main));
		else if (side.get()) parts.push_back(std::move(side));
		if (auto status = taken("status"); status.get()) {
			parts.push_back(terminal::divider());
			parts.push_back(std::move(status));
		}
		return terminal::column(std::move(parts));
	}

	terminal::view group(page& p, std::string name, bool small, bool, std::function<std::vector<terminal::view>()> build) {
		auto was = p.in_group();
		p.in_group(small);
		auto tools = build();
		p.in_group(was);
		std::erase_if(tools, [](const terminal::view& v) { return !v.get(); });
		if (tools.empty()) return {};
		tools.insert(tools.begin(), terminal::text(name + ":", dim()));
		return terminal::flow(std::move(tools)).spacing(1);
	}

	terminal::view toggle(page& p, std::string name, std::string label, std::string, std::vector<std::string> keys) {
		auto flip = [&p, name] { p.toggle(name); };
		for (const auto& k : keys) p.on_key(k, flip);
		return terminal::button((p.toggled(name) ? "[x] " : "[ ] ") + label, flip);
	}

	terminal::view history(page& p, bool redo, std::string label, std::string, std::vector<std::string> keys) {
		std::string command = redo ? "one::redo" : "one::undo";
		auto take = [&p, command, redo] {
			if (p.running(command)) return;
			p.run(command, value::map(), [told = p.told(), redo](const answer& a) {
				if (a) told.set(std::string(redo ? "Redid " : "Undid ") + step_words(*a) + ".");
			});
		};
		for (const auto& k : keys) p.on_key(k, take);
		if (label.empty()) label = redo ? "Redo" : "Undo";
		return terminal::button(label, take);
	}

	terminal::view messages(page& p) {
		std::string wrong = *p.problem();
		if (!wrong.empty()) return terminal::text(std::move(wrong), {.foreground = color::red});
		std::string told = *p.told();
		if (told.empty()) return {};
		return terminal::text(std::move(told), dim());
	}

	terminal::view full_screen(page&, std::string, std::string, std::vector<std::string>) { return {}; }

}  // namespace one::terminal::runtime
