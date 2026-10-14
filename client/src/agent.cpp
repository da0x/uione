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
#include <random>
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
		// Which of the agents working these stages this is, shown on what it works.
		std::string runner = setting("ONE_AGENT_ID");
		if (runner.empty()) {
			std::mt19937 seed{std::random_device{}()};
			runner = std::to_string(std::uniform_int_distribution<int>(1, 999)(seed));
		}
		auto me = app.me();

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
		std::println(stderr, "{} {} is working as {}; waiting for what arrives.", what.name, runner, me.name);

		std::map<std::string, running> working;  // by record id
		std::set<std::string> tried;  // taken by someone else as it was asked for; asked again once a queue changes
		std::map<std::string, std::chrono::steady_clock::time_point> waiting;  // when it asks for each, as it spreads the work
		std::mt19937 generator{std::random_device{}()};
		std::uniform_int_distribution<int> chance(0, 300);
		// A command, asked again when it wasn't answered rather than said no to, like
		// a key asked to slow down: half a second, then twice as long each time, up to
		// eight seconds, what arrives meanwhile still heard. A no is thrown at once.
		auto transient = [](const refused& e) { return e.status == 0 || e.status == 429 || e.status >= 500; };
		auto run = [&](const std::string& command, const std::map<std::string, ember::value>& fields) {
			for (int attempt = 0;; ++attempt) {
				try {
					app.run(command, ember::value(fields));
					return;
				} catch (const refused& e) {
					if (!transient(e) || attempt >= 10 || stopping) throw;
					auto until = std::chrono::steady_clock::now() + std::min(std::chrono::milliseconds(500) * (1 << std::min(attempt, 4)), std::chrono::milliseconds(8000));
					while (std::chrono::steady_clock::now() < until && !stopping) loop.run_one_for(std::chrono::milliseconds(50));
				}
			}
		};
		// What it says of what it holds, for a board to show: its status, since when,
		// and which runner; a null takes one out.
		auto report = [&](const agent_rule& rule, const std::string& id, std::map<std::string, ember::value> data) {
			try {
				run(rule.entity + "::report", {{"id", id}, {"data", ember::value(std::move(data))}});
			} catch (const refused& e) {
				std::println(stderr, "{}: couldn't say how it's going: {}", id, e.what());
			}
		};
		auto now_said = [] { return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now())); };

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
			report(rule, id, {{"status", "working"}, {"since", now_said()}, {"runner", runner}});
		};

		// Moves a record on once its program has ended: to then, to else, or lets it go.
		auto finish = [&](const std::string& id, running& r, int status) {
			bool fine = WIFEXITED(status) && WEXITSTATUS(status) == 0;
			const agent_rule& rule = *r.rule;
			try {
				if (fine) {
					report(rule, id, {{"status", ember::value()}, {"since", ember::value()}, {"runner", ember::value()}});
					run(rule.entity + "::move", {{"id", id}, {rule.along, rule.then}});
					std::println(stderr, "{}: done, moved to {}", called(r.record), rule.then);
				} else {
					std::string why = WIFEXITED(status) ? "exited " + std::to_string(WEXITSTATUS(status)) : "was stopped";
					std::println(stderr, "{}: {} {}, its log {}:\n{}", called(r.record), rule.program, why, r.log.string(), end_of(r.log));
					report(rule, id, {{"status", "failed"}, {"since", now_said()}, {"runner", ember::value()}, {"note", rule.program + " " + why}});
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
			// What's arrived in a stage it works, free or held past its lease, is taken,
			// after a wait that grows with how busy this agent is, and a little chance:
			// where several agents work the same stages, the least busy one usually asks
			// first. The lease still decides; the wait only spreads the work.
			auto now = std::chrono::steady_clock::now();
			std::set<std::string> eligible;
			for (const auto& rule : what.rules) {
				// A copy: a command asked again pumps the loop, and a queue heard meanwhile
				// replaces the one read here.
				const std::vector<ember::value> arrived = queues[rule.queue];
				for (const auto& record : arrived) {
					if (working.size() >= at_once) break;
					std::string id = text(record["id"]);
					if (id.empty() || working.contains(id) || tried.contains(id) || text(record[rule.along]) != rule.stage) continue;
					std::string holder = text(record["held_by"]);
					bool lapsed = record["held_until"].is_time() && when(record["held_until"]) < std::chrono::system_clock::now();
					// Held under this agent's own key but not run here is another agent's,
					// started with the same key, until its lease runs out.
					if (!holder.empty() && !lapsed) continue;
					eligible.insert(id);
					{
						auto [due, fresh] = waiting.try_emplace(id, now);
						if (fresh) due->second = now + std::chrono::milliseconds(working.size() * 1500 / at_once + chance(generator));
						if (now < due->second) continue;
					}
					try {
						try {
							app.run(rule.entity + "::claim", ember::value(std::map<std::string, ember::value>{{"id", id}}));
						} catch (const refused& e) {
							// Not answered now: asked again in a while, not counted as taken.
							if (!transient(e)) throw;
							waiting[id] = now + std::chrono::milliseconds(1000 + chance(generator) * 5);
							continue;
						}
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
			std::erase_if(waiting, [&](const auto& w) { return !eligible.contains(w.first); });
		}
		// Stopped: what's running is stopped too, and let go for another to take.
		for (auto& [id, r] : working) {
			::kill(r.child, SIGTERM);
			::waitpid(r.child, nullptr, 0);
			report(*r.rule, id, {{"status", ember::value()}, {"since", ember::value()}, {"runner", ember::value()}});
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
