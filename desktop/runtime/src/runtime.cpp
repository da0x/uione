// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

#include "one/desktop/runtime/runtime.hpp"

#include <one/client/client.hpp>

#include <algorithm>
#include <chrono>
#include <format>

#include "one/desktop/app.hpp"

namespace one::desktop::runtime {

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

	// page

	page::page(navigator& opened_by, runtime::source& from, const screen& shows, std::string address, std::map<std::string, std::string> params)
	    : navigator_(opened_by), source_(from), screen_(&shows), address_(std::move(address)), params_(std::move(params)) {}

	std::string page::param(const std::string& name) const {
		auto found = params_.find(name);
		return found == params_.end() ? "" : found->second;
	}

	state<view_state> page::view(const std::string& name, const std::string& subject) {
		std::string at = subject.empty() ? name : name + ":" + subject;
		auto found = views_.find(at);
		if (found == views_.end()) {
			auto made = std::make_unique<listened>();
			state<view_state> current = made->current;
			made->keeping = source_.listen(name, subject, [current](const view_state& v) { current.set(v); });
			found = views_.emplace(at, std::move(made)).first;
		}
		return found->second->current;
	}

	void page::open(const std::string& address) { navigator_.open(address); }

	void page::run(const std::string& command, const value& fields, std::function<void(const answer&)> after, bool says_why_itself) {
		// Told through states, which outlive the page if it's left before the answer.
		auto running = running_;
		auto problem = problem_;
		auto errors = errors_;
		running.change([&](auto& now) { ++now[command]; });
		problem.set("");
		clear(command);
		source_.run(command, fields, [running, problem, errors, command, says_why_itself, after = std::move(after)](const answer& a) {
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

	bool page::running(const std::string& command) const { return running_.get().contains(command); }

	std::string page::error(const std::string& command) const {
		auto found = errors_.get().find(command);
		return found == errors_.get().end() ? "" : found->second;
	}

	void page::clear(const std::string& command) {
		if (!errors_.peek().contains(command)) return;
		errors_.change([&](auto& now) { now.erase(command); });
	}

	std::string page::title() const { return title_.empty() ? screen_->title : said(title_); }

	// navigator

	navigator::navigator(const runtime::site& s, runtime::source& from) : site_(s), source_(from) {}

	const screen* navigator::match(const std::string& address, std::map<std::string, std::string>& params) const {
		auto given = segments(address.substr(0, address.find('?')));
		for (const auto& s : site_.screens) {
			auto wanted = segments(s.route);
			std::map<std::string, std::string> found;
			bool fits = true;
			std::size_t at = 0;
			for (; at < wanted.size() && fits; ++at) {
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
				} else if (w != given[at]) {
					fits = false;
				}
			}
			if (fits && given.size() <= wanted.size()) {
				params = std::move(found);
				return &s;
			}
		}
		return nullptr;
	}

	bool navigator::open(const std::string& address) {
		std::map<std::string, std::string> params;
		const screen* s = match(address, params);
		if (!s) return false;
		trail_.push_back(std::make_unique<page>(*this, source_, *s, address, std::move(params)));
		changed_.set(changed_.peek() + 1);
		return true;
	}

	bool navigator::back() {
		if (trail_.size() < 2) return false;
		trail_.pop_back();
		changed_.set(changed_.peek() + 1);
		return true;
	}

	desktop::view navigator::page_view(page& p) {
		const auto& t = current_theme();
		desktop::view built = p.shown().build ? p.shown().build(p) : desktop::column();
		auto around = p.take_around();
		std::vector<desktop::view> parts;
		for (auto& v : around.above) parts.push_back(std::move(v));
		std::vector<desktop::view> title_row;
		title_row.push_back(desktop::text(p.title(), t.heading(26)).grow());
		for (auto& v : around.beside) title_row.push_back(std::move(v));
		parts.push_back(desktop::row(std::move(title_row)).spacing(10).align(align::center));
		for (auto& v : around.under) parts.push_back(std::move(v));
		if (std::string wrong = *p.problem(); !wrong.empty()) {
			auto style = t.strong();
			style.color = t.danger;
			parts.push_back(desktop::text(wrong, style));
		}
		parts.push_back(std::move(built));
		if (p.take_wide()) return desktop::scroll(desktop::column(std::move(parts)).spacing(16).padding(edges(24, 32)));
		return desktop::scroll(desktop::column(std::move(parts)).spacing(16).padding(edges(24, 32)).width(880)).align(align::center);
	}

	desktop::view navigator::show() {
		const auto& t = current_theme();
		(void)*changed_;
		page& here = current();
		auto brand = t.strong();
		brand.color = t.accent;
		brand.size = 17;
		std::vector<desktop::view> bar;
		if (trail_.size() > 1) bar.push_back(desktop::button("‹ Back", [this] { back(); }));
		bar.push_back(desktop::link(site_.name, [this] {
			if (!open("/") && !site_.screens.empty()) open(site_.screens.front().route);
		}, brand));
		for (const auto& s : site_.screens) {
			if (s.nav.empty()) continue;
			std::string route = s.route;
			bar.push_back(desktop::link(s.nav, [this, route] { open(route); }, s.route == here.shown().route ? t.strong() : t.body()));
		}
		bar.push_back(desktop::spacer());
		if (auto who = here.reader(); who && !who->name.empty()) {
			bar.push_back(desktop::avatar(who->name, 24));
			bar.push_back(desktop::text(who->name, t.quiet()));
		}
		auto top = desktop::row(std::move(bar)).spacing(14).align(align::center).padding(edges(10, 20)).background({.fill = t.surface});
		// Keyed by where it is, so a page keeps nothing of the one before it.
		std::string key = std::format("{}#{}", here.address(), trail_.size());
		return desktop::column(std::move(top), desktop::divider(), page_view(here).key(key).grow());
	}

	int run(const runtime::site& s, desktop::app& app, runtime::source& from, const std::string& address) {
		auto& window = app.open({.title = s.name, .id = s.id, .size = {1200, 800}});
		window.look(s.look);
		window.background = s.look.page;
		navigator nav(s, from);
		if (!nav.open(address) && !nav.open(s.screens.empty() ? "/" : s.screens.front().route)) return 2;
		window.show([&] {
			auto shown = nav.show();
			std::string title = nav.current().title();
			window.title(title.empty() || title == s.name ? s.name : title + " · " + s.name);
			return shown;
		});
		window.on_key = [&](const key_event& e) {
			if (e.is(keys::escape) || e.is(keys::backspace)) return nav.back();
			return false;
		};
		return app.run();
	}

}  // namespace one::desktop::runtime
