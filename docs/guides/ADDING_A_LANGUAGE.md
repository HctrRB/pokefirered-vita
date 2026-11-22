# Adding a language

Type: guide · Audience: developers and translators · Status: current (Spanish `es` built and tested on PS Vita)
Verified against: `-Locale es -Rom` build and PS Vita hardware test (2026-11-23)

A build has one language, and everything on screen follows it: the game's own GBA screens
(dialogue, battle text, Help System, Fame Checker, Teachy TV, Easy Chat) and the native 960x544
interface. The repository's sources stay in English; a translated build compiles a staging copy of
them (`build-vita-<code>/stage/`) with the language's text and patches applied. Only Latin-script
languages are supported.

## What a language is made of

One row of `tools/locales/languages.json` and one folder, `tools/locales/<code>/`:

| Piece | Where | What |
|---|---|---|
| Registry row | `tools/locales/languages.json` | `code` (folder and `VITA_LOCALE` value), `gameLanguage` (`LANGUAGE_*`), `romCode` (4th letter of the cartridge code, `BPR?`), `romSha1` (clean official dump), `source` (`none` or `rom`), `letters` (letters beyond ASCII the UI fonts need), `units` (`metric` or `imperial`) |
| Interface text | `tools/locales/<code>/ui.json` | Every key of `tools/locales/en/ui.json` and its translation |
| Game text | `tools/locales/<code>/manifest.json.gz` | Source spans and ROM offsets/sizes of every game text, text-bearing graphic and Easy Chat ID list; no ROM bytes |
| Layout fixes | `tools/locales/<code>/patches/` | Patches for GBA code whose word order or windows assume English, applied only to the staging tree |

## Steps

1. **Register it.** Add the row to `tools/locales/languages.json`. Every letter in `letters` must
   already be in `tools/ui2/fonts/locale_glyphs.json` (`tools/ui2/build_atlas.py --locale` refuses
   unbaked letters; `tools/ui2/bake_locale_glyphs.py` bakes new ones).
2. **Translate the interface.** Fill `tools/locales/<code>/ui.json` as
   `{"strings": {"English": "Translation"}}`. Keep every printf conversion (`%s`, `%d`, `%03u`) in
   the same order and type; `tools/locales/compile_ui.py` stops the build otherwise and writes
   `build-vita-<code>/ui2/locale.bin.txt` with missing strings and translations much wider than
   the English text. After changing any `UI_TR` / `UI_N` string in the code, run
   `python tools/locales/extract_ui.py` (the build runs it with `--check`).
3. **Map the game text (`source: rom`, developers only).** Build pret/pokefirered at the commit in
   `upstream.lock` with agbcc (`make compare_firered_rev1`, linked with `--emit-relocs`, so the ROM
   is byte-identical to BPRE 1.1), then:
   ```sh
   python tools/locales/gen_manifest.py --locale <code> --rom <official .gba> \
       --elf pokefirered_rev1.elf --english-rom pokefirered_rev1.gba
   ```
   `tools/locales/match_rom.py` places the English build's symbols in the other ROM (function
   matching with relocations masked, pointer votes, gap alignment, text chains); `gen_manifest.py`
   then maps every `.string` run and every `_("...")` initializer of the sources to the other
   ROM's text. Spans it cannot place safely stay English and are listed under `unmapped` (by file,
   position and reason): text the port added, text whose placeholders the English code does not
   fill, or lengths that point to another string. Re-run it whenever a source holding game text
   or a patch changes: `tools/locales/localize.py` refuses files whose hash differs.
4. **Fix layouts.** Add `tools/locales/<code>/patches/NN-name.patch` (unified diff against the
   repository sources) for GBA code that assembles text in English order. Screens drawn by the
   native UI need no patch. Re-run step 3 after adding a patch.
5. **Build.**
   ```powershell
   powershell.exe -ExecutionPolicy Bypass -File .\build_vita.ps1 -Locale <code> -Rom <official .gba>
   powershell.exe -ExecutionPolicy Bypass -File .\build_vita.ps1 -Locale <code> -UiOnly   # UI only, English game text
   ```
   `build_vita.sh` runs `localize.py` (copy, patches, ROM text and graphics, Easy Chat order, town
   signs through `tools/ui2/gen_town_signs.py --root`), builds the language's atlas and catalog,
   and configures CMake on the staging tree with `-DVITA_DOC_CHECK=OFF`.

## Rules

- Nothing extracted from a ROM is committed: manifests hold offsets only, and the staging tree and
  the research builds live under the ignored `build-vita-<code>/`.
- `localize.py` checks the ROM's SHA-1 against `romSha1` before reading it.
- Test every translated screen on real PS Vita hardware (Vita3K shows a black screen with vitaGL).
