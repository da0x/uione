// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// one::desktop's tests: colors, text layout, and scenes drawn offscreen compared with the
// images in tests/golden. ONE_DESKTOP_GOLDEN=write draws them again; each is then looked at
// before it's committed.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <ranges>
#include <fstream>
#include <span>
#include <thread>

#include "check.hpp"
#include "one/desktop/icons.hpp"
#include "one/desktop/files.hpp"
#include "one/desktop/screenshot.hpp"
#include "one/desktop/canvas.hpp"
#include "one/desktop/editing.hpp"
#include "one/desktop/state.hpp"
#include "one/desktop/ui.hpp"
#include "app_internal.hpp"
#include "offscreen.hpp"
#include "photos.hpp"
#include "fonts.hpp"
#include "ui_internal.hpp"

namespace {

	const std::filesystem::path golden_folder = ONE_DESKTOP_TESTS "/golden";

	// A scene drawn and compared with its golden image, allowing small differences
	// from one GPU's rounding to another's.
	void matches_golden(const std::string& name, one::desktop::size size, double scale, const std::function<void(one::desktop::canvas&)>& paint) {
		auto drawn = one::desktop::offscreen::draw(size, scale, one::desktop::color::hex(0xf7f8fa), paint);
		auto file = golden_folder / (name + ".png");
		const char* mode = std::getenv("ONE_DESKTOP_GOLDEN");
		if (mode && std::string(mode) == "write") {
			one::desktop::offscreen::write_png(file, drawn);
			std::println("     wrote {}", file.string());
			return;
		}
		auto golden = one::desktop::offscreen::read_png(file);
		CHECK_SAYING(golden.has_value(), "no golden image " + file.string() + "; draw it with ONE_DESKTOP_GOLDEN=write and look at it");
		if (!golden) return;
		std::size_t different = one::desktop::offscreen::differences(drawn, *golden, 3);
		std::size_t allowed = drawn.rgba.size() / 4 / 2000;  // a twentieth of a percent
		if (different > allowed) one::desktop::offscreen::write_png(std::filesystem::temp_directory_path() / (name + ".drawn.png"), drawn);
		CHECK_SAYING(different <= allowed, std::format("{} pixels differ from {}, more than {}; what was drawn is in {}", different, file.string(), allowed,
		                                               (std::filesystem::temp_directory_path() / (name + ".drawn.png")).string()));
	}

	const one::desktop::text_style body{.family = "Noto Sans", .size = 15};

	void boxes(one::desktop::canvas& c) {
		using one::desktop::color;
		c.shadow({24, 24, 160, 96}, 10, 16, color::hex(0x0e1726, 0.2f), {0, 6});
		c.box({24, 24, 160, 96}, {.fill = color::hex(0xffffff), .border = color::hex(0xdde2ea), .border_width = 1, .radius = 10});
		c.box({208, 24, 96, 96}, {.fill = color::hex(0x2457d6), .radius = 48});
		c.box({328, 24, 96, 96}, {.fill = color::hex(0xc2312d, 0.5f), .border = color::hex(0xc2312d), .border_width = 3, .radius = 0});
		auto clipped = c.clip({24, 140, 200, 40});
		c.box({0, 120, 400, 80}, {.fill = color::hex(0x1a7f37)});
	}

	void words(one::desktop::canvas& c) {
		std::vector<one::desktop::text_span> spans{
		    {"Renaming a phase keeps its old ", body},
		    {"name", {.family = "Noto Sans Mono", .size = 14, .color = one::desktop::color::hex(0x2457d6)}},
		    {", so a new phase titled the same is ", body},
		    {"refused", {.family = "Noto Sans", .size = 15, .weight = 700}},
		    {". Underlined, ", body},
		    {"struck", {.family = "Noto Sans", .size = 15, .strike = true}},
		    {" and ", body},
		    {"linked", {.family = "Noto Sans", .size = 15, .color = one::desktop::color::hex(0x2457d6), .underline = true}},
		    {".", body},
		};
		c.text(one::desktop::lay_out(spans, 300, c.scale()), {20, 16});
	}

}  // namespace

TEST("a screen on its side is as tall as its mode is wide") {
	one::desktop::output o;
	o.mode = {2560, 1440};
	CHECK((o.pixels() == one::desktop::size{2560, 1440}));
	o.transform = WL_OUTPUT_TRANSFORM_90;
	CHECK((o.pixels() == one::desktop::size{1440, 2560}));
	o.transform = WL_OUTPUT_TRANSFORM_180;
	CHECK((o.pixels() == one::desktop::size{2560, 1440}));
	o.transform = WL_OUTPUT_TRANSFORM_FLIPPED_270;
	CHECK((o.pixels() == one::desktop::size{1440, 2560}));
}

TEST("a drawing takes the space it's given, and paints there") {
	one::desktop::rect told{};
	auto drawn = one::desktop::offscreen::draw({200, 100}, 1, one::desktop::color::hex(0xffffff), [&](one::desktop::canvas& c) {
		auto root = one::desktop::column(one::desktop::text("Above"), one::desktop::drawing([&](one::desktop::canvas& on, one::desktop::rect area) {
			            told = area;
			            on.box(area, {.fill = one::desktop::color::hex(0xff0000)});
		            }).grow())
		                .take();
		root->place({0, 0, 200, 100});
		root->paint(c);
	});
	CHECK(told.width == 200);
	CHECK(told.y > 0);
	CHECK(told.y + told.height == 100);
	auto at = [&](int x, int y) { return &drawn.rgba[static_cast<std::size_t>((y * drawn.width + x) * 4)]; };
	CHECK(at(100, 95)[0] == 255);
	CHECK(at(100, 95)[1] == 0);
	CHECK(at(100, 2)[1] != 0);  // the text above it isn't painted over
}

TEST("a picture and text turned are drawn turned about where they're turned, and the rest as it was") {
	// A red bar 80 wide and 10 high across the middle, turned a quarter about its middle:
	// it stands up, so its middle's red, above and below it are red, and its ends aren't.
	std::vector<std::uint8_t> red;
	for (int i = 0; i < 8 * 8; ++i) red.insert(red.end(), {255, 0, 0, 255});
	auto bar = one::desktop::picture::of(8, 8, red);
	auto drawn = one::desktop::offscreen::draw({100, 100}, 1, one::desktop::color::hex(0xffffff), [&](one::desktop::canvas& c) {
		auto turned = c.turned({50, 50}, 90);
		c.picture(bar, {10, 45, 80, 10});
	});
	auto at = [&](int x, int y) { return &drawn.rgba[static_cast<std::size_t>((y * drawn.width + x) * 4)]; };
	CHECK(at(50, 50)[1] < 40);
	CHECK(at(50, 15)[1] < 40);
	CHECK(at(50, 85)[1] < 40);
	CHECK(at(15, 50)[1] > 200);
	CHECK(at(85, 50)[1] > 200);
	// Text turned a quarter reaches down from where it begins, not across.
	auto words = one::desktop::offscreen::draw({100, 100}, 1, one::desktop::color::hex(0xffffff), [&](one::desktop::canvas& c) {
		auto turned = c.turned({50, 10}, 90);
		c.text(one::desktop::lay_out(one::desktop::text_span{"Mmmmm", {.size = 16, .color = one::desktop::color::hex(0x000000)}}, 200, 1), {50, 10});
	});
	auto inked = [&](int x0, int y0, int x1, int y1) {
		int n = 0;
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x) n += words.rgba[static_cast<std::size_t>((y * words.width + x) * 4)] < 128;
		return n;
	};
	CHECK(inked(25, 20, 50, 60) > 20);
	CHECK(inked(55, 5, 100, 25) == 0);
}

namespace {

	// Red, green, blue and white, a pixel each, left to right and top to bottom.
	one::desktop::picture four_colors() {
		return one::desktop::picture::of(2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255});
	}

	// The same four colors, each a quarter of eight pixels by eight, so a crop to one
	// quarter has an inside away from where smoothing meets its neighbors.
	one::desktop::picture four_quarters() {
		std::vector<std::uint8_t> rgba;
		const std::uint8_t colors[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
		for (int y = 0; y < 8; ++y)
			for (int x = 0; x < 8; ++x) {
				const auto& c = colors[(y / 4) * 2 + x / 4];
				rgba.insert(rgba.end(), {c[0], c[1], c[2], 255});
			}
		return one::desktop::picture::of(8, 8, std::move(rgba));
	}

	const std::uint8_t* pixel(const one::desktop::offscreen::image& i, int x, int y) { return &i.rgba[static_cast<std::size_t>((y * i.width + x) * 4)]; }

}  // namespace

