// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// What each thing on a screen works out before it's drawn: the web's rules, here
// once for every toolkit.

#include "one/screens/models.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <one/client/client.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <random>
#include <set>
#include <thread>

extern char** environ;

namespace one::screens {

	namespace {

		std::string lower(std::string s) {
			for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		std::string trimmed(const std::string& s, const char* spaces = " \n\t") {
			std::size_t from = s.find_first_not_of(spaces), to = s.find_last_not_of(spaces);
			return from == std::string::npos ? "" : s.substr(from, to - from + 1);
		}

		std::vector<std::string> cut(const std::string& s, const std::string& by) {
			std::vector<std::string> out;
			std::size_t at = 0;
			for (std::size_t next; (next = s.find(by, at)) != std::string::npos; at = next + by.size()) out.push_back(s.substr(at, next - at));
			out.push_back(s.substr(at));
			return out;
		}

		std::string spaced(std::string s) {
			std::ranges::replace(s, '_', ' ');
			return s;
		}

		bool nothing(const value& v) { return v.is_null() || (v.is_string() && v.text.empty()) || (v.is_array() && v.items.empty()); }

		std::string action_of(const std::string& command) {
			auto at = command.rfind("::");
			return at == std::string::npos ? command : command.substr(at + 2);
		}

		bool numbered(const std::string& field) { return field == "number" || field.ends_with(".number"); }

		std::optional<double> number_of(const value& v) {
			if (v.is_integer() || v.is_double()) return v.as_double();
			return std::nullopt;
		}

		// A list of changes with each command said once: one like close, setting status
		// and closed_at together, keeps only its first change.
		std::vector<const value*> once_each(const std::vector<value>& rows) {
			auto made = [](const value& row) {
				auto parts = cut(row["action"].as_string(), "::");
				return parts.size() >= 2 && parts[parts.size() - 2] == row["field"].as_string();
			};
			std::vector<const value*> kept_rows;
			for (const auto& row : rows) {
				bool said = row["field"].is_string() && !row["field"].text.empty() && row.find("before") && row.find("after") && nothing(row["before"]) && nothing(row["after"]);
				if (made(row) || !said) kept_rows.push_back(&row);
			}
			std::vector<const value*> out;
			for (std::size_t i = 0; i < kept_rows.size(); ++i) {
				const value& row = *kept_rows[i];
				std::string act = action_of(row["action"].as_string());
				bool own = row["action"].is_string() && act != "create" && act != "update" && act != "delete";
				if (own && i > 0 && (*kept_rows[i - 1])["action"].as_string() == row["action"].as_string() && show((*kept_rows[i - 1])["created_at"]) == show(row["created_at"])) continue;
				out.push_back(&row);
			}
			return out;
		}

		// What a form field shows for a value from a view: a date as 2026-09-30, a list
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

		std::vector<std::string> split(const std::string& written) {
			std::vector<std::string> out;
			std::size_t at = 0;
			while (at <= written.size()) {
				std::size_t comma = written.find(',', at);
				if (comma == std::string::npos) comma = written.size();
				if (std::string item = trimmed(written.substr(at, comma - at), " "); !item.empty()) out.push_back(item);
				at = comma + 1;
			}
			return out;
		}

		// Each line written, as files are, one a line.
		std::vector<std::string> split_lines(const std::string& written) {
			std::vector<std::string> out;
			std::size_t at = 0;
			while (at <= written.size()) {
				std::size_t end = written.find('\n', at);
				if (end == std::string::npos) end = written.size();
				out.push_back(written.substr(at, end - at));
				at = end + 1;
			}
			return out;
		}

		// What a form sends for a field: a yes or no as true or false, a number as one, a
		// list or several choices as the list of them, and anything else as it's written.
		value as_input(const std::string& written, const std::string& type) {
			if (type == "boolean") return value(written == "true");
			// A number as a number, and nothing for an empty box.
			if (type == "number" || type == "slider") {
				char* end = nullptr;
				double n = std::strtod(written.c_str(), &end);
				if (written.empty()) return value();
				return end == written.c_str() + written.size() ? value(n) : value(written);
			}
			if (type != "list" && type != "choices") return value(written);
			std::vector<value> items;
			for (auto& item : split(written)) items.emplace_back(item);
			return value(std::move(items));
		}

		// The roles the person holds within a place, by their ids.
		std::set<std::string> roles_held(const std::optional<state<view_state>>& roles, const std::string& place, const std::string& role, const std::string& within) {
			std::set<std::string> out;
			if (!roles) return out;
			for (const auto& row : rows_of(roles->get())) {
				if (field_of(row, place).as_string() == within) out.insert(field_of(row, role).as_string());
			}
			return out;
		}

		// Orders by a field, largest or latest first with a -.
		void sorted(std::vector<value>& rows, const std::string& by) {
			if (by.empty()) return;
			bool down = by.starts_with('-');
			std::string field = down ? by.substr(1) : by;
			auto less = [&](const value& a, const value& b) {
				const value &x = field_of(a, field), &y = field_of(b, field);
				if (number_of(x) && number_of(y)) return *number_of(x) < *number_of(y);
				if (x.is_time() && y.is_time()) return x.as_time() < y.as_time();
				return show(x) < show(y);
			};
			std::ranges::stable_sort(rows, [&](const value& a, const value& b) { return down ? less(b, a) : less(a, b); });
		}

	}  // namespace

	// Words

	std::string done(const std::string& action) {
		static const std::map<std::string, std::string> irregular{
		    {"take", "took"}, {"make", "made"}, {"give", "gave"}, {"send", "sent"}, {"leave", "left"}, {"begin", "began"}, {"write", "wrote"},
		    {"set", "set"},   {"put", "put"},   {"run", "ran"},   {"find", "found"}, {"hold", "held"},  {"keep", "kept"},   {"get", "got"},
		    {"bring", "brought"}, {"buy", "bought"}, {"pay", "paid"}, {"lead", "led"}};
		static const std::set<std::string> stressed_last{"transfer", "refer", "prefer", "defer", "confer", "infer", "admit", "commit", "submit",
		                                                  "permit",   "omit",  "compel", "expel", "occur", "recur", "regret", "control"};
		auto words_of = cut(action, "_");
		std::string verb = words_of.front(), past;
		auto vowel = [](char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; };
		// One vowel, before a last consonant other than w, x or y, like stop: stopped.
		auto short_closed = [&] {
			std::size_t first = verb.find_first_of("aeiou");
			if (first == std::string::npos || first + 2 != verb.size()) return false;
			char last = verb.back();
			return !vowel(last) && last != 'w' && last != 'x' && last != 'y';
		};
		if (auto found = irregular.find(verb); found != irregular.end()) past = found->second;
		else if (verb.ends_with('e')) past = verb + "d";
		else if (verb.size() >= 2 && verb.back() == 'y' && !vowel(verb[verb.size() - 2])) past = verb.substr(0, verb.size() - 1) + "ied";
		else if (short_closed() || stressed_last.contains(verb)) past = verb + verb.back() + "ed";
		else past = verb + "ed";
		words_of.front() = past;
		std::string out;
		for (const auto& w : words_of) out += (out.empty() ? "" : " ") + w;
		return out;
	}

