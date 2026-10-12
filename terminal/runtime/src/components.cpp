// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What's on a screen, drawn in a terminal from what one::screens works out of it:
// text and titles, links and commands, forms, tables, boards, cards, threads,
// timelines, and what goes between a list's things.

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <one/markdown.hpp>

#include <algorithm>
#include <format>
#include <thread>

#include "one/terminal/runtime/components.hpp"

namespace one::terminal::runtime {

	namespace {

		terminal::view quiet(std::string words) { return terminal::text(std::move(words), dim()); }

		std::optional<terminal::view> not_shown(const view_state& v) {
			if (v.status == status::loading) return quiet("Loading…");
			if (v.status == status::failed) return terminal::text(v.error.empty() ? "This couldn't be read." : v.error, colored(color::red));
			return std::nullopt;
		}

		// The library's colors, as a terminal has them.
		color named(const std::string& name) {
			static const std::map<std::string, color> colors{{"red", color::red},       {"orange", color::yellow}, {"amber", color::yellow}, {"yellow", color::yellow},
			                                                {"green", color::green},   {"teal", color::cyan},     {"blue", color::blue},    {"indigo", color::blue},
			                                                {"violet", color::magenta}, {"pink", color::magenta},  {"gray", color::bright_black}};
			auto found = colors.find(name);
			return found == colors.end() ? color::blue : found->second;
		}

		// A tone of one::screens' tone_of: red, yellow, none in the middle, green and blue.
		std::optional<color> tone(int t) {
			switch (t) {
				case 1: return color::red;
				case 2: return color::yellow;
				case 4: return color::green;
				case 5: return color::blue;
				default: return std::nullopt;
			}
		}

		// A mark down a row's side in its tone, or room for one.
		terminal::view edge(int t) {
			if (auto c = tone(t)) return terminal::text("▌", {.foreground = *c}).width(1);
			return terminal::text(" ").width(1);
		}

		void open_in_browser(const std::string& address) {
			pid_t child = 0;
			char opener[] = "xdg-open";
			std::string copied = address;
			char* arguments[] = {opener, copied.data(), nullptr};
			if (posix_spawnp(&child, opener, nullptr, nullptr, arguments, environ) == 0) std::thread([child] { waitpid(child, nullptr, 0); }).detach();
		}

		// The clipboard, as terminals take it: an OSC 52 sequence carrying the text.
		void copy_to_terminal(const std::string& text) {
			static constexpr char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string coded;
			for (std::size_t i = 0; i < text.size(); i += 3) {
				unsigned n = static_cast<unsigned char>(text[i]) << 16;
				if (i + 1 < text.size()) n |= static_cast<unsigned char>(text[i + 1]) << 8;
				if (i + 2 < text.size()) n |= static_cast<unsigned char>(text[i + 2]);
				coded += letters[(n >> 18) & 63];
				coded += letters[(n >> 12) & 63];
				coded += i + 1 < text.size() ? letters[(n >> 6) & 63] : '=';
				coded += i + 2 < text.size() ? letters[n & 63] : '=';
			}
			std::string sequence = "\033]52;c;" + coded + "\a";
			(void)!write(STDOUT_FILENO, sequence.data(), sequence.size());
		}

		std::string lowered_first(std::string s) {
			if (!s.empty() && s[0] >= 'A' && s[0] <= 'Z') s[0] = static_cast<char>(s[0] - 'A' + 'a');
			return s;
		}

		std::vector<terminal::option> drawn_options(const std::vector<screens::option>& from) {
			std::vector<terminal::option> out;
			for (const auto& o : from) out.push_back({o.value, o.label});
			return out;
		}

	}  // namespace

	terminal::view items(std::vector<terminal::view> all) {
		std::erase_if(all, [](const terminal::view& v) { return !v.get(); });
		return terminal::column(std::move(all)).spacing(1);
	}

	// Words with a link written in them, like [Daher](https://…), are Markdown.
	terminal::view text(std::vector<part> parts) {
		std::string words = said(parts);
		if (words.find("](") != std::string::npos) return markdown_view(words);
		return terminal::text(std::move(words));
	}

	terminal::view subtitle(page& p, std::vector<part> parts) {
		p.under_title(quiet(said(parts)));
		return {};
	}

	terminal::view crumbs(page& p, std::vector<crumb> trail) {
		std::vector<terminal::view> shown;
		for (const auto& c : crumb_links(p, trail)) {
			if (!shown.empty()) shown.push_back(quiet("›"));
			shown.push_back(terminal::link(c.title, [&p, address = c.address] { p.open(address); }, dim()));
		}
		p.above_title(terminal::row(std::move(shown)).spacing(1));
		return {};
	}

