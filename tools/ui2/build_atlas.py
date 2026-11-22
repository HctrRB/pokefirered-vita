"""Builds assets/ui2/ui_atlas.bin (format v3) and src/platform/ui/ui_sprite_ids.h.

Combines pre-baked UI & theme bitmap/antialiased font sheets (ui_fonts.*, theme_fonts.*)
with authentic FireRed sprites sliced from graphics/ and src/platform/fr3ds/fr_ui_icons.h,
tightly shelf-packed into a 1024xH 16-bit atlas texture (H <= 1280 so >= 128 64x64 runtime
sprite cells remain in the 1024x2048 texture).
"""

import argparse
import array
import json
import os
import re
import struct

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
FONTS_DIR = os.path.join(HERE, "fonts")
OUT_BIN = os.path.join(ROOT, "assets", "ui2", "ui_atlas.bin")
OUT_HDR = os.path.join(ROOT, "src", "platform", "ui", "ui_sprite_ids.h")

MAGIC = 0x33414955  # "UIA3" little-endian
VERSION = 3
TEX_W = 1024
MAX_FIXED_H = 1280
MAX_SPRITES = 256
MAX_FONTS = 40

TYPE_ICON_ENTRIES = [
    ("UI_SPR_TYPE_NORMAL",   0x20),
    ("UI_SPR_TYPE_FIGHTING", 0x64),
    ("UI_SPR_TYPE_FLYING",   0x60),
    ("UI_SPR_TYPE_POISON",   0x80),
    ("UI_SPR_TYPE_GROUND",   0x48),
    ("UI_SPR_TYPE_ROCK",     0x44),
    ("UI_SPR_TYPE_BUG",      0x6C),
    ("UI_SPR_TYPE_GHOST",    0x68),
    ("UI_SPR_TYPE_STEEL",    0x88),
    ("UI_SPR_TYPE_MYSTERY",  0xA4),
    ("UI_SPR_TYPE_FIRE",     0x24),
    ("UI_SPR_TYPE_WATER",    0x28),
    ("UI_SPR_TYPE_GRASS",    0x2C),
    ("UI_SPR_TYPE_ELECTRIC", 0x40),
    ("UI_SPR_TYPE_PSYCHIC",  0x84),
    ("UI_SPR_TYPE_ICE",      0x4C),
    ("UI_SPR_TYPE_DRAGON",   0xA0),
    ("UI_SPR_TYPE_DARK",     0x8C),
]

BALL_NAMES = [
    ("UI_SPR_BALL_POKE",    "poke_ball.png"),
    ("UI_SPR_BALL_GREAT",   "great_ball.png"),
    ("UI_SPR_BALL_ULTRA",   "ultra_ball.png"),
    ("UI_SPR_BALL_MASTER",  "master_ball.png"),
    ("UI_SPR_BALL_SAFARI",  "safari_ball.png"),
    ("UI_SPR_BALL_NET",     "net_ball.png"),
    ("UI_SPR_BALL_DIVE",    "dive_ball.png"),
    ("UI_SPR_BALL_NEST",    "nest_ball.png"),
    ("UI_SPR_BALL_REPEAT",  "repeat_ball.png"),
    ("UI_SPR_BALL_TIMER",   "timer_ball.png"),
    ("UI_SPR_BALL_LUXURY",  "luxury_ball.png"),
    ("UI_SPR_BALL_PREMIER", "premier_ball.png"),
]


def pack_rgba_to_texel(r, g, b, a):
    if a < 128:
        return 0
    return ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1


def load_jasc_pal(rel_path):
    with open(os.path.join(ROOT, rel_path), "r", encoding="utf-8") as fh:
        lines = [line.strip() for line in fh.read().splitlines() if line.strip()]
    return [tuple(int(v) for v in line.split()) for line in lines[3:]]