	std::pair<std::string, std::string> phrase(const value& field, const value& before, const value& after, const value& command) {
		auto parts = command.is_string() ? cut(command.text, "::") : std::vector<std::string>{};
		std::string made = parts.size() >= 2 ? parts[parts.size() - 2] : "";
		std::string name = field.as_string();
		// A change kept for something of its own, like a comment on an issue.
		if (!name.empty() && name == made) {
			std::string thing = spaced(made), did = parts.back();
			if (did == "create") return {"added a " + thing + " to", ""};
			if (did == "delete") return {"removed a " + thing + " from", ""};
			return {"changed a " + thing + " on", ""};
		}
		std::string verb = command.is_string() ? parts.back() : "";
		if (!verb.empty() && verb != "create" && verb != "update" && verb != "delete") {
			std::string was = show(before), is = show(after);
			bool says = !name.empty() && !was.empty() && !is.empty() && lower(done(verb)).find(lower(is)) == std::string::npos;
			return {done(verb), says ? "from " + was + " to " + is : ""};
		}
		if (name.empty()) return {"created", ""};
		std::string spoken = spaced(name), was = show(before), is = show(after);
		auto long_text = [](const std::string& t) { return t.size() > 80 || t.find('\n') != std::string::npos; };
		if (long_text(was) || long_text(is)) return {was.empty() ? "wrote " + spoken + " of" : is.empty() ? "cleared " + spoken + " of" : "changed " + spoken + " of", ""};
		if (was.empty()) return {"set " + spoken + " of", "to " + is};
		if (is.empty()) return {"cleared " + spoken + " of", ""};
		return {"changed " + spoken + " of", "from " + was + " to " + is};
	}

	std::string changed(const value& field, const value& before, const value& after, const value& command) {
		auto [head, tail] = phrase(field, before, after, command);
		return head.ends_with(" of") ? head.substr(0, head.size() - 3) + (tail.empty() ? "" : " " + tail) : head + " this" + (tail.empty() ? "" : " " + tail);
	}

	std::string when(const value& at) {
		if (!at.is_time()) return show(at);
		auto t = std::chrono::zoned_time(time_zone(), std::chrono::floor<std::chrono::minutes>(at.as_time())).get_local_time();
		std::string day = std::format("{:%b %e, %Y, %H:%M}", t);
		if (auto doubled = day.find("  "); doubled != std::string::npos) day.erase(doubled, 1);
		return day;
	}

	std::vector<option> list_choices(const state<view_state>& from, const std::string& list, const std::string& shown, const std::string& person) {
		std::vector<option> out;
		for (const auto& row : rows_of(*from, list)) {
			std::string value_of = person.empty() ? row["id"].as_string() : field_of(row, person).as_string();
			if (value_of.empty() || std::ranges::any_of(out, [&](const option& o) { return o.value == value_of; })) continue;
			out.push_back({value_of, show(field_of(row, shown))});
		}
		return out;
	}

	std::string markdown_of(const std::string& title, const value& data, const std::vector<copied_field>& fields, const std::vector<copied_list>& lists,
	                        const std::map<std::string, std::vector<option>>& choices) {
		std::vector<std::string> out;
		if (!title.empty()) out.insert(out.end(), {"# " + title, ""});
		auto value_of = [&](const std::string& name) {
			const value& stored = field_of(data, name);
			if (auto found = choices.find(name); found != choices.end() && stored.is_string()) return shown_as(found->second, stored.text);
			return show(stored);
		};
		bool said_any = false;
		for (const auto& f : fields) {
			if (f.markdown || value_of(f.name).empty()) continue;
			out.push_back("**" + f.label + ":** " + value_of(f.name) + "  ");
			said_any = true;
		}
		if (said_any) out.push_back("");
		for (const auto& f : fields) {
			std::string body = trimmed(field_of(data, f.name).as_string());
			if (f.markdown && !body.empty()) out.insert(out.end(), {"## " + f.label, "", body, ""});
		}
		for (const auto& l : lists) {
			const auto& rows = data[l.name].as_array();
			if (rows.empty()) continue;
			out.insert(out.end(), {"## " + l.label, ""});
			for (const auto& row : rows) {
				if (l.kind == "thread") {
					out.insert(out.end(), {"**" + show(row["author.name"]) + "** · " + when(row["created_at"]), "", trimmed(row["body"].as_string()), ""});
				} else if (l.kind == "changes") {
					std::string who = show(row["created_by.name"]);
					out.push_back("- " + (who.empty() ? std::string("System") : who) + " " + changed(row["field"], row["before"], row["after"], row["action"]) + " · " + when(row["created_at"]));
				} else {
					std::string line;
					for (const auto& c : l.columns) {
						if (std::string v = show(field_of(row, c)); !v.empty()) line += (line.empty() ? "" : " · ") + v;
					}
					out.push_back("- " + line);
				}
			}
			if (l.kind != "thread") out.push_back("");
		}
		std::string joined;
		for (const auto& line : out) joined += line + "\n";
		return trimmed(joined) + "\n";
	}

	std::string without_license(std::string source) {
		if (source.starts_with("// Copyright ") || source.starts_with("# Copyright ")) {
			std::size_t at = source.find("SPDX-License-Identifier: ");
			if (at != std::string::npos) {
				at = source.find('\n', at);
				while (at != std::string::npos && at + 1 < source.size() && source[at + 1] == '\n') ++at;
				source = at == std::string::npos ? "" : source.substr(at + 1);
			}
		}
		while (!source.empty() && source.back() == '\n') source.pop_back();
		return source;
	}

	bool allows(const state<view_state>& roles, const std::string& place_field, const std::string& place, const std::string& command, const std::string& allowed) {
		return one::client::allows(roles.get().data, place_field, place, command, allowed);
	}

	bool holds(const state<view_state>& roles, const std::string& place_field, const std::string& place, const std::vector<std::string>& granting) {
		return one::client::holds(roles.get().data, place_field, place, granting);
	}

