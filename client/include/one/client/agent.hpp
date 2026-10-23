// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// An agent: what a define agent is written as, working, as a role for services, the
// stages it names by itself. It listens to each workflow's queue for its role, takes
// what arrives in a stage it works, runs the stage's program on it, keeping its lease
// while it runs, and moves it on: to then when the program succeeds, to else when it
// fails, or lets it go when there's no else.
//
//     one::client::agent press{.name = "press", .rules = {{.queue = "shop::order_work", .entity = "shop::order",
//                                                          .stage = "printing", .program = "tools/print {order.id}",
//                                                          .then = "printed", .otherwise = "to_do"}}};
//     return one::client::work(app, loop, press);
//
// The program runs from the folder the agent was started in, through /bin/sh, with
// what's in braces filled in from the record, each quoted for the shell. It's given
// the record as JSON on its input, a folder of its own as WORK_DIR, and the record's
// id as ONE_RECORD; what it writes goes to a log of its own, whose end is said when
// it fails.

#pragma once

#include <ember.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "one/client/client.hpp"

namespace one::client {

	struct agent_rule {
		std::string queue;      // the workflow's queue for a role, like shop::order_work
		std::string entity;     // what moves by it, like shop::order, whose claim, renew, release and move it runs
		std::string along = "stage";  // the field it moves along
		std::string stage;      // the stage it works, like printing
		std::string program;    // what it runs, with {order.id} or {id} filled in
		std::string then;       // where it goes when the program succeeds
		std::string otherwise;  // where it goes when it fails; empty lets it go
		int lease_minutes = 10;  // how long the workflow's claim lasts, renewed at half of it
	};

	struct agent {
		std::string name;
		std::vector<agent_rule> rules{};
		std::size_t at_once = 4;  // how many it works at a time, unless ONE_AGENT_PARALLEL says
		std::filesystem::path logs{};  // where each run's output is kept; ~/.local/state/<name> unless said
	};

	// A program as it's run on a record: each {field}, or {entity.field}, filled in
	// from the record's fields, quoted for the shell, so nothing a person typed runs.
	std::string filled(const std::string& program, const std::string& entity, const ember::value& record);

	// Quoted for /bin/sh: 'it'\''s'.
	std::string quoted(const std::string& text);

	// Works until it's stopped, by SIGINT or SIGTERM, or its sign-in ends: 0 when it
	// was stopped, 1 when it couldn't go on.
	int work(session& app, ember::queue_executor& loop, const agent& what);

}  // namespace one::client
