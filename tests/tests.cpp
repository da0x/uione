// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: Apache-2.0

// loom's tests: colors, text layout, and scenes drawn offscreen compared with the
// images in tests/golden. LOOM_GOLDEN=write draws them again; each is then looked at
// before it's committed.

#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "check.hpp"
#include "loom/canvas.hpp"
#include "loom/state.hpp"
#include "loom/ui.hpp"
#include "offscreen.hpp"
#include "ui_internal.hpp"

namespace {

	const std::filesystem::path golden_folder = LOOM_TESTS "/golden";

	// A scene drawn and compared with its golden image, allowing small differences
	// from one GPU's rounding to another's.
	void matches_golden(const std::string& name, loom::size size, double scale, const std::function<void(loom::canvas&)>& paint) {
		auto drawn = loom::offscreen::draw(size, scale, loom::color::hex(0xf7f8fa), paint);
		auto file = golden_folder / (name + ".png");
		const char* mode = std::getenv("LOOM_GOLDEN");
		if (mode && std::string(mode) == "write") {
			loom::offscreen::write_png(file, drawn);
			std::println("     wrote {}", file.string());
			return;
		}
		auto golden = loom::offscreen::read_png(file);
		CHECK_SAYING(golden.has_value(), "no golden image " + file.string() + "; draw it with LOOM_GOLDEN=write and look at it");
		if (!golden) return;
		std::size_t different = loom::offscreen::differences(drawn, *golden, 3);
		std::size_t allowed = drawn.rgba.size() / 4 / 2000;  // a twentieth of a percent
		if (different > allowed) loom::offscreen::write_png(std::filesystem::temp_directory_path() / (name + ".drawn.png"), drawn);
		CHECK_SAYING(different <= allowed, std::format("{} pixels differ from {}, more than {}; what was drawn is in {}", different, file.string(), allowed,
		                                               (std::filesystem::temp_directory_path() / (name + ".drawn.png")).string()));
	}

	const loom::text_style body{.family = "Noto Sans", .size = 15};

	void boxes(loom::canvas& c) {
		using loom::color;
		c.shadow({24, 24, 160, 96}, 10, 16, color::hex(0x0e1726, 0.2f), {0, 6});
		c.box({24, 24, 160, 96}, {.fill = color::hex(0xffffff), .border = color::hex(0xdde2ea), .border_width = 1, .radius = 10});
		c.box({208, 24, 96, 96}, {.fill = color::hex(0x2457d6), .radius = 48});
		c.box({328, 24, 96, 96}, {.fill = color::hex(0xc2312d, 0.5f), .border = color::hex(0xc2312d), .border_width = 3, .radius = 0});
		auto clipped = c.clip({24, 140, 200, 40});
		c.box({0, 120, 400, 80}, {.fill = color::hex(0x1a7f37)});
	}

	void words(loom::canvas& c) {
		std::vector<loom::text_span> spans{
		    {"Renaming a phase keeps its old ", body},
		    {"name", {.family = "Noto Sans Mono", .size = 14, .color = loom::color::hex(0x2457d6)}},
		    {", so a new phase titled the same is ", body},
		    {"refused", {.family = "Noto Sans", .size = 15, .weight = 700}},
		    {". Underlined, ", body},
		    {"struck", {.family = "Noto Sans", .size = 15, .strike = true}},
		    {" and ", body},
		    {"linked", {.family = "Noto Sans", .size = 15, .color = loom::color::hex(0x2457d6), .underline = true}},
		    {".", body},
		};
		c.text(loom::lay_out(spans, 300, c.scale()), {20, 16});
	}

}  // namespace

TEST("a color written in sRGB is kept in linear light") {
	CHECK(loom::color::hex(0xffffff).red == 1.f);
	CHECK(loom::color::hex(0x000000).green == 0.f);
	CHECK(std::abs(loom::color::hex(0x808080).blue - 0.2158605f) < 1e-5f);
}

TEST("text wraps between words to fit its width, and a newline starts a line") {
	auto wide = loom::lay_out({"Open project tracking for everyone, with every change kept.", body}, 0, 1);
	CHECK(wide.lines().size() == 1);
	auto narrow = loom::lay_out({"Open project tracking for everyone, with every change kept.", body}, 150, 1);
	CHECK(narrow.lines().size() > 1);
	for (const auto& line : narrow.lines()) CHECK_SAYING(line.width <= 150, std::format("a line {} wide", line.width));
	CHECK(narrow.height() > wide.height());
	auto broken = loom::lay_out({"first\nsecond\n\nfourth", body}, 0, 1);
	CHECK(broken.lines().size() == 4);
	auto empty = loom::lay_out({"", body}, 0, 1);
	CHECK(empty.lines().size() == 1);
	CHECK(empty.height() > 0);
}

TEST("text laid out at a larger scale is the same size, in more pixels") {
	auto one = loom::lay_out({"Take it", body}, 0, 1);
	auto more = loom::lay_out({"Take it", body}, 0, 1.5);
	CHECK(std::abs(one.width() - more.width()) < 2);
	CHECK(std::abs(one.height() - more.height()) < 1);
}

TEST("boxes: fills, borders, corners, a shadow and a clip, at scale 1") { matches_golden("boxes-1", {448, 200}, 1, boxes); }
TEST("boxes at scale 1.5") { matches_golden("boxes-1.5", {448, 200}, 1.5, boxes); }
TEST("text: styles, wrapping, underline and strike, at scale 1") { matches_golden("text-1", {340, 140}, 1, words); }
TEST("text at scale 1.5") { matches_golden("text-1.5", {340, 140}, 1.5, words); }

