// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

#pragma once

// Colors as a theme writes them, #rrggbb: how far apart two are for reading, as WCAG
// measures it, and the nearest color to one that reads on another, its hue and
// chroma kept and only its lightness moved, in OKLab, where lightness is as an eye
// sees it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace one::language::colors {

    inline std::array<double, 3> rgb(const std::string& hex) {
        auto channel = [&](std::size_t at) { return std::stoi(hex.substr(at, 2), nullptr, 16) / 255.0; };
        return {channel(1), channel(3), channel(5)};
    }

    inline double linear(double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); }
    inline double gamma(double v) { return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055; }

    inline double luminance(const std::string& hex) {
        auto [r, g, b] = rgb(hex);
        return 0.2126 * linear(r) + 0.7152 * linear(g) + 0.0722 * linear(b);
    }

    // How much one color stands out from another: 21 for black on white, 1 for the same.
    inline double contrast(const std::string& a, const std::string& b) {
        double x = luminance(a), y = luminance(b);
        if (x < y) std::swap(x, y);
        return (x + 0.05) / (y + 0.05);
    }

    inline std::array<double, 3> oklab(const std::string& hex) {
        auto [r0, g0, b0] = rgb(hex);
        double r = linear(r0), g = linear(g0), b = linear(b0);
        double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
        double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
        double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
        return {0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s, 1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s,
                0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s};
    }

    inline std::string hex(const std::array<double, 3>& lab) {
        auto [L, a, b] = lab;
        double l = std::pow(L + 0.3963377774 * a + 0.2158037573 * b, 3);
        double m = std::pow(L - 0.1055613458 * a - 0.0638541728 * b, 3);
        double s = std::pow(L - 0.0894841775 * a - 1.291485548 * b, 3);
        std::array<double, 3> out{4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
                                  -0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s};
        char text[8];
        auto byte = [](double v) { return static_cast<int>(std::lround(std::clamp(gamma(std::clamp(v, 0.0, 1.0)), 0.0, 1.0) * 255)); };
        std::snprintf(text, sizeof text, "#%02x%02x%02x", byte(out[0]), byte(out[1]), byte(out[2]));
        return text;
    }

    // The nearest color to one that reads on another at the contrast asked for: darker
    // on a light ground, lighter on a dark one. Empty when no lightness reaches it.
    inline std::string nearest(const std::string& color, const std::string& ground, double wanted = 4.5) {
        auto lab = oklab(color);
        double step = luminance(ground) > 0.18 ? -0.005 : 0.005;
        for (int i = 0; i < 200; ++i) {
            std::string tried = hex(lab);
            if (contrast(tried, ground) >= wanted) return tried;
            lab[0] += step;
            if (lab[0] < 0 || lab[0] > 1) break;
        }
        return "";
    }

    // Whether a ground is dark, so what reads on it is light.
    inline bool dark_ground(const std::string& hex) { return luminance(hex) < 0.18; }

    // Text on the accent, as on a button: white, or the page's own color, whichever
    // reads better.
    inline std::string on(const std::string& accent, const std::string& page) {
        return contrast("#ffffff", accent) >= contrast(page, accent) ? "#ffffff" : page;
    }

    inline std::string ratio(double contrast) {
        char text[16];
        std::snprintf(text, sizeof text, "%.2f:1", contrast);
        return text;
    }

} // namespace one::language::colors
