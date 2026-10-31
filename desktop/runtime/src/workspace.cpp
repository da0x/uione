// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A workspace, laid out as a drawing app is, as @uione/react's is on the web: a bar,
// a toolbar of named groups whose buttons are tools, the work beside what's at its
// side, and a status line with its messages; toggles, undo and redo, full screen,
// and the keys that run what's on a line.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <set>
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
		desktop::view tool(page& p, const std::string& icon, const std::string& words, const std::vector<std::string>& keys, std::function<void()> run, bool on, bool off = false) {
			bool small = p.in_group().value_or(false);
			off = off || p.off();
			return desktop::tool_button(desktop::has_icon(icon) ? icon : "", small ? hint_of(words, keys) : words, std::move(run), {.large = !small, .on = on && !off, .off = off});
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

	namespace {
		// Whether a screen that's only its picture shows its bars and the pointer, until
		// when, and whether a wait to see is on its way: one at a time, however often
		// the pointer moves.
		state<bool> woken_now(true);
		std::chrono::steady_clock::time_point woken_until;
		bool waiting = false;
		// Whether the screen shown is one, which is all the wait is for, and whether the
		// one built before it was.
		bool shown_alone = false, was_alone = false;
		bool awake_before = true;  // as they were before the key being heard

		void wait_out() {
			if (waiting || !shown_alone || !posting()) return;
			waiting = true;
			std::thread([until = woken_until] {
				std::this_thread::sleep_until(until);
				post([] {
					waiting = false;
					if (std::chrono::steady_clock::now() >= woken_until) rest();
					else wait_out();
				});
			}).detach();
		}

		// What's over a picture alone: its bars at the top, its tools down the left, its
		// side down the right and its status line along the bottom, each as tall or wide
		// as it is, the picture under them all, filling the screen whether they're shown
		// or not, so it doesn't move when they come and go. What's pointed at between
		// them is the picture.
		class alone_element : public desktop::element {
		public:
			enum class edge { top, left, right, bottom };
			alone_element(desktop::view picture, std::vector<std::pair<edge, desktop::view>> over) {
				children.push_back(std::move(picture).take());
				for (auto& [at, v] : over) {
					edges_.push_back(at);
					children.push_back(std::move(v).take());
				}
				for (auto& c : children) c->parent = this;
			}
			void place_content(const desktop::rect& a) override {
				children[0]->place(a);
				float top = a.y, bottom = a.y + a.height;
				for (std::size_t i = 1; i < children.size(); ++i) {
					auto& c = *children[i];
					if (edges_[i - 1] == edge::top) {
						float h = c.measure(a.width).height;
						c.place({a.x, top, a.width, h});
						top += h;
					}
				}
				for (std::size_t i = children.size(); i-- > 1;) {
					auto& c = *children[i];
					if (edges_[i - 1] == edge::bottom) {
						float h = c.measure(a.width).height;
						bottom -= h;
						c.place({a.x, bottom, a.width, h});
					}
				}
				for (std::size_t i = 1; i < children.size(); ++i) {
					auto& c = *children[i];
					edge at = edges_[i - 1];
					if (at != edge::left && at != edge::right) continue;
					float w = std::min(a.width, c.measure(a.width).width);
					c.place({at == edge::left ? a.x : a.x + a.width - w, top, w, std::max(0.f, bottom - top)});
				}
			}
			desktop::element* hit(desktop::point at) override {
				if (!frame.contains(at)) return nullptr;
				for (std::size_t i = children.size(); i-- > 0;) {
					if (auto* found = children[i]->hit(at)) return found;
				}
				return nullptr;
			}

		private:
			std::vector<edge> edges_;
		};
	}  // namespace

	bool awake() { return woken_now.get(); }

	void wake() {
		woken_until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		if (!woken_now.peek()) woken_now.set(true);
		wait_out();
	}

	void rest() {
		if (woken_now.peek()) woken_now.set(false);
	}

	void key_down(const key_event& e) {
		// A key held for another, like Ctrl for the wheel, isn't one pressed.
		if (e.symbol >= 0xffe1 && e.symbol <= 0xffee) return;
		awake_before = woken_now.peek();
		wake();
		input_heard();
	}

	void stepped() {
		if (!awake_before) rest();
	}

	void screen_begun() { was_alone = std::exchange(shown_alone, false); }

	desktop::view workspace(page& p, std::vector<region> regions) {
		p.fill();
		const auto& t = current_theme();
		std::map<std::string, desktop::view> by;
		std::set<std::string> drawers;
		for (auto& r : regions) {
			if (r.inside.get()) by[r.name] = std::move(r.inside);
			if (r.drawer) drawers.insert(r.name);
		}
		auto taken = [&](const std::string& name) -> desktop::view {
			auto found = by.find(name);
			return found == by.end() ? desktop::view() : std::move(found->second);
		};
		// A bar or a toolbar as it's drawn, in its place or over a picture.
		auto drawn_across = [&](const char* across, desktop::view v) {
			// Filling the window, its bar says the app's name in the middle, in the theme's
			// heading font, as a window's title is, and switches light and dark at its end,
			// as the site's would: its lines and the switch each on a side as wide as the
			// other, so the name stays centered.
			if (std::string_view(across) == "bar") {
				auto mark = t.heading(20);
				mark.line_height = 1.1f;
				std::vector<desktop::view> start, end;
				start.push_back(std::move(v));
				start.push_back(desktop::spacer());
				end.push_back(desktop::spacer());
				if (offers_dark()) end.push_back(desktop::tool_button(dark() ? "sun" : "moon", dark() ? "Light" : "Dark", [] { dark(!dark()); }));
				std::vector<desktop::view> bar;
				bar.push_back(desktop::row(std::move(start)).spacing(6).align(align::center).width(0).grow());
				if (std::string name = app_name(); !name.empty()) bar.push_back(desktop::text(std::move(name), mark));
				bar.push_back(desktop::row(std::move(end)).spacing(6).align(align::center).width(0).grow());
				v = desktop::row(std::move(bar)).spacing(6).align(align::center);
			}
			// A toolbar's groups with a line between each and the next, as the web draws them.
			// When every one is a single row of small tools, it's a thin strip, their names
			// said only by their tools', as the web's.
			if (std::string_view(across) == "toolbar") {
				auto groups = std::move(v).take();
				auto drawn = [](desktop::element* g) { return g->key == "group at end" && !g->children.empty() ? g->children.back().get() : g; };
				bool thin = std::ranges::all_of(groups->children, [&](const auto& g) { return !drawn(g.get())->key.starts_with("group") || drawn(g.get())->key == "group one row"; });
				if (thin) {
					for (auto& g : groups->children) {
						auto* group = drawn(g.get());
						if (group->key == "group one row" && group->children.size() == 3) group->children.resize(1);
					}
				}
				std::vector<desktop::view> lined;
				for (auto& g : groups->children) {
					if (!lined.empty()) lined.push_back(desktop::spacer().grow(0).width(1).background({.fill = t.line}));
					lined.push_back(desktop::view(std::move(g)));
				}
				v = desktop::row(std::move(lined)).align(align::stretch);
			}
			return v;
		};
		// Only its picture: its bars over it, while the pointer's moved lately, and not
		// at all while it fills the screen.
		bool alone = p.take_alone();
		shown_alone = alone;
		if (alone && !was_alone) wake();
		if (alone) {
			std::vector<std::pair<alone_element::edge, desktop::view>> over;
			if (awake() && !fullscreen()) {
				desktop::color veiled = t.surface;
				veiled.alpha = 0.86f;
				for (const char* across : {"bar", "toolbar"}) {
					if (auto v = taken(across); v.get()) over.emplace_back(alone_element::edge::top, drawn_across(across, std::move(v)).padding(edges(6, 12)).background({.fill = veiled}));
				}
				if (auto tools = taken("tools"); tools.get()) over.emplace_back(alone_element::edge::left, std::move(tools).padding(edges(8, 6)).background({.fill = veiled}));
				if (auto side = taken("side"); side.get()) over.emplace_back(alone_element::edge::right, desktop::scroll(std::move(side).padding(edges(12, 12))).width(288).background({.fill = veiled}));
				if (auto status = taken("status"); status.get()) over.emplace_back(alone_element::edge::bottom, std::move(status).padding(edges(4, 12)).background({.fill = veiled}));
			}
			// A drawer that's open stays, as it was asked for, while the rest rests.
			if (drawers.contains("side")) {
				if (auto side = taken("side"); side.get()) {
					desktop::color veiled = t.surface;
					veiled.alpha = 0.86f;
					over.emplace_back(alone_element::edge::right, desktop::scroll(std::move(side).padding(edges(12, 12))).width(288).background({.fill = veiled}));
				}
			}
			return desktop::view(std::make_unique<alone_element>(taken("main").grow(), std::move(over))).grow();
		}
		// Filling the screen, it's only its work, like a picture on a wall, with a side that's
		// open, like a timeline along it; still scrolled as it was.
		bool only_work = fullscreen();
		std::vector<desktop::view> parts;
		for (const char* across : {"bar", "toolbar"}) {
			if (auto v = taken(across); v.get() && !only_work) {
				v = drawn_across(across, std::move(v));
				parts.push_back(std::move(v).padding(edges(6, 12)).background({.fill = t.surface}));
				parts.push_back(desktop::divider());
			}
		}
		std::vector<desktop::view> middle;
		// Its tools in a column down its left side, as a drawing app's are.
		if (auto tools = taken("tools"); tools.get() && !only_work) {
			middle.push_back(std::move(tools).padding(edges(8, 6)).background({.fill = t.surface}));
			middle.push_back(desktop::spacer().grow(0).width(1).background({.fill = t.line}));
		}
		// Work that fills its place, like a collage, is as big as it; anything else, like
		// a gallery of cards, scrolls when there's more of it than there's room for.
		auto main = taken("main");
		// A form opened from a toolbar's button, over its work, as a page has one under
		// its title.
		if (auto around = p.take_around(); !around.under.empty()) {
			std::vector<desktop::view> opened;
			for (auto& v : around.under) opened.push_back(std::move(v));
			if (main.get()) opened.push_back(std::move(main).grow());
			main = desktop::column(std::move(opened)).spacing(12).align(align::stretch).grow();
		}
		bool fills = main.get() && std::ranges::any_of(main.get()->children, [](const auto& c) { return c && c->grow > 0; });
		float margin = p.take_bleed() ? 2 : 12;
		if (fills || !main.get()) middle.push_back(std::move(main).padding(edges(margin, margin)).grow());
		else middle.push_back(desktop::scroll(std::move(main).padding(edges(margin, margin)), p.where_left().scroll).grow());
		if (auto side = taken("side"); side.get()) {
			// A side holding only something as wide as it says, like a timeline drawn along
			// the cards as a scrollbar is, is that alone at the work's edge: as tall as it,
			// not scrolled, padded or set on the surface.
			auto& inside = side.get()->children;
			if (inside.size() == 1 && inside.front()->fixed_width) {
				float wide = *inside.front()->fixed_width;
				middle.push_back(std::move(side).width(wide).grow(0));
			} else {
				middle.push_back(desktop::scroll(std::move(side).padding(edges(12, 12))).width(288).grow(0).background({.fill = t.surface}));
			}
		}
		parts.push_back(desktop::row(std::move(middle)).align(align::stretch).grow());
		if (auto status = taken("status"); status.get() && !only_work) {
			parts.push_back(desktop::divider());
			parts.push_back(std::move(status).padding(edges(4, 12)).background({.fill = t.surface}));
		}
		return desktop::column(std::move(parts)).background({.fill = t.sunken}).grow();
	}

	desktop::view group(page& p, std::string name, bool small, bool at_end, std::function<std::vector<desktop::view>()> build, int across_said, bool said) {
		// Unsaid, as tall as a name still, so it lines up with every other group.
		if (!said) name = " ";
		const auto& t = current_theme();
		auto was = p.in_group();
		p.in_group(small);
		auto tools = build();
		p.in_group(was);
		std::erase_if(tools, [](const desktop::view& v) { return !v.get(); });
		if (tools.empty()) return {};
		desktop::view laid;
		bool one_row = false;  // small, its tools in a single row
		if (small) {
			// Rows of icons, read a row at a time: as long as the group says, or two rows, the
			// first the longer. Each column is one from each row, so they line up.
			std::size_t across = across_said > 0 ? static_cast<std::size_t>(across_said) : (tools.size() + 1) / 2;
			one_row = tools.size() <= across;
			std::vector<desktop::view> columns;
			for (std::size_t i = 0; i < across && i < tools.size(); ++i) {
				std::vector<desktop::view> down;
				for (std::size_t j = i; j < tools.size(); j += across) down.push_back(std::move(tools[j]));
				columns.push_back(desktop::column(std::move(down)).spacing(2));
			}
			// A column of one sits in the top row, beside the rest of it.
			laid = desktop::row(std::move(columns)).spacing(2).align(align::start);
		} else {
			laid = desktop::row(std::move(tools)).spacing(2);
		}
		// As small as the web's, 11 points to its 15.
		auto label = t.quiet();
		label.size = t.text_size - 4;
		// Its name at the bottom, in line with every other group's, however many rows of
		// tools are over it, as the web's.
		// Marked by how it's laid out, for the toolbar to tell whether it's thin.
		auto drawn = desktop::column(std::move(laid), desktop::spacer(), desktop::text(std::move(name), label)).spacing(2).align(align::center).padding(edges(0, 8)).key(one_row ? "group one row" : "group");
		if (!at_end) return drawn;
		return desktop::row(desktop::spacer(), std::move(drawn)).align(align::stretch).grow().key("group at end");
	}

	desktop::view options(page& p, std::string label, std::string icon, std::function<std::vector<desktop::view>()> build) {
		std::string key = label;
		return options(p, std::move(label), std::move(icon), std::move(key), std::move(build));
	}

	desktop::view options(page& p, std::string label, std::string icon, std::string key, std::function<std::vector<desktop::view>()> build) {
		auto open = p.kept<bool>("options " + key);
		// Its lines built as choices, closing it once one's chosen.
		auto was = p.in_options();
		auto in_group = p.in_group();
		p.in_options(open), p.in_group(std::nullopt);
		auto lines = build();
		p.in_options(was), p.in_group(in_group);
		std::erase_if(lines, [](const desktop::view& v) { return !v.get(); });
		auto flip = [open] { open.set(!open.peek()); };
		desktop::view trigger = p.in_group() ? tool(p, icon, label, {}, flip, open.get()) : desktop::tool_button(desktop::has_icon(icon) ? icon : "", label, flip, {.on = open.get()});
		return desktop::popover(std::move(trigger), desktop::column(std::move(lines)).spacing(2).align(align::stretch), open);
	}

	desktop::view tool_column(page& p, std::function<std::vector<desktop::view>()> build) {
		auto was = p.in_group();
		p.in_group(true);
		auto tools = build();
		p.in_group(was);
		std::erase_if(tools, [](const desktop::view& v) { return !v.get(); });
		if (tools.empty()) return {};
		return desktop::column(std::move(tools)).spacing(2).align(align::center);
	}

	desktop::view toggle(page& p, std::string name, std::string label, std::string icon, std::vector<std::string> keys) {
		bool on = p.toggled(name);
		// A slideshow's toggle pressed while it plays stops it, and stays off.
		auto flip = [&p, name] {
			if (!stopped_just_now(name)) p.toggle(name);
		};
		if (!(p.in_group() && p.off())) for (const auto& k : keys) p.on_key(k, flip);
		if (p.in_group()) return tool(p, icon, label, keys, flip, on);
		return desktop::tool_button(desktop::has_icon(icon) ? icon : "", hint_of(label, keys), flip, {.on = on, .flashed = flashing(keys)});
	}

	desktop::view choose(page& p, std::string name, std::string choice, std::string first, std::string label, std::string icon, std::vector<std::string> keys) {
		bool on = p.chosen(name, first) == choice;
		auto pick = [&p, name, choice] { p.choose(name, choice); };
		if (!(p.in_group() && p.off())) for (const auto& k : keys) p.on_key(k, pick);
		if (p.in_group()) return tool(p, icon, label, keys, pick, on);
		return desktop::tool_button(desktop::has_icon(icon) ? icon : "", hint_of(label, keys), pick, {.on = on, .flashed = flashing(keys)});
	}

	desktop::view history(page& p, bool redo, std::string label, std::string icon, std::vector<std::string> keys) {
		std::string command = redo ? "one::redo" : "one::undo";
		// Off while there's nothing to take back, or again, as the source's history says;
		// a source that doesn't say leaves it on.
		const view_state& kept = *p.view("one::history", "");
		bool none = kept.status == status::ready && kept.data[redo ? "redo" : "undo"].as_string().empty();
		auto take = [&p, command, redo] {
			if (p.running(command)) return;
			p.run(command, value::map(), [told = p.told(), redo](const answer& a) {
				if (a) told.set(std::string(redo ? "Redid " : "Undid ") + step_words(*a) + ".");
			});
		};
		if (!(p.in_group() && p.off()) && !none) for (const auto& k : keys) p.on_key(k, take);
		if (label.empty()) label = redo ? "Redo" : "Undo";
		if (p.in_group()) return tool(p, icon, label, keys, take, false, none);
		return desktop::tool_button(desktop::has_icon(icon) ? icon : "", hint_of(label, keys), take, {.off = none, .flashed = flashing(keys)});
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
		if (!(p.in_group() && p.off())) for (const auto& k : keys) p.on_key(k, flip);
		if (label.empty()) label = "Full screen";
		if (p.in_group()) return tool(p, icon, label, keys, flip, fullscreen());
		return desktop::tool_button(desktop::has_icon(icon) ? icon : "", hint_of(label, keys), flip, {.on = fullscreen(), .flashed = flashing(keys)});
	}

}  // namespace one::desktop::runtime