	int tone_of(const value& row, const std::optional<tint_spec>& tint) {
		if (!tint) return 3;
		auto at = std::ranges::find(tint->order, show(field_of(row, tint->field)));
		if (at == tint->order.end() || tint->order.size() == 1) return 3;
		return static_cast<int>(std::lround(1 + static_cast<double>(at - tint->order.begin()) * 4 / static_cast<double>(tint->order.size() - 1)));
	}

	bool ready(const state<view_state>& v) { return v.get().status == status::ready; }

	const value& at(const state<view_state>& v, const std::string& field) { return field_of(v.get().data, field); }

	// Whether a field holds none: it's missing or null, or it's empty text or an empty
	// list, as an entity's field reads before it's given a value.
	bool none(const value& v) {
		return v.is_null() || (v.is_string() && v.as_string().empty()) || (v.is_array() && v.as_array().empty());
	}

	bool same(const value& a, const value& b) {
		// Compared with none, anything holding nothing is none.
		if (a.is_null() || b.is_null()) return none(a) && none(b);
		// A number is a number, whether it came as a whole one or not.
		if (number_of(a) && number_of(b)) return *number_of(a) == *number_of(b);
		return ember::to_json(a) == ember::to_json(b);
	}

	bool has(const value& list, const value& item) { return std::ranges::any_of(list.as_array(), [&](const value& held) { return same(held, item); }); }

	int compared(const value& a, const value& b) {
		if (a.is_null() || b.is_null()) return a.is_null() == b.is_null() ? 0 : a.is_null() ? -1 : 1;
		if (number_of(a) && number_of(b)) return *number_of(a) < *number_of(b) ? -1 : *number_of(a) > *number_of(b) ? 1 : 0;
		if (a.is_time() && b.is_time()) return a.as_time() < b.as_time() ? -1 : a.as_time() > b.as_time() ? 1 : 0;
		std::string x = show(a), y = show(b);
		return x < y ? -1 : x > y ? 1 : 0;
	}

	// Around a page

	std::vector<crumb_link> crumb_links(page_base& p, const std::vector<crumb>& trail) {
		std::vector<crumb_link> out;
		for (const auto& c : trail) {
			// Its parameters this page's address doesn't have, from the views that hold them.
			std::string address;
			std::size_t at = 0;
			while (at < c.to.size()) {
				std::size_t end = c.to.find('/', at + 1);
				std::string segment = c.to.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
				address += "/";
				if (segment.starts_with(':')) {
					std::string name = segment.substr(1);
					if (auto f = c.fill.find(name); f != c.fill.end()) {
						std::string held = show(field_of(f->second.from.get().data, f->second.field));
						auto split_up = f->second.keys > 1 ? parts_of(held, static_cast<std::size_t>(f->second.keys)) : std::nullopt;
						address += split_up ? split_up->back() : held;
					} else {
						address += p.param(name);
					}
				} else {
					address += segment;
				}
				at = end == std::string::npos ? c.to.size() : end;
			}
			out.push_back({address, said(c.title)});
		}
		return out;
	}

	std::vector<menu_entry> menu_entries(page_base& p, const std::vector<menu_link>& links) {
		std::vector<menu_entry> out;
		std::string here = p.address().substr(0, p.address().find('?'));
		for (const auto& l : links) {
			std::string to = filled(l.to, p.params());
			out.push_back({to, l.label, to == here});
		}
		return out;
	}

	bool leaves(const std::string& to) { return to.starts_with("https://") || to.starts_with("http://") || to.starts_with("mailto:"); }

	std::string address_of(page_base& p, const std::string& to) { return leaves(to) || to.find(':') == std::string::npos ? to : filled(to, p.params()); }

	// Commands

	std::optional<command_model> command_of(page_base& p, const command_button& b) {
		if (!b.allowed || !b.when) return std::nullopt;
		value fields = b.fields.is_map() ? b.fields : value::map();
		if (!b.id.empty()) fields.fields["id"] = value(b.id);
		bool busy = p.running(b.command);
		return command_model{busy ? b.label + "…" : b.label, busy, std::move(fields)};
	}

	steps_model steps_of(page_base& p, const steps_spec& spec) {
		steps_model out;
		const view_state& current = *spec.current;
		const view_state& steps_view = *spec.steps;
		if (current.status != status::ready || steps_view.status != status::ready) return out;
		std::string now = field_of(current.data, spec.field).as_string();
		std::set<std::string> mine = roles_held(spec.roles, spec.place, spec.role, spec.within);
		std::vector<const value*> leaving, open;
		for (const auto& step : rows_of(steps_view, spec.list)) {
			if (step["from"].as_string() != now) continue;
			leaving.push_back(&step);
			if (spec.held.empty() || std::ranges::any_of(field_of(step, spec.held).as_array(), [&](const value& r) { return mine.contains(r.as_string()); })) open.push_back(&step);
		}
		// Someone who holds a role there is told when none of theirs moves it on.
		if (open.empty()) {
			if (spec.held.empty() || mine.empty() || now.empty()) return out;
			std::string here = !spec.here.empty() ? show(field_of(current.data, spec.here)) : !spec.from.empty() && !leaving.empty() ? show(field_of(*leaving.front(), spec.from)) : "";
			out.none = here.empty() ? "No moves from here for your roles" : "No moves from " + here + " for your roles";
			return out;
		}
		out.busy = p.running(spec.command);
		for (const value* step : open) {
			std::string says = !spec.shown.empty() ? show(field_of(*step, spec.shown)) : "";
			if (says.empty() && !spec.to.empty() && !show(field_of(*step, spec.to)).empty()) says = "Move to " + show(field_of(*step, spec.to));
			if (says.empty()) says = label(action_of(spec.command));
			std::map<std::string, value> fields{{spec.field, (*step)["to"]}};
			if (!spec.id.empty()) fields["id"] = value(spec.id);
			out.buttons.push_back({out.busy ? says + "…" : says, value(fields)});
		}
		return out;
	}

	// Forms

	form_model::form_model(page_base& p, const form_spec& spec) : page_(p), spec_(spec) {
		key_ = "form:" + spec_.command + ":" + (spec_.key.empty() ? spec_.id : spec_.key);
		says_ = spec_.submit.empty() ? label(action_of(spec_.command)) : spec_.submit;
		opens_ = !spec_.opener.empty() ? spec_.opener : says_;
		if (shown()) follow();
	}

