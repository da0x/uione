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
          std::vector<std::string>{"24: a group is part of a workspace's toolbar, or at the end of its bar, so it goes in toolbar { ... }"});
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
                                   "54: a drag draws a box, by line a line, by hand what it goes through, by points a path node by node, or by typing words where it's pressed; there's no by curve"});
    // By typing: words typed where it's pressed, kept in the layer's words and size.
    std::string typing = with_tools(collage_screen("\t\ttoggle tool::move \"Move\"\n\t\ttoggle tool::box \"Text\"\n\t\tcollage poster_page.layers size width height {\n"
                                                   "\t\t\tdraw layer::create { size = 48 } by typing when tool == tool::box\n\t\t}\n"));
    typing.replace(typing.find("\t\tx        number"), 0, "\t\twords    text\n\t\tsize     number  = 0\n");
    typing.replace(typing.find("around  x  y  w  h"), 0, "words  size  ");
    CHECK(messages(typing).empty());
    auto typed = built(typing);
    CHECK(typed["web/src/screens/main.tsx"].find(R"(given: { "size": 48 }, when: ((chosenTool === "box")), by: "typing", back:)") != std::string::npos);
    CHECK(typed["desktop/src/screens/main.cpp"].find(R"(.by = "typing", .back =)") != std::string::npos);
    std::string unworded = with_tools(collage_screen("\t\tcollage poster_page.layers size width height {\n\t\t\tdraw layer::create by typing\n\t\t}\n"));
    CHECK(messages(unworded) == std::vector<std::string>{"50: words typed are kept in a layer's words and size, which layer doesn't have; add words text and size number to it, or say their fields"});
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

TEST_CASE("a tool's options open its lines, each a choice with what it's for, a page's shape drawn as a page of it") {
    std::string body = R"(		layout workspace
		toolbar {
			group "Page" {
				options "Page" icon fit {
					poster::resize "Wide" about "16:9, for screens" { width = 1920  height = 1080 }
					poster::resize "Tall" { width = 1080  height = 1920 }
				}
			}
		}
		main {
			collage poster_page.layers size width height {
			}
		}
)";
    std::string source = collage_screen(body);
    source.replace(source.find("\tcommand poster::create\n"), 0, "\tcommand poster::resize {\n\t\tchanges width height\n\t}\n");
    CHECK(messages(source).empty());
    auto files = built(source);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(<Options label="Page" icon="fit">)") != std::string::npos);
    CHECK(web.find(R"(label="Wide" swatch="page 1920 1080")") != std::string::npos);
    CHECK(web.find(R"(about="16:9, for screens")") != std::string::npos);
    CHECK(native.find(R"(options(p, "Page", "fit", "Page 50", [&] { return tools()") != std::string::npos);
    CHECK(native.find(R"(.swatch = "page 1080 1920")") != std::string::npos);
    CHECK(native.find(R"(.about = "16:9, for screens")") != std::string::npos);
    // Its group holds it alone, by its own name, so the name isn't said twice.
    CHECK(web.find(R"(<Group name="Page" unsaid>)") != std::string::npos);
    CHECK(native.find(R"(); }, 0, false))") != std::string::npos);
    // In a toolbar, and only buttons.
    std::string outside = collage_screen("\t\toptions \"Page\" {\n\t\t\ttext \"Hi\"\n\t\t}\n");
    CHECK(messages(outside) == std::vector<std::string>{"44: options are a tool in a workspace's toolbar, so they go in one of its groups",
                                                        "45: options are buttons, each a choice, like drawing::resize \"Square\" { width = 1080  height = 1080 }"});
}

TEST_CASE("a button's value along a slider, from what it is on the picked row, its command run as it's let go") {
    std::string body = R"(		layout workspace
		toolbar {
			group "Place" {
				options "Across" icon fit {
					layer::place "Across" { x from 0 to 1 }
				}
				layer::place "Down" { y from -10 to 10 by 0.5 }
			}
		}
		main {
			pick layer from poster_page.layers
			collage poster_page.layers size width height {
			}
		}
)";
    std::string source = collage_screen(body);
    CHECK(messages(source).empty());
    auto files = built(source);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(slide={{ field: "x", from: 0, to: 1, by: (1 - 0) / 100, now: ()") != std::string::npos);
    CHECK(web.find(R"(slide={{ field: "y", from: -10, to: 10, by: 0.5,)") != std::string::npos);
    CHECK(native.find(R"(.slide = command_slide{.field = "x", .from = 0, .to = 1, .by = (1 - 0) / 100.0, .now = field_of()") != std::string::npos);
    // One number, the least first, and nothing opened after.
    std::string wrong = body;
    wrong.replace(wrong.find("{ x from 0 to 1 }"), std::string("{ x from 0 to 1 }").size(), "{ x from 1 to 0  y from 0 to 1 }");
    CHECK(messages(collage_screen(wrong)) == std::vector<std::string>{"48: a slider goes from the least to the most, like from 0 to 1",
                                                                        "48: a button is one slider or color picker, so only one of its values is chosen"});
}

TEST_CASE("a collage draws with what a command's buttons last gave, and those buttons stay on with nothing picked") {
    std::string body = R"(		layout workspace
		toolbar {
			group "Look" {
				layer::place "Far" { x = 10 }
			}
		}
		main {
			pick layer from poster_page.layers
			collage poster_page.layers size width height {
				draw layer::create { y = 0 } with x from layer::place
			}
		}
)";
    std::string source = collage_screen(body);
    CHECK(messages(source).empty());
    auto files = built(source);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CHECK(web.find(R"(with: [{ fields: ["x"], from: "studio::layer::place" }])") != std::string::npos);
    CHECK(web.find(R"( remembers)") != std::string::npos);
    CHECK(web.find(R"(when={layer !== null})") == std::string::npos);
    CHECK(native.find(R"(.with = {collage_taken{{"x"}, "studio::layer::place"}})") != std::string::npos);
    CHECK(native.find(R"(.remembers = true, .picks = "/posters/:poster layer")") != std::string::npos);
    CHECK(web.find(R"( remembers picks="/posters/:poster layer")") != std::string::npos);
    // Only what the command changes.
    std::string wrong = body;
    wrong.replace(wrong.find("with x from"), std::string("with x from").size(), "with depth from");
    CHECK(messages(collage_screen(wrong)) == std::vector<std::string>{"53: layer::place doesn't change depth, so it's never given it to take"});
}

TEST_CASE("a button's value any color, from a color picker, from what it is on the picked row") {
    std::string body = R"(		layout workspace
		toolbar {
			group "Color" {
				options "Fill" icon color {
					layer::place "Fill" { around any color }
				}
			}
		}
		main {
			pick layer from poster_page.layers
			collage poster_page.layers size width height {
			}
		}
)";
    // Only text holds a color.
    CHECK(messages(collage_screen(body)) == std::vector<std::string>{"48: around isn't text, so it can't hold a color written #rrggbb"});
    std::string source = collage_screen(body);
    source.replace(source.find("{ around any color }"), std::string("{ around any color }").size(), "{ name any color }");
    source.replace(source.find("\t\tx        number"), 0, "\t\tname     text\n");
    source.replace(source.find("around  x  y  w  h"), 0, "name  ");
    CHECK(messages(source).empty());
    auto files = built(source);
    CAPTURE(files["web/src/screens/main.tsx"]);
    CHECK(files["web/src/screens/main.tsx"].find(R"(color={{ field: "name", now: ()") != std::string::npos);
    CHECK(files["desktop/src/screens/main.cpp"].find(R"(.color = command_color{.field = "name", .now = field_of()") != std::string::npos);
}

