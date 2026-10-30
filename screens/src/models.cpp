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
#include <cctype>
#include <array>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <format>
#include <random>
#include <set>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <tuple>

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
			// Any other time, with its time of day where it's read: 2026-10-28 09:00.
			if (v.is_time()) return std::format("{:%Y-%m-%d %H:%M}", std::chrono::floor<std::chrono::minutes>(std::chrono::zoned_time(time_zone(), v.as_time()).get_local_time()));
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
			// A day alone at midnight UTC, so it's that day wherever it's read, like
			// 2026-10-28; a day and a time of day, that time where it's read, like
			// 2026-10-28 9:00.
			if (type == "date") {
				using namespace std::chrono;
				std::istringstream in(written);
				year_month_day day{};
				in >> parse("%F", day);
				if (in.fail() || !day.ok()) return written.empty() ? value() : value(written);
				int hour = 0, minute = 0;
				char colon = 0;
				in >> std::ws;
				if (in.peek() == 'T') in.get();
				bool timed = static_cast<bool>(in >> hour >> colon >> minute) && colon == ':' && hour >= 0 && hour < 24 && minute >= 0 && minute < 60;
				sys_seconds at = timed ? floor<seconds>(time_zone()->to_sys(local_days(day) + hours(hour) + minutes(minute), choose::earliest)) : sys_days(day);
				value out;
				out.type = value::kind::time;
				out.time.seconds = at.time_since_epoch().count();
				return out;
			}
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

	value worked(char op, const value& a, const value& b) {
		bool numbers = (a.is_integer() || a.is_double()) && (b.is_integer() || b.is_double());
		if (!numbers) return value();
		double x = a.as_double(), y = b.as_double();
		switch (op) {
			case '+': return value(x + y);
			case '-': return value(x - y);
			case '*': return value(x * y);
			// As a clock counts, never below none: -1 % 4 is 3. Either by 0 is 0, on every
			// target alike.
			case '%': return value(y == 0 ? 0.0 : x - y * std::floor(x / y));
			default: return value(y == 0 ? 0.0 : x / y);
		}
	}

	value picked(page_base& p, const std::string& name, const state<view_state>& v, const std::string& list) {
		std::string id = p.picked_id(name);
		if (id.empty() || !ready(v)) return value();
		for (const auto& row : at(v, list).as_array()) {
			if (row["id"].as_string() == id) return row;
		}
		return value();
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

	bool leaves(const std::string& to) { return to.starts_with("https://") || to.starts_with("http://") || to.starts_with("mailto:") || to.starts_with("tel:"); }

	std::string address_of(page_base& p, const std::string& to) { return leaves(to) || to.find(':') == std::string::npos ? to : filled(to, p.params()); }

	// Commands

	std::optional<command_model> command_of(page_base& p, const command_button& b) {
		if (!b.allowed || !b.when) return std::nullopt;
		value fields = b.fields.is_map() ? b.fields : value::map();
		if (!b.id.empty()) fields.fields["id"] = value(b.id);
		bool busy = p.running(b.command);
		return command_model{busy ? b.label + "…" : b.label, busy, std::move(fields)};
	}

	value last_given(page_base& p, const std::string& command) {
		value kept = p.kept<value>("given " + command).get();
		return kept.is_map() ? kept : value::map();
	}

	void remember_given(page_base& p, const std::string& command, const value& given) {
		auto kept = p.kept<value>("given " + command);
		value now = kept.peek().is_map() ? kept.peek() : value::map();
		for (const auto& [name, v] : given.as_map()) {
			if (name != "id" && !name.starts_with("one:")) now.fields[name] = v;
		}
		kept.set(std::move(now));
	}

	std::vector<std::string> styled(page_base& p, const std::string& pick) { return p.kept<std::vector<std::string>>("styled " + pick).get(); }

	void style_these(page_base& p, const std::string& pick, std::vector<std::string> ids) {
		auto kept = p.kept<std::vector<std::string>>("styled " + pick);
		if (kept.peek() != ids) kept.set(std::move(ids));
	}

	bool holds_last_given(page_base& p, const command_button& b) {
		value kept = last_given(p, b.command);
		if (!b.fields.is_map() || b.fields.as_map().empty()) return false;
		for (const auto& [name, v] : b.fields.as_map()) {
			auto now = kept.fields.find(name);
			if (now == kept.fields.end() || !same(now->second, v)) return false;
		}
		return true;
	}

	bool opening_now(page_base& p, bool ready, const std::vector<opening>& lines) {
		auto opened = p.kept<bool>("opened");
		if (opened.peek()) return false;
		if (!ready) return true;
		opened.set(true);
		for (const auto& line : lines) {
			if (!line.when) continue;
			if (line.run) {
				auto shown = command_of(p, *line.run);
				if (!shown) continue;
				std::string then = line.run->then_open;
				// What a screen does as it's opened isn't the person's to undo (decision 0020).
				value fields = shown->fields;
				fields.fields["one:history"] = value("apart");
				p.run(line.run->command, fields, [&p, then](const answer& made) {
					if (made && !then.empty()) p.open_instead(then.substr(0, then.rfind(':')) + *made);
				});
				return false;
			}
			for (const auto& row : rows_of(*line.view, line.list)) {
				if (line.where && !line.where(row)) continue;
				p.open_instead(line.open.substr(0, line.open.rfind(':')) + row["id"].as_string());
				return false;
			}
		}
		return false;
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
		std::string after = spec_.after, then = spec_.then_open;
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
			// What it made, opened, like a board made from New board.
			if (!then.empty()) here->open(then.substr(0, then.rfind(':')) + *a);
		}, true);
	}

	// Lists

	void status_of(page_base& p, const value& card, const std::string& column_status, board_card& shown);

	std::optional<track_model> track_of(page_base& p, const std::string& key, const std::optional<track_spec>& spec, const std::vector<value>& rows, const std::string& noun) {
		if (!spec) return std::nullopt;
		// A switch's track is one pick for every way it's shown.
		auto picking = p.kept<std::string>((spec->key.empty() ? key : spec->key) + ":track");
		const auto& records = rows_of(spec->view.get(), spec->list);
		if (records.empty()) return std::nullopt;
		track_model out{label(spec->field), {}, picking.get(), picking, spec->above, spec->picked_last};
		// Every row, once the last's been put down on purpose; the last, until any is picked.
		bool every = out.picked == track_model::every;
		if (every) out.picked.clear();
		// Each stage's status, for each row's dot, as a board's card has it.
		std::map<std::string, std::string> stage_status = spec->statuses;
		bool staged = !spec->by.empty() && spec->over && (*spec->over)->status == status::ready;
		if (staged && !spec->status_by.empty()) {
			for (const auto& stage : rows_of(**spec->over, spec->over_list)) {
				if (std::string said = show(field_of(stage, spec->status_by)); !said.empty()) stage_status[stage["id"].as_string()] = said;
			}
		}
		for (const auto& record : records) {
			std::string id = record["id"].as_string();
			auto count = std::ranges::count_if(rows, [&](const value& row) { return field_of(row, spec->field).as_string() == id; });
			// Without rows of its own, it's left out when the track says so, unless it's picked.
			if (spec->hide_empty && count == 0 && out.picked != id) continue;
			std::string title = show(field_of(record, spec->shown));
			track_record r{id, title.empty() ? id : title, {}, count, out.picked == id};
			for (const auto& c : spec->columns) {
				if (std::string said_now = show(field_of(record, c.field)); !said_now.empty()) r.details.push_back(c.label + " " + said_now);
			}
			// One issue, two issues.
			r.details.push_back(std::format("{} {}", count, count == 1 && noun.ends_with('s') ? noun.substr(0, noun.size() - 1) : noun));
			// Failed once any row in it has, and succeeded once every row has.
			if (staged && count > 0) {
				bool all = true, any_failed = false;
				for (const auto& row : rows) {
					if (field_of(row, spec->field).as_string() != id) continue;
					board_card dot;
					std::string at = field_of(row, spec->by).as_string();
					status_of(p, row, stage_status.contains(at) ? stage_status.at(at) : "", dot);
					all = all && dot.status == "success";
					any_failed = any_failed || dot.status == "failed";
				}
				r.status = any_failed ? "failed" : all ? "success" : "";
			}
			out.records.push_back(std::move(r));
		}
		if (spec->picked_last && out.picked.empty() && !every && !out.records.empty()) {
			out.picked = out.records.back().id;
			out.records.back().picked = true;
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
			return x && y && (spec->descending ? *x > *y : *x < *y);
		});
		return rows;
	}

	bool move_row(page_base& p, const std::string& key, const reorder_spec& spec, const std::vector<value>& around, const std::string& id, int by) {
		if (!spec.allowed) return false;
		// Shown highest first, moved as the same rows lowest first, the other way.
		if (spec.descending) {
			reorder_spec rising = spec;
			rising.descending = false;
			return move_row(p, key, rising, std::vector<value>(around.rbegin(), around.rend()), id, -by);
		}
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
		out.kept = std::move(rows);
		return out;
	}

	std::string csv_of(const std::vector<column>& columns, const std::map<std::string, std::vector<option>>& choices, const std::vector<value>& rows) {
		auto quoted = [](const std::string& text) {
			if (text.find_first_of("\",\r\n") == std::string::npos) return text;
			std::string out = "\"";
			for (char c : text) out += c == '"' ? std::string("\"\"") : std::string(1, c);
			return out + "\"";
		};
		std::string out;
		for (std::size_t i = 0; i < columns.size(); ++i) out += (i ? "," : "") + quoted(columns[i].label);
		out += "\r\n";
		for (const auto& row : rows) {
			for (std::size_t i = 0; i < columns.size(); ++i) {
				const value& v = field_of(row, columns[i].field);
				std::string said;
				if (v.is_array()) {
					for (const auto& item : v.items) said += (said.empty() ? "" : ", ") + show(item);
				} else if (v.is_time()) {
					// The full time, in UTC, as the web saves it.
					said = std::format("{:%FT%T}Z", std::chrono::floor<std::chrono::milliseconds>(v.as_time()));
				} else {
					said = cell_text(choices, row, columns[i].field);
				}
				out += (i ? "," : "") + quoted(said);
			}
			out += "\r\n";
		}
		return out;
	}

	std::expected<std::string, std::string> save_download(const std::string& name, const std::string& text) {
		namespace fs = std::filesystem;
		std::string file = fs::path(name).filename().string();
		if (file.empty() || file == "." || file == "..") return std::unexpected("there's no file name to save it as");
		fs::path folder;
		if (const char* named = std::getenv("XDG_DOWNLOAD_DIR"); named && *named) folder = named;
		else if (const char* home = std::getenv("HOME"); home && *home) folder = fs::exists(fs::path(home) / "Downloads") ? fs::path(home) / "Downloads" : fs::path(home);
		else folder = fs::current_path();
		fs::path at = folder / file;
		std::string stem = fs::path(file).stem().string(), extension = fs::path(file).extension().string();
		for (int n = 2; fs::exists(at); ++n) at = folder / (stem + "-" + std::to_string(n) + extension);
		std::ofstream out(at, std::ios::binary);
		out << text;
		if (!out) return std::unexpected("it couldn't be saved in " + folder.string());
		return at.string();
	}

	namespace {
		// The day a calendar is on: the one it's been gone to, or today, where it's read.
		std::chrono::local_days calendar_day_of(page_base& p, const calendar_spec& spec) {
			using namespace std::chrono;
			std::string kept = p.kept<std::string>("calendar " + spec.key).get();
			local_days today = floor<days>(time_zone()->to_local(system_clock::now()));
			if (kept.empty()) return today;
			std::istringstream in(kept);
			year_month_day day{};
			in >> parse("%F", day);
			return in.fail() ? today : local_days(day);
		}
	}  // namespace

	calendar_model calendar_of(page_base& p, const calendar_spec& spec) {
		using namespace std::chrono;
		calendar_model out;
		out.month = spec.month;
		const view_state& v = spec.view.get();
		if (v.status == status::loading) out.loading = true;
		if (v.status == status::failed) out.error = v.error.empty() ? "This couldn't be read." : v.error;
		local_days on = calendar_day_of(p, spec);
		local_days today = floor<days>(time_zone()->to_local(system_clock::now()));
		year_month_day shown(on);
		// From Monday: the week it's on, or the weeks a month's days fall in.
		local_days first = on, final;
		if (spec.month) {
			local_days start = local_days(shown.year() / shown.month() / 1);
			local_days end = local_days(year_month_day_last(shown.year(), month_day_last(shown.month())));
			first = start - days((weekday(start).c_encoding() + 6) % 7);
			final = end + days(6 - (weekday(end).c_encoding() + 6) % 7);
			out.title = std::format("{:%B %Y}", start);
		} else {
			first = on - days((weekday(on).c_encoding() + 6) % 7);
			final = first + days(6);
			std::string day = std::format("{:%b %e, %Y}", first);
			if (auto doubled = day.find("  "); doubled != std::string::npos) day.erase(doubled, 1);
			out.title = "Week of " + day;
		}
		std::string picked = spec.pick.empty() ? "" : p.picked_id(spec.pick);
		std::string title = spec.columns.empty() ? "title" : spec.columns.front().field;
		// Each row on its day, as it's read here: a day alone that day wherever it's read.
		struct placed {
			local_days day;
			std::optional<local_time<minutes>> at;
			const value* row;
		};
		std::vector<placed> rows;
		for (const auto& row : rows_of(v, spec.list)) {
			const value& when = field_of(row, spec.by);
			if (!when.is_time()) continue;
			auto at = time_point_cast<system_clock::duration>(when.as_time());
			bool alone = floor<days>(at) == at;
			local_time<minutes> here = floor<minutes>(alone ? local_days(floor<days>(at).time_since_epoch()) : time_zone()->to_local(at));
			rows.push_back({floor<days>(here), alone ? std::nullopt : std::optional(here), &row});
		}
		std::stable_sort(rows.begin(), rows.end(), [](const placed& a, const placed& b) {
			if (a.at.has_value() != b.at.has_value()) return !a.at.has_value();
			return a.at && b.at && *a.at < *b.at;
		});
		for (local_days d = first; d <= final; d += days(1)) {
			year_month_day ymd(d);
			calendar_day day{std::format("{:%F}", d), std::format("{:%a}", weekday(d)), static_cast<int>(static_cast<unsigned>(ymd.day())), d == today,
			                 !spec.month || ymd.month() == shown.month(), {}};
			for (const auto& r : rows) {
				if (r.day != d) continue;
				const value& row = *r.row;
				calendar_card card{row["id"].as_string(), show(field_of(row, title)), {}, r.at ? std::format("{:%H:%M}", *r.at) : "", tone_of(row, spec.tint),
				                   row["id"].as_string() == picked};
				for (std::size_t i = 1; i < spec.columns.size(); ++i) {
					if (std::string said = show(field_of(row, spec.columns[i].field)); !said.empty()) card.details.push_back(said);
				}
				day.cards.push_back(std::move(card));
			}
			out.days.push_back(std::move(day));
		}
		return out;
	}

	void calendar_go(page_base& p, const calendar_spec& spec, int by) {
		using namespace std::chrono;
		auto kept = p.kept<std::string>("calendar " + spec.key);
		if (by == 0) return kept.set("");
		local_days on = calendar_day_of(p, spec);
		if (spec.month) {
			year_month_day day(on);
			on = local_days(year_month_day(day.year() / day.month() / 1) + months(by));
		} else {
			on += days(7 * by);
		}
		kept.set(std::format("{:%F}", on));
	}

	void calendar_move(page_base& p, const calendar_spec& spec, const std::string& row, const std::string& date) {
		using namespace std::chrono;
		if (spec.move.empty()) return;
		std::istringstream in(date);
		year_month_day day{};
		in >> parse("%F", day);
		if (in.fail()) return;
		value when;
		for (const auto& r : rows_of(spec.view.get(), spec.list)) {
			if (r["id"].as_string() == row) when = field_of(r, spec.by);
		}
		// A day alone stays one; a time of day stays the same, on the day it's moved to.
		sys_seconds to = sys_days(local_days(day).time_since_epoch());
		if (when.is_time()) {
			auto at = time_point_cast<system_clock::duration>(when.as_time());
			if (floor<days>(at) != at) {
				auto here = time_zone()->to_local(at);
				to = floor<seconds>(time_zone()->to_sys(local_days(day) + (floor<seconds>(here) - floor<days>(here)), choose::earliest));
			}
		}
		value moved;
		moved.type = value::kind::time;
		moved.time.seconds = to.time_since_epoch().count();
		p.run(spec.move, value(std::map<std::string, value>{{"id", row}, {spec.by, moved}}));
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
		// The track's dots are worked out from the board's own stages, unless it's told others.
		auto track = spec.track;
		if (track && track->by.empty()) {
			track->by = spec.by, track->over = spec.over, track->over_list = spec.over_list;
			track->status_by = spec.status_by, track->statuses = spec.statuses;
		}
		out.track = track_of(p, out.key, track, all, spec.list);
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
			if (spec.move->anywhere) {
				for (const auto& thing : rows_of(over, spec.over_list)) {
					if (thing["id"].as_string() != at) to.push_back(thing["id"].as_string());
				}
				return to;
			}
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
		// Columns kept as records say for themselves whether they chime and their status.
		board_spec own = spec;
		for (const auto& thing : things) {
			std::string id = thing["id"].as_string();
			if (!spec.notify_by.empty() && std::ranges::any_of(field_of(thing, spec.notify_by).is_array() ? field_of(thing, spec.notify_by).as_array() : std::vector<value>{},
			                                                    [&](const value& w) { return w.is_string() && w.as_string() == spec.notify_has; }))
				own.notify.push_back(id);
			if (!spec.status_by.empty()) {
				if (std::string said = show(field_of(thing, spec.status_by)); !status_words(said).empty()) own.statuses[id] = said;
			}
		}
		const board_spec& with = own;
		for (const auto& thing : things) {
			board_column column{thing["id"].as_string(), title_of(thing["id"].as_string()), {}, {}};
			for (const auto& card : all) {
				if (where(card) != column.id || !keeps(card)) continue;
				column.rows.push_back(card);
				board_card shown{card["id"].as_string(), show(field_of(card, title)), {}, {}, {}, tone_of(card, spec.tint), "", "", "", ""};
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
				status_of(p, card, with.statuses.contains(column.id) ? with.statuses.at(column.id) : "", shown);
				// What it waits on that hasn't succeeded keeps it waiting, saying on what.
				if (!spec.waits_on.empty()) {
					std::vector<std::string> undone;
					const value& listed = field_of(card, spec.waits_on);
					for (const auto& id : listed.is_array() ? listed.as_array() : std::vector<value>{}) {
						for (const auto& other : all) {
							if (other["id"].as_string() != id.as_string()) continue;
							std::string at = where(other);
							std::string status = with.statuses.contains(at) ? with.statuses.at(at) : "";
							if (status != "success" && status != "done") undone.push_back(show(field_of(other, title)));
						}
					}
					if (!undone.empty()) {
						if (shown.status.empty() || shown.status == "inactive") shown.status = "waiting";
						std::string names;
						for (const auto& n : undone) names += (names.empty() ? "" : ", ") + n;
						shown.details.push_back("waiting on " + names);
					}
				}
				shown.reaches = reaches(column.id);
				column.cards.push_back(std::move(shown));
			}
			column.notify = std::ranges::contains(with.notify, column.id);
			out.columns.push_back(std::move(column));
		}
		// A card that arrives in a column that notifies chimes, once; those there when
		// the board's first drawn don't. Every card counts, not only those a search or
		// a pick on the track shows, so showing more of them chimes nothing.
		if (!with.notify.empty()) {
			auto seen = p.kept<std::optional<std::map<std::string, std::set<std::string>>>>(out.key + ":seen");
			std::map<std::string, const board_column*> notifying;
			for (const auto& column : out.columns) {
				if (column.notify) notifying[column.id] = &column;
			}
			std::map<std::string, std::set<std::string>> now;
			std::map<std::string, std::string> title_of;
			std::string title = spec.columns.empty() ? spec.shown : spec.columns.front().field;
			for (const auto& card : all) {
				std::string at = where(card);
				if (!notifying.contains(at)) continue;
				now[at].insert(card["id"].as_string());
				title_of[card["id"].as_string()] = show(field_of(card, title));
			}
			if (seen.peek() && *seen.peek() != now) {
				const auto& before = *seen.peek();
				for (const auto& [at, cards] : now) {
					auto was = before.find(at);
					// A card arriving where things fail chimes the error sound, not the one for done.
					bool failing = with.statuses.contains(at) && with.statuses.at(at) == "failed";
					for (const auto& id : cards) {
						if (was == before.end() || !was->second.contains(id)) chime(title_of[id] + ": " + notifying[at]->title, failing);
					}
				}
			}
			if (!seen.peek() || *seen.peek() != now) seen.set(now);
		}
		if (spec.move) out.error = p.error(spec.move->command);
		return out;
	}

	namespace {
		std::function<void(const std::string&, bool)>& heard_instead() {
			static std::function<void(const std::string&, bool)> heard;
			return heard;
		}
	}  // namespace

	void chime_into(std::function<void(const std::string& message, bool failed)> heard) { heard_instead() = std::move(heard); }

	void chime(const std::string& message, bool failed) {
		if (heard_instead()) {
			heard_instead()(message, failed);
			return;
		}
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
			// The terminal's bell, where there's a terminal to ring: twice for a failure.
			if (::isatty(STDOUT_FILENO)) (void)!::write(STDOUT_FILENO, failed ? "\a\a" : "\a", failed ? 2 : 1);
			return;
		}
		std::string said = message;
		char program[] = "chime", flag[] = "--message", error[] = "--error";
		char* arguments[] = {program, flag, said.data(), failed ? error : nullptr, nullptr};
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

	std::string status_words(const std::string& status) {
		static const std::map<std::string, std::string> words{
		    {"inactive", "Not started"}, {"waiting", "Waiting"}, {"working", "Working"}, {"success", "Succeeded"}, {"done", "Succeeded"}, {"failed", "Failed"}};
		auto found = words.find(status);
		return found == words.end() ? "" : found->second;
	}

	std::string running_time(std::chrono::seconds took) {
		auto s = std::max<long long>(0, took.count());
		if (s < 60) return std::format("{}s", s);
		if (s < 3600) return std::format("{}m", s / 60);
		if (s < 86400) return s % 3600 / 60 ? std::format("{}h {}m", s / 3600, s % 3600 / 60) : std::format("{}h", s / 3600);
		return s % 86400 / 3600 ? std::format("{}d {}h", s / 86400, s % 86400 / 3600) : std::format("{}d", s / 86400);
	}

	value value_at(const value& record, const std::string& path) {
		if (!record.is_map()) return value();
		if (const value* whole = record.find(path)) return *whole;
		value at = record;
		std::size_t from = 0;
		while (from <= path.size()) {
			auto dot = path.find('.', from);
			std::string part = path.substr(from, dot == std::string::npos ? std::string::npos : dot - from);
			const value* next = at.is_map() ? at.find(part) : nullptr;
			if (!next) return value();
			value inner = *next;  // copied out first: next points into at
			at = std::move(inner);
			if (dot == std::string::npos) break;
			from = dot + 1;
		}
		return at;
	}

	std::string read_value(double v, const std::string& unit) {
		if (unit == "percent") return std::format("{}%", static_cast<long long>(std::llround(v)));
		if (unit == "bytes" || unit == "rate") {
			static const char* names[] = {"B", "KB", "MB", "GB", "TB", "PB"};
			std::size_t at = 0;
			double n = std::max(0.0, v);
			while (n >= 1024 && at + 1 < std::size(names)) n /= 1024, ++at;
			std::string shown = at == 0 || n >= 100 ? std::to_string(std::llround(n)) : std::format("{:.1f}", n);
			if (shown.ends_with(".0")) shown.resize(shown.size() - 2);
			return shown + " " + names[at] + (unit == "rate" ? "/s" : "");
		}
		if (v == std::floor(v)) return std::to_string(static_cast<long long>(v));
		return std::format("{:.1f}", v);
	}

	double scale_top(const std::vector<double>& values, const std::string& unit) {
		if (unit == "percent") return 100;
		double highest = 0;
		for (double v : values) highest = std::max(highest, v);
		if (highest <= 0) return 1;
		bool binary = unit == "bytes" || unit == "rate";
		double base = binary ? 1024 : 10, power = 1;
		while (power * base <= highest) power *= base;
		static const double binary_steps[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
		static const double decimal_steps[] = {1, 2, 5, 10};
		if (binary) {
			for (double step : binary_steps) if (power * step >= highest) return power * step;
		} else {
			for (double step : decimal_steps) if (power * step >= highest) return power * step;
		}
		return power * base;
	}

	std::string tone_of_state(const std::string& word) {
		std::string w;
		for (char c : word) w += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (w == "working" || w == "busy" || w == "running") return "working";
		if (w == "standby" || w == "waiting" || w == "paused") return "waiting";
		if (w == "idle" || w == "ready" || w == "success" || w == "done" || w == "live" || w == "online") return "success";
		if (w == "stopped" || w == "failed" || w == "error" || w == "crashed") return "failed";
		return "inactive";
	}

	chart_model chart_of(const std::string& title, const value& numbers, const std::string& unit) {
		chart_model out{.title = title, .unit = unit};
		if (numbers.is_array()) {
			for (const auto& n : numbers.as_array()) {
				if (n.is_integer() || n.is_double()) out.values.push_back(n.as_double());
			}
		} else if (numbers.is_integer() || numbers.is_double()) {
			out.values.push_back(numbers.as_double());
		}
		out.top = scale_top(out.values, unit);
		out.latest = out.values.empty() ? "–" : read_value(out.values.back(), unit);
		out.top_said = read_value(out.top, unit);
		return out;
	}

	std::vector<chart_model> charts_of(page_base&, const chart_spec& spec) {
		const view_state& v = *spec.view;
		std::vector<chart_model> out;
		for (const auto& line : spec.series) out.push_back(chart_of(line.title, v.status == status::ready ? value_at(v.data, line.path) : value(), line.unit));
		return out;
	}

	monitor_model monitor_of(page_base& p, const monitor_spec& spec) {
		monitor_model out;
		const view_state& v = *spec.view;
		out.loading = v.status == status::loading;
		auto now = p.now();
		auto time_of = [](const value& t) -> std::optional<std::chrono::sys_seconds> {
			if (t.is_time()) return std::chrono::floor<std::chrono::seconds>(t.as_time());
			if (!t.is_string()) return std::nullopt;
			std::chrono::sys_seconds at;
			std::istringstream in(t.as_string());
			if (in >> std::chrono::parse("%FT%T", at)) return at;
			return std::nullopt;
		};
		for (const auto& row : rows_of(v, spec.list)) {
			monitor_tile tile;
			tile.id = row["id"].as_string();
			tile.title = show(value_at(row, spec.title));
			if (tile.title.empty()) tile.title = tile.id;
			tile.address = spec.link.empty() ? "" : row_link(spec.link, row, p.params(), spec.keyed, spec.named);
			std::string word = show(value_at(row, spec.status));
			bool gone = false;
			if (!spec.seen.empty() && spec.seen_seconds > 0) {
				auto heard = time_of(value_at(row, spec.seen));
				gone = !heard || now - *heard > std::chrono::seconds(spec.seen_seconds);
			}
			if (gone) {
				tile.state = "Offline", tile.tone = "inactive";
			} else {
				auto found = std::ranges::find_if(spec.choices, [&](const option& o) { return o.value == word; });
				tile.state = found != spec.choices.end() ? found->label : word;
				if (!tile.state.empty()) tile.state[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(tile.state[0])));
				if (tile.state.empty()) tile.state = "Unknown";
				tile.tone = tone_of_state(word);
				if (!spec.since.empty()) {
					if (auto began = time_of(value_at(row, spec.since))) tile.since = running_time(now - *began);
				}
				if (!spec.doing.empty()) tile.doing = show(value_at(row, spec.doing));
				if (!spec.step.empty()) {
					value at = value_at(row, spec.step), of = value_at(row, spec.steps);
					bool numbers = (at.is_integer() || at.is_double()) && (of.is_integer() || of.is_double());
					if (numbers && of.as_double() > 0) tile.progress = std::pair{std::clamp(at.as_double(), 0.0, of.as_double()), of.as_double()};
				}
			}
			for (const auto& c : spec.charts) tile.charts.push_back(chart_of(c.title, gone ? value() : value_at(row, c.path), c.unit));
			out.tiles.push_back(std::move(tile));
		}
		return out;
	}

	namespace {
		std::string switch_memory(page_base& p, const std::string& id) {
			std::string address = p.address();
			if (auto query = address.find('?'); query != std::string::npos) address.resize(query);
			return "switch " + id + " " + address;
		}
	}  // namespace

	state<int> switch_pick(page_base& p, const std::string& id, const std::vector<std::string>& options) {
		int remembered_at = -1;
		if (auto said = remembered::get(switch_memory(p, id))) {
			auto found = std::ranges::find(options, *said);
			if (found != options.end()) remembered_at = static_cast<int>(found - options.begin());
		}
		return p.kept<int>("switched:" + id, remembered_at);
	}

	void remember_switch(page_base& p, const std::string& id, const std::string& option) { remembered::set(switch_memory(p, id), option); }

	std::string switch_icon(const std::string& option) {
		return option == "Whiteboard" ? "whiteboard" : option == "Pipeline" ? "pipeline" : option == "Board" ? "board" : "table";
	}

	// A card's dot, running time and runner: what its holder reported in its data,
	// else working while someone's lease on it holds, else its column's status.
	void status_of(page_base& p, const value& card, const std::string& column_status, board_card& shown) {
		const value& data = card["data"];
		// done is said success now; what was stored before reads as it.
		auto named = [](std::string status) { return status == "done" ? std::string("success") : status; };
		std::string said = named(data.is_map() ? show(data["status"]) : "");
		std::string holder = show(card["held_by"]);
		const value& until = card["held_until"];
		bool held = !holder.empty() && until.is_time() && until.as_time() > std::chrono::system_clock::now();
		shown.status = !status_words(said).empty() ? said : held ? "working" : named(column_status);
		if (!data.is_map()) return;
		// How far it is in its stage, as its holder says: step 2 of 5. Only while they
		// hold it: let go or moved on, what they last said is about where it was.
		const value &step = data["step"], &steps = data["steps"];
		if (held && (step.is_integer() || step.is_double()) && (steps.is_integer() || steps.is_double()) && steps.as_double() > 0) {
			shown.progress = std::pair{std::clamp(step.as_double(), 0.0, steps.as_double()), steps.as_double()};
		}
		if (std::string runner = show(data["runner"]); !runner.empty()) shown.runner = "runner " + runner;
		std::string since = show(data["since"]);
		std::chrono::sys_seconds started;
		std::istringstream in(since);
		if (!since.empty() && (in >> std::chrono::parse("%FT%T", started))) shown.running = running_time(p.now() - started);
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

	pipeline_model pipeline_of(page_base& p, const pipeline_spec& spec) {
		pipeline_model out;
		const view_state& v = *spec.view;
		const view_state& over = *spec.over;
		if (v.status == status::loading || over.status == status::loading) {
			out.loading = true;
			return out;
		}
		auto words_of = [](const value& field) {
			std::vector<std::string> said;
			if (field.is_array()) {
				for (const auto& w : field.as_array()) said.push_back(show(w));
			} else if (std::string one = show(field); !one.empty()) {
				said.push_back(one);
			}
			return said;
		};
		struct stage_seen {
			std::string id, title, status;
			std::vector<std::string> kinds;
		};
		std::vector<stage_seen> stages;
		for (const auto& thing : rows_of(over, spec.over_list)) {
			std::string status = spec.status_by.empty() ? "" : show(field_of(thing, spec.status_by));
			if (status == "done") status = "success";  // as it was said before
			stages.push_back({thing["id"].as_string(), show(field_of(thing, spec.shown)), status,
			                  words_of(field_of(thing, spec.path))});
		}
		// The lanes, in the order their kinds first come along the stages.
		std::vector<std::string> kinds;
		for (const auto& stage : stages) {
			for (const auto& kind : stage.kinds) {
				if (!std::ranges::contains(kinds, kind)) kinds.push_back(kind);
			}
		}
		// A kind's path: its own stages, and those naming no kind but where cards fail.
		auto path_of = [&](const std::string& kind) {
			std::vector<const stage_seen*> path;
			for (const auto& stage : stages) {
				if (std::ranges::contains(stage.kinds, kind) || (stage.kinds.empty() && stage.status != "failed")) path.push_back(&stage);
			}
			return path;
		};
		std::map<std::string, pipeline_lane> lanes;
		std::string title = spec.columns.empty() ? "" : spec.columns.front().field;
		// In a switch, only what the track above it keeps.
		auto cards = rows_of(v, spec.list);
		if (auto kept_to = track_of(p, "pipeline:" + spec.list, spec.track, cards, spec.list)) std::erase_if(cards, [&](const value& row) { return !kept_to->keeps(row, spec.track->field); });
		for (const auto& card : cards) {
			if (!p.keeps(card)) continue;
			auto said = words_of(field_of(card, spec.lanes));
			auto kind = std::ranges::find_if(kinds, [&](const std::string& k) { return std::ranges::contains(said, k); });
			std::string lane = kind == kinds.end() ? "" : *kind;
			pipeline_row row{card["id"].as_string(), show(field_of(card, title)), "", {}, {}, "", "", "", ""};
			for (std::size_t c = 1; c < spec.columns.size(); ++c) {
				if (std::string said_now = show(field_of(card, spec.columns[c].field)); !said_now.empty()) row.details.push_back(numbered(spec.columns[c].field) ? "#" + said_now : said_now);
			}
			if (!spec.link.empty()) row.link = row_link(spec.link, card, p.params(), spec.keyed, spec.named);
			std::string at = field_of(card, spec.by).as_string();
			const stage_seen* here = nullptr;
			for (const auto& stage : stages) {
				if (stage.id == at) here = &stage;
			}
			board_card dot;
			status_of(p, card, here ? here->status : "", dot);
			row.status = dot.status, row.running = dot.running, row.runner = dot.runner, row.progress = dot.progress;
			const value& data = card["data"];
			if (data.is_map()) row.note = show(data["note"]);
			// What it waits on that hasn't succeeded keeps it waiting, saying on what.
			if (!spec.waits_on.empty()) {
				std::vector<std::string> undone;
				const value& listed = field_of(card, spec.waits_on);
				for (const auto& id : listed.is_array() ? listed.as_array() : std::vector<value>{}) {
					for (const auto& other : rows_of(v, spec.list)) {
						if (other["id"].as_string() != id.as_string()) continue;
						std::string at_other = field_of(other, spec.by).as_string();
						std::string status;
						for (const auto& stage : stages) {
							if (stage.id == at_other) status = stage.status;
						}
						if (status != "success") undone.push_back(show(field_of(other, title)));
					}
				}
				if (!undone.empty()) {
					if (row.status.empty() || row.status == "inactive") row.status = "waiting";
					std::string names;
					for (const auto& n : undone) names += (names.empty() ? "" : ", ") + n;
					row.note = "waiting on " + names;
				}
			}
			// Where it is along its path, or where it failed, as its data says.
			auto path = path_of(lane);
			std::string failed_at = here && here->status == "failed" && data.is_map() ? show(data["at"]) : "";
			std::size_t now = path.size();
			bool failed = here && here->status == "failed";
			for (std::size_t i = 0; i < path.size(); ++i) {
				if (path[i]->id == at || (failed && (path[i]->title == failed_at || path[i]->id == failed_at))) now = i;
			}
			for (std::size_t i = 0; i < path.size(); ++i) {
				std::string state = i < now ? "done" : i == now ? (failed ? "failed" : "now") : "later";
				// An end it's reached is done, not still going.
				if (i == now && !failed && path[i]->status == "success") state = "done";
				row.steps.push_back({path[i]->id, path[i]->title, state});
			}
			if (failed && now == path.size()) row.steps.push_back({here->id, here->title, "failed"});
			auto& shown = lanes[lane];
			if (shown.title.empty()) {
				shown.title = lane.empty() ? "Others" : lane;
				shown.title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(shown.title[0])));
			}
			shown.rows.push_back(std::move(row));
		}
		for (const auto& kind : kinds) {
			if (auto found = lanes.find(kind); found != lanes.end()) out.lanes.push_back(std::move(found->second));
		}
		if (auto others = lanes.find(""); others != lanes.end()) out.lanes.push_back(std::move(others->second));
		return out;
	}

	double snapped(double at, double origin, double grid) {
		if (grid <= 0) return at;
		return std::round((at - origin) / grid) * grid + origin;
	}

	namespace {

		// A canvas's regions: over's rows in order, each as wide as its field says, as
		// it's being dragged, or two grid steps, side by side from the grid's start.
		std::vector<canvas_region> regions_of(page_base& p, const canvas_spec& spec) {
			std::vector<canvas_region> out;
			if (spec.regions.empty() || !spec.over) return out;
			auto resized = p.kept<std::map<std::string, double>>("canvas:" + spec.list + ":resized");
			double step = spec.grid > 0 ? spec.grid : 160;
			double x = spec.origin_x;
			for (const auto& thing : rows_of(**spec.over, spec.over_list)) {
				std::string id = thing["id"].as_string();
				double width = number_of(field_of(thing, spec.regions)).value_or(0);
				if (auto found = resized.get().find(id); found != resized.get().end()) width = found->second;
				if (width <= 0) width = 2 * step;
				std::string title = show(thing["title"]);
				out.push_back({id, title.empty() ? show(thing["name"]) : title, x, width});
				x += width;
			}
			return out;
		}

	}  // namespace

	canvas_model canvas_of(page_base& p, const canvas_spec& spec) {
		canvas_model out;
		out.key = "canvas:" + spec.list;
		out.placing = !spec.place.empty();
		out.grid = spec.grid, out.origin_x = spec.origin_x, out.origin_y = spec.origin_y;
		const view_state& v = *spec.view;
		if (v.status == status::loading || (spec.over && (*spec.over)->status == status::loading)) {
			out.loading = true;
			return out;
		}
		// Where each placed card is until the view says so too: x, y, w, h.
		auto placed = p.kept<std::map<std::string, std::array<double, 4>>>(out.key + ":placed");
		auto rows = rows_of(v, spec.list);
		// In a switch, only what the track above it keeps.
		if (auto kept_to = track_of(p, out.key, spec.track, rows, spec.list)) std::erase_if(rows, [&](const value& row) { return !kept_to->keeps(row, spec.track->field); });
		if (!placed.peek().empty()) {
			auto settled = placed.peek();
			std::erase_if(settled, [&](const auto& m) {
				return std::ranges::any_of(rows, [&](const value& row) {
					return row["id"].as_string() == m.first && number_of(field_of(row, spec.x)) == m.second[0] && number_of(field_of(row, spec.y)) == m.second[1];
				});
			});
			if (settled.size() != placed.peek().size()) placed.set(settled);
		}
		// The columns the unplaced are laid out in: the over list's rows in order, or
		// the by field's values as they come.
		std::vector<std::string> columns;
		if (spec.over) {
			for (const auto& thing : rows_of(**spec.over, spec.over_list)) columns.push_back(thing["id"].as_string());
		}
		double step = spec.grid > 0 ? spec.grid : 160;
		std::map<std::string, int> down;  // how many of each column's cards come before, placed or not, so placing one moves no other
		std::string title = spec.columns.empty() ? "" : spec.columns.front().field;
		out.regions = regions_of(p, spec);
		out.resizing = !spec.resize.empty() && !out.regions.empty();
		// A resize the view now shows is settled.
		if (auto resized = p.kept<std::map<std::string, double>>(out.key + ":resized"); !resized.peek().empty() && spec.over) {
			auto settled = resized.peek();
			std::erase_if(settled, [&](const auto& r) {
				return std::ranges::any_of(rows_of(**spec.over, spec.over_list), [&](const value& thing) { return thing["id"].as_string() == r.first && number_of(field_of(thing, spec.regions)) == r.second; });
			});
			if (settled.size() != resized.peek().size()) resized.set(settled);
		}
		for (const auto& row : rows) {
			if (!p.keeps(row)) continue;
			canvas_card card;
			card.id = row["id"].as_string();
			card.title = show(field_of(row, title));
			if (!spec.kind.empty()) card.kind = show(field_of(row, spec.kind));
			card.tone = tone_of(row, spec.tint);
			for (std::size_t c = 1; c < spec.columns.size(); ++c) {
				if (std::string said_now = show(field_of(row, spec.columns[c].field)); !said_now.empty()) card.details.push_back(numbered(spec.columns[c].field) ? "#" + said_now : said_now);
			}
			if (!spec.link.empty()) card.link = row_link(spec.link, row, p.params(), spec.keyed, spec.named);
			board_card dot;
			status_of(p, row, "", dot);
			card.status = dot.status, card.running = dot.running, card.runner = dot.runner, card.progress = dot.progress;
			if (auto w = number_of(field_of(row, spec.w)); w && *w > 0) card.w = *w;
			if (auto h = number_of(field_of(row, spec.h)); h && *h > 0) card.h = *h;
			auto x = number_of(field_of(row, spec.x)), y = number_of(field_of(row, spec.y));
			std::string in = spec.by.empty() ? "" : show(field_of(row, spec.by));
			int place_in_column = down[in]++;
			if (auto moved = placed.get().find(card.id); moved != placed.get().end()) {
				std::tie(card.x, card.y, card.w, card.h) = std::tuple{moved->second[0], moved->second[1], moved->second[2], moved->second[3]};
				card.placed = true;
			} else if (x && y && number_of(field_of(row, spec.w)).value_or(0) > 0 && number_of(field_of(row, spec.h)).value_or(0) > 0) {
				// Placed: with a size, which only a place gives, as a number never given reads 0.
				card.x = *x, card.y = *y;
				card.placed = true;
			} else {
				// Laid out: its column's place across, and the next free place down it.
				auto at = std::ranges::find(columns, in);
				if (at == columns.end()) {
					columns.push_back(in);
					at = columns.end() - 1;
				}
				card.x = spec.origin_x + static_cast<double>(at - columns.begin()) * step;
				// In its region, when there are regions, a little in from its left.
				for (const auto& region : out.regions) {
					if (region.id == in) card.x = region.x + std::max(0.0, (region.width - card.w) / 2);
				}
				card.y = spec.origin_y + place_in_column * step;
			}
			out.cards.push_back(std::move(card));
		}
		return out;
	}

	void place_card(page_base& p, const canvas_spec& spec, const std::string& card, double x, double y, double w, double h) {
		if (spec.place.empty()) return;
		// Let go in another region, it moves there, as a board's card does.
		if (!spec.move.empty() && !spec.by.empty()) {
			double middle = x + w / 2;
			for (const auto& region : regions_of(p, spec)) {
				if (middle < region.x || middle >= region.x + region.width) continue;
				for (const auto& row : rows_of(*spec.view, spec.list)) {
					if (row["id"].as_string() == card && field_of(row, spec.by).as_string() != region.id) {
						p.run(spec.move, value(std::map<std::string, value>{{"id", card}, {spec.by, region.id}}), [](const answer&) {}, true);
					}
				}
			}
		}
		auto placed = p.kept<std::map<std::string, std::array<double, 4>>>("canvas:" + spec.list + ":placed");
		placed.change([&](auto& now) { now[card] = {x, y, w, h}; });
		p.run(spec.place, value(std::map<std::string, value>{{"id", card}, {spec.x, x}, {spec.y, y}, {spec.w, w}, {spec.h, h}}), [placed, card](const answer& a) {
			if (!a) placed.change([&](auto& now) { now.erase(card); });
		}, true);
	}

	bool paste_on_canvas(page_base& p, const canvas_spec& spec, const std::string& pasted, double x, double y) {
		if (spec.paste.empty() || spec.paste_into.empty()) return false;
		double step = spec.grid > 0 ? spec.grid : 160;
		int made = 0;
		std::istringstream lines(pasted);
		for (std::string line; std::getline(lines, line);) {
			std::string path = local_path(line);
			if (path.empty()) continue;
			std::map<std::string, value> fields;
			for (const auto& [name, id] : spec.given) fields[name] = value(id);
			if (!spec.paste_named.empty()) fields[spec.paste_named] = value(std::filesystem::path(path).stem().string());
			fields[spec.x] = value(x + made * step / 4);
			fields[spec.y] = value(y + made * step / 4);
			fields[spec.w] = value(step);
			fields[spec.h] = value(step);
			p.run(spec.paste, value(std::move(fields)), attached_files{{spec.paste_into, {line}}}, {}, false);
			++made;
		}
		return made > 0;
	}

	void resize_region(page_base& p, const canvas_spec& spec, const std::string& region, double width) {
		if (spec.resize.empty() || spec.regions.empty()) return;
		auto resized = p.kept<std::map<std::string, double>>("canvas:" + spec.list + ":resized");
		resized.change([&](auto& now) { now[region] = width; });
		p.run(spec.resize, value(std::map<std::string, value>{{"id", region}, {spec.regions, width}}), [resized, region](const answer& a) {
			if (!a) resized.change([&](auto& now) { now.erase(region); });
		}, true);
	}

	mosaic_place mosaic_tile(std::size_t n) {
		static constexpr mosaic_place first[] = {{0, 0, 1, 2}, {1, 0, 2, 1}, {1, 1, 1, 1}, {2, 1, 1, 1}};
		static constexpr mosaic_place second[] = {{2, 0, 1, 2}, {0, 0, 2, 1}, {0, 1, 1, 1}, {1, 1, 1, 1}};
		std::size_t tile = n % 8;
		mosaic_place at = tile < 4 ? first[tile] : second[tile - 4];
		at.row += static_cast<int>(n / 8) * 4 + (tile < 4 ? 0 : 2);
		return at;
	}

	int mosaic_rows(std::size_t count) { return static_cast<int>((count + 3) / 4) * 2; }

	std::vector<time_group> time_groups(const std::vector<value>& rows, const std::string& field, time_unit by) {
		using namespace std::chrono;
		static constexpr const char* months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
		std::vector<time_group> out;
		// The zone's offset, looked up again only once a time is past the span it holds for.
		const std::chrono::time_zone* zone = time_zone();
		sys_info offset{};
		// What the last heading is of, as a number, so a row under it is only counted.
		long last = -1;
		for (std::size_t i = 0; i < rows.size(); ++i) {
			const value& when = field_of(rows[i], field);
			long of = 0;
			year_month_day ymd{};
			if (when.is_time()) {
				// A day alone is that day wherever it's read, as a calendar places it.
				auto at = time_point_cast<system_clock::duration>(when.as_time());
				if (!(at >= offset.begin && at < offset.end)) offset = zone->get_info(at);
				ymd = floor<days>(at) == at ? year_month_day(floor<days>(at)) : year_month_day(floor<days>(at + offset.offset));
				of = 1 + static_cast<int>(ymd.year()) * 10000L + (by == time_unit::year ? 0 : static_cast<unsigned>(ymd.month()) * 100L) + (by == time_unit::day ? static_cast<unsigned>(ymd.day()) : 0);
			}
			if (!out.empty() && of == last) {
				++out.back().count;
				continue;
			}
			last = of;
			std::string key = "none", title = "No date";
			if (of != 0) {
				int y = static_cast<int>(ymd.year());
				unsigned m = static_cast<unsigned>(ymd.month()), d = static_cast<unsigned>(ymd.day());
				if (by == time_unit::year) key = std::format("{:04}", y), title = std::to_string(y);
				else if (by == time_unit::month) key = std::format("{:04}-{:02}", y, m), title = std::format("{} {}", months[m - 1], y);
				else key = std::format("{:04}-{:02}-{:02}", y, m, d), title = std::format("{} {}, {}", months[m - 1], d, y);
			}
			out.push_back({key, title, i, 1});
		}
		return out;
	}

	bool within(const std::string& key, const std::string& entry) { return key.starts_with(entry) && (key.size() == entry.size() || key[entry.size()] == '-'); }

	std::vector<dated_entry> years_of(const std::vector<time_group>& groups, time_unit by) {
		static constexpr const char* months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
		std::vector<dated_entry> out;
		std::size_t year_at = 0;
		for (const auto& g : groups) {
			std::string year = g.key == "none" ? "none" : g.key.substr(0, 4);
			if (out.empty() || out[year_at].key != year) {
				year_at = out.size();
				out.push_back({year, year == "none" ? g.title : year, 0, true});
			}
			out[year_at].count += g.count;
			if (by == time_unit::year || year == "none") continue;
			std::string month = g.key.substr(0, 7);
			if (out.back().key != month) out.push_back({month, months[std::stoi(month.substr(5)) - 1], 0, false});
			out.back().count += g.count;
		}
		return out;
	}

	namespace {
		// The row of the step's list it goes to, by the record the page's address names.
		const value* row_stepped_to(page_base& p, const step_spec& spec) {
			const view_state& v = *spec.view;
			std::size_t at = spec.route.rfind("/:");
			if (v.status != status::ready || at == std::string::npos) return nullptr;
			std::string last = spec.route.substr(at + 2);
			if (last.ends_with('*') || last.ends_with('?')) last.pop_back();
			// A row is the one shown when it holds what the parameter names, as its link does,
			// or is that one itself.
			auto named = [&](const value& row) {
				const value& own = row[last];
				return own.is_string() && !own.text.empty() ? own.text : row["id"].as_string();
			};
			const auto& rows = rows_of(v, spec.list);
			std::string shown = p.param(last);
			for (std::size_t i = 0; i < rows.size(); ++i) {
				if (named(rows[i]) != shown) continue;
				if (spec.next ? i + 1 == rows.size() : i == 0) return nullptr;
				return &rows[spec.next ? i + 1 : i - 1];
			}
			return nullptr;
		}
	}  // namespace

	std::string picture_stepped_to(page_base& p, const step_spec& spec, const std::string& to) {
		if (to.empty()) return "";
		if (std::string shown = spec.opened ? picture_shown_for(p, to) : ""; !shown.empty()) return shown;
		if (spec.picture.empty()) return "";
		const value* row = row_stepped_to(p, spec);
		return row && row_link(spec.route, *row, p.params()) == to ? (*row)[spec.picture].as_string() : "";
	}

	std::string stepped_to(page_base& p, const step_spec& spec) {
		if (const place* from = spec.opened ? p.opened_from() : nullptr) {
			for (const auto& [list, addresses] : from->lists) {
				auto at = std::ranges::find(*addresses, p.address());
				if (at == addresses->end()) continue;
				if (spec.next ? at + 1 == addresses->end() : at == addresses->begin()) return "";
				return *(spec.next ? at + 1 : at - 1);
			}
		}
		const value* row = row_stepped_to(p, spec);
		return row ? row_link(spec.route, *row, p.params()) : "";
	}

	std::string picture_shown_for(page_base& p, const std::string& to) {
		const place* from = p.opened_from();
		if (!from || to.empty()) return "";
		for (const auto& [list, addresses] : from->lists) {
			auto pictures = from->pictures.find(list);
			if (pictures == from->pictures.end() || std::ranges::find(*addresses, p.address()) == addresses->end()) continue;
			auto at = std::ranges::find(*addresses, to);
			if (at == addresses->end() || pictures->second->size() != addresses->size()) continue;
			return (*pictures->second)[static_cast<std::size_t>(at - addresses->begin())];
		}
		return "";
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
				std::string said_now = show(field_of(row, spec.columns[c].field));
				if (said_now.empty()) continue;
				// A choice is a tag beside its title, as its choices say it; the rest are details.
				if (auto choices = spec.choices.find(spec.columns[c].field); choices != spec.choices.end()) {
					auto found = std::ranges::find_if(choices->second, [&](const option& o) { return o.value == said_now; });
					card.tags.push_back(found != choices->second.end() ? found->label : said_now);
					continue;
				}
				card.details.push_back(spec.columns[c].label + ": " + said_now);
			}
			// What it holds, counted by what each is at, in order: 3 To do · 1 Done.
			if (spec.tally) {
				struct group {
					std::string label;
					int count = 0;
					double order = 0;
					std::string state = "partial";  // what its stage's status says of it
				};
				std::map<std::string, group> groups;
				int total = 0;
				// The first stage anything is in, across the whole list: where work hasn't started.
				double first_order = std::numeric_limits<double>::infinity();
				if (!spec.tally->order.empty()) {
					for (const auto& held : rows_of(spec.tally->view.get(), spec.tally->list)) {
						if (value o = field_of(held, spec.tally->order); o.is_integer() || o.is_double()) first_order = std::min(first_order, o.as_double());
					}
				}
				for (const auto& held : rows_of(spec.tally->view.get(), spec.tally->list)) {
					if (field_of(held, spec.tally->by).as_string() != card.id) continue;
					++total;
					std::string at = show(field_of(held, spec.tally->and_by));
					auto& g = groups[at];
					if (g.label.empty()) g.label = show(field_of(held, spec.tally->shown.empty() ? spec.tally->and_by : spec.tally->shown));
					if (g.label.empty()) g.label = "None";
					if (!spec.tally->order.empty()) g.order = field_of(held, spec.tally->order).as_double();
					// Done once its status is success, failed once it's failed, not started in
					// the first stage of all, and under way anywhere else.
					std::string status = spec.tally->status.empty() ? "" : show(field_of(held, spec.tally->status));
					bool first = !spec.tally->order.empty() && field_of(held, spec.tally->order).as_double() == first_order;
					g.state = status == "success" || status == "done" ? "done" : status == "failed" ? "failed" : first ? "none" : "partial";
					++g.count;
				}
				std::vector<group> in_order;
				for (auto& [_, g] : groups) in_order.push_back(g);
				std::ranges::stable_sort(in_order, {}, &group::order);
				for (const auto& g : in_order) {
					card.tally += (card.tally.empty() ? "" : " · ") + std::format("{} {}", g.count, g.label);
					card.counted.emplace_back(g.label, g.count);
					card.counted_states.push_back(g.state);
				}
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
			// What holds nothing isn't said, as the web leaves it out. A moment is said
			// with its time of day, as a record's own details are read closely.
			const value& held = field_of(v.data, f.field);
			std::string says = f.bytes && (held.is_integer() || held.is_double()) ? read_value(held.as_double(), "bytes") : cell_text(choices, v.data, f.field);
			if (held.is_time() && (held.time.seconds % 86400 != 0 || held.time.nanos != 0)) says = said_with_time(held);
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
