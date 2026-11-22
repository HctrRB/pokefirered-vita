#!/usr/bin/env python3
"""Writes tools/locales/<code>/manifest.json.gz: where each English game text of the sources is
found in an official ROM of another language. Development tool
(docs/guides/ADDING_A_LANGUAGE.md); the game build never runs it.

    python tools/locales/gen_manifest.py --locale es --rom <BPRS .gba> \\
        --elf <pret pokefirered_rev1.elf> --english-rom <pret pokefirered_rev1.gba>

--elf / --english-rom: pret/pokefirered at the commit pinned in upstream.lock, built with agbcc
(`make compare_firered_rev1`, so the ROM is byte-identical to the official BPRE 1.1) and linked
with --emit-relocs. tools/locales/match_rom.py places that build's symbols in the other ROM; this
script then reads, in this repository's sources (with the language's patches applied, as
localize.py stages them), every text span:
  - asm: the `.string` lines under one label (data/**/*.inc, data/*.s), kind "asm";
  - C:   every `_("...")` literal of src/ (not src/platform/), kind "c";
finds its English bytes in the English build (the label's symbol, or the literal inside the
object that defines it) and the same text in the other ROM. A span whose English bytes are not
where the sources say (text added by the port) or whose object could not be placed is listed in
"unmapped" by file and position only, and stays English.

The manifest holds positions, offsets and sizes only, never ROM bytes or text.
"""
import argparse
import bisect
import gzip
import hashlib
import json
import os
import pickle
import re
import shutil
import subprocess
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
LANGUAGES = os.path.join(HERE, "languages.json")
sys.path.insert(0, HERE)
import match_rom  # noqa: E402
from match_rom import ROM, u32  # noqa: E402

ENGLISH_SHA1 = "dd5945db9b930750cb39d00c84da8571feebf417"   # official BPRE 1.1
MAX_TEXT = 4096

ASM_LABEL = re.compile(r"^([A-Za-z_]\w*)::?[ \t]*(?:@.*)?$", re.M)
ASM_STRING = re.compile(r'^[ \t]*\.string[ \t]+"((?:[^"\\\n]|\\.)*)"[ \t]*(?:@.*)?$', re.M)
C_LITERAL = re.compile(r'\b_\(\s*((?:"(?:[^"\\\n]|\\.)*"\s*)+)\)')
C_PIECE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
# A definition starts a line or follows a ';' (some generated tables put several on one line)
C_DEF = re.compile(r'(?:^|(?<=;))[ \t]*(?:ALIGNED\(\d+\)\s+)?(?:static\s+)?(?:ALIGNED\(\d+\)\s+)?(?:const\s+)?(?:struct\s+)?'
                   r'\w+[\s\*]+(?:const\s+)?\**\s*(\w+)\s*(?:\[[^\]\n]*\]\s*)*=', re.M)


# ---------------------------------------------------------------- charmap & sources

def load_charmap(path):
    cm = {}
    for line in open(path, encoding="utf-8"):
        line = line.split("@")[0].strip()
        m = re.match(r"^'(.+)'\s*=\s*([0-9A-Fa-f ]+)$", line)
        if m:
            key = m.group(1)
            # charmap.txt spells the apostrophe '\'': the source text writes it bare
            cm.setdefault("'" if key == "\\'" else key, bytes.fromhex(m.group(2)))
            continue
        m = re.match(r"^([A-Z_0-9]+)\s*=\s*([0-9A-Fa-f ]+)$", line)
        if m:
            cm.setdefault("{" + m.group(1) + "}", bytes.fromhex(m.group(2)))
    return cm


def encode(text, cm):
    """Bytes of the contents of a "..." literal, or None for anything the charmap lacks"""
    out, i = bytearray(), 0
    while i < len(text):
        c = text[i]
        if c == "{":
            j = text.find("}", i)
            if j < 0:
                return None
            tok = text[i:j + 1]
            if tok in cm:
                out += cm[tok]
            else:
                inner = tok[1:-1].split()
                if not inner:
                    return None
                head = "{" + inner[0] + "}"
                rest = inner
                if head in cm:
                    out += cm[head]
                    rest = inner[1:]
                for x in rest:
                    if re.fullmatch(r"0x[0-9A-Fa-f]+", x):
                        out.append(int(x, 16) & 0xFF)
                    elif re.fullmatch(r"\d+", x):
                        out.append(int(x, 10) & 0xFF)
                    elif "{" + x + "}" in cm:
                        out += cm["{" + x + "}"]
                    else:
                        return None
            i = j + 1
            continue
        if c == "\\" and i + 1 < len(text):
            pair = text[i:i + 2]
            if pair in cm:
                out += cm[pair]
            elif pair == '\\"' and '"' in cm:
                out += cm['"']
            else:
                return None
            i += 2
            continue
        if c not in cm:
            return None
        out += cm[c]
        i += 1
    return bytes(out)


