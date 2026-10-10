#!/usr/bin/env python3
"""Regenerate the MMU display's LVGL fonts and icon macros.

Writes, relative to the repository root:

    src/ui/fonts/ui_font_inter_{12,12m,14,16,20,28}.c   Inter text faces
    src/ui/fonts/ui_font_icons_{18,24}.c                Material Design Icons
    src/ui/fonts/ui_fonts.h                             LV_FONT_DECLARE for all eight
    src/ui/ui_icons.h                                   one UI_ICON_* macro per glyph

The 18 px icon face is the one the UI draws with (UI_FONT_ICON); the 24 px
face exists only for empty-state illustrations (UI_FONT_ICON_L).

Two modes:

    python3 tools/gen_fonts.py            the restyled home screen's faces only:
                                          Geist and IBM Plex Mono (type.device
                                          sizes), the three CJK languages'
                                          faces, and the two severity glyphs,
                                          into new files, plus ui_fonts_home.h.
    python3 tools/gen_fonts.py --legacy   the Inter and icon faces above. DO NOT
                                          run this casually: every screen that
                                          predates the design system draws with
                                          them, and a regeneration (a different
                                          lv_font_conv, an added glyph) changes
                                          their output. They are committed and
                                          checked byte for byte by the U3 gate.

Every lv_font_conv call uses ``--format lvgl --lv-include lvgl.h --no-compress``:
compressed fonts are slower to draw on the ESP32, and this build runs the fonts
straight off flash. The text faces are 4 bpp, the CJK ones 2 bpp (CJK_BPP) -
flash is the constraint, and 2 bpp is what was measured to fit.

The character set of every face is read from ``design/strings/*.json`` on every
run, never hard-coded: the translations belong to another task and change after
this one. The collection mirrors tools/check_glyphs.mjs's collectCharacters().

lv_font_conv runs from the repository root with relative paths, so a generated
file's ``Opts:`` comment (which lv_font_conv writes from the command line) holds
no absolute path from the machine that generated it.

Icon names are resolved through the MDI font's cmap with fontTools, so a name
that the shipped font does not contain is a hard error (see MISSING_ICON_ALIASES
for the one documented exception).

Run from anywhere:  python3 tools/gen_fonts.py
"""

import json
import os
import re
import shutil
import subprocess
import sys

try:
    from fontTools.ttLib import TTFont
except ImportError:  # pragma: no cover - dependency check
    sys.exit("error: fontTools is required (pip install fonttools)")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(ROOT, "assets", "fonts")
OUT_DIR = os.path.join(ROOT, "src", "ui", "fonts")
ICONS_H = os.path.join(ROOT, "src", "ui", "ui_icons.h")
STRINGS_DIR = os.path.join(ROOT, "design", "strings")
CJK_DIR = os.path.join(FONT_DIR, "cjk")

# The repository root as a relative prefix. Every path handed to lv_font_conv
# goes through this: the tool copies the command line verbatim into the
# generated file's "Opts:" comment, and an absolute path there would put the
# generating machine's home directory into the repository.
REL_FONT_DIR = "assets/fonts"
REL_OUT_DIR = "src/ui/fonts"
REL_CJK_DIR = "assets/fonts/cjk"
REL_ICONS_H = "src/ui/ui_icons.h"

INTER_REGULAR = os.path.join(FONT_DIR, "Inter-Regular.ttf")
INTER_MEDIUM = os.path.join(FONT_DIR, "Inter-Medium.ttf")
INTER_SEMIBOLD = os.path.join(FONT_DIR, "Inter-SemiBold.ttf")
GEIST_REGULAR = os.path.join(FONT_DIR, "Geist-Regular.ttf")
GEIST_MEDIUM = os.path.join(FONT_DIR, "Geist-Medium.ttf")
GEIST_SEMIBOLD = os.path.join(FONT_DIR, "Geist-SemiBold.ttf")
PLEX_MONO = os.path.join(FONT_DIR, "IBMPlexMono-Regular.ttf")
MDI = os.path.join(FONT_DIR, "materialdesignicons-webfont.ttf")

