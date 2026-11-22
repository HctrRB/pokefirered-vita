// Native 960x544 TRAINER CARD (ported 1:1 from pokeemerald-vita-3ds with FireRed Kanto
// Gym Badges, Red/Leaf portraits, 0..4 Kanto/National/Minigame Stars, Last Hall of Fame
// team icons, and Four Island Stickers on Front & Back tabs).

#include <stdio.h>
#include "ui_internal.h"
#include "ui_game.h"
#include "../vita/vita_platform.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include "global.h"
#include "event_data.h"
#include "mail_data.h"
#include "overworld.h"
#include "pokedex.h"
#include "pokemon_icon.h"
#include "sound.h"
#include "trainer_card.h"
#pragma GCC diagnostic pop
#include "constants/flags.h"
#include "constants/game_stat.h"
#include "constants/songs.h"
#include "constants/species.h"
#include "constants/trainers.h"
#include "constants/vars.h"

enum { TAB_CARD = 0, TAB_BACK, TAB_COUNT };

static const char *const sTabs[TAB_COUNT] = { UI_N("CARD"), UI_N("BACK") };
static const char *const sGbaPages[TAB_COUNT] = { UI_N("TRAINER FRONT"), UI_N("TRAINER BACK") };
static const char *const sBadgeNames[8] = {
    UI_N("BOULDER"), UI_N("CASCADE"), UI_N("THUNDER"), UI_N("RAINBOW"),
    UI_N("SOUL"), UI_N("MARSH"), UI_N("VOLCANO"), UI_N("EARTH")
};
static const char *const sLeaderNames[8] = {
    UI_N("BROCK"), UI_N("MISTY"), UI_N("LT. SURGE"), UI_N("ERIKA"),
    UI_N("KOGA"), UI_N("SABRINA"), UI_N("BLAINE"), UI_N("GIOVANNI")
};
static const char *const sGymTowns[8] = {
    UI_N("PEWTER"), UI_N("CERULEAN"), UI_N("VERMILION"), UI_N("CELADON"),
    UI_N("FUCHSIA"), UI_N("SAFFRON"), UI_N("CINNABAR"), UI_N("VIRIDIAN")
};
static const char *const sBadgeDescs[8] = {
    UI_N("Boosts ATTACK and enables FLASH outside battle."),
    UI_N("POKéMON up to Lv.30 obey. Enables CUT."),
    UI_N("Boosts SPEED and enables FLY outside battle."),
    UI_N("POKéMON up to Lv.50 obey. Enables STRENGTH."),
    UI_N("Boosts DEFENSE and enables SURF outside battle."),
    UI_N("POKéMON up to Lv.70 obey. Enables ROCK SMASH."),
    UI_N("Boosts SP.ATK & SP.DEF. Enables WATERFALL."),
    UI_N("All POKéMON obey without question!")
};

static int32_t sTab, sBadge, sTabX[TAB_COUNT], sTabW[TAB_COUNT];
static uint32_t sFrame;

static void Enter(void)
{
    sTab = TAB_CARD;
    sBadge = 0;
}

static bool Update(const UiInput *in)
{
    const bool gba = gUiTheme->gbaFrame;
    sFrame++;
    if (in->pressed & VITA_BTN_CIRCLE)
    {
        PlaySE(SE_SELECT);
        return false;
    }

    int32_t t = -1;
    if (gba)
    {
        const int32_t ph = UiW_GbaPagerHit(in, true, TAB_COUNT);
        if (ph != 0)
            t = sTab ^ 1;
    }
    else
        t = UiKit_TabHit(in, sTabX, sTabW, TAB_COUNT);
    if (in->pressed & (VITA_BTN_L | VITA_BTN_R | VITA_BTN_CROSS))
        t = sTab ^ 1;
    if (t >= 0 && t != sTab)
    {
        sTab = t;
        PlaySE(SE_SELECT);
        return true;
    }

    if (sTab == TAB_CARD)
    {
        const int32_t before = sBadge;
        if (in->repeat & VITA_BTN_LEFT) sBadge = (sBadge + 7) % 8;
        if (in->repeat & VITA_BTN_RIGHT) sBadge = (sBadge + 1) % 8;
        if (gba)
        {
            for (int32_t i = 0; i < 8; i++)
                if (UiHit(in, 614 + (i & 1) * 162, 108 + (i >> 1) * 69, 154, 62))
                    sBadge = i;
        }
        else
        {
            for (int32_t i = 0; i < 8; i++)
                if (UiHit(in, 492 + (i % 4) * 110, 116 + (i / 4) * 92, 104, 86))
                    sBadge = i;
        }
        if (sBadge != before)
            PlaySE(SE_SELECT);
    }
    return true;
}