def extract_fr3ds_icon(header_text, base_name):
    m_rgb = re.search(rf"static\s+const\s+u16\s+{base_name}_rgb\[\]\s*=\s*\{{(.*?)\}};", header_text, re.DOTALL)
    m_a = re.search(rf"static\s+const\s+u8\s+{base_name}_a\[\]\s*=\s*\{{(.*?)\}};", header_text, re.DOTALL)
    if not m_rgb or not m_a:
        raise ValueError(f"Could not find {base_name}_rgb/a in fr_ui_icons.h")
    rgbs = [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", m_rgb.group(1))]
    alphas = [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", m_a.group(1))]
    size = int(len(rgbs) ** 0.5)
    if size * size != len(rgbs) or len(rgbs) != len(alphas):
        raise ValueError(f"Unexpected icon dimensions for {base_name}: {len(rgbs)}/{len(alphas)}")
    texels = []
    for rgb565, alpha in zip(rgbs, alphas):
        if alpha < 96:
            texels.append(0)
            continue
        # fr_ui_icons RGB565 blends over dark or transparent icon; keep crisp quantized RGB5551.
        r5 = (rgb565 >> 11) & 0x1F
        g5 = (rgb565 >> 6) & 0x1F
        b5 = rgb565 & 0x1F
        texels.append((r5 << 11) | (g5 << 6) | (b5 << 1) | 1)
    return size, size, texels


def slice_indexed_png(rel_path, x0, y0, w, h, pal_override=None, transparent_idx=0):
    im = Image.open(os.path.join(ROOT, rel_path))
    if im.mode != "P":
        rgba = im.convert("RGBA").crop((x0, y0, x0 + w, y0 + h))
        return w, h, [pack_rgba_to_texel(r, g, b, a) for r, g, b, a in rgba.getdata()]
    raw_pal = im.getpalette()
    texels = []
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            idx = im.getpixel((x, y))
            if idx == transparent_idx:
                texels.append(0)
            elif pal_override is not None:
                r, g, b = pal_override[idx % len(pal_override)]
                texels.append(pack_rgba_to_texel(r, g, b, 255))
            else:
                r, g, b = raw_pal[idx * 3], raw_pal[idx * 3 + 1], raw_pal[idx * 3 + 2]
                texels.append(pack_rgba_to_texel(r, g, b, 255))
    return w, h, texels


def render_region_map(bin_rel_path):
    tiles_raw = open(os.path.join(ROOT, "graphics/region_map/region_map.4bpp"), "rb").read()
    pal = load_jasc_pal("graphics/region_map/region_map.pal")
    entries = struct.unpack("<600H", open(os.path.join(ROOT, bin_rel_path), "rb").read())
    # Bounds of non-empty tiles verified across kanto/sevii_123/sevii_45/sevii_67: x=2..27, y=2..19
    tx0, tx1 = 2, 28
    ty0, ty1 = 2, 20
    w = (tx1 - tx0) * 8
    h = (ty1 - ty0) * 8
    texels = [0] * (w * h)
    for ty in range(ty0, ty1):
        for tx in range(tx0, tx1):
            entry = entries[ty * 30 + tx]
            tile_idx = entry & 0x3FF
            hflip = (entry >> 10) & 1
            vflip = (entry >> 11) & 1
            pal_bank = (entry >> 12) & 0xF
            tile_off = tile_idx * 32
            if tile_off + 32 > len(tiles_raw):
                continue
            for py in range(8):
                sy = (7 - py) if vflip else py
                for px in range(8):
                    sx = (7 - px) if hflip else px
                    byte = tiles_raw[tile_off + sy * 4 + (sx // 2)]
                    cidx = (byte >> 4) if (sx & 1) else (byte & 0x0F)
                    if cidx == 0:
                        continue
                    r, g, b = pal[pal_bank * 16 + cidx]
                    ox = (tx - tx0) * 8 + px
                    oy = (ty - ty0) * 8 + py
                    texels[oy * w + ox] = pack_rgba_to_texel(r, g, b, 255)
    return w, h, texels


def bitmap_sprite(rows, fg_rgb, edge_rgb=None):
    h = len(rows)
    w = len(rows[0])
    texels = []
    for row in rows:
        for ch in row:
            if ch == "#":
                texels.append(pack_rgba_to_texel(*fg_rgb, 255))
            elif ch == "." and edge_rgb is not None:
                texels.append(pack_rgba_to_texel(*edge_rgb, 255))
            else:
                texels.append(0)
    return w, h, texels


def build_sprites():
    fr_icons = open(os.path.join(ROOT, "src/platform/fr3ds/fr_ui_icons.h"), "r", encoding="utf-8").read()
    sprites = []

    def add(sym, w, h, texels):
        if sum(1 for v in texels if v != 0) == 0:
            raise AssertionError(f"Sprite {sym} has zero opaque pixels")
        sprites.append((sym, w, h, texels))

    # 1. Poke Balls (centered 16x16 crop from 24x24 GBA icons)
    for sym, fname in BALL_NAMES:
        w, h, texels = slice_indexed_png(f"graphics/items/icons/{fname}", 4, 4, 16, 16)
        add(sym, w, h, texels)

    # 2. FireRed Hub & Battle Split Category Icons
    for sym, c_name in [
        ("UI_SPR_START_DEX",   "sIconDex26"),
        ("UI_SPR_START_PARTY", "sIconBall26"),
        ("UI_SPR_START_BAG",   "sIconBag28"),
        ("UI_SPR_START_CARD",  "sIconCard26"),
        ("UI_SPR_START_SAVE",  "sIconSave26"),
        ("UI_SPR_START_GEAR",  "sIconGear26"),
        ("UI_SPR_MEGA_KEY",    "sIconKey22"),
        ("UI_SPR_CAT_PHYSICAL", "sIconPhys14"),
        ("UI_SPR_CAT_SPECIAL",  "sIconSpec14"),
        ("UI_SPR_CAT_STATUS",   "sIconStat14"),
    ]:
        w, h, texels = extract_fr3ds_icon(fr_icons, c_name)
        add(sym, w, h, texels)

    w, h, texels = slice_indexed_png("graphics/items/icons/town_map.png", 0, 0, 24, 24)
    add("UI_SPR_START_MAP", w, h, texels)

    # 3. Male & Female Bag sprites (frames 0=closed, 1=Items, 2=Key Items, 3=Poke Balls)
    for prefix, png in [("UI_SPR_BAG_MALE", "graphics/interface/bag_male.png"),
                        ("UI_SPR_BAG_FEMALE", "graphics/interface/bag_female.png")]:
        for f_idx in range(4):
            w, h, texels = slice_indexed_png(png, 0, f_idx * 64, 64, 64)
            add(f"{prefix}_{f_idx}", w, h, texels)

    # 4. Crisp GBA gender symbols and shiny star
    male_rows = [
        "....####.",
        "...#####.",
        ".....###.",
        ".######..",
        "##...##..",
        "#....#...",
        "#....#...",
        "##..##...",
        ".####....",
        ".........",
    ]
    female_rows = [
        "..####...",
        ".##..##..",
        "##....##.",
        "##....##.",
        ".##..##..",
        "..####...",
        "...##....",
        ".######..",
        "...##....",
        ".........",
    ]
    star_rows = [
        "....#....",
        "....#....",
        "...###...",
        "#########",
        ".#######.",
        "..#####..",
        "..##.##..",
        ".##...##.",
        ".........",
    ]
    add("UI_SPR_GENDER_MALE", *bitmap_sprite(male_rows, (64, 152, 248)))
    add("UI_SPR_GENDER_FEMALE", *bitmap_sprite(female_rows, (240, 88, 128)))
    add("UI_SPR_SHINY_STAR", *bitmap_sprite(star_rows, (255, 214, 48)))

    # 5. Kanto & Sevii Town Maps
    for sym, bin_path in [
        ("UI_SPR_MAP_KANTO",     "graphics/region_map/kanto.bin"),
        ("UI_SPR_MAP_SEVII_123", "graphics/region_map/sevii_123.bin"),
        ("UI_SPR_MAP_SEVII_45",  "graphics/region_map/sevii_45.bin"),
        ("UI_SPR_MAP_SEVII_67",  "graphics/region_map/sevii_67.bin"),
    ]:
        w, h, texels = render_region_map(bin_path)
        add(sym, w, h, texels)

    # 6. Player Map Heads
    add("UI_SPR_HEAD_RED", *slice_indexed_png("graphics/region_map/player_icon_red.png", 0, 0, 16, 16))
    add("UI_SPR_HEAD_LEAF", *slice_indexed_png("graphics/region_map/player_icon_leaf.png", 0, 0, 16, 16))

    # 7. 8 Kanto Gym Badges
    for i in range(8):
        add(f"UI_SPR_BADGE_{i + 1}", *slice_indexed_png("graphics/trainer_card/badges.png", i * 16, 0, 16, 16))

    # 8. 7 Status Ailment Icons
    for idx, name in enumerate(["PSN", "PAR", "SLP", "FRZ", "BRN", "PKRS", "FNT"]):
        add(f"UI_SPR_STATUS_{name}", *slice_indexed_png("graphics/summary_screen/status_ailment_icons.png", 0, idx * 8, 32, 8))

    # 9. Hold Item / Mail Icons
    add("UI_SPR_HOLD_ITEM", *slice_indexed_png("graphics/party_menu/hold_icons.png", 0, 0, 8, 8))
    add("UI_SPR_HOLD_MAIL", *slice_indexed_png("graphics/party_menu/hold_icons.png", 0, 8, 8, 8))

    # 10. All 18 Pokemon Type Pills (32x12)
    for sym, off in TYPE_ICON_ENTRIES:
        tx = (off % 16) * 8
        ty = (off // 16) * 8
        add(sym, *slice_indexed_png("graphics/interface/menu_info.png", tx, ty, 32, 12))

    # 11. Battle Party Summary / Caught Pokeball Icons (8x8 tiles 66..70)
    healthbar_pal = load_jasc_pal("graphics/battle_interface/healthbar.pal")
    for sym, tile_idx in [
        ("UI_SPR_BATTLE_BALL_OK",      66),
        ("UI_SPR_BATTLE_BALL_EMPTY",   67),
        ("UI_SPR_BATTLE_BALL_STATUS",  68),
        ("UI_SPR_BATTLE_BALL_FAINTED", 69),
        ("UI_SPR_BATTLE_CAUGHT",       70),
    ]:
        tx = (tile_idx % 40) * 8
        ty = (tile_idx // 40) * 8
        add(sym, *slice_indexed_png("graphics/battle_interface/healthbox_elements.png", tx, ty, 8, 8, pal_override=healthbar_pal))

    return sprites


def extract_glyph_texels(sheet_img, x, y, w, h):
    if w == 0 or h == 0:
        return []
    texels = []
    for j in range(h):
        for i in range(w):
            _, _, _, a = sheet_img.getpixel((x + i, y + j))
            texels.append((a << 1) if a > 0 else 0)
    return texels


def build_fonts(locale_letters):
    fonts = []
    sheet_cache = {}

    def get_img(path):
        if path not in sheet_cache:
            sheet_cache[path] = Image.open(path).convert("RGBA")
        return sheet_cache[path]

    locale_by_key = {}
    if locale_letters:
        lj = json.load(open(os.path.join(FONTS_DIR, "locale_glyphs.json"), "r", encoding="utf-8"))
        limg = get_img(os.path.join(FONTS_DIR, "locale_glyphs.png"))
        allowed = {ord(c) for c in locale_letters}
        for lf in lj["fonts"]:
            key = (lf["face"], lf["px"])
            items = []
            for g in lf["glyphs"]:
                cp = g[0]
                if cp in allowed:
                    items.append((cp, g[3], g[4], g[5], g[6], g[7], extract_glyph_texels(limg, g[1], g[2], g[3], g[4])))
            if items:
                locale_by_key[key] = items

    for name in ["ui_fonts", "theme_fonts"]:
        j = json.load(open(os.path.join(FONTS_DIR, f"{name}.json"), "r", encoding="utf-8"))
        img = get_img(os.path.join(FONTS_DIR, f"{name}.png"))
        for f in j["fonts"]:
            key = (f["face"], f["px"])
            glyphs_map = {}
            for g in f["glyphs"]:
                cp, gx, gy, gw, gh, xoff, yoff, adv = g
                glyphs_map[cp] = (cp, gw, gh, xoff, yoff, adv, extract_glyph_texels(img, gx, gy, gw, gh))
            for item in locale_by_key.get(key, []):
                glyphs_map.setdefault(item[0], item)
            glyphs_sorted = [glyphs_map[k] for k in sorted(glyphs_map)]
            fonts.append({
                "face": f["face"],
                "px": f["px"],
                "ascent": f["ascent"],
                "lineH": f["lineH"],
                "glyphs": glyphs_sorted,
            })

    fonts.sort(key=lambda f: (f["face"], f["px"]))
    return fonts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--locale", default="", help="Optional locale code or letter set to include")
    args = parser.parse_args()

    sprites = build_sprites()
    fonts = build_fonts(args.locale)

    if len(sprites) > MAX_SPRITES:
        raise SystemExit(f"Too many sprites: {len(sprites)} > {MAX_SPRITES}")
    if len(fonts) > MAX_FONTS:
        raise SystemExit(f"Too many fonts: {len(fonts)} > {MAX_FONTS}")

    # Collect packable rectangles sorted tallest-first to minimize shelf waste.
    items = [("white", None, 4, 4, [0xFFFF] * 16)]
    for idx, (sym, w, h, texels) in enumerate(sprites):
        items.append(("spr", idx, w, h, texels))
    for f_idx, f in enumerate(fonts):
        for g_idx, (cp, w, h, xoff, yoff, adv, texels) in enumerate(f["glyphs"]):
            if w > 0 and h > 0:
                items.append(("glyph", (f_idx, g_idx), w, h, texels))

    items.sort(key=lambda it: (-it[3], -it[2]))

    cx, cy, shelf_h = 0, 0, 0
    placements = {}
    for kind, ref, w, h, texels in items:
        if cx + w + 1 > TEX_W:
            cx = 0
            cy += shelf_h + 1
            shelf_h = 0
        placements[(kind, ref)] = (cx, cy, w, h, texels)
        cx += w + 1
        if h > shelf_h:
            shelf_h = h

    used_h = cy + shelf_h + 1
    tex_h = ((used_h + 63) // 64) * 64
    if tex_h > MAX_FIXED_H:
        raise SystemExit(f"Atlas too tall: tex_h={tex_h} > MAX_FIXED_H={MAX_FIXED_H} (used={used_h})")

    buf = array.array("H", [0] * (TEX_W * tex_h))
    for (kind, ref), (px, py, w, h, texels) in placements.items():
        for row in range(h):
            dst = (py + row) * TEX_W + px
            src = row * w
            buf[dst:dst + w] = array.array("H", texels[src:src + w])

    white_x, white_y, _, _, _ = placements[("white", None)]

    sprite_records = []
    for idx in range(len(sprites)):
        sx, sy, sw, sh, _ = placements[("spr", idx)]
        sprite_records.append((sx, sy, sw, sh))

    font_records = []
    glyph_records = []
    for f_idx, f in enumerate(fonts):
        first_glyph = len(glyph_records)
        for g_idx, (cp, gw, gh, xoff, yoff, adv, _) in enumerate(f["glyphs"]):
            if gw > 0 and gh > 0:
                gx, gy, _, _, _ = placements[("glyph", (f_idx, g_idx))]
            else:
                gx, gy = 0, 0
            glyph_records.append((cp, gx, gy, gw, gh, xoff, yoff, adv))
        font_records.append((f["face"], f["px"], f["ascent"], f["lineH"], len(f["glyphs"]), first_glyph))

    os.makedirs(os.path.dirname(OUT_BIN), exist_ok=True)
    with open(OUT_BIN, "wb") as fh:
        fh.write(struct.pack("<8I", MAGIC, VERSION, TEX_W, tex_h,
                             len(font_records), len(sprite_records), white_x, white_y))
        for face, px, ascent, line_h, gcount, first_g in font_records:
            fh.write(struct.pack("<4BHHII", face, px, ascent, line_h, gcount, 0, first_g, 0))
        for cp, gx, gy, gw, gh, xoff, yoff, adv in glyph_records:
            fh.write(struct.pack("<IHHBBbbBBBB", cp, gx, gy, gw, gh, xoff, yoff, adv, 0, 0, 0))
        for sx, sy, sw, sh in sprite_records:
            fh.write(struct.pack("<4H", sx, sy, sw, sh))
        buf.tofile(fh)

    hdr_lines = [
        "// Generated by tools/ui2/build_atlas.py: ids of fixed FireRed sprites in assets/ui2/ui_atlas.bin.",
        "// Do not edit; re-run tools/ui2/build_atlas.py after changing its sprite list.",
        "",
        "#ifndef UI_SPRITE_IDS_H",
        "#define UI_SPRITE_IDS_H",
        "",
        "enum UiSpriteId",
        "{",
    ]
    for idx, (sym, _, _, _) in enumerate(sprites):
        hdr_lines.append(f"    {sym} = {idx},")
    hdr_lines.extend([
        f"    UI_SPR_COUNT = {len(sprites)}",
        "};",
        "",
        "// Run base ids indexed by FireRed game constants",
        "#define UI_SPR_BALL UI_SPR_BALL_POKE",
        "#define UI_SPR_STATUS_FIRST UI_SPR_STATUS_PSN",
        "#define UI_SPR_TYPE_FIRST UI_SPR_TYPE_NORMAL",
        "#define UI_SPR_BADGE_FIRST UI_SPR_BADGE_1",
        "#define UI_SPR_BAG_MALE_FIRST UI_SPR_BAG_MALE_0",
        "#define UI_SPR_BAG_FEMALE_FIRST UI_SPR_BAG_FEMALE_0",
        "#define UI_SPR_CAT_FIRST UI_SPR_CAT_PHYSICAL",
        "#define UI_SPR_BATTLE_BALL_FIRST UI_SPR_BATTLE_BALL_OK",
        "",
        "#endif // UI_SPRITE_IDS_H",
        "",
    ])
    os.makedirs(os.path.dirname(OUT_HDR), exist_ok=True)
    with open(OUT_HDR, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(hdr_lines))

    cells_left = ((2048 - tex_h) // 64) * 16
    bin_kb = os.path.getsize(OUT_BIN) / 1024.0
    print(f"Wrote {OUT_BIN} ({bin_kb:.1f} KB): {TEX_W}x{tex_h} (used {used_h}), "
          f"{len(font_records)} fonts ({len(glyph_records)} glyphs), {len(sprite_records)} sprites, "
          f"{cells_left} runtime 64x64 cells remaining")


if __name__ == "__main__":
    main()
