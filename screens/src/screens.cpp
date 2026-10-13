// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/screens/screens.hpp"

#include <one/client/client.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <format>

namespace one::screens {

	// Values

	namespace {
		const std::chrono::time_zone*& zone_named() {
			static const std::chrono::time_zone* named = nullptr;
			return named;
		}
	}  // namespace

	const std::chrono::time_zone* time_zone() {
		if (zone_named()) return zone_named();
		try {
			return std::chrono::current_zone();
		} catch (const std::exception&) {
			return std::chrono::locate_zone("UTC");
		}
	}

	void use_time_zone(const std::string& name) { zone_named() = std::chrono::locate_zone(name); }

	std::string show(const value& v) {
		switch (v.type) {
			case value::kind::string:
			case value::kind::reference: return v.text;
			case value::kind::integer: return std::to_string(v.integer);
			case value::kind::floating: return std::format("{}", v.floating);
			case value::kind::boolean: return v.boolean ? "yes" : "no";
			case value::kind::time: {
				// A date alone, kept as midnight UTC, is that day wherever it's read.
				bool date_only = v.time.seconds % 86400 == 0 && v.time.nanos == 0;
				if (date_only) {
					std::string day = std::format("{:%b %e, %Y}", std::chrono::floor<std::chrono::days>(v.as_time()));
					if (auto doubled = day.find("  "); doubled != std::string::npos) day.erase(doubled, 1);
					return day;
				}
				auto when = std::chrono::zoned_time(time_zone(), std::chrono::floor<std::chrono::minutes>(v.as_time())).get_local_time();
				auto now = std::chrono::zoned_time(time_zone(), std::chrono::system_clock::now()).get_local_time();
				// Today's by its time, any other by its day.
				if (std::chrono::floor<std::chrono::days>(when) == std::chrono::floor<std::chrono::days>(now)) return std::format("{:%H:%M}", when);
				std::string day = std::format("{:%b %e, %Y}", std::chrono::floor<std::chrono::days>(when));
				if (auto doubled = day.find("  "); doubled != std::string::npos) day.erase(doubled, 1);
				return day;
			}
			case value::kind::array: {
				std::string out;
				for (const auto& item : v.items) {
					std::string shown = show(item);
					if (!shown.empty()) out += (out.empty() ? "" : ", ") + shown;
				}
				return out;
			}
			default: return "";
		}
	}

