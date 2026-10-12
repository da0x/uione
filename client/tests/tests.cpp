// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::client's tests: reading fields and roles, keeping a sign-in, and, through
// tests/run, a person signed in to the tasks example on the emulators, watching their
// list change live as they create a task and complete it, and turned down when they
// complete it twice.

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>

#include "check.hpp"
#include "one/client/client.hpp"

namespace {

	using value = ember::value;
	using fields = std::map<std::string, value>;

	std::string env(const char* name) {
		const char* v = std::getenv(name);
		return v ? v : "";
	}

	// Runs the loop's work until `done` says so, or a few seconds pass.
	template <class F>
	bool until(ember::queue_executor& loop, F done) {
		auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		while (!done()) {
			if (std::chrono::steady_clock::now() > end) return false;
			loop.run_one_for(std::chrono::milliseconds(50));
		}
		return true;
	}

	// A person made in the Auth emulator, new each run: their refresh token.
	std::string signed_up(const std::string& name) {
		auto made = std::chrono::system_clock::now().time_since_epoch().count();
		std::string email = name + "." + std::to_string(made) + "@example.com";
		auto answer = ember::https_post("http://localhost:9099/identitytoolkit.googleapis.com/v1/accounts:signUp?key=local", {{"Content-Type", "application/json"}},
		                                ember::to_json(value(fields{{"email", email}, {"password", "password"}, {"displayName", name}, {"returnSecureToken", true}})));
		std::string token = one::client::text(ember::parse_json(answer.body)["refreshToken"]);
		CHECK_SAYING(!token.empty(), "the Auth emulator answered " + answer.body);
		return token;
	}

}  // namespace

TEST("a field is read by the name a view stores it under, or through its maps") {
	value document(fields{{"title", "Ship it"}, {"stage.title", "Review"}, {"board", value(fields{{"name", "main"}})}, {"rows", value(std::vector<value>{value(fields{{"id", "a"}})})}});
	CHECK(one::client::text(one::client::field(document, "title")) == "Ship it");
	CHECK(one::client::text(one::client::field(document, "stage.title")) == "Review");
	CHECK(one::client::text(one::client::field(document, "board.name")) == "main");
	CHECK(one::client::field(document, "board.missing").is_null());
	CHECK(one::client::rows(document).size() == 1);
}

TEST("roles allow a command within the place a row of them is for") {
	value roles(fields{{"rows", value(std::vector<value>{
	                                 value(fields{{"project", "neotrac"}, {"role", "neotrac-maintainer"}, {"role.may", value(std::vector<value>{"issue::move", "board::create"})}}),
	                                 value(fields{{"project", "other"}, {"role", "other-contributor"}, {"role.may", value(std::vector<value>{"issue::create"})}}),
	                             })}});
	CHECK(one::client::allows(roles, "project", "neotrac", "issue::move"));
	CHECK(!one::client::allows(roles, "project", "neotrac", "issue::create"));  // only in the other project
	CHECK(!one::client::allows(roles, "project", "", "issue::move"));
	CHECK(one::client::holds(roles, "project", "neotrac", {"neotrac-maintainer"}));
	CHECK(!one::client::holds(roles, "project", "other", {"neotrac-maintainer"}));
}

TEST("a sign-in is kept readable by its owner alone, and refused when others can read it") {
	one::client::keep("tasks", {.server = "http://localhost:8097", .refresh_token = "kept"});
	std::string problem;
	auto kept = one::client::read_kept("tasks", problem);
	CHECK_SAYING(kept.has_value(), problem);
	if (kept) CHECK(kept->refresh_token == "kept");
	using std::filesystem::perms;
	std::filesystem::permissions(one::client::kept_at("tasks"), perms::group_read, std::filesystem::perm_options::add);
	CHECK(!one::client::read_kept("tasks", problem).has_value());
	CHECK(problem.find("chmod 600") != std::string::npos);
}

TEST("a person sees their list change live as they create a task and complete it, and is turned down the second time") {
	std::string server = env("ONE_CLIENT_SERVER");
	CHECK_SAYING(!server.empty(), "run through client/tests/run, which starts the tasks example's backend");
	if (server.empty()) return;
	ember::queue_executor loop;
	one::client::session app({.server = server, .project = "demo-uione", .firestore_host = "localhost:8080", .auth_host = "localhost:9099"}, &loop);
	std::string token = signed_up("ada");
	if (token.empty()) return;
	app.sign_in_with_refresh_token(token);
	CHECK(app.signed_in());
	CHECK(app.me().name == "ada");

	value latest;
	bool told = false;
	auto listening = app.listen("tasks::list", app.uid(), [&](const ember::snapshot& s) {
		latest = s.data;
		told = true;
	});
	CHECK(until(loop, [&] { return told; }));

	std::string id = app.run("tasks::task::create", value(fields{{"title", "Write the client"}}));
	CHECK(!id.empty());
	auto row_of = [&]() -> const value* {
		for (const auto& row : one::client::rows(latest))
			if (one::client::text(row["id"]) == id) return &row;
		return nullptr;
	};
	CHECK_SAYING(until(loop, [&] { return row_of() != nullptr; }), "the new task never arrived in the list");
	if (const value* row = row_of()) CHECK(one::client::text(one::client::field(*row, "title")) == "Write the client");

	app.run("tasks::task::complete", value(fields{{"id", id}}));
	CHECK_SAYING(until(loop, [&] { return row_of() && one::client::flag((*row_of())["done"]); }), "the task never showed as done");

	std::string said;
	int status = 0;
	try {
		app.run("tasks::task::complete", value(fields{{"id", id}}));
	} catch (const one::client::refused& e) {
		said = e.what();
		status = e.status;
	}
	CHECK(said == "that task is already done");
	CHECK(status == 400);
}

int main() { return check::run(); }
