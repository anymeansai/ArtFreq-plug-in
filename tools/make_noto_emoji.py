#!/usr/bin/env python3
"""Builds assets/fonts/NotoEmoji-<Weight>.ttf from the npm package @fontsource/noto-emoji.

    npm pack @fontsource/noto-emoji@5.3.2 && tar xzf fontsource-noto-emoji-5.3.2.tgz
    pip install fonttools brotli
    python3 make_noto_emoji.py package/files 700 NotoEmoji-Bold.ttf

The package's "emoji" subset is the whole font in one WOFF (the numbered subsets are slices
of it by unicode range), layout tables included, so it is converted rather than merged: the
ccmp lookups that build skin tones, ZWJ sequences, keycaps and flags stay as Google made them.
Tables only vertical text, variable-font menus and screen hinting use are left out."""

import sys
from fontTools.ttLib import TTFont

files, weight, output = sys.argv[1], sys.argv[2], sys.argv[3]
font = TTFont(f"{files}/noto-emoji-emoji-{weight}-normal.woff")
font.flavor = None                     # plain TrueType, not WOFF

for tag in ("vhea", "vmtx", "STAT", "gasp"):
    if tag in font:
        del font[tag]

font["post"].formatType = 3.0          # no glyph names (JUCE and HarfBuzz use indices)
font.save(output)
print(output, "glyphs", len(font.getGlyphOrder()), "code points", len(font.getBestCmap()))
