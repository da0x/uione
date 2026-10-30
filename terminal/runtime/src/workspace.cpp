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
		for (const char* across : {"bar", "toolbar", "tools"}) {
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

	terminal::view options(page& p, std::string label, std::string icon, std::string, std::function<std::vector<terminal::view>()> build) {
		return options(p, std::move(label), std::move(icon), std::move(build));
	}

	terminal::view options(page&, std::string label, std::string, std::function<std::vector<terminal::view>()> build) {
		auto lines = build();
		std::erase_if(lines, [](const terminal::view& v) { return !v.get(); });
		lines.insert(lines.begin(), terminal::text(label + ":", dim()));
		return terminal::flow(std::move(lines)).spacing(1);
	}

	terminal::view tool_column(page& p, std::function<std::vector<terminal::view>()> build) {
		auto was = p.in_group();
		p.in_group(true);
		auto tools = build();
		p.in_group(was);
		std::erase_if(tools, [](const terminal::view& v) { return !v.get(); });
		if (tools.empty()) return {};
		return terminal::flow(std::move(tools)).spacing(1);
	}

	terminal::view toggle(page& p, std::string name, std::string label, std::string, std::vector<std::string> keys) {
		auto flip = [&p, name] { p.toggle(name); };
		for (const auto& k : keys) p.on_key(k, flip);
		return terminal::button((p.toggled(name) ? "[x] " : "[ ] ") + label, flip);
	}

	terminal::view choose(page& p, std::string name, std::string choice, std::string first, std::string label, std::string, std::vector<std::string> keys) {
		auto pick = [&p, name, choice] { p.choose(name, choice); };
		for (const auto& k : keys) p.on_key(k, pick);
		return terminal::button((p.chosen(name, first) == choice ? "(•) " : "( ) ") + label, pick);
	}

	terminal::view history(page& p, bool redo, std::string label, std::string, std::vector<std::string> keys) {
		std::string command = redo ? "one::redo" : "one::undo";
		if (label.empty()) label = redo ? "Redo" : "Undo";
		// Dim while there's nothing to take back, or again, as the source's history says.
		const view_state& kept = *p.view("one::history", "");
		if (kept.status == status::ready && kept.data[redo ? "redo" : "undo"].as_string().empty()) return terminal::text(label, terminal::dim());
		auto take = [&p, command, redo] {
			if (p.running(command)) return;
			p.run(command, value::map(), [told = p.told(), redo](const answer& a) {
				if (a) told.set(std::string(redo ? "Redid " : "Undid ") + step_words(*a) + ".");
			});
		};
		for (const auto& k : keys) p.on_key(k, take);
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
	terminal::view picture_alone(page&, const std::string&, const std::string&) { return {}; }

	terminal::view collage_button(page& p, collage_ask ask) {
		// What needs pictures drawn, a screen of its own, or a file chooser, isn't here.
		if (ask.does == "crop" || ask.does == "screenshot" || ask.does == "add pictures" || ask.does.starts_with("copy") || ask.does.starts_with("save")) return {};
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
		// Those hidden aren't shown or picked; held in layers, they're in their layers'
		// order, and those of a hidden layer aren't either.
		const value shown = spec.layers.empty() ? shown_layers(data[spec.list]) : in_layers(shown_layers(data[spec.list]), data[spec.layers], spec.layer);
		const auto& layers = shown.as_array();
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
			std::string name = words ? "\"" + l["words"].as_string() + "\"" : "Picture " + std::to_string(++pictures);
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
		// pixels; the arrows alone go from layer to layer. Nothing's dragged here, so
		// nothing snaps.
		std::string picked = spec.pick.empty() ? "" : p.picked_id(spec.pick);
		for (const auto& l : layers) {
			if (picked.empty() || l["id"].as_string() != picked || spec.place.empty()) continue;
			for (auto [key, dx, dy] : {std::tuple{"left", -1.0, 0.0}, {"right", 1.0, 0.0}, {"up", 0.0, -1.0}, {"down", 0.0, 1.0}}) {
				p.on_key(std::string("alt+") + key, [&p, command = spec.place, kept = spec.fields, l, dx, dy] {
					p.run(command, collage_fields_of(value(std::map<std::string, value>{{"id", l["id"]}, {"x", l["x"].as_double() + dx * 10}, {"y", l["y"].as_double() + dy * 10}, {"w", l["w"]}, {"h", l["h"]}}), kept));
				});
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

	terminal::view layer_list(page& p, layers_spec spec) {
		const view_state& v = *spec.view;
		if (v.status == status::loading) return terminal::text("Loading…", dim());
		const value data = collage_parts_of(v.data, spec.objects.empty() ? spec.list : spec.objects, spec.fields);
		std::vector<terminal::view> rows;
		// The layers objects are held in, topmost first: each by its name, with its buttons,
		// its objects going with it when it's deleted.
		if (!spec.objects.empty()) {
			std::vector<value> groups = data[spec.list].as_array();
			std::stable_sort(groups.begin(), groups.end(), [](const value& a, const value& b) { return a["depth"].as_double() < b["depth"].as_double(); });
			const value ordered = value(groups);
			for (std::size_t i = groups.size(); i-- > 0;) {
				const value& g = groups[i];
				std::string id = g["id"].as_string();
				bool hidden = hidden_layer(g);
				std::vector<terminal::view> row;
				row.push_back(terminal::text((id == p.picked_id(spec.pick) ? "> " : "  ") + (g["name"].as_string().empty() ? std::string("Layer") : g["name"].as_string()), hidden ? dim() : terminal::style{}));
				if (!spec.hide.empty()) row.push_back(terminal::button(hidden ? "Show" : "Hide", [&p, spec, id, hidden] { p.run(spec.hide, value(std::map<std::string, value>{{"id", id}, {"hidden", hidden ? 0 : 1}})); }));
				if (!spec.order.empty()) {
					auto move = [&p, spec, ordered, i](std::size_t to) {
						bool first = true;
						for (const auto& [moved, depth] : depths_moved(ordered, i, to)) {
							value sent = value(std::map<std::string, value>{{"id", moved}, {"depth", depth}});
							if (!std::exchange(first, false)) sent.fields["one:history"] = value("joined");
							p.run(spec.order, sent);
						}
					};
					if (i + 1 < groups.size()) row.push_back(terminal::button("Up", [move, i] { move(i + 1); }));
					if (i > 0) row.push_back(terminal::button("Down", [move, i] { move(i - 1); }));
				}
				if (!spec.remove.empty()) {
					row.push_back(terminal::button("Delete", [&p, spec, id, objects = data[spec.objects]] {
						bool first = true;
						for (const auto& o : objects.as_array()) {
							if (o[spec.object_layer].as_string() != id || spec.object_remove.empty()) continue;
							value sent = value(std::map<std::string, value>{{"id", o["id"]}});
							if (!std::exchange(first, false)) sent.fields["one:history"] = value("joined");
							p.run(spec.object_remove, sent);
						}
						value gone = value(std::map<std::string, value>{{"id", id}});
						if (!first) gone.fields["one:history"] = value("joined");
						p.run(spec.remove, gone);
					}));
				}
				if (!spec.pick.empty()) row.push_back(terminal::button("Pick", [&p, pick = spec.pick, id] { p.pick(pick, id); }));
				rows.push_back(terminal::flow(std::move(row)).spacing(1));
			}
			if (rows.empty()) return terminal::text("No layers yet.", dim());
			return terminal::column(std::move(rows));
		}
		const value& layers = data[spec.list];
		const auto& all = layers.as_array();
		// Topmost first, as it's seen: its name, then its buttons.
		for (std::size_t i = all.size(); i-- > 0;) {
			const value& l = all[i];
			std::string id = l["id"].as_string();
			bool hidden = hidden_layer(l);
			std::string name = l["picture"].as_string().empty() ? l["words"].as_string().substr(0, l["words"].as_string().find('\n')) : "Picture";
			if (name.empty()) name = l["shape"].as_string().empty() ? "Rectangle" : l["shape"].as_string();
			std::vector<terminal::view> row;
			row.push_back(terminal::text((id == p.picked_id(spec.pick) ? "> " : "  ") + name, hidden ? dim() : terminal::style{}));
			if (!spec.hide.empty()) {
				row.push_back(terminal::button(hidden ? "Show" : "Hide", [&p, spec, id, hidden] { p.run(spec.hide, collage_fields_of(value(std::map<std::string, value>{{"id", id}, {"hidden", hidden ? 0 : 1}}), spec.fields)); }));
			}
			if (!spec.order.empty()) {
				auto move = [&p, spec, layers, i](std::size_t to) {
					bool first = true;
					for (const auto& [moved, depth] : depths_moved(layers, i, to)) {
						value sent = collage_fields_of(value(std::map<std::string, value>{{"id", moved}, {"depth", depth}}), spec.fields);
						if (!std::exchange(first, false)) sent.fields["one:history"] = value("joined");
						p.run(spec.order, sent);
					}
				};
				if (i + 1 < all.size()) row.push_back(terminal::button("Up", [move, i] { move(i + 1); }));
				if (i > 0) row.push_back(terminal::button("Down", [move, i] { move(i - 1); }));
			}
			if (!spec.remove.empty()) row.push_back(terminal::button("Delete", [&p, spec, id] { p.run(spec.remove, value(std::map<std::string, value>{{"id", id}})); }));
			if (!spec.pick.empty()) row.push_back(terminal::button("Pick", [&p, pick = spec.pick, id] { p.pick(pick, id); }));
			rows.push_back(terminal::flow(std::move(row)).spacing(1));
		}
		if (rows.empty()) return terminal::text("No layers yet.", dim());
		return terminal::column(std::move(rows));
	}

}  // namespace one::terminal::runtime
