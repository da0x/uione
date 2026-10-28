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

TEST_CASE("a toggle may be one of an enum's choices, pressed one at a time, and its whens compare the enum with a choice") {
    std::string chosen = screen("\t\ttoggle tool::select \"Select\" key v\n\t\ttoggle tool::pen \"Pen\" key p\n\t\tposter::create \"New\" when tool == tool::pen\n");
    chosen.replace(chosen.find("\tentity poster"), 0, "\tenum tool {\n\t\tselect\n\t\tpen\n\t}\n\n");
    CHECK(messages(chosen).empty());
    std::string wrong = screen("\t\ttoggle tool::brush \"Brush\"\n\t\ttoggle shade::dark \"Dark\"\n\t\ttoggle tool::pen \"Pen\"\n\t\ttoggle tool::pen \"Again\"\n\t\ttoggle tool \"Tool\"\n");
    wrong.replace(wrong.find("\tentity poster"), 0, "\tenum tool {\n\t\tselect\n\t\tpen\n\t}\n\n");
    CHECK(messages(wrong) == std::vector<std::string>{"27: enum tool has no choice brush",
                                                      "28: there's no enum shade for this toggle to choose from in namespace studio",
                                                      "30: toggle tool::pen is here twice; a screen has one of each",
                                                      "31: toggle tool is one of a choice elsewhere here, so it can't be on or off alone too"});
}

TEST_CASE("two lines always shown can't share a key, as the first would always take it; shown at different times, they can") {
    CHECK(messages(screen("\t\tposter::create \"New\" key ctrl+n\n\t\tlink / \"Home\" key ctrl+n\n")) ==
          std::vector<std::string>{"23: ctrl+n already runs the line at 22, which is always shown too; say when each is shown, or give this one another key"});
    CHECK(messages(screen("\t\ttoggle list \"List\"\n\t\tposter::create \"New\" key ctrl+n when list\n\t\tlink / \"Home\" key ctrl+n\n")).empty());
}

TEST_CASE("undo and redo take back steps kept on the machine, so a project in the cloud can't say them yet, nor keep a command apart from them") {
    std::string cloud = "import one\nproject studio {\n\tone  \"0.18.0\"\n}\n\n" + poster + "\n\tscreen \"Studio\" / {\n\t\tundo \"Undo\"\n\t\tredo \"Redo\"\n\t}\n}\n";
    CHECK(messages(cloud) == std::vector<std::string>{"20: undo takes a step back that's kept on this machine, so it needs storage local in the project block, for now",
                                                      "21: redo takes a step back that's kept on this machine, so it needs storage local in the project block, for now"});
    std::string apart = cloud;
    apart.replace(apart.find("\tcommand poster::create\n"), 0, "\tcommand poster::seen {\n\t\tapart from undo\n\t}\n");
    CHECK(messages(apart) == std::vector<std::string>{
                                 "12: apart from undo keeps a step out of what's undone on this machine, so it needs storage local in the project block, for now",
                                 "23: undo takes a step back that's kept on this machine, so it needs storage local in the project block, for now",
                                 "24: redo takes a step back that's kept on this machine, so it needs storage local in the project block, for now"});
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
    CHECK(web.find(R"(given={{ "tint": "ink" }} pressed={sameValue((posterPage.data?.["tint"] ?? null), "ink")})") != std::string::npos);
    CHECK(native.find(R"(value picked_layer = picked(p, "/:poster layer", poster_page, "layers");)") != std::string::npos);
    CHECK(native.find(R"(.pick = "/:poster layer")") != std::string::npos);
    CHECK(native.find(R"(.empty = "Nothing on it yet")") != std::string::npos);
    CHECK(native.find(R"(words(show(field_of(picked_layer, "size"))))") != std::string::npos);
    CHECK(native.find(R"(command(p, {.command = "studio::layer::scale", .label = "Twice", .fields = value(std::map<std::string, value>{{"by", value(2)}}), .id = picked_layer["id"].as_string(), .when = !none(picked_layer)})") != std::string::npos);
    CHECK(native.find(R"(.pressed = same(at(poster_page, "tint"), value("ink")))") != std::string::npos);
}

TEST_CASE("a create button sends what the page's address names, as a create form does, beside what it's given") {
    std::string source = layered_screen("\t\tlayer::create \"Layer\" { size = 20 }\n");
    source.replace(source.find("\tcommand layer::delete"), 0, "\tcommand layer::create\n");
    auto files = built(source);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(<Command name="studio::layer::create" label="Layer" given={{ "size": 20, "poster": posterId }} />)") != std::string::npos);
    CHECK(native.find(R"(.fields = value(std::map<std::string, value>{{"size", value(20)}, {"poster", value(poster_id)}}))") != std::string::npos);
}