TEST("a picture written as a PNG reads back the same, and what isn't one says so") {
	auto file = std::filesystem::temp_directory_path() / "one-desktop-picture.png";
	CHECK(four_colors().write_png(file).has_value());
	auto read = one::desktop::picture::read(file);
	CHECK_SAYING(read.has_value(), read ? "" : read.error());
	if (read) {
		CHECK(read->width() == 2);
		CHECK(read->height() == 2);
		CHECK(read->rgba() == four_colors().rgba());
	}
	std::filesystem::remove(file);
	std::string words = "hello";
	auto not_one = one::desktop::picture::decode(std::span(reinterpret_cast<const std::uint8_t*>(words.data()), words.size()));
	CHECK(!not_one.has_value());
	CHECK(not_one.error().starts_with("That isn't a picture"));
	CHECK(!one::desktop::picture::read("/nowhere/at/all.png").has_value());
}

TEST("a picture's size is read from its file without reading the picture, an SVG's from its own, and what isn't one has none") {
	auto folder = std::filesystem::temp_directory_path() / "one-desktop-sizes";
	std::filesystem::create_directories(folder);
	CHECK(one::desktop::picture::of(30, 20, std::vector<std::uint8_t>(30 * 20 * 4, 90)).write_png(folder / "wide.png").has_value());
	std::ofstream(folder / "drawing.svg") << R"(<svg xmlns="http://www.w3.org/2000/svg" width="120" height="80"><rect width="120" height="80"/></svg>)";
	std::ofstream(folder / "words.txt") << "hello";
	CHECK(one::desktop::picture::size_of(folder / "wide.png") == std::pair(30, 20));
	CHECK(one::desktop::picture::size_of(folder / "drawing.svg") == std::pair(120, 80));
	CHECK(!one::desktop::picture::size_of(folder / "words.txt"));
	CHECK(!one::desktop::picture::size_of(folder / "gone.png"));
	std::filesystem::remove_all(folder);
}

TEST("a picture made smaller keeps its shape, each pixel the average of those it covers, and one small already is itself") {
	// Black and white columns, two pixels each, made half as wide: each pixel gray.
	std::vector<std::uint8_t> stripes;
	for (int y = 0; y < 4; ++y) {
		for (int x = 0; x < 8; ++x) {
			std::uint8_t v = x % 2 == 0 ? 0 : 255;
			stripes.insert(stripes.end(), {v, v, v, 255});
		}
	}
	auto made = one::desktop::picture::of(8, 4, stripes).smaller(4);
	CHECK(made.width() == 4 && made.height() == 2);
	CHECK(std::ranges::all_of(made.rgba() | std::views::stride(4), [](std::uint8_t v) { return v >= 126 && v <= 129; }));
	auto small = four_colors();
	CHECK(small.smaller(64).rgba() == small.rgba());
}

TEST("a picture is drawn stretched to its area, cropped to a part, and partly showing") {
	auto drawn = one::desktop::offscreen::draw({300, 100}, 1, one::desktop::color::hex(0xffffff), [](one::desktop::canvas& c) {
		c.picture(four_colors(), {0, 0, 100, 100});
		c.picture(four_quarters(), {100, 0, 100, 100}, {0, 0, 0.5f, 0.5f});
		c.picture(four_quarters(), {200, 0, 100, 100}, {0, 0, 0.5f, 0.5f}, 0.5f);
	});
	auto near = [](const std::uint8_t* p, int r, int g, int b) { return std::abs(p[0] - r) <= 8 && std::abs(p[1] - g) <= 8 && std::abs(p[2] - b) <= 8; };
	CHECK(near(pixel(drawn, 10, 10), 255, 0, 0));
	CHECK(near(pixel(drawn, 90, 10), 0, 255, 0));
	CHECK(near(pixel(drawn, 10, 90), 0, 0, 255));
	CHECK(near(pixel(drawn, 90, 90), 255, 255, 255));
	CHECK(near(pixel(drawn, 110, 10), 255, 0, 0));  // its top left quarter, filling the area
	CHECK(near(pixel(drawn, 150, 50), 255, 0, 0));
	CHECK(near(pixel(drawn, 180, 80), 255, 0, 0));
	const std::uint8_t* half = pixel(drawn, 250, 50);  // half red over white, mixed in linear light
	CHECK(half[0] > 245);
	CHECK(half[1] > 150);
	CHECK(half[1] < 205);
}

TEST("files dragged from a file manager are read from their addresses") {
	auto paths = one::desktop::detail::paths_of(
	    "# copied from a file manager\r\nfile:///home/maya/Pictures/beach%202019.jpg\r\nfile://localhost/tmp/a.png\r\nfile://other/x.png\r\nhttps://example.com/b.png\r\n");
	CHECK(paths.size() == 2);
	if (paths.size() != 2) return;
	CHECK(paths[0] == std::filesystem::path("/home/maya/Pictures/beach 2019.jpg"));
	CHECK(paths[1] == std::filesystem::path("/tmp/a.png"));
	auto web = one::desktop::detail::elsewhere_of("https://example.com/b.png\r\nfile:///tmp/a.png\r\n");
	CHECK(web.size() == 1);
	CHECK(web.front() == "https://example.com/b.png");
}

TEST("files let go over a file target are given to it, with where, and nowhere else") {
	std::vector<std::filesystem::path> given;
	std::vector<std::uint8_t> picture;
	one::desktop::point where{};
	one::desktop::rect drawn_in{};
	auto root = one::desktop::column(one::desktop::text("Above"), one::desktop::file_target(one::desktop::drawing([&](one::desktop::canvas&, one::desktop::rect area) { drawn_in = area; }),
	                                                                                        [&](one::desktop::dropped what, one::desktop::point at) {
		                                                                                        given = std::move(what.files);
		                                                                                        picture = std::move(what.picture);
		                                                                                        where = at;
	                                                                                        },
	                                                                                        {})
	                                                                          .grow())
	                .take();
	(void)one::desktop::offscreen::draw({200, 100}, 1, one::desktop::color::hex(0xffffff), [&](one::desktop::canvas& c) {
		root->place({0, 0, 200, 100});
		root->paint(c);
	});
	CHECK(drawn_in.width == 200);
	CHECK(drawn_in.y + drawn_in.height == 100);
	one::desktop::detail::drop_files(*root, {10, 2}, {.files = {"/tmp/missed.png"}, .picture = {}, .elsewhere = {}});
	CHECK(given.empty());
	one::desktop::detail::drop_files(*root, {120, 90}, {.files = {"/tmp/a.png", "/tmp/b.png"}, .picture = {}, .elsewhere = {}});
	CHECK(given.size() == 2);
	CHECK(where.x == 120);
	one::desktop::detail::drop_files(*root, {120, 90}, {.files = {}, .picture = {137, 80, 78, 71}, .elsewhere = {}});
	CHECK(picture.size() == 4);  // a picture's own bytes, as a browser drags one
}

TEST("a drawing hears a press it takes, the moves while it's held, and the release, and shows a cursor where the pointer is") {
	std::vector<std::string> heard;
	auto root = one::desktop::column(one::desktop::drawing([](one::desktop::canvas&, one::desktop::rect) {},
	                                                       {.on_press =
	                                                            [&](one::desktop::point at, int clicks) {
		                                                            heard.push_back(std::format("press {} {} {}", at.x, at.y, clicks));
		                                                            return at.x < 100;
	                                                            },
	                                                        .on_drag = [&](one::desktop::point at) { heard.push_back(std::format("drag {} {}", at.x, at.y)); },
	                                                        .on_release = [&](one::desktop::point at) { heard.push_back(std::format("release {} {}", at.x, at.y)); },
	                                                        .cursor = [](one::desktop::point at) { return at.x < 100 ? one::desktop::cursor::move : one::desktop::cursor::crosshair; }})
	                                    .grow())
	                .take();
	root->place({0, 0, 200, 100});
	one::desktop::element* found = root->hit({40, 50});
	CHECK(found != root.get());
	CHECK(found->cursor() == one::desktop::cursor::move);
	CHECK(found->press({40, 50}, 1));
	found->drag({60, 55});
	found->release({70, 60}, found);
	CHECK((heard == std::vector<std::string>{"press 40 50 1", "drag 60 55", "release 70 60"}));
	CHECK(root->hit({150, 50})->cursor() == one::desktop::cursor::crosshair);
	CHECK(!root->hit({150, 50})->press({150, 50}, 2));  // a press it doesn't take
	CHECK(!one::desktop::held().shift);  // no keyboard here
	CHECK(found->focusable());  // it takes the keyboard when pressed
	CHECK(!one::desktop::drawing([](one::desktop::canvas&, one::desktop::rect) {}).take()->focusable());
}

