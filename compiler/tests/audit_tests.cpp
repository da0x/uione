// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// An audit of a place: audit kept 1 year on the entity whose roles define role keeps
// declares the audit records, which the runtime writes with every command, read in a
// view only its owners read, and downloaded as comma-separated values.

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "generators/api.hpp"
#include "generators/indexes.hpp"
#include "generators/native.hpp"
#include "generators/web.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    const std::string club = R"(namespace clubs {
	entity club {
		readers member
		slug  text  required  unique  key
		audit kept 1 year
	}
	command club::update
	define role chair "Chair" in club {
		club::update
	}
	define role auditor "Auditor" in club {
	}
	view club_audit per club {
		readers chair
		readers auditor
		entries = each audit in club {
			order by created_at descending
			limit 200
			command  person  person.name  address  kind  item  changed  created_at
		}
	}
	screen "Audit" /clubs/:club/audit {
		table club_audit.entries {
			download "audit.csv"
			command "Command"
			person.name "Who"
			created_at "When"
		}
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

} // namespace

TEST_CASE("a place that keeps an audit has its records, read by the roles a view names and downloaded from a table") {
    language::diagnostics out;
    auto files = checked(club, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));

    auto generated = generators::generate_api(files, root + "/examples/tasks", root + "/examples/tasks/build/api");
    REQUIRE(generated.errors.empty());
    auto go = std::find_if(generated.files.begin(), generated.files.end(), [](const auto& f) { return f.path == "clubs/clubs.go"; });
    REQUIRE(go != generated.files.end());
    CAPTURE(go->content);
    // The records, kept a year, and the view kept to the chairs and auditors.
    CHECK(go->content.find("type Audit struct") != std::string::npos);
    CHECK(go->content.find("Changes one.Data") != std::string::npos);
    CHECK(go->content.find("\tSuspends(one.Entity[Suspension]()).\n\tAudits(one.Entity[Audit](), one.Days(365))\n") != std::string::npos);
    CHECK(go->content.find(R"(.Readers(one.Entity[Member]()).Holding("chair", "auditor"))") != std::string::npos);

    // An audit's records go once they're older than it keeps them.
    CHECK(generators::indexes_file(files).content.find(R"({ "collection": "clubs_audit", "field": "until" })") != std::string::npos);

    // Its table saves every row it keeps, on the web and in the apps.
    auto web = generators::generate_web(files, root + "/examples/tasks", root + "/examples/tasks/build/web");
    auto screen = std::ranges::find_if(web, [](const auto& f) { return f.content.find("<Table") != std::string::npos && f.content.find("club_audit") != std::string::npos; });
    REQUIRE(screen != web.end());
    CHECK(screen->content.find(R"( download="audit.csv")") != std::string::npos);
    auto native = generators::generate_native(files, root + "/examples/tasks", "terminal");
    auto drawn = std::ranges::find_if(native, [](const auto& f) { return f.content.find("table(p, {") != std::string::npos; });
    REQUIRE(drawn != native.end());
    CHECK(drawn->content.find(R"(.download = "audit.csv")") != std::string::npos);
}

TEST_CASE("an audit is changed by no command, kept for days to years, read by roles and nothing else, and downloaded to a .csv") {
    auto wrong = [](std::string from, std::string to) {
        std::string source = club;
        source.replace(source.find(from), from.size(), to);
        language::diagnostics out;
        checked(source, out);
        return messages(out);
    };
    CHECK(wrong("\tcommand club::update\n", "\tcommand club::update\n\tcommand audit::delete\n")
              .find("an audit is written by the commands it records; no command makes, changes or deletes one") != std::string::npos);
    CHECK(wrong("audit kept 1 year", "audit kept 1 decade").find("an audit is kept days, weeks, months or years, like audit kept 1 year") != std::string::npos);
    CHECK(wrong("\t\treaders auditor\n", "\t\treaders treasurer\n").find("readers treasurer isn't a role in clubs") != std::string::npos);
    // Roles, or one entity: never two.
    CHECK(wrong("\t\treaders chair\n\t\treaders auditor\n", "\t\treaders member\n\t\treaders club\n")
              .find("a view's readers come from one entity, like readers member, or are roles, like readers owner; another readers line names people, like readers report.author") !=
          std::string::npos);
    CHECK(wrong("download \"audit.csv\"", "download \"../audit.csv\"").find("a table downloads to a file named like \"audit.csv\", ending in .csv, with no folder") !=
          std::string::npos);
}
