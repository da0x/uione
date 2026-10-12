// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// Forms: what a person types, ticks and picks, kept by the page while it's open,
// and sent as a command's fields, the way the web's Form sends them.

#include <algorithm>
#include <format>

#include "one/desktop/runtime/components.hpp"

namespace one::desktop::runtime {

	namespace {

		std::string trimmed(std::string_view s) {
			std::size_t from = s.find_first_not_of(' '), to = s.find_last_not_of(' ');
			return from == std::string_view::npos ? "" : std::string(s.substr(from, to - from + 1));
		}

		std::vector<std::string> split(const std::string& written) {
			std::vector<std::string> out;
			std::size_t at = 0;
			while (at <= written.size()) {
				std::size_t comma = written.find(',', at);
				if (comma == std::string::npos) comma = written.size();
				if (std::string item = trimmed(std::string_view(written).substr(at, comma - at)); !item.empty()) out.push_back(item);
				at = comma + 1;
			}
			return out;
		}

		// What a field shows for a value from a view: a date as 2026-09-30, a list
		// separated by commas, and anything else as it reads.
		std::string as_field(const value& v) {
			if (v.is_time() && v.time.seconds % 86400 == 0 && v.time.nanos == 0) return std::format("{:%Y-%m-%d}", std::chrono::floor<std::chrono::days>(v.as_time()));
			if (v.is_time()) return std::format("{:%Y-%m-%d}", std::chrono::floor<std::chrono::days>(std::chrono::zoned_time(time_zone(), v.as_time()).get_local_time()));
			if (v.is_array()) {
				std::string out;
				for (const auto& item : v.items) out += (out.empty() ? "" : ", ") + as_field(item);
				return out;
			}
			if (v.is_bool()) return v.boolean ? "true" : "false";
			return show(v);
		}

		// What a form sends for a field: a yes or no as true or false, a list or several
		// choices as the list of them, and anything else as it's written.
		value as_input(const std::string& written, const std::string& type) {
			if (type == "boolean") return value(written == "true");
			if (type != "list" && type != "choices") return value(written);
			std::vector<value> items;
			for (auto& item : split(written)) items.emplace_back(item);
			return value(std::move(items));
		}

		std::string action_of(const std::string& command) {
			auto cut = command.rfind("::");
			return cut == std::string::npos ? command : command.substr(cut + 2);
		}

		text_style label_style() {
			auto s = current_theme().strong();
			s.size -= 1;
			return s;
		}

	}  // namespace

	std::string label(std::string_view name) {
		std::string out;
		for (char c : name) out += c == '_' ? ' ' : c;
		if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
		return out;
	}

	std::vector<desktop::option> list_choices(const state<view_state>& from, const std::string& list, const std::string& shown, const std::string& person) {
		std::vector<desktop::option> out;
		for (const auto& row : rows_of(*from, list)) {
			std::string value_of = person.empty() ? row["id"].as_string() : field_of(row, person).as_string();
			if (value_of.empty() || std::ranges::any_of(out, [&](const desktop::option& o) { return o.value == value_of; })) continue;
			out.push_back({value_of, show(field_of(row, shown))});
		}
		return out;
	}