	terminal::view code(std::string, std::string source) {
		std::vector<line> rows;
		std::string all = without_license(std::move(source));
		std::size_t at = 0;
		while (at <= all.size()) {
			std::size_t end = all.find('\n', at);
			if (end == std::string::npos) end = all.size();
			rows.push_back({{"│ ", dim()}, {all.substr(at, end - at), {.foreground = color::cyan}}});
			at = end + 1;
		}
		return terminal::text_rows(std::move(rows));
	}

	terminal::view section(section_title title, std::vector<terminal::view> inside) {
		std::vector<terminal::view> parts;
		parts.push_back(terminal::text(std::move(title.title), {.bold = true, .foreground = title.color.empty() ? color::none : named(title.color)}));
		for (auto& v : inside) {
			if (v.get()) parts.push_back(std::move(v));
		}
		return terminal::column(std::move(parts)).spacing(1);
	}

	// The part of the page a link last brought into view, and how many times one has.
	using anchor = std::pair<std::string, int>;

	terminal::view anchored(page& p, std::string name, terminal::view part) {
		auto [wanted, times] = p.kept<anchor>("anchor").get();
		if (wanted != name || !part.get()) return part;
		return std::move(part).key("#" + name + "-" + std::to_string(times)).brought_to_top();
	}

	terminal::view link(page& p, std::string words, std::string to, std::string) {
		if (leaves(to)) return terminal::link(std::move(words), [to] { open_in_browser(to); });
		if (to.starts_with("#")) {
			return terminal::link(std::move(words), [&p, name = to.substr(1)] {
				auto at = p.kept<anchor>("anchor");
				at.set({name, at.peek().second + 1});
			});
		}
		return terminal::link(std::move(words), [&p, address = address_of(p, to)] { p.open(address); });
	}

	terminal::view hero(std::string title, std::vector<terminal::view> inside) {
		std::vector<terminal::view> parts;
		parts.push_back(terminal::text(std::move(title), {.bold = true}));
		for (auto& v : inside) {
			if (v.get()) parts.push_back(std::move(v));
		}
		return terminal::column(std::move(parts)).spacing(1);
	}

	terminal::view command(page& p, command_button b) {
		auto shown = command_of(p, b);
		if (!shown) return {};
		return terminal::button(shown->label, [&p, name = b.command, fields = shown->fields, busy = shown->busy] {
			if (!busy) p.run(name, fields);
		}, b.primary);
	}

	terminal::view confirm(page& p, std::string command_name, std::string question) {
		p.ask_before(command_name, std::move(question));
		return {};
	}

	terminal::view actions(std::vector<terminal::view> all) {
		std::erase_if(all, [](const terminal::view& v) { return !v.get(); });
		if (all.empty()) return {};
		return terminal::flow(std::move(all)).spacing(2);
	}

	terminal::view heading(page& p, terminal::view beside) {
		if (beside.get()) p.beside_title(std::move(beside));
		return {};
	}

	// The main region with the others beside it, in a terminal wide enough, as the web
	// lays them out; one after another in a narrow one, main first.
	terminal::view layout(std::string, std::vector<region> regions) {
		terminal::view main;
		std::vector<terminal::view> others;
		for (auto& r : regions) {
			if (!r.inside.get()) continue;
			if (r.name == "main" && !main.get()) main = std::move(r.inside);
			else others.push_back(std::move(r.inside));
		}
		if (!main.get()) return terminal::column(std::move(others)).spacing(1);
		if (others.empty()) return main;
		return terminal::beside(std::move(main), terminal::column(std::move(others)).spacing(1));
	}

	terminal::view menu(page& p, std::vector<menu_link> links, terminal::view beside) {
		std::vector<terminal::view> across;
		for (const auto& entry : menu_entries(p, links)) {
			if (entry.here) across.push_back(terminal::text(entry.label, {.bold = true, .underline = true}));
			else across.push_back(terminal::link(entry.label, [&p, to = entry.address] { p.open(to); }, {}));
		}
		return terminal::column(terminal::row(std::move(across)).spacing(3), std::move(beside)).spacing(1);
	}

	terminal::view switched(page& p, std::string id, std::string label_text, std::vector<std::string> options, std::vector<terminal::view> ways) {
		auto chosen = p.kept<int>("switched:" + id, 0);
		int now = std::clamp(chosen.get(), 0, static_cast<int>(ways.size()) - 1);
		std::vector<terminal::view> buttons;
		buttons.push_back(quiet(label_text));
		for (std::size_t i = 0; i < options.size() && i < ways.size(); ++i) {
			int which = static_cast<int>(i);
			buttons.push_back(terminal::button(options[i], [chosen, which] { chosen.set(which); }, which == now));
		}
		return terminal::column(terminal::row(std::move(buttons)).spacing(2), std::move(ways[static_cast<std::size_t>(now)])).spacing(1);
	}

