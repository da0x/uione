// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A screen laid out as a workspace, like a drawing app's: a bar, a toolbar of named
// groups, the work in main, a side shown while a toggle's on, and a status line with
// its messages; keys on its buttons and links; undo and redo, for a project kept on
// its machine; and full screen.

#include <doctest/doctest.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <filesystem>

#include "driver/build.hpp"
#include "language/checker.hpp"
#include "language/parser.hpp"
#include "platform/files.hpp"

using namespace one::language;

namespace {

    diagnostics checked(const std::string& source) {
        diagnostics out;
        std::vector<file> files;
        files.push_back(parse("main.one", source, out));
        for (const auto& d : out) FAIL_CHECK(format(d));
        out.clear();
        check(files, out);
        return out;
    }

    std::vector<std::string> messages(const std::string& source) {
        std::vector<std::string> said;
        for (const auto& d : checked(source)) said.push_back(std::to_string(d.where.line) + ": " + d.message);
        return said;
    }

    const std::string project = "import one\nproject studio {\n\tone      \"0.18.0\"\n\tstorage  local\n\ttarget   desktop web\n}\n\n";

    const std::string poster = R"(namespace studio at / {
	entity poster {
		title  text  required
	}

	command poster::create

	view posters {
		each poster {
			title
		}
	}
)";

    std::string screen(const std::string& body) { return project + poster + "\n\tscreen \"Studio\" / {\n" + body + "\t}\n}\n"; }

}  // namespace