TEST("a drawing hears two fingers pinched over it, by how many times larger, and may hide the pointer there") {
	std::vector<float> pinched;
	auto root = one::desktop::column(one::desktop::drawing([](one::desktop::canvas&, one::desktop::rect) {},
	                                                       {.on_pinch =
	                                                            [&](one::desktop::point at, float by) {
		                                                            pinched.push_back(by);
		                                                            return at.x < 100;
	                                                            },
	                                                        .cursor = [](one::desktop::point) { return one::desktop::cursor::hidden; }})
	                                    .grow())
	                .take();
	root->place({0, 0, 200, 100});
	one::desktop::element* found = root->hit({40, 50});
	CHECK(found->pinch({40, 50}, 1.5f));
	CHECK(!found->pinch({150, 50}, 0.5f));  // one it doesn't take
	CHECK((pinched == std::vector<float>{1.5f, 0.5f}));
	CHECK(found->cursor() == one::desktop::cursor::hidden);
	// One that doesn't hear them leaves them to what it's in.
	CHECK(!one::desktop::drawing([](one::desktop::canvas&, one::desktop::rect) {}).take()->pinch({0, 0}, 2));
}

TEST("a color picker tells the color chosen as it's let go, from its shades and its hues") {
	one::screens::state<std::string> held("#ff0000");
	std::vector<std::string> settled;
	auto root = one::desktop::column(one::desktop::color_picker(held, [&](const std::string& hex) { settled.push_back(hex); })).take();
	root->place({0, 0, 240, 220});
	// Past the square's bottom left is black, whatever the hue.
	one::desktop::element* found = root->hit({6, 142});
	CHECK(found != nullptr);
	if (!found) return;
	CHECK(found->press({2, 148}, 1));
	CHECK(settled.empty());
	found->release({2, 148}, found);
	CHECK((settled == std::vector<std::string>{"#000000"}));
	// Its top right, along the hues a third of the way: green.
	CHECK(found->press({4 + 232.f / 3, 4 + 140 + 10 + 6}, 1));
	found->release({0, 0}, found);
	CHECK(found->press({241, 0}, 1));
	found->release({241, 0}, found);
	CHECK(settled.back() == "#00ff00");
}

TEST("a slider tells what it settled on as it's let go, not while it's held") {
	one::screens::state<std::string> held("0.5");
	std::vector<std::string> settled;
	auto root = one::desktop::column(one::desktop::slider(held, {.from = 0, .to = 1, .by = 0.01, .percent = true}, [&](const std::string& n) { settled.push_back(n); })).take();
	root->place({0, 0, 300, 40});
	one::desktop::element* found = root->hit({10, 20});
	CHECK(found != nullptr);
	if (!found) return;
	CHECK(found->press({10, 20}, 1));
	CHECK(settled.empty());  // not while it's held
	found->release({10, 20}, found);
	CHECK(settled.size() == 1);
	CHECK(settled.size() == 1 && settled[0] == held.peek());
	CHECK(held.peek() != "0.5");
}

TEST("a picture drawn with no window holds what was drawn, a picture drawn into it too, and nothing where nothing was") {
	std::vector<std::uint8_t> pixels;
	for (int i = 0; i < 4; ++i) pixels.insert(pixels.end(), {0, 0, 255, 255});
	auto blue = one::desktop::picture::of(2, 2, std::move(pixels));
	auto drawn = one::desktop::picture::drawn(30, 10, [&](one::desktop::canvas& c) {
		c.box({0, 0, 10, 10}, {.fill = one::desktop::color::hex(0xff0000)});
		c.picture(blue, {10, 0, 10, 10});
	});
	CHECK(drawn.width() == 30);
	CHECK(drawn.height() == 10);
	auto at = [&](int x, int y, int channel) { return drawn.rgba()[(static_cast<std::size_t>(y) * 30 + x) * 4 + channel]; };
	CHECK(at(5, 5, 0) == 255 && at(5, 5, 2) == 0 && at(5, 5, 3) == 255);
	CHECK(at(15, 5, 0) == 0 && at(15, 5, 2) == 255 && at(15, 5, 3) == 255);
	CHECK(at(25, 5, 3) == 0);
}

TEST("a picture copied is on the clipboard as a PNG to paste, and text copied after takes its place") {
	std::vector<std::uint8_t> pixels;
	for (int i = 0; i < 6; ++i) pixels.insert(pixels.end(), {200, 40, 10, 255});
	one::desktop::copy_picture(one::desktop::picture::of(3, 2, std::move(pixels)));
	auto pasted = one::desktop::clipboard_picture();
	CHECK(pasted.has_value());
	if (!pasted) return;
	auto back = one::desktop::picture::decode(*pasted);
	CHECK(back && back->width() == 3 && back->height() == 2 && back->rgba()[0] == 200);
	CHECK(!one::desktop::clipboard_text());
	one::desktop::copy_text("words");
	CHECK(!one::desktop::clipboard_picture());
	CHECK(one::desktop::clipboard_text() == "words");
}

TEST("every icon a toolbar draws with draws something, its paths read whole") {
	for (std::string name : {"crop", "copy", "rotate_left", "rotate_right", "flip_horizontal", "flip_vertical", "raise", "lower", "check", "close",
	                         "paste", "save", "undo", "redo", "text", "screenshot", "picture", "gallery", "fullscreen", "sun", "moon", "bold",
	                         "smaller", "larger", "fill", "fit", "stretch", "center", "color", "edges", "blend", "harmonize", "clear", "square", "rounded", "italic", "magic", "contents", "calendar", "bell", "inbox", "previous", "next", "timeline"}) {
		CHECK_SAYING(one::desktop::has_icon(name), "there's no icon " + name);
		auto drawn = one::desktop::picture::drawn(32, 32, [&](one::desktop::canvas& c) { one::desktop::draw_icon(c, name, {0, 0, 32, 32}, one::desktop::color::hex(0x000000)); });
		std::size_t inked = 0;
		// Inked, if only partly: drawn large, its line is still a pixel at most.
		for (std::size_t i = 3; i < drawn.rgba().size(); i += 4) inked += drawn.rgba()[i] > 64;
		CHECK_SAYING(inked > 20, "the icon " + name + " draws " + std::to_string(inked) + " pixels");
	}
}

TEST("an icon's rounded corner turns the short way round, as SVG draws it") {
	// The copy icon's back page has a corner of radius 1 at (2, 2); turned the long way
	// it would loop out past the corner, through (1, 1).
	auto drawn = one::desktop::picture::drawn(32, 32, [](one::desktop::canvas& c) { one::desktop::draw_icon(c, "copy", {0, 0, 32, 32}, one::desktop::color::hex(0x000000)); });
	auto alpha = [&](int x, int y) { return drawn.rgba()[(static_cast<std::size_t>(y) * 32 + static_cast<std::size_t>(x)) * 4 + 3]; };
	CHECK(alpha(2, 2) == 0);
	// The front page's left edge, at 5.5 of 16, is drawn.
	CHECK(alpha(10, 18) > 128);
}

TEST("a focus ring shows once the keyboard moved to it, not after a click or where focus was put first") {
	auto root = one::desktop::row(one::desktop::button("Save", [] {})).take();
	auto& button = *root->children[0];
	button.focused = true;
	CHECK(button.shows_focus());
	button.focused_by_pointer = true;
	CHECK(!button.shows_focus());
	button.focused_by_pointer = false;
	button.focused_by_default = true;
	CHECK(!button.shows_focus());
}