# Latin-1 printable plus the handful of symbols the screens use:
# degree, middle dot, en/em dash, bullet, ellipsis, the three arrows.
TEXT_RANGE = "0x20-0x7E,0xB0,0xB7,0x2013,0x2014,0x2022,0x2026,0x2192,0x2193,0x2191"

# The restyled home screen's fixed part (docs/design/UNIFIED_UI.md 5, U3): ASCII
# plus the three symbols it needs - the infinite sign for the spare badge, the
# middle dot between name and meta, and the degree sign in the environment
# reading. The en dash and the arrow cover the toolhead message row, which the
# core composes. Kept deliberately small: every extra codepoint is flash.
# Above U+007E, HOME_RANGE gains the accents and Cyrillic the translations
# use, read from design/strings on every run (geist_range() below).
HOME_SYMBOLS = "0xB0,0xB7,0x2013,0x2026,0x2192,0x221E"
HOME_RANGE = "0x20-0x7E," + HOME_SYMBOLS

# The languages whose characters go into Geist itself: Latin-1 accented
# letters and Polish diacritics (Latin Extended-A) for de/es/fr/it/pt-BR/pl,
# and the Cyrillic alphabet for ru. The CJK languages get a face of their own
# (CJK_LANGUAGES below) and are deliberately not in this list.
GEIST_LANGUAGES = ["en", "de", "es", "fr", "it", "pl", "pt-BR", "ru"]

# code -> the Noto Sans CJK subset that draws it. Regional, not a pan-CJK
# face: the same character has different forms in Japanese, Korean and
# Simplified Chinese, and a Japanese reader must get the Japanese one.
# The full language code ("zh-Hans") does not fit a C symbol name, so the
# face is named after the country code: ui_font_cjk_<key>_<size>.
CJK_LANGUAGES = [
    ("ja", "ja", "JP"),
    ("ko", "ko", "KR"),
    ("zh-Hans", "zh", "SC"),
]

# The one place the CJK encoding is decided. 2 bpp, uncompressed: measured at
# 90.5 KB (ja) + 66.2 KB (ko) + 87.2 KB (zh-Hans) against 436.5 KB at 4 bpp,
# and the ESP32 app slot has 455,200 bytes free with a 128 KiB floor to keep.
# Change it here and the faces, the budget and the fallback chain move together.
CJK_BPP = 2

# name, ttf, px, fallback symbol
TEXT_FONTS = [
    ("ui_font_inter_12", INTER_REGULAR, 12, "ui_font_icons_18"),
    ("ui_font_inter_12m", INTER_MEDIUM, 12, "ui_font_icons_18"),
    ("ui_font_inter_14", INTER_MEDIUM, 14, "ui_font_icons_18"),
    ("ui_font_inter_16", INTER_SEMIBOLD, 16, "ui_font_icons_18"),
    ("ui_font_inter_20", INTER_SEMIBOLD, 20, "ui_font_icons_24"),
    ("ui_font_inter_28", INTER_SEMIBOLD, 28, "ui_font_icons_24"),
]

# Geist and IBM Plex Mono at the sizes design/tokens.json's `type.device` asks
# for: 20 semibold (title, tile label), 15 semibold (section), 13 medium (body),
# 12 regular (meta) and one 18 px mono for values. (symbol, ttf, px, fallback,
# range) - the range is carried per face so the two families can stay apart.
# The Geist fallback is filled in at run time with the size's language slot
# (slot_for()); the icons are what a slot holds for a non-CJK language.
HOME_FONTS = [
    ("ui_font_geist_20", GEIST_SEMIBOLD, 20, "ui_font_icons_24", HOME_RANGE),
    ("ui_font_geist_15", GEIST_SEMIBOLD, 15, "ui_font_icons_18", HOME_RANGE),
    ("ui_font_geist_13", GEIST_MEDIUM, 13, "ui_font_icons_18", HOME_RANGE),
    ("ui_font_geist_12", GEIST_REGULAR, 12, "ui_font_icons_18", HOME_RANGE),
    ("ui_font_plex_18", PLEX_MONO, 18, "ui_font_icons_18", HOME_RANGE),
]