static int32_t CardStarCount(void)
{
    int32_t stars = 0;
    if (GetGameStat(GAME_STAT_ENTERED_HOF) > 0)
        stars++;
    if (HasAllKantoMons())
        stars++;
    if (HasAllMons())
        stars++;
    if (gSaveBlock2Ptr->berryPick.berriesPicked >= 200 && gSaveBlock2Ptr->pokeJump.jumpsInRow >= 200)
        stars++;
    return stars;
}

static int32_t StickerLevel(const uint16_t varId, const uint16_t th1, const uint16_t th2, const uint16_t th3)
{
    const uint16_t v = VarGet(varId);
    if (v >= th3) return 3;
    if (v >= th2) return 2;
    if (v >= th1) return 1;
    return 0;
}

static unsigned StatCapped(const u8 statId, const u32 maxVal)
{
    const u32 v = GetGameStat(statId);
    return (unsigned)(v > maxVal ? maxVal : v);
}

static void Row(int32_t x, int32_t y, int32_t w, const char *k, const char *v, uint32_t vcol)
{
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, x, y, k, UI_COL_INK);
    UiText_Draw(UI_FACE_VT323, UI_BODY_PX, x + w - UiText_Width(UI_FACE_VT323, UI_BODY_PX, v), y, v, vcol);
    UiDraw_Rect(x, y + 32, w, 2, UI_COL_PAPER2);
}

static void CardRowGba(int32_t x, int32_t y, int32_t w, const char *k, const char *v, uint32_t vcol)
{
    UiW_GbaBox(x, y, w, 46, 3, UI_RGBA(200, 180, 104, 255), UI_RGBA(255, 253, 245, 255), UI_RGBA(255, 253, 245, 255), C_WHITE, 0);
    UiDraw_PushClip(x + 4, y + 2, w - 8, 42);
    UiW_GbaTextMid(UI_FACE_VT323, 24, x + 12, y, 46, k, UI_RGBA(110, 88, 32, 255), UI_GBA_SHAD_LIGHT);
    UiW_GbaTextMid(UI_FACE_VT323, 28, x + w - 12 - UiText_Width(UI_FACE_VT323, 28, v), y, 46, v, vcol, UI_GBA_SHAD_LIGHT);
    UiDraw_PopClip();
}