TEST("a font an app brings, from its bytes, is offered with the system's, and text still lays out") {
	std::ifstream in(one::desktop::fonts::find(body, 15).file, std::ios::binary);
	std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	CHECK(!bytes.empty());
	CHECK(one::desktop::use_font(bytes));
	CHECK(one::desktop::use_font(bytes));  // the second time finds the one written the first
	CHECK(!one::desktop::use_font({}));
	CHECK(one::desktop::lay_out(one::desktop::text_span{"After", body}, 0, 1).width() > 0);
}

TEST("an SVG document is drawn by the pinned nanosvg, an ellipse filled inside and clear at its corners, its edge smooth") {
	auto drawn = one::desktop::picture::from_svg(R"(<svg xmlns="http://www.w3.org/2000/svg" width="40" height="20"><ellipse cx="20" cy="10" rx="20" ry="10" fill="#2e86de"/></svg>)", 80, 40);
	CHECK(drawn.width() == 80);
	CHECK(drawn.height() == 40);
	auto at = [&](int x, int y) { return &drawn.rgba()[(static_cast<std::size_t>(y) * 80 + x) * 4]; };
	CHECK(at(40, 20)[2] == 0xde);
	CHECK(at(40, 20)[3] == 255);
	CHECK(at(1, 1)[3] == 0);
	// Its edge is neither all in nor all out somewhere.
	bool smooth = false;
	for (int x = 0; x < 40; ++x) smooth = smooth || (at(x, 20)[3] > 0 && at(x, 20)[3] < 255);
	CHECK(smooth);
	CHECK(!one::desktop::picture::from_svg("not an svg", 10, 10));
}

TEST("an SVG file is read as an SVG, never as pixels it isn't: known by what it says, sized by its width and height or its viewBox, and drawn from its vectors") {
	std::string svg = "\xEF\xBB\xBF  <?xml version=\"1.0\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 12\"><rect width=\"24\" height=\"12\" fill=\"#e4572e\"/></svg>";
	std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(svg.data()), svg.size());
	CHECK(one::desktop::picture::is_svg(bytes));
	std::string png = "\x89PNG\r\n";
	CHECK(!one::desktop::picture::is_svg({reinterpret_cast<const std::uint8_t*>(png.data()), png.size()}));
	auto size = one::desktop::picture::svg_size(svg);
	CHECK(size.has_value());
	if (size) CHECK((size->first == 24 && size->second == 12));
	auto read = one::desktop::picture::decode(bytes);
	CHECK(read.has_value());
	if (!read) return;
	CHECK(read->width() == 24);
	CHECK(read->height() == 12);
	CHECK(read->rgba()[0] == 0xe4);
	CHECK(read->rgba()[3] == 255);
}

TEST("an SVG broken apart is its shapes, lowest first, their paths with their transforms applied, their colors, and how much of each shows") {
	auto shapes = one::desktop::svg_shapes(R"svg(<svg xmlns="http://www.w3.org/2000/svg" width="100" height="50">
		<rect x="0" y="0" width="10" height="10" fill="#2e86de" fill-opacity="0.5"/>
		<g transform="translate(20 5)" opacity="0.5"><circle cx="5" cy="5" r="5" fill="none" stroke="#e4572e" stroke-width="2"/></g>
		<rect width="5" height="5" display="none"/>
	</svg>)svg");
	CHECK(shapes.size() == 2);
	if (shapes.size() != 2) return;
	CHECK(shapes[0].path.starts_with("M 0 0 C "));
	CHECK(shapes[0].path.ends_with(" Z"));
	CHECK(shapes[0].fill == "#2e86de");
	CHECK(shapes[0].stroke == "none");
	CHECK(std::abs(shapes[0].opacity - 0.5) < 0.01);
	CHECK(shapes[1].fill == "none");
	CHECK(shapes[1].stroke == "#e4572e");
	CHECK(shapes[1].stroke_width == 2);
	CHECK(std::abs(shapes[1].opacity - 0.5) < 0.01);
	// The circle, moved by its group, from 20 to 30 across.
	CHECK(shapes[1].path.find(" 30 ") != std::string::npos);
	CHECK(one::desktop::svg_shapes("not an svg").empty());
}

TEST("a screenshot with no desktop to ask says so, rather than waiting or failing badly") {
	const char* was = std::getenv("DBUS_SESSION_BUS_ADDRESS");
	std::string kept = was ? was : "";
	setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/one-tests-bus", 1);
	auto shot = one::desktop::screenshot();
	if (was) setenv("DBUS_SESSION_BUS_ADDRESS", kept.c_str(), 1);
	else unsetenv("DBUS_SESSION_BUS_ADDRESS");
	CHECK(!shot);
	if (!shot) CHECK(!shot.error().empty());
}

TEST("files chosen with no desktop to ask says so, rather than waiting or failing badly") {
	const char* was = std::getenv("DBUS_SESSION_BUS_ADDRESS");
	std::string kept = was ? was : "";
	setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/one-tests-bus", 1);
	auto chosen = one::desktop::chosen_files({.title = "Add pictures", .pictures = true, .several = true});
	if (was) setenv("DBUS_SESSION_BUS_ADDRESS", kept.c_str(), 1);
	else unsetenv("DBUS_SESSION_BUS_ADDRESS");
	CHECK(!chosen);
	if (!chosen) CHECK(!chosen.error().empty());
}

TEST("text is laid out alike on several threads at once") {
	const float alone = one::desktop::lay_out(one::desktop::text_span{"Laid out together", body}, 0, 1).width();
	std::atomic<int> differing = 0;
	std::vector<std::thread> threads;
	for (int t = 0; t < 4; ++t)
		threads.emplace_back([&] {
			for (int i = 0; i < 200; ++i)
				if (one::desktop::lay_out(one::desktop::text_span{"Laid out together", body}, 0, 1 + (i % 3) * 0.5).width() <= 0 ||
				    one::desktop::lay_out(one::desktop::text_span{"Laid out together", body}, 0, 1).width() != alone)
					++differing;
		});
	for (auto& t : threads) t.join();
	CHECK(differing == 0);
}

TEST("a color written in sRGB is kept in linear light") {
	CHECK(one::desktop::color::hex(0xffffff).red == 1.f);
	CHECK(one::desktop::color::hex(0x000000).green == 0.f);
	CHECK(std::abs(one::desktop::color::hex(0x808080).blue - 0.2158605f) < 1e-5f);
}

TEST("text wraps between words to fit its width, and a newline starts a line") {
	auto wide = one::desktop::lay_out({"Open project tracking for everyone, with every change kept.", body}, 0, 1);
	CHECK(wide.lines().size() == 1);
	auto narrow = one::desktop::lay_out({"Open project tracking for everyone, with every change kept.", body}, 150, 1);
	CHECK(narrow.lines().size() > 1);
	for (const auto& line : narrow.lines()) CHECK_SAYING(line.width <= 150, std::format("a line {} wide", line.width));
	CHECK(narrow.height() > wide.height());
	auto broken = one::desktop::lay_out({"first\nsecond\n\nfourth", body}, 0, 1);
	CHECK(broken.lines().size() == 4);
	auto empty = one::desktop::lay_out({"", body}, 0, 1);
	CHECK(empty.lines().size() == 1);
	CHECK(empty.height() > 0);
}

TEST("text laid out at a larger scale is the same size, in more pixels") {
	auto one = one::desktop::lay_out({"Take it", body}, 0, 1);
	auto more = one::desktop::lay_out({"Take it", body}, 0, 1.5);
	CHECK(std::abs(one.width() - more.width()) < 2);
	CHECK(std::abs(one.height() - more.height()) < 1);
}

TEST("boxes: fills, borders, corners, a shadow and a clip, at scale 1") { matches_golden("boxes-1", {448, 200}, 1, boxes); }
TEST("boxes at scale 1.5") { matches_golden("boxes-1.5", {448, 200}, 1.5, boxes); }
TEST("text: styles, wrapping, underline and strike, at scale 1") { matches_golden("text-1", {340, 140}, 1, words); }
TEST("text at scale 1.5") { matches_golden("text-1.5", {340, 140}, 1.5, words); }

