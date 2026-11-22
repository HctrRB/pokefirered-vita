# User interface

Type: reference · Audience: developers · Status: Stage 2 Complete Native 960x544 16:9 UI Suite Active (Phase F7 complete: F7a + F7b-1 + F7b-2 + F7b-3 + F7c; Stage 1 3DS overlay retired)
Verified against: Phase F7 complete build, ELF, core_boundary & doc_check (2026-11-22)

How the complete native 960x544 16:9 UI (`src/platform/ui/`, `tools/ui2/`) works in `pokefirered-vita`
(Phase F7), ported 1:1 from
`pokeemerald-vita-3ds` with authentic Pokémon FireRed GBA palettes (`UI_THEME_GBA`) and adapted to
FireRed v1.1 (`REVISION=1`, Kanto & Sevii Islands + `Pokemon-FireRed-3DS` v0.5.2 extras).

---

## Stage 1 Retirement (`F7b-1`): FireRed 3DS Touch Overlay Removed from Build

During bring-up (`Phases F1–F6`), `pokefirered-vita` used the 320x240 bottom-screen overlay from
`NachoGonzalez2001/Pokemon-FireRed-3DS` (`fr_bottom_ui.c` + `fr_vita_ui.c`, while preserving
`fr_extras.c` and `fr_ui_icons.h`). From **Sub-Fase F7b-1** onward, the dual-screen Stage 1 overlay
is retired from the build:

- `CMakeLists.txt` excludes `fr_bottom_ui.c` and `fr_vita_ui.c` from `GAME_C`.
- `vita_main.c` reads controls solely via `VitaControls_MapButtons` merged with `Ui_TakeInjectedKeys()`,
  and calls `FrVitaBridge_FrameDone()` / `FrVitaBridge_NoteReset()` (`fr_vita_bridge.c`) for frame/reset counters.
- Legacy `Port_MenuOnBottom`, `Port_NativePage`, `Port_RegionMapOnBottom`, `Port_BattleMenu`, and
  `CtrExtras_ShowScreen` symbols remain as clean no-op stubs in `fr_vita_bridge.c` so `PORT_BRIDGE`
  merge points stay intact.

---

## Stage 2 (Complete `F7a–F7c`): Native 960x544 16:9 Vita UI (`src/platform/ui/`)

The native 960x544 pipeline resolves the three structural limits of the 320x240 3DS overlay:
1. Renders crisply at native 960x544 in hardware GL (`Ui_Render()`) using baked vector typography and
   RGBA5551 sprites (`ui_sprites.c`).
2. In 3D Voxel + HD-2D mode ([VOXEL_ENGINE.md](VOXEL_ENGINE.md)), overworld dialogues, YES/NO,
   multichoice, money/coins boxes, zone signs, starter (`ScriptMenu_GetMonPicForUi`) cards, and 3D
   battle HUDs (`Ui_BattleDispcntClear`, `gUiHideBattleHudSprites`) are rendered natively at 960x544,
   suppressing GBA **Pass 6** whenever `Ui_MirrorCovers()` or the native 3D Battle HUD is active
   (`5–18 ms/frame` saved on hardware).
3. Shares the 1:1 `pokeemerald-vita-3ds` layout, bevels, panels, and input discipline across all 6
   themes (`GBA`, `RED`, `EMERALD`, `CRYSTAL`, `TROPICAL`, `ROYAL`), while `UI_THEME_GBA` uses
   authentic **Pokémon FireRed GBA** colors (vermilion `#D54129`, crimson `#9C2418`, `type1`
   indigo-slate `#8B8BCD` borders, cobalt slate strips/rails, warm ivory panels `#F4F4EA`).

### Languages (`ui_locale.c`, [ADDING_A_LANGUAGE.md](../guides/ADDING_A_LANGUAGE.md))

