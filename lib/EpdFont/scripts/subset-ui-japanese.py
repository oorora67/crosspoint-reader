#!/usr/bin/env python3
"""Cut BIZ UDGothic down to the glyphs the Japanese UI needs.

The built-in UI fonts (Ubuntu 10/12, Noto Sans 8) have no Japanese glyphs.
convert-builtin-fonts.sh appends these subset files to their font stacks, so the
Japanese UI draws without an SD card font. Only the characters of
lib/I18n/translations/japanese.yaml are kept, plus all kana and CJK punctuation
(for short dynamic text such as kana book titles); the full font would not fit
in the firmware partition.

Rerun after changing japanese.yaml, then rerun convert-builtin-fonts.sh:

    python subset-ui-japanese.py BIZUDGothic-Regular.ttf BIZUDGothic-Bold.ttf

The full fonts are on Google Fonts (OFL):
https://github.com/google/fonts/tree/main/ofl/bizudgothic
"""

import argparse
import os
import re

from fontTools import subset

HERE = os.path.dirname(os.path.abspath(__file__))
TRANSLATION = os.path.join(HERE, "../../I18n/translations/japanese.yaml")
OUT_DIR = os.path.join(HERE, "../builtinFonts/source/BIZUDGothic")

# Ranges always kept: CJK symbols and punctuation, hiragana, katakana, and the
# fullwidth forms of ASCII.
ALWAYS = [(0x3000, 0x30FF), (0xFF01, 0xFF5E)]
# Characters below this come from the Latin UI fonts; never take them from here.
CJK_START = 0x2E80


def ui_codepoints():
    cps = set()
    for lo, hi in ALWAYS:
        cps.update(range(lo, hi + 1))
    with open(TRANSLATION, encoding="utf-8") as f:
        for line in f:
            match = re.match(r'^[A-Z_]+:\s*"(.*)"\s*$', line)
            if not match:
                continue
            cps.update(ord(c) for c in match.group(1) if ord(c) >= CJK_START)
    return sorted(cps)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("fonts", nargs="+", help="full BIZ UDGothic .ttf files")
    args = parser.parse_args()

    cps = ui_codepoints()
    os.makedirs(OUT_DIR, exist_ok=True)
    options = subset.Options()
    options.layout_features = []  # the converter reads cmap and glyf only
    options.name_IDs = ["*"]  # keep copyright and license names (OFL)
    for path in args.fonts:
        font = subset.load_font(path, options)
        subsetter = subset.Subsetter(options)
        subsetter.populate(unicodes=cps)
        subsetter.subset(font)
        out = os.path.join(OUT_DIR, os.path.basename(path))
        subset.save_font(font, out, options)
        print(f"{out}: {len(font.getBestCmap())} glyphs")


if __name__ == "__main__":
    main()