namespace {

	// The tests' theme: harbor in a font every machine running them has.
	const one::desktop::theme& tested_theme() {
		static one::desktop::theme t = [] {
			one::desktop::theme made = one::desktop::theme::harbor();
			made.text_family = "Noto Sans";
			made.code_family = "Noto Sans Mono";
			return made;
		}();
		return t;
	}

	// A view built and placed in an area, as a window would.
	std::unique_ptr<one::desktop::element> placed(one::desktop::view v, one::desktop::rect area) {
		one::desktop::detail::theme_in_use() = &tested_theme();
		auto root = std::move(v).take();
		root->place(area);
		return root;
	}

	one::desktop::view rows_of(const std::vector<std::string>& keys, std::string* opened = nullptr) {
		std::vector<one::desktop::list_row> rows;
		for (const auto& k : keys) rows.push_back({k, one::desktop::text("Issue " + k)});
		return one::desktop::list(std::move(rows), [opened](const std::string& k) {
			if (opened) *opened = k;
		});
	}

	one::desktop::element* first_list(one::desktop::element& e) {
		if (e.prefers_focus()) return &e;
		for (auto& child : e.children) {
			if (auto* found = first_list(*child)) return found;
		}
		return nullptr;
	}

	one::desktop::key_event key(std::uint32_t symbol, const char* text = "") { return {.symbol = symbol, .text = text}; }

}  // namespace

TEST("a state tells what read it when it changes, once, and not after it's gone") {
	one::desktop::state<int> count = 1;
	int told = 0;
	auto reader = std::make_shared<one::desktop::detail::reader>();
	reader->changed = [&] { ++told; };
	{
		one::desktop::detail::reading noting(reader);
		CHECK(*count == 1);
		CHECK(*count == 1);  // read twice, told once
	}
	count.set(2);
	CHECK(told == 1);
	count.set(3);  // read before, but not since
	CHECK(told == 1);
	{
		one::desktop::detail::reading noting(reader);
		(void)*count;
	}
	reader.reset();
	count.set(4);
	CHECK(told == 1);
}

TEST("a column stacks with spacing inside its padding, and a row shares what's left among those that grow") {
	auto root = placed(one::desktop::column(one::desktop::text("one").height(20), one::desktop::text("two").height(30)).spacing(10).padding(5), {0, 0, 200, 300});
	CHECK(root->children[0]->frame.y == 5);
	CHECK(root->children[1]->frame.y == 35);
	CHECK(root->children[1]->frame.width == 190);
	auto across = placed(one::desktop::row(one::desktop::text("a").width(40), one::desktop::spacer(), one::desktop::text("b").width(60).grow(2), one::desktop::edge(one::desktop::color::hex(0xc2312d))).spacing(10),
	                     {0, 0, 400, 50});
	auto& kids = across->children;
	float left = 400 - 40 - 3 - 3 * 10;
	CHECK(std::abs(kids[1]->frame.width - left / 3) < 0.01f);
	CHECK(std::abs(kids[2]->frame.width - 2 * left / 3) < 0.01f);
	CHECK(kids[3]->frame.height == 50);  // the edge fills the row's height
}

TEST("a list is chosen with j, k and the arrows, opened with Enter or a click") {
	std::string opened;
	auto root = placed(rows_of({"a", "b", "c", "d"}, &opened), {0, 0, 300, 400});
	auto& l = *root;
	CHECK(l.handle_key(key(one::desktop::keys::down)));
	CHECK(l.handle_key(key('j', "j")));
	CHECK(l.handle_key(key(one::desktop::keys::enter)));
	CHECK(opened == "c");
	CHECK(l.handle_key(key('k', "k")));
	CHECK(l.handle_key(key(one::desktop::keys::enter)));
	CHECK(opened == "b");
	CHECK(!l.handle_key(key('x', "x")));  // not the list's to take
	CHECK(l.handle_key(key('k', "k")));
	CHECK(!l.handle_key(key('k', "k")));  // past the first row: for whatever's above
	auto& last = l.children[3]->frame;
	CHECK(l.click({last.x + 5, last.y + 5}));
	CHECK(opened == "d");
}

TEST("the chosen row and the scroll stay where they were when the view is built again, turned around or shorter") {
	std::vector<std::string> keys;
	for (int i = 0; i < 40; ++i) keys.push_back("issue-" + std::to_string(i));
	one::desktop::rect window{0, 0, 300, 200};
	auto before = placed(one::desktop::scroll(rows_of(keys)), window);
	auto& list = *first_list(*before);
	for (int i = 0; i < 25; ++i) list.handle_key(key(one::desktop::keys::down));
	std::string opened;
	list.handle_key(key(one::desktop::keys::enter));
	// The chosen row was brought into view.
	auto& chosen = *list.children[25];
	CHECK(chosen.frame.y >= window.y && chosen.frame.bottom() <= window.bottom());

	std::vector<std::string> turned(keys.rbegin(), keys.rend());
	auto after = std::move(one::desktop::scroll(rows_of(turned, &opened))).take();
	one::desktop::detail::reconcile(*after, *before);
	after->place(window);
	auto& relisted = *first_list(*after);
	relisted.handle_key(key(one::desktop::keys::enter));
	CHECK(opened == "issue-25");  // the same issue, now at another place
	auto& moved = *relisted.children[14];  // where issue-25 is now, scrolled back into view
	CHECK(moved.frame.y >= window.y && moved.frame.bottom() <= window.bottom());

	std::vector<std::string> fewer(keys.begin(), keys.begin() + 10);
	auto shorter = std::move(one::desktop::scroll(rows_of(fewer, &opened))).take();
	one::desktop::detail::reconcile(*shorter, *after);
	shorter->place(window);
	first_list(*shorter)->handle_key(key(one::desktop::keys::enter));
	CHECK(opened == "issue-9");  // gone, so the nearest that's left
}

TEST("three fingers swiped across a touchpad are told to what's under the pointer, how far they've gone, and when they're lifted") {
	one::desktop::detail::theme_in_use() = &tested_theme();
	one::desktop::app::state app;
	one::desktop::window::state w;
	w.owner = &app;
	w.size = {400, 200};
	std::vector<std::tuple<float, float, bool>> told;
	w.build = [&] {
		return one::desktop::drawing([](one::desktop::canvas&, one::desktop::rect) {}, {.on_swipe = [&](one::desktop::point, float dx, float dy, bool ended) {
			told.emplace_back(dx, dy, ended);
			return true;
		}}).grow();
	};
	w.needs_building = true;
	one::desktop::bring_up_to_date(w);
	one::desktop::deliver_swipe(w, {200, 100}, -40, 2, false);
	one::desktop::deliver_swipe(w, {200, 100}, -120, 3, true);
	CHECK(told.size() == 2);
	CHECK((told[0] == std::tuple{-40.f, 2.f, false}));
	CHECK((told[1] == std::tuple{-120.f, 3.f, true}));
}

TEST("a key the window's lines take is theirs before it moves the keyboard to what's beside, and one they don't take still moves it") {
	one::desktop::detail::theme_in_use() = &tested_theme();
	one::desktop::app::state app;
	one::desktop::window outer(std::make_unique<one::desktop::window::state>());
	auto& w = outer.internals();
	w.outside = &outer;
	w.owner = &app;
	w.size = {400, 100};
	w.build = [] { return one::desktop::row(one::desktop::button("First", [] {}).autofocus(), one::desktop::button("Second", [] {})).spacing(8); };
	int stepped = 0;
	outer.on_key = [&](const one::desktop::key_event& e) {
		if (!e.is(one::desktop::keys::right)) return false;
		++stepped;
		return true;
	};
	w.needs_building = true;
	one::desktop::bring_up_to_date(w);
	auto& first = *w.root->children[0];
	auto& second = *w.root->children[1];
	CHECK(first.focused);
	// Right is the window's: it's run, and the keyboard stays where it was.
	one::desktop::deliver_key(w, key(one::desktop::keys::right));
	CHECK(stepped == 1);
	CHECK(first.focused);
	CHECK(!second.focused);
	// l isn't, so it moves the keyboard on, as the arrows otherwise do.
	one::desktop::deliver_key(w, {.symbol = 'l', .text = "l"});
	CHECK(stepped == 1);
	CHECK(second.focused);
}

