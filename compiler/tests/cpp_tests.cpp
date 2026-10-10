// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// An app's C++ client, cpp/<app>.hpp: what it holds, that it compiles, and that it
// makes the ids the app makes.

#include <doctest/doctest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "generators/cpp.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

using namespace one;

namespace {

    const std::string root = UIONE_ROOT;

    const std::string crew = R"(namespace work {
enum priority {
	high
	low
}
entity crew {
	slug  slug  required  unique  key
}
define role captain "Captain" in crew {
	member::create
	job::create
	job::move
	stage::delete
}
entity stage {
	crew  crew  required  key
	name  text  required  key = slug(title)
	title  text  required
	worked_by  list of role
}
entity job {
	crew      crew  required  key
	number    serial  per crew  key
	title     text  required
	stage     stage
	priority  priority  required = priority::low
	takers    list of user
	due       date
	done      boolean
}
command crew::create {
	by anyone signed in
	dispatch member::create {
		crew = id  person = me  role = role::captain
	}
}
command member::create
command job::create
command job::move {
	changes stage
}
command stage::delete {
	input into stage
}
view crew_page per crew {
	readers member
	slug = crew.slug
	jobs = each job in crew {
		number  title  stage.title  priority  takers.name  due  done
	}
}
}
)";

    generators::output_file generated(const std::string& source) {
        language::diagnostics out;
        std::vector<language::file> files;
        files.push_back(language::parse("main.one", source, out));
        language::check(files, out);
        for (const auto& d : out) FAIL_CHECK(language::format(d));
        return generators::generate_cpp(files, root + "/examples/tasks");
    }

    bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

    std::string without_license(const std::string& text) {
        if (!text.starts_with("// Copyright")) return text;
        auto end = text.find("\n\n");
        return end == std::string::npos ? text : text.substr(end + 2);
    }

} // namespace

TEST_CASE("an app's C++ client has its records, their ids, its commands and its views") {
    auto header = generated(crew);
    CHECK(header.path == "cpp/tasks.hpp");  // without a project block, its folder's name
    const auto& h = header.content;
    // Records, typed as the language says, an enum in full.
    CHECK(has(h, "namespace tasks {"));
    CHECK(has(h, "namespace work {"));
    CHECK(has(h, "enum class priority { high, low };"));
    CHECK(has(h, "struct job {"));
    CHECK(has(h, "\tdouble number{};"));
    CHECK(has(h, "\t::tasks::work::priority priority{};"));
    CHECK(has(h, "\tstd::vector<std::string> takers{};"));
    CHECK(has(h, "\ttime due{};"));
    CHECK(has(h, "\tbool done{};"));
    // An id from its keys.
    CHECK(has(h, "inline std::string job_id(const std::string& crew, double number) { return key({key_part(crew), key_part(number)}); }"));
    // A command takes what it's sent, and what it takes besides.
    CHECK(has(h, "struct job_move {"));
    CHECK(has(h, "std::optional<std::string> into{};"));
    CHECK(has(h, "std::string job_move(const work::job_move& sent) {"));
    CHECK(has(h, "post(\"/api/work/job/move\", sent.json())"));
    // A view, its list's columns typed through what they point at.
    CHECK(has(h, "struct crew_page {"));
    CHECK(has(h, "std::string slug{};"));
    CHECK(has(h, "struct jobs_row {"));
    CHECK(has(h, "std::string stage_title{};"));
    CHECK(has(h, "std::vector<std::string> takers_name{};"));
    CHECK(has(h, "static std::string path(const std::string& of) { return \"views/work::crew_page:\" + of; }"));
    CHECK(has(h, "live<work::crew_page> crew_page(const std::string& crew) {"));
    // Each line says where it came from.
    REQUIRE(header.sources.size() > 10);
}

TEST_CASE("an app's C++ client compiles, and makes the ids the app makes") {
    namespace fs = std::filesystem;
    if (std::system("g++ --version > /dev/null 2>&1") != 0) {
        MESSAGE("no g++ here, so the client isn't compiled");
        return;
    }
    fs::path dir = fs::temp_directory_path() / "uione-cpp-client";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto header = generated(crew);
    std::ofstream(dir / "tasks.hpp") << header.content;
    // libember itself when EMBER_INCLUDE says where it is, or what it's agreed to declare.
    const char* ember = std::getenv("EMBER_INCLUDE");
    std::string libraries;
    if (ember && *ember) {
        fs::copy_file(fs::path(ember) / "ember.hpp", dir / "ember.hpp");
        libraries = " $(pkg-config --cflags --libs botan-3)";
    } else {
        fs::copy_file(fs::path(root) / "compiler" / "tests" / "ember_contract.hpp", dir / "ember.hpp");
    }
    // The ids one/record.go's Key makes, for the same parts.
    std::ofstream(dir / "keys.cpp") << R"(#include "tasks.hpp"
#include <cstdio>
int main() {
	using tasks::key;
	const char* made[] = {
		"neotrac-42", "neotrac-main-to_do", "engine-x%2D1", "x-y-z",
		"a%20b-c%2Fd", "%C3%BC-%C3%A9@:+$&=~._", "p-q%3Br%2Cs%3Ft", "crew-uid123-crew%2Dcaptain",
	};
	std::string ours[] = {
		key({"neotrac", "42"}), key({"neotrac", "main", "to_do"}), key({"engine", "x-1"}), key({"x-y", "z"}),
		key({"a b", "c/d"}), key({"\xC3\xBC", "\xC3\xA9@:+$&=~._"}), key({"p", "q;r,s?t"}), key({"crew", "uid123", "crew-captain"}),
	};
	int wrong = 0;
	for (int i = 0; i < 8; ++i) {
		if (ours[i] != made[i]) std::printf("%s, not %s\n", ours[i].c_str(), made[i]), ++wrong;
	}
	if (tasks::work::job_id("ark", 3) != "ark-3") std::printf("job_id\n"), ++wrong;
	return wrong;
}
)";
    std::string build = "g++ -std=c++23 -Wall -Wextra -Werror -I" + dir.string() + " " + (dir / "keys.cpp").string() + " -o " + (dir / "keys").string() + libraries +
                        " > " + (dir / "build.log").string() + " 2>&1";
    int built = std::system(build.c_str());
    if (built != 0) MESSAGE(platform::read_file((dir / "build.log").string()).value_or(""));
    REQUIRE(built == 0);
    CHECK(std::system((dir / "keys").string().c_str()) == 0);
    fs::remove_all(dir);
}

TEST_CASE("the site's C++ client is generated exactly as its target says") {
    language::diagnostics out;
    std::vector<language::file> files;
    for (const auto& path : platform::find_one_files({root + "/site"})) files.push_back(language::parse(path, *platform::read_file(path), out));
    language::check(files, out);
    for (const auto& d : out) FAIL_CHECK(language::format(d));
    auto target = platform::read_file(root + "/site/target/cpp/uione.hpp");
    REQUIRE(target);
    auto header = generators::generate_cpp(files, root + "/site");
    CHECK(header.path == "cpp/uione.hpp");
    CHECK(header.content == without_license(*target));
}