static void BuildGba(void)
{
    char buf[96], name[16];
    const uint16_t tid = (uint16_t)(gSaveBlock2Ptr->playerTrainerId[0] | (gSaveBlock2Ptr->playerTrainerId[1] << 8));
    const int32_t stars = CardStarCount();
    const bool cleared = GetGameStat(GAME_STAT_ENTERED_HOF) > 0;
    UiGame_PlayerName(name, sizeof(name));
    UiW_GbaTopBarPages(UI_GBA_BG_CARD, UiGame_PlayerIsMale() ? UI_SPR_HEAD_RED : UI_SPR_HEAD_LEAF,
                       UI_TR("TRAINER CARD"), sGbaPages, TAB_COUNT, sTab);

    int32_t rx = UI_SCREEN_W - 14;
    UiW_GbaHelpBadge(&rx, "O", UI_TR("BACK"), true);
    if (sTab == TAB_CARD)
        UiW_GbaHelpBadge(&rx, "<>", UI_TR("BADGE"), false);
    UiW_GbaHelpBadge(&rx, "X", UI_TR("FLIP CARD"), false);

    if (sTab == TAB_CARD)
    {
        // Left Trainer Card (14, 60, 572, 472) with FireRed crimson/gold accent header
        UiW_GbaWin(14, 60, 572, 472, UI_GBA_WIN_GOLD);
        UiW_GbaBox(30, 76, 540, 42, 4, UI_RGBA(41, 49, 49, 255), UI_RGBA(186, 48, 48, 255), UI_RGBA(134, 24, 32, 255),
                   UI_RGBA(238, 124, 118, 140), 0);
        UiW_GbaTextMid(UI_FACE_PS2P, 16, 42, 76, 42, UI_TR("KANTO TRAINER CARD"), C_WHITE, UI_GBA_SHAD_DARK);

        for (int32_t s = 0; s < 4; s++)
            UiW_GbaStar(346 + s * 18, 91, s < stars ? UI_RGBA(255, 230, 57, 255) : UI_RGBA(94, 18, 24, 255));

        snprintf(buf, sizeof(buf), UI_TR("IDNo. %05u"), (unsigned)tid);
        UiW_GbaBox(436, 82, 124, 30, 3, UI_RGBA(16, 24, 24, 255), UI_RGBA(24, 32, 32, 255), UI_RGBA(24, 32, 32, 255), 0, 0);
        UiW_GbaTextCentred(UI_FACE_VT323, 24, 436, 82, 124, 30, buf, C_WHITE, UI_GBA_SHAD_DARK);

        CardRowGba(30, 128, 376, UI_TR("NAME"), name, UI_RGBA(49, 49, 49, 255));
        snprintf(buf, sizeof(buf), "$%u", (unsigned)UiGame_Money());
        CardRowGba(30, 182, 376, UI_TR("MONEY"), buf, UI_RGBA(176, 120, 8, 255));
        if (FlagGet(FLAG_SYS_POKEDEX_GET))
            snprintf(buf, sizeof(buf), "%u", (unsigned)UiGame_DexOwned());
        else
            snprintf(buf, sizeof(buf), "-");
        CardRowGba(30, 236, 376, UI_TR("POKéDEX"), buf, UI_RGBA(49, 49, 49, 255));
        UiGame_PlayTime(buf, sizeof(buf));
        CardRowGba(30, 290, 376, UI_TR("PLAY TIME"), buf, UI_RGBA(49, 49, 49, 255));
        snprintf(buf, sizeof(buf), "%u", (unsigned)GetGameStat(GAME_STAT_ENTERED_HOF));
        CardRowGba(30, 344, 376, UI_TR("HALL OF FAME"), cleared ? buf : "-", UI_RGBA(49, 49, 49, 255));
        snprintf(buf, sizeof(buf), "%u", (unsigned)UiGame_Coins());
        CardRowGba(30, 398, 376, UI_TR("GAME CORNER COINS"), buf, UI_RGBA(158, 34, 40, 255));

        // Portrait grid well (418, 128, 152, 316)
        UiW_GbaBox(418, 128, 152, 316, 4, UI_RGBA(150, 132, 72, 255), UI_RGBA(247, 243, 220, 255), UI_RGBA(247, 243, 220, 255), UI_GBA_SHAD_LIGHT, 0);
        for (int32_t g = 16; g < 146; g += 16)
            UiDraw_Rect(421 + g, 131, 1, 310, UI_RGBA(186, 48, 48, 26));
        for (int32_t g = 16; g < 310; g += 16)
            UiDraw_Rect(421, 131 + g, 146, 1, UI_RGBA(186, 48, 48, 26));
        UiSpr_Trainer(UiGame_PlayerIsMale() ? TRAINER_PIC_RED : TRAINER_PIC_LEAF, 430, 198, 2);
        UiW_GbaBox(426, 402, 136, 32, 3, UI_RGBA(24, 32, 32, 255), UI_RGBA(40, 56, 64, 255), UI_RGBA(40, 56, 64, 255), 0, 0);
        UiW_GbaTextCentred(UI_FACE_VT323, 24, 426, 402, 136, 32,
                           cleared ? UI_TR("CHAMPION RANK") : UI_TR("CHALLENGER"), UI_RGBA(255, 230, 57, 255), UI_GBA_SHAD_DARK);

        UiW_GbaBox(30, 474, 540, 38, 3, UI_RGBA(184, 164, 88, 255), UI_RGBA(232, 220, 158, 255), UI_RGBA(232, 220, 158, 255), C_WHITE, 0);
        UiW_GbaTextMid(UI_FACE_VT323, 24, 42, 474, 38, UI_TR("KANTO LEAGUE · FIRERED"), UI_RGBA(92, 70, 18, 255), C_WHITE);
        snprintf(buf, sizeof(buf), UI_TR("BADGES: %u / 8"), (unsigned)UiGame_BadgeCount());
        UiW_GbaTextMid(UI_FACE_VT323, 24, 558 - UiText_Width(UI_FACE_VT323, 24, buf), 474, 38, buf, UI_RGBA(92, 70, 18, 255), C_WHITE);

        // Right Kanto Badge Case panel (598, 60, 348, 472)
        UiW_GbaWin(598, 60, 348, 472, UI_GBA_WIN_DEFAULT);
        snprintf(buf, sizeof(buf), "%u / 8", (unsigned)UiGame_BadgeCount());
        UiW_GbaSectionStrip(614, 74, 316, 26, UI_GBA_TAG_CRIMSON, UI_TR("BADGE CASE"), buf);

        for (int32_t i = 0; i < 8; i++)
        {
            const int32_t bx = 614 + (i & 1) * 162;
            const int32_t by = 108 + (i >> 1) * 69;
            const bool got = FlagGet(FLAG_BADGE01_GET + i);
            const bool on = (i == sBadge);
            if (on)
                UiW_GbaRingBox(bx, by, 154, 62, 4, UI_RGBA(255, 90, 32, 255), UI_RGBA(230, 148, 65, 255),
                               UI_RGBA(255, 246, 205, 255), UI_RGBA(255, 246, 205, 255), C_WHITE, 0);
            else
                UiW_GbaBox(bx, by, 154, 62, 4, UI_RGBA(184, 190, 176, 255), UI_RGBA(244, 245, 236, 255), UI_RGBA(244, 245, 236, 255), UI_GBA_SHAD_LIGHT, 0);
            UiDraw_SpriteTint((uint16_t)(UI_SPR_BADGE_FIRST + i), bx + 10, by + 15, 2,
                              got ? 0xFFFFFFFFu : UI_RGBA(180, 175, 160, 110));
            UiDraw_PushClip(bx + 46, by + 4, 104, 54);
            UiW_GbaText(UI_FACE_VT323, 24, bx + 48, by + 8, got ? UiLocale_Lookup(sBadgeNames[i]) : UI_TR("LOCKED"), UI_RGBA(49, 49, 49, 255), UI_GBA_SHAD_LIGHT);
            UiW_GbaText(UI_FACE_VT323, 20, bx + 48, by + 34, UiLocale_Lookup(sLeaderNames[i]), UI_RGBA(82, 88, 100, 255), UI_GBA_SHAD_LIGHT);
            UiDraw_PopClip();
        }

        const bool selGot = FlagGet(FLAG_BADGE01_GET + sBadge);
        UiW_GbaBox(614, 392, 316, 124, 4, UI_RGBA(150, 172, 156, 255), UI_RGBA(244, 245, 236, 255), UI_RGBA(244, 245, 236, 255), UI_GBA_SHAD_LIGHT, 0);
        snprintf(buf, sizeof(buf), UI_TR("%s BADGE · %s GYM"), UiLocale_Lookup(sBadgeNames[sBadge]), UiLocale_Lookup(sGymTowns[sBadge]));
        UiDraw_PushClip(622, 396, 300, 116);
        UiW_GbaText(UI_FACE_VT323, 24, 624, 400, buf, UI_RGBA(158, 34, 40, 255), UI_GBA_SHAD_LIGHT);
        snprintf(buf, sizeof(buf), UI_TR("Leader %s (%s)"), UiLocale_Lookup(sLeaderNames[sBadge]), selGot ? UI_TR("OBTAINED") : UI_TR("LOCKED"));
        UiW_GbaText(UI_FACE_VT323, 20, 624, 424, buf, selGot ? UI_RGBA(176, 120, 8, 255) : UI_RGBA(82, 88, 100, 255), UI_GBA_SHAD_LIGHT);
        UiW_GbaTextWrapped(UI_FACE_VT323, 24, 624, 448, 296, 24, UiLocale_Lookup(sBadgeDescs[sBadge]), UI_RGBA(41, 49, 49, 255), UI_GBA_SHAD_LIGHT, 2);
        UiDraw_PopClip();
        return;
    }

    // Tab 1: BACK (Trainer Records on Left + Last HoF Team & Four Island Stickers on Right)
    UiW_GbaWin(14, 60, 572, 472, UI_GBA_WIN_GOLD);
    UiW_GbaBox(30, 76, 540, 42, 4, UI_RGBA(41, 49, 49, 255), UI_RGBA(186, 48, 48, 255), UI_RGBA(134, 24, 32, 255),
               UI_RGBA(238, 124, 118, 140), 0);
    UiW_GbaTextMid(UI_FACE_PS2P, 16, 42, 76, 42, UI_TR("TRAINER RECORDS · BACK"), C_WHITE, UI_GBA_SHAD_DARK);
    snprintf(buf, sizeof(buf), UI_TR("IDNo. %05u"), (unsigned)tid);
    UiW_GbaBox(436, 82, 124, 30, 3, UI_RGBA(16, 24, 24, 255), UI_RGBA(24, 32, 32, 255), UI_RGBA(24, 32, 32, 255), 0, 0);
    UiW_GbaTextCentred(UI_FACE_VT323, 24, 436, 82, 124, 30, buf, C_WHITE, UI_GBA_SHAD_DARK);

    if (cleared)
    {
        const u32 hofTime = GetGameStat(GAME_STAT_FIRST_HOF_PLAY_TIME);
        u32 hh = hofTime >> 16;
        u32 mm = (hofTime >> 8) & 0xFF;
        u32 ss = hofTime & 0xFF;
        if (hh > 999) { hh = 999; mm = 59; ss = 59; }
        snprintf(buf, sizeof(buf), "%u:%02u:%02u", (unsigned)hh, (unsigned)mm, (unsigned)ss);
    }
    else
        snprintf(buf, sizeof(buf), "--:--:--");
    CardRowGba(30, 128, 540, UI_TR("HALL OF FAME DEBUT"), buf, UI_RGBA(49, 49, 49, 255));

    snprintf(buf, sizeof(buf), "W:%u  L:%u",
             StatCapped(GAME_STAT_LINK_BATTLE_WINS, 9999),
             StatCapped(GAME_STAT_LINK_BATTLE_LOSSES, 9999));
    CardRowGba(30, 182, 540, UI_TR("LINK BATTLES"), buf, UI_RGBA(49, 49, 49, 255));

    snprintf(buf, sizeof(buf), "%u", StatCapped(GAME_STAT_POKEMON_TRADES, 0xFFFF));
    CardRowGba(30, 236, 540, UI_TR("POKéMON TRADES"), buf, UI_RGBA(49, 49, 49, 255));

    snprintf(buf, sizeof(buf), "%u", StatCapped(GAME_STAT_NUM_UNION_ROOM_BATTLES, 0xFFFF));
    CardRowGba(30, 290, 540, UI_TR("UNION ROOM BATTLES"), buf, UI_RGBA(49, 49, 49, 255));

    snprintf(buf, sizeof(buf), "%u", StatCapped(GAME_STAT_BERRY_CRUSH_POINTS, 0xFFFF));
    CardRowGba(30, 344, 540, UI_TR("BERRY CRUSH POINTS"), buf, UI_RGBA(158, 34, 40, 255));

    snprintf(buf, sizeof(buf), UI_TR("B:%u  J:%u"),
             (unsigned)gSaveBlock2Ptr->berryPick.berriesPicked,
             (unsigned)gSaveBlock2Ptr->pokeJump.jumpsInRow);
    CardRowGba(30, 398, 540, UI_TR("DODRIO BERRY / JUMP"), buf, UI_RGBA(49, 49, 49, 255));

    UiW_GbaBox(30, 474, 540, 38, 3, UI_RGBA(184, 164, 88, 255), UI_RGBA(232, 220, 158, 255), UI_RGBA(232, 220, 158, 255), C_WHITE, 0);
    UiW_GbaTextMid(UI_FACE_VT323, 24, 42, 474, 38, UI_TR("KANTO TRAINER PASSPORT"), UI_RGBA(92, 70, 18, 255), C_WHITE);

    // Right panel: Hall of Fame Squad & Four Island Stickers (598, 60, 348, 472)
    UiW_GbaWin(598, 60, 348, 472, UI_GBA_WIN_DEFAULT);
    UiW_GbaSectionStrip(614, 74, 316, 26, UI_GBA_TAG_CRIMSON, UI_TR("LAST HOF TEAM"), NULL);

    static const uint16_t kTeamVars[6] = {
        VAR_TRAINER_CARD_MON_ICON_1, VAR_TRAINER_CARD_MON_ICON_2, VAR_TRAINER_CARD_MON_ICON_3,
        VAR_TRAINER_CARD_MON_ICON_4, VAR_TRAINER_CARD_MON_ICON_5, VAR_TRAINER_CARD_MON_ICON_6
    };
    const uint8_t frame = (uint8_t)((sFrame / 16U) & 1U);
    for (int32_t i = 0; i < 6; i++)
    {
        const int32_t sx = 614 + (i % 3) * 108;
        const int32_t sy = 108 + (i / 3) * 78;
        const uint16_t sp = cleared ? MailSpeciesToIconSpecies(VarGet(kTeamVars[i])) : SPECIES_NONE;
        UiW_GbaBox(sx, sy, 100, 72, 4, UI_RGBA(184, 190, 176, 255), UI_RGBA(244, 245, 236, 255), UI_RGBA(244, 245, 236, 255), UI_GBA_SHAD_LIGHT, 0);
        if (sp != SPECIES_NONE && sp < NUM_SPECIES)
            UiSpr_MonIcon(sp, frame, sx + 18, sy + 4, 2);
        else
            UiW_GbaTextCentred(UI_FACE_VT323, 24, sx, sy, 100, 72, "---", UI_RGBA(160, 168, 160, 255), UI_GBA_SHAD_LIGHT);
    }

    UiW_GbaSectionStrip(614, 272, 316, 26, UI_GBA_TAG_GOLD, UI_TR("BRAG STICKERS"), NULL);
    static const char *const kStickerNames[3] = { UI_N("HOF"), UI_N("EGGS"), UI_N("WINS") };
    const int32_t stickerLv[3] = {
        StickerLevel(VAR_HOF_BRAG_STATE, 1, 40, 100),
        StickerLevel(VAR_EGG_BRAG_STATE, 1, 100, 200),
        StickerLevel(VAR_LINK_WIN_BRAG_STATE, 1, 20, 50)
    };
    for (int32_t i = 0; i < 3; i++)
    {
        const int32_t sy = 306 + i * 68;
        const int32_t lv = stickerLv[i];
        UiW_GbaBox(614, sy, 316, 60, 4, lv > 0 ? UI_RGBA(216, 160, 40, 255) : UI_RGBA(184, 190, 176, 255),
                   lv > 0 ? UI_RGBA(255, 250, 224, 255) : UI_RGBA(244, 245, 236, 255),
                   lv > 0 ? UI_RGBA(255, 250, 224, 255) : UI_RGBA(244, 245, 236, 255), UI_GBA_SHAD_LIGHT, 0);
        UiW_GbaTextMid(UI_FACE_PS2P, 16, 628, sy, 60, UiLocale_Lookup(kStickerNames[i]), UI_RGBA(49, 49, 49, 255), UI_GBA_SHAD_LIGHT);
        for (int32_t st = 0; st < 3; st++)
            UiW_GbaStar(850 + st * 22, sy + 24, st < lv ? UI_RGBA(255, 196, 32, 255) : UI_RGBA(200, 204, 196, 255));
    }
}

