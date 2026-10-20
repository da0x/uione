// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// at most: how often each person or service may run a command, how many of a kind
// they may make in a day, and, in the project block, what every command and entity
// allows unless it says its own. Every site is limited without it; this changes how.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "generators/api.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    const std::string desk = R"(import one

project desk {
	at most 600 per minute
	at most 5000 per day
}

namespace desk {
	entity ticket {
		at most 10 per day
		title  text  required
	}
	command ticket::create {
		by anyone signed in
		at most 30 per hour
	}
	command ticket::update {
		by anyone signed in
	}
}
)";

    std::vector<language::file> checked(const std::string& source, language::diagnostics& out) {
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", source, out));
        language::check(files, out);
        return files;
    }

    std::string messages(const language::diagnostics& found) {
        std::string out;
        for (const auto& d : found) out += d.message + "\n";
        return out;
    }

    const generators::output_file* named(const generators::generated_api& g, std::string_view path) {
        auto found = std::find_if(g.files.begin(), g.files.end(), [&](const auto& f) { return f.path == path; });
        return found == g.files.end() ? nullptr : &*found;
    }

} // namespace

TEST_CASE("at most on a command, an entity and the project becomes the runtime's limits") {
    language::diagnostics out;
    auto files = checked(desk, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));

    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto* go = named(generated, "desk/desk.go");
    REQUIRE(go != nullptr);
    CAPTURE(go->content);
    CHECK(go->content.find(R"(one.Command[Ticket]("ticket::create").Allow(one.SignedIn).AtMost(30, time.Hour))") != std::string::npos);
    CHECK(go->content.find(R"(one.Command[Ticket]("ticket::update").Allow(one.SignedIn))" "\n") != std::string::npos);
    CHECK(go->content.find("var AtMostADayOfTicket = one.AtMostADayOf[Ticket](10)\n") != std::string::npos);
    CHECK(go->content.find(", AtMostADayOfTicket)") != std::string::npos);
    CHECK(go->content.find("\t\"time\"\n") != std::string::npos);

    auto* main = named(generated, "main.go");
    REQUIRE(main != nullptr);
    CAPTURE(main->content);
    CHECK(main->content.find("one.Serve(desk.Module,\n\t\tone.AtMost(600, time.Minute),\n\t\tone.AtMostADay(5000),\n\t)") != std::string::npos);
    CHECK(main->content.find("\t\"time\"\n") != std::string::npos);
}

TEST_CASE("a project that says no at most serves its modules as before") {
    std::string source = desk;
    source.erase(source.find("\tat most 600 per minute\n"), std::string("\tat most 600 per minute\n\tat most 5000 per day\n").size());
    language::diagnostics out;
    auto files = checked(source, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    auto* main = named(generated, "main.go");
    REQUIRE(main != nullptr);
    CHECK(main->content.find("one.Serve(desk.Module)\n") != std::string::npos);
    CHECK(main->content.find("\"time\"") == std::string::npos);
}

TEST_CASE("at most counts from 1, a command's is in seconds to hours, an entity's in days, and a project says each once") {
    auto wrong = [](std::string from, std::string to) {
        std::string source = desk;
        source.replace(source.find(from), from.size(), to);
        language::diagnostics out;
        checked(source, out);
        return messages(out);
    };
    CHECK(wrong("at most 30 per hour", "at most 30 per day")
              .find("a command's at most is per second, minute or hour; how many of a kind each person may make in a day is said on the entity, like at most 10 per day") !=
          std::string::npos);
    CHECK(wrong("at most 30 per hour", "at most 30 per fortnight").find("'fortnight' isn't a time this takes; say per second, minute or hour, like at most 30 per hour") !=
          std::string::npos);
    CHECK(wrong("at most 30 per hour", "at most 0 per hour").find("at most is a count from 1 to 1000000") != std::string::npos);
    CHECK(wrong("at most 10 per day", "at most 10 per hour")
              .find("on an entity, at most is how many of it each person may make in a day, like at most 10 per day; how often a command may be run is said on the command") !=
          std::string::npos);
    CHECK(wrong("\tat most 5000 per day\n", "\tat most 5000 per day\n\tat most 9 per day\n").find("a project says once how many of each kind someone may make in a day") !=
          std::string::npos);
    CHECK(wrong("\tat most 600 per minute\n", "\tat most 600 per minute\n\tat most 9 per second\n").find("a project says once how often someone may run each command") !=
          std::string::npos);
}