# The four Geist sizes a CJK face is generated for, with the weight and the
# icon face each one falls back to. (size, weight, icon face) - the icon face
# is the one the Geist size uses today, so icons resolve through the chain
# Geist -> slot -> icons whatever the language.
CJK_SIZES = [
    (12, "Regular", "ui_font_icons_18"),
    (13, "Medium", "ui_font_icons_18"),
    (15, "Medium", "ui_font_icons_18"),
    (20, "Medium", "ui_font_icons_24"),
]

# Severity by shape as well as color (PRINCIPLES.md 6): an octagon with "!" for an
# error, a triangle with "!" for information. Two glyphs of the Material Design
# Icons face in a face of their own, so the icon face every other screen uses is
# left exactly as it was committed.
SEVERITY_ICONS = [
    ("alert-octagon-outline", "UI_HOME_ICON_ERROR"),
    ("alert-outline", "UI_HOME_ICON_INFO"),
]
SEVERITY_FONT = ("ui_font_sev_18", 18)

# The home screen's paging chevrons (ui_chevron.c), Material Design's own
# chevrons in a face of their own for the same reason. At 36 px, the size of
# their touch target, the glyph is an 11x18 px mark with a 3 px stroke.
CHEVRON_ICONS = [
    ("chevron-left", "UI_HOME_ICON_PAGE_LEFT"),
    ("chevron-right", "UI_HOME_ICON_PAGE_RIGHT"),
]
CHEVRON_FONT = ("ui_font_chevron_36", 36)

ICON_FONTS = [
    ("ui_font_icons_18", 18, "lv_font_montserrat_14"),
    ("ui_font_icons_24", 24, "lv_font_montserrat_20"),
]

# The icon set the screens may use. Keep this list in the documented order: it
# is also the order of the generated UI_ICON_* macros.
ICON_NAMES = [
    "chevron-left", "chevron-right", "menu", "apps", "bell", "bell-outline",
    "cog", "water", "water-percent", "thermometer", "fire", "fan",
    "heat-wave", "printer-3d-nozzle", "filament", "swap-horizontal",
    "swap-vertical", "refresh", "sync", "alert", "alert-circle",
    "information-outline", "check", "check-circle", "close", "pencil", "tag",
    "nfc-variant", "help-circle-outline", "power-plug", "timer-outline",
    "play", "pause", "stop", "arrow-down-bold", "arrow-up-bold", "eject",
    "link-variant", "chip", "format-list-bulleted", "home", "palette", "tune",
    "rotate-right", "dots-vertical", "tray-arrow-down",
    "plus", "minus", "pencil-outline",
]

# Names the shipped MDI webfont does not carry, with a documented stand-in.
# Keep this as small as possible and shout about it: a silent substitution
# would put the wrong picture on the screen.
MISSING_ICON_ALIASES = {
    # Added to MDI after the webfont this repo vendors; a two-circle "roll"
    # reads as a filament spool, which is the intent.
    "filament": "circle-double",
}


def collect_characters(language):
    """Every code point language's strings draw, collected the way
    tools/check_glyphs.mjs's collectCharacters() does: the {placeholder} names
    are removed first (they are not literal text, they are substituted), and a
    group marker with no text contributes nothing."""
    path = os.path.join(STRINGS_DIR, language + ".json")
    if not os.path.exists(path):
        sys.exit("error: no strings for %s (%s)" % (language, os.path.relpath(STRINGS_DIR, ROOT)))
    with open(path, encoding="utf-8") as f:
        document = json.load(f)
    strings = document.get("strings", document) if isinstance(document, dict) else document
    entries = strings if isinstance(strings, list) else list(strings.values())
    characters = set()
    for entry in entries:
        text = entry if isinstance(entry, str) else (entry or {}).get("text")
        if not isinstance(text, str):
            continue
        for character in re.sub(r"\{[^{}]+\}", "", text):
            characters.add(ord(character))
    return characters


