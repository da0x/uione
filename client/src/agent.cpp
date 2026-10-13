// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#include "one/client/agent.hpp"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <map>
#include <print>
#include <set>

extern char** environ;

namespace one::client {

	namespace {

		volatile std::sig_atomic_t stopping = 0;

		std::string setting(const char* name) {
			const char* given = std::getenv(name);
			return given ? given : "";
		}

		// A field of a record as text: a list's values between spaces.
		std::string said(const ember::value& v) {
			if (v.is_string()) return v.as_string();
			if (v.is_bool()) return v.as_bool() ? "true" : "false";
			if (v.is_integer()) return std::to_string(v.as_integer());
			if (v.is_double()) return std::format("{}", v.as_double());
			if (v.is_array()) {
				std::string out;
				for (const auto& item : v.as_array()) out += (out.empty() ? "" : " ") + said(item);
				return out;
			}
			return "";
		}

		// What a record is called in what the agent says: its title or name, and its id.
		std::string called(const ember::value& record) {
			std::string id = text(record["id"]);
			for (const char* name : {"title", "name"}) {
				if (std::string named = text(record[name]); !named.empty()) return named + " (" + id + ")";
			}
			return id;
		}

		// The last lines of a log, to say why a program failed.
		std::string end_of(const std::filesystem::path& log, std::size_t lines = 20) {
			std::ifstream in(log);
			std::vector<std::string> kept;
			for (std::string line; std::getline(in, line);) {
				kept.push_back(line);
				if (kept.size() > lines) kept.erase(kept.begin());
			}
			std::string out;
			for (const auto& line : kept) out += "    " + line + "\n";
			return out;
		}

		// A record being worked: its program running, and when its lease was last kept.
		struct running {
			const agent_rule* rule;
			ember::value record;
			pid_t child;
			std::filesystem::path folder, log;
			std::chrono::steady_clock::time_point renewed;
		};

	}  // namespace

	std::string quoted(const std::string& text) {
		std::string out = "'";
		for (char c : text) {
			if (c == '\'') out += "'\\''";
			else out += c;
		}
		return out + "'";
	}

	std::string filled(const std::string& program, const std::string& entity, const ember::value& record) {
		std::string bare = entity.substr(entity.rfind(':') == std::string::npos ? 0 : entity.rfind(':') + 1);
		std::string out;
		for (std::size_t at = 0; at < program.size();) {
			std::size_t open = program.find('{', at);
			std::size_t close = open == std::string::npos ? std::string::npos : program.find('}', open);
			if (close == std::string::npos) {
				out += program.substr(at);
				break;
			}
			out += program.substr(at, open - at);
			std::string name = program.substr(open + 1, close - open - 1);
			if (name.starts_with(bare + ".")) name = name.substr(bare.size() + 1);
			out += quoted(said(record[name]));
			at = close + 1;
		}
		return out;
	}

