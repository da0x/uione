# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# Writes what one::math and math.ts set equations by: the math font's measures, read
# once from its file, so neither asks a font library or a browser how wide anything is
# and both place every glyph in the same spot. Each character's advance and outline box,
# and the constants of its OpenType MATH table, in its own units. Run again when the
# font changes; it needs fontTools.
#
#   python3 math/font/measure.py

import json
from pathlib import Path
from fontTools.ttLib import TTFont

here = Path(__file__).parent
font = TTFont(here / "noto-sans-math.ttf")

# The characters an equation is set in: the printable ASCII, Greek, the math italic
# letters, and the signs the notation's words and symbols are drawn as.
wanted = set(range(0x20, 0x7F))
wanted |= set(range(0x391, 0x3AA)) | set(range(0x3B1, 0x3CA))
wanted |= set(range(0x1D434, 0x1D468)) | set(range(0x1D44E, 0x1D482)) | {0x210E}
wanted |= set(range(0x1D6E2, 0x1D71C))
wanted |= {0x00B1, 0x2213, 0x00D7, 0x22C5, 0x2212, 0x2264, 0x2265, 0x2260, 0x2248, 0x2192, 0x21D2, 0x21D4, 0x2208, 0x221E, 0x2202, 0x221A, 0x2211, 0x220F,
           0x222B, 0x2032, 0x2033, 0x230A, 0x230B, 0x2308, 0x2309, 0x2016, 0x02C6, 0x00AF, 0x20D7, 0x02D9, 0x00A8, 0x2026, 0x22EF}

cmap = font.getBestCmap()
glyphs = font.getGlyphSet()
widths = font["hmtx"].metrics
boxes = font["glyf"]
out = {}
for code in sorted(wanted):
    name = cmap.get(code)
    if not name:
        continue
    g = boxes[name]
    box = [g.xMin, g.yMin, g.xMax, g.yMax] if hasattr(g, "xMin") and g.numberOfContours != 0 else [0, 0, 0, 0]
    out[code] = [widths[name][0], *box]

c = font["MATH"].table.MathConstants
names = ["ScriptPercentScaleDown", "ScriptScriptPercentScaleDown", "AxisHeight", "AccentBaseHeight", "SubscriptShiftDown", "SubscriptTopMax",
         "SubscriptBaselineDropMin", "SuperscriptShiftUp", "SuperscriptShiftUpCramped", "SuperscriptBottomMin", "SuperscriptBaselineDropMax",
         "SubSuperscriptGapMin", "SpaceAfterScript", "UpperLimitGapMin", "UpperLimitBaselineRiseMin", "LowerLimitGapMin", "LowerLimitBaselineDropMin",
         "FractionNumeratorShiftUp", "FractionNumeratorDisplayStyleShiftUp", "FractionDenominatorShiftDown", "FractionDenominatorDisplayStyleShiftDown",
         "FractionNumeratorGapMin", "FractionNumDisplayStyleGapMin", "FractionRuleThickness", "FractionDenominatorGapMin", "FractionDenomDisplayStyleGapMin",
         "OverbarVerticalGap", "OverbarRuleThickness", "RadicalVerticalGap", "RadicalDisplayStyleVerticalGap", "RadicalRuleThickness", "RadicalExtraAscender",
         "RadicalKernBeforeDegree", "RadicalKernAfterDegree", "RadicalDegreeBottomRaisePercent"]
constants = {}
for n in names:
    v = getattr(c, n)
    constants[n] = v if isinstance(v, int) else v.Value

measures = {"font": "Noto Sans Math", "units": font["head"].unitsPerEm, "ascender": font["hhea"].ascent, "descender": font["hhea"].descent,
            "constants": constants, "glyphs": {str(k): v for k, v in out.items()}}
(here / "measures.json").write_text(json.dumps(measures, separators=(",", ":")) + "\n")
# And as a module of @uione/react's, which can't reach outside its own folder once it's
# published.
(here.parent.parent / "packages" / "react" / "src" / "math-measures.ts").write_text(
    "// Copyright 2026 Daher Alfawares\n// SPDX-License-Identifier: AGPL-3.0-only\n\n"
    "// Written by math/font/measure.py from the math font. Don't edit; run it again.\n\n"
    "export const measures = " + json.dumps(measures, separators=(",", ":")) + " as const;\n")

# The same for C++, in the one header that includes it.
lines = ["// Copyright 2026 Daher Alfawares", "// SPDX-License-Identifier: AGPL-3.0-only", "",
         "// Written by math/font/measure.py from the math font. Don't edit; run it again.", "",
         "#pragma once", "", "#include <array>", "#include <cstdint>", "", "namespace one::math::measures {", "",
         f'\tinline constexpr const char* font = "{measures["font"]}";',
         f"\tinline constexpr int units = {measures['units']}, ascender = {measures['ascender']}, descender = {measures['descender']};", ""]
for n, v in constants.items():
    snake = "".join("_" + ch.lower() if ch.isupper() else ch for ch in n).lstrip("_")
    lines.append(f"\tinline constexpr int {snake} = {v};")
lines += ["", "\t// Each character: its code, advance, and outline box from the bottom left.",
          "\tstruct glyph {", "\t\tstd::uint32_t code;", "\t\tint advance, x_min, y_min, x_max, y_max;", "\t};",
          f"\tinline constexpr std::array<glyph, {len(out)}> glyphs{{{{"]
for code, (adv, x0, y0, x1, y1) in out.items():
    lines.append(f"\t\t{{{code}, {adv}, {x0}, {y0}, {x1}, {y1}}},")
lines += ["\t}};", "", "}  // namespace one::math::measures", ""]
(here.parent / "include" / "one" / "math_measures.hpp").write_text("\n".join(lines))
print(len(out), "characters,", len(constants), "constants")

# And the font itself for @uione/radix, cut down to the same characters, as the web sets
# nothing else. Woff, so a browser fetches it compressed.
from fontTools import subset
options = subset.Options()
options.flavor = "woff"
options.layout_features = ["*"]
options.drop_tables = []
cut = TTFont(here / "noto-sans-math.ttf")
subsetter = subset.Subsetter(options)
subsetter.populate(unicodes=wanted)
subsetter.subset(cut)
fonts = here.parent.parent / "packages" / "radix" / "src" / "fonts"
fonts.mkdir(exist_ok=True)
cut.flavor = "woff"
cut.save(fonts / "noto-sans-math.woff")
