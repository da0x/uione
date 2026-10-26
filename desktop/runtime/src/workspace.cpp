// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A workspace, laid out as a drawing app is, as @uione/react's is on the web: a bar,
// a toolbar of named groups whose buttons are tools, the work beside what's at its
// side, and a status line with its messages; toggles, undo and redo, full screen,
// and the keys that run what's on a line.

#include <algorithm>
#include <cctype>
#include <map>
#include <thread>

#include "one/desktop/folder.hpp"
#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	namespace {

		// studio::layer::place, said as words: "place".
		std::string step_words(const std::string& command) {
			std::string last = command.substr(command.rfind("::") == std::string::npos ? 0 : command.rfind("::") + 2);
			std::ranges::replace(last, '_', ' ');
			return last.empty() ? "the last change" : last;
		}

		std::vector<std::string> split_chord(std::string_view chord) {
			std::vector<std::string> parts;
			for (std::size_t from = 0; from <= chord.size();) {
				std::size_t plus = std::min(chord.find('+', from), chord.size());
				parts.emplace_back(chord.substr(from, plus - from));
				from = plus + 1;
			}
			return parts;
		}

		// A line drawn as a tool in a toolbar's group: large, its icon over its words;
		// small, its icon alone, its words and its key said when it's pointed at.
		desktop::view tool(page& p, const std::string& icon, const std::string& words, const std::vector<std::string>& keys, std::function<void()> run, bool on) {
			bool small = p.in_group().value_or(false);
			return desktop::tool_button(desktop::has_icon(icon) ? icon : "", small ? hint_of(words, keys) : words, std::move(run), {.large = !small, .on = on});
		}

	}  // namespace

	bool pressed(const key_event& e, std::string_view chord) {
		auto parts = split_chord(chord);
		std::string key = parts.back();
		parts.pop_back();
		auto holds = [&](std::string_view m) { return std::ranges::find(parts, m) != parts.end(); };
		if (e.control != holds("ctrl") || e.alt != holds("alt")) return false;
		static const std::map<std::string, std::uint32_t, std::less<>> named{
		    {"escape", keys::escape}, {"enter", keys::enter},     {"tab", keys::tab},       {"space", keys::space},   {"backspace", keys::backspace},
		    {"delete", keys::delete_key}, {"insert", 0xff63},     {"home", keys::home},     {"end", keys::end},       {"page_up", keys::page_up},
		    {"page_down", keys::page_down}, {"left", keys::left}, {"right", keys::right},   {"up", keys::up},         {"down", keys::down},
		    {"minus", '-'},           {"equal", '='},             {"plus", '+'},            {"comma", ','},           {"period", '.'},
		    {"slash", '/'}};
		std::uint32_t wanted = 0;
		if (auto found = named.find(key); found != named.end()) wanted = found->second;
		else if (key.size() > 1 && key[0] == 'f') wanted = keys::f11 - 11 + static_cast<std::uint32_t>(std::stoi(key.substr(1)));
		else wanted = static_cast<std::uint32_t>(static_cast<unsigned char>(key[0]));
		std::uint32_t symbol = e.symbol < 0x80 ? static_cast<std::uint32_t>(std::tolower(static_cast<int>(e.symbol))) : e.symbol;
		if (symbol != wanted) return false;
		// A shifted key that's a character, like + or Z, says it's shifted by what it is.
		bool letter = wanted < 0x80 && std::isalnum(static_cast<int>(wanted));
		return wanted >= 0x80 || letter || wanted == ' ' ? e.shift == holds("shift") : true;
	}

	std::string key_shown(std::string_view chord) {
		static const std::map<std::string, std::string, std::less<>> names{{"page_up", "Page Up"}, {"page_down", "Page Down"}, {"minus", "-"}, {"equal", "="},
		                                                                   {"plus", "+"},         {"comma", ","},              {"period", "."}, {"slash", "/"}};
		std::string out;
		for (auto& part : split_chord(chord)) {
			if (!out.empty()) out += "+";
			if (auto found = names.find(part); found != names.end()) out += found->second;
			else {
				part[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
				out += part;
			}
		}
		return out;
	}

	std::string hint_of(const std::string& words, const std::vector<std::string>& keys) {
		return keys.empty() ? words : words + " (" + key_shown(keys.front()) + ")";
	}

	desktop::view along(std::vector<desktop::view> all) {
		std::erase_if(all, [](const desktop::view& v) { return !v.get(); });
		if (all.empty()) return {};
		return desktop::row(std::move(all)).spacing(6).align(align::center);
	}

	desktop::view workspace(page& p, std::vector<region> regions) {
		p.fill();
		const auto& t = current_theme();
		std::map<std::string, desktop::view> by;
		for (auto& r : regions) {
			if (r.inside.get()) by[r.name] = std::move(r.inside);
		}
		auto taken = [&](const std::string& name) -> desktop::view {
			auto found = by.find(name);
			return found == by.end() ? desktop::view() : std::move(found->second);
		};
		// Filling the screen, it's only its work, like a picture on a wall.
		if (fullscreen()) return taken("main").grow();
		std::vector<desktop::view> parts;
		for (const char* across : {"bar", "toolbar"}) {
			if (auto v = taken(across); v.get()) {
				// Filling the window, its bar says the app's name and switches light and
				// dark, as the site's would.
				if (std::string_view(across) == "bar") {
					auto mark = t.heading(20);
					mark.line_height = 1.1f;
					std::vector<desktop::view> bar;
					if (std::string name = app_name(); !name.empty()) bar.push_back(desktop::text(std::move(name), mark).padding(edges(0, 8, 0, 0)));
					bar.push_back(std::move(v));
					bar.push_back(desktop::spacer());
					if (offers_dark()) bar.push_back(desktop::tool_button(dark() ? "sun" : "moon", dark() ? "Light" : "Dark", [] { dark(!dark()); }));
					v = desktop::row(std::move(bar)).spacing(6).align(align::center);
				}
				parts.push_back(std::move(v).padding(edges(6, 12)).background({.fill = t.surface}));
				parts.push_back(desktop::divider());
			}
		}
		std::vector<desktop::view> middle;
		middle.push_back(taken("main").padding(edges(12, 12)).grow());
		if (auto side = taken("side"); side.get()) middle.push_back(desktop::scroll(std::move(side).padding(edges(12, 12))).width(288).grow(0).background({.fill = t.surface}));
		parts.push_back(desktop::row(std::move(middle)).align(align::stretch).grow());
		if (auto status = taken("status"); status.get()) {
			parts.push_back(desktop::divider());
			parts.push_back(std::move(status).padding(edges(4, 12)).background({.fill = t.surface}));
		}
		return desktop::column(std::move(parts)).background({.fill = t.sunken}).grow();
	}

	desktop::view group(page& p, std::string name, bool small, bool at_end, std::function<std::vector<desktop::view>()> build) {
		const auto& t = current_theme();
		auto was = p.in_group();
		p.in_group(small);
		auto tools = build();
		p.in_group(was);
		std::erase_if(tools, [](const desktop::view& v) { return !v.get(); });
		if (tools.empty()) return {};
		desktop::view laid;
		if (small) {
			// Two rows of icons, a column at a time.
			std::vector<desktop::view> columns;
			for (std::size_t i = 0; i < tools.size(); i += 2) {
				if (i + 1 < tools.size()) columns.push_back(desktop::column(std::move(tools[i]), std::move(tools[i + 1])).spacing(2));
				else columns.push_back(desktop::column(std::move(tools[i])));
			}
			laid = desktop::row(std::move(columns)).spacing(2);
		} else {
			laid = desktop::row(std::move(tools)).spacing(2);
		}
		auto label = t.quiet();
		label.size -= 1;
		auto drawn = desktop::column(std::move(laid), desktop::text(std::move(name), label)).spacing(2).align(align::center).padding(edges(0, 6));
		if (!at_end) return drawn;
		return desktop::row(desktop::spacer(), std::move(drawn)).grow();
	}

	desktop::view toggle(page& p, std::string name, std::string label, std::string icon, std::vector<std::string> keys) {
		bool on = p.toggled(name);
		auto flip = [&p, name] { p.toggle(name); };
		for (const auto& k : keys) p.on_key(k, flip);
		if (p.in_group()) return tool(p, icon, label, keys, flip, on);
		return desktop::tool_button(desktop::has_icon(icon) ? icon : "", hint_of(label, keys), flip, {.on = on});
	}

	desktop::view history(page& p, bool redo, std::string label, std::string icon, std::vector<std::string> keys) {
		std::string command = redo ? "one::redo" : "one::undo";
		auto take = [&p, command, redo] {
			if (p.running(command)) return;
			p.run(command, value::map(), [told = p.told(), redo](const answer& a) {
				if (a) told.set(std::string(redo ? "Redid " : "Undid ") + step_words(*a) + ".");
			});
		};
		for (const auto& k : keys) p.on_key(k, take);
		if (label.empty()) label = redo ? "Redo" : "Undo";
		if (p.in_group()) return tool(p, icon, label, keys, take, false);
		return desktop::tool_button(desktop::has_icon(icon) ? icon : "", hint_of(label, keys), take);
	}

	desktop::view messages(page& p) {
		const auto& t = current_theme();
		std::string wrong = *p.problem();
		if (!wrong.empty()) {
			auto s = t.quiet();
			s.color = t.danger;
			return desktop::text(std::move(wrong), s);
		}
		std::string told = *p.told();
		if (told.empty()) return {};
		// While what's said is a file just saved, the file shown in its folder by a press.
		auto [about, file] = p.kept<std::pair<std::string, std::string>>("said of a file").peek();
		if (file.empty() || about != told) return desktop::text(std::move(told), t.quiet());
		auto show = [problem = p.problem(), file] {
			std::thread([problem, file] {
				if (auto shown = desktop::show_in_folder(file); !shown) post([problem, why = shown.error()] { problem.set(why); });
			}).detach();
		};
		std::vector<desktop::view> said;
		said.push_back(desktop::text(std::move(told), t.quiet()));
		said.push_back(desktop::tool_button("", "Show in folder", std::move(show)));
		return along(std::move(said));
	}

	desktop::view on_open(page& p, bool ready, std::vector<opening> lines) {
		if (!opening_now(p, ready, lines)) return {};
		return desktop::text("Opening…", current_theme().quiet());
	}

	double screen_width() {
		auto s = screen_size();
		return s && s->width > 0 ? s->width : 1920;
	}

	double screen_height() {
		auto s = screen_size();
		return s && s->height > 0 ? s->height : 1080;
	}

	desktop::view full_screen(page& p, std::string label, std::string icon, std::vector<std::string> keys) {
		auto flip = [] { fullscreen(!fullscreen()); };
		for (const auto& k : keys) p.on_key(k, flip);
		if (label.empty()) label = "Full screen";
		if (p.in_group()) return tool(p, icon, label, keys, flip, fullscreen());
		return desktop::tool_button(desktop::has_icon(icon) ? icon : "", hint_of(label, keys), flip, {.on = fullscreen()});
	}

}  // namespace one::desktop::runtime