TEST_CASE("a status line's words are quiet on the desktop, as its messages are, and as the web's status line draws them") {
    auto files = built(layered_screen(R"(		pick layer from poster_page.layers
		layout workspace
		main {
			text "{layer.size}"
		}
		status {
			text "{layer.size} wide"
			messages
		}
)"));
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(native);
    CHECK(native.find(R"(region{"status", along(text({words(show(field_of(picked_layer, "size"))), words(" wide")}, true), messages(p))})") != std::string::npos);
    CHECK(native.find(R"(region{"main", items(text({words(show(field_of(picked_layer, "size")))}))})") != std::string::npos);
}

TEST_CASE("in a toolbar, a group or a tool that doesn't apply now is there but off, and tools side by side with the same words are one") {
    auto files = built(layered_screen(R"(		pick layer from poster_page.layers
		layout workspace
		toolbar {
			group "Size" small when layer != none {
				layer::scale "Twice" icon larger { by = 2 } when layer.size < 100
				layer::scale "Twice" icon larger { by = 0.5 } on when layer.size >= 100 when layer.size >= 100
				layer::delete "Delete" icon delete
			}
		}
		main {
			table poster_page.layers {
				depth  size
			}
		}
)"));
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    // The group is there while nothing's picked, off.
    CHECK(native.find(R"(p.off_unless((!same(picked_layer, value())), [&] { return group(p, "Size", true, false, [&] { return tools(either(p, {command_button{)") != std::string::npos);
    CHECK(web.find(R"(<Applies when={(!isNone(pickedLayer))}>)") != std::string::npos);
    // The two Twice lines are one tool, and Delete its own.
    CHECK(native.find(R"(command(p, {.command = "studio::layer::delete")") != std::string::npos);
    auto either = web.find("<Either>"), delete_at = web.find(R"(name="studio::layer::delete")");
    CHECK(either != std::string::npos);
    CHECK(web.find("</Either>", either) < delete_at);
}

TEST_CASE("in a toolbar, a tool giving a color written #rrggbb, with no icon, is drawn as a circle of it") {
    std::string source = layered_screen(R"(		pick layer from poster_page.layers
		layout workspace
		toolbar {
			group "Tint" small {
				poster::arrange "Ink" { tint = tint::ink }
				layer::scale "Twice" { by = 2 }
			}
		}
		main {
			text "{poster_page.title}"
		}
)");
    auto files = built(source);
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(native);
    // Neither gives a color, so neither is a swatch.
    CHECK(native.find(".swatch") == std::string::npos);
    source.replace(source.find("{ tint = tint::ink }"), std::string("{ tint = tint::ink }").size(), "{ title = \"#ffd23f\" }");
    files = built(source);
    CHECK(files["desktop/src/screens/main.cpp"].find(R"(.swatch = "#ffd23f")") != std::string::npos);
    CHECK(files["web/src/screens/main.tsx"].find(R"( swatch="#ffd23f")") != std::string::npos);
}

TEST_CASE("a command apart from undo is kept, but never a step undo takes back") {
    auto files = built(layered_screen("\t\ttext \"Poster\"\n"));
    CHECK(files["desktop/src/local/studio.cpp"].find(R"(command("studio::poster::preview").on("studio::poster").apart())") != std::string::npos);
    CHECK(files["web/src/local.generated.ts"].find(R"(command("studio::poster::preview").on("studio::poster").apart())") != std::string::npos);
}

namespace {

    const std::string collaged = R"(namespace studio at / {
	entity poster {
		title    text    required
		width    number  = 1600
		height   number  = 900
	}

	entity layer {
		poster   poster  required
		depth    number  = 0
		picture  file
		picture_width   number  = 0
		picture_height  number  = 0
		around   enum clear | edges | color | harmonize = around::clear
		x        number  = 0
		y        number  = 0
		w        number  = 400
		h        number  = 300
	}

	command poster::create
	command layer::place {
		changes x y w h
	}
	command layer::create
	command layer::delete
	command layer::update
	view poster_page per poster {
		title  width  height
		layers = each layer in poster {
			order by depth
			picture  picture_width  picture_height  around  x  y  w  h
		}
	}
)";

    std::string collage_screen(const std::string& body) { return project + collaged + "\n\tscreen \"Poster\" /posters/:poster {\n" + body + "\t}\n}\n"; }

}  // namespace

TEST_CASE("a workspace's tools are a column of small tools down its left side, on the web and the desktop alike") {
    auto files = built(screen("\t\tlayout workspace\n\t\ttools {\n\t\t\ttoggle list \"List\" icon layers key l\n\t\t}\n\t\tmain {\n\t\t\ttext \"The posters\"\n\t\t}\n"));
    CAPTURE(files["web/src/screens/main.tsx"]);
    CAPTURE(files["desktop/src/screens/main.cpp"]);
    CHECK(files["web/src/screens/main.tsx"].find(R"(<Region name="tools">)") != std::string::npos);
    CHECK(files["web/src/screens/main.tsx"].find(R"(<Tools>)") != std::string::npos);
    CHECK(files["desktop/src/screens/main.cpp"].find(R"(region{"tools", tool_column(p, [&] { return tools(toggle(p, "/ list", "List", "layers", std::vector<std::string>{"l"})); })})") != std::string::npos);
    CHECK(messages(screen("\t\ttools {\n\t\t\ttoggle list \"List\"\n\t\t}\n")) ==
          std::vector<std::string>{"22: layout single has no region tools; its regions are main"});
}

TEST_CASE("a table reordered descending shows its rows highest first, on the web and the desktop alike") {
    std::string source = layered_screen(R"(		table poster_page.layers {
			size
			reorder depth descending
		}
)");
    source.replace(source.find("\tcommand layer::delete\n"), 0, "\tcommand layer::update\n");
    auto files = built(source);
    CAPTURE(files["desktop/src/screens/main.cpp"]);
    CHECK(files["desktop/src/screens/main.cpp"].find(R"(.reorder = reorder_spec{"studio::layer::update", "depth", true, true})") != std::string::npos);
    CHECK(files["web/src/screens/main.tsx"].find(R"(reorder={{ command: "studio::layer::update", field: "depth", descending: true }})") != std::string::npos);
}

TEST_CASE("a collage matches its pictures' colors while a condition holds, on the web and the desktop alike") {
    auto files = built(collage_screen(R"(		collage poster_page.layers size width height {
			match colors when poster_page.width > 1000
		}
)"));
    CAPTURE(files["desktop/src/screens/main.cpp"]);
    CHECK(files["desktop/src/screens/main.cpp"].find(R"(.match_colors = (ready(poster_page) && (compared(at(poster_page, "width"), value(1000)) > 0)))") != std::string::npos);
    CHECK(files["web/src/screens/main.tsx"].find(R"( matchColors={)") != std::string::npos);
    CHECK(messages(collage_screen("\t\tcollage poster_page.layers size width height {\n\t\t\tmatch colors when poster_page.wide > 1\n\t\t}\n")) ==
          std::vector<std::string>{"45: view poster_page has no wide for the button to read"});
}

TEST_CASE("a collage's layer is drawn by a drag while its draw's when holds, given what it says, and the choices its when reads go back to their first") {
    auto with_tools = [](std::string source) {
        source.replace(source.find("\tentity poster"), 0, "\tenum tool {\n\t\tmove\n\t\tbox\n\t}\n\n");
        return source;
    };
    auto files = built(with_tools(collage_screen(R"(		toggle tool::move "Move" key v
		toggle tool::box "Box" key b
		collage poster_page.layers size width height {
			draw layer::create { around = around::edges } when tool == tool::box
		}
)")));
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"( draws={[{ command: "studio::layer::create", given: { "around": "edges" }, when: ((chosenTool === "box")), back: [["/posters/:poster tool", "move"]] }]})") != std::string::npos);
    CHECK(native.find(R"(.draws = {collage_draw{.command = "studio::layer::create", .fields = value(std::map<std::string, value>{{"around", value("edges")}}), .when = )") != std::string::npos);
    CHECK(native.find(R"(.back = {{"/posters/:poster tool", "move"}}})") != std::string::npos);
    // By line, kept as a path's points.
    std::string lined = with_tools(collage_screen("\t\ttoggle tool::move \"Move\"\n\t\ttoggle tool::box \"Line\"\n\t\tcollage poster_page.layers size width height {\n"
                                                  "\t\t\tdraw layer::create by line when tool == tool::box\n\t\t}\n"));
    lined.replace(lined.find("\t\tx        number"), 0, "\t\tpoints   text\n");
    lined.replace(lined.find("around  x  y  w  h"), 0, "points  ");
    auto by_line = built(lined);
    CHECK(by_line["web/src/screens/main.tsx"].find(R"(when: ((chosenTool === "box")), by: "line", back:)") != std::string::npos);
    CHECK(by_line["desktop/src/screens/main.cpp"].find(R"(.by = "line", .back =)") != std::string::npos);
    CHECK(messages(with_tools(collage_screen(R"(		collage poster_page.layers size width height {
			draw layer::delete
			draw poster::create
			draw layer::create { tint = "#ffffff" }
			draw layer::create by line
			draw layer::create by curve
		}
)"))) == std::vector<std::string>{"50: a drag on the collage makes one of its layers, so draw names layer::create",
                                   "51: a drag on the collage makes one of its layers, so draw names layer::create",
                                   "52: entity layer has no field tint to be given",
                                   "53: a line is kept as a path's points, which layer doesn't have; add points text to it, or say its field",
                                   "54: a drag draws a box, by line a line, by hand what it goes through, or by points a path node by node; there's no by curve",
                                   "54: a line is kept as a path's points, which layer doesn't have; add points text to it, or say its field"});
}

TEST_CASE("a clear collage has nothing behind its layers, on the web and the desktop alike") {
    auto files = built(collage_screen("\t\tcollage poster_page.layers size width height {\n\t\t\tclear\n\t\t}\n"));
    CHECK(files["web/src/screens/main.tsx"].find(R"( clear )") != std::string::npos);
    CHECK(files["desktop/src/screens/main.cpp"].find(R"(.clear = true)") != std::string::npos);
}

TEST_CASE("a collage's layers are listed, each hidden or shown by its hidden, ordered by its depth, and deleted, on the web and the desktop alike") {
    CHECK(messages(collage_screen("\t\tlayers poster_page.layers {\n\t\t\tdelete layer::delete\n\t\t}\n")) ==
          std::vector<std::string>{"44: layers lists the layers of a collage on this screen, and none here is of poster_page.layers"});
    CHECK(messages(collage_screen("\t\tcollage poster_page.layers size width height {\n\t\t}\n\t\tlayers poster_page.layers {\n\t\t\thide layer::update\n\t\t\torder layer::update\n"
                                  "\t\t\tdelete poster::create\n\t\t}\n")) ==
          std::vector<std::string>{"47: hide sets a layer's hidden, which layer doesn't have or the view doesn't read; add hidden number to it, and to the list's block",
                                   "48: order sets a layer's depth, which layer doesn't have or the view doesn't read; add depth number to it, and to the list's block",
                                   "49: the layers are layers, so delete names a command on layer"});
    std::string fine = collage_screen("\t\tpick layer from poster_page.layers\n\t\tcollage poster_page.layers size width height {\n\t\t\tclear\n\t\t\tover layer {\n\t\t\t}\n\t\t}\n"
                                      "\t\tlayers poster_page.layers {\n\t\t\thide layer::update\n\t\t\torder layer::update\n\t\t\tdelete layer::delete\n\t\t}\n");
    fine.replace(fine.find("\t\tx        number"), 0, "\t\thidden   number  = 0\n");
    fine.replace(fine.find("around  x  y  w  h"), 0, "hidden  depth  ");
    CHECK(messages(fine).empty());
    auto files = built(fine);
    CAPTURE(files["web/src/screens/main.tsx"]);
    CHECK(files["web/src/screens/main.tsx"].find(R"( clear pick="/posters/:poster layer" hide="studio::layer::update" order="studio::layer::update" remove="studio::layer::delete" />)") != std::string::npos);
    CHECK(files["desktop/src/screens/main.cpp"].find(R"(.clear = true, .pick = "/posters/:poster layer", .hide = "studio::layer::update", .order = "studio::layer::update", .remove = "studio::layer::delete"})") !=
          std::string::npos);
}

TEST_CASE("a collage's objects held in layers: each naming its layer, the layers named, ordered and made by their create, and listed with their objects") {
    const std::string drawing = R"(namespace studio at / {
	entity drawing {
		width   number  = 1600
		height  number  = 900
	}
	entity layer {
		drawing  drawing  required
		name     text
		depth    number  = 0
		hidden   number  = 0
	}
	entity item {
		drawing  drawing  required
		layer    layer
		depth    number  = 0
		x  number = 0
		y  number = 0
		w  number = 0
		h  number = 0
	}
	command layer::create
	command layer::delete
	command layer::show {
		changes hidden
	}
	command item::create
	command item::delete
	view drawing_page per drawing {
		width  height
		layers = each layer in drawing {
			order by depth
			name  depth  hidden
		}
		items = each item in drawing {
			order by depth
			layer  depth  x  y  w  h
		}
	}
	command layer::rename {
		changes name
	}
)";
    auto on_screen = [&](const std::string& body) { return project + drawing + "\n\tscreen \"Drawing\" /drawings/:drawing {\n" + body + "\t}\n}\n"; };
    const std::string fine = R"(		pick item from drawing_page.items
		pick layer from drawing_page.layers
		collage drawing_page.items size width height {
			in drawing_page.layers by layer
			add item::create
			delete item::delete
			over item {
			}
		}
		layers drawing_page.layers {
			hide layer::show
			rename layer::rename
			delete layer::delete
		}
)";
    CHECK(messages(on_screen(fine)).empty());
    auto files = built(on_screen(fine));
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"( layers="layers" layer="layer" layerCreate="studio::layer::create" layerParent="drawing" layerPick="/drawings/:drawing layer")") != std::string::npos);
    CHECK(native.find(R"(.layers = "layers", .layer = "layer", .layer_create = "studio::layer::create", .layer_parent = "drawing", .layer_pick = "/drawings/:drawing layer")") != std::string::npos);
    CHECK(web.find(R"(<Layers view={drawingPage} list="layers")") != std::string::npos);
    CHECK(web.find(R"(pick="/drawings/:drawing layer" objects="items" objectLayer="layer" objectRemove="studio::item::delete" objectPick="/drawings/:drawing item")") != std::string::npos);
    CHECK(native.find(R"(.objects = "items", .object_layer = "layer", .object_remove = "studio::item::delete", .object_pick = "/drawings/:drawing item", .rename = "studio::layer::rename")") != std::string::npos);
    CHECK(web.find(R"( rename="studio::layer::rename")") != std::string::npos);
    // Each object names its layer by a field pointing at one, read by the view.
    std::string wrong = on_screen("\t\tcollage drawing_page.items size width height {\n\t\t\tin drawing_page.layers by depth\n\t\t}\n");
    CHECK(messages(wrong) == std::vector<std::string>{"52: each item names its layer by depth, so it's a field pointing at layer, like depth  layer"});
}

