// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/screens/source.hpp"

#include <filesystem>

namespace one::screens {

	void memory_source::set(const std::string& view, const std::string& subject, value data) {
		std::string at = key(view, subject);
		views_[at] = {status::ready, std::move(data), ""};
		tell(at);
	}

	void memory_source::remove(const std::string& view, const std::string& subject) {
		std::string at = key(view, subject);
		views_[at] = {status::missing, value::map(), ""};
		tell(at);
	}

	void memory_source::tell(const std::string& at) {
		auto found = listeners_.find(at);
		if (found == listeners_.end()) return;
		auto told = found->second;  // a listener may stop listening as it's told
		for (auto& [_, listener] : told) listener(views_[at]);
	}

	listening memory_source::listen(const std::string& view, const std::string& subject, std::function<void(const view_state&)> told) {
		std::string at = key(view, subject);
		int id = next_++;
		listeners_[at][id] = told;
		// What's there now, or that there's nothing.
		auto known = views_.find(at);
		told(known != views_.end() ? known->second : view_state{status::missing, value::map(), ""});
		std::weak_ptr<bool> alive = alive_;
		return listening([this, alive, at, id] {
			if (alive.lock()) listeners_[at].erase(id);
		});
	}

	void memory_source::run(const std::string& command_name, const value& fields, std::function<void(const answer&)> answered) {
		runs_.push_back({command_name, fields});
		auto found = commands_.find(command_name);
		answered(found == commands_.end() ? answer(std::string()) : found->second(fields, *this));
	}

	void memory_source::run(const std::string& command_name, const value& fields, const attached_files& files, std::function<void(const answer&)> answered) {
		value sent = fields;
		for (const auto& [field, written] : files) {
			std::vector<std::string> uploaded;
			for (const auto& w : written) {
				std::string at = local_path(w);
				if (!std::filesystem::is_regular_file(at)) {
					answered(std::unexpected(w + " isn't a file"));
					return;
				}
				uploads_.push_back(at);
				uploaded.push_back("uploads/" + std::filesystem::path(at).filename().string());
			}
			sent = with_uploaded(std::move(sent), field, uploaded);
		}
		run(command_name, sent, std::move(answered));
	}

}  // namespace one::screens