TEST("a page shown again is scrolled where it was left once what it holds reaches that far, what it was left from given the keyboard and brought into view") {
	one::desktop::detail::theme_in_use() = &tested_theme();
	one::desktop::state<float> left(300);
	one::desktop::state<int> rows(3);
	int pressed = -1;
	one::desktop::app::state app;
	one::desktop::window::state w;
	w.owner = &app;
	w.size = {300, 200};
	w.build = [&] {
		std::vector<one::desktop::view> shown;
		for (int i = 0; i < rows.get(); ++i) {
			auto row = one::desktop::pressable(one::desktop::text("Piece " + std::to_string(i)).height(40), [&pressed, i] { pressed = i; });
			// The piece it was left from.
			shown.push_back(i == 9 ? std::move(row).autofocus().brought_into_view() : std::move(row));
		}
		return one::desktop::scroll(one::desktop::column(std::move(shown)), left);
	};
	w.needs_building = true;
	one::desktop::bring_up_to_date(w);
	// While it's too short to scroll that far, as while what it holds comes in, it's
	// at the top, and where it was left is kept.
	CHECK(w.root->children[0]->frame.y == 0);
	CHECK(left.peek() == 300);
	rows.set(40);
	one::desktop::bring_up_to_date(w);
	CHECK(w.root->children[0]->frame.y == -300);
	auto& piece = *w.root->children[0]->children[9];
	CHECK(piece.focused);
	CHECK(piece.shows_focus());
	CHECK(piece.handle_key(key(one::desktop::keys::enter)));
	CHECK(pressed == 9);
	// Scrolled on, it's kept there.
	CHECK(w.root->wheel({10, 10}, 0, 100));
	CHECK(left.peek() == 400);
	// And shown afresh with one left from further down, that one's brought into view.
	w.root.reset();
	left.set(0);
	w.needs_building = true;
	one::desktop::bring_up_to_date(w);
	auto& again = *w.root->children[0]->children[9];
	CHECK(again.frame.y >= 0);
	CHECK(again.frame.bottom() <= 200);
	CHECK(left.peek() > 0);
}

TEST("a view: rows, a list, buttons, an avatar and a divider, at scale 1") {
	matches_golden("view-1", {520, 260}, 1, [](one::desktop::canvas& c) {
		one::desktop::detail::theme_in_use() = &tested_theme();
		const auto& t = tested_theme();
		auto root = placed(one::desktop::column(one::desktop::row(one::desktop::text("Issues", t.heading()), one::desktop::spacer(), one::desktop::button("Turn around", [] {}),
		                                          one::desktop::button("Add an issue", [] {}, true))
		                                    .spacing(10),
		                                one::desktop::divider(),
		                                one::desktop::row(one::desktop::avatar("Maya Garcia"), one::desktop::text("Maya Garcia reported it", t.quiet())).spacing(8),
		                                rows_of({"12", "16", "17"}))
		                       .spacing(12)
		                       .padding(20),
		                   {0, 0, 520, 260});
		root->paint(c);
	});
}

TEST("a caret stands before each character, at a wrapped line's start, and on the line after a final newline") {
	auto laid = one::desktop::lay_out({"Ship it", body}, 0, 1);
	CHECK(laid.carets().front().at == 0);
	CHECK(laid.carets().back().at == 7);
	CHECK(laid.caret(4).x > laid.caret(3).x);  // the space has its width
	CHECK(laid.caret(7).x == laid.lines()[0].width);
	auto accented = one::desktop::lay_out({"café", body}, 0, 1);
	std::vector<std::size_t> places;
	for (const auto& c : accented.carets()) places.push_back(c.at);
	CHECK((places == std::vector<std::size_t>{0, 1, 2, 3, 5}));  // é is two bytes, one character
	auto wrapped = one::desktop::lay_out({"Open project tracking for everyone", body}, 120, 1);
	CHECK(wrapped.lines().size() > 1);
	std::size_t second = 0;
	for (const auto& c : wrapped.carets()) {
		if (c.line == 1) {
			second = c.at;
			break;
		}
	}
	CHECK(wrapped.caret(second).line == 1);
	CHECK(wrapped.caret(second).x == 0);
	auto ended = one::desktop::lay_out({"one\n", body}, 0, 1);
	CHECK(ended.lines().size() == 2);
	CHECK(ended.caret(4).line == 1);
	CHECK(laid.offset_at(laid.caret(5).x + 1, 5) == 5);
	CHECK(laid.offset_at(-10, 5) == 0);
	CHECK(laid.offset_at(1000, 5) == 7);
}

TEST("editing: typing, erasing and moving by characters and words, selecting, and undoing a word at a time") {
	one::desktop::editing e("Hello");
	CHECK(e.caret() == 5);
	e.type(",");
	e.type(" ");
	for (char c : std::string("world")) e.type(std::string(1, c));
	CHECK(e.text() == "Hello, world");
	e.left(true, true);
	CHECK(e.selected() == "world");
	e.erase_back(false);
	CHECK(e.text() == "Hello, ");
	CHECK(e.undo());
	CHECK(e.text() == "Hello, world");
	CHECK(e.undo());  // the word typed letter by letter, whole
	CHECK(e.text() == "Hello, ");
	CHECK(e.redo());
	CHECK(e.text() == "Hello, world");
	e.start(false);
	e.right(true, false);
	CHECK(e.caret() == 7);  // past "Hello, " to "world"
	e.erase_forward(true);
	CHECK(e.text() == "Hello, ");
	one::desktop::editing arabic("سلام عليكم");
	arabic.left(false, false);
	CHECK(arabic.caret() == arabic.text().size() - 2);  // a character is two bytes here
	arabic.left(true, false);
	CHECK(arabic.caret() == std::string("سلام ").size());
	one::desktop::editing one_line("a", false);
	one_line.type("b\nc");
	CHECK(one_line.text() == "ab c");
	one::desktop::editing lines("first\nsecond", true);
	lines.line_start(false);
	CHECK(lines.caret() == 6);
	lines.select_word(8);
	CHECK(lines.selected() == "second");
	lines.select_line(2);
	CHECK(lines.selected() == "first");
	lines.replace_all("x");
	CHECK(lines.caret() <= 1);
	CHECK(!lines.undo());  // nothing before what came from elsewhere
}

TEST("a field takes what's typed into its state, with the clipboard, undo, and Enter submitting") {
	one::desktop::state<std::string> title("Ship");
	int submitted = 0;
	auto root = placed(one::desktop::field(title, {.placeholder = "Title", .on_submit = [&] { ++submitted; }}), {0, 0, 300, 40});
	auto& f = *root;
	CHECK(f.focusable());
	CHECK(f.handle_key(key(' ', " ")));
	CHECK(f.handle_key(key('i', "i")));
	CHECK(f.handle_key(key('t', "t")));
	CHECK(title.peek() == "Ship it");
	CHECK(f.handle_key(key('h', "h")));  // a letter, not a move to the left
	CHECK(f.handle_key(key(one::desktop::keys::backspace)));
	CHECK(title.peek() == "Ship it");
	one::desktop::key_event all{.symbol = 'a', .text = "", .control = true};
	one::desktop::key_event copy{.symbol = 'c', .text = "", .control = true};
	one::desktop::key_event paste{.symbol = 'v', .text = "", .control = true};
	one::desktop::key_event undo{.symbol = 'z', .text = "", .control = true};
	CHECK(f.handle_key(all));
	CHECK(f.handle_key(copy));
	CHECK(f.handle_key(key(one::desktop::keys::end)));
	CHECK(f.handle_key(key(' ', " ")));
	CHECK(f.handle_key(paste));
	CHECK(title.peek() == "Ship it Ship it");
	CHECK(f.handle_key(undo));
	CHECK(title.peek() == "Ship it ");
	CHECK(f.handle_key(key(one::desktop::keys::enter)));
	CHECK(submitted == 1);
	CHECK(!f.handle_key(key(one::desktop::keys::escape)));  // for what it's in, like going back
	// Built again, it takes a value set elsewhere, its caret where it was.
	title.set("Ship it now");
	auto again = placed(one::desktop::field(title, {}), {0, 0, 300, 40});
	one::desktop::detail::reconcile(*again, f);
	CHECK(again->handle_key(key('!', "!")));
	CHECK(title.peek() == "Ship it !now");
}