TEST_CASE("a workspace has a bar, a toolbar of groups, main, a side shown while a toggle's on, and a status line") {
    CHECK(messages(screen(R"(		layout workspace
		bar {
			undo "Undo" icon undo key ctrl+z
			redo "Redo" icon redo key ctrl+y key ctrl+shift+z
			text "Posters"
		}
		toolbar {
			group "Poster" {
				poster::create "New" icon add key ctrl+n
			}
			group "View" small at end {
				toggle list "List" icon layers key l
				full screen "Full screen" icon fullscreen key f11
			}
		}
		main {
			table posters {
				title
			}
		}
		side when list {
			text "The posters, one a row."
		}
		status {
			messages
		}
)")).empty());
}

TEST_CASE("a workspace's regions are its own, and a group goes in its toolbar") {
    CHECK(messages(screen("\t\tlayout workspace\n\t\tmain {\n\t\t\tgroup \"Poster\" {\n\t\t\t\tposter::create \"New\"\n\t\t\t}\n\t\t}\n")) ==
          std::vector<std::string>{"24: a group is part of a workspace's toolbar, so it goes in toolbar { ... }"});
    CHECK(messages(screen("\t\tlayout two_columns\n\t\tbar {\n\t\t\ttext \"Posters\"\n\t\t}\n")) ==
          std::vector<std::string>{"23: layout two_columns has no region bar; its regions are main and side"});
    CHECK(messages(screen("\t\tlayout desk\n")) == std::vector<std::string>{"22: layout is single, two_columns or workspace"});
}

TEST_CASE("a key is any of ctrl, shift and alt, then a key, and a toggle is named once and read by its name") {
    CHECK(messages(screen("\t\tposter::create \"New\" key ctrl+ctrl+n\n\t\tlink / \"Home\" key hyper+h\n\t\tposter::create \"Again\" key ctrl+shift+plus\n")) ==
          std::vector<std::string>{"22: there's no key ctrl+ctrl+n; a key is like ctrl+z, shift+delete, f11 or escape",
                                   "23: there's no key hyper+h; a key is like ctrl+z, shift+delete, f11 or escape"});
    CHECK(messages(screen("\t\ttoggle list \"List\"\n\t\ttoggle list \"Again\"\n\t\ttext \"Shown\" when list\n\t\ttext \"Not\" when grid\n")) ==
          std::vector<std::string>{"23: toggle list is here twice; a screen has one of each",
                                   "25: a button's when compares a view's fields with values, like issue_page.status == status::open"});
}

TEST_CASE("undo and redo take back steps kept on the machine, so a project in the cloud can't say them yet") {
    std::string cloud = "import one\nproject studio {\n\tone  \"0.18.0\"\n}\n\n" + poster + "\n\tscreen \"Studio\" / {\n\t\tundo \"Undo\"\n\t\tredo \"Redo\"\n\t}\n}\n";
    CHECK(messages(cloud) == std::vector<std::string>{"20: undo takes a step back that's kept on this machine, so it needs storage local in the project block, for now",
                                                      "21: redo takes a step back that's kept on this machine, so it needs storage local in the project block, for now"});
}

namespace {

    const std::string layered = R"(namespace studio at / {
	entity poster {
		title  text  required
		tint   enum  ink | paper = tint::paper
	}

	entity layer {
		poster  poster  required
		depth   number  = 0
		size    number  = 10
	}

	command poster::create
	command poster::arrange {
		changes tint
	}
	command layer::scale {
		input by number
		changes size
		size = size * by
	}
	command layer::delete
	command poster::preview {
		apart from undo
	}

	view poster_page per poster {
		title  tint
		layers = each layer in poster {
			order by depth
			depth  size
		}
	}
)";

    std::string layered_screen(const std::string& body) { return project + layered + "\n\tscreen \"Poster\" /:poster {\n" + body + "\t}\n}\n"; }

    // What one build writes for a project, by path.
    std::map<std::string, std::string> built(const std::string& source) {
        auto dir = std::filesystem::temp_directory_path() / "uione-workspace";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        one::platform::write_file((dir / "main.one").string(), source);
        auto b = one::driver::build(dir.string(), (dir / "build").string());
        for (const auto& d : b.problems) FAIL_CHECK(format(d));
        std::map<std::string, std::string> out;
        for (const auto& f : b.files) out[f.path] = f.content;
        std::filesystem::remove_all(dir);
        return out;
    }

}  // namespace

TEST_CASE("a pick is a row of a view's list, read by its name, and a button's given values are its command's fields") {
    CHECK(messages(layered_screen(R"(		pick layer from poster_page.layers
		text "{layer.size}" when layer != none
		text "{layer.color}"
		layer::scale "Twice" { by = layer.size * 2 }
		poster::arrange "Ink" { tint = tint::ink  shade = 1 }
		pick layer from poster_page.layers
		pick other from poster_page.sides
)")) == std::vector<std::string>{"48: layer is picked here twice, or is a toggle too; a screen names each once",
                                   "49: view poster_page has no list called sides",
                                   "45: a picked layer's row has no color; add it to the list's block",
                                   "47: poster::arrange has no field or input shade to be given"});
}

TEST_CASE("a pick is read, picked from a table, and acted on, on the web and the desktop alike") {
    auto files = built(layered_screen(R"(		pick layer from poster_page.layers
		table poster_page.layers {
			depth  size
			empty "Nothing on it yet"
		}
		text "{layer.size}"
		layer::scale "Twice" { by = 2 }
		layer::delete "Delete" icon delete key delete
		poster::arrange "Ink" { tint = tint::ink }
)"));
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    // The row picked, read by the screen, picked from the table, and what's said of it.
    CHECK(web.find(R"(const pickedLayer = usePicked("/:poster layer", posterPage, "layers");)") != std::string::npos);
    CHECK(web.find(R"(pick="/:poster layer")") != std::string::npos);
    CHECK(web.find(R"(empty="Nothing on it yet")") != std::string::npos);
    CHECK(web.find(R"(<Live row={pickedLayer} field="size" />)") != std::string::npos);
    // A command on the picked layer acts on it, there only while one is, with what it's given.
    CHECK(web.find(R"(<Command name="studio::layer::scale" id={pickedLayer?.id as string | undefined} label="Twice" given={{ "by": 2 }} when={pickedLayer !== null} />)") != std::string::npos);
    CHECK(web.find(R"(keys={["delete"]})") != std::string::npos);
    // One on the poster the address names is pressed while the poster already says so.
    CHECK(web.find(R"(given={{ "tint": "ink" }} pressed={(posterPage.data?.["tint"] ?? null) === "ink"})") != std::string::npos);
    CHECK(native.find(R"(value picked_layer = picked(p, "/:poster layer", poster_page, "layers");)") != std::string::npos);
    CHECK(native.find(R"(.pick = "/:poster layer")") != std::string::npos);
    CHECK(native.find(R"(.empty = "Nothing on it yet")") != std::string::npos);
    CHECK(native.find(R"(words(show(field_of(picked_layer, "size"))))") != std::string::npos);
    CHECK(native.find(R"(command(p, {.command = "studio::layer::scale", .label = "Twice", .fields = value(std::map<std::string, value>{{"by", value(2)}}), .id = picked_layer["id"].as_string(), .when = !none(picked_layer)})") != std::string::npos);
    CHECK(native.find(R"(.pressed = same(at(poster_page, "tint"), value("ink")))") != std::string::npos);
}

TEST_CASE("a command apart from undo is kept, but never a step undo takes back") {
    auto files = built(layered_screen("\t\ttext \"Poster\"\n"));
    CHECK(files["desktop/src/local/studio.cpp"].find(R"(command("studio::poster::preview").on("studio::poster").apart())") != std::string::npos);
    CHECK(files["web/src/local.generated.ts"].find(R"(command("studio::poster::preview").on("studio::poster").apart())") != std::string::npos);
}