Every UI string is English in the code and goes through `UI_TR` (literal, cached per call site) or
`UiLocale_Lookup` (table strings marked with `UI_N`, including the Options rows in
`src/platform/vita/vita_extras.c`). English builds compile both to their argument; a non-English
build (`UI_LOCALE_CATALOG`) looks the English text up by FNV-1a hash in `app0:ui2/locale.bin`, which
`UiLocale_Load` reads once in `Ui_Init` before the boot snapshot and the game thread, so Core 0 and
Core 1 read it without locks and a SoftReset keeps it. GBA text reaches the UI as UTF-8
(`UiGame_Text`), and `UiText_Draw` folds any Latin-1 letter the build's atlas lacks to its base
letter. Prompts that embed a name use one format string split around `%s` (battle
`"What will %s do?"`), so each language keeps its own word order.

### 1. Multicore Lock-Free UI Architecture (`src/platform/ui/`)

Ported directly from `pokeemerald-vita-3ds/src/platform/ui/`:

- **Core 1 (`Ui_Tick`):** Runs on the game thread inside `VBlankIntrWait()`, reads game structures
  safely on Core 1 (`ui_game.c`, `ui_mirror.c`), and emits a compact quad command list (`UiDrawList`,
  `UI_MAX_QUADS = 4096`, triple-buffered via `__atomic_exchange_n`). `Ui_Tick()` auto-closes outside
  `CB2_Overworld` (unless `UiCore_IsHostCallback()` is true) and whenever `Port_SceneShown()` or
  `QL_IS_PLAYBACK_STATE` is active. Returning from GBA sub-scenes restores stacked menus cleanly via
  `UiCore_TryRestoreAfterScene()` (`src/start_menu.c`).
- **Field Coexistence Fixes (`F7b-1`):**
  - `src/scrcmd.c` guards `START_BUTTON` (`ScriptContext_GetQuestLogInput`) behind `!Ui_OwnsFieldStart()`.
  - `src/help_system_util.c` blocks Help System (`RunHelpSystemCallback`) when `Ui_IsOpen() || Ui_ConsumesInput()`.
  - `src/text_printer.c` maintains `gUiPrinterStart` / `GetTextPrinterForUi` so live dialogue boxes
    track exact active strings; `src/script_menu.c` exposes `ScriptMenu_GetMultichoiceWindowForUi`
    and `ScriptMenu_GetMonPicForUi` (rendering starter selection `208x216` preview cards in 3D);
    `src/money.c`, `src/coins.c`, and `src/list_menu.c` expose their BG0 window IDs so `OtherBg0Windows`
    only covers Pass 6 when every active window has a native mirror.
- **Core 0 (`Ui_Render`, Pass 7):** Draws the active `UiDrawList` through a 4-slot dynamic VBO ring
  (`UI_VBO_RING_SIZE`) from one **`UI_TEX_W x UI_TEX_H` (`1024x2048`) RGBA8888 texture** (`8 MiB`
  unpacked via `Ui_UnpackTexel`):
  - **Fixed top atlas (`assets/ui2/ui_atlas.bin` v3, up to `UI_FIXED_MAX_H = 1280` rows):** Built at
    compile time by `tools/ui2/build_atlas.py`. Uploaded once at boot; if `ui_atlas.bin` fails to
    load, the game cleanly falls back to GBA menus.
  - **Runtime 64x64 sprite cells (`UiAssets_CellsY()` to `UI_TEX_H`, up to `UI_MAX_SPRITES = 256`):**
    Core 1 decodes live GBA sprites into a staging ring; Core 0 writes dirty cells **in place** via
    `vglGetTexDataPointer` and only reuses cells idle for `UI_SPR_IDLE_TICKS` (4 ticks).

### 2. Six Menu Themes (`ui_theme.c`) — Featuring `RED` and `GBA`

The theme engine in `pokeemerald-vita-3ds:ui_theme.c` already includes:

| Theme | Visual Style | Typography | FireRed Role |
|---|---|---|---|
| `RED` | Chamfered dark-slate corners, diagonal frame stripes, crimson-red highlight bar | Saira Condensed | **Signature FireRed theme** (default or co-default with `GBA`) |
| `GBA` | FireRed GBA UI palette replacing Emerald's greens (vermilion/crimson headers & active accents, cool cobalt/slate blue borders/bars, warm cream/off-white panels), GBA bevels, top section bar (`UiW_GbaTopBar`) & L/R help badges | VT323 + Press Start 2P | Authentic original Pokémon FireRed GBA window feel at native 960x544 |
| `EMERALD` | Warm paper panels, double bevel, dark teal frame | Press Start 2P + VT323 | Optional classic theme |
| `CRYSTAL` | Cool ice panels, glossy pill highlights | M PLUS Rounded 1c | Optional theme |
| `TROPICAL` | Warm sand panels, coral/amber pills, heavy dark outlines | Fredoka | Optional theme |
| `ROYAL` | Deep forest-and-gold aesthetic, chamfered selection bar | Exo 2 | Optional theme |