	bool form_model::open() const { return !spec_.button || page_.kept<bool>(key_ + ":open", false).get(); }
	void form_model::set_open(bool now) const { page_.kept<bool>(key_ + ":open", false).set(now); }
	state<std::string> form_model::written(const form_field& f) const { return page_.kept<std::string>(key_ + ":" + f.name, f.start); }
	state<bool> form_model::ticked(const form_field& f, const std::string& choice) const { return page_.kept<bool>(key_ + ":" + f.name + ":" + choice, false); }
	state<bool> form_model::picking(const form_field& f) const { return page_.kept<bool>(key_ + ":" + f.name + ":open", false); }

	std::vector<option> form_model::choices_of(const form_field& f) const {
		std::vector<option> out = f.choices;
		if (!f.required && f.type == "choice") out.insert(out.begin(), {"", "None"});
		return out;
	}

	// What a field says now, ticked boxes included.
	std::string form_model::current(const form_field& f) const {
		if (f.type == "boolean") return ticked(f, "true").peek() ? "true" : "false";
		if (f.type != "choices") return written(f).peek();
		std::string now;
		for (const auto& o : f.choices) {
			if (ticked(f, o.value).peek()) now += (now.empty() ? "" : ", ") + o.value;
		}
		return now;
	}

	// An update starts from what's stored, and follows it until someone changes a
	// field: one that no longer says what it was last given from the view.
	void form_model::follow() {
		if (!spec_.from) return;
		auto touched = page_.kept<bool>(key_ + ":touched", false);
		auto given = [&](const form_field& f) { return page_.kept<std::string>(key_ + ":" + f.name + ":given", f.start); };
		if (!touched.peek()) {
			for (const auto& f : spec_.fields) {
				if (current(f) != given(f).peek()) touched.set(true);
			}
		}
		if (spec_.from->get().status != status::ready || touched.peek()) return;
		const value& stored = spec_.from->get().data;
		for (const auto& f : spec_.fields) {
			// What's attached is shown by the page; the field is for files to add.
			if (f.type == "file" || f.type == "files") {
				given(f).set(current(f));
				continue;
			}
			std::string now = as_field(stored[f.name]);
			if (f.type == "boolean") now = now == "true" ? "true" : "false";
			if (written(f).peek() != now) written(f).set(now);
			if (f.type == "boolean" || f.type == "choices") {
				auto chosen = f.type == "boolean" ? std::vector<std::string>{now == "true" ? "true" : ""} : split(now);
				std::vector<option> boxes = f.choices;
				if (f.type == "boolean") boxes.assign(1, option{"true", ""});
				for (const auto& o : boxes) {
					bool ticks = std::ranges::contains(chosen, o.value);
					if (ticked(f, o.value).peek() != ticks) ticked(f, o.value).set(ticks);
				}
			}
			given(f).set(current(f));
		}
	}

	void form_model::send() const {
		if (page_.running(spec_.command)) return;
		std::map<std::string, value> fields;
		attached_files files;
		for (const auto& f : spec_.fields) {
			// Files are written as where they are on this machine, one a line, and
			// uploaded as it's sent; a list keeps what it had and adds them. A field with
			// none written isn't sent, so what's attached stays.
			if (f.type == "file" || f.type == "files") {
				std::vector<std::string> written;
				for (const auto& line : split_lines(current(f))) {
					if (!local_path(line).empty()) written.push_back(line);
				}
				if (written.empty()) continue;
				files[f.name] = written;
				if (f.type == "files") {
					value had = spec_.from && spec_.from->get().status == status::ready ? spec_.from->get().data[f.name] : value();
					fields[f.name] = had.is_array() ? had : value(std::vector<value>{});
				}
				continue;
			}
			fields[f.name] = as_input(current(f), f.type);
		}
		for (const auto& [name, given] : spec_.given) fields[name] = value(given);
		if (!spec_.id.empty()) fields["id"] = value(spec_.id);
		bool updating = spec_.from.has_value();
		// What's needed once it's done is copied, as the page may be built again by then.
		std::vector<std::pair<state<std::string>, std::string>> emptied, sent;
		std::vector<state<std::string>> uploaded;  // files sent, emptied whatever the form
		std::vector<state<bool>> unticked;
		for (const auto& f : spec_.fields) {
			if (f.type == "file" || f.type == "files") uploaded.push_back(written(f));
			emptied.emplace_back(written(f), f.start);
			// What an update sent is what it was given, so it follows the view again.
			sent.emplace_back(page_.kept<std::string>(key_ + ":" + f.name + ":given", f.start), current(f));
			std::vector<option> boxes = f.choices;
			if (f.type == "boolean") boxes.assign(1, option{"true", ""});
			for (const auto& o : boxes) unticked.push_back(ticked(f, o.value));
		}
		auto touched = page_.kept<bool>(key_ + ":touched", false);
		auto opened = page_.kept<bool>(key_ + ":open", false);
		std::string after = spec_.after;
		auto on_done = spec_.on_done;
		std::map<std::string, std::string> params = page_.params();
		page_base* here = &page_;
		page_.run(spec_.command, value(std::move(fields)), std::move(files), [=](const answer& a) {
			if (!a) return;
			for (const auto& s : uploaded) s.set("");
			if (updating) {
				for (const auto& [given, now] : sent) given.set(now);
				touched.set(false);
			} else {
				for (const auto& [s, start] : emptied) s.set(start);
				for (const auto& b : unticked) b.set(false);
			}
			opened.set(false);
			if (on_done) on_done();
			if (!after.empty()) here->open(row_link(after, value::map(), params));
		}, true);
	}

	// Lists

	std::optional<track_model> track_of(page_base& p, const std::string& key, const std::optional<track_spec>& spec, const std::vector<value>& rows, const std::string& noun) {
		if (!spec) return std::nullopt;
		auto picking = p.kept<std::string>(key + ":track");
		const auto& records = rows_of(spec->view.get(), spec->list);
		if (records.empty()) return std::nullopt;
		track_model out{label(spec->field), {}, picking.get(), picking};
		for (const auto& record : records) {
			std::string id = record["id"].as_string();
			auto count = std::ranges::count_if(rows, [&](const value& row) { return field_of(row, spec->field).as_string() == id; });
			std::string title = show(field_of(record, spec->shown));
			track_record r{id, title.empty() ? id : title, {}, count, out.picked == id};
			for (const auto& c : spec->columns) {
				if (std::string said_now = show(field_of(record, c.field)); !said_now.empty()) r.details.push_back(c.label + " " + said_now);
			}
			// One issue, two issues.
			r.details.push_back(std::format("{} {}", count, count == 1 && noun.ends_with('s') ? noun.substr(0, noun.size() - 1) : noun));
			out.records.push_back(std::move(r));
		}
		return out;
	}

