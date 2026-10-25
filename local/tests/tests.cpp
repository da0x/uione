// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::local's tests: events kept a line each and read back, a command's step seeing
// its own changes before they're kept, a folder held by one copy of an app at a
// time, files kept once by their hash, a crash's cut line left out and cut off, and
// views told live, only when they change, and the same after the app starts again.

#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "check.hpp"
#include "one/local/local.hpp"

namespace {

	using namespace one::local;
	using fields = std::map<std::string, value>;

	// A folder of its own for each test, gone after it.
	struct scratch {
		std::filesystem::path at;
		scratch() {
			static int count = 0;
			at = std::filesystem::temp_directory_path() / ("one-local-" + std::to_string(::getpid()) + "-" + std::to_string(count++));
			std::filesystem::remove_all(at);
		}
		~scratch() { std::filesystem::remove_all(at); }
	};

	std::string text_of(const std::filesystem::path& file) {
		std::ifstream in(file, std::ios::binary);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}

	// A board's layers, as the commands and views a project would have them.
	void define(local_source& s) {
		s.on("layer::create", [](const value& f, step& changes) -> one::screens::answer {
			if (f["board"].as_string().empty()) return std::unexpected(std::string("a layer needs a board"));
			value made = f;
			made.fields["order"] = changes.next_serial("layer", "order", "board", f["board"].as_string());
			return changes.create("layer", made);
		});
		s.on("layer::remove", [](const value& f, step& changes) -> one::screens::answer {
			if (!changes.remove("layer", f["id"].as_string())) return std::unexpected(std::string("there's no such layer"));
			return f["id"].as_string();
		});
		s.define("board_page", [](const store& entities, const std::string& board) -> std::optional<value> {
			std::vector<value> layers;
			for (const value* row : entities.each("layer"))
				if ((*row)["board"].as_string() == board) layers.push_back(value(fields{{"id", (*row)["id"]}, {"order", (*row)["order"]}}));
			if (layers.empty()) return std::nullopt;
			std::ranges::sort(layers, {}, [](const value& l) { return l["order"].as_integer(); });
			return value(fields{{"layers", value(layers)}});
		});
	}

}  // namespace

TEST("an event is a line of JSON, read back as it was") {
	event e{.command = "layer::place", .at = "2026-10-24T09:00:00Z", .changes = {}};
	e.changes.push_back({.entity = "layer", .id = "a", .fields = value(fields{{"x", 12}, {"name", "beach"}})});
	e.changes.push_back({.entity = "layer", .id = "b", .removed = true});
	std::string line = to_line(e);
	CHECK(line.find('\n') == std::string::npos);
	auto read = from_line(line);
	CHECK(read.has_value());
	CHECK(read->command == "layer::place");
	CHECK(read->at == "2026-10-24T09:00:00Z");
	CHECK(read->changes.size() == 2);
	CHECK(read->changes[0].fields["x"].as_integer() == 12);
	CHECK(read->changes[0].fields["name"].as_string() == "beach");
	CHECK(read->changes[1].removed);
	CHECK(!from_line("{\"command\": 3}").has_value());
	CHECK(!from_line("not json").has_value());
}

TEST("a step sees its own changes, and the store none until they're applied") {
	store kept;
	kept.apply({.command = "", .at = "", .changes = {{.entity = "layer", .id = "a", .fields = value(fields{{"order", 1}})}}});
	step changes(kept);
	std::string made = changes.create("layer", value(fields{{"order", 2}}));
	CHECK(made.size() == 20);
	CHECK(changes.update("layer", "a", value(fields{{"x", 5}})));
	CHECK(changes.find("layer", "a")->operator[]("x").as_integer() == 5);
	CHECK(changes.find("layer", "a")->operator[]("order").as_integer() == 1);
	CHECK(changes.each("layer").size() == 2);
	CHECK(!changes.update("layer", "nothing", value(fields{{"x", 1}})));
	CHECK(changes.remove("layer", made));
	CHECK(changes.each("layer").size() == 1);
	CHECK(changes.changes().size() == 2);  // the one made and taken away again is one change
	CHECK(kept.find("layer", "a")->operator[]("x").is_null());
	CHECK(kept.each("layer").size() == 1);
}