Each theme selects the matching GBA text window frame (`UiTheme.gbaWindowFrame` via
`Ui_GbaWindowFrame()`) for full-screen GBA scenes without altering the save file.

### 3. Screen-by-Screen Adaptation Matrix (`pokeemerald-vita-3ds` -> `pokefirered-vita`)

Following the proven rules of `pokeemerald-vita-3ds:UI_PLAN.md` and `pokeemerald-vita-3ds:UI_AUDIT.md`:
- **Rule 1:** Menus, HUDs, dialogues, and lists become native 960x544 (`src/platform/ui/`), calling
  the game's own rules via `*ForUi` helpers in `src/*.c` so script state (`gSpecialVar_Result`, etc.)
  and Quest Log recording (`QL_STATE_RECORDING`) stay 100% intact.
- **Rule 2:** Mini-games and full-screen animated scenes remain **Group A full-screen GBA scenes**
  (rendered at 816x544 centered with black bars, or 16:9 for stage scenes).
- **Rule 3:** Preserve existing `ux0:data/FRVITA/settings.txt` user preferences across the Stage 1 ->
  Stage 2 migration.

| Subsystem / Screen | Implementation in `src/platform/ui/` | FireRed Adaptation & Game Hooks |
|---|---|---|
| **Overworld Mirrors & Signs** | `ui_mirror.c`, `ui_sign.c` | Dialogues, YES/NO, multichoice, Coins box, scrollable multichoice mirrors (`OtherBg0Windows`), Oak's Lab 3D starter preview (`ScriptMenu_GetMonPicForUi`), Kanto/Sevii zone signs (`ui_town_signs.h`). When `Ui_MirrorCovers()` is true, **Pass 6 is skipped completely** in 3D |
| **START Menu Hub** | `ui_scr_hub.c` (`gUiHubScreen`) | FireRed entries: `POKéDEX` (`HUB_DEX`), `POKéMON` (`HUB_MON`), `BAG` (`HUB_BAG`), `[PLAYER]` (`HUB_CARD`), `SAVE`, `OPTION`, plus Fuchsia **Safari Zone** balls/steps banner (`SafariZoneRetirePrompt`) and `gVitaBuildVersion` |
| **Battle HUD & Battle Menus** | `ui_battle_hud.c`, `ui_game_battle.c` | Native 960x544 Battle HUD, Move Split icons, Mega Evolution Key Stone button (`SELECT`), Battle Party (`Ui_OpenBattleParty`), Battle Bag (`Ui_OpenBattleBag`), Level-Up stat card, Yes/No prompt, Safari Zone plate (`SAFARI BALLS / Left: N`), and Old Man / Poké Dude GBA fallback. Sets `Ui_BattleDispcntClear` & `gUiHideBattleHudSprites` so 3D battles skip Pass 6 HUD |
| **Party, 3-Page Summary & Choose/Forget/Maniac** | `ui_scr_party.c`, `ui_scr_choose.c`, `ui_scr_forget.c` | Native 6-slot Party + 3-page Summary (`INFO`, `SKILLS`, `MOVES` with Move Split icons, no Contest page), FireRed HMs (`CUT`..`WATERFALL` + `TELEPORT`, `DIG`, `SOFTBOILED`, `MILK DRINK`), `gUiTargetScreen` item use, `Ui_OpenChooseMon` (`src/party_menu.c`: Tutor, Minigame, Daycare, Multi Battle, Relearner, Any), `Ui_OpenForgetMove` (`src/pokemon_summary_screen.c`), and **Two Island Move Maniac** (`Ui_OpenMoveRelearner` in `src/learn_move.c`) |
| **Bag, TM Case, Berry Pouch & Berry Crush** | `ui_scr_bag.c`, `ui_game_items.c`, `ui_scr_berry.c` | Native 5-Pocket FireRed Bag (`ITEMS`, `KEY ITEMS`, `POKé BALLS`, `TMs & HMs`, `BERRIES`), direct jump-to-tab hooks from `src/tm_case.c` and `src/berry_pouch.c`, dedicated Berry Crush / Dodrio Berry Picking / Berry Blender picker (`gUiBerryScreen`, `Ui_OpenBerryChoice`), and `QL_STATE_RECORDING` preservation |
| **Pokédex & Post-Catch Dex Card** | `ui_scr_dex.c` (`gUiDexScreen`, `gUiDexDetailScreen`) | Kanto & National Pokédex + FireRed's **9 Habitat categories**, 6 sort orders, 3 filters, 4-tab detail view (`INFO`, `AREA`, `CRY`, `SIZE` with Red/Leaf silhouettes), and post-catch native Pokédex entry (`Ui_OpenCaughtDexPage` via `Cmd_displaydexinfo` in `src/battle_script_commands.c`) |
| **Kanto & Sevii Region / Fly Map** | `ui_scr_nav.c`, `ui_game_nav.c` (`gUiMapScreen`, `gUiFlyScreen`) | Standalone 4-region **Kanto & Sevii Region / Fly Map** (`L`/`R` switching across **Kanto**, **One–Three Islands**, **Four–Five Islands**, and **Six–Seven Islands**), hooked from `MOVE_FLY`, `ITEM_TOWN_MAP`, and wall Town Maps (`src/region_map.c`) |
| **Trainer Card** | `ui_scr_card.c` (`gUiCardScreen`) | Front & back Kanto Trainer Card: 8 Kanto Badges (`Boulder`..`Earth`), Hall of Fame debut, Trades, Link Battles, Berry Crush, Union Room, and Four Island Bragging Stickers (`src/trainer_card.c`) |
| **Poké Mart** | `ui_scr_mart.c` (`gUiMartScreen`) | Native Buy/Sell Poké Mart & Celadon Dept. Store floors (`src/shop.c`) |
| **Bill's PC & Player's PC + Mail Reader** | `ui_scr_pc.c`, `ui_scr_playerpc.c` (`gUiPcScreen`, `gUiPlayerPcScreen`, `gUiMailReadScreen`) | Bill's / Celio's 14-box storage (`SELECT` QUICK, `UiParty_PushSummary`, box jump/wallpaper/rename, Kanto Indigo Plateau HM release safety) + **Player's PC** (`src/player_pc.c`: Withdraw, Deposit, Toss, Mailbox & Mail Reader `UiMailRead_Open`) |
| **Options, Graphics, Enhancements, Cheats & Nuzlocke** | `ui_scr_options.c`, `ui_scr_nuz.c` (`gUiOptionsScreen`, `gUiNuzlockeScreen`) | 6-tab Options (`GAME`, `GRAPHICS`, `CONTROLS`, `ENHANCEMENTS`, `CHEATS`, `SAVE SLOT`) + Kanto 8-badge Nuzlocke tracker, preserving `ux0:data/FRVITA/settings.txt` keys |
| **Naming & Easy Chat** | `ui_scr_naming.c`, `ui_scr_easychat.c` (`gUiNamingScreen`, `gUiEasyChatScreen`) | Native Naming Screen (`DoNamingScreen` in `src/naming_screen.c`, UPPER/lower/0-9 pages + Vita physical buttons) and Native Easy Chat (`DoEasyChatScreen` in `src/easy_chat.c`: Profile, Mail, Questionnaire) |
| **Group A Full-Screen GBA Scenes** | [SCENES_16_9.md](SCENES_16_9.md) (`GameFrameOverlay.gbaFormat`, `GameFrameOverlay.gbaWindow`) | **Help System** (`L/R`), **Save Failed**, **Quest Log playback** (`QL_IS_PLAYBACK_STATE`), **Fame Checker**, **Teachy TV**, Oak's Lab starter table, Celadon Slot Machine, Berry Crush gameplay, Pokémon Jump, Dodrio Berry Picking, Sevii ferries, Evolution, Egg Hatch, Trade, Link rooms, Intro, Title, Credits, Hall of Fame, Diploma |