	terminal::view steps(page& p, steps_spec spec) {
		auto model = steps_of(p, spec);
		if (!model.none.empty()) return quiet(model.none);
		if (model.buttons.empty()) return {};
		std::vector<terminal::view> buttons;
		for (const auto& b : model.buttons) {
			buttons.push_back(terminal::button(b.label, [&p, command_name = spec.command, fields = b.fields, busy = model.busy] {
				if (!busy) p.run(command_name, fields);
			}, true));
		}
		return terminal::flow(std::move(buttons)).spacing(2);
	}

	terminal::view form(page& p, form_spec spec) {
		form_model model(p, spec);
		if (!model.shown()) return {};
		if (!model.open()) return terminal::button(model.opens(), [model] { model.set_open(true); }, true);
		std::vector<terminal::view> parts;
		for (const auto& f : model.fields()) {
			std::string shown = model.shown_label(f);
			std::vector<terminal::view> field_parts;
			if (f.type != "boolean") field_parts.push_back(terminal::text(shown, {.bold = true}));
			if (f.type == "boolean") {
				field_parts.push_back(terminal::toggle(model.ticked(f, "true"), shown));
			} else if (f.type == "choice" || f.type == "pick") {
				field_parts.push_back(terminal::picker(model.written(f), drawn_options(model.choices_of(f)), model.picking(f), "Choose " + lowered_first(shown) + "…"));
			} else if (f.type == "file" || f.type == "files") {
				// Uploading isn't drawn in a terminal yet: what's attached stays as it is.
				field_parts.push_back(quiet("Files are attached on the web"));
			} else if (f.type == "choices") {
				std::vector<terminal::view> each;
				for (const auto& o : f.choices) each.push_back(terminal::toggle(model.ticked(f, o.value), o.label));
				field_parts.push_back(terminal::column(std::move(each)));
			} else {
				bool several = f.type == "markdown";
				std::string placeholder = f.type == "date" ? "YYYY-MM-DD" : f.type == "list" ? "separated by commas" : "";
				field_parts.push_back(terminal::field(model.written(f), {.placeholder = placeholder, .multiline = several, .lines = several ? 4 : 1}));
			}
			if (!f.hint.empty()) field_parts.push_back(quiet(f.hint));
			if (spec.button && parts.empty() && field_parts.size() > 1) field_parts[1] = std::move(field_parts[1]).autofocus();
			parts.push_back(terminal::column(std::move(field_parts)));
		}
		if (std::string why = model.error(); !why.empty()) parts.push_back(terminal::text(why, colored(color::red)));
		std::vector<terminal::view> buttons;
		buttons.push_back(terminal::button(model.busy() ? model.says() + "…" : model.says(), [model] { model.send(); }, true));
		if (spec.button) buttons.push_back(terminal::button("Cancel", [model] { model.set_open(false); }));
		parts.push_back(terminal::row(std::move(buttons)).spacing(2));
		auto panel = terminal::column(std::move(parts)).spacing(1).padding(0, 1).boxed(model.opens());
		if (spec.button) {
			p.under_title(std::move(panel));
			return {};
		}
		return panel;
	}

	terminal::view track(const std::optional<track_model>& model) {
		if (!model) return {};
		std::vector<terminal::view> across;
		for (const auto& record : model->records) {
			std::string details;
			for (const auto& d : record.details) details += (details.empty() ? "" : ", ") + d;
			across.push_back(terminal::column(terminal::link(record.title, [model = *model, id = record.id] { model.pick(id); }, {.bold = true, .reverse = record.picked}), quiet(details)));
		}
		return terminal::column(quiet(model->label), terminal::row(std::move(across)).spacing(3));
	}

	terminal::view filter_note(page& p) {
		std::string said_now = p.filtered();
		if (said_now.empty()) return {};
		return terminal::row(terminal::text("Showing " + said_now, {.bold = true, .foreground = color::blue}), terminal::link("Show all", [&p] { p.clear_filter(); }, dim())).spacing(2);
	}