TEST_CASE("a collage's layer turned by a drag past a corner is sent to its rotate, how far it's turned kept in its rotation or the field it names, on the web and the desktop alike") {
    std::string body = "\t\tcollage poster_page.layers size width height {\n\t\t\tplace layer::place\n\t\t\trotate layer::rotate\n\t\t}\n";
    std::string source = collage_screen(body);
    source.replace(source.find("\tcommand layer::create\n"), 0, "\tcommand layer::rotate {\n\t\tchanges x y\n\t}\n");
    CHECK(messages(source) == std::vector<std::string>{"49: how far a layer's turned is kept in its rotation, which layer doesn't have; add rotation number to it, or say its field, like rotated angle"});
    source.replace(source.find("\t\th        number  = 300\n"), 0, "\t\tangle    number  = 0\n");
    source.replace(source.find("\t\tchanges x y\n\t}\n\tcommand layer::create"), 14, "\t\tchanges angle x y\n");
    source.replace(source.find("around  x  y  w  h"), 0, "angle  ");
    body = "\t\tcollage poster_page.layers size width height {\n\t\t\tplace layer::place\n\t\t\trotate layer::rotate\n\t\t\trotated angle\n\t\t}\n";
    source.replace(source.find("\t\tcollage poster_page.layers"), source.find("\t}\n}\n", source.find("\t\tcollage poster_page.layers")) - source.find("\t\tcollage poster_page.layers"), body);
    CHECK(messages(source).empty());
    auto files = built(source);
    CAPTURE(files["desktop/src/screens/main.cpp"]);
    CHECK(files["desktop/src/screens/main.cpp"].find(R"(.rotate = "studio::layer::rotate")") != std::string::npos);
    CHECK(files["desktop/src/screens/main.cpp"].find(R"({"rotation", "angle"})") != std::string::npos);
    CHECK(files["web/src/screens/main.tsx"].find(R"( rotate="studio::layer::rotate")") != std::string::npos);
    CHECK(files["web/src/screens/main.tsx"].find(R"(rotation: "angle")") != std::string::npos);
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
				add folder
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
    CHECK(messages(unsized) == std::vector<std::string>{"52: a picture's own size, when it isn't kept yet, is filled in by layer::update, which isn't declared in namespace studio"});
    CHECK(native.find(R"(collage_button(p, {.key = "/posters/:poster collage", .does = "screenshot", .label = "Screenshot", .icon = "screenshot"}))") != std::string::npos);
    CHECK(native.find(R"(collage_button(p, {.key = "/posters/:poster collage", .does = "add folder", .label = "Folder", .icon = ""}))") != std::string::npos);
    CHECK(web.find(R"(<CollageButton collage="/posters/:poster collage" does="add folder" label="Folder" />)") != std::string::npos);
    // Only where pictures go.
    std::string nowhere = collage_screen("\t\tlayout workspace\n" + fine);
    nowhere.replace(nowhere.find("\t\t\t\tpaste\n\t\t\t}"), std::string("\t\t\t\tpaste\n").size(), "");
    CHECK(std::ranges::count(messages(nowhere), std::string("49: this screen has no collage, paper or cards that say paste for this button to use")) == 1);
}