TEST_CASE("a toolbar's color buttons are drawn as what they give: a fill as a circle, a stroke as a line, a width as a line that wide, and none clear") {
    std::string source = collage_screen(R"(		pick layer from poster_page.layers
		layout workspace
		toolbar {
			group "Paint" small {
				layer::paint "Blue" { fill = "#2e86de" }
				layer::paint "No fill" { fill = none }
				layer::paint "Ink" { stroke = "#1d1d1f" }
				layer::paint "No stroke" { stroke = none  stroke_width = 0 }
				layer::paint "Wide" { stroke_width = 4 }
			}
		}
		main {
			collage poster_page.layers size width height {
				over layer {
				}
			}
		}
)");
    source.replace(source.find("\t\tx        number"), 0, "\t\tfill     text\n\t\tstroke   text\n\t\tstroke_width  number = 0\n");
    source.replace(source.find("around  x  y  w  h"), 0, "fill  stroke  stroke_width  ");
    source.replace(source.find("\tcommand layer::create\n"), 0, "\tcommand layer::paint {\n\t\tchanges fill stroke stroke_width\n\t}\n");
    auto files = built(source);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    for (const auto& [label, swatch] : {std::pair{"Blue", "#2e86de"}, {"No fill", "none"}, {"Ink", "line #1d1d1f"}, {"No stroke", "line none"}, {"Wide", "width 4"}}) {
        CAPTURE(label);
        CHECK(web.find(std::string("label=\"") + label + "\" swatch=\"" + swatch + "\"") != std::string::npos);
        CHECK(native.find(std::string(".label = \"") + label + "\"") != std::string::npos);
        CHECK(native.find(std::string(".swatch = \"") + swatch + "\"") != std::string::npos);
    }
}

