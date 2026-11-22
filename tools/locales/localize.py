"""Stages a non-English build: the game's sources with the language's patches, text and graphics.

    python tools/locales/localize.py --locale es --rom <the player's ROM> --stage build-vita-es/stage

The build of a translated game never touches the repository's sources (they stay English). It
compiles a copy of them, the staging tree, made by this script for the languages
tools/locales/languages.json lists with "source": "rom":
  1. copies the repository into the staging tree (only files that changed since the last run);
  2. applies the language's layout and word-order patches (tools/locales/<code>/patches/);
  3. checks the player's ROM (its SHA-1, from languages.json) and replaces every text span the
     language's manifest (tools/locales/<code>/manifest.json.gz) lists with the ROM's own bytes,
     refusing a source file that changed since the manifest was made (re-run gen_manifest.py);
  4. writes the ROM's text graphics and the Easy Chat ID lists in the ROM's alphabetical order
     when the manifest lists them, and regenerates the native UI's town signs from the staged
     map texts (tools/ui2/gen_town_signs.py --root).
Files are only rewritten when their content changes, so a second build recompiles nothing new.
Nothing from the ROM is written outside the staging tree. Adapted from pokeemerald-vita-3ds's
stager, itself based on Emerald 3DS's tools/localize_*.py (CC BY-NC-SA 4.0, NOTICE.md); the
Emerald-only steps (credits pages, Trainer Hill floors) do not exist in FireRed.
"""
import argparse
import gzip
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
LANGUAGES = os.path.join(HERE, "languages.json")
# Copied into the staging tree: everything the build reads, nothing it writes or ignores
SKIP_TOP = {".git", ".trail", ".claude", "logs", "releases", "voxelgen", "rom esp", "node_modules", "graft",
            "Live Area Images", "imagenes para live area de referencia"}
SKIP_PREFIX = ("build", "cmake-build")
# Easy Chat's move and species groups: bare ID lists in the alphabetical order of their names
ID_LISTS = (
    ("gEasyChatGroup_Move1", "src/data/easy_chat/easy_chat_group_move_1.h", "include/constants/moves.h"),
    ("gEasyChatGroup_Move2", "src/data/easy_chat/easy_chat_group_move_2.h", "include/constants/moves.h"),
    ("gEasyChatGroup_Pokemon", "src/data/easy_chat/easy_chat_group_pokemon.h", "include/constants/species.h"),
    ("gEasyChatGroup_Pokemon2", "src/data/easy_chat/easy_chat_group_pokemon2.h", "include/constants/species.h"),
)


def apply_patches(tree, patches, only=None):
    """git apply each patch inside `tree`, outside any repository; `only`: files to touch"""
    env = dict(os.environ, GIT_CEILING_DIRECTORIES=os.path.dirname(os.path.abspath(tree)))
    include = [f"--include={p}" for p in only] if only else []
    for patch in patches:
        if only and not any(f"+++ b/{p}" in open(patch, encoding="utf-8").read() for p in only):
            continue
        # Fed with LF line endings: a Windows checkout may hold the patch with CRLF, and the
        # sources it applies to are LF (as git stores them)
        body = open(patch, encoding="utf-8").read().encode("utf-8")
        r = subprocess.run(["git", "apply", "--whitespace=nowarn", *include, "-"], input=body,
                           cwd=tree, env=env, capture_output=True)
        r.stderr = r.stderr.decode("utf-8", "replace")
        if r.returncode != 0:
            sys.exit(f"{patch} does not apply: {r.stderr.strip()}")


def patched_files(patches):
    files = set()
    for patch in patches:
        files |= set(re.findall(r"^\+\+\+ b/(\S+)", open(patch, encoding="utf-8").read(), re.M))
    return files


def lz77_decompress(data, offset):
    """GBA BIOS LZ77 (type 0x10) data at `offset`"""
    if data[offset] != 0x10:
        raise ValueError("not LZ77 data")
    size = data[offset + 1] | (data[offset + 2] << 8) | (data[offset + 3] << 16)
    out, i = bytearray(), offset + 4
    while len(out) < size:
        flags = data[i]
        i += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                b0, b1 = data[i], data[i + 1]
                i += 2
                length, disp = (b0 >> 4) + 3, (((b0 & 0xF) << 8) | b1) + 1
                for _ in range(length):
                    out.append(out[-disp])
            else:
                out.append(data[i])
                i += 1
    return bytes(out[:size])