TEST_CASE("cards take pictures dropped, pasted or chosen, each a record made by their add, its file in their picture and its other parts in the fields named like them") {
    const std::string clippings = R"(namespace studio at / {
	entity poster {
		title  text
	}

	entity clipping {
		poster          poster  required
		name            text
		image           file
		picture_width   number  = 0
		picture_height  number  = 0
		picture_bytes   number  = 0
		picture_date    date
		picture_camera  text
	}

	command clipping::create
	command poster::create

	view poster_page per poster {
		title
		clippings = each clipping in poster {
			name  image
		}
	}

	screen "Clippings" /posters/:poster {
		layout workspace
		toolbar {
			group "Insert" {
				paste "Paste" icon paste key ctrl+shift+v
				add pictures "Pictures" icon picture
				add folder "Folder" icon folder key ctrl+shift+o
			}
		}
		main {
			cards poster_page.clippings {
				picture image fit
				name
				add clipping::create
				paste
			}
		}
	}
}
)";
    CHECK(messages(project + clippings).empty());
    auto files = built(project + clippings);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    // The screen's paste and add pictures buttons go to the cards, as it has no collage or paper.
    CHECK(web.find(R"(<PaperButton paper="/posters/:poster cards clippings" label="Paste" icon="paste" keys={["ctrl+shift+v"]} pastes />)") != std::string::npos);
    CHECK(web.find(R"(<PaperButton paper="/posters/:poster cards clippings" label="Pictures" icon="picture" chooses />)") != std::string::npos);
    CHECK(web.find(R"(paste={{ command: "studio::clipping::create", fields: { name: "name", picture: "image", picture_bytes: "picture_bytes", picture_camera: "picture_camera", picture_date: "picture_date", picture_height: "picture_height", picture_width: "picture_width" }, given: { poster: posterId } }} name="/posters/:poster cards clippings" />)") != std::string::npos);
    CHECK(native.find(R"(paper_button(p, {.key = "/posters/:poster cards clippings", .label = "Pictures", .icon = "picture", .chooses = true}))") != std::string::npos);
    // And a folder of them.
    CHECK(web.find(R"(<PaperButton paper="/posters/:poster cards clippings" label="Folder" icon="folder" keys={["ctrl+shift+o"]} chooses folder />)") != std::string::npos);
    CHECK(native.find(R"(paper_button(p, {.key = "/posters/:poster cards clippings", .label = "Folder", .icon = "folder", .keys = std::vector<std::string>{"ctrl+shift+o"}, .chooses = true, .folder = true}))") != std::string::npos);
    CHECK(native.find(R"(.add = "studio::clipping::create", .fields = {{"name", "name"}, {"picture", "image"}, {"picture_bytes", "picture_bytes"}, {"picture_camera", "picture_camera"}, {"picture_date", "picture_date"}, {"picture_height", "picture_height"}, {"picture_width", "picture_width"}}, .given = {{"poster", poster_id}}, .key = "/posters/:poster cards clippings", .paste = true})") != std::string::npos);
    // A part only where the entity keeps it, of the part's type; and add and paste go together, with a picture to keep.
    std::string wrong = project + clippings;
    wrong.replace(wrong.find("picture_bytes   number  = 0"), std::string("picture_bytes   number  = 0").size(), "picture_bytes   text");
    wrong.replace(wrong.find("picture_camera  text"), std::string("picture_camera  text").size(), "picture_camera  number");
    wrong.replace(wrong.find("\t\t\t\tpicture image fit\n"), std::string("\t\t\t\tpicture image fit\n").size(), "");
    CHECK(messages(wrong) == std::vector<std::string>{"47: a pasted picture is kept in the field the cards' picture line names, like picture image",
                                                     "47: clipping.picture_bytes keeps how many bytes a pasted picture is, so it's a number",
                                                     "47: clipping.picture_camera keeps the camera a pasted picture was taken with, so it's text"});
    std::string alone = project + clippings;
    alone.replace(alone.find("\t\t\t\tpaste\n\t\t\t}"), std::string("\t\t\t\tpaste\n").size(), "");
    alone.replace(alone.find("add clipping::create"), std::string("add clipping::create").size(), "add poster::create");
    CHECK(messages(alone) == std::vector<std::string>{"38: this screen has no collage, paper or cards that say paste for this button to use",
                                                     "39: this screen has no collage, paper or cards that say paste for this button to use",
                                                     "40: this screen has no collage, paper or cards that say paste for this button to use",
                                                     "47: these cards are clippings, so add names clipping::create",
                                                     "47: cards make records of pictures dropped or pasted on them, so add goes with paste"});
}

TEST_CASE("cards set as a mosaic of their pictures say so to both runtimes, and say which picture each fills its tile with") {
    const std::string pieces = R"(namespace studio at / {
	entity poster {
		title  text
	}

	entity clipping {
		poster  poster  required
		name    text
		image   file
	}

	view poster_page per poster {
		title
		clippings = each clipping in poster {
			name  image
		}
	}

	screen "Clipping" /posters/:poster/clippings/:clipping {
	}

	screen "Clippings" /posters/:poster {
		cards poster_page.clippings mosaic link /posters/:poster/clippings/:clipping {
			picture image
			name
		}
	}
}
)";
    CHECK(messages(project + pieces).empty());
    auto files = built(project + pieces);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(<Cards view={posterPage} list="clippings" columns={{ name: "Name" }} picture="image" mosaic link="/posters/:poster/clippings/:clipping")") != std::string::npos);
    CHECK(native.find(R"(.picture = "image", .mosaic = true)") != std::string::npos);
    // A mosaic is its pictures, each filling its tile.
    std::string bare = project + pieces;
    bare.replace(bare.find("\t\t\tpicture image\n"), std::string("\t\t\tpicture image\n").size(), "");
    CHECK(messages(bare) == std::vector<std::string>{"30: a mosaic is its cards' pictures, so its cards say which file each shows, like picture image"});
    std::string fitted = project + pieces;
    fitted.replace(fitted.find("picture image\n"), std::string("picture image").size(), "picture image fit");
    CHECK(messages(fitted) == std::vector<std::string>{"31: a mosaic's pictures each fill their tile, so its picture line says fill, or nothing"});
}

TEST_CASE("cards in columns say how many to both runtimes, and say why when they can't be") {
    const std::string pieces = R"(namespace studio at / {
	entity poster {
		title  text
	}

	entity clipping {
		poster          poster  required
		name            text
		image           file
		picture_width   number
		picture_height  number
	}

	view poster_page per poster {
		title
		clippings = each clipping in poster {
			name  image  picture_width  picture_height
		}
	}

	screen "Clipping" /posters/:poster/clippings/:clipping {
	}

	screen "Clippings" /posters/:poster {
		cards poster_page.clippings columns 5 link /posters/:poster/clippings/:clipping {
			picture image
			name
		}
	}
}
)";
    CHECK(messages(project + pieces).empty());
    auto files = built(project + pieces);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(picture="image" across={5} link="/posters/:poster/clippings/:clipping")") != std::string::npos);
    CHECK(native.find(R"(.picture = "image", .across = 5)") != std::string::npos);
    std::string bare = project + pieces;
    bare.replace(bare.find("\t\t\tpicture image\n"), std::string("\t\t\tpicture image\n").size(), "");
    CHECK(messages(bare) == std::vector<std::string>{"32: columns are their cards' pictures, so its cards say which file each shows, like picture image"});
    std::string both = project + pieces;
    both.replace(both.find("columns 5"), std::string("columns 5").size(), "mosaic columns 5");
    CHECK(messages(both) == std::vector<std::string>{"32: cards are a mosaic or in columns, not both"});
    std::string many = project + pieces;
    many.replace(many.find("columns 5"), std::string("columns 5").size(), "columns 20");
    CHECK(messages(many) == std::vector<std::string>{"32: cards are in 1 to 12 columns, like columns 5"});
}