	int work(session& app, ember::queue_executor& loop, const agent& what) {
		std::size_t at_once = what.at_once;
		if (std::string said_at_once = setting("ONE_AGENT_PARALLEL"); !said_at_once.empty()) at_once = std::max(1, std::atoi(said_at_once.c_str()));
		std::filesystem::path logs = what.logs;
		if (logs.empty()) {
			std::string state = setting("XDG_STATE_HOME");
			logs = (state.empty() ? std::filesystem::path(setting("HOME")) / ".local" / "state" : std::filesystem::path(state)) / what.name;
		}
		std::filesystem::create_directories(logs);
		auto me = app.me();
		std::string uid = app.uid();

		// The latest of each queue the rules read, for the role the agent works as.
		std::map<std::string, std::vector<ember::value>> queues;
		std::vector<ember::database::registration> listening;
		int ended = 0;
		bool changed = true;
		std::set<std::string> asked;
		for (const auto& rule : what.rules) {
			if (!asked.insert(rule.queue).second) continue;
			listening.push_back(app.listen(
				rule.queue, me.role,
				[&, queue = rule.queue](const ember::snapshot& s) {
					queues[queue] = s.data.is_map() && s.data["items"].is_array() ? s.data["items"].as_array() : std::vector<ember::value>{};
					changed = true;
				},
				[&](const ember::error& e) {
					std::println(stderr, "{}: {}", what.name, e.message);
					ended = 1;
					stopping = 1;
				}));
		}
		std::signal(SIGINT, [](int) { stopping = 1; });
		std::signal(SIGTERM, [](int) { stopping = 1; });
		std::println(stderr, "{} is working as {}; waiting for what arrives.", what.name, me.name);

		std::map<std::string, running> working;  // by record id
		std::set<std::string> tried;  // taken by someone else as it was asked for; asked again once a queue changes
		auto run = [&](const std::string& command, const std::map<std::string, ember::value>& fields) { app.run(command, ember::value(fields)); };

		// Runs the stage's program on a record it holds, in a folder of its own.
		auto start = [&](const agent_rule& rule, const ember::value& record) {
			std::string id = text(record["id"]);
			std::string root = (setting("TMPDIR").empty() ? std::string("/tmp") : setting("TMPDIR")) + "/" + what.name + "-XXXXXX";
			if (!::mkdtemp(root.data())) throw refused(0, "couldn't make a folder to work in");
			std::filesystem::path log = logs / (id + "-" + rule.stage + ".log");
			int input[2];
			if (::pipe(input) != 0) throw refused(0, "couldn't start " + rule.program);
			posix_spawn_file_actions_t files;
			posix_spawn_file_actions_init(&files);
			posix_spawn_file_actions_adddup2(&files, input[0], STDIN_FILENO);
			posix_spawn_file_actions_addclose(&files, input[1]);
			posix_spawn_file_actions_addopen(&files, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
			posix_spawn_file_actions_adddup2(&files, STDOUT_FILENO, STDERR_FILENO);
			::setenv("WORK_DIR", root.c_str(), 1);
			::setenv("ONE_RECORD", id.c_str(), 1);
			std::string script = filled(rule.program, rule.entity, record);
			char sh[] = "/bin/sh", c[] = "-c";
			char* argv[] = {sh, c, script.data(), nullptr};
			pid_t child = 0;
			int started = ::posix_spawn(&child, "/bin/sh", &files, nullptr, argv, environ);
			posix_spawn_file_actions_destroy(&files);
			::close(input[0]);
			if (started != 0) {
				::close(input[1]);
				throw refused(0, "couldn't start " + rule.program);
			}
			std::string given = ember::to_json(record) + "\n";
			(void)!::write(input[1], given.data(), given.size());
			::close(input[1]);
			std::println(stderr, "{} in {}: running {}", called(record), rule.stage, script);
			working[id] = {&rule, record, child, root, log, std::chrono::steady_clock::now()};
		};

		// Moves a record on once its program has ended: to then, to else, or lets it go.
		auto finish = [&](const std::string& id, running& r, int status) {
			bool fine = WIFEXITED(status) && WEXITSTATUS(status) == 0;
			const agent_rule& rule = *r.rule;
			try {
				if (fine) {
					run(rule.entity + "::move", {{"id", id}, {rule.along, rule.then}});
					std::println(stderr, "{}: done, moved to {}", called(r.record), rule.then);
				} else {
					std::string why = WIFEXITED(status) ? "exited " + std::to_string(WEXITSTATUS(status)) : "was stopped";
					std::println(stderr, "{}: {} {}, its log {}:\n{}", called(r.record), rule.program, why, r.log.string(), end_of(r.log));
					if (!rule.otherwise.empty()) {
						run(rule.entity + "::move", {{"id", id}, {rule.along, rule.otherwise}});
						std::println(stderr, "{}: moved to {}", called(r.record), rule.otherwise);
					} else {
						run(rule.entity + "::release", {{"id", id}});
					}
				}
			} catch (const refused& e) {
				std::println(stderr, "{}: {}", called(r.record), e.what());
				try {
					run(rule.entity + "::release", {{"id", id}});
				} catch (const refused&) {
				}
			}
			std::error_code ignored;
			std::filesystem::remove_all(r.folder, ignored);
		};

		while (!stopping) {
			loop.run_one_for(std::chrono::milliseconds(200));
			// What's ended is moved on; what's still running keeps its lease.
			for (auto it = working.begin(); it != working.end();) {
				int status = 0;
				if (::waitpid(it->second.child, &status, WNOHANG) == it->second.child) {
					finish(it->first, it->second, status);
					it = working.erase(it);
					continue;
				}
				// Kept at half its lease, in seconds, since a minute's half is no whole minute.
				auto half = std::chrono::seconds(it->second.rule->lease_minutes * 30);
				if (std::chrono::steady_clock::now() - it->second.renewed > half) {
					try {
						run(it->second.rule->entity + "::renew", {{"id", it->first}});
						it->second.renewed = std::chrono::steady_clock::now();
					} catch (const refused& e) {
						// Not said no to, only not answered now: asked again in a few seconds.
						if (e.status == 0 || e.status == 429 || e.status >= 500) {
							std::println(stderr, "{}: couldn't keep it just now: {}", called(it->second.record), e.what());
							it->second.renewed = std::chrono::steady_clock::now() - half + std::chrono::seconds(5);
							++it;
							continue;
						}
						// Someone else has it now: what this run makes is no longer wanted.
						std::println(stderr, "{}: lost it: {}", called(it->second.record), e.what());
						::kill(it->second.child, SIGTERM);
						::waitpid(it->second.child, nullptr, 0);
						it = working.erase(it);
						continue;
					}
				}
				++it;
			}
			if (changed) tried.clear(), changed = false;
			// What's arrived in a stage it works, free or held past its lease, is taken.
			for (const auto& rule : what.rules) {
				for (const auto& record : queues[rule.queue]) {
					if (working.size() >= at_once) break;
					std::string id = text(record["id"]);
					if (id.empty() || working.contains(id) || tried.contains(id) || text(record[rule.along]) != rule.stage) continue;
					std::string holder = text(record["held_by"]);
					bool lapsed = record["held_until"].is_time() && when(record["held_until"]) < std::chrono::system_clock::now();
					if (!holder.empty() && holder != uid && !lapsed) continue;
					try {
						if (holder != uid) run(rule.entity + "::claim", {{"id", id}});
						start(rule, record);
					} catch (const refused& e) {
						if (e.status == 0) {
							std::println(stderr, "{}: {}", called(record), e.what());
							ended = 1;
							stopping = 1;
							break;
						}
						tried.insert(id);
					}
				}
			}
		}
		// Stopped: what's running is stopped too, and let go for another to take.
		for (auto& [id, r] : working) {
			::kill(r.child, SIGTERM);
			::waitpid(r.child, nullptr, 0);
			try {
				run(r.rule->entity + "::release", {{"id", id}});
			} catch (const refused&) {
			}
			std::error_code ignored;
			std::filesystem::remove_all(r.folder, ignored);
		}
		return ended;
	}

}  // namespace one::client