	terminal::view table(page& p, table_spec spec) {
		const view_state& v = *spec.view;
		if (auto instead = not_shown(v)) return std::move(*instead);
		auto model = table_of(p, spec);
		if (model.hidden) return {};
		std::vector<terminal::view> parts;
		if (auto shown = track(model.track); shown.get()) parts.push_back(std::move(shown));
		if (auto note = filter_note(p); note.get()) parts.push_back(std::move(note));
		std::vector<terminal::view> tools;
		if (!spec.search.empty()) tools.push_back(terminal::field(model.query, {.placeholder = "Search"}).width(30));
		if (spec.tools)
			if (auto made = spec.tools(); made.get()) tools.push_back(std::move(made));
		if (!tools.empty()) parts.push_back(terminal::row(std::move(tools)).spacing(2));
		if (!model.tabs.empty()) {
			std::vector<terminal::view> tabs;
			for (const auto& tab : model.tabs) {
				tabs.push_back(terminal::button(std::format("{} {}", tab.label, tab.count), [model, value_of = tab.value] {
					model.tab.set(value_of);
					model.at.set(0);
				}, tab.open));
			}
			parts.push_back(terminal::row(std::move(tabs)).spacing(1));
		}
		if (model.rows.empty()) {
			parts.push_back(quiet(spec.empty));
			return terminal::column(std::move(parts));
		}
		// What each cell says, so each column is as wide as what it holds, up to a
		// point, and the title takes the rest: a short stage doesn't get as much room
		// as a long title, which would wrap.
		auto said = [&](const value& row, const std::string& field) {
			std::string out;
			if (spec.people.contains(field)) {
				for (const auto& name : people_in(row, field)) out += (out.empty() ? "" : ", ") + name;
			} else if (std::ranges::contains(spec.labels, field)) {
				for (const auto& word : field_of(row, field).as_array()) out += (out.empty() ? "" : " ") + ("[" + screens::show(word) + "]");
			} else {
				out = cell_text(spec.choices, row, field);
			}
			return out;
		};
		std::map<std::string, int> widest;
		for (const auto& c : spec.columns) {
			int w = display_width(c.label);
			for (const auto& row : model.rows) w = std::max(w, display_width(said(row, c.field)));
			widest[c.field] = w;
		}
		// The title takes what's left, or with none, the widest column.
		std::string growing;
		for (const auto& c : spec.columns) {
			if (growing.empty() && (c.field == "title" || c.field.ends_with(".title"))) growing = c.field;
		}
		if (growing.empty()) {
			for (const auto& c : spec.columns) {
				if (growing.empty() || widest[c.field] > widest[growing]) growing = c.field;
			}
		}
		auto sized = [&](terminal::view view_of, const std::string& column) {
			if (column == growing) return std::move(view_of).grow(1.f);
			return std::move(view_of).width(std::min(widest[column], 28));
		};
		// Each row keeps the same room for its actions, so its columns line up with the others'.
		int acting = 0;
		for (const auto& a : spec.actions) {
			if (a.allowed) acting += display_width(a.label) + 4 + 1;
		}
		std::vector<terminal::view> labels;
		if (spec.tint) labels.push_back(terminal::text(" ").width(1));
		for (const auto& c : spec.columns) labels.push_back(sized(terminal::text(c.label, {.bold = true, .dim = true}), c.field));
		if (acting > 0) labels.push_back(terminal::text("").width(acting));
		parts.push_back(terminal::row(std::move(labels)).spacing(2).padding(0, 0, 0, 2));  // over the rows, past where a list marks the one chosen
		std::vector<terminal::list_row> shown;
		std::map<std::string, value> by_id;
		for (const auto& row : model.rows) {
			std::string id = row["id"].as_string();
			by_id.emplace(id, row);
			std::vector<terminal::view> cells;
			if (spec.tint) cells.push_back(edge(tone_of(row, spec.tint)));
			for (const auto& c : spec.columns) {
				cells.push_back(sized(terminal::text(said(row, c.field)), c.field));
			}
			std::vector<terminal::view> buttons;
			for (const auto& a : spec.actions) {
				if (!a.allowed || (a.when && !a.when(row))) continue;
				std::string opens = id + "|" + a.command;
				bool asks = !a.form.empty();
				buttons.push_back(terminal::button(p.running(a.command) ? a.label + "…" : a.label, [&p, command_name = a.command, id, asks, acting = model.acting, opens] {
					if (asks) acting.set(acting.peek() == opens ? "" : opens);
					else p.run(command_name, value(std::map<std::string, value>{{"id", id}}));
				}));
			}
			if (acting > 0) {
				buttons.insert(buttons.begin(), terminal::spacer());
				cells.push_back(terminal::row(std::move(buttons)).spacing(1).width(acting));
			}
			std::vector<terminal::view> inside;
			inside.push_back(terminal::row(std::move(cells)).spacing(2));
			for (const auto& a : spec.actions) {
				if (model.acting.get() != id + "|" + a.command || a.form.empty()) continue;
				inside.push_back(form(p, {.command = a.command, .fields = a.form, .from = acted_on(p, spec.list, a.command, row), .id = id,
				                          .submit = a.submit.empty() ? a.label : a.submit, .on_done = [acting = model.acting] { acting.set(""); }}));
			}
			shown.push_back({id, terminal::column(std::move(inside))});
		}
		terminal::list_options options;
		if (!spec.link.empty()) {
			options.on_open = [&p, by_id, link_to = spec.link, keyed = spec.keyed, named_by = spec.named](const std::string& id) {
				if (auto found = by_id.find(id); found != by_id.end()) p.open(row_link(link_to, found->second, p.params(), keyed, named_by));
			};
		}
		if (model.arranging) {
			options.on_row_key = [&p, key = model.key, reorder = *spec.reorder, around = model.rows](const std::string& id, const key_press& e) {
				if (!e.alt || (!e.is(keys::up) && !e.is(keys::down))) return false;
				move_row(p, key, reorder, around, id, e.is(keys::up) ? -1 : 1);
				return true;
			};
		}
		parts.push_back(terminal::list(std::move(shown), std::move(options)));
		if (spec.page > 0 && model.total > static_cast<std::size_t>(spec.page)) {
			std::vector<terminal::view> paging;
			if (model.first > 0) paging.push_back(terminal::button("Previous", [at = model.at] { at.set(at.peek() - 1); }));
			paging.push_back(quiet(std::format("{}–{} of {}", model.first + 1, model.last, model.total)));
			if (model.last < model.total) paging.push_back(terminal::button("Next", [at = model.at] { at.set(at.peek() + 1); }));
			parts.push_back(terminal::row(std::move(paging)).spacing(2));
		}
		return terminal::column(std::move(parts));
	}