TEST_CASE("cards under a heading for each month of a date, and the timeline drawn along them, say so to both runtimes, at a workspace's side while a toggle is on, and say why when they can't be") {
    const std::string pieces = R"(namespace studio at / {
	entity poster {
		title  text
	}

	entity clipping {
		poster        poster  required
		name          text
		image         file
		picture_date  date
	}

	view poster_page per poster {
		title
		clippings = each clipping in poster {
			order by picture_date descending
			name  image  picture_date
		}
	}

	screen "Clipping" /posters/:poster/clippings/:clipping {
	}

	screen "Clippings" /posters/:poster {
		cards poster_page.clippings mosaic by picture_date month link /posters/:poster/clippings/:clipping {
			picture image
			name
		}
		timeline poster_page.clippings by picture_date
	}
}
)";
    CHECK(messages(project + pieces).empty());
    auto files = built(project + pieces);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(mosaic by={{ field: "picture_date", unit: "month" }} link="/posters/:poster/clippings/:clipping")") != std::string::npos);
    CHECK(web.find(R"(name="/posters/:poster cards clippings" />)") != std::string::npos);
    CHECK(web.find(R"(<DatedTimeline view={posterPage} list="clippings" by="picture_date" unit="month" cards="/posters/:poster cards clippings" />)") != std::string::npos);
    CHECK(native.find(R"(.mosaic = true, .by = "picture_date", .unit = time_unit::month, .key = "/posters/:poster cards clippings"})") != std::string::npos);
    CHECK(native.find(R"(dated_timeline(p, {.view = poster_page, .list = "clippings", .by = "picture_date", .unit = time_unit::month, .cards = "/posters/:poster cards clippings"}))") != std::string::npos);
    // By year or day as well.
    std::string daily = project + pieces;
    daily.replace(daily.find("picture_date month"), std::string("picture_date month").size(), "picture_date day");
    CHECK(messages(daily).empty());
    CHECK(built(daily)["desktop/src/screens/main.cpp"].find(".unit = time_unit::day") != std::string::npos);
    // Under headings by a date the list is ordered by first, so each month's are together.
    std::string unordered = project + pieces;
    unordered.replace(unordered.find("order by picture_date descending"), std::string("order by picture_date descending").size(), "order by name");
    CHECK(messages(unordered) == std::vector<std::string>{"32: cards under headings by picture_date keep each month together, so their list is ordered by it first, like order by picture_date descending"});
    std::string named = project + pieces;
    named.replace(named.find("mosaic by picture_date"), std::string("mosaic by picture_date").size(), "mosaic by name");
    named.replace(named.find("order by picture_date"), std::string("order by picture_date").size(), "order by name");
    CHECK(messages(named)[0] == "32: cards are under headings by a date, and clipping.name isn't one");
    // A timeline by a date goes with cards under headings by it.
    std::string lone = project + pieces;
    lone.replace(lone.find(" by picture_date month"), std::string(" by picture_date month").size(), "");
    CHECK(messages(lone) == std::vector<std::string>{"36: a timeline by picture_date goes with the cards it scrolls, so this screen has cards poster_page.clippings by picture_date month"});
    // Year, month or day.
    std::string week = project + pieces;
    week.replace(week.find("picture_date month"), std::string("picture_date month").size(), "picture_date week");
    diagnostics said;
    (void)parse("main.one", week, said);
    CHECK(!said.empty());
    if (!said.empty()) CHECK(said.front().message == "expected year, month or day, as in by picture_date month, found 'week'");
    // In a workspace, at the side of its work while a toggle in its toolbar is on.
    std::string side = project + pieces;
    side.replace(side.find("	screen \"Clippings\" /posters/:poster {"), std::string("	screen \"Clippings\" /posters/:poster {").size(), R"(	screen "Clippings" /posters/:poster {
		layout workspace
		toolbar {
			group "View" small at end {
				toggle timeline "Timeline" icon timeline key t
				full screen "Full screen" icon fullscreen key f11
			}
		}
		main {)");
    side.replace(side.find("		timeline poster_page.clippings by picture_date"), std::string("		timeline poster_page.clippings by picture_date").size(), R"(		}
		side when timeline {
			timeline poster_page.clippings by picture_date
		})");
    CAPTURE(side);
    CHECK(messages(side).empty());
    auto sided = built(side);
    CAPTURE(sided["desktop/src/screens/main.cpp"]);
    CHECK(sided["desktop/src/screens/main.cpp"].find(R"(region{"side", [&] { if ((p.toggled("/posters/:poster timeline"))) return items(dated_timeline(p, {.view = poster_page)") != std::string::npos);
    CAPTURE(sided["web/src/screens/main.tsx"]);
    CHECK(sided["web/src/screens/main.tsx"].find(R"(<Toggle name="/posters/:poster timeline" label="Timeline" icon="timeline" keys={["t"]} />)") != std::string::npos);
}