TEST_CASE("a collage is a view's list as layers, as big as two of its values, placed by a command, with buttons over the picked layer") {
    const std::string fine = R"(		pick layer from poster_page.layers
		collage poster_page.layers size width height {
			place layer::place
			snap to edges
			empty "Add a picture to start"
			over layer {
				layer::delete "Delete" icon delete key delete
			}
		}
)";
    CHECK(messages(collage_screen(fine)).empty());
    CHECK(messages(collage_screen(R"(		collage poster_page.layers size wide tall {
			place poster::create
			over layer {
				text "Not a button"
			}
		}
)")) == std::vector<std::string>{"44: view poster_page has no wide, which says how big the collage is; add it to the view",
                                   "44: view poster_page has no tall, which says how big the collage is; add it to the view",
                                   "45: a collage's layers are layers, so place names a command on layer, like layer::place",
                                   "46: over names the screen's pick of these layers, like pick layer from poster_page.layers",
                                   "47: what's over a picked layer are buttons, like layer::delete \"Delete\" icon delete, or crop \"Crop\""});
    auto files = built(collage_screen(fine));
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(<Collage view={posterPage} list="layers" width="width" height="height" fields={{ around: "around", h: "h", picture: "picture", picture_height: "picture_height", picture_width: "picture_width", w: "w", x: "x", y: "y" }} place="studio::layer::place" snap empty="Add a picture to start" pick="/posters/:poster layer" parent="poster" name="/posters/:poster layer" update="studio::layer::update" subject={posterId}>)") !=
          std::string::npos);
    CHECK(web.find(R"(<Command name="studio::layer::delete" id={pickedLayer?.id as string | undefined} label="Delete" icon="delete" keys={["delete"]} when={pickedLayer !== null} />)") != std::string::npos);
    CHECK(native.find(R"(collage(p, {.view = poster_page, .list = "layers", .width = "width", .height = "height", .fields = {{"around", "around"}, {"h", "h"}, {"picture", "picture"}, {"picture_height", "picture_height"}, {"picture_width", "picture_width"}, {"w", "w"}, {"x", "x"}, {"y", "y"}}, .place = "studio::layer::place", .snap = true, .empty = "Add a picture to start", .pick = "/posters/:poster layer", .over = {command_button{.command = "studio::layer::delete", .label = "Delete", .id = picked_layer["id"].as_string(), .when = !none(picked_layer), .icon = "delete", .keys = std::vector<std::string>{"delete"}}}, .subject = poster_id, .parent = "poster", .key = "/posters/:poster layer", .update = "studio::layer::update"})") !=
          std::string::npos);
}