def strip_comments(text):
    """C comments blanked (same length, so positions keep), string literals kept"""
    out, i, n = list(text), 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out[i:j] = " " * (j - i)
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out[i:j] = [c if c == "\n" else " " for c in text[i:j]]
            i = j
        elif text[i] in "\"'":
            q, j = text[i], i + 1
            while j < n and text[j] != q and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
        else:
            i += 1
    return "".join(out)


def asm_spans(text, cm):
    """(start, end, label, english bytes) of each label's run of .string lines"""
    labels = [(m.start(), m.group(1)) for m in ASM_LABEL.finditer(text)]
    starts = [p for p, _ in labels]
    spans, run = [], []

    def flush():
        if not run:
            return
        i = bisect.bisect_right(starts, run[0][0]) - 1
        label = labels[i][1] if i >= 0 else None
        # only the first run right under a label is that label's text
        between = text[labels[i][0]:run[0][0]] if i >= 0 else ""
        first = i >= 0 and not ASM_STRING.search(between) and \
            all(l.strip() == "" or l.lstrip().startswith("@") or ASM_LABEL.match(l.strip())
                for l in between.splitlines()[1:])
        data = b"".join(encode(p, cm) or b"\x00\x00\x00INVALID" for _, _, p in run)
        if b"\x00\x00\x00INVALID" in data:
            data = None
        spans.append((run[0][0], run[-1][1], label if first else None, data))
        run.clear()
    prev_end = None
    for m in ASM_STRING.finditer(text):
        if run and text[prev_end:m.start()].strip() != "":
            flush()
        run.append((m.start(), m.end(), m.group(1)))
        prev_end = m.end()
    flush()
    return spans


def c_spans(text, cm):
    """(start, end, owner, english bytes) of each _("...") literal"""
    clean = strip_comments(text)
    owners = [(m.start(), m.group(1), definition_end(clean, m.end())) for m in C_DEF.finditer(clean)]
    ostarts = [p for p, _, _ in owners]
    out = []
    for m in C_LITERAL.finditer(clean):
        i = bisect.bisect_right(ostarts, m.start()) - 1
        # Only literals inside a definition's initializer can become a {0x..} byte list; one passed
        # to a function (or anywhere else) gets no owner and stays English.
        owner = owners[i][1] if i >= 0 and m.start() < owners[i][2] else None
        data = b""
        for p in C_PIECE.findall(m.group(1)):
            b = encode(p, cm)
            if b is None:
                data = None
                break
            data += b
        out.append((m.start(), m.end(), owner, None if data is None else data + b"\xff"))
    return out


def definition_end(clean, at):
    """Position of the ';' that ends the initializer starting at `at` (braces balanced)"""
    depth, i, n = 0, at, len(clean)
    while i < n:
        c = clean[i]
        if c == '"':
            i += 1
            while i < n and clean[i] != '"':
                i += 2 if clean[i] == "\\" else 1
        elif c in "{(":
            depth += 1
        elif c in "})":
            depth -= 1
        elif c == ";" and depth <= 0:
            return i
        i += 1
    return n


def text_files():
    files = []
    for top, exts in (("data", (".inc", ".s")), ("src", (".c", ".h"))):
        for dirpath, dirs, names in os.walk(os.path.join(ROOT, top)):
            rel_dir = os.path.relpath(dirpath, ROOT).replace("\\", "/")
            if rel_dir.startswith("src/platform"):
                continue
            files += [f"{rel_dir}/{n}" for n in names if n.endswith(exts)]
    return sorted(files)


