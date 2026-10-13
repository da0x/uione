// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

// A theme made at random from a palette: a seed picks a hue and how vivid it is, a
// scheme the hues around it, and every role of a theme comes from them, light and
// dark. Danger, warning and success stay red, amber and green, so they mean what
// they always do, each pulled a little toward the palette's hue and given its
// vividness. Every color then reads on what it sits on, as the compiler checks a
// theme, so the theme written out is one a project can say as it is.
//
// @uione/react's palette.ts is this, line for line, for the web: a seed is the same
// theme in every app, which screens/tests/golden/palettes.txt holds both to.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "one/screens/colors.hpp"

namespace one::screens::palette {

	// The ways the hues around the palette's are picked.
	inline constexpr std::array<std::string_view, 5> schemes{"complementary", "analogous", "triadic", "split_complementary", "monochrome"};

	// A theme's roles, in the order a theme says them.
	inline constexpr std::array<std::string_view, 10> roles{"page", "surface", "sunken", "ink", "muted", "line", "accent", "danger", "success", "warning"};

	struct color_pair {
		std::string light, dark;
	};

	struct theme {
		std::uint32_t seed = 0;
		std::string scheme;
		std::array<color_pair, 10> colors;  // in the order of roles

		const color_pair& operator[](std::string_view role) const { return colors[at(role)]; }
		color_pair& operator[](std::string_view role) { return colors[at(role)]; }
		static std::size_t at(std::string_view role) { return static_cast<std::size_t>(std::ranges::find(roles, role) - roles.begin()) % roles.size(); }
	};

	namespace detail {

		// Numbers from a seed, the same in C++ and JavaScript: mulberry32.
		class numbers {
		public:
			explicit numbers(std::uint32_t seed) : state_(seed) {}
			double next() {
				state_ += 0x6D2B79F5u;
				std::uint32_t t = state_;
				t = (t ^ (t >> 15)) * (t | 1u);
				t ^= t + (t ^ (t >> 7)) * (t | 61u);
				return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
			}

		private:
			std::uint32_t state_;
		};

		// The hues around the palette's, by its scheme, in degrees from it.
		inline std::vector<double> around(std::string_view scheme) {
			if (scheme == "complementary") return {0, 180};
			if (scheme == "analogous") return {0, 30, -30};
			if (scheme == "triadic") return {0, 120, 240};
			if (scheme == "split_complementary") return {0, 150, 210};
			return {0};
		}

		// Whether a color in OKLab is one a screen can show, before it's rounded.
		inline bool shown(double L, double a, double b) {
			double l = std::pow(L + 0.3963377774 * a + 0.2158037573 * b, 3);
			double m = std::pow(L - 0.1055613458 * a - 0.0638541728 * b, 3);
			double s = std::pow(L - 0.0894841775 * a - 1.291485548 * b, 3);
			for (double v : {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
			                 -0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s}) {
				if (v < -0.0001 || v > 1.0001) return false;
			}
			return true;
		}

		// A color by lightness, chroma and hue in degrees, made less vivid until a
		// screen can show it.
		inline std::string oklch(double L, double C, double hue) {
			double radians = hue * std::numbers::pi / 180;
			for (int i = 0; i < 60 && !shown(L, C * std::cos(radians), C * std::sin(radians)); ++i) C *= 0.92;
			return colors::hex({L, C * std::cos(radians), C * std::sin(radians)});
		}

		// A hue moved toward another by a share of the way, no further than most degrees.
		inline double toward(double from, double to, double share, double most) {
			double apart = std::fmod(to - from + 540, 360) - 180;
			return from + std::clamp(apart * share, -most, most);
		}

		// A color that reads at 4.5:1 on each ground, by lightness alone.
		inline std::string reads(std::string color, const std::vector<std::string>& grounds) {
			for (int round = 0; round < 3; ++round) {
				const std::string* worst = &grounds.front();
				for (const auto& g : grounds) {
					if (colors::contrast(color, g) < colors::contrast(color, *worst)) worst = &g;
				}
				if (colors::contrast(color, *worst) >= 4.5) return color;
				std::string moved = colors::nearest(color, *worst);
				if (moved.empty()) return colors::dark_ground(*worst) ? "#ffffff" : "#000000";
				color = moved;
			}
			return color;
		}