TEST("a field shows what an input method composes at its caret, and takes what it types, erasing what it says") {
	one::desktop::state<std::string> name("Tokyo ");
	auto root = placed(one::desktop::field(name, {}), {0, 0, 300, 40});
	auto* typing = root->entry();
	CHECK(typing != nullptr);
	if (!typing) return;
	std::size_t caret = 0, anchor = 0;
	CHECK(typing->surrounding(caret, anchor) == "Tokyo ");
	CHECK(caret == 6);
	auto before = typing->caret_area();
	typing->compose("にほ", -1);
	CHECK(name.peek() == "Tokyo ");  // shown, not yet typed
	CHECK(typing->caret_area().x > before.x);  // after what's composed
	typing->commit("日本", 0, 0);
	typing->compose("", -1);
	CHECK(name.peek() == "Tokyo 日本");
	typing->commit("東京", std::string("日本").size(), 0);
	CHECK(name.peek() == "Tokyo 東京");
}

TEST("fields: typed in with a word selected, empty with a placeholder, and several lines, at scale 1") {
	matches_golden("field-1", {360, 230}, 1, [](one::desktop::canvas& c) {
		one::desktop::detail::theme_in_use() = &tested_theme();
		one::desktop::state<std::string> title("Ship the desktop app"), empty, body("First line\nand a second, which is long enough to wrap onto a third");
		auto root = placed(one::desktop::column(one::desktop::field(title), one::desktop::field(empty, {.placeholder = "Search issues"}),
		                                        one::desktop::field(body, {.multiline = true, .lines = 3}))
		                       .spacing(10)
		                       .padding(16),
		                   {0, 0, 360, 230});
		auto& first = *root->children[0];
		first.focused = true;
		first.press({first.frame.x + 70, first.frame.y + 15}, 2);  // a double click on "desktop"
		root->paint(c);
	});
}

TEST("a toolbar: large tool buttons with their words, small ones, one on and one pointed at with its words under it, and small ones of words alone") {
	matches_golden("tools-1", {420, 120}, 1, [](one::desktop::canvas& c) {
		one::desktop::detail::theme_in_use() = &tested_theme();
		auto root = placed(one::desktop::row(one::desktop::tool_button("paste", "Paste", [] {}, {.large = true}),
		                                     one::desktop::tool_button("text", "Text", [] {}, {.large = true}),
		                                     one::desktop::tool_button("fill", "Fill", [] {}, {.on = true}), one::desktop::tool_button("fit", "Fit", [] {}),
		                                     one::desktop::tool_button("bold", "Bold", [] {}), one::desktop::tool_button("moon", "Dark", [] {}),
		                                     one::desktop::tool_button("", "1×", [] {}, {.on = true}), one::desktop::tool_button("", "1.5×", [] {}))
		                       .spacing(4)
		                       .align(one::desktop::align::center)
		                       .padding(12),
		                   {0, 0, 420, 120});
		root->children[3]->hovered = true;
		root->paint(c);
		c.finish();
	});
}

TEST("a tool pushed in draws darker than one pointed at, its icon a pixel lower, large or small, on or not, and a key's flash draws it so too") {
	// A tool as it's drawn idle, pointed at, and pushed in under the pointer.
	auto drawn = [](one::desktop::tool_options options, bool hovered, bool held) {
		return one::desktop::offscreen::draw({120, 100}, 1, tested_theme().surface, [&](one::desktop::canvas& c) {
			one::desktop::detail::theme_in_use() = &tested_theme();
			auto root = placed(one::desktop::row(one::desktop::tool_button("delete", "Remove", [] {}, options)).padding(12), {0, 0, 120, 100});
			auto& tool = *root->children[0];
			tool.hovered = hovered;
			tool.held_down = held;
			root->paint(c);
			c.finish();
		});
	};
	auto differ = [](const auto& a, const auto& b) { return one::desktop::offscreen::differences(a, b, 3) > 20; };
	for (auto options : {one::desktop::tool_options{}, one::desktop::tool_options{.large = true}, one::desktop::tool_options{.on = true}}) {
		auto idle = drawn(options, false, false), pointed = drawn(options, true, false), pushed = drawn(options, true, true);
		CHECK(differ(pushed, idle));
		CHECK(differ(pushed, pointed));
		// Let go off it, it's as it was.
		CHECK(!differ(drawn(options, false, false), idle));
	}
	// On stays distinct from pushed in.
	CHECK(differ(drawn({.on = true}, false, false), drawn({}, true, true)));
	// A key that ran it pushes it in a moment, as a press does.
	auto flashed = one::desktop::tool_options{.flashed = true};
	CHECK(!differ(drawn(flashed, false, false), drawn({}, false, true)));
	// One that can't be pressed isn't pushed in.
	CHECK(!differ(drawn({.off = true}, false, true), drawn({.off = true}, false, false)));
}

namespace {

	// A picture `width` × `height` whose every pixel says where it is: red its column,
	// green its row, so a pixel turned shows where it came from.
	std::vector<std::uint8_t> placed_pixels(int width, int height, int step = 40) {
		std::vector<std::uint8_t> rgba;
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) rgba.insert(rgba.end(), {static_cast<std::uint8_t>(x * step), static_cast<std::uint8_t>(y * step), 0, 255});
		}
		return rgba;
	}

	std::pair<int, int> came_from(const one::desktop::picture& p, int x, int y) {
		const auto& rgba = p.rgba();
		std::size_t at = (static_cast<std::size_t>(y) * p.width() + x) * 4;
		return {rgba[at] / 40, rgba[at + 1] / 40};
	}

}  // namespace

TEST("a photo's EXIF is read from a JPEG, a PNG, a WebP or a TIFF without decoding it: when it was taken, with what, and which way up") {
	photos::said said{.orientation = 6, .make = "FUJIFILM", .model = "X-T5", .taken = "2026:10:12 08:30:15", .offset = "+02:00"};
	auto block = photos::exif(said);
	auto jpeg = photos::jpeg(16, 8, placed_pixels(16, 8, 15), block);
	for (const auto& bytes : {jpeg, photos::png(4, 3, placed_pixels(4, 3), block), photos::webp(block, true), photos::webp(block, false), block}) {
		auto facts = one::desktop::facts_of(bytes);
		CHECK(facts.orientation == 6);
		CHECK(facts.make == "FUJIFILM" && facts.model == "X-T5");
		CHECK(facts.camera() == "FUJIFILM X-T5");
		CHECK(facts.taken == "2026:10:12 08:30:15" && facts.offset == "+02:00");
		CHECK(facts.when() && std::format("{:%FT%T}Z", *facts.when()) == "2026-10-12T06:30:15Z");
	}
	// Its numbers most significant byte first, as Motorola's order says.
	said.little = false;
	CHECK(one::desktop::facts_of(photos::exif(said)).camera() == "FUJIFILM X-T5");
	CHECK(one::desktop::facts_of(photos::exif(said)).orientation == 6);
	// From its file, reading only the start of it.
	auto file = std::filesystem::temp_directory_path() / "uione-exif.jpg";
	std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(jpeg.data()), static_cast<std::streamsize>(jpeg.size()));
	CHECK(one::desktop::facts_of(file).camera() == "FUJIFILM X-T5");
	// Measured and drawn the way up it was held: 8 × 16, its stored left side its top.
	auto size = one::desktop::picture::size_of(file);
	CHECK(size && size->first == 8 && size->second == 16);
	auto read = one::desktop::picture::read(file);
	CHECK(read && read->width() == 8 && read->height() == 16);
	if (read) {
		const auto& rgba = read->rgba();
		CHECK(rgba[0] < 60 && rgba[1] > 20);  // stored at its bottom left: column 0, row 7
		std::size_t last = (static_cast<std::size_t>(15) * 8 + 7) * 4;
		CHECK(rgba[last] > 180 && rgba[last + 1] < 60);  // stored at its top right
	}
	std::filesystem::remove(file);
	// None, in a picture that says nothing, or one that isn't a picture.
	auto plain = one::desktop::facts_of(photos::jpeg(4, 4, placed_pixels(4, 4), {}));
	CHECK(plain.orientation == 1 && plain.camera().empty() && !plain.when());
	CHECK(one::desktop::facts_of(std::vector<std::uint8_t>{'h', 'e', 'l', 'l', 'o'}).orientation == 1);
	CHECK(one::desktop::facts_of(std::filesystem::temp_directory_path() / "uione-no-such-photo.jpg").orientation == 1);
}