TEST("a command can change rows while it goes through them") {
	store kept;
	kept.apply({.command = "", .at = "", .changes = {{.entity = "layer", .id = "a", .fields = value(fields{{"z", 1}})},
	                                                 {.entity = "layer", .id = "b", .fields = value(fields{{"z", 2}})}}});
	step changes(kept);
	for (const value& row : changes.each("layer")) changes.update("layer", row["id"].as_string(), value(fields{{"z", row["z"].as_integer() + 10}}));
	CHECK(changes.find("layer", "a")->operator[]("z").as_integer() == 11);
	CHECK(changes.find("layer", "b")->operator[]("z").as_integer() == 12);
}

TEST("a serial counts on within what it's per") {
	store kept;
	kept.apply({.command = "", .at = "", .changes = {{.entity = "layer", .id = "a", .fields = value(fields{{"board", "x"}, {"order", 4}})},
	                                                 {.entity = "layer", .id = "b", .fields = value(fields{{"board", "y"}, {"order", 9}})}}});
	step changes(kept);
	CHECK(changes.next_serial("layer", "order", "board", "x") == 5);
	CHECK(changes.next_serial("layer", "order", "board", "z") == 1);
	CHECK(changes.next_serial("layer", "order") == 10);
}

TEST("SHA-256 is FIPS 180-4's") {
	std::istringstream empty(""), abc("abc"), long_one(std::string(1'000'000, 'a'));
	CHECK(sha256(empty) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(sha256(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(sha256(long_one) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST("a folder is where XDG says, and held by one copy of the app at a time") {
	scratch s;
	::setenv("XDG_DATA_HOME", s.at.c_str(), 1);
	CHECK(folder::where("mine") == s.at / "mine");
	::setenv("XDG_DATA_HOME", "relative", 1);  // the specification ignores one that isn't absolute
	CHECK(folder::where("mine").string().ends_with("/.local/share/mine"));
	::unsetenv("XDG_DATA_HOME");
	{
		auto first = folder::open_at(s.at);
		CHECK(first.has_value());
		CHECK(std::filesystem::is_directory(s.at / "files"));
		auto second = folder::open_at(s.at);
		CHECK(!second.has_value());
		CHECK_SAYING(second.error().find("another copy") != std::string::npos, second.error());
	}
	CHECK(folder::open_at(s.at).has_value());  // let go when the first closed
}

TEST("events are kept and read back, a cut last line left out, then cut off") {
	scratch s;
	auto kept = folder::open_at(s.at);
	CHECK(kept->read()->empty());
	CHECK(kept->append({.command = "one", .at = "", .changes = {}}).has_value());
	CHECK(kept->append({.command = "two", .at = "", .changes = {}}).has_value());
	{
		std::ofstream torn(s.at / "events.jsonl", std::ios::app | std::ios::binary);
		torn << "{\"command\":\"thr";
	}
	auto read = kept->read();
	CHECK(read.has_value());
	CHECK(read->size() == 2);
	CHECK(kept->append({.command = "four", .at = "", .changes = {}}).has_value());
	read = kept->read();
	CHECK_SAYING(read.has_value(), read ? "" : read.error());
	CHECK(read->size() == 3);
	CHECK(read->back().command == "four");
	CHECK(text_of(s.at / "events.jsonl").find("thr") == std::string::npos);
}

TEST("a damaged line inside the log is an error, never skipped") {
	scratch s;
	auto kept = folder::open_at(s.at);
	{
		std::ofstream log(s.at / "events.jsonl", std::ios::binary);
		log << "{\"command\":\"one\",\"at\":\"\",\"changes\":[]}\nnonsense\n{\"command\":\"two\",\"at\":\"\",\"changes\":[]}\n";
	}
	auto read = kept->read();
	CHECK(!read.has_value());
	CHECK_SAYING(read.error().ends_with("events.jsonl:2: it isn't JSON") || read.error().find("events.jsonl:2:") != std::string::npos, read.error());
}

TEST("a file is kept once, named by its hash") {
	scratch s;
	auto kept = folder::open_at(s.at);
	std::filesystem::path photo = s.at / "photo.png", same = s.at / "copy.png";
	{
		std::ofstream(photo, std::ios::binary) << "abc";
		std::ofstream(same, std::ios::binary) << "abc";
	}
	auto first = kept->keep(photo);
	auto again = kept->keep(same);
	CHECK(first.has_value());
	CHECK(*first == "files/ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(again.has_value() && *again == *first);
	CHECK(text_of(kept->file(*first)) == "abc");
	int in_files = 0;
	for (const auto& entry : std::filesystem::directory_iterator(s.at / "files")) in_files += entry.is_regular_file();
	CHECK(in_files == 1);
	CHECK(!kept->keep(s.at / "nothing.png").has_value());
}

TEST("a view is told live, only when it changes, and the same after starting again") {
	scratch s;
	std::vector<one::screens::view_state> told;
	{
		auto source = local_source::open(*folder::open_at(s.at));
		CHECK(source.has_value());
		define(**source);
		auto listening = (*source)->listen("board_page", "home", [&](const one::screens::view_state& v) { told.push_back(v); });
		CHECK(told.size() == 1);
		CHECK(told.back().status == one::screens::status::missing);

		std::string first;
		(*source)->run("layer::create", value(fields{{"board", "home"}}), [&](const one::screens::answer& a) { first = a.value_or(""); });
		CHECK(!first.empty());
		CHECK(told.size() == 2);
		CHECK(told.back().status == one::screens::status::ready);
		CHECK(told.back().data["layers"].as_array().size() == 1);

		(*source)->run("layer::create", value(fields{{"board", "elsewhere"}}), [](const one::screens::answer&) {});
		CHECK(told.size() == 2);  // nothing it shows changed

		one::screens::answer refused;
		(*source)->run("layer::create", value(fields{}), [&](const one::screens::answer& a) { refused = a; });
		CHECK(!refused.has_value());
		CHECK(refused.error() == "a layer needs a board");
		(*source)->run("layer::create", value(fields{{"board", "home"}}), [](const one::screens::answer&) {});
		CHECK(told.back().data["layers"].as_array()[1]["order"].as_integer() == 2);

		one::screens::answer unknown;
		(*source)->run("layer::nothing", value(fields{}), [&](const one::screens::answer& a) { unknown = a; });
		CHECK(!unknown.has_value());
	}
	CHECK(text_of(s.at / "events.jsonl").find("a layer needs") == std::string::npos);  // a refused command keeps nothing
	auto again = local_source::open(*folder::open_at(s.at));
	CHECK(again.has_value());
	define(**again);
	std::vector<one::screens::view_state> after;
	auto listening = (*again)->listen("board_page", "home", [&](const one::screens::view_state& v) { after.push_back(v); });
	CHECK(after.size() == 1);
	CHECK(ember::to_json(after.back().data) == ember::to_json(told.back().data));
}

TEST("a command's files are kept first, its field then naming them") {
	scratch s;
	std::filesystem::create_directories(s.at);
	std::filesystem::path photo = s.at / "beach.png";
	std::ofstream(photo, std::ios::binary) << "abc";
	auto source = local_source::open(*folder::open_at(s.at / "data"));
	value got;
	(*source)->on("layer::add", [&](const value& f, step& changes) -> one::screens::answer {
		got = f;
		return changes.create("layer", f);
	});
	one::screens::answer a;
	(*source)->run("layer::add", value(fields{}), {{"picture", {photo.string()}}}, [&](const one::screens::answer& answer) { a = answer; });
	CHECK(a.has_value());
	CHECK(got["picture"].as_string() == "files/ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	one::screens::answer missing;
	(*source)->run("layer::add", value(fields{}), {{"picture", {(s.at / "gone.png").string()}}}, [&](const one::screens::answer& answer) { missing = answer; });
	CHECK(!missing.has_value());
}

int main() { return check::run(); }

TEST("files nothing names any more are let go of when the folder opens, and those named stay") {
	scratch s;
	std::filesystem::create_directories(s.at);
	std::ofstream(s.at / "kept.png") << "kept";
	std::ofstream(s.at / "dropped.png") << "dropped";
	std::string kept, dropped;
	{
		auto f = folder::open_at(s.at / "data");
		CHECK(f.has_value());
		if (!f) return;
		kept = *f->keep(s.at / "kept.png");
		dropped = *f->keep(s.at / "dropped.png");
		CHECK(f->append({.command = "make", .at = "", .changes = {{.entity = "picture", .id = "a", .fields = value(std::map<std::string, value>{{"file", kept}})}}}).has_value());
	}
	auto opened = local_source::open(*folder::open_at(s.at / "data"));
	CHECK(opened.has_value());
	CHECK(std::filesystem::exists(s.at / "data" / kept));
	CHECK(!std::filesystem::exists(s.at / "data" / dropped));
}
