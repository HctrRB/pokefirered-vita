"""Generates src/platform/ui/ui_town_signs.h from Kanto & Sevii town sign texts.

    python tools/ui2/gen_town_signs.py [--root <tree>]

--root (default: the repository) reads and writes another source tree: a translated build's
staging tree (tools/locales/localize.py), whose sign texts are the ROM's bytes as `.byte` lines;
those are decoded through charmap.txt to UTF-8.
"""

import argparse
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT_PATH = os.path.join(ROOT, "src", "platform", "ui", "ui_town_signs.h")

TOWNS = [
    ("MAPSEC_PALLET_TOWN",     "data/maps/PalletTown/text.inc",     "PalletTown_Text_TownSign"),
    ("MAPSEC_VIRIDIAN_CITY",   "data/maps/ViridianCity/text.inc",   "ViridianCity_Text_CitySign"),
    ("MAPSEC_PEWTER_CITY",     "data/maps/PewterCity/text.inc",     "PewterCity_Text_CitySign"),
    ("MAPSEC_CERULEAN_CITY",   "data/maps/CeruleanCity/text.inc",   "CeruleanCity_Text_CitySign"),
    ("MAPSEC_LAVENDER_TOWN",   "data/maps/LavenderTown/text.inc",   "LavenderTown_Text_TownSign"),
    ("MAPSEC_VERMILION_CITY",  "data/maps/VermilionCity/text.inc",  "VermilionCity_Text_CitySign"),
    ("MAPSEC_CELADON_CITY",    "data/maps/CeladonCity/text.inc",    "CeladonCity_Text_CitySign"),
    ("MAPSEC_FUCHSIA_CITY",    "data/maps/FuchsiaCity/text.inc",    "FuchsiaCity_Text_CitySign"),
    ("MAPSEC_CINNABAR_ISLAND", "data/maps/CinnabarIsland/text.inc", "CinnabarIsland_Text_IslandSign"),
    ("MAPSEC_SAFFRON_CITY",    "data/maps/SaffronCity/text.inc",    "SaffronCity_Text_CitySign"),
    ("MAPSEC_ONE_ISLAND",      "data/maps/OneIsland/text.inc",      "OneIsland_Text_IslandSign"),
    ("MAPSEC_TWO_ISLAND",      "data/maps/TwoIsland/text.inc",      "TwoIsland_Text_IslandSign"),
    ("MAPSEC_THREE_ISLAND",    "data/maps/ThreeIsland/text.inc",    "ThreeIsland_Text_IslandSign"),
    ("MAPSEC_FOUR_ISLAND",     "data/maps/FourIsland/text.inc",     "FourIsland_Text_IslandSign"),
    ("MAPSEC_FIVE_ISLAND",     "data/maps/FiveIsland/text.inc",     "FiveIsland_Text_IslandSign"),
    ("MAPSEC_SIX_ISLAND",      "data/maps/SixIsland/text.inc",      "SixIsland_Text_IslandSign"),
    ("MAPSEC_SEVEN_ISLAND",    "data/maps/SevenIsland/text.inc",    "SevenIsland_Text_IslandSign"),
]


def load_decoder(root):
    """GBA byte -> its charmap text (the first character the charmap gives that byte)"""
    dec = {}
    for line in open(os.path.join(root, "charmap.txt"), encoding="utf-8"):
        line = line.split("@")[0].strip()
        m = re.match(r"^'(.+)'\s*=\s*([0-9A-Fa-f]{2})$", line)
        if m:
            dec.setdefault(int(m.group(2), 16), m.group(1))
    return dec


def bytes_to_source_text(data, dec):
    """ROM text bytes as the .string syntax decode_gba_text reads ("\\n", "{...}" for codes)"""
    out, i = [], 0
    while i < len(data):
        b = data[i]
        if b == 0xFF:
            break
        if b == 0xFE:
            out.append("\\n")
        elif b in (0xFA, 0xFB):
            out.append("\\p")
        elif b == 0xFC:
            i += 2                      # control code + one argument byte: dropped
            continue
        elif b == 0xFD:
            i += 2                      # placeholder ({PLAYER}, ...): dropped
            continue
        else:
            out.append(dec.get(b, ""))
        i += 1
    return "".join(out) + "$"