namespace {

	// The tests' theme: harbor in a font every machine running them has.
	const loom::theme& tested_theme() {
		static loom::theme t = [] {
			loom::theme made = loom::theme::harbor();
			made.text_family = "Noto Sans";
			made.code_family = "Noto Sans Mono";
			return made;
		}();
		return t;
	}

	// A view built and placed in an area, as a window would.
	std::unique_ptr<loom::element> placed(loom::view v, loom::rect area) {
		loom::detail::theme_in_use() = &tested_theme();
		auto root = std::move(v).take();
		root->place(area);
		return root;
	}

	loom::view rows_of(const std::vector<std::string>& keys, std::string* opened = nullptr) {
		std::vector<loom::list_row> rows;
		for (const auto& k : keys) rows.push_back({k, loom::text("Issue " + k)});
		return loom::list(std::move(rows), [opened](const std::string& k) {
			if (opened) *opened = k;
		});
	}

	loom::element* first_list(loom::element& e) {
		if (e.prefers_focus()) return &e;
		for (auto& child : e.children) {
			if (auto* found = first_list(*child)) return found;
		}
		return nullptr;
	}

	loom::key_event key(std::uint32_t symbol, const char* text = "") { return {.symbol = symbol, .text = text}; }

}  // namespace

TEST("a state tells what read it when it changes, once, and not after it's gone") {
	loom::state<int> count = 1;
	int told = 0;
	auto reader = std::make_shared<loom::detail::reader>();
	reader->changed = [&] { ++told; };
	{
		loom::detail::reading noting(reader);
		CHECK(*count == 1);
		CHECK(*count == 1);  // read twice, told once
	}
	count.set(2);
	CHECK(told == 1);
	count.set(3);  // read before, but not since
	CHECK(told == 1);
	{
		loom::detail::reading noting(reader);
		(void)*count;
	}
	reader.reset();
	count.set(4);
	CHECK(told == 1);
}

TEST("a column stacks with spacing inside its padding, and a row shares what's left among those that grow") {
	auto root = placed(loom::column(loom::text("one").height(20), loom::text("two").height(30)).spacing(10).padding(5), {0, 0, 200, 300});
	CHECK(root->children[0]->frame.y == 5);
	CHECK(root->children[1]->frame.y == 35);
	CHECK(root->children[1]->frame.width == 190);
	auto across = placed(loom::row(loom::text("a").width(40), loom::spacer(), loom::text("b").width(60).grow(2), loom::edge(loom::color::hex(0xc2312d))).spacing(10),
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
	CHECK(l.handle_key(key(loom::keys::down)));
	CHECK(l.handle_key(key('j', "j")));
	CHECK(l.handle_key(key(loom::keys::enter)));
	CHECK(opened == "c");
	CHECK(l.handle_key(key('k', "k")));
	CHECK(l.handle_key(key(loom::keys::enter)));
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
	loom::rect window{0, 0, 300, 200};
	auto before = placed(loom::scroll(rows_of(keys)), window);
	auto& list = *first_list(*before);
	for (int i = 0; i < 25; ++i) list.handle_key(key(loom::keys::down));
	std::string opened;
	list.handle_key(key(loom::keys::enter));
	// The chosen row was brought into view.
	auto& chosen = *list.children[25];
	CHECK(chosen.frame.y >= window.y && chosen.frame.bottom() <= window.bottom());

	std::vector<std::string> turned(keys.rbegin(), keys.rend());
	auto after = std::move(loom::scroll(rows_of(turned, &opened))).take();
	loom::detail::reconcile(*after, *before);
	after->place(window);
	auto& relisted = *first_list(*after);
	relisted.handle_key(key(loom::keys::enter));
	CHECK(opened == "issue-25");  // the same issue, now at another place
	auto& moved = *relisted.children[14];  // where issue-25 is now, scrolled back into view
	CHECK(moved.frame.y >= window.y && moved.frame.bottom() <= window.bottom());

	std::vector<std::string> fewer(keys.begin(), keys.begin() + 10);
	auto shorter = std::move(loom::scroll(rows_of(fewer, &opened))).take();
	loom::detail::reconcile(*shorter, *after);
	shorter->place(window);
	first_list(*shorter)->handle_key(key(loom::keys::enter));
	CHECK(opened == "issue-9");  // gone, so the nearest that's left
}

TEST("a view: rows, a list, buttons, an avatar and a divider, at scale 1") {
	matches_golden("view-1", {520, 260}, 1, [](loom::canvas& c) {
		loom::detail::theme_in_use() = &tested_theme();
		const auto& t = tested_theme();
		auto root = placed(loom::column(loom::row(loom::text("Issues", t.heading()), loom::spacer(), loom::button("Turn around", [] {}),
		                                          loom::button("Add an issue", [] {}, true))
		                                    .spacing(10),
		                                loom::divider(),
		                                loom::row(loom::avatar("Maya Garcia"), loom::text("Maya Garcia reported it", t.quiet())).spacing(8),
		                                rows_of({"12", "16", "17"}))
		                       .spacing(12)
		                       .padding(20),
		                   {0, 0, 520, 260});
		root->paint(c);
	});
}

int main() { return check::run(); }
