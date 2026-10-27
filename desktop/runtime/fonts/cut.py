# Copyright 2026 Daher Alfawares
# SPDX-License-Identifier: AGPL-3.0-only

# Cuts the faces a paper is set in down to the Latin characters, every weight and
# optical size kept, as the web's are: Literata for book and Fraunces for lyric, each
# upright and italic, from Google Fonts' variable files, under the SIL Open Font
# License. Run again with newer files beside it; it needs fontTools.
#
#   python3 desktop/runtime/fonts/cut.py Literata.ttf Literata-Italic.ttf Fraunces.ttf Fraunces-Italic.ttf

import sys
from pathlib import Path
from fontTools import subset
from fontTools.ttLib import TTFont

here = Path(__file__).parent
latin = set(range(0x20, 0x250)) | set(range(0x2000, 0x2070)) | set(range(0x2190, 0x2200))
latin |= {0x20AC, 0x2122, 0x2212, 0x2026, 0xFB00, 0xFB01, 0xFB02, 0xFB03, 0xFB04}
for source in sys.argv[1:]:
    font = TTFont(source)
    options = subset.Options()
    options.layout_features = ["*"]
    options.drop_tables = []
    options.name_IDs = ["*"]
    options.notdef_outline = True
    cut = subset.Subsetter(options)
    cut.populate(unicodes=latin)
    cut.subset(font)
    font.save(here / Path(source).name.lower())