def patched_sources(rels, patches, work):
    """{rel: LF text} of the sources with the language's patches applied (as localize.py does)"""
    shutil.rmtree(work, ignore_errors=True)
    for rel in rels:
        os.makedirs(os.path.dirname(os.path.join(work, rel)), exist_ok=True)
        with open(os.path.join(work, rel), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(open(os.path.join(ROOT, rel), encoding="utf-8").read())
    env = dict(os.environ, GIT_CEILING_DIRECTORIES=os.path.dirname(os.path.abspath(work)))
    for patch in patches:
        body = open(patch, encoding="utf-8").read().encode("utf-8")
        r = subprocess.run(["git", "apply", "--whitespace=nowarn", "-"], input=body, cwd=work, env=env,
                           capture_output=True)
        if r.returncode != 0:
            sys.exit(f"{patch} does not apply: {r.stderr.decode('utf-8', 'replace').strip()}")
    out = {rel: open(os.path.join(work, rel), encoding="utf-8").read() for rel in rels}
    shutil.rmtree(work, ignore_errors=True)
    return out


# ---------------------------------------------------------------- English -> other ROM

class Mapper:
    def __init__(self, m):
        self.m = m
        objs = sorted((s[0], s[1], s[3], s[2]) for s in m.syms if s[1])
        self.objs = objs
        self.starts = [o[0] for o in objs]
        self.text = {o[0] for o in m.text_objects()}
        self.byname = {}
        for s in m.syms:
            self.byname.setdefault(s[3], s)

    def containing(self, addr):
        i = bisect.bisect_right(self.starts, addr) - 1
        best = None
        while i >= 0 and addr - self.starts[i] < 0x20000:
            off, size, name, t = self.objs[i]
            if off <= addr < off + size and (best is None or size < best[1]):
                best = self.objs[i]
            i -= 1
        return best

    def to_other(self, addr):
        """(address in the other ROM, how) for an English text address, or (None, why)"""
        m = self.m
        if addr in m.placed and addr in self.text:
            return m.placed[addr], m.how[addr]
        voted = m.resolved(addr)
        if voted is not None and m.votes[addr][voted] >= 2:
            return voted, "pointer"
        obj = self.containing(addr)
        if obj is None:
            return (voted, "pointer") if voted is not None else (None, "no-object")
        off, size, name, t = obj
        if off not in m.placed:
            return (voted, "pointer") if voted is not None else (None, "unplaced")
        how = m.how[off]
        if how in ("data-layout-differs", "remapped", "chained-strings", "fixed-rows"):
            return (voted, "pointer") if voted is not None else (None, "layout")
        if off in self.text and addr != off:
            return None, "inside-text"
        return m.placed[off] + (addr - off), how


INCBIN = re.compile(r'\b(\w+)\s*\[\s*\]\s*=\s*INCBIN_U(?:8|16|32)\(\s*"([^"]+)"\s*\)')
# Easy Chat's move and species groups: ID lists the other ROM sorts by its own names
ID_LISTS = (
    ("sEasyChatGroup_Move1", "src/data/easy_chat/easy_chat_group_move_1.h", "include/constants/moves.h"),
    ("sEasyChatGroup_Move2", "src/data/easy_chat/easy_chat_group_move_2.h", "include/constants/moves.h"),
    ("sEasyChatGroup_Pokemon", "src/data/easy_chat/easy_chat_group_pokemon.h", "include/constants/species.h"),
    ("sEasyChatGroup_Pokemon2", "src/data/easy_chat/easy_chat_group_pokemon2.h", "include/constants/species.h"),
)


def lz77(data, offset):
    """(decompressed bytes, compressed size) of GBA BIOS LZ77 data at `offset`, or None"""
    if offset < 0 or offset + 4 > len(data) or data[offset] != 0x10:
        return None
    size = data[offset + 1] | (data[offset + 2] << 8) | (data[offset + 3] << 16)
    if not 0 < size <= 0x40000:
        return None
    out, i = bytearray(), offset + 4
    try:
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
                    if disp > len(out):
                        return None
                    for _ in range(length):
                        out.append(out[-disp])
                else:
                    out.append(data[i])
                    i += 1
    except IndexError:
        return None
    return bytes(out[:size]), i - offset


def text_graphics(m, mapper, en, other):
    """[rel, offset, size] of every INCBIN'd graphic whose bytes the other ROM changes (text drawn
    into tiles), when this repository's file still holds the English bytes (the port did not
    redraw it) and the other ROM's data has the same shape"""
    syms = {s[3]: s for s in m.syms}
    out, seen = [], set()
    for dirpath, dirs, names in os.walk(os.path.join(ROOT, "src")):
        if "platform" in os.path.relpath(dirpath, ROOT).replace("\\", "/").split("/"):
            continue
        for name in names:
            if not name.endswith((".c", ".h")):
                continue
            text = open(os.path.join(dirpath, name), encoding="utf-8").read()
            for sym, rel in INCBIN.findall(text):
                if rel in seen or sym not in syms or not rel.startswith("graphics/"):
                    continue
                seen.add(rel)
                off, size = syms[sym][0], syms[sym][1]
                if off not in m.placed or m.how[off] in ("data-layout-differs", "remapped"):
                    continue
                at = m.placed[off]
                path = os.path.join(ROOT, rel)
                if not os.path.exists(path):
                    continue
                mine = open(path, "rb").read()
                if rel.endswith(".lz"):
                    e, o, f = lz77(en, off), lz77(other, at), lz77(mine, 0)
                    if not e or not o or not f or f[0] != e[0] or len(o[0]) != len(e[0]) or o[0] == e[0]:
                        continue
                    out.append([rel, at, o[1]])
                else:
                    n = len(mine)
                    if en[off:off + n] != mine or other[at:at + n] == mine:
                        continue
                    same = sum(a == b for a, b in zip(other[at:at + n], mine))
                    if same < n // 3:
                        continue        # mostly different: more likely a misplacement than text
                    out.append([rel, at, n])
    return sorted(out)


def id_lists(m, en, other):
    syms = {s[3]: s for s in m.syms}
    out = []
    for name, rel, constants in ID_LISTS:
        if name not in syms or syms[name][0] not in m.placed:
            continue
        off, size = syms[name][0], syms[name][1]
        at = m.placed[off]
        ids = sorted(en[off + i] | (en[off + i + 1] << 8) for i in range(0, size, 2))
        theirs = sorted(other[at + i] | (other[at + i + 1] << 8) for i in range(0, size, 2))
        if ids == theirs:
            out.append([name, rel, constants, at, size])
    return out


def placeholders(data):
    """The {PLACEHOLDER} codes (0xFD xx) a text uses"""
    out, i = set(), 0
    while i < len(data) - 1:
        if data[i] == 0xFD:
            out.add(data[i + 1])
            i += 2
        elif data[i] == 0xFC:
            i += 2                  # a control code's first argument is never a placeholder
        else:
            i += 1
    return out


def other_string(rom, at):
    if at is None or not 0 <= at < len(rom):
        return None
    end = rom.find(b"\xff", at, at + MAX_TEXT)
    return None if end < 0 else end + 1 - at


def main():
    ap = argparse.ArgumentParser(description="Writes a language's text manifest")
    ap.add_argument("--locale", required=True)
    ap.add_argument("--rom", required=True, help="the official ROM of that language")
    ap.add_argument("--elf", required=True, help="pret pokefirered_rev1.elf linked with --emit-relocs")
    ap.add_argument("--english-rom", required=True, help="pret pokefirered_rev1.gba (byte-identical BPRE 1.1)")
    ap.add_argument("--report", help="write a per-file coverage report here")
    ap.add_argument("--cache", help="pickle of the matcher's placements, reused when present (development)")
    opts = ap.parse_args()

    lang = next((l for l in json.load(open(LANGUAGES, encoding="utf-8"))["languages"] if l["code"] == opts.locale), None)
    if lang is None or lang.get("source") != "rom":
        sys.exit(f"'{opts.locale}' is not a language made from a ROM in {LANGUAGES}")
    other = open(opts.rom, "rb").read()
    if hashlib.sha1(other).hexdigest() != lang["romSha1"]:
        sys.exit(f"{opts.rom}: SHA-1 differs from languages.json ({lang['romSha1']})")
    en = open(opts.english_rom, "rb").read()
    if hashlib.sha1(en).hexdigest() != ENGLISH_SHA1:
        sys.exit(f"{opts.english_rom} is not byte-identical to BPRE 1.1 (build it with make compare_firered_rev1)")

    if opts.cache and os.path.exists(opts.cache):
        m = match_rom.Matcher(opts.elf, en, other)
        state = pickle.load(open(opts.cache, "rb"))
        m.placed, m.how, m.votes = state["placed"], state["how"], state["votes"]
    else:
        m = match_rom.run(opts.elf, en, other)
        if opts.cache:
            with open(opts.cache, "wb") as fh:
                pickle.dump({"placed": m.placed, "how": m.how, "votes": m.votes}, fh)
    mp = Mapper(m)
    syms = {}
    for off, size, t, name, bind in m.syms:
        syms.setdefault(name, (off, size))

    cm = load_charmap(os.path.join(ROOT, "charmap.txt"))
    patch_dir = os.path.join(HERE, opts.locale, "patches")
    patches = sorted(os.path.join(patch_dir, p) for p in os.listdir(patch_dir)) if os.path.isdir(patch_dir) else []
    rels = text_files()
    sources = patched_sources(rels, patches, os.path.join(ROOT, "build-vita-" + opts.locale, "manifest-base"))

    files, unmapped, why_count = {}, [], Counter()
    per_file = {}
    for rel in rels:
        text = sources[rel]
        is_asm = rel.endswith((".inc", ".s"))
        spans = asm_spans(text, cm) if is_asm else c_spans(text, cm)
        if not spans:
            continue
        edits, cursor = [], defaultdict(int)
        for start, end, owner, data in spans:
            why = None
            if data is None:
                why = "encoding"
            elif owner is None or owner not in syms:
                why = "no-symbol"
            else:
                base, size = syms[owner]
                if is_asm:
                    addr = base if en[base:base + len(data)] == data else None
                else:
                    lo = base + cursor[owner]
                    hit = en.find(data, lo, base + max(size, len(data)))
                    addr = hit if hit >= 0 else None
                    if addr is not None:
                        cursor[owner] = addr + len(data) - base
                if addr is None:
                    why = "english-differs"
                else:
                    at, how = mp.to_other(addr)
                    n = other_string(other, at)
                    if n is None:
                        why = how if at is None else "no-terminator"
                    elif placeholders(other[at:at + n]) - placeholders(data):
                        # The other ROM's text asks for a buffer ({STR_VAR_n}, {PLAYER}...) that
                        # its own code fills but the English code does not
                        why = "placeholders"
                    elif len(data) > 12 and not 0.4 < n / len(data) < 3.0:
                        why = "length"        # almost certainly another string: keep English
                    else:
                        edits.append([start, end, at, n, "asm" if is_asm else "c"])
            if why:
                why_count[why] += 1
                unmapped.append([rel, start, why])
        per_file[rel] = (len(edits), len(spans))
        if edits:
            files[rel] = {"sha256": hashlib.sha256(text.encode("utf-8")).hexdigest(), "edits": edits}

    total = sum(t for _, t in per_file.values())
    done = sum(e for e, _ in per_file.values())
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    manifest = {
        "_comment": "Text manifest of this language (tools/locales/gen_manifest.py): source spans and ROM "
                    "offsets, no game text. Method derived from Emerald 3DS's locale tools (CC BY-NC-SA 4.0, NOTICE.md).",
        "version": 1, "romSha1": lang["romSha1"], "sourceCommit": head,
        "files": files, "graphics": text_graphics(m, mp, en, other), "idLists": id_lists(m, en, other),
        "unmapped": unmapped,
    }
    out = os.path.join(HERE, opts.locale, "manifest.json.gz")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "wb") as fh:
        fh.write(gzip.compress(json.dumps(manifest, separators=(",", ":")).encode("utf-8"), mtime=0))
    print(f"{out}: {done} of {total} text spans mapped in {len(files)} files; unmapped by reason: {dict(why_count)}; "
          f"{len(manifest['graphics'])} text graphics, {len(manifest['idLists'])} Easy Chat ID lists")
    if opts.report:
        with open(opts.report, "w", encoding="utf-8", newline="\n") as fh:
            for rel, (e, t) in sorted(per_file.items(), key=lambda kv: kv[1][0] - kv[1][1]):
                fh.write(f"{e:5d}/{t:<5d} {rel}\n")


if __name__ == "__main__":
    main()