TEST_CASE("a record's screen steps to the one before or after it in a list's own order, in its place, and says why when it can't") {
    const std::string pieces = R"(namespace studio at / {
	entity poster {
		title  text
	}

	entity clipping {
		poster  poster  required
		name    text
		image   file
	}

	view archive {
		posters = each poster {
			order by title
			title
		}
	}

	view poster_page per poster {
		title
		clippings = each clipping in poster {
			name  image
		}
	}

	screen "Posters" /posters {
		cards archive.posters link /posters/:poster {
			title
		}
	}

	screen "{poster_page.title}" /posters/:poster {
		link previous in archive.posters "Previous" icon previous key left
		link next in archive.posters "Next" icon next key right
		link /posters "Posters" key escape
	}
}
)";
    CHECK(messages(project + pieces).empty());
    auto files = built(project + pieces);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(<StepLink view={archive} list="posters" to="/posters/:poster" icon="previous" keys={["left"]}>Previous</StepLink>)") != std::string::npos);
    CHECK(web.find(R"(<StepLink view={archive} list="posters" to="/posters/:poster" next icon="next" keys={["right"]}>Next</StepLink>)") != std::string::npos);
    CHECK(native.find(R"(step(p, {.view = archive, .list = "posters", .route = "/posters/:poster"}, "Previous", "previous", std::vector<std::string>{"left"}))") != std::string::npos);
    CHECK(native.find(R"(step(p, {.view = archive, .list = "posters", .route = "/posters/:poster", .next = true}, "Next", "next", std::vector<std::string>{"right"}))") != std::string::npos);
    // A view of each poster's own, not one of them all.
    std::string per = project + pieces;
    per.replace(per.find("link next in archive.posters"), std::string("link next in archive.posters").size(), "link next in poster_page.clippings");
    CHECK(messages(per) == std::vector<std::string>{"41: view poster_page is one per poster, so its list changes with each record; step through a view of them all"});
    std::string missing = project + pieces;
    missing.replace(missing.find("link next in archive.posters"), std::string("link next in archive.posters").size(), "link next in catalog.posters");
    CHECK(messages(missing) == std::vector<std::string>{"41: there's no view catalog in namespace studio"});
    std::string unlisted = project + pieces;
    unlisted.replace(unlisted.find("link next in archive.posters"), std::string("link next in archive.posters").size(), "link next in archive.prints");
    CHECK(messages(unlisted) == std::vector<std::string>{"41: view archive has no list called prints"});
    // The list is of what the screen's address names.
    std::string other = project + pieces;
    other.replace(other.find("\t\tposters = each poster {"), std::string("\t\tposters = each poster {").size(), "\t\tposters = each clipping {");
    other.replace(other.find("\t\t\torder by title\n\t\t\ttitle\n"), std::string("\t\t\torder by title\n\t\t\ttitle\n").size(), "\t\t\torder by name\n\t\t\tname\n");
    other.replace(other.find("\t\t\ttitle\n\t\t}\n\t}\n\n\tscreen \"{poster_page"), std::string("\t\t\ttitle\n").size(), "\t\t\tname\n");
    CHECK(messages(other) == std::vector<std::string>{"40: archive.posters lists clippings, and this screen's address names a poster, so it can't step through them",
                                                     "41: archive.posters lists clippings, and this screen's address names a poster, so it can't step through them"});
    // And its address names one.
    std::string nameless = project + pieces;
    nameless.replace(nameless.find("\t\tlink next in archive.posters"), 0, "\t}\n\n\tscreen \"Every poster\" /every {\n");
    CHECK(messages(nameless) == std::vector<std::string>{"44: a link next steps from one record to another, so its screen's address names one, like /things/:thing"});
}

TEST_CASE("a record's screen steps through the list it was opened from when its link names none, or else the first whose rows open it, and says why when none does") {
    const std::string pieces = R"(namespace studio at / {
	entity poster {
		title  text
		year   number
	}

	view archive {
		posters = each poster {
			order by title
			title  year
		}
	}

	view by_year {
		posters = each poster {
			order by year descending
			title  year
		}
	}

	screen "Posters" /posters {
		cards archive.posters link /posters/:poster {
			title
		}
	}

	screen "Years" /years {
		table by_year.posters link /posters/:poster {
			title
			year
		}
	}

	screen "{poster.title}" /posters/:poster {
		link previous "Previous" icon previous key left
		link next "Next" icon next key right
	}
}
)";
    CHECK(messages(project + pieces).empty());
    auto files = built(project + pieces);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    // Through the list it was opened from, the archive's when it wasn't.
    CHECK(web.find(R"(<StepLink view={archive} list="posters" to="/posters/:poster" opened icon="previous" keys={["left"]}>Previous</StepLink>)") != std::string::npos);
    CHECK(web.find(R"(<StepLink view={archive} list="posters" to="/posters/:poster" next opened icon="next" keys={["right"]}>Next</StepLink>)") != std::string::npos);
    CHECK(native.find(R"(step(p, {.view = archive, .list = "posters", .route = "/posters/:poster", .next = true, .opened = true}, "Next", "next", std::vector<std::string>{"right"}))") != std::string::npos);
    // No list's rows open it.
    std::string unopened = project + pieces;
    unopened.replace(unopened.find(" link /posters/:poster {\n\t\t\ttitle\n\t\t}"), std::string(" link /posters/:poster").size(), "");
    unopened.replace(unopened.find(" link /posters/:poster {\n\t\t\ttitle\n\t\t\tyear"), std::string(" link /posters/:poster").size(), "");
    CHECK(messages(unopened) == std::vector<std::string>{"42: a link previous with no list steps through the one its record was opened from, and no list's rows open /posters/:poster; say which it steps through, like link previous in library.things",
                                                        "43: a link next with no list steps through the one its record was opened from, and no list's rows open /posters/:poster; say which it steps through, like link next in library.things"});
    // Only a table of each poster's own opens it: through the list it was opened from, and nothing else.
    std::string own = project + pieces;
    own.replace(own.find(" link /posters/:poster {\n\t\t\ttitle\n\t\t}"), std::string(" link /posters/:poster").size(), "");
    own.replace(own.find("\tview by_year {"), std::string("\tview by_year {").size(), "\tview by_year per poster {");
    own.replace(own.find("\tscreen \"Years\" /years {"), std::string("\tscreen \"Years\" /years {").size(), "\tscreen \"Years\" /years/:poster {");
    CHECK(messages(own).empty());
    CHECK(built(own)["desktop/src/screens/main.cpp"].find(R"(step(p, {.view = {}, .list = "", .route = "/posters/:poster", .next = true, .opened = true})") != std::string::npos);
}

TEST_CASE("a side a toggle opens is a drawer over a picture alone, and details say a number of bytes as KB, MB or GB") {
    const std::string pieces = R"(namespace studio at / {
	entity piece {
		name   text
		image  file
		bytes  number = 0
		taken  date
	}

	view piece_page per piece {
		name  image  bytes  taken
	}

	screen "{piece_page.name}" /pieces/:piece {
		layout workspace
		toolbar {
			group "View" small at end {
				toggle about "About" icon info key i
			}
		}
		main {
			picture piece_page.image fit alone
		}
		side when about {
			details piece_page {
				name  taken "Taken"  bytes "Size" as bytes
			}
		}
	}
}
)";
    CHECK(messages(project + pieces).empty());
    auto files = built(project + pieces);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(<Region name="side" drawer>)") != std::string::npos);
    CHECK(web.find(R"(<Details view={piecePage} fields={[["name", "Name"], ["taken", "Taken"], ["bytes", "Size"]]} bytes={["bytes"]} />)") != std::string::npos);
    CHECK(native.find(R"(details(p, piece_page, {{"name", "Name"}, {"taken", "Taken"}, {"bytes", "Size", true}}))") != std::string::npos);
    CHECK(native.find(R"(return view(); }(), true})") != std::string::npos);
    // Only a number is said as bytes.
    std::string named = project + pieces;
    named.replace(named.find("name  taken \"Taken\""), std::string("name").size(), "name as bytes");
    CHECK(messages(named) == std::vector<std::string>{"32: name isn't a number, so it isn't said as bytes"});
}