def geist_characters():
    """What a Geist face has to draw: the whole of the fixed HOME range, plus
    every character above U+007E that a non-CJK language uses. The CJK
    languages are excluded - their characters live in a face of their own and
    putting them in Geist would put a Japanese reader's text in the Latin
    face's weight and proportions."""
    characters = set(range(0x20, 0x7F))
    for part in HOME_SYMBOLS.split(","):
        characters.add(int(part, 16))
    for language in GEIST_LANGUAGES:
        characters |= {cp for cp in collect_characters(language) if cp > 0x7E}
    return characters


def format_range(characters):
    """A --range argument: runs of consecutive code points collapsed, the rest
    listed in order, so the generated Opts: comment stays readable."""
    points = sorted(characters)
    parts = []
    start = previous = None
    for point in points + [None]:
        if start is not None and point == previous + 1:
            previous = point
            continue
        if start is not None:
            parts.append("0x%X" % start if start == previous
                         else "0x%X-0x%X" % (start, previous))
        start = previous = point
    return ",".join(parts)


def is_geist(symbol):
    return symbol.startswith("ui_font_geist_")


def cjk_characters(language, geist):
    """What the CJK face of @p language has to draw: every character that
    language's strings use which Geist does not. The CJK punctuation
    (、。「」（）：) is in here too - it is in the strings and in no Latin face."""
    return sorted(collect_characters(language) - geist)


def slot_for(symbol):
    """The writable per-size font slot a Geist face of @p symbol falls back to."""
    return "ui_font_cjk_slot_" + symbol.rsplit("_", 1)[1]


def lv_font_conv_bin():
    cand = os.path.expanduser("~/.local/bin/lv_font_conv")
    if os.path.exists(cand):
        return cand
    found = shutil.which("lv_font_conv")
    if found:
        return found
    sys.exit("error: lv_font_conv not found (~/.local/bin/lv_font_conv or $PATH)")


def icon_codepoints():
    """Map every name in ICON_NAMES to its MDI codepoint."""
    cmap = TTFont(MDI).getBestCmap()
    by_name = {}
    for cp, name in cmap.items():
        by_name.setdefault(name, cp)

    resolved = {}
    for name in ICON_NAMES:
        lookup = MISSING_ICON_ALIASES.get(name, name)
        if lookup not in by_name:
            sys.exit(
                "error: icon %r is not in %s (and has no alias). Add it to "
                "MISSING_ICON_ALIASES with a documented stand-in."
                % (name, os.path.relpath(MDI, ROOT))
            )
        if lookup != name:
            print("warning: icon %r is absent from the MDI webfont; "
                  "using %r as a documented stand-in" % (name, lookup))
        resolved[name] = by_name[lookup]
    return resolved


def check_range(fonts):
    """Every codepoint a face is asked to draw must exist in its TTF, or
    lv_font_conv silently drops it and the glyph comes out blank. The strings
    change after this task, so a translation that asks for a character no
    shipped face has has to fail here, loudly, and not become a blank box on
    the device."""
    for symbol, ttf, rng in fonts:
        cmap = TTFont(ttf).getBestCmap()
        missing = [cp for cp in expand_range(rng) if cp not in cmap]
        if missing:
            sys.exit("error: %s (%s) has no %s" % (
                symbol, os.path.relpath(ttf, ROOT),
                ", ".join("U+%04X" % cp for cp in missing)))


def expand_range(rng):
    """A --range argument as the code points it names."""
    points = []
    for part in rng.split(","):
        if "-" in part:
            lo, hi = (int(c, 16) for c in part.split("-"))
            points.extend(range(lo, hi + 1))
        else:
            points.append(int(part, 16))
    return points


def run_lv_font_conv(font_path, size, symbol, rng, fallback, out_path, bpp=4):
    """Run lv_font_conv on paths relative to the repository root (the caller
    has chdir'd there), so the Opts: comment it writes into the generated file
    is the same on every machine."""
    cmd = [
        lv_font_conv_bin(),
        "--font", font_path,
        "--size", str(size),
        "--bpp", str(bpp),
        "--format", "lvgl",
        "--lv-include", "lvgl.h",
        "--no-compress",
        "--lv-font-name", symbol,
        "--range", rng,
        "--lv-fallback", fallback,
        "-o", out_path,
    ]
    print("  " + symbol)
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stdout.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        sys.exit("error: lv_font_conv failed for %s" % symbol)
    if proc.stderr.strip():
        sys.stderr.write(proc.stderr)