TEST("a photo is drawn the way up its camera says, each of EXIF's eight, mirrored and turned") {
	// Where the pixel drawn at the top left, top right and bottom left comes from in the
	// 3 × 2 it's stored as, for each.
	struct way {
		int orientation, width, height;
		std::pair<int, int> top_left, top_right, bottom_left;
	};
	for (const way& w : {way{1, 3, 2, {0, 0}, {2, 0}, {0, 1}}, way{2, 3, 2, {2, 0}, {0, 0}, {2, 1}}, way{3, 3, 2, {2, 1}, {0, 1}, {2, 0}}, way{4, 3, 2, {0, 1}, {2, 1}, {0, 0}},
	                     way{5, 2, 3, {0, 0}, {0, 1}, {2, 0}}, way{6, 2, 3, {0, 1}, {0, 0}, {2, 1}}, way{7, 2, 3, {2, 1}, {2, 0}, {0, 1}}, way{8, 2, 3, {2, 0}, {2, 1}, {0, 0}}}) {
		auto shown = one::desktop::picture::decode(photos::png(3, 2, placed_pixels(3, 2), photos::exif({.orientation = w.orientation})));
		CHECK(shown.has_value());
		if (!shown) continue;
		CHECK_SAYING(shown->width() == w.width && shown->height() == w.height, std::format("orientation {}", w.orientation));
		CHECK_SAYING(came_from(*shown, 0, 0) == w.top_left, std::format("orientation {}", w.orientation));
		CHECK_SAYING(came_from(*shown, shown->width() - 1, 0) == w.top_right, std::format("orientation {}", w.orientation));
		CHECK_SAYING(came_from(*shown, 0, shown->height() - 1) == w.bottom_left, std::format("orientation {}", w.orientation));
	}
	// One EXIF doesn't count is left as it's stored.
	auto odd = one::desktop::picture::decode(photos::png(3, 2, placed_pixels(3, 2), photos::exif({.orientation = 9})));
	CHECK(odd && odd->width() == 3);
}

TEST("an EXIF block cut short, or made wrong anywhere, is read as far as it's sound, never past what's there, and never for long") {
	photos::said said{.orientation = 8, .make = "Canon", .model = "Canon EOS R5", .taken = "2026:01:02 03:04:05", .offset = "-05:30"};
	CHECK(one::desktop::facts_of(photos::exif(said)).camera() == "Canon EOS R5");
	CHECK(std::format("{:%FT%T}Z", *one::desktop::facts_of(photos::exif(said)).when()) == "2026-01-02T08:34:05Z");
	auto jpeg = photos::jpeg(8, 8, placed_pixels(8, 8), photos::exif(said));
	auto png = photos::png(3, 2, placed_pixels(3, 2), photos::exif(said));
	auto webp = photos::webp(photos::exif(said), true);
	auto started = std::chrono::steady_clock::now();
	// Cut short at every length.
	for (const auto* whole : {&jpeg, &png, &webp}) {
		for (std::size_t n = 0; n <= whole->size(); ++n) {
			auto facts = one::desktop::facts_of(std::span(whole->data(), n));
			CHECK(facts.orientation >= 1 && facts.orientation <= 8);
			CHECK(facts.make.size() <= 256 && facts.model.size() <= 256);
		}
	}
	// A byte made wrong, again and again, anywhere, as a damaged file would be.
	std::uint32_t seed = 12345;
	auto next = [&] { return seed = seed * 1664525u + 1013904223u; };
	for (const auto* whole : {&jpeg, &png, &webp}) {
		for (int i = 0; i < 4000; ++i) {
			auto damaged = *whole;
			for (int k = 0; k < 1 + static_cast<int>(next() % 4); ++k) damaged[next() % damaged.size()] = static_cast<std::uint8_t>(next() >> 24);
			auto facts = one::desktop::facts_of(damaged);
			CHECK(facts.orientation >= 1 && facts.orientation <= 8);
			(void)facts.when();
			(void)facts.camera();
		}
	}
	// What a hostile one says: a directory of 65535 entries, offsets past its end, a
	// text as long as can be said, segments and chunks that say they're empty or larger
	// than anything, and a directory that points to itself.
	auto block = photos::exif(said);
	auto broken = [&](std::size_t at, std::vector<std::uint8_t> with) {
		auto b = block;
		for (std::size_t i = 0; i < with.size(); ++i) b[at + i] = with[i];
		return one::desktop::facts_of(b);
	};
	CHECK(broken(8, {0xff, 0xff}).orientation >= 1);
	CHECK(broken(4, {0xff, 0xff, 0xff, 0x7f}).camera().empty());
	CHECK(broken(4, {8, 0, 0, 0}).camera() == "Canon EOS R5");
	CHECK(broken(10 + 4, {0xff, 0xff, 0xff, 0xff}).make.empty());
	CHECK(broken(10 + 8, {0xff, 0xff, 0xff, 0xff}).make.empty());
	auto self = block;
	for (std::size_t i = 10; i + 12 <= self.size(); i += 12) {
		if (self[i] == 0x69 && self[i + 1] == 0x87) self[i + 8] = 8, self[i + 9] = self[i + 10] = self[i + 11] = 0;
	}
	CHECK(one::desktop::facts_of(self).orientation == 8);
	std::vector<std::uint8_t> empty_segment{0xff, 0xd8, 0xff, 0xe0, 0x00, 0x00};
	CHECK(one::desktop::facts_of(empty_segment).orientation == 1);
	std::vector<std::uint8_t> padded(4096, 0xff);
	padded[1] = 0xd8;
	CHECK(one::desktop::facts_of(padded).orientation == 1);
	auto huge = png;
	huge[33] = huge[34] = huge[35] = huge[36] = 0xff;
	CHECK(one::desktop::facts_of(huge).orientation == 8);  // read as far as the file goes
	huge[8] = huge[9] = huge[10] = huge[11] = 0xff;
	CHECK(one::desktop::facts_of(huge).orientation == 1);  // a chunk before it says it's larger than the file
	auto riff = webp;
	riff[34] = riff[35] = riff[36] = riff[37] = 0xff;
	CHECK(one::desktop::facts_of(riff).orientation >= 1);
	// A date that isn't one, or a camera's empty one, isn't when it was taken.
	for (std::string taken : {"0000:00:00 00:00:00", "2026:13:01 00:00:00", "2026:02:30 10:00:00", "2026-10-12 08:30:15", "    :  :     :  :  ", "2026:10:12"}) {
		CHECK_SAYING(!(one::desktop::photo_facts{.taken = taken}.when()), taken);
	}
	CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));
}

TEST("a camera is its make and model, the make said once when the model starts with it") {
	using one::desktop::photo_facts;
	CHECK((photo_facts{.make = "Canon", .model = "Canon EOS R5"}.camera() == "Canon EOS R5"));
	CHECK((photo_facts{.make = "NIKON CORPORATION", .model = "NIKON Z 6"}.camera() == "NIKON Z 6"));
	CHECK((photo_facts{.make = "Apple", .model = "iPhone 15 Pro"}.camera() == "Apple iPhone 15 Pro"));
	CHECK((photo_facts{.make = "samsung", .model = "SAMSUNG SM-S918B"}.camera() == "SAMSUNG SM-S918B"));
	CHECK((photo_facts{.make = "Leica"}.camera() == "Leica"));
	CHECK((photo_facts{.model = "X100VI"}.camera() == "X100VI"));
	// Without an offset, it's when it was where this runs.
	photo_facts here{.taken = "2026:07:01 12:00:00"};
	auto local = std::chrono::local_days(std::chrono::year(2026) / 7 / 1) + std::chrono::hours(12);
	CHECK(here.when() && *here.when() == std::chrono::floor<std::chrono::seconds>(std::chrono::current_zone()->to_sys(local, std::chrono::choose::earliest)));
}

int main() { return check::run(); }
