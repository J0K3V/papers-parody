"""Repair the pixel font so browsers accept it.

The original pixelplay.ttf ships with a broken format 4 cmap subtable: it maps
characters onto glyph ids past the end of the font. Pillow shrugs this off, but
every browser runs the file through a sanitiser first and rejects it outright,
which silently drops the whole look on the web.

This rewrites the cmap with only the mappings that point at real glyphs, then
saves a cleaned ttf plus a woff2 for the web. Metrics are untouched, so the
desktop renderer keeps producing identical frames.
"""

import shutil
import sys
from pathlib import Path

from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent.parent
FONT = ROOT / "assets" / "fonts" / "pixelplay.ttf"
ORIGINAL = ROOT / "assets" / "fonts" / "pixelplay.original.ttf"
WOFF2 = ROOT / "assets" / "fonts" / "pixelplay.woff2"


def main() -> int:
    """Clean the font in place, keeping a copy of the original."""
    if not FONT.exists():
        print(f"Font not found: {FONT}")
        return 1

    source = ORIGINAL if ORIGINAL.exists() else FONT
    if not ORIGINAL.exists():
        shutil.copy2(FONT, ORIGINAL)
        print(f"Kept the untouched original as {ORIGINAL.name}")

    font = TTFont(source)
    known = set(font.getGlyphOrder())
    removed = 0

    for table in font["cmap"].tables:
        bad = [code for code, glyph in table.cmap.items() if glyph not in known]
        for code in bad:
            del table.cmap[code]
        removed += len(bad)

    ascent = font["hhea"].ascent
    descent = font["hhea"].descent
    units = font["head"].unitsPerEm

    font.save(FONT)
    print(f"Dropped {removed} dangling mapping(s) -> {FONT.name}")

    try:
        font.flavor = "woff2"
        font.save(WOFF2)
        print(f"Wrote {WOFF2.name}")
    except Exception as error:  # noqa: BLE001
        print(f"Could not write woff2 ({error}); the ttf alone will do.")

    print(f"Metrics unchanged: unitsPerEm={units} ascent={ascent} descent={descent}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
