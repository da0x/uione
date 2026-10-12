// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// one::desktop's tests: colors, text layout, and scenes drawn offscreen compared with the
// images in tests/golden. ONE_DESKTOP_GOLDEN=write draws them again; each is then looked at
// before it's committed.

#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "check.hpp"
#include "one/desktop/canvas.hpp"
#include "one/desktop/state.hpp"
#include "one/desktop/ui.hpp"
#include "offscreen.hpp"
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

int main() { return check::run(); }