TEST_CASE("pictures dropped, pasted or taken from the screen are made by a collage's add, kept in its picture, their own size in its picture_width and picture_height") {
    const std::string fine = R"(		toolbar {
			group "Insert" {
				paste "Paste" icon paste key ctrl+shift+v
				screenshot "Screenshot" icon screenshot
			}
		}
		main {
			collage poster_page.layers size width height {
				add layer::create
				paste
			}
		}
)";
    CHECK(messages(collage_screen("\t\tlayout workspace\n" + fine)).empty());
    CHECK(messages(collage_screen(R"(		layout workspace
		toolbar {
			group "Insert" {
				paste "Paste"
				crop "Crop"
			}
		}
		main {
			collage poster_page.layers size width height {
				picture x sized picture picture_height
				paste
			}
		}
)")) == std::vector<std::string>{"48: this screen has no collage that says crop layer::crop for this button to use",
                                   "53: layer.x keeps a collage's picture, so it's a file", "53: layer.picture keeps a collage's picture_width, so it's a number",
                                   "54: pasted pictures are made by the collage's add, so it says one too, like add layer::create"});
    auto files = built(collage_screen("\t\tlayout workspace\n" + fine));
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(<CollageButton collage="/posters/:poster collage" does="paste" label="Paste" icon="paste" keys={["ctrl+shift+v"]} />)") != std::string::npos);
    // Each part in the field named like it, the runtimes sent which; one whose own size
    // isn't kept yet has it filled in by the layer's update.
    CHECK(web.find(R"(<Collage view={posterPage} list="layers" width="width" height="height" fields={{ around: "around", h: "h", picture: "picture", picture_height: "picture_height", picture_width: "picture_width", w: "w", x: "x", y: "y" }} fills add="studio::layer::create" parent="poster" name="/posters/:poster collage" paste update="studio::layer::update" subject={posterId} />)") != std::string::npos);
    CHECK(native.find(R"(.paste = true, .update = "studio::layer::update"})") != std::string::npos);
    std::string unsized = collage_screen("\t\tlayout workspace\n" + fine);
    unsized.erase(unsized.find("\tcommand layer::update\n"), std::string("\tcommand layer::update\n").size());
    CHECK(messages(unsized) == std::vector<std::string>{"51: a picture's own size, when it isn't kept yet, is filled in by layer::update, which isn't declared in namespace studio"});
    CHECK(native.find(R"(collage_button(p, {.key = "/posters/:poster collage", .does = "screenshot", .label = "Screenshot", .icon = "screenshot"}))") != std::string::npos);
}