	namespace {

		// The numbers that put a row between two others, as the web works them out:
		// halfway between theirs, one past the last, one before the first, or, when
		// there's no room, every row numbered again in its new order.
		std::vector<std::pair<std::string, double>> placed(const std::vector<value>& list, const std::string& field, const value& row, const value* before, const value* after) {
			auto low = before ? number_of(field_of(*before, field)) : std::nullopt;
			auto high = after ? number_of(field_of(*after, field)) : std::nullopt;
			std::string id = row["id"].as_string();
			if (low && high && *low < *high) return {{id, (*low + *high) / 2}};
			if (low && !high && !after) return {{id, *low + 1}};
			if (high && !low && !before) return {{id, *high - 1}};
			if (!before && !after) return {};
			std::vector<const value*> order;
			for (const auto& other : list) {
				if (other["id"].as_string() != id) order.push_back(&other);
			}
			std::size_t at = 0;
			for (std::size_t i = 0; i < order.size(); ++i) {
				if (before && (*order[i])["id"].as_string() == (*before)["id"].as_string()) at = i + 1;
				if (!before && after && (*order[i])["id"].as_string() == (*after)["id"].as_string()) at = i;
			}
			order.insert(order.begin() + static_cast<std::ptrdiff_t>(at), &row);
			std::vector<std::pair<std::string, double>> out;
			for (std::size_t i = 0; i < order.size(); ++i) {
				auto now = number_of(field_of(*order[i], field));
				if (!now || *now != static_cast<double>(i + 1)) out.emplace_back((*order[i])["id"].as_string(), static_cast<double>(i + 1));
			}
			return out;
		}

	}  // namespace

	std::vector<value> ordered(page_base& p, const std::string& key, const std::optional<reorder_spec>& spec, std::vector<value> rows) {
		if (!spec) return rows;
		auto placing = p.kept<std::map<std::string, double>>(key + ":placing");
		// A place the view now shows is settled.
		if (!placing.peek().empty()) {
			auto now = placing.peek();
			std::erase_if(now, [&](const auto& placed_row) {
				return std::ranges::any_of(rows, [&](const value& row) {
					auto n = number_of(field_of(row, spec->field));
					return row["id"].as_string() == placed_row.first && n && *n == placed_row.second;
				});
			});
			if (now.size() != placing.peek().size()) placing.set(now);
		}
		for (auto& row : rows) {
			if (auto found = placing.get().find(row["id"].as_string()); found != placing.get().end()) row.fields[spec->field] = value(found->second);
		}
		std::ranges::stable_sort(rows, [&](const value& a, const value& b) {
			auto x = number_of(field_of(a, spec->field)), y = number_of(field_of(b, spec->field));
			return x && y && *x < *y;
		});
		return rows;
	}

	bool move_row(page_base& p, const std::string& key, const reorder_spec& spec, const std::vector<value>& around, const std::string& id, int by) {
		if (!spec.allowed) return false;
		auto at = std::ranges::find_if(around, [&](const value& row) { return row["id"].as_string() == id; });
		if (at == around.end()) return false;
		std::ptrdiff_t index = at - around.begin(), to = index + by;
		if (to < 0 || to >= static_cast<std::ptrdiff_t>(around.size())) return false;
		std::vector<const value*> rest;
		for (const auto& row : around) {
			if (row["id"].as_string() != id) rest.push_back(&row);
		}
		const value* before = to > 0 ? rest[static_cast<std::size_t>(to - 1)] : nullptr;
		const value* after = static_cast<std::size_t>(to) < rest.size() ? rest[static_cast<std::size_t>(to)] : nullptr;
		auto changes = placed(around, spec.field, *at, before, after);
		auto placing = p.kept<std::map<std::string, double>>(key + ":placing");
		placing.change([&](auto& now) {
			for (const auto& [row, number] : changes) now[row] = number;
		});
		for (const auto& [row, number] : changes) {
			p.run(spec.command, value(std::map<std::string, value>{{"id", row}, {spec.field, number}}), [placing, row](const answer& a) {
				if (!a) placing.change([&](auto& now) { now.erase(row); });
			}, true);
		}
		return true;
	}

	std::string cell_text(const std::map<std::string, std::vector<option>>& choices, const value& row, const std::string& column) {
		std::string said_now = show(field_of(row, column));
		if (auto found = choices.find(column); found != choices.end()) return shown_as(found->second, said_now);
		return said_now;
	}

	std::vector<std::string> people_in(const value& row, const std::string& column) {
		std::vector<std::string> names;
		const value& v = field_of(row, column);
		if (v.is_array()) {
			for (const auto& name : v.items)
				if (!show(name).empty()) names.push_back(show(name));
		} else if (!show(v).empty()) {
			names.push_back(show(v));
		}
		return names;
	}

	state<view_state> acted_on(page_base& p, const std::string& list, const std::string& command, const value& row) {
		auto started = p.kept<view_state>("table:" + list + ":" + command + ":" + row["id"].as_string() + ":row");
		if (started.peek().status != status::ready || ember::to_json(started.peek().data) != ember::to_json(row)) started.set(view_state{status::ready, row, ""});
		return started;
	}