TEST_CASE("a step steps by itself every so many seconds while a toggle of its screen's is on, as a slideshow does") {
    const std::string pieces = R"(namespace studio at / {
	entity piece {
		name   text
		image  file
	}

	view archive {
		pieces = each piece {
			order by name
			name  image
		}
	}

	view piece_page per piece {
		name  image
	}

	screen "Pieces" /pieces {
		cards archive.pieces link /pieces/:piece {
			picture image
			name
		}
	}

	screen "{piece_page.name}" /pieces/:piece {
		layout workspace
		toolbar {
			group "Piece" {
				link next "Next" icon next key right every 4 seconds while slideshow
				toggle slideshow "Slideshow" icon play key s
			}
		}
		main {
			picture piece_page.image fit alone
		}
	}
}
)";
    CHECK(messages(project + pieces).empty());
    auto files = built(project + pieces);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(<StepLink view={archive} list="pieces" to="/pieces/:piece" next opened every={4} whileOn="/pieces/:piece slideshow" picture="image" icon="next" keys={["right"]}>Next</StepLink>)") != std::string::npos);
    CHECK(native.find(R"(step(p, {.view = archive, .list = "pieces", .route = "/pieces/:piece", .next = true, .opened = true, .every = 4, .while_on = "/pieces/:piece slideshow", .picture = "image"}, "Next", "next", std::vector<std::string>{"right"}))") != std::string::npos);
    // Its toggle is the screen's own, and it waits some time.
    std::string missing = project + pieces;
    missing.replace(missing.find("while slideshow"), std::string("while slideshow").size(), "while playing");
    missing.replace(missing.find("every 4 seconds"), std::string("every 4 seconds").size(), "every 0 seconds");
    CHECK(messages(missing) == std::vector<std::string>{"36: a link next steps every so many seconds, more than none, like every 4 seconds",
                                                        "36: there's no toggle playing on this screen for it to step while; add one, like toggle playing \"Slideshow\" icon play key s"});
    std::string minutes = project + pieces;
    minutes.replace(minutes.find("every 4 seconds"), std::string("every 4 seconds").size(), "every 4 minutes");
    diagnostics said;
    (void)parse("main.one", minutes, said);
    REQUIRE(said.size() == 1);
    CHECK(said.front().where.line == 36);
    CHECK(said.front().message == "it steps every so many seconds, like every 4 seconds, not minutes");
}

TEST_CASE("a picture alone is all of a workspace's screen, its bars over it, zoomed by the screen's zoom buttons") {
    const std::string pieces = R"(namespace studio at / {
	entity clipping {
		name   text
		image  file
	}

	view archive {
		clippings = each clipping {
			order by name
			name
		}
	}

	view clipping_page per clipping {
		name  image
	}

	screen "{clipping_page.name}" /clippings/:clipping {
		layout workspace
		toolbar {
			link next in archive.clippings "Next" icon next key right
			zoom in "Larger" icon zoom_in
			zoom to fit "Fit" icon zoom_fit
		}
		main {
			picture clipping_page.image fit alone
		}
		status {
			text "{clipping_page.name}"
		}
	}
}
)";
    CHECK(messages(project + pieces).empty());
    auto files = built(project + pieces);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(native.find(R"(region{"main", items(picture_alone(p, at(clipping_page, "image").as_string(), "/clippings/:clipping collage"))})") != std::string::npos);
    CHECK(native.find(R"(collage_button(p, {.key = "/clippings/:clipping collage", .does = "zoom in", .label = "Larger", .icon = "zoom_in"}))") != std::string::npos);
    CHECK(web.find(R"(<Layout name="workspace" alone>)") != std::string::npos);
    CHECK(web.find(R"(<Picture source={(clippingPage.data?.["image"] ?? null)} fit alone zoom="/clippings/:clipping collage" />)") != std::string::npos);
    CHECK(web.find(R"(<CollageButton collage="/clippings/:clipping collage" does="zoom to fit" label="Fit" icon="zoom_fit" />)") != std::string::npos);
    auto changed = [&](const std::string& from, const std::string& to) {
        std::string source = project + pieces;
        source.replace(source.find(from), from.size(), to);
        return messages(source);
    };
    // Shown whole, the only line of main, on a workspace's screen.
    CHECK(changed("image fit alone", "image fill alone") == std::vector<std::string>{"33: a picture alone is shown whole, fitted in, so it says fit alone"});
    CHECK(changed("\t\t\tpicture clipping_page.image fit alone\n", "\t\t\tpicture clipping_page.image fit alone\n\t\t\ttext \"{clipping_page.name}\"\n") ==
          std::vector<std::string>{"33: a picture alone is all of its screen, so it's the only line in main, as in main { picture clipping_page.image fit alone }"});
    CHECK(changed("\t\tlayout workspace\n", "\t\tlayout two_columns\n") ==
          std::vector<std::string>{"27: layout two_columns has no region toolbar; its regions are main and side", "35: layout two_columns has no region status; its regions are main and side",
                                   "33: a picture alone is all of a workspace's screen, its bars drawn over it, so its screen says layout workspace"});
    // Without it, there's nothing for a zoom button to zoom.
    CHECK(changed("image fit alone", "image fit") == std::vector<std::string>{"29: this screen has no collage, nor a picture alone, for this button to zoom",
                                                                            "30: this screen has no collage, nor a picture alone, for this button to zoom"});
}