TEST_CASE("a collage reads each part from the field named like it, or from the one it says, and hands its runtimes which") {
    const std::string noted = R"(namespace studio at / {
	entity poster {
		width    number  = 1600
		height   number  = 900
	}

	entity note {
		poster   poster  required
		stack    number  = 0
		caption  text
		letters  number  = 48
		margin   enum clear | edges = margin::clear
		left     number  = 0
		top      number  = 0
		w        number  = 0
		h        number  = 0
	}

	command note::place {
		changes left top w h
	}
	command note::write {
		changes caption letters left top w h
	}
	view poster_page per poster {
		width  height
		notes = each note in poster {
			order by stack
			stack  caption  letters  margin  left  top  w  h
		}
	}

	screen "Poster" /posters/:poster {
		collage poster_page.notes size width height {
			at left top
			stacked by stack
			words caption sized letters
			around margin
			place note::place
			write note::write
		}
	}
}
)";
    CHECK(messages(project + noted).empty());
    auto files = built(project + noted);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(fields={{ around: "margin", depth: "stack", h: "h", size: "letters", w: "w", words: "caption", x: "left", y: "top" }})") != std::string::npos);
    CHECK(native.find(R"(.fields = {{"around", "margin"}, {"depth", "stack"}, {"h", "h"}, {"size", "letters"}, {"w", "w"}, {"words", "caption"}, {"x", "left"}, {"y", "top"}})") != std::string::npos);
    // Without them, there's no place, and no words to write.
    std::string unsaid = project + noted;
    for (const std::string line : {"\t\t\tat left top\n", "\t\t\twords caption sized letters\n"}) unsaid.erase(unsaid.find(line), line.size());
    CHECK(messages(unsaid) == std::vector<std::string>{
                                  "41: entity note has no field x, where a collage keeps a layer's place; add x number to it, or say the fields, like at x y  size w h",
                                  "41: entity note has no field y, where a collage keeps a layer's place; add y number to it, or say the fields, like at x y  size w h",
                                  "45: words written are kept in a layer's words and size, which note doesn't have; add them, or say its fields, like words caption sized letters"});
}