def macro_name(name):
    return "UI_ICON_" + name.replace("-", "_").upper()


def utf8_escapes(cp):
    return "".join("\\x%02X" % b for b in chr(cp).encode("utf-8"))


def write_ui_icons(codepoints):
    lines = [
        "/**",
        " * @file ui_icons.h",
        " * Material Design Icons as UTF-8 string literals for labels.",
        " *",
        " * Generated by tools/gen_fonts.py - do not edit by hand.",
        " *",
        " * Every macro can be concatenated into a label with any Inter font",
        " * (they fall back to the icon fonts), e.g.",
        " *     lv_label_set_text(l, UI_ICON_FIRE \" Drying\");",
        " */",
        "",
        "#ifndef UI_ICONS_H",
        "#define UI_ICONS_H",
        "",
    ]
    for name in ICON_NAMES:
        cp = codepoints[name]
        note = name
        if MISSING_ICON_ALIASES.get(name):
            note += " (MDI %s)" % MISSING_ICON_ALIASES[name]
        lines.append('/* %s - U+%04X */' % (note, cp))
        lines.append('#define %-32s "%s"' % (macro_name(name), utf8_escapes(cp)))
    lines += ["", "#endif /* UI_ICONS_H */", ""]
    with open(ICONS_H, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def write_ui_fonts_h():
    lines = [
        "/**",
        " * @file ui_fonts.h",
        " * Declarations for the generated Inter and Material Design Icons fonts.",
        " *",
        " * Generated by tools/gen_fonts.py - do not edit by hand.",
        " */",
        "",
        "#ifndef UI_FONTS_H",
        "#define UI_FONTS_H",
        "",
        '#include "lvgl.h"',
        "",
    ]
    for symbol, _ttf, _size, _fb in TEXT_FONTS:
        lines.append("LV_FONT_DECLARE(%s);" % symbol)
    for symbol, _size, _fb in ICON_FONTS:
        lines.append("LV_FONT_DECLARE(%s);" % symbol)
    lines += ["", "#endif /* UI_FONTS_H */", ""]
    with open(os.path.join(OUT_DIR, "ui_fonts.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def relax_line_metrics(out_path, size):
    """Give a face a roomier line box than its own hhea metrics ask for.

    Geist ships a 1.0 em line box with a 0.17 em descender, so at 12 px a
    descender is 2 px and lv_font_conv emits ``line_height = 12``: a label box
    exactly as tall as the em, and the tail of every g, y and p is clipped.
    lv_font_conv has no line-height option, so the two numbers are rewritten
    here, to the 1.25 em box and 0.25 em descender the Inter faces in this
    build already use. The glyph bitmaps themselves are untouched.

    Only the home faces get this: the Inter faces are correct already, and
    rewriting theirs would move text on every screen that still uses them.
    """
    line_height = int(round(size * 1.25))
    base_line = int(round(size * 0.25))
    with open(out_path, encoding="utf-8") as f:
        text = f.read()
    for field, value in (("line_height", line_height), ("base_line", base_line)):
        text, n = re.subn(r"\.%s = \d+" % field, ".%s = %d" % (field, value), text, count=1)
        if n != 1:
            sys.exit("error: no .%s in %s" % (field, os.path.relpath(out_path, ROOT)))
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(text)


def write_ui_fonts_home_h(sev_codes, chevron_codes):
    lines = [
        "/**",
        " * @file ui_fonts_home.h",
        " * Declarations for the restyled home screen's faces (Geist, the CJK",
        " * faces, IBM Plex Mono, the severity glyphs and the paging chevrons),",
        " * kept apart from",
        " * ui_fonts.h so the older screens' faces stay exactly as committed.",
        " *",
        " * Generated by tools/gen_fonts.py - do not edit by hand.",
        " */",
        "",
        "#ifndef UI_FONTS_HOME_H",
        "#define UI_FONTS_HOME_H",
        "",
        '#include "lvgl.h"',
        "",
    ]
    for symbol, _ttf, _size, _fb, _rng in HOME_FONTS:
        lines.append("LV_FONT_DECLARE(%s);" % symbol)
    lines.append("LV_FONT_DECLARE(%s);" % SEVERITY_FONT[0])
    lines.append("LV_FONT_DECLARE(%s);" % CHEVRON_FONT[0])
    lines += [
        "",
        "/* The CJK faces: one per language, at the four Geist sizes. Which one",
        " * is in use is a run-time choice made by the slots below. */",
    ]
    for _code, key, _subset in CJK_LANGUAGES:
        for size, _weight, _icons in CJK_SIZES:
            lines.append("LV_FONT_DECLARE(ui_font_cjk_%s_%d);" % (key, size))
    lines += [
        "",
        "/* The per-size language slots (src/ui/ui_fonts_lang.c): the writable",
        " * lv_font_t the Geist face of that size falls back to. */",
    ]
    for size, _weight, _icons in CJK_SIZES:
        lines.append("extern lv_font_t %s;" % slot_for("ui_font_geist_%d" % size))
    lines.append("")
    lines.append("/* The severity glyphs, drawn with %s. */" % SEVERITY_FONT[0])
    for (name, macro), cp in zip(SEVERITY_ICONS, sev_codes):
        lines.append("/* %s - U+%04X */" % (name, cp))
        lines.append('#define %-24s "%s"' % (macro, utf8_escapes(cp)))
    lines.append("")
    lines.append("/* The paging chevrons, drawn with %s. */" % CHEVRON_FONT[0])
    for (name, macro), cp in zip(CHEVRON_ICONS, chevron_codes):
        lines.append("/* %s - U+%04X */" % (name, cp))
        lines.append('#define %-24s "%s"' % (macro, utf8_escapes(cp)))
    lines += ["", "#endif /* UI_FONTS_HOME_H */", ""]
    with open(os.path.join(OUT_DIR, "ui_fonts_home.h"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def main_home():
    # lv_font_conv runs from the repository root on relative paths: the Opts:
    # comment it writes into every generated file is the command line it was
    # given, and an absolute path there would name the generating machine.
    os.chdir(ROOT)
    os.makedirs(OUT_DIR, exist_ok=True)

    geist = geist_characters()
    geist_range_arg = format_range(geist)
    home_faces = [
        (symbol, ttf, size, icons, geist_range_arg if is_geist(symbol) else rng)
        for symbol, ttf, size, icons, rng in HOME_FONTS
    ]
    cjk_faces = cjk_font_faces(geist)
    check_range([(s, t, r) for s, t, _size, _fb, r in home_faces] + cjk_faces)

    print("home fonts (Geist + IBM Plex Mono, type.device sizes):")
    for symbol, ttf, size, icons, rng in home_faces:
        out = os.path.join(REL_OUT_DIR, symbol + ".c")
        fallback = slot_for(symbol) if is_geist(symbol) else icons
        run_lv_font_conv(os.path.relpath(ttf, ROOT), size, symbol, rng, fallback, out)
        relax_line_metrics(out, size)
        if is_geist(symbol):
            declare_slot(out, fallback)

    generate_cjk_fonts(cjk_faces)

    cmap = TTFont(MDI).getBestCmap()
    by_name = {}
    for cp, name in cmap.items():
        by_name.setdefault(name, cp)
    def mdi_codes(icons):
        codes = []
        for name, _macro in icons:
            if name not in by_name:
                sys.exit("error: icon %r is not in %s" % (name, os.path.relpath(MDI, ROOT)))
            codes.append(by_name[name])
        return codes

    codes = mdi_codes(SEVERITY_ICONS)
    print("severity glyphs:")
    symbol, size = SEVERITY_FONT
    run_lv_font_conv(os.path.relpath(MDI, ROOT), size, symbol, ",".join("0x%X" % c for c in codes),
                     "lv_font_montserrat_14", os.path.join(REL_OUT_DIR, symbol + ".c"))
    chevron_codes = mdi_codes(CHEVRON_ICONS)
    print("paging chevrons:")
    symbol, size = CHEVRON_FONT
    run_lv_font_conv(os.path.relpath(MDI, ROOT), size, symbol,
                     ",".join("0x%X" % c for c in chevron_codes),
                     "lv_font_montserrat_14", os.path.join(REL_OUT_DIR, symbol + ".c"))
    write_ui_fonts_home_h(codes, chevron_codes)
    print("wrote the home faces to %s" % os.path.relpath(OUT_DIR, ROOT))


def cjk_font_faces(geist):
    """The (symbol, ttf, range) of every CJK face: one per language, at the
    four Geist sizes, holding only what that language's strings need and Geist
    lacks."""
    faces = []
    for code, key, subset in CJK_LANGUAGES:
        rng = format_range(cjk_characters(code, geist))
        for size, weight, _icons in CJK_SIZES:
            ttf = os.path.join(CJK_DIR, "NotoSans%s-%s.otf" % (subset, weight))
            faces.append(("ui_font_cjk_%s_%d" % (key, size), ttf, rng))
    return faces


def generate_cjk_fonts(faces):
    print("CJK faces (%d bpp, uncompressed, one per language and size):" % CJK_BPP)
    for symbol, ttf, rng in faces:
        size = int(symbol.rsplit("_", 1)[1])
        icons = dict((s, i) for s, _w, i in CJK_SIZES)[size]
        out = os.path.join(REL_OUT_DIR, symbol + ".c")
        run_lv_font_conv(os.path.relpath(ttf, ROOT), size, symbol, rng, icons, out,
                         bpp=CJK_BPP)
        relax_line_metrics(out, size)


def declare_slot(out_path, symbol):
    """lv_font_conv writes `extern const lv_font_t <fallback>;` for the face it
    was given as --lv-fallback. The language slots are the one fallback in
    this build that is not const - src/ui/ui_fonts_lang.c copies a face into
    them - so the qualifier comes off, or C rejects the definition against the
    declaration."""
    with open(out_path, encoding="utf-8") as f:
        text = f.read()
    pattern = re.compile(r"^extern const lv_font_t %s;$" % re.escape(symbol), re.MULTILINE)
    if not pattern.search(text):
        sys.exit("error: lv_font_conv declared no %s in %s" % (symbol, out_path))
    replacement = ("/* The language slot this face falls back to: writable, unlike every\n"
                   " * other face in the build, because ui_fonts_lang_set() copies one\n"
                   " * into it when the language changes (ui/ui_fonts_lang.c). */\n"
                   "extern lv_font_t %s;" % symbol)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(pattern.sub(lambda _m: replacement, text, count=1))


def main_legacy():
    os.chdir(ROOT)
    os.makedirs(OUT_DIR, exist_ok=True)

    codepoints = icon_codepoints()
    icon_range = ",".join("0x%X" % codepoints[n] for n in ICON_NAMES)

    print("text fonts:")
    for symbol, ttf, size, fallback in TEXT_FONTS:
        run_lv_font_conv(os.path.relpath(ttf, ROOT), size, symbol, TEXT_RANGE, fallback,
                         os.path.join(REL_OUT_DIR, symbol + ".c"))

    print("icon fonts (%d glyphs):" % len(ICON_NAMES))
    for symbol, size, fallback in ICON_FONTS:
        run_lv_font_conv(os.path.relpath(MDI, ROOT), size, symbol, icon_range, fallback,
                         os.path.join(REL_OUT_DIR, symbol + ".c"))

    write_ui_fonts_h()
    write_ui_icons(codepoints)
    print("wrote %s and %s" % (os.path.relpath(OUT_DIR, ROOT), os.path.relpath(ICONS_H, ROOT)))


if __name__ == "__main__":
    if "--legacy" in sys.argv[1:]:
        main_legacy()
    else:
        main_home()
