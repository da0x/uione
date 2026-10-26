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

namespace {

    const std::string collaged = R"(namespace studio at / {
	entity poster {
		title    text    required
		width    number  = 1600
		height   number  = 900
		slide_x  number  = 0.5
		slide_y  number  = 0.5
	}

	entity layer {
		poster   poster  required
		depth    number  = 0
		picture  file
		picture_width   number  = 0
		picture_height  number  = 0
		x        number  = 0
		y        number  = 0
		w        number  = 400
		h        number  = 300
	}

	command poster::create
	command poster::slide {
		changes slide_x slide_y
	}
	command layer::place {
		changes x y w h
	}
	command layer::create
	command layer::delete
	command layer::update
	view poster_page per poster {
		title  width  height  slide_x  slide_y
		layers = each layer in poster {
			order by depth
			picture  x  y  w  h
		}
	}
)";

    std::string collage_screen(const std::string& body) { return project + collaged + "\n\tscreen \"Poster\" /posters/:poster {\n" + body + "\t}\n}\n"; }

}  // namespace

TEST_CASE("a collage is a view's list as layers, as big as two of its values, placed and slid by commands, with buttons over the picked layer") {
    const std::string fine = R"(		pick layer from poster_page.layers
		collage poster_page.layers size width height {
			place layer::place
			slide poster::slide
			snap to edges
			empty "Add a picture to start"
			over layer {
				layer::delete "Delete" icon delete key delete
			}
		}
)";
    CHECK(messages(collage_screen(fine)).empty());
    CHECK(messages(collage_screen(R"(		collage poster_page.layers size wide tall {
			place poster::slide
			slide layer::place
			over layer {
				text "Not a button"
			}
		}
)")) == std::vector<std::string>{"48: view poster_page has no wide, which says how big the collage is; add it to the view",
                                   "48: view poster_page has no tall, which says how big the collage is; add it to the view",
                                   "49: a collage's layers are layers, so place names a command on layer, like layer::place",
                                   "50: a collage's background slides as the view poster_page says, so by a command on what the view is per, like poster::slide",
                                   "51: over names the screen's pick of these layers, like pick layer from poster_page.layers",
                                   "52: what's over a picked layer are buttons, like layer::delete \"Delete\" icon delete, or crop \"Crop\""});
    auto files = built(collage_screen(fine));
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(<Collage view={posterPage} list="layers" place="studio::layer::place" slide="studio::poster::slide" snap empty="Add a picture to start" pick="/posters/:poster layer" parent="poster" name="/posters/:poster layer" subject={posterId}>)") !=
          std::string::npos);
    CHECK(web.find(R"(<Command name="studio::layer::delete" id={pickedLayer?.id as string | undefined} label="Delete" icon="delete" keys={["delete"]} when={pickedLayer !== null} />)") != std::string::npos);
    CHECK(native.find(R"(collage(p, {.view = poster_page, .list = "layers", .place = "studio::layer::place", .slide = "studio::poster::slide", .snap = true, .empty = "Add a picture to start", .pick = "/posters/:poster layer", .over = {command_button{.command = "studio::layer::delete", .label = "Delete", .id = picked_layer["id"].as_string(), .when = !none(picked_layer), .icon = "delete", .keys = std::vector<std::string>{"delete"}}}, .subject = poster_id, .parent = "poster", .key = "/posters/:poster layer"})") !=
          std::string::npos);
}


TEST_CASE("pictures dropped, pasted or taken from the screen are made by a collage's add, kept in a file field, their own size in two numbers") {
    const std::string fine = R"(		toolbar {
			group "Insert" {
				paste "Paste" icon paste key ctrl+shift+v
				screenshot "Screenshot" icon screenshot
			}
		}
		main {
			collage poster_page.layers size width height {
				add layer::create
				paste into picture sized picture_width picture_height
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
				paste into title sized x title
			}
		}
)")) == std::vector<std::string>{"52: this screen has no collage that says crop layer::crop for this button to use",
                                   "57: pasted pictures are made by the collage's add, so it says one too, like add layer::create",
                                   "57: layer.title holds what's pasted, so it's a file",
                                   "57: layer.title keeps a picture's own size, so it's a number"});
    auto files = built(collage_screen("\t\tlayout workspace\n" + fine));
    const std::string& web = files["web/src/screens/main.tsx"];
    const std::string& native = files["desktop/src/screens/main.cpp"];
    CAPTURE(web);
    CAPTURE(native);
    CHECK(web.find(R"(<CollageButton collage="/posters/:poster collage" does="paste" label="Paste" icon="paste" keys={["ctrl+shift+v"]} />)") != std::string::npos);
    // One whose own size isn't kept yet has it filled in by the layer's update.
    CHECK(web.find(R"(pasteInto="picture" sizedWidth="picture_width" sizedHeight="picture_height" update="studio::layer::update")") != std::string::npos);
    CHECK(native.find(R"(.paste_into = "picture", .sized_width = "picture_width", .sized_height = "picture_height", .update = "studio::layer::update"})") != std::string::npos);
    std::string unsized = collage_screen("\t\tlayout workspace\n" + fine);
    unsized.erase(unsized.find("\tcommand layer::update\n"), std::string("\tcommand layer::update\n").size());
    CHECK(messages(unsized) == std::vector<std::string>{"57: a picture's own size, when it isn't kept yet, is filled in by layer::update, which isn't declared in namespace studio"});
    CHECK(native.find(R"(collage_button(p, {.key = "/posters/:poster collage", .does = "screenshot", .label = "Screenshot", .icon = "screenshot"}))") != std::string::npos);
}

TEST_CASE("a collage is copied or saved as a picture, all of it or its picked layer, and keeps a small picture of itself on what its view is per") {
    std::string kept = project + collaged;
    kept.replace(kept.find("\t\tslide_y  number  = 0.5\n"), 0, "\t\tpreview  file\n");
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
    CHECK(lines == std::vector<std::string>{"53: posters isn't a collage on this screen, or its pick; a picture is made of one, like poster_page",
                                            "55: a collage's preview is kept on poster, so by a command on it, like poster::preview",
                                            "55: poster.title holds its preview, so it's a file", "55: a preview is from 16 to 4096 wide"});
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
    CHECK(web.find(R"({(((Number(screenSize().width) - Number(2) * Math.floor(Number(screenSize().width) / Number(2))) === 0)) && <Text>An even screen</Text>})") != std::string::npos);
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
}