static void Build(void)
{
    if (gUiTheme->gbaFrame)
    {
        BuildGba();
        return;
    }
    char buf[48], name[16];
    const uint16_t tid = (uint16_t)(gSaveBlock2Ptr->playerTrainerId[0] | (gSaveBlock2Ptr->playerTrainerId[1] << 8));
    const bool cleared = GetGameStat(GAME_STAT_ENTERED_HOF) > 0;
    const int32_t stars = CardStarCount();

    UiKit_Header(UI_TR("TRAINER CARD"), sTabs, TAB_COUNT, sTab, sTabX, sTabW, 12);
    snprintf(buf, sizeof(buf), UI_TR("IDNo. %05u"), (unsigned)tid);
    UiW_HeaderValue(buf, UI_COL_ON_FRAME);
    UiGame_PlayerName(name, sizeof(name));

    if (sTab == TAB_CARD)
    {
        UiW_Panel(16, 60, 440, 432);
        UiW_Inset(32, 76, 152, 152);
        UiSpr_Trainer(UiGame_PlayerIsMale() ? TRAINER_PIC_RED : TRAINER_PIC_LEAF, 44, 88, 2);
        UiText_Draw(UI_FACE_PS2P, 16, 200, 84, name, UI_COL_INK);
        snprintf(buf, sizeof(buf), "$%u", (unsigned)UiGame_Money());
        Row(200, 112, 240, UI_TR("MONEY"), buf, UI_COL_MONEY);
        if (FlagGet(FLAG_SYS_POKEDEX_GET))
            snprintf(buf, sizeof(buf), "%u", (unsigned)UiGame_DexOwned());
        else
            snprintf(buf, sizeof(buf), "-");
        Row(200, 150, 240, UI_TR("POKéDEX"), buf, UI_COL_INK);
        UiGame_PlayTime(buf, sizeof(buf));
        Row(200, 188, 240, UI_TR("TIME"), buf, UI_COL_INK);
        snprintf(buf, sizeof(buf), "%u", (unsigned)UiGame_Coins());
        Row(32, 250, 408, UI_TR("COINS"), buf, UI_COL_INK);
        snprintf(buf, sizeof(buf), "%u", (unsigned)GetGameStat(GAME_STAT_ENTERED_HOF));
        Row(32, 288, 408, UI_TR("HALL OF FAME"), cleared ? buf : "-", UI_COL_INK);
        snprintf(buf, sizeof(buf), "%d / 4", (int)stars);
        Row(32, 326, 408, UI_TR("CARD STARS"), buf, UI_COL_INK);

        UiW_Panel(468, 60, 476, 432);
        snprintf(buf, sizeof(buf), "%u / 8", (unsigned)UiGame_BadgeCount());
        UiW_PanelHead(468, 60, 476, UI_TR("BADGES"), buf);
        for (int32_t i = 0; i < 8; i++)
        {
            const int32_t x = 492 + (i % 4) * 110;
            const int32_t y = 116 + (i / 4) * 92;
            const bool got = FlagGet(FLAG_BADGE01_GET + i);
            UiW_Inset(x, y, 104, 86);
            if (i == sBadge)
                UiDraw_Frame(x, y, 104, 86, 3, UI_COL_SELECT);
            UiDraw_SpriteTint((uint16_t)(UI_SPR_BADGE_FIRST + i), x + 36, y + 8, 2, got ? 0xFFFFFFFFu : UI_RGBA(200, 192, 170, 120));
            UiText_Draw(UI_FACE_VT323, UI_BODY_PX, x + 52 - UiText_Width(UI_FACE_VT323, UI_BODY_PX, got ? UiLocale_Lookup(sBadgeNames[i]) : "-") / 2, y + 46, got ? UiLocale_Lookup(sBadgeNames[i]) : "-", UI_COL_INK);
        }
        if (FlagGet(FLAG_BADGE01_GET + sBadge))
            snprintf(buf, sizeof(buf), UI_TR("%s BADGE · %s"), UiLocale_Lookup(sBadgeNames[sBadge]), UiLocale_Lookup(sLeaderNames[sBadge]));
        else
            snprintf(buf, sizeof(buf), UI_TR("Not obtained yet."));
        UiText_Draw(UI_FACE_VT323, UI_BODY_PX, 492, 316, buf, UI_COL_INK);
    }
    else
    {
        UiW_Panel(16, 60, 440, 432);
        UiW_PanelHead(16, 60, 440, UI_TR("TRAINER RECORDS"), NULL);
        snprintf(buf, sizeof(buf), "W:%u  L:%u",
                 StatCapped(GAME_STAT_LINK_BATTLE_WINS, 9999),
                 StatCapped(GAME_STAT_LINK_BATTLE_LOSSES, 9999));
        Row(32, 110, 408, UI_TR("LINK BATTLES"), buf, UI_COL_INK);
        snprintf(buf, sizeof(buf), "%u", StatCapped(GAME_STAT_POKEMON_TRADES, 0xFFFF));
        Row(32, 150, 408, UI_TR("TRADES"), buf, UI_COL_INK);
        snprintf(buf, sizeof(buf), "%u", StatCapped(GAME_STAT_NUM_UNION_ROOM_BATTLES, 0xFFFF));
        Row(32, 190, 408, UI_TR("UNION ROOM"), buf, UI_COL_INK);
        snprintf(buf, sizeof(buf), "%u", StatCapped(GAME_STAT_BERRY_CRUSH_POINTS, 0xFFFF));
        Row(32, 230, 408, UI_TR("BERRY CRUSH"), buf, UI_COL_INK);
        snprintf(buf, sizeof(buf), "%u / %u",
                 (unsigned)gSaveBlock2Ptr->berryPick.berriesPicked,
                 (unsigned)gSaveBlock2Ptr->pokeJump.jumpsInRow);
        Row(32, 270, 408, UI_TR("BERRY / JUMP"), buf, UI_COL_INK);

        UiW_Panel(468, 60, 476, 432);
        UiW_PanelHead(468, 60, 476, UI_TR("LAST HOF TEAM"), NULL);
        static const uint16_t kTeamVars[6] = {
            VAR_TRAINER_CARD_MON_ICON_1, VAR_TRAINER_CARD_MON_ICON_2, VAR_TRAINER_CARD_MON_ICON_3,
            VAR_TRAINER_CARD_MON_ICON_4, VAR_TRAINER_CARD_MON_ICON_5, VAR_TRAINER_CARD_MON_ICON_6
        };
        const uint8_t frame = (uint8_t)((sFrame / 16U) & 1U);
        for (int32_t i = 0; i < 6; i++)
        {
            const int32_t x = 492 + (i % 3) * 148;
            const int32_t y = 116 + (i / 3) * 110;
            const uint16_t sp = cleared ? MailSpeciesToIconSpecies(VarGet(kTeamVars[i])) : SPECIES_NONE;
            UiW_Inset(x, y, 136, 98);
            if (sp != SPECIES_NONE && sp < NUM_SPECIES)
                UiSpr_MonIcon(sp, frame, x + 36, y + 17, 2);
            else
                UiText_DrawCentred(UI_FACE_VT323, UI_BODY_PX, x, y, 136, 98, "-", UI_COL_INK_SOFT);
        }
    }

    int32_t fx = 16;
    UiW_FooterGlyph(&fx, UI_GLYPH_CROSS, UI_TR("Flip card"));
    UiW_FooterGlyph(&fx, UI_GLYPH_CIRCLE, UI_TR("Back"));
    UiKit_FooterClock();
}

const UiScreen gUiCardScreen = { Enter, Update, Build, NULL };