TEST_CASE("a collage is copied or saved as a picture, all of it or its picked layer, and keeps a small picture of itself on what its view is per") {
    std::string kept = project + collaged;
    kept.replace(kept.find("\t\theight   number  = 900\n"), 0, "\t\tpreview  file\n");
    kept.replace(kept.find("\tcommand poster::create\n"), 0, "\tcommand poster::preview {\n\t\tchanges preview\n\t\tapart from undo\n\t}\n");
    auto on_screen = [&](const std::string& body) { return kept + "\n\tscreen \"Poster\" /posters/:poster {\n" + body + "\t}\n}\n"; };
    const std::string fine = R"(		pick layer from poster_page.layers
		copy poster_page as picture "Copy" icon copy
		save poster_page as picture "Save" icon save key ctrl+s
		collage poster_page.layers size width height {
			keep preview poster::preview into preview 480 wide
			over layer {
				copy layer as picture "Copy" icon copy
			}
		}
)";
    CHECK(messages(on_screen(fine)).empty());
    std::string said = on_screen(R"(		save posters as picture
		collage poster_page.layers size width height {
			keep preview layer::place into title 8 wide
		}
)");
    auto lines = messages(said);
    CHECK(lines == std::vector<std::string>{"49: posters isn't a collage on this screen, or its pick; a picture is made of one, like poster_page",
                                            "51: a collage's preview is kept on poster, so by a command on it, like poster::preview",
                                            "51: poster.title holds its preview, so it's a file", "51: a preview is from 16 to 4096 wide"});
    auto files = built(on_screen(fine));
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(<CollageButton collage="/posters/:poster layer" does="save" label="Save" icon="save" keys={["ctrl+s"]} />)") != std::string::npos);
    CHECK(web.find(R"(<CollageButton collage="/posters/:poster layer" does="copy picked" label="Copy" icon="copy" />)") != std::string::npos);
    CHECK(web.find(R"(preview="studio::poster::preview" previewInto="preview" previewWide={480})") != std::string::npos);
    CHECK(native.find(R"(collage_ask{.key = "/posters/:poster layer", .does = "copy picked", .label = "Copy", .icon = "copy"})") != std::string::npos);
    CHECK(native.find(R"(.preview = "studio::poster::preview", .preview_into = "preview", .preview_wide = 480})") != std::string::npos);

    // As SVG, from its vectors, all of it or the picked layer.
    auto as_svg = built(on_screen(R"(		pick layer from poster_page.layers
		save poster_page as svg "Save SVG" icon save
		collage poster_page.layers size width height {
			over layer {
				copy layer as svg "Copy SVG" icon copy
			}
		}
)"));
    CHECK(as_svg["web/src/screens/main.tsx"].find(R"(does="save svg" label="Save SVG")") != std::string::npos);
    CHECK(as_svg["desktop/src/screens/main.cpp"].find(R"(.does = "copy svg picked", .label = "Copy SVG")") != std::string::npos);
}

TEST_CASE("cards show a file each row holds as a picture on top, fitted in or filling its place") {
    const std::string source = project + R"(namespace studio at / {
	entity poster {
		title    text  required
		preview  file
	}

	view posters {
		each poster {
			title  preview
		}
	}

	screen "Gallery" / {
		cards posters {
			picture preview fit
			title
		}
		cards posters {
			picture title
			title
		}
	}
}
)";
    CHECK(messages(source) == std::vector<std::string>{"26: poster.title is shown as a picture, so it's a file"});
    std::string fine = source;
    fine.erase(fine.find("\t\tcards posters {\n\t\t\tpicture title"), std::string("\t\tcards posters {\n\t\t\tpicture title\n\t\t\ttitle\n\t\t}\n").size());
    CHECK(messages(fine).empty());
    auto files = built(fine);
    CAPTURE(files["web/src/screens/main.tsx"]);
    CHECK(files["web/src/screens/main.tsx"].find(R"(<Cards view={posters} list="rows" columns={{ title: "Title" }} picture="preview" fit)") != std::string::npos);
    CHECK(files["desktop/src/screens/main.cpp"].find(R"(.picture = "preview", .fit = true})") != std::string::npos);
}

TEST_CASE("a picture shows the file a pick or a view holds, on its own, fitted in or filling its place") {
    const std::string source = project + R"(namespace studio at / {
	entity poster {
		title    text  required
		preview  file
	}

	view posters {
		each poster {
			title  preview
		}
	}
	view poster_page per poster {
		title  preview
	}

	screen "Studio" / {
		pick poster from posters
		picture poster.preview fit when poster != none
		picture poster.title
		picture posters.preview
	}
	screen "Poster" /posters/:poster {
		picture poster_page.preview
	}
}
)";
    CHECK(messages(source) == std::vector<std::string>{"26: poster.title isn't a file, so there's no picture to show",
                                                       "27: a picture shows a file a view or a pick holds, like picture poster_page.preview, or picture layer.picture"});
    std::string fine = source;
    for (const std::string line : {"\t\tpicture poster.title\n", "\t\tpicture posters.preview\n"}) fine.erase(fine.find(line), line.size());
    CHECK(messages(fine).empty());
    auto files = built(fine);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(<Picture source={(pickedPoster?.["preview"] ?? null)} fit />)") != std::string::npos);
    CHECK(web.find(R"(<Picture source={(posterPage.data?.["preview"] ?? null)} />)") != std::string::npos);
    CHECK(native.find(R"(picture_of(p, field_of(picked_poster, "preview").as_string(), true, 320))") != std::string::npos);
    CHECK(native.find(R"(picture_of(p, at(poster_page, "preview").as_string(), false, 320))") != std::string::npos);
}