TEST_CASE("a copy naming nothing puts the picture a screen shows alone on the clipboard") {
    const std::string pieces = R"(namespace studio at / {
	entity clipping {
		name   text
		image  file
	}

	view clipping_page per clipping {
		name  image
	}

	screen "{clipping_page.name}" /clippings/:clipping {
		layout workspace
		toolbar {
			copy "Copy" icon copy key ctrl+c
		}
		main {
			picture clipping_page.image fit alone
		}
	}
}
)";
    CHECK(messages(project + pieces).empty());
    auto files = built(project + pieces);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(native.find(R"(.does = "copy", .label = "Copy", .icon = "copy")") != std::string::npos);
    CHECK(web.find(R"(<CollageButton collage="/clippings/:clipping collage" does="copy" label="Copy" icon="copy")") != std::string::npos);
    // Without a picture alone, there's nothing for it to copy.
    std::string shown = project + pieces;
    shown.replace(shown.find("image fit alone"), std::string("image fit alone").size(), "image fit");
    CHECK(messages(shown) == std::vector<std::string>{"21: this screen has no picture alone for this button to copy"});
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

    // Cut: copied, then taken away, so it's the pick's, kept as SVG, and the collage deletes.
    auto cut = [&](const std::string& as, const std::string& deletes) {
        return on_screen("\t\tpick layer from poster_page.layers\n\t\tcollage poster_page.layers size width height {\n" + deletes + "\t\t\tover layer {\n\t\t\t\tcut layer as " + as + " \"Cut\" icon cut key ctrl+x\n\t\t\t}\n\t\t}\n");
    };
    CHECK(messages(cut("svg", "")) == std::vector<std::string>{"52: a cut takes the collage's pick away, so it names its pick, as in cut layer as svg, and the collage says delete layer::delete"});
    CHECK(messages(cut("picture", "\t\t\tdelete layer::delete\n")) == std::vector<std::string>{"53: a cut is pasted back as what it was, so it's kept as svg, as in cut layer as svg"});
    auto cuts = built(cut("svg", "\t\t\tdelete layer::delete\n"));
    CAPTURE(cuts["web/src/screens/main.tsx"]);
    CHECK(cuts["web/src/screens/main.tsx"].find(R"(does="cut svg picked" label="Cut" icon="cut" keys={["ctrl+x"]})") != std::string::npos);
    CHECK(cuts["desktop/src/screens/main.cpp"].find(R"(.does = "cut svg picked", .label = "Cut", .icon = "cut")") != std::string::npos);
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
                                                       "27: on open runs a command, like poster::create { width = screen.width } then open /posters/:poster, opens a row, like open /posters/:poster for posters, "
                                                       "picks one of a choice, like toggle mode::edit, or turns a toggle on or off, like toggle layers off"});
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

TEST_CASE("a button can ask first, its key too, the question reading the view and its own words saying yes") {
    auto files = built(layered_screen(R"(		layer::delete "Remove" icon delete key delete confirm "Remove it from {poster_page.title}?" then open /posters/:poster
		poster::arrange "Ink" { tint = tint::ink } confirm "Make it ink?"
)"));
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    // Its question reads the view, and a delete's answer is drawn as a danger.
    CHECK(web.find(R"(<Command name="studio::layer::delete" label="Remove" icon="delete" keys={["delete"]} thenOpen="/posters/:poster" confirm={`Remove it from ${show(posterPage.data?.["title"])}?`} destroys />)") != std::string::npos);
    CHECK(web.find(R"(confirm={"Make it ink?"} />)") != std::string::npos);
    CHECK(native.find(R"(.then_open = "/posters/:poster", .confirm = said({words("Remove it from "), live(poster_page, "title"), words("?")}), .destroys = true})") != std::string::npos);
    CHECK(native.find(R"(.confirm = "Make it ink?"})") != std::string::npos);
    // What it reads is checked as any text is.
    CHECK(messages(layered_screen(R"(		layer::delete "Remove" confirm "Remove it from {build.name}?"
)")) == std::vector<std::string>{"43: {build.name} isn't something a site is built from; it's {build.version} or {build.commit}"});
}

TEST_CASE("a layout kept on a record, chosen from a toolbar, shows one of two cards on a list, and the timeline goes with whichever is shown") {
    const std::string pieces = R"(namespace studio at / {
	enum layout { mosaic "Magazine"  columns "Columns" }

	entity poster {
		title   text  required
		layout  layout = layout::mosaic
	}

	entity clipping {
		poster          poster  required
		name            text
		image           file
		picture_date    date
		picture_width   number
		picture_height  number
		starred         boolean
	}

	command poster::arrange {
		changes layout
	}

	view poster_page per poster {
		title  layout
		clippings = each clipping in poster {
			order by picture_date descending
			name  image  picture_date  picture_width  picture_height
		}
	}

	view starred_page per poster {
		layout
		clippings = each clipping in poster {
			order by picture_date descending
			name  image  picture_date  picture_width  picture_height
		}
	}

	screen "Clipping" /posters/:poster/clippings/:clipping {
	}

	screen "Clippings" /posters/:poster {
		layout workspace
		toolbar {
			group "Layout" small rows of 2 {
				poster::arrange "Magazine" icon mosaic { layout = layout::mosaic } on when poster_page.layout == layout::mosaic
				poster::arrange "Columns" icon columns { layout = layout::columns } on when poster_page.layout == layout::columns
			}
		}
		main {
			cards poster_page.clippings mosaic by picture_date month link /posters/:poster/clippings/:clipping when poster_page.layout == layout::mosaic {
				picture image
				name
			}
			cards poster_page.clippings columns 5 by picture_date month link /posters/:poster/clippings/:clipping when poster_page.layout == layout::columns {
				picture image
				name
			}
		}
		side {
			timeline poster_page.clippings by picture_date
		}
	}

	screen "Starred" /posters/:poster/starred {
		cards starred_page.clippings mosaic by picture_date month link /posters/:poster/clippings/:clipping when starred_page.layout == layout::mosaic {
			picture image
			name
		}
		cards starred_page.clippings columns 5 by picture_date month link /posters/:poster/clippings/:clipping when starred_page.layout == layout::columns {
			picture image
			name
		}
	}
}
)";
    CHECK(messages(project + pieces).empty());
    auto files = built(project + pieces);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    // The choice is the record's, set by its tools, each pressed while it's the one.
    CHECK(web.find(R"(<Command name="studio::poster::arrange" id={posterId} label="Columns" icon="columns" given={{ "layout": "columns" }} pressed={posterPage.status === "live" && (((posterPage.data?.["layout"] ?? null) === "columns"))} />)") != std::string::npos);
    CHECK(native.find(R"(.icon = "columns", .pressed = (ready(poster_page) && same(at(poster_page, "layout"), value("columns")))})") != std::string::npos);
    // Either cards, as it says, under one name, which the timeline goes with whichever is shown.
    CHECK(native.find(R"(if ((ready(poster_page) && same(at(poster_page, "layout"), value("columns")))) return cards(p, {.view = poster_page, .list = "clippings", .columns = {{"name", "Name"}}, .link = "/posters/:poster/clippings/:clipping", .picture = "image", .across = 5, .by = "picture_date", .unit = time_unit::month, .key = "/posters/:poster cards clippings"});)") != std::string::npos);
    CHECK(web.find(R"(across={5} by={{ field: "picture_date", unit: "month" }} link="/posters/:poster/clippings/:clipping" name="/posters/:poster cards clippings" />)") != std::string::npos);
    CHECK(web.find(R"(<DatedTimeline view={posterPage} list="clippings" by="picture_date" unit="month" cards="/posters/:poster cards clippings" />)") != std::string::npos);
    CHECK(native.find(R"(.cards = "/posters/:poster cards clippings"})") != std::string::npos);
    // Another screen reads the same choice through a view of its own.
    CHECK(native.find(R"(same(at(starred_page, "layout"), value("columns")))) return cards(p, {.view = starred_page)") != std::string::npos);
}

TEST_CASE("a mode the person switches to: only when takes a toolbar's line away, and on open picks the mode a screen starts in") {
    const std::string source = project + R"(namespace studio at / {
	enum mode {
		read  "Read"
		edit  "Edit"
	}

	entity poster {
		title   text
	}

	command poster::create
	command poster::rename {
		changes title
	}

	view poster_page per poster {
		title
		named = count(poster where title != none)
	}

	screen "Poster" /posters/:poster {
		layout workspace
		on open toggle mode::edit when poster_page.named == 0
		on open toggle mode::read
		bar {
			undo "Undo" icon undo key ctrl+z when mode == mode::edit
		}
		toolbar {
			group "Edit" {
				toggle mode::edit "Edit" icon edit only when mode == mode::read
				toggle mode::read "Done" icon check only when mode == mode::edit
			}
			group "Write" only when mode == mode::edit {
				poster::rename "Untitled" { title = none }
			}
			group "Kept" when mode == mode::edit {
				poster::rename "Plain" { title = none }
			}
		}
		main {
			text "{poster_page.title}"
		}
	}
}
)";
    CHECK(messages(source).empty());
    auto files = built(source);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    // Picked on open, the first line that applies.
    CHECK(web.find(R"({ choose: { name: "/posters/:poster mode", choice: "edit" }, when: ((posterPage.data?.["named"] ?? null) === 0) }, { choose: { name: "/posters/:poster mode", choice: "read" } })") != std::string::npos);
    CHECK(native.find(R"(opening{.choose = "/posters/:poster mode", .choice = "read"})") != std::string::npos);
    // Only when: gone, not off; when alone: there, but off.
    CHECK(web.find(R"({((chosenMode === "read")) && (<>)") != std::string::npos);
    CHECK(web.find(R"(<Applies when={((chosenMode === "edit"))}>)") != std::string::npos);
    CHECK(web.find(R"(<Applies when={((chosenMode === "read"))}>)") == std::string::npos);
    CHECK(native.find(R"(if ((same(value(p.chosen("/posters/:poster mode", "edit")), value("edit")))) return group(p, "Write")") != std::string::npos);
}

