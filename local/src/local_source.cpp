// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include <algorithm>
#include <chrono>
#include <filesystem>

#include "one/local/local.hpp"

namespace one::local {

	namespace {

		std::string now() {
			auto since = std::chrono::system_clock::now().time_since_epoch();
			auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since);
			ember::timestamp t;
			t.seconds = seconds.count();
			t.nanos = static_cast<int>(std::chrono::duration_cast<std::chrono::nanoseconds>(since - seconds).count());
			return ember::to_string(t);
		}

	}  // namespace

	std::expected<std::unique_ptr<local_source>, std::string> local_source::open(folder kept) {
		auto events = kept.read();
		if (!events) return std::unexpected(events.error());
		std::unique_ptr<local_source> source(new local_source(std::move(kept)));
		for (const auto& e : *events) source->store_.apply(e);
		// What nothing names any more, like a picture deleted or a preview drawn again,
		// goes, now, while there's no undoing to bring it back.
		source->folder_.forget_files_but(source->store_.files_named());
		return source;
	}

	screens::view_state local_source::build(const std::string& view, const std::string& subject) const {
		auto found = views_.find(view);
		if (found == views_.end()) return {screens::status::failed, value::map(), "there's no view " + view};
		std::optional<value> built = found->second(store_, subject);
		if (!built) return {screens::status::missing, value::map(), ""};
		return {screens::status::ready, std::move(*built), ""};
	}

	void local_source::tell(listener& l) {
		screens::view_state state = build(l.view, l.subject);
		std::string seen = std::to_string(static_cast<int>(state.status)) + ember::to_json(state.data);
		if (seen == l.last) return;
		l.last = std::move(seen);
		l.told(state);
	}

	screens::listening local_source::listen(const std::string& view, const std::string& subject, std::function<void(const screens::view_state&)> told) {
		int id = next_++;
		tell(listeners_[id] = listener{view, subject, std::move(told), ""});
		std::weak_ptr<bool> alive = alive_;
		return screens::listening([this, alive, id] {
			if (alive.lock()) listeners_.erase(id);
		});
	}

	void local_source::run(const std::string& command_name, const value& fields, std::function<void(const screens::answer&)> answered) {
		if (command_name == "one::undo") return take_back(undoing_, redoing_, "undo", std::move(answered));
		if (command_name == "one::redo") return take_back(redoing_, undoing_, "redo", std::move(answered));
		auto found = commands_.find(command_name);
		if (found == commands_.end()) {
			answered(std::unexpected("there's no command " + command_name));
			return;
		}
		// The history's word on it is the source's, not the command's.
		value sent = fields;
		std::string history = sent["one:history"].as_string();
		if (sent.is_map()) sent.fields.erase("one:history");
		step changes(store_);
		screens::answer answer = found->second(sent, changes);
		if (!answer || changes.changes().empty()) {
			answered(answer);
			return;
		}
		auto back = back_from(changes.changes());
		event e{.command = command_name, .at = now(), .changes = changes.changes()};
		if (auto kept = folder_.append(e); !kept) {
			answered(std::unexpected(kept.error()));  // nothing changed, since nothing was kept
			return;
		}
		store_.apply(e);
		if (history == "joined" && !undoing_.empty()) {
			// Taken back before what was already in the step, since it came after.
			undoing_.back().back.insert(undoing_.back().back.begin(), back.begin(), back.end());
		} else if (history != "apart") {
			undoing_.push_back({command_name, std::move(back)});
			redoing_.clear();
		}
		tell_all();
		answered(answer);
	}

	// Each row a change touches, as it is before the change: what puts it back.
	std::vector<local_source::putting_back> local_source::back_from(const std::vector<change>& changes) const {
		std::vector<putting_back> back;
		for (const auto& c : changes) {
			const value* before = store_.find(c.entity, c.id);
			if (!before) {
				if (!c.removed) back.push_back({c.entity, c.id, putting_back::removing});
				continue;
			}
			if (c.removed) {
				back.push_back({c.entity, c.id, putting_back::making, *before});
				continue;
			}
			putting_back fields{c.entity, c.id};
			for (const auto& [name, now] : c.fields.as_map()) {
				const value& was = (*before)[name];
				if (ember::to_json(was) != ember::to_json(now)) fields.was.fields[name] = was;
			}
			for (const auto& [name, was] : before->as_map())
				if (!c.fields.as_map().contains(name)) fields.was.fields[name] = was;
			if (!fields.was.as_map().empty()) back.push_back(std::move(fields));
		}
		std::ranges::reverse(back);
		return back;
	}

	// What puts rows back as they were, as changes to the rows as they are now.
	std::vector<change> local_source::changes_for(const std::vector<putting_back>& back) const {
		std::vector<change> out;
		std::map<std::pair<std::string, std::string>, value> rows;  // as each is left by those before it
		auto row_of = [&](const putting_back& b) -> std::optional<value> {
			if (auto it = rows.find({b.entity, b.id}); it != rows.end()) return it->second;
			const value* now = store_.find(b.entity, b.id);
			return now ? std::optional<value>(*now) : std::nullopt;
		};
		for (const auto& b : back) {
			if (b.how == putting_back::removing) {
				out.push_back({.entity = b.entity, .id = b.id, .removed = true});
				rows[{b.entity, b.id}] = value();
				continue;
			}
			value row = b.how == putting_back::making ? b.was : row_of(b).value_or(value());
			if (b.how == putting_back::fields) {
				if (row.is_null()) continue;  // gone since, so there's nothing to put back
				for (const auto& [name, was] : b.was.as_map()) {
					if (was.is_null()) row.fields.erase(name);
					else row.fields[name] = was;
				}
			}
			row.fields["id"] = b.id;
			out.push_back({.entity = b.entity, .id = b.id, .fields = row});
			rows[{b.entity, b.id}] = row;
		}
		return out;
	}

	// The last step of one history put back, kept as an event, and what puts it back
	// again onto the other.
	void local_source::take_back(std::vector<history_step>& from, std::vector<history_step>& onto, const std::string& name,
	                             std::function<void(const screens::answer&)> answered) {
		if (from.empty()) {
			answered(std::unexpected("There's nothing to " + name + "."));
			return;
		}
		history_step taken = std::move(from.back());
		from.pop_back();
		auto changes = changes_for(taken.back);
		auto back = back_from(changes);
		event e{.command = "one::" + name, .at = now(), .changes = std::move(changes)};
		if (auto kept = folder_.append(e); !kept) {
			from.push_back(std::move(taken));
			answered(std::unexpected(kept.error()));
			return;
		}
		store_.apply(e);
		onto.push_back({taken.command, std::move(back)});
		tell_all();
		answered(taken.command);
	}

	void local_source::tell_all() {
		// A listener may stop listening, or another start, as it's told.
		std::vector<int> listening;
		for (const auto& [id, _] : listeners_) listening.push_back(id);
		for (int id : listening)
			if (auto l = listeners_.find(id); l != listeners_.end()) tell(l->second);
	}

	void local_source::run(const std::string& command_name, const value& fields, const screens::attached_files& files, std::function<void(const screens::answer&)> answered) {
		value sent = fields;
		for (const auto& [field, written] : files) {
			std::vector<std::string> kept;
			for (const auto& w : written) {
				std::string at = screens::local_path(w);
				if (!std::filesystem::is_regular_file(at)) {
					answered(std::unexpected(w + " isn't a file"));
					return;
				}
				auto name = folder_.keep(at);
				if (!name) {
					answered(std::unexpected(name.error()));
					return;
				}
				kept.push_back(*name);
			}
			sent = screens::with_uploaded(std::move(sent), field, kept);
		}
		run(command_name, sent, std::move(answered));
	}

}  // namespace one::local