	table_model table_rows(page_base& p, const state<view_state>& view, const std::string& list, const std::vector<std::pair<std::string, std::string>>& only, bool hide_empty,
	                       const std::string& sort, const std::optional<reorder_spec>& reorder, const std::optional<track_spec>& track,
	                       const std::vector<std::string>& search, const std::map<std::string, std::vector<option>>& choices, const std::string& by, int page_size) {
		table_model out;
		out.key = "table:" + (list.empty() ? std::string("rows") : list);
		out.query = p.kept<std::string>(out.key + ":search");
		out.tab = p.kept<std::string>(out.key + ":tab");
		out.at = p.kept<int>(out.key + ":page", 0);
		out.acting = p.kept<std::string>(out.key + ":acting");  // the row and action whose form is open
		std::vector<value> rows;
		for (const auto& row : rows_of(view.get(), list)) {
			if (p.keeps(row) && kept(row, only, p.me())) rows.push_back(row);
		}
		if (rows.empty() && hide_empty) {
			out.hidden = true;
			return out;
		}
		sorted(rows, sort);
		rows = ordered(p, out.key, reorder, std::move(rows));
		out.track = track_of(p, out.key, track, rows, list.empty() ? std::string("rows") : list);
		if (track && out.track) std::erase_if(rows, [&](const value& row) { return !out.track->keeps(row, track->field); });
		// What's typed, over the fields it searches, as they're shown.
		std::string wanted = lower(out.query.get());
		if (!wanted.empty()) {
			std::erase_if(rows, [&](const value& row) {
				return std::ranges::none_of(search, [&](const std::string& f) { return lower(cell_text(choices, row, f)).find(wanted) != std::string::npos; });
			});
		}
		out.arranging = reorder && reorder->allowed && wanted.empty();
		// Tabs, one for each choice of the column it's by, with how many each has.
		if (!by.empty()) {
			auto found = choices.find(by);
			std::vector<option> tab_choices = found != choices.end() ? found->second : std::vector<option>{};
			std::string open = out.tab.get().empty() && !tab_choices.empty() ? tab_choices.front().value : out.tab.get();
			for (const auto& o : tab_choices) {
				auto count = std::ranges::count_if(rows, [&](const value& row) { return show(field_of(row, by)) == o.value; });
				out.tabs.push_back({o.value, o.label, count, o.value == open});
			}
			std::erase_if(rows, [&](const value& row) { return show(field_of(row, by)) != open; });
		}
		// A page of them, when it says how many a page has.
		out.total = rows.size();
		out.first = 0;
		out.last = out.total;
		if (page_size > 0 && out.total > 0) {
			std::size_t size = static_cast<std::size_t>(page_size);
			std::size_t pages = (out.total + size - 1) / size;
			std::size_t now = std::min(static_cast<std::size_t>(std::max(0, out.at.get())), pages - 1);
			out.first = now * size;
			out.last = std::min(out.total, out.first + size);
		}
		out.rows.assign(rows.begin() + static_cast<std::ptrdiff_t>(out.first), rows.begin() + static_cast<std::ptrdiff_t>(out.last));
		return out;
	}

	board_model board_of(page_base& p, const board_spec& spec) {
		board_model out;
		out.key = "board:" + spec.list;
		const view_state& v = *spec.view;
		const view_state& over = *spec.over;
		if (v.status == status::loading || over.status == status::loading) {
			out.loading = true;
			return out;
		}
		// Where each moved card is until the view says so too.
		auto moved = p.kept<std::map<std::string, std::string>>(out.key + ":moved");
		out.moving = p.kept<std::string>(out.key + ":moving");  // the card whose steps are offered
		out.query = p.kept<std::string>(out.key + ":search");
		auto all = ordered(p, out.key, spec.reorder, rows_of(v, spec.list));
		out.track = track_of(p, out.key, spec.track, all, spec.list);
		// A move the view now shows is settled.
		if (!moved.peek().empty()) {
			auto settled = moved.peek();
			std::erase_if(settled, [&](const auto& m) {
				return std::ranges::any_of(all, [&](const value& card) { return card["id"].as_string() == m.first && field_of(card, spec.by).as_string() == m.second; });
			});
			if (settled.size() != moved.peek().size()) moved.set(settled);
		}
		auto where = [&](const value& card) {
			auto found = moved.get().find(card["id"].as_string());
			return found != moved.get().end() ? found->second : field_of(card, spec.by).as_string();
		};
		std::set<std::string> mine = spec.move ? roles_held(spec.move->roles, spec.move->place, spec.move->role, spec.move->within) : std::set<std::string>{};
		// The columns a step from where a card is leads to, for one of the person's roles.
		auto reaches = [&](const std::string& at) {
			std::vector<std::string> to;
			if (!spec.move) return to;
			for (const auto& step : rows_of(spec.move->steps.get(), spec.move->list)) {
				if (step["from"].as_string() != at) continue;
				if (!spec.move->held.empty() && std::ranges::none_of(field_of(step, spec.move->held).as_array(), [&](const value& r) { return mine.contains(r.as_string()); }))
					continue;
				to.push_back(step["to"].as_string());
			}
			return to;
		};
		std::string wanted = lower(out.query.get());
		out.arranging = spec.reorder && spec.reorder->allowed && wanted.empty();
		auto keeps = [&](const value& card) {
			if (!p.keeps(card)) return false;
			if (spec.track && out.track && !out.track->keeps(card, spec.track->field)) return false;
			if (wanted.empty()) return true;
			return std::ranges::any_of(spec.search, [&](const std::string& f) { return lower(show(field_of(card, f))).find(wanted) != std::string::npos; });
		};
		const auto& things = rows_of(over, spec.over_list);
		auto title_of = [&](const std::string& id) {
			for (const auto& thing : things) {
				if (thing["id"].as_string() == id) return show(field_of(thing, spec.shown));
			}
			return id;
		};
		std::string title = spec.columns.empty() ? "" : spec.columns.front().field;
		// The steps offered for the card being moved.
		if (!out.moving.get().empty()) {
			for (const auto& card : all) {
				if (card["id"].as_string() != out.moving.get()) continue;
				board_offer offer{card["id"].as_string(), show(field_of(card, title)), {}};
				for (const auto& to : reaches(where(card))) offer.steps.push_back({to, title_of(to)});
				out.offer = std::move(offer);
			}
		}
		for (const auto& thing : things) {
			board_column column{thing["id"].as_string(), title_of(thing["id"].as_string()), {}, {}};
			for (const auto& card : all) {
				if (where(card) != column.id || !keeps(card)) continue;
				column.rows.push_back(card);
				board_card shown{card["id"].as_string(), show(field_of(card, title)), {}, {}, {}, tone_of(card, spec.tint), ""};
				for (std::size_t c = 1; c < spec.columns.size(); ++c) {
					const auto& f = spec.columns[c].field;
					if (spec.people.contains(f)) {
						for (auto& name : people_in(card, f)) shown.people.push_back(std::move(name));
					} else if (std::ranges::contains(spec.labels, f)) {
						for (const auto& word : field_of(card, f).as_array()) shown.labels.push_back(show(word));
					} else if (std::string said_now = show(field_of(card, f)); !said_now.empty()) {
						shown.details.push_back(numbered(f) ? "#" + said_now : said_now);
					}
				}
				if (!spec.link.empty()) shown.link = row_link(spec.link, card, p.params(), spec.keyed, spec.named);
				column.cards.push_back(std::move(shown));
			}
			column.notify = std::ranges::contains(spec.notify, column.id);
			out.columns.push_back(std::move(column));
		}
		// A card that arrives in a column that notifies chimes, once; those there when
		// the board's first drawn don't.
		if (!spec.notify.empty()) {
			auto seen = p.kept<std::optional<std::map<std::string, std::set<std::string>>>>(out.key + ":seen");
			std::map<std::string, std::set<std::string>> now;
			for (const auto& column : out.columns) {
				if (!column.notify) continue;
				for (const auto& card : column.cards) now[column.id].insert(card.id);
			}
			if (seen.peek() && *seen.peek() != now) {
				const auto& before = *seen.peek();
				for (const auto& column : out.columns) {
					auto was = before.find(column.id);
					for (const auto& card : column.cards) {
						if (column.notify && (was == before.end() || !was->second.contains(card.id))) chime(card.title + ": " + column.title);
					}
				}
			}
			if (!seen.peek() || *seen.peek() != now) seen.set(now);
		}
		if (spec.move) out.error = p.error(spec.move->command);
		return out;
	}