TEST_CASE("on open turns a toggle on or off, as its press would, and only one the screen has") {
    const std::string source = project + R"(namespace studio at / {
	entity poster {
		title   text
	}

	view poster_page per poster {
		title
		named = count(poster where title != none)
	}

	screen "Poster" /posters/:poster {
		layout workspace
		on open toggle layers off when poster_page.named == 0
		toolbar {
			group "View" {
				toggle layers "Layers" icon layers
			}
		}
		main {
			text "{poster_page.title}"
		}
		side when layers {
			text "Layers"
		}
	}
}
)";
    CHECK(messages(source).empty());
    auto files = built(source);
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"({ turn: { name: "/posters/:poster layers", on: false }, when: ((posterPage.data?.["named"] ?? null) === 0) })") != std::string::npos);
    CHECK(native.find(R"(opening{.turn = "/posters/:poster layers", .on = false, .when = )") != std::string::npos);

    std::string missing = source;
    missing.replace(missing.find("toggle layers off"), 17, "toggle outline on");
    CHECK(messages(missing) == std::vector<std::string>{"20: there's no toggle outline on this screen, on or off, for on open to turn"});
}

TEST_CASE("a workspace's bar holds one group, at its end, before light and dark") {
    CHECK(messages(screen("\t\tlayout workspace\n\t\tbar {\n\t\t\tgroup \"Mode\" {\n\t\t\t\tposter::create \"New\"\n\t\t\t}\n\t\t}\n")) ==
          std::vector<std::string>{"24: a group in a bar sits at its end, before the switch between light and dark, as group \"Mode\" at end { ... }"});
    CHECK(messages(screen("\t\tlayout workspace\n\t\tbar {\n\t\t\tgroup \"Mode\" at end {\n\t\t\t\tposter::create \"New\"\n\t\t\t}\n\t\t\tgroup \"More\" at end {\n\t\t\t\tposter::create \"Again\"\n\t\t\t}\n\t\t}\n")) ==
          std::vector<std::string>{"27: a bar has one group at its end, so this one's lines go in the first"});
    auto files = built(layered_screen("\t\tlayout workspace\n\t\tbar {\n\t\t\tundo \"Undo\" icon undo\n\t\t\tgroup \"Mode\" at end {\n\t\t\t\tredo \"Redo\" icon redo\n\t\t\t}\n\t\t}\n"));
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find("<Region name=\"barEnd\">") != std::string::npos);
    CHECK(web.find("<Group") == std::string::npos);
    CHECK(web.find("Undo") < web.find("barEnd"));
    CHECK(web.find("barEnd") < web.find("Redo"));
    CHECK(native.find("region{\"bar_end\", along(history(p, true, \"Redo\"") != std::string::npos);
}
