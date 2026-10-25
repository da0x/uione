// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

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
		auto found = commands_.find(command_name);
		if (found == commands_.end()) {
			answered(std::unexpected("there's no command " + command_name));
			return;
		}
		step changes(store_);
		screens::answer answer = found->second(fields, changes);
		if (answer && !changes.changes().empty()) {
			event e{.command = command_name, .at = now(), .changes = changes.changes()};
			if (auto kept = folder_.append(e); !kept) {
				answered(std::unexpected(kept.error()));  // nothing changed, since nothing was kept
				return;
			}
			store_.apply(e);
			// A listener may stop listening, or another start, as it's told.
			std::vector<int> listening;
			for (const auto& [id, _] : listeners_) listening.push_back(id);
			for (int id : listening)
				if (auto l = listeners_.find(id); l != listeners_.end()) tell(l->second);
		}
		answered(answer);
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