	// A board in a terminal: its columns one after another, each its cards, as there's
	// no room for them side by side; m on a card offers where it may go.
	terminal::view board(page& p, board_spec spec) {
		auto model = board_of(p, spec);
		if (model.loading) return quiet("Loading…");
		std::vector<terminal::view> parts;
		if (auto shown = track(model.track); shown.get()) parts.push_back(std::move(shown));
		if (auto note = filter_note(p); note.get()) parts.push_back(std::move(note));
		if (!spec.search.empty()) parts.push_back(terminal::field(model.query, {.placeholder = "Find cards"}).width(30));
		if (model.offer) {
			std::vector<terminal::list_row> steps_shown;
			for (const auto& step : model.offer->steps) steps_shown.push_back({step.value, terminal::text(step.label)});
			auto moving = model.moving;
			std::vector<terminal::view> offer;
			if (steps_shown.empty()) offer.push_back(quiet("There's no step from here you may take."));
			else
				offer.push_back(terminal::list(std::move(steps_shown), [&p, spec, card = model.offer->card](const std::string& to) { move_card(p, spec, card, to); })
				                    .autofocus()
				                    .on_key([moving](const key_press& e) {
					                    if (!e.is(keys::escape)) return false;
					                    moving.set("");
					                    return true;
				                    }));
			offer.push_back(terminal::button("Cancel", [moving] { moving.set(""); }));
			parts.push_back(terminal::column(std::move(offer)).padding(0, 1).boxed(std::format("Move “{}” to", model.offer->title)));
		}
		if (!model.error.empty()) parts.push_back(terminal::text(model.error, colored(color::red)));
		for (const auto& column : model.columns) {
			std::vector<terminal::view> lane;
			lane.push_back(terminal::row(terminal::text(column.title, {.bold = true}), quiet(std::to_string(column.cards.size()))).spacing(1));
			if (column.cards.empty()) {
				lane.push_back(quiet("  Nothing here"));
			} else {
				std::vector<terminal::list_row> cards_shown;
				std::map<std::string, std::string> links;
				for (const auto& card : column.cards) {
					links[card.id] = card.link;
					std::string details;
					for (const auto& d : card.details) details += (details.empty() ? "" : " ") + d;
					for (const auto& w : card.labels) details += (details.empty() ? "" : " ") + ("[" + w + "]");
					std::string people;
					for (const auto& name : card.people) people += (people.empty() ? "" : ", ") + name;
					cards_shown.push_back({card.id, terminal::row(edge(card.tone), terminal::text(card.title).grow(2), quiet(details).grow(1), quiet(people).grow(1)).spacing(1)});
				}
				terminal::list_options options;
				if (!spec.link.empty()) {
					options.on_open = [&p, links](const std::string& card) {
						if (auto found = links.find(card); found != links.end() && !found->second.empty()) p.open(found->second);
					};
				}
				if (spec.move || model.arranging) {
					options.on_row_key = [&p, key = model.key, moving = model.moving, has_move = spec.move.has_value(), reorder = spec.reorder, arranging = model.arranging,
					                      in_column = column.rows](const std::string& card, const key_press& e) {
						if (has_move && e.is('m')) {
							moving.set(card);
							return true;
						}
						if (arranging && e.alt && (e.is(keys::up) || e.is(keys::down))) {
							move_row(p, key, *reorder, in_column, card, e.is(keys::up) ? -1 : 1);
							return true;
						}
						return false;
					};
				}
				lane.push_back(terminal::list(std::move(cards_shown), std::move(options)));
			}
			parts.push_back(terminal::column(std::move(lane)));
		}
		return terminal::column(std::move(parts)).spacing(1);
	}