	std::string this_year() {
		auto today = std::chrono::year_month_day(std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()));
		return std::to_string(static_cast<int>(today.year()));
	}

	std::string said(const std::vector<part>& parts) {
		std::string out;
		for (const auto& p : parts) out += p.from ? show(field_of(p.from->get().data, p.field)) : p.text;
		return out;
	}

	const value& field_of(const value& document, const std::string& path) { return one::client::field(document, path); }

	const std::vector<value>& rows_of(const view_state& v, const std::string& list) { return v.data[list.empty() ? "rows" : list].as_array(); }

	// Addresses and keys, as the web makes them

	namespace {

		bool kept_plain(unsigned char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
		}

		// As encodeURIComponent does.
		std::string encoded(std::string_view text, std::string_view also_plain = "") {
			std::string out;
			for (unsigned char c : text) {
				if (kept_plain(c) || also_plain.find(static_cast<char>(c)) != std::string_view::npos || c == '!' || c == '\'' || c == '(' || c == ')' || c == '*') {
					out += static_cast<char>(c);
				} else {
					out += std::format("%{:02X}", c);
				}
			}
			return out;
		}

		std::optional<std::string> decoded(std::string_view text) {
			std::string out;
			for (std::size_t i = 0; i < text.size(); ++i) {
				if (text[i] != '%') {
					out += text[i];
					continue;
				}
				if (i + 2 >= text.size()) return std::nullopt;
				int high = std::isxdigit(static_cast<unsigned char>(text[i + 1])) ? std::stoi(std::string(text.substr(i + 1, 2)), nullptr, 16) : -1;
				if (high < 0) return std::nullopt;
				out += static_cast<char>(high);
				i += 2;
			}
			return out;
		}

		std::vector<std::string> segments_by(std::string_view text, char by) {
			std::vector<std::string> out;
			std::size_t at = 0;
			while (at < text.size()) {
				std::size_t end = text.find(by, at);
				if (end == std::string_view::npos) end = text.size();
				if (end > at) out.emplace_back(text.substr(at, end - at));
				at = end + 1;
			}
			return out;
		}

		std::vector<std::string> segments(std::string_view path) {
			std::vector<std::string> out;
			std::size_t at = 0;
			while (at < path.size()) {
				std::size_t end = path.find('/', at);
				if (end == std::string_view::npos) end = path.size();
				if (end > at) out.emplace_back(path.substr(at, end - at));
				at = end + 1;
			}
			return out;
		}

	}  // namespace

	// As keyOf does: each part escaped as in a path, and a hyphen within a part after
	// the first escaped too, so the last hyphens are where it's cut.
	std::string key_of(const std::vector<std::string>& parts) {
		std::string out;
		for (std::size_t i = 0; i < parts.size(); ++i) {
			std::string escaped;
			for (unsigned char c : parts[i]) {
				if (c == '!' || c == '\'' || c == '(' || c == ')' || c == '*') escaped += std::format("%{:02X}", c);
				else escaped += encoded(std::string_view(reinterpret_cast<const char*>(&c), 1), "$&+:=@");
			}
			if (i > 0) {
				for (std::size_t at = escaped.find('-'); at != std::string::npos; at = escaped.find('-', at + 3)) escaped.replace(at, 1, "%2D");
			}
			out += (i == 0 ? "" : "-") + escaped;
		}
		return out;
	}

	std::optional<std::vector<std::string>> parts_of(const std::string& id, std::size_t count) {
		if (count == 0) return std::nullopt;
		std::vector<std::size_t> cuts;
		for (std::size_t at = id.size(); at-- > 0 && cuts.size() < count - 1;) {
			if (id[at] == '-') cuts.insert(cuts.begin(), at);
		}
		if (cuts.size() != count - 1) return std::nullopt;
		std::vector<std::string> out;
		std::size_t from = 0;
		for (std::size_t cut : cuts) {
			auto part = decoded(std::string_view(id).substr(from, cut - from));
			if (!part) return std::nullopt;
			out.push_back(*part);
			from = cut + 1;
		}
		auto last = decoded(std::string_view(id).substr(from));
		if (!last) return std::nullopt;
		out.push_back(*last);
		return out;
	}

	std::string row_link(const std::string& route, const value& row, const std::map<std::string, std::string>& params, const std::vector<std::string>& keyed,
	                     const std::map<std::string, int>& named) {
		std::vector<std::string> names;
		for (const auto& s : segments(route)) {
			if (s.starts_with(':')) names.push_back(s.substr(1, s.ends_with('?') ? s.size() - 2 : std::string::npos));
		}
		auto own = [&](const std::string& name) -> std::optional<std::string> {
			const value& f = row[name];
			if (f.is_string() && !f.text.empty()) return f.text;
			return std::nullopt;
		};
		std::string last = names.empty() ? "" : names.back();
		// A row that holds what the last parameter names, like a change's issue, links
		// to that; otherwise to the row itself.
		std::string opened = (last.empty() ? std::nullopt : own(last)).value_or(row["id"].as_string());
		auto parts = keyed.empty() ? std::nullopt : parts_of(opened, keyed.size() + 1);
		std::map<std::string, std::string> values;
		if (parts)
			for (std::size_t i = 0; i < keyed.size(); ++i) values[keyed[i]] = (*parts)[i];
		std::string out;
		for (const auto& s : segments(route)) {
			out += "/";
			if (!s.starts_with(':')) {
				out += s;
				continue;
			}
			std::string name = s.substr(1, s.ends_with('?') ? s.size() - 2 : std::string::npos);
			if (name == last) {
				out += encoded(parts ? parts->back() : opened);
				continue;
			}
			std::optional<std::string> filled;
			if (auto v = values.find(name); v != values.end()) filled = v->second;
			else if (auto p = params.find(name); p != params.end()) filled = p->second;
			else if (auto held = own(name)) {
				// A row's board, by its id, fills /:board with the board's own name.
				auto keys = named.find(name);
				auto split = keys != named.end() ? parts_of(*held, static_cast<std::size_t>(keys->second)) : std::nullopt;
				filled = split ? split->back() : *held;
			} else {
				filled = row["id"].as_string();
			}
			out += encoded(*filled);
		}
		return out.empty() ? "/" : out;
	}

	std::string label(std::string_view name) {
		std::string out;
		for (char c : name) out += c == '_' ? ' ' : c;
		if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
		return out;
	}

	std::string shown_as(const std::vector<option>& choices, const std::string& value) {
		for (const auto& o : choices) {
			if (o.value == value) return o.label;
		}
		return value;
	}

	std::string filled(const std::string& route, const std::map<std::string, std::string>& params) {
		std::string out;
		for (const auto& segment : segments(route)) {
			std::string name = segment.substr(1, segment.ends_with('?') ? segment.size() - 2 : std::string::npos);
			auto found = params.find(name);
			out += "/" + (segment.starts_with(':') ? (found == params.end() ? std::string() : found->second) : segment);
		}
		return out.empty() ? "/" : out;
	}

	std::vector<std::pair<std::string, std::string>> query_of(const std::string& address) {
		std::vector<std::pair<std::string, std::string>> out;
		std::size_t mark = address.find('?');
		if (mark == std::string::npos) return out;
		for (const auto& pair : segments_by(address.substr(mark + 1), '&')) {
			std::size_t equals = pair.find('=');
			auto plain = [](std::string s) {
				std::ranges::replace(s, '+', ' ');
				return decoded(s).value_or(s);
			};
			out.emplace_back(plain(pair.substr(0, equals)), equals == std::string::npos ? "" : plain(pair.substr(equals + 1)));
		}
		return out;
	}

	int match(const std::vector<std::string>& routes, const std::string& address, std::map<std::string, std::string>& params) {
		auto given = segments(address.substr(0, address.find('?')));
		int best = -1, best_score = -1;
		for (std::size_t i = 0; i < routes.size(); ++i) {
			auto wanted = segments(routes[i]);
			std::map<std::string, std::string> found;
			bool fits = given.size() <= wanted.size();
			int score = 0;
			for (std::size_t at = 0; at < wanted.size() && fits; ++at) {
				const std::string& w = wanted[at];
				bool optional = w.starts_with(':') && w.ends_with('?');
				if (at >= given.size()) {
					fits = optional;
					continue;
				}
				if (w.starts_with(':')) {
					auto value = decoded(given[at]);
					if (!value) fits = false;
					else found[w.substr(1, optional ? w.size() - 2 : std::string::npos)] = *value;
					score += optional ? 2 : 3;
				} else if (w != given[at]) {
					fits = false;
				} else {
					score += 10;  // as the web's router weighs a part named outright
				}
			}
			if (fits && score > best_score) {
				best = static_cast<int>(i);
				best_score = score;
				params = std::move(found);
			}
		}
		return best;
	}

	// page

	page_base::page_base(screens::source& from, std::size_t screen, std::string screen_title, std::string address, std::map<std::string, std::string> params,
	                     std::vector<std::pair<std::string, std::string>> query, std::function<bool(const std::string&)> opener,
	                     std::function<bool(const std::string&)> replacer)
	    : source_(from), screen_(screen), screen_title_(std::move(screen_title)), address_(std::move(address)), params_(std::move(params)), query_(std::move(query)),
	      opener_(std::move(opener)), replacer_(std::move(replacer)) {}

	namespace {
		// Whether a time is before or after one counted from now, like -7d.
		bool kept_by_time(const value& held, const std::string& op, const std::string& offset) {
			if (!held.is_time() || offset.size() < 3 || (offset[0] != '-' && offset[0] != '+')) return false;
			char unit = offset.back();
			double hours = unit == 'm' ? 1.0 / 60 : unit == 'h' ? 1 : unit == 'd' ? 24 : unit == 'w' ? 24 * 7 : 0;
			if (hours == 0) return false;
			double count = 0;
			try {
				count = std::stod(offset.substr(1, offset.size() - 2));
			} catch (...) {
				return false;
			}
			auto then = std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::system_clock::duration>(
			                                                     std::chrono::duration<double, std::ratio<3600>>((offset[0] == '-' ? -1 : 1) * count * hours));
			auto at = held.as_time();
			return op == "<" ? at < then : op == ">" ? at > then : op == "<=" ? at <= then : at >= then;
		}

		bool filtering(const std::string& name) { return name != "is" && name != "find" && name != "search"; }
	}  // namespace

	bool page_base::keeps(const value& row) const { return screens::kept(row, query_, source_.reader() ? source_.reader()->uid : ""); }

	bool kept(const value& row, const std::vector<std::pair<std::string, std::string>>& picks, const std::string& me) {
		return std::ranges::all_of(picks, [&](const auto& pick) {
			const auto& [name, wanted] = pick;
			if (!filtering(name)) return true;
			for (const char* op : {"<=", ">=", "<", ">"}) {
				if (name.ends_with(op)) return kept_by_time(field_of(row, name.substr(0, name.size() - std::strlen(op))), op, wanted);
			}
			std::string looked_for = wanted == "me" ? me : wanted;
			const value& held = field_of(row, name);
			if (held.is_array()) return std::ranges::any_of(held.items, [&](const value& item) { return show(item) == looked_for; });
			return show(held) == looked_for;
		});
	}

	std::string page_base::filtered() const {
		std::string said, is;
		for (const auto& [name, wanted] : query_) {
			if (name == "is") is = wanted;
			else if (filtering(name)) said += (said.empty() ? "" : ", ") + label(name) + ": " + wanted;
		}
		return said.empty() ? "" : is.empty() ? said : is;
	}

	void page_base::clear_filter() { replacer_(address_.substr(0, address_.find('?'))); }

	std::string page_base::param(const std::string& name) const {
		auto found = params_.find(name);
		return found == params_.end() ? "" : found->second;
	}

	state<view_state> page_base::view(const std::string& name, const std::string& subject) {
		std::string at = subject.empty() ? name : name + ":" + subject;
		auto found = views_.find(at);
		if (found == views_.end()) {
			auto made = std::make_unique<listened>();
			follow(*made, name, subject);
			found = views_.emplace(at, std::move(made)).first;
		} else if (!found->second->moved->empty()) {
			// What it read moved, as a board does when it's renamed: it's read where it
			// went, here as it's built, never from inside what told it.
			follow(*found->second, name, std::exchange(*found->second->moved, ""));
		}
		return found->second->current;
	}

	// Listens to a view's document for a subject; one that says where its subject
	// moved is followed there the next time the page reads it.
	void page_base::follow(listened& l, const std::string& name, const std::string& subject) {
		l.keeping = source_.listen(name, subject, [current = l.current, moved = l.moved](const view_state& v) {
			if (const value* to = v.data.find("moved"); to && to->is_string() && !to->text.empty()) {
				*moved = to->text;
				current.set(view_state{});  // loading, until it's read where it went
				return;
			}
			current.set(v);
		});
	}

	void page_base::open(const std::string& address) { opener_(address); }

	void page_base::answer_asked(bool yes) {
		auto was = asked_.peek();
		asked_.set(std::nullopt);
		if (!yes || !was) return;
		std::string question = std::exchange(questions_[was->command], "");  // asked once
		run(was->command, was->fields, was->files, was->after, was->says_why_itself);
		questions_[was->command] = question;
	}

	void page_base::run(const std::string& command, const value& fields, std::function<void(const answer&)> after, bool says_why_itself) {
		run(command, fields, attached_files{}, std::move(after), says_why_itself);
	}

	void page_base::run(const std::string& command, const value& fields, attached_files files, std::function<void(const answer&)> after, bool says_why_itself) {
		if (auto q = questions_.find(command); q != questions_.end() && !q->second.empty()) {
			asked_.set(asking{command, q->second, fields, std::move(after), says_why_itself, std::move(files)});
			return;
		}
		// Told through states, which outlive the page if it's left before the answer.
		auto running = running_;
		auto problem = problem_;
		auto errors = errors_;
		running.change([&](auto& now) { ++now[command]; });
		problem.set("");
		clear(command);
		source_.run(command, fields, files, [running, problem, errors, command, says_why_itself, after = std::move(after)](const answer& a) {
			running.change([&](auto& now) {
				if (--now[command] <= 0) now.erase(command);
			});
			if (!a) {
				if (!says_why_itself) problem.set(a.error());
				errors.change([&](auto& now) { now[command] = a.error(); });
			}
			if (after) after(a);
		});
	}

	bool page_base::running(const std::string& command) const { return running_.get().contains(command); }

	std::string page_base::error(const std::string& command) const {
		auto found = errors_.get().find(command);
		return found == errors_.get().end() ? "" : found->second;
	}

	void page_base::clear(const std::string& command) {
		if (!errors_.peek().contains(command)) return;
		errors_.change([&](auto& now) { now.erase(command); });
	}

	std::string page_base::title() const { return title_.empty() ? screen_title_ : said(title_); }

	// navigator

	navigator_base::navigator_base(std::vector<std::string> routes, screens::source& from) : source_(from), routes_(std::move(routes)) {}

	std::unique_ptr<page_base> navigator_base::page_at(const std::string& address) {
		std::map<std::string, std::string> params;
		int at = match(address, params);
		if (at < 0) return nullptr;
		return made(static_cast<std::size_t>(at), address, std::move(params), query_of(address));
	}

	bool navigator_base::open(const std::string& address) {
		auto p = page_at(address);
		if (!p) return false;
		trail_.push_back(std::move(p));
		changed_.set(changed_.peek() + 1);
		return true;
	}

	bool navigator_base::replace(const std::string& address) {
		auto p = page_at(address);
		if (!p) return false;
		trail_.back() = std::move(p);
		changed_.set(changed_.peek() + 1);
		return true;
	}

	bool navigator_base::back() {
		if (trail_.size() < 2) return false;
		trail_.pop_back();
		changed_.set(changed_.peek() + 1);
		return true;
	}

	state<view_state> navigator_base::view(const std::string& name, const std::string& subject) {
		std::string at = subject.empty() ? name : name + ":" + subject;
		auto found = views_.find(at);
		if (found == views_.end()) {
			state<view_state> current;
			found = views_.emplace(at, std::pair{current, source_.listen(name, subject, [current](const view_state& v) { current.set(v); })}).first;
		}
		return found->second.first;
	}

	// Files

	void source::run(const std::string& command, const value& fields, const attached_files& files, std::function<void(const answer&)> answered) {
		if (files.empty()) {
			run(command, fields, std::move(answered));
			return;
		}
		answered(std::unexpected(std::string("files can't be attached from here")));
	}

	std::string local_path(std::string written) {
		auto trimmed = [](std::string s) {
			while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
			std::size_t start = 0;
			while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
			return s.substr(start);
		};
		written = trimmed(std::move(written));
		// Quoted, as a shell or a file manager may copy it.
		if (written.size() >= 2 && (written.front() == '\'' || written.front() == '"') && written.back() == written.front()) written = written.substr(1, written.size() - 2);
		if (written.starts_with("file://")) {
			std::string decoded;
			for (std::size_t i = 7; i < written.size(); ++i) {
				if (written[i] == '%' && i + 2 < written.size() && std::isxdigit(static_cast<unsigned char>(written[i + 1])) && std::isxdigit(static_cast<unsigned char>(written[i + 2]))) {
					decoded += static_cast<char>(std::stoi(written.substr(i + 1, 2), nullptr, 16));
					i += 2;
				} else {
					decoded += written[i];
				}
			}
			// file://host/path, the host most often left out, as file:///path.
			written = decoded.starts_with("/") ? decoded : decoded.substr(std::min(decoded.find('/'), decoded.size()));
		}
		if (written == "~" || written.starts_with("~/")) {
			const char* home = std::getenv("HOME");
			written = std::string(home ? home : "") + written.substr(1);
		}
		return written;
	}

	value with_uploaded(value fields, const std::string& field, const std::vector<std::string>& uploaded) {
		if (uploaded.empty()) return fields;
		std::map<std::string, value> all = fields.as_map();
		if (auto had = all.find(field); had != all.end() && had->second.is_array()) {
			std::vector<value> now = had->second.as_array();
			for (const auto& path : uploaded) now.push_back(value(path));
			had->second = value(std::move(now));
		} else {
			all[field] = value(uploaded.front());
		}
		return value(std::move(all));
	}

}  // namespace one::screens