TEST_CASE("a table shows a file each row holds as a small picture before its cells, and % leaves what's over as a clock counts") {
    const std::string source = project + R"(namespace studio at / {
	entity poster {
		title    text  required
		preview  file
	}

	view posters {
		each poster {
			title  preview
		}
	}

	screen "Posters" / {
		table posters {
			picture preview fit
			title
		}
		table posters {
			picture title
			title
		}
		text "An even screen" when screen.width % 2 == 0
	}
}
)";
    CHECK(messages(source) == std::vector<std::string>{"26: poster.title is shown as a picture, so it's a file"});
    std::string fine = source;
    fine.erase(fine.find("\t\ttable posters {\n\t\t\tpicture title"), std::string("\t\ttable posters {\n\t\t\tpicture title\n\t\t\ttitle\n\t\t}\n").size());
    CHECK(messages(fine).empty());
    auto files = built(fine);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"( picture="preview" fit)") != std::string::npos);
    CHECK(native.find(R"(.picture = "preview", .fit = true})") != std::string::npos);
    CHECK(web.find(R"({((remainder(Number(screenSize().width), Number(2)) === 0)) && <Text>An even screen</Text>})") != std::string::npos);
    CHECK(native.find(R"((same(worked('%', value(screen_width()), value(2)), value(0))) ? text({words("An even screen")}) : view())") != std::string::npos);
}

TEST_CASE("on open, a screen opens the first row a condition holds for, or runs a command, the first of its lines that applies") {
    const std::string source = project + R"(namespace studio at / {
	entity poster {
		title   text
		width   number  = 1600
		height  number  = 900
	}

	command poster::create

	view posters {
		each poster {
			title  width
		}
	}

	screen "New" / {
		on open open /posters/:poster for posters where poster.width == screen.width && poster.depth == 1
		on open open /nowhere/:poster for posters where poster.width == screen.size
		on open poster::create { width = screen.width } then open /posters/:poster
		on open text "Not a command"
	}

	screen "Poster" /posters/:poster {
		text "A poster"
	}
}
)";
    CHECK(messages(source) == std::vector<std::string>{"24: a picked poster's row has no depth; add it to the list's block", "25: there's no screen at /nowhere/:poster to open",
                                                       "25: the screen is measured by screen.width and screen.height",
                                                       "27: on open runs a command, like poster::create { width = screen.width } then open /posters/:poster, or opens a row, like open /posters/:poster for posters"});
    std::string fine = source;
    for (const char* wrong : {" && poster.depth == 1", "\t\ton open open /nowhere/:poster for posters where poster.width == screen.size\n", "\t\ton open text \"Not a command\"\n"}) {
        fine.erase(fine.find(wrong), std::string(wrong).size());
    }
    CHECK(messages(fine).empty());
    auto files = built(fine);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(<OnOpen ready={posters.status === "live"} lines={[{ open: "/posters/:poster", rows: (posters.data?.["rows"] as Record<string, unknown>[] | undefined) ?? [], where: (row: Record<string, unknown>) => ((row["width"] ?? null) === screenSize().width) }, { run: { name: "studio::poster::create", given: { "width": screenSize().width }, thenOpen: "/posters/:poster" } }]} />)") != std::string::npos);
    CHECK(native.find(R"(on_open(p, ready(posters), {opening{.open = "/posters/:poster", .view = posters, .list = "", .where = [&](const value& row) { return same(field_of(row, "width"), value(screen_width())); }}, opening{.run = command_button{.command = "studio::poster::create", .label = "Create", .fields = value(std::map<std::string, value>{{"width", value(screen_width())}}), .then_open = "/posters/:poster"}}}))") != std::string::npos);
    // A workspace's on open is the screen's own, outside its regions, and still done.
    auto spread = built(project + poster + "\n\tscreen \"Studio\" / {\n\t\tlayout workspace\n\t\ton open poster::create\n\t\tmain {\n\t\t\ttext \"Hello\"\n\t\t}\n\t}\n}\n");
    CHECK(spread["web/src/screens/main.tsx"].find(R"(<OnOpen ready={true} lines={[{ run: { name: "studio::poster::create" } }]} />)") != std::string::npos);
    CHECK(spread["desktop/src/screens/main.cpp"].find(R"(on_open(p, true, {opening{.run = command_button{.command = "studio::poster::create", .label = "Create"}}}))") != std::string::npos);
}
