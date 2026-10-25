// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/local/local.hpp"

namespace one::local {

	std::string to_line(const event& e) {
		std::vector<value> changes;
		for (const auto& c : e.changes) {
			value written = value::map();
			written.fields["entity"] = c.entity;
			written.fields["id"] = c.id;
			if (c.removed) written.fields["removed"] = true;
			else written.fields["fields"] = c.fields;
			changes.push_back(std::move(written));
		}
		value line = value::map();
		line.fields["command"] = e.command;
		line.fields["at"] = e.at;
		line.fields["changes"] = value(std::move(changes));
		return ember::to_json(line);
	}

	std::expected<event, std::string> from_line(std::string_view line) {
		value read;
		try {
			read = ember::parse_json(line);
		} catch (const std::exception& e) {
			return std::unexpected(std::string("it isn't JSON: ") + e.what());
		}
		if (!read.is_map() || !read["command"].is_string() || !read["changes"].is_array()) return std::unexpected(std::string("it isn't an event"));
		event e{.command = read["command"].as_string(), .at = read["at"].as_string(), .changes = {}};
		for (const auto& c : read["changes"].as_array()) {
			if (!c["entity"].is_string() || !c["id"].is_string()) return std::unexpected(std::string("a change in it names no entity"));
			change made{.entity = c["entity"].as_string(), .id = c["id"].as_string()};
			made.removed = c["removed"].as_bool();
			if (!made.removed) {
				if (!c["fields"].is_map()) return std::unexpected("its change to " + made.entity + " " + made.id + " has no fields");
				made.fields = c["fields"];
			}
			e.changes.push_back(std::move(made));
		}
		return e;
	}

}  // namespace one::local