	terminal::view cards(page& p, cards_spec spec) {
		const view_state& v = *spec.view;
		if (auto instead = not_shown(v)) return std::move(*instead);
		std::vector<terminal::list_row> shown;
		std::map<std::string, std::string> opens;
		for (const auto& card : cards_of(p, spec)) {
			opens[card.id] = card.address;
			std::vector<terminal::view> inside;
			inside.push_back(terminal::text(card.title, {.bold = true}));
			for (const auto& d : card.details) inside.push_back(quiet(d));
			if (spec.tally) inside.push_back(quiet(card.tally));
			if (!card.filters.empty()) {
				std::vector<terminal::view> filters;
				for (const auto& f : card.filters) {
					filters.push_back(terminal::link(f.label, [&p, address = f.address] { p.open(address); }, {.foreground = f.color.empty() ? color::blue : named(f.color)}));
				}
				inside.push_back(terminal::row(std::move(filters)).spacing(3));
			}
			shown.push_back({card.id, terminal::column(std::move(inside))});
		}
		if (shown.empty()) return quiet("Nothing here yet.");
		return terminal::list(std::move(shown), [&p, opens](const std::string& id) {
			if (auto found = opens.find(id); found != opens.end() && !found->second.empty()) p.open(found->second);
		});
	}

	terminal::view find(page& p, std::string label_text, std::vector<find_source> sources) {
		auto query = p.kept<std::string>("find");
		std::vector<terminal::view> parts;
		parts.push_back(terminal::field(query, {.placeholder = label_text}));
		if (!query.get().empty()) {
			auto groups = find_in(p, query.get(), sources);
			for (const auto& g : groups) {
				std::vector<terminal::view> results;
				results.push_back(terminal::text(g.label, {.bold = true}));
				for (const auto& [says, to] : g.results) {
					if (to.empty()) results.push_back(terminal::text(says));
					else results.push_back(terminal::link(says, [&p, to] { p.open(to); }));
				}
				if (g.more > 0) results.push_back(quiet(std::format("and {} more", g.more)));
				parts.push_back(terminal::column(std::move(results)));
			}
			if (groups.empty()) parts.push_back(quiet("Nothing found."));
		}
		return terminal::column(std::move(parts)).spacing(1);
	}

	terminal::view copy(page& p, state<view_state> from, std::string label_text, std::vector<copied_field> fields, std::vector<copied_list> lists,
	                    std::map<std::string, std::vector<screens::option>> choices) {
		if (from->status != status::ready) return {};
		auto copied = p.kept<bool>("copy:" + label_text, false);
		return terminal::button(copied.get() ? "Copied" : label_text, [&p, from, fields, lists, choices, copied] {
			copy_to_terminal(markdown_of(p.title(), from.peek().data, fields, lists, choices));
			copied.set(true);
		});
	}

	namespace {