	void chime(const std::string& message) {
		std::string path = std::getenv("PATH") ? std::getenv("PATH") : "";
		bool installed = false;
		for (std::size_t at = 0; at <= path.size();) {
			std::size_t end = path.find(':', at);
			std::string folder = path.substr(at, end == std::string::npos ? std::string::npos : end - at);
			if (!folder.empty() && ::access((folder + "/chime").c_str(), X_OK) == 0) installed = true;
			if (end == std::string::npos) break;
			at = end + 1;
		}
		if (!installed) {
			// The terminal's bell, where there's a terminal to ring.
			if (::isatty(STDOUT_FILENO)) (void)!::write(STDOUT_FILENO, "\a", 1);
			return;
		}
		std::string said = message;
		char program[] = "chime", flag[] = "--message";
		char* arguments[] = {program, flag, said.data(), nullptr};
		posix_spawn_file_actions_t quiet;
		posix_spawn_file_actions_init(&quiet);
		posix_spawn_file_actions_addopen(&quiet, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
		posix_spawn_file_actions_addopen(&quiet, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
		pid_t child = 0;
		if (::posix_spawnp(&child, "chime", &quiet, nullptr, arguments, environ) == 0) {
			// Reaped by a thread of its own, so it's never left a zombie.
			std::thread([child] { ::waitpid(child, nullptr, 0); }).detach();
		}
		posix_spawn_file_actions_destroy(&quiet);
	}

	void move_card(page_base& p, const board_spec& spec, const std::string& card, const std::string& to) {
		if (!spec.move) return;
		std::string key = "board:" + spec.list;
		auto moved = p.kept<std::map<std::string, std::string>>(key + ":moved");
		p.kept<std::string>(key + ":moving").set("");
		moved.change([&](auto& now) { now[card] = to; });
		p.run(spec.move->command, value(std::map<std::string, value>{{"id", card}, {spec.by, to}}), [moved, card](const answer& a) {
			if (!a) moved.change([&](auto& now) { now.erase(card); });
		}, true);
	}

	std::vector<card_model> cards_of(page_base& p, const cards_spec& spec) {
		std::vector<card_model> out;
		std::string title = spec.columns.empty() ? "" : spec.columns.front().field;
		for (const auto& row : rows_of(spec.view.get(), spec.list)) {
			card_model card;
			card.id = row["id"].as_string();
			card.address = spec.link.empty() ? "" : row_link(spec.link, row, p.params(), spec.keyed, spec.named);
			card.title = show(field_of(row, title));
			for (std::size_t c = 1; c < spec.columns.size(); ++c) {
				if (std::string said_now = show(field_of(row, spec.columns[c].field)); !said_now.empty()) card.details.push_back(spec.columns[c].label + ": " + said_now);
			}
			// What it holds, counted by what each is at, in order: 3 To do · 1 Done.
			if (spec.tally) {
				struct group {
					std::string label;
					int count = 0;
					double order = 0;
				};
				std::map<std::string, group> groups;
				int total = 0;
				for (const auto& held : rows_of(spec.tally->view.get(), spec.tally->list)) {
					if (field_of(held, spec.tally->by).as_string() != card.id) continue;
					++total;
					std::string at = show(field_of(held, spec.tally->and_by));
					auto& g = groups[at];
					if (g.label.empty()) g.label = show(field_of(held, spec.tally->shown.empty() ? spec.tally->and_by : spec.tally->shown));
					if (g.label.empty()) g.label = "None";
					if (!spec.tally->order.empty()) g.order = field_of(held, spec.tally->order).as_double();
					++g.count;
				}
				std::vector<group> in_order;
				for (auto& [_, g] : groups) in_order.push_back(g);
				std::ranges::stable_sort(in_order, {}, &group::order);
				for (const auto& g : in_order) card.tally += (card.tally.empty() ? "" : " · ") + std::format("{} {}", g.count, g.label);
				if (total == 0) card.tally = "No " + (spec.tally->noun.empty() ? std::string("items") : spec.tally->noun) + " yet";
			}
			if (!card.address.empty()) {
				for (const auto& f : spec.filters) {
					std::string address = card.address + "?is=" + f.label;
					for (const auto& [name, wanted] : f.query) address += "&" + name + "=" + wanted;
					card.filters.push_back({f.label, address, f.color});
				}
			}
			out.push_back(std::move(card));
		}
		return out;
	}

	std::vector<found> find_in(page_base& p, const std::string& query, const std::vector<find_source>& sources) {
		std::vector<found> out;
		std::string wanted = lower(trimmed(query));
		if (wanted.empty()) return out;
		// #12, or 12, finds by number too.
		std::string number = wanted.starts_with('#') ? wanted.substr(1) : wanted;
		bool numeric = !number.empty() && std::ranges::all_of(number, [](char c) { return c >= '0' && c <= '9'; });
		for (const auto& source : sources) {
			std::string with_number, named;
			for (const auto& f : source.by) {
				if (with_number.empty() && numbered(f)) with_number = f;
				else if (named.empty()) named = f;
			}
			std::vector<const value*> hits;
			for (const auto& row : rows_of(source.view.get(), source.list)) {
				bool by_number = numeric && !with_number.empty() && show(field_of(row, with_number)) == number;
				bool by_words = std::ranges::any_of(source.by, [&](const std::string& f) { return f != with_number && lower(show(field_of(row, f))).find(wanted) != std::string::npos; });
				if (by_number || by_words || (!numeric && !with_number.empty() && lower(show(field_of(row, with_number))).find(wanted) != std::string::npos)) hits.push_back(&row);
			}
			if (hits.empty()) continue;
			found group{source.label, {}, hits.size() > 8 ? hits.size() - 8 : 0};
			for (std::size_t i = 0; i < hits.size() && i < 8; ++i) {
				const value& row = *hits[i];
				std::string says = (with_number.empty() ? "" : "#" + show(field_of(row, with_number)) + " ") + (named.empty() ? "" : show(field_of(row, named)));
				if (trimmed(says).empty() || says == "#") says = row["id"].as_string();
				group.results.emplace_back(says, source.link.empty() ? "" : row_link(source.link, row, p.params(), source.keyed));
			}
			out.push_back(std::move(group));
		}
		return out;
	}

	std::string between_model::name(const between_spec& spec, const value& thing) const {
		std::string says = show(field_of(thing, spec.shown));
		return says.empty() ? thing["id"].as_string() : says;
	}

	const value* between_model::between(const between_spec& spec, const value& a, const value& b) const {
		for (const auto& e : entries) {
			if (field_of(e, spec.from).as_string() == a["id"].as_string() && field_of(e, spec.to).as_string() == b["id"].as_string()) return &e;
		}
		return nullptr;
	}

	std::string between_model::says(const between_spec& spec, const value& entry) const {
		std::string out = spec.cell.empty() ? "" : show(field_of(entry, spec.cell));
		return out.empty() ? "✓" : out;
	}

	bool between_model::may(const between_spec& spec, bool entry) const {
		auto allowed = [](const std::optional<between_command>& c) { return c && c->allowed; };
		return entry ? allowed(spec.update) || allowed(spec.remove) : allowed(spec.create);
	}

	between_model between_of(const between_spec& spec) { return {rows_of(spec.over.get(), spec.over_list), rows_of(spec.view.get(), spec.list)}; }

	std::vector<thread_entry> thread_of(const view_state& v, const std::string& list) {
		std::vector<thread_entry> out;
		for (const auto& row : rows_of(v, list)) out.push_back({row["id"].as_string(), show(row["author.name"]), when(row["created_at"]), row["body"].as_string()});
		return out;
	}

	timeline_model timeline_of(page_base& p, const timeline_spec& spec) {
		timeline_model out;
		const view_state& v = *spec.view;
		auto rows = v.status == status::ready ? once_each(rows_of(v, spec.list)) : std::vector<const value*>{};
		// The views it judges new by, as they were when the page opened.
		auto looked = p.kept<std::optional<std::vector<value>>>("timeline:" + spec.list + ":looked");
		bool ready = std::ranges::all_of(spec.fresh_views, [](const state<view_state>& f) { return f->status != status::loading; });
		if (spec.fresh && ready && !looked.peek()) {
			std::vector<value> read;
			for (const auto& f : spec.fresh_views) read.push_back(f.peek().data);
			looked.set(read);
		}
		auto is_new = [&](const value& row) { return spec.fresh && looked.peek() && spec.fresh(row, *looked.peek()); };
		std::size_t fresh = std::ranges::count_if(rows, [&](const value* row) { return is_new(*row); });
		auto told = p.kept<bool>("timeline:" + spec.list + ":said", false);
		if (!spec.on_open.empty() && fresh > 0 && !told.peek()) {
			told.set(true);
			p.run(spec.on_open, value::map());
		}
		for (const value* row : rows) {
			timeline_entry e;
			e.id = (*row)["id"].as_string();
			e.who = show((*row)["created_by.name"]);
			if (e.who.empty()) e.who = "System";  // a change no one made, like a migration's
			e.when = when((*row)["created_at"]);
			e.fresh = is_new(*row);
			if (spec.subject.empty()) {
				e.what = changed((*row)["field"], (*row)["before"], (*row)["after"], (*row)["action"]);
			} else {
				auto [head, tail] = phrase((*row)["field"], (*row)["before"], (*row)["after"], (*row)["action"]);
				e.what = head;
				e.after = tail;
				for (const auto& column : spec.subject) {
					std::string part_of = numbered(column) ? "#" + show(field_of(*row, column)) : show(field_of(*row, column));
					if (!part_of.empty() && part_of != "#") e.subject += (e.subject.empty() ? "" : " ") + part_of;
				}
				if (!spec.link.empty()) e.address = row_link(spec.link, *row, p.params(), spec.keyed, spec.named);
			}
			out.entries.push_back(std::move(e));
		}
		if (!spec.title.empty()) out.title = fresh > 0 ? std::format("{} ({} new)", spec.title, fresh) : spec.title;
		return out;
	}

	std::string file_names(const value& held) {
		std::vector<value> each = held.is_array() ? held.as_array() : std::vector<value>{held};
		std::string out;
		for (const auto& path : each) {
			if (!path.is_string() || path.as_string().empty()) continue;
			std::string name = path.as_string().substr(path.as_string().find_last_of('/') + 1);
			out += (out.empty() ? "" : ", ") + name;
		}
		return out;
	}

	std::vector<detail_line> details_of(const view_state& v, const std::vector<column>& fields, const std::map<std::string, std::vector<option>>& choices,
	                                    const std::optional<tint_spec>& tint) {
		std::vector<detail_line> out;
		for (const auto& f : fields) {
			// What holds nothing isn't said, as the web leaves it out.
			std::string says = cell_text(choices, v.data, f.field);
			if (says.empty()) continue;
			out.push_back({f.field, f.label, says, tint && tint->field == f.field ? tone_of(v.data, tint) : 3});
		}
		return out;
	}

	namespace tried_themes {

		namespace {
			std::function<void(const std::optional<palette::theme>&)>& applying() {
				static std::function<void(const std::optional<palette::theme>&)> apply;
				return apply;
			}
			void tried(std::optional<palette::theme> made) {
				theme().set(made);
				if (applying()) applying()(made);
			}
		}  // namespace

		state<std::optional<palette::theme>> theme() {
			static state<std::optional<palette::theme>> kept;
			return kept;
		}
		state<std::string> scheme() {
			static state<std::string> kept = [] {
				std::random_device device;
				return state<std::string>(std::string(palette::schemes[device() % palette::schemes.size()]));
			}();
			return kept;
		}
		void make_new() {
			std::random_device device;
			tried(palette::generate(std::uniform_int_distribution<std::uint32_t>(1, 99999)(device), scheme().peek()));
		}
		void pick(const std::string& chosen) {
			scheme().set(chosen);
			if (auto now = theme().peek()) tried(palette::generate(now->seed, chosen));
			else make_new();
		}
		void put_back() { tried(std::nullopt); }
		void on_tried(std::function<void(const std::optional<palette::theme>&)> apply) { applying() = std::move(apply); }

	}  // namespace tried_themes

}  // namespace one::screens