	desktop::view form(page& p, form_spec spec) {
		if (!spec.when || !spec.allowed) return {};
		const auto& t = current_theme();
		std::string key = "form:" + spec.command + ":" + spec.id;
		auto touched = p.kept<bool>(key + ":touched", false);
		auto open = p.kept<bool>(key + ":open", false);
		// Each field's value as it's written, and for a box or several choices, each ticked or not.
		// Held by what runs once the form's sent, so they hold what they need themselves.
		auto written = [&p, key](const form_field& f) { return p.kept<std::string>(key + ":" + f.name, f.start); };
		auto ticked = [&p, key](const form_field& f, const std::string& choice) { return p.kept<bool>(key + ":" + f.name + ":" + choice, false); };
		auto boxes = [&](const form_field& f) {
			std::vector<desktop::option> all = f.choices;
			if (f.type == "boolean") all = {{"true", ""}};
			return all;
		};
		auto set_ticks = [&](const form_field& f, const std::string& value_of) {
			auto chosen = f.type == "boolean" ? std::vector<std::string>{value_of == "true" ? "true" : ""} : split(value_of);
			for (const auto& o : boxes(f)) {
				bool now = std::ranges::contains(chosen, o.value);
				if (ticked(f, o.value).peek() != now) ticked(f, o.value).set(now);
			}
		};
		// What a field says now, ticked boxes included.
		auto current = [&](const form_field& f) {
			if (f.type == "boolean") return std::string(ticked(f, "true").peek() ? "true" : "false");
			if (f.type != "choices") return written(f).peek();
			std::string now;
			for (const auto& o : f.choices) {
				if (ticked(f, o.value).peek()) now += (now.empty() ? "" : ", ") + o.value;
			}
			return now;
		};
		// An update starts from what's stored, and follows it until someone changes a
		// field: one that no longer says what it was last given from the view.
		if (spec.from && !touched.peek()) {
			for (const auto& f : spec.fields) {
				auto given = p.kept<std::string>(key + ":" + f.name + ":given", f.start);
				if (current(f) != given.peek()) touched.set(true);
			}
		}
		if (spec.from && spec.from->get().status == status::ready && !touched.peek()) {
			const value& stored = spec.from->get().data;
			for (const auto& f : spec.fields) {
				std::string now = as_field(stored[f.name]);
				if (f.type == "boolean") now = now == "true" ? "true" : "false";
				if (written(f).peek() != now) written(f).set(now);
				if (f.type == "boolean" || f.type == "choices") set_ticks(f, now);
				p.kept<std::string>(key + ":" + f.name + ":given", f.start).set(current(f));
			}
		}
		std::string says = spec.submit.empty() ? label(action_of(spec.command)) : spec.submit;
		std::string opens = !spec.opener.empty() ? spec.opener : says;
		if (spec.button && !open.get()) return desktop::button(opens, [open] { open.set(true); }, true);

		std::vector<desktop::view> parts;
		for (const auto& f : spec.fields) {
			std::string shown = f.label.empty() ? label(f.name) : f.label;
			auto value_of = written(f);
			std::vector<desktop::view> field_parts;
			if (f.type != "boolean") field_parts.push_back(desktop::text(shown, label_style()));
			if (f.type == "boolean") {
				field_parts.push_back(desktop::toggle(ticked(f, "true"), shown));
			} else if (f.type == "choice" || f.type == "pick") {
				std::vector<desktop::option> options = f.choices;
				if (!f.required && f.type == "choice") options.insert(options.begin(), {"", "None"});
				std::string lower = shown;
				if (!lower.empty() && lower[0] >= 'A' && lower[0] <= 'Z') lower[0] = static_cast<char>(lower[0] - 'A' + 'a');
				field_parts.push_back(desktop::picker(value_of, std::move(options), p.kept<bool>(key + ":" + f.name + ":open", false), "Choose " + lower + "…"));
			} else if (f.type == "choices") {
				std::vector<desktop::view> each;
				for (const auto& o : f.choices) each.push_back(desktop::toggle(ticked(f, o.value), o.label));
				field_parts.push_back(desktop::column(std::move(each)).spacing(4));
			} else {
				bool several = f.type == "markdown";
				std::string placeholder = f.type == "date" ? "YYYY-MM-DD" : f.type == "list" ? "Separated by commas" : "";
				field_parts.push_back(desktop::field(value_of, {.placeholder = placeholder, .multiline = several, .lines = several ? 6 : 1}));
			}
			if (!f.hint.empty()) field_parts.push_back(desktop::text(f.hint, t.quiet()));
			// A form opened from its button is typed into at once.
			if (spec.button && parts.empty() && field_parts.size() > 1) field_parts[1] = std::move(field_parts[1]).autofocus();
			parts.push_back(desktop::column(std::move(field_parts)).spacing(6));
		}
		if (std::string why = p.error(spec.command); !why.empty()) {
			auto s = t.body();
			s.color = t.danger;
			parts.push_back(desktop::text(why, s));
		}
		bool busy = p.running(spec.command);
		auto send = [&p, spec, key, touched, open, written, ticked]() {
			if (p.running(spec.command)) return;
			std::map<std::string, value> fields;
			for (const auto& f : spec.fields) {
				std::string now = written(f).peek();
				if (f.type == "boolean") now = ticked(f, "true").peek() ? "true" : "false";
				if (f.type == "choices") {
					now.clear();
					for (const auto& o : f.choices) {
						if (ticked(f, o.value).peek()) now += (now.empty() ? "" : ",") + o.value;
					}
				}
				fields[f.name] = as_input(now, f.type);
			}
			for (const auto& [name, given] : spec.given) fields[name] = value(given);
			if (!spec.id.empty()) fields["id"] = value(spec.id);
			bool updating = spec.from.has_value();
			// What's needed once it's done is copied, as the page may be built again by then.
			std::vector<std::pair<state<std::string>, std::string>> emptied, sent;
			std::vector<state<bool>> unticked;
			for (const auto& f : spec.fields) {
				emptied.emplace_back(written(f), f.start);
				// What an update sent is what it was given, so it follows the view again.
				std::string now = f.type == "boolean" ? (ticked(f, "true").peek() ? "true" : "false") : written(f).peek();
				if (f.type == "choices") {
					now.clear();
					for (const auto& o : f.choices) {
						if (ticked(f, o.value).peek()) now += (now.empty() ? "" : ", ") + o.value;
					}
				}
				sent.emplace_back(p.kept<std::string>(key + ":" + f.name + ":given", f.start), now);
				for (const auto& o : f.type == "boolean" ? std::vector<desktop::option>{{"true", ""}} : f.choices) unticked.push_back(ticked(f, o.value));
			}
			std::string after = spec.after;
			std::map<std::string, std::string> params = p.params();
			page* here = &p;
			p.run(spec.command, value(std::move(fields)), [=](const answer& a) {
				if (!a) return;
				if (updating) {
					for (const auto& [given, now] : sent) given.set(now);
					touched.set(false);
				} else {
					for (const auto& [s, start] : emptied) s.set(start);
					for (const auto& b : unticked) b.set(false);
				}
				open.set(false);
				if (!after.empty()) here->open(row_link(after, value::map(), params));
			}, true);
		};
		std::vector<desktop::view> buttons;
		buttons.push_back(desktop::button(busy ? says + "…" : says, send, true));
		if (spec.button) buttons.push_back(desktop::button("Cancel", [open] { open.set(false); }));
		parts.push_back(desktop::row(std::move(buttons)).spacing(8));
		auto panel = desktop::column(std::move(parts)).spacing(12).padding(16).background({.fill = t.surface, .border = t.line, .border_width = 1, .radius = t.corners});
		// Opened from its button, it goes under the title, across the page, in its button's place.
		if (spec.button) {
			p.under_title(std::move(panel));
			return {};
		}
		return panel;
	}

}  // namespace one::desktop::runtime