		terminal::view asking(page& p, const between_spec& spec, const between_model& b, state<std::string> open) {
			std::string now = open.get();
			if (now.empty()) return {};
			auto cut = now.find('|');
			const value *a = nullptr, *c = nullptr;
			for (const auto& thing : b.things) {
				if (thing["id"].as_string() == now.substr(0, cut)) a = &thing;
				if (thing["id"].as_string() == now.substr(cut + 1)) c = &thing;
			}
			if (!a || !c) return {};
			const value* entry = b.between(spec, *a, *c);
			std::vector<terminal::view> parts;
			if (entry && spec.update && spec.update->allowed) {
				auto started = p.kept<view_state>("between:" + now + ":entry");
				if (ember::to_json(started.peek().data) != ember::to_json(*entry)) started.set(view_state{status::ready, *entry, ""});
				parts.push_back(form(p, {.command = spec.update->command, .fields = spec.update->fields, .from = started, .id = (*entry)["id"].as_string(),
				                         .submit = spec.update->submit, .on_done = [open] { open.set(""); }}));
			}
			if (!entry && spec.create && spec.create->allowed) {
				auto given = spec.create->given;
				given[spec.from] = (*a)["id"].as_string();
				given[spec.to] = (*c)["id"].as_string();
				parts.push_back(form(p, {.command = spec.create->command, .fields = spec.create->fields, .given = given, .submit = spec.create->submit,
				                         .on_done = [open] { open.set(""); }, .key = now}));
			}
			std::vector<terminal::view> buttons;
			if (entry && spec.remove && spec.remove->allowed) {
				buttons.push_back(terminal::button(spec.remove->submit.empty() ? "Remove" : spec.remove->submit, [&p, command_name = spec.remove->command, id = (*entry)["id"].as_string(), open] {
					p.run(command_name, value(std::map<std::string, value>{{"id", id}}), [open](const answer& got) {
						if (got) open.set("");
					});
				}));
			}
			buttons.push_back(terminal::button("Close", [open] { open.set(""); }));
			parts.push_back(terminal::row(std::move(buttons)).spacing(2));
			return terminal::column(std::move(parts)).spacing(1).padding(0, 1).boxed(b.name(spec, *a) + " to " + b.name(spec, *c));
		}

	}  // namespace

	terminal::view grid(page& p, between_spec spec) {
		if (spec.over->status == status::loading || spec.view->status == status::loading) return quiet("Loading…");
		auto b = between_of(spec);
		auto open = p.kept<std::string>("grid:" + spec.list);
		std::vector<terminal::view> rows;
		std::vector<terminal::view> header;
		header.push_back(quiet(label(spec.from) + ", " + lowered_first(label(spec.to))).width(14));
		for (const auto& thing : b.things) header.push_back(terminal::text(b.name(spec, thing), {.bold = true}).grow());
		rows.push_back(terminal::row(std::move(header)).spacing(1));
		for (const auto& a : b.things) {
			std::vector<terminal::view> cells;
			cells.push_back(terminal::text(b.name(spec, a), {.bold = true}).width(14));
			for (const auto& c : b.things) {
				if (a["id"].as_string() == c["id"].as_string()) {
					cells.push_back(quiet("·").grow());
					continue;
				}
				const value* entry = b.between(spec, a, c);
				std::string key = a["id"].as_string() + "|" + c["id"].as_string();
				if (b.may(spec, entry != nullptr)) cells.push_back(terminal::button(entry ? b.says(spec, *entry) : "+", [open, key] { open.set(open.peek() == key ? "" : key); }).grow());
				else cells.push_back(terminal::text(entry ? b.says(spec, *entry) : "").grow());
			}
			rows.push_back(terminal::row(std::move(cells)).spacing(1));
		}
		if (auto asked = asking(p, spec, b, open); asked.get()) rows.push_back(std::move(asked));
		return terminal::column(std::move(rows));
	}

	terminal::view diagram(page& p, between_spec spec) {
		if (spec.over->status == status::loading || spec.view->status == status::loading) return quiet("Loading…");
		auto b = between_of(spec);
		auto open = p.kept<std::string>("diagram:" + spec.list);
		std::vector<terminal::list_row> moves;
		for (const auto& a : b.things) {
			for (const auto& c : b.things) {
				const value* entry = b.between(spec, a, c);
				if (!entry) continue;
				std::string says = spec.cell.empty() ? "" : screens::show(field_of(*entry, spec.cell));
				moves.push_back({a["id"].as_string() + "|" + c["id"].as_string(),
				                 terminal::text(line{{b.name(spec, a), {.bold = true}}, {" → ", dim()}, {b.name(spec, c), {.bold = true}}, {"  " + says, dim()}})});
			}
		}
		std::vector<terminal::view> parts;
		if (moves.empty()) parts.push_back(quiet("Nothing goes anywhere yet."));
		else
			parts.push_back(terminal::list(std::move(moves), [open, spec, b](const std::string& key) {
				if (b.may(spec, true)) open.set(key);
			}));
		if (auto asked = asking(p, spec, b, open); asked.get()) parts.push_back(std::move(asked));
		return terminal::column(std::move(parts)).spacing(1);
	}

	terminal::view thread(page&, thread_spec spec) {
		const view_state& v = *spec.view;
		if (v.status == status::loading) return quiet("Loading…");
		std::vector<terminal::view> entries;
		for (const auto& e : thread_of(v, spec.list)) {
			entries.push_back(terminal::column(terminal::text(line{{e.author, {.bold = true}}, {"  " + e.when, dim()}}),
			                                   spec.body ? spec.body(e.body) : terminal::text(e.body)));
		}
		if (entries.empty()) return quiet("Nothing yet.");
		return terminal::column(std::move(entries)).spacing(1);
	}