		// An accent that text reads on, white or the page's: darker on a light page,
		// lighter on a dark one, still reading on the page and on panels.
		inline std::string pressable(std::string accent, const std::string& page, const std::string& surface) {
			auto lab = colors::oklab(accent);
			double step = colors::dark_ground(page) ? 0.005 : -0.005;
			for (int i = 0; i < 200 && colors::contrast(colors::on(accent, page), accent) < 4.5; ++i) {
				lab[0] = std::clamp(lab[0] + step, 0.0, 1.0);
				accent = colors::hex(lab);
			}
			return reads(accent, {page, surface});
		}

	}  // namespace detail

	// The theme a seed and a scheme make.
	inline theme generate(std::uint32_t seed, std::string_view scheme) {
		detail::numbers random(seed);
		double hue = random.next() * 360;
		double vivid = 0.09 + random.next() * 0.09;  // the accent's chroma
		double tint = 0.008 + random.next() * 0.02;  // how much the grounds take the hue
		auto offsets = detail::around(scheme);
		double ground = hue + offsets[1 % offsets.size()], quiet = hue + offsets[offsets.size() > 2 ? 2 : offsets.size() - 1];
		theme t{.seed = seed, .scheme = std::string(scheme), .colors = {}};
		auto set = [&](std::string_view role, std::string light, std::string dark) { t[role] = {std::move(light), std::move(dark)}; };
		using detail::oklch;
		set("page", oklch(0.985, tint * 0.6, ground), oklch(0.16, tint * 1.2, ground));
		set("surface", oklch(0.997, tint * 0.3, ground), oklch(0.205, tint * 1.3, ground));
		set("sunken", oklch(0.95, tint, ground), oklch(0.13, tint * 1.1, ground));
		set("ink", oklch(0.22, std::min(0.04, vivid * 0.25), ground), oklch(0.95, tint * 0.6, ground));
		set("muted", oklch(0.5, vivid * 0.25, quiet), oklch(0.74, vivid * 0.2, quiet));
		set("line", oklch(0.89, tint * 1.2, ground), oklch(0.32, tint * 1.4, ground));
		set("accent", oklch(0.52, vivid + 0.03, hue), oklch(0.76, vivid, hue));
		// Red, amber and green, leaning to the palette.
		double danger = detail::toward(27, hue, 0.2, 14), warning = detail::toward(80, hue, 0.2, 12), success = detail::toward(150, hue, 0.2, 14);
		set("danger", oklch(0.54, std::clamp(vivid + 0.06, 0.13, 0.21), danger), oklch(0.72, std::clamp(vivid + 0.04, 0.12, 0.19), danger));
		set("warning", oklch(0.62, std::clamp(vivid + 0.04, 0.11, 0.17), warning), oklch(0.82, std::clamp(vivid + 0.02, 0.1, 0.16), warning));
		set("success", oklch(0.55, std::clamp(vivid + 0.02, 0.1, 0.17), success), oklch(0.76, std::clamp(vivid, 0.1, 0.16), success));
		// Each reads on what it sits on, as the compiler asks of a theme.
		for (bool dark : {false, true}) {
			auto at = [&](std::string_view role) -> std::string& { return dark ? t[role].dark : t[role].light; };
			std::string page = at("page"), surface = at("surface");
			for (auto role : {"ink", "muted"}) at(role) = detail::reads(at(role), {page, surface});
			at("accent") = detail::pressable(at("accent"), page, surface);
			for (auto role : {"danger", "success", "warning"}) at(role) = detail::reads(at(role), {surface});
		}
		return t;
	}

	// What a scheme is called: Split complementary.
	inline std::string scheme_title(std::string_view scheme) {
		std::string out(scheme);
		std::ranges::replace(out, '_', ' ');
		if (!out.empty()) out[0] = static_cast<char>(out[0] - 'a' + 'A');
		return out;
	}

	// The theme as a project says it, to paste into a .one file and name in its
	// project block: theme triadic_48213.
	inline std::string name_of(const theme& t) { return t.scheme + "_" + std::to_string(t.seed); }

	inline std::string written(const theme& t) {
		std::string out = "define theme " + name_of(t) + " \"" + scheme_title(t.scheme) + " " + std::to_string(t.seed) + "\" {\n";
		for (std::size_t i = 0; i < roles.size(); ++i) {
			std::string role(roles[i]);
			out += "\t" + role + std::string(9 - role.size(), ' ') + t.colors[i].light + "  dark " + t.colors[i].dark + "\n";
		}
		return out + "}\n";
	}

}  // namespace one::screens::palette
