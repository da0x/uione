// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A workspace in a terminal, as the desktop's: its bar, its toolbar of named groups,
// the work beside what's at its side, and a status line with its messages; toggles,
// undo and redo, and the keys that run what's on a line.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>
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

	terminal::view on_open(page& p, bool ready, std::vector<opening> lines) {
		if (!opening_now(p, ready, lines)) return {};
		return terminal::text("Opening…", dim());
	}

	double screen_width() { return 1920; }
	double screen_height() { return 1080; }

	terminal::view picture_of(page&, const std::string&, bool, float) { return {}; }

	terminal::view collage_button(page& p, collage_ask ask) {
		// What needs pictures drawn, or a screen of its own, isn't here.
		if (ask.does == "crop" || ask.does == "screenshot" || ask.does.starts_with("copy") || ask.does.starts_with("save")) return {};
		auto asked = p.kept<std::string>("collage do " + ask.key);
		auto press = [asked, does = ask.does] { asked.set(does); };
		for (const auto& k : ask.keys) p.on_key(k, press);
		return terminal::button(ask.label, std::move(press));
	}

	terminal::view collage(page& p, collage_spec spec) {
		const view_state& v = *spec.view;
		if (v.status == status::loading) return terminal::text("Loading…", dim());
		// Read as its parts, whichever of the app's fields keep them.
		const value data = collage_parts_of(v.data, spec.list, spec.fields);
		const auto& layers = data[spec.list].as_array();
		std::string chosen = spec.pick.empty() ? "" : p.picked_id(spec.pick);
		// Words being written: a line to type them in, kept with Enter.
		auto writing = p.kept<std::string>("collage writing " + spec.key);
		auto pasting = p.kept<bool>("collage pasting " + spec.key);
		auto path = p.kept<std::string>("collage path " + spec.key);
		auto typed = p.kept<std::string>("collage words " + spec.key);
		// Read so a button's ask is acted on at once.
		if (auto asked = p.kept<std::string>("collage do " + spec.key); !asked.get().empty()) {
			std::string does = asked.peek();
			asked.set("");
			double width = data[spec.width].as_double(), height = data[spec.height].as_double();
			if (does == "paste" && spec.paste && spec.fields.contains("picture")) {
				pasting.set(true);
			} else if (does == "edit" && !spec.write.empty()) {
				for (const auto& l : layers) {
					if (l["id"].as_string() == chosen && l["picture"].as_string().empty()) writing.set(chosen), typed.set(l["words"].as_string());
				}
			} else if (does == "add" && !spec.add.empty() && spec.fields.contains("words")) {
				double size = std::max(8.0, height / 12);
				value made = value(std::map<std::string, value>{{"words", "Your text"}, {"size", size}, {"x", width / 3}, {"y", (height - size) / 2}, {"w", width / 3}, {"h", size * 1.2}});
				if (!spec.parent.empty()) made.fields[spec.parent] = value(spec.subject);
				if (!layers.empty() && !layers.back()["depth"].is_null()) made.fields["depth"] = value(layers.back()["depth"].as_double() + 1);
				p.run(spec.add, collage_fields_of(made, spec.fields), [&p, pick = spec.pick, writing, typed](const answer& a) {
					if (!a) return;
					if (!pick.empty()) p.pick(pick, *a);
					writing.set(*a), typed.set("Your text");
				});
			}
		}
		std::vector<terminal::list_row> rows;
		int pictures = 0;
		for (std::size_t i = 0; i < layers.size(); ++i) {
			const auto& l = layers[i];
			bool words = l["picture"].as_string().empty();
			std::string name = words ? "\"" + l["words"].as_string() + "\"" : i == 0 ? "Background" : "Picture " + std::to_string(++pictures);
			name += std::format("  {}×{} at {}, {}", static_cast<long long>(l["w"].as_double()), static_cast<long long>(l["h"].as_double()),
			                    static_cast<long long>(l["x"].as_double()), static_cast<long long>(l["y"].as_double()));
			rows.push_back({l["id"].as_string(), terminal::text(name)});
		}
		terminal::list_options options;
		if (!spec.pick.empty()) {
			options.on_open = [&p, name = spec.pick](const std::string& id) { p.pick(name, id); };
			options.start = p.picked_id(spec.pick);
		}
		// Alt and the arrows, as a board's cards move, take the picked layer ten of its
		// pixels, or slide the background a twentieth of the way; the arrows alone go
		// from layer to layer. Nothing's dragged here, so nothing snaps.
		std::string picked = spec.pick.empty() ? "" : p.picked_id(spec.pick);
		for (std::size_t i = 0; i < layers.size() && !picked.empty(); ++i) {
			const auto& l = layers[i];
			if (l["id"].as_string() != picked) continue;
			bool background = i == 0 && !l["picture"].as_string().empty();
			for (auto [key, dx, dy] : {std::tuple{"left", -1.0, 0.0}, {"right", 1.0, 0.0}, {"up", 0.0, -1.0}, {"down", 0.0, 1.0}}) {
				if (background && !spec.slide.empty()) {
					double sx = data["slide_x"].is_null() ? 0.5 : data["slide_x"].as_double(), sy = data["slide_y"].is_null() ? 0.5 : data["slide_y"].as_double();
					p.on_key(std::string("alt+") + key, [&p, command = spec.slide, kept = spec.fields, subject = spec.subject, sx, sy, dx, dy] {
						p.run(command, collage_fields_of(value(std::map<std::string, value>{{"id", subject}, {"slide_x", std::clamp(sx + dx * 0.05, 0.0, 1.0)}, {"slide_y", std::clamp(sy + dy * 0.05, 0.0, 1.0)}}), kept));
					});
				} else if (!background && !spec.place.empty()) {
					p.on_key(std::string("alt+") + key, [&p, command = spec.place, kept = spec.fields, l, dx, dy] {
						p.run(command, collage_fields_of(value(std::map<std::string, value>{{"id", l["id"]}, {"x", l["x"].as_double() + dx * 10}, {"y", l["y"].as_double() + dy * 10}, {"w", l["w"]}, {"h", l["h"]}}), kept));
					});
				}
			}
		}
		std::vector<terminal::view> parts;
		if (layers.empty()) parts.push_back(terminal::text(spec.empty.empty() ? "Nothing here yet." : spec.empty, dim()));
		else parts.push_back(terminal::list(std::move(rows), std::move(options)));
		if (!spec.pick.empty() && !p.picked_id(spec.pick).empty()) {
			std::vector<terminal::view> over;
			for (const auto& b : spec.over) over.push_back(command(p, b));
			parts.push_back(along(std::move(over)));
		}
		// A picture pasted by its path, as a terminal can't be dropped on: the first covers
		// the collage, the rest go in the middle as big as the collage allows.
		if (pasting.get()) {
			double width = data[spec.width].as_double(), height = data[spec.height].as_double();
			parts.push_back(terminal::field(path, {.placeholder = "The picture's path, like ~/Pictures/harbor.png", .on_submit = [&p, spec, pasting, path, layers, width, height] {
				value made = layers.empty() ? value(std::map<std::string, value>{{"x", 0}, {"y", 0}, {"w", width}, {"h", height}})
				                            : value(std::map<std::string, value>{{"x", width / 4}, {"y", height / 4}, {"w", width / 2}, {"h", height / 2}});
				if (!spec.parent.empty()) made.fields[spec.parent] = value(spec.subject);
				if (!layers.empty() && !layers.back()["depth"].is_null()) made.fields["depth"] = value(layers.back()["depth"].as_double() + 1);
				std::string file = path.peek();
				if (const char* home = std::getenv("HOME"); file.starts_with("~/") && home) file = home + file.substr(1);
				p.run(spec.add, collage_fields_of(made, spec.fields), attached_files{{spec.fields.at("picture"), {file}}}, [](const answer&) {}, false);
				pasting.set(false), path.set("");
			}}));
		}
		if (!writing.get().empty()) {
			parts.push_back(terminal::field(typed, {.on_submit = [&p, spec, writing, typed, layers] {
				for (const auto& l : layers) {
					if (l["id"].as_string() != writing.peek()) continue;
					if (typed.peek().find_first_not_of(" \t") == std::string::npos && !spec.remove.empty()) p.run(spec.remove, value(std::map<std::string, value>{{"id", l["id"]}}));
					else p.run(spec.write, collage_fields_of(value(std::map<std::string, value>{{"id", l["id"]}, {"words", typed.peek()}, {"size", l["size"]}, {"x", l["x"]}, {"y", l["y"]}, {"w", l["w"]}, {"h", l["h"]}}), spec.fields));
				}
				writing.set("");
			}}));
		}
		return terminal::column(std::move(parts)).spacing(1);
	}

}  // namespace one::terminal::runtime