	terminal::view timeline(page& p, timeline_spec spec) {
		auto model = timeline_of(p, spec);
		if (model.entries.empty()) return {};
		std::vector<terminal::view> parts;
		if (!model.title.empty()) parts.push_back(terminal::text(model.title, {.bold = true}));
		// Each change one sentence, wrapped as a whole, its time beside it keeping its
		// width; one with an address is chosen and opened as a table's row is.
		std::vector<terminal::list_row> rows;
		std::map<std::string, std::string> addresses;
		for (std::size_t i = 0; i < model.entries.size(); ++i) {
			const auto& e = model.entries[i];
			terminal::line said{{e.who, {.bold = true}}, {" " + e.what, {}}};
			if (!e.subject.empty()) said.push_back({" " + e.subject, {.bold = true, .underline = !e.address.empty()}});
			if (!e.after.empty()) said.push_back({" " + e.after, {}});
			std::string key = std::to_string(i);
			if (!e.address.empty()) addresses[key] = e.address;
			rows.push_back({key, terminal::row(terminal::text(e.fresh ? "●" : " ", {.foreground = color::blue}).width(1), terminal::text(std::move(said)).grow(),
			                                   quiet(e.when).width(display_width(e.when)))
			                         .spacing(1)});
		}
		parts.push_back(terminal::list(std::move(rows), {.on_open = [&p, addresses](const std::string& key) {
			if (auto found = addresses.find(key); found != addresses.end()) p.open(found->second);
		}}));
		return terminal::column(std::move(parts));
	}

	terminal::view details(page&, state<view_state> from, std::vector<screens::column> fields, std::map<std::string, std::vector<screens::option>> choices,
	                       std::optional<tint_spec> tint, std::vector<std::string> labels) {
		const view_state& v = *from;
		if (auto instead = not_shown(v)) return std::move(*instead);
		std::vector<terminal::view> shown;
		for (const auto& d : details_of(v, fields, choices, tint)) {
			auto c = tone(d.tone);
			std::string said = d.value;
			if (std::ranges::contains(labels, d.field)) {
				// Labels as the table says them, each in brackets.
				said.clear();
				for (const auto& word : field_of(v.data, d.field).as_array()) {
					if (!screens::show(word).empty()) said += (said.empty() ? "" : " ") + ("[" + screens::show(word) + "]");
				}
			}
			shown.push_back(terminal::row(quiet(d.label).width(16), terminal::text(said, {.foreground = c.value_or(color::none)}).grow()).spacing(1));
		}
		return terminal::column(std::move(shown));
	}

	// Markdown

	namespace {

		terminal::line converted(const one::markdown::line& from) {
			terminal::line out;
			for (const auto& r : from) {
				out.push_back({r.text,
				               {.bold = r.look.bold, .italic = r.look.italic, .underline = r.look.underline || !r.look.link.empty(), .dim = r.look.dim,
				                .foreground = static_cast<color>(static_cast<int>(r.look.foreground)), .background = static_cast<color>(static_cast<int>(r.look.background))}});
			}
			return out;
		}

	}  // namespace

	terminal::view markdown_view(const std::string& source) {
		return terminal::text_laid([source](int width) {
			std::vector<terminal::line> out;
			for (const auto& l : one::markdown::layout(source, {.width = std::max(10, width), .hyperlinks = false})) out.push_back(converted(l));
			return out;
		});
	}

	terminal::view markdown(page&, state<view_state> from, std::string field, std::string empty) {
		const view_state& v = *from;
		if (v.status != status::ready) return {};
		std::string source = field_of(v.data, field).as_string();
		if (source.empty()) return empty.empty() ? terminal::view() : quiet(empty);
		return markdown_view(source);
	}

	terminal::view pages(page& p, std::string base, std::vector<written_page> all) {
		if (all.empty()) return {};
		std::string here = p.param("page");
		const written_page* shown = &all.front();
		for (const auto& w : all) {
			if (w.name == here) shown = &w;
		}
		std::vector<terminal::view> across;
		for (const auto& w : all) {
			if (&w == shown) across.push_back(terminal::text(w.title, {.bold = true, .underline = true}));
			else across.push_back(terminal::link(w.title, [&p, to = base + "/" + w.name] { p.open(to); }, {}));
		}
		return terminal::column(terminal::row(std::move(across)).spacing(3), markdown_view(shown->source)).spacing(1);
	}

}  // namespace one::terminal::runtime
