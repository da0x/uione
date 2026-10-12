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
#include "offscreen.hpp"

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

int main() { return check::run(); }