def encoded(data):
    return ",".join("0x%02X" % b for b in data)


def replacement(kind, data, size):
    if kind == "c":
        return "{" + encoded(data) + "}"
    if kind == "asm":
        return "\n\t.byte " + encoded(data) + "\n"
    if kind == "u32":
        return str(struct.unpack("<I", data)[0])
    if kind == "u16s":
        return ",".join("0x%04X" % n for n in struct.unpack("<6H", data))
    if kind == "u16array":
        return ",".join("0x%04X" % n for n in struct.unpack("<%dH" % (size // 2), data))
    if kind == "u8rows22":
        if size != 18 * 22:
            raise ValueError("type name table dimensions")
        return "{" + ",".join("{" + encoded(data[i:i + 22]) + "}" for i in range(0, size, 22)) + "}"
    if kind == "u16rows10":
        rows = [",".join("0x%04X" % n for n in struct.unpack_from("<10H", data, i)) for i in range(0, size, 20)]
        return "{" + ",".join("{" + r + "}" for r in rows) + "}"
    if kind == "bard":
        phonemes = []
        for i in range(0, size, 8):
            if data[i + 6:i + 8] != b"\0\0":
                raise ValueError("bard record padding")
            phonemes.append("{" + ",".join(str(n) for n in struct.unpack_from("<BbHh", data, i)) + "}")
        return "{" + ",".join("{" + ",".join(phonemes[i:i + 6]) + "}" for i in range(0, len(phonemes), 6)) + "}"
    raise ValueError("unknown edit kind " + kind)


def id_list(text, constants, rom, offset, size, name):
    """An Easy Chat ID list in the ROM's order (the same IDs, sorted by the language's names)"""
    names = re.findall(r"^[ \t]*((?:MOVE|SPECIES)_\w+),[ \t]*$", text, re.M)
    defines = dict(re.findall(r"^#define\s+((?:MOVE|SPECIES)_\w+)\s+(\d+)\b", constants, re.M))
    by_id = {int(defines[n]): n for n in names}
    if size != 2 * len(names) or not 0 <= offset <= len(rom) - size:
        raise ValueError(name + ": the ROM's list does not fit the source list")
    ids = list(struct.unpack_from("<%dH" % len(names), rom, offset))
    if Counter(ids) != Counter(int(defines[n]) for n in names):
        raise ValueError(name + ": the ROM's list holds other IDs")
    order = iter(by_id[i] for i in ids)
    return re.sub(r"^([ \t]*)(?:MOVE|SPECIES)_\w+,[ \t]*$", lambda m: m.group(1) + next(order) + ",", text, flags=re.M)


def sync(stage, owned):
    """Copies the repository into the staging tree, file by file when size or time differ"""
    copied = 0
    for top in os.listdir(ROOT):
        if top in SKIP_TOP or top.startswith(SKIP_PREFIX):
            continue
        src = os.path.join(ROOT, top)
        for dirpath, dirs, names in os.walk(src) if os.path.isdir(src) else [(ROOT, [], [top])]:
            dirs[:] = [d for d in dirs if d not in ("__pycache__", ".git")]
            for name in names:
                s = os.path.join(dirpath, name)
                rel = os.path.relpath(s, ROOT).replace("\\", "/")
                if rel in owned:
                    continue
                d = os.path.join(stage, rel)
                st = os.stat(s)
                if os.path.exists(d):
                    dt = os.stat(d)
                    if dt.st_size == st.st_size and int(dt.st_mtime) == int(st.st_mtime):
                        continue
                os.makedirs(os.path.dirname(d), exist_ok=True)
                shutil.copy2(s, d)
                copied += 1
    return copied


def write_if_changed(stage, rel, data):
    path = os.path.join(stage, rel)
    if isinstance(data, str):
        data = data.encode("utf-8")
    if os.path.exists(path) and open(path, "rb").read() == data:
        return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(data)
    return True


def stage(locale, rom_path, stage_dir):
    lang = next((l for l in json.load(open(LANGUAGES, encoding="utf-8"))["languages"] if l["code"] == locale), None)
    if lang is None or lang.get("source") != "rom":
        sys.exit(f"'{locale}' is not a language made from a ROM in {LANGUAGES}")
    rom = open(rom_path, "rb").read()
    if hashlib.sha1(rom).hexdigest() != lang["romSha1"]:
        sys.exit(f"{rom_path} is not the clean {lang['name']} ROM this language is made from (SHA-1 {lang['romSha1']})")
    manifest = json.loads(gzip.decompress(open(os.path.join(HERE, locale, "manifest.json.gz"), "rb").read()))
    patch_dir = os.path.join(HERE, locale, "patches")
    patches = sorted(os.path.join(patch_dir, p) for p in os.listdir(patch_dir)) if os.path.isdir(patch_dir) else []

    texts = set(manifest["files"]) | patched_files(patches)
    texts |= {rel for _, rel, _, _, _ in manifest.get("idLists", [])}
    graphics = {}
    for rel, offset, size in manifest.get("graphics", []):
        # Padded to 4 bytes as gbagfx pads its .lz output: INCBIN_U32 reads them as words
        graphics[rel] = rom[offset:offset + size] + bytes(-size % 4)
        if rel.endswith(".lz"):
            graphics[rel[:-3]] = lz77_decompress(rom, offset)
    # Made from the staged scripts after the text is in place (tools/ui2/gen_town_signs.py)
    town_signs = "src/platform/ui/ui_town_signs.h"
    owned = texts | set(graphics) | {town_signs}
    os.makedirs(stage_dir, exist_ok=True)
    copied = sync(stage_dir, owned)

    # The patched English sources, built in a scratch folder next to the staging tree
    base_dir = os.path.join(os.path.dirname(os.path.abspath(stage_dir)), "locale-base")
    shutil.rmtree(base_dir, ignore_errors=True)
    for rel in texts:
        if os.path.exists(os.path.join(ROOT, rel)):
            # Line endings as git stores them (LF): the patches and the manifest's spans count on it
            os.makedirs(os.path.dirname(os.path.join(base_dir, rel)), exist_ok=True)
            with open(os.path.join(base_dir, rel), "w", encoding="utf-8", newline="\n") as fh:
                fh.write(open(os.path.join(ROOT, rel), encoding="utf-8").read())
    apply_patches(base_dir, patches)
    out = {rel: open(os.path.join(base_dir, rel), encoding="utf-8").read()
           for rel in texts if os.path.exists(os.path.join(base_dir, rel))}
    shutil.rmtree(base_dir, ignore_errors=True)

    stale = [rel for rel, f in manifest["files"].items()
             if hashlib.sha256(out[rel].encode("utf-8")).hexdigest() != f["sha256"]]
    if stale:
        sys.exit("these sources changed since the text manifest was made; re-run "
                 f"python tools/locales/gen_manifest.py --locale {locale} (docs/guides/ADDING_A_LANGUAGE.md):\n  "
                 + "\n  ".join(sorted(stale)))
    for rel, f in manifest["files"].items():
        text, last = out[rel], len(out[rel])
        for start, end, offset, size, kind in sorted(f["edits"], reverse=True):
            if not 0 <= start < end <= last or not 0 <= offset < offset + size <= len(rom):
                sys.exit(f"{rel}: edit out of range")
            text = text[:start] + replacement(kind, rom[offset:offset + size], size) + text[end:]
            last = start
        out[rel] = text
    for name, rel, constants, offset, size in manifest.get("idLists", []):
        consts = open(os.path.join(ROOT, constants), encoding="utf-8").read()
        out[rel] = id_list(out[rel], consts, rom, offset, size, name)

    written = sum(write_if_changed(stage_dir, rel, data) for rel, data in out.items())
    written += sum(write_if_changed(stage_dir, rel, data) for rel, data in graphics.items())
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "ui2", "gen_town_signs.py"), "--root", stage_dir],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("town signs: " + r.stderr.strip())
    print(f"{stage_dir}: {lang['name']} staged ({copied} files copied, {written} localized files written, "
          f"{len(manifest.get('unmapped', []))} text spans left in English)")


def main():
    args = argparse.ArgumentParser(description="Stages a translated build's sources")
    args.add_argument("--locale", required=True)
    args.add_argument("--rom", required=True)
    args.add_argument("--stage", required=True)
    opts = args.parse_args()
    stage(opts.locale, opts.rom, opts.stage)


if __name__ == "__main__":
    main()