def decode_gba_text(raw):
    # Replace GBA line breaks and special glyphs with readable UTF-8.
    text = raw.replace("\\n", "\n").replace("\\l", " ").replace("\\p", " ")
    text = text.replace("{PKMN}", "Pokemon").replace("POKéMON", "Pokemon")
    text = text.replace("…", "...")
    text = re.sub(r"\{[^}]+\}", "", text)
    return text.rstrip("$")


def extract_motto(rel_path, label, root=ROOT, dec=None):
    full_path = os.path.join(root, rel_path)
    with open(full_path, "r", encoding="utf-8") as fh:
        content = fh.read()
    m = re.search(rf"^{re.escape(label)}::\s*(.*?)(?=^[A-Za-z0-9_]+::|\Z)",
                  content, flags=re.MULTILINE | re.DOTALL)
    if not m:
        raise ValueError(f"Missing label {label} in {rel_path}")
    strings = re.findall(r'\.string\s+"([^"]*)"', m.group(1))
    if not strings and dec is not None:
        raw = re.findall(r"\.byte\s+([0-9A-Fa-fx, ]+)", m.group(1))
        data = bytes(int(v, 16) for line in raw for v in line.replace(" ", "").split(",") if v)
        if data:
            strings = [bytes_to_source_text(data, dec)]
    if not strings:
        raise ValueError(f"No .string entries for {label} in {rel_path}")
    joined = decode_gba_text("".join(strings))
    lines = [line.strip() for line in joined.split("\n") if line.strip()]
    if len(lines) < 2:
        raise ValueError(f"Expected title + motto for {label}, got {lines!r}")
    motto = re.sub(r"\s+", " ", " ".join(lines[1:])).strip()
    if not motto:
        raise ValueError(f"Empty motto extracted for {label}")
    return motto


def c_escape(text):
    return text.replace("\\", "\\\\").replace('"', '\\"')


def main():
    ap = argparse.ArgumentParser(description="Generates the native UI's town sign mottos")
    ap.add_argument("--root", default=ROOT, help="source tree to read and write (default: the repository)")
    root = os.path.abspath(ap.parse_args().root)
    out_path = os.path.join(root, "src", "platform", "ui", "ui_town_signs.h")
    dec = load_decoder(root)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    rows = []
    for mapsec, rel_path, label in TOWNS:
        motto = extract_motto(rel_path, label, root, dec)
        rows.append((mapsec, motto))

    lines = [
        "// Generated by tools/ui2/gen_town_signs.py from data/maps/*/text.inc.",
        "// Do not edit; re-run the generator if town sign strings change.",
        "",
        "#ifndef UI_TOWN_SIGNS_H",
        "#define UI_TOWN_SIGNS_H",
        "",
        "#include <stdint.h>",
        "#include \"constants/region_map_sections.h\"",
        "",
        "typedef struct",
        "{",
        "    uint16_t mapSec;",
        "    const char *motto;",
        "} UiTownSign;",
        "",
        "static const UiTownSign kUiTownSigns[] = {",
    ]
    for mapsec, motto in rows:
        lines.append(f'    {{ {mapsec}, "{c_escape(motto)}" }},')
    lines.extend([
        "};",
        "",
        "static inline const char *Ui_TownMotto(uint16_t mapSec)",
        "{",
        "    for (uint32_t i = 0; i < (uint32_t)(sizeof(kUiTownSigns) / sizeof(kUiTownSigns[0])); i++)",
        "    {",
        "        if (kUiTownSigns[i].mapSec == mapSec)",
        "            return kUiTownSigns[i].motto;",
        "    }",
        "    return NULL;",
        "}",
        "",
        "#endif // UI_TOWN_SIGNS_H",
        "",
    ])

    text = "\n".join(lines)
    if not os.path.exists(out_path) or open(out_path, encoding="utf-8", newline="").read() != text:
        with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
    print(f"Wrote {out_path} with {len(rows)} town/city/island mottos")


if __name__ == "__main__":
    main()
