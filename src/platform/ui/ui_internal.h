#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

// Shared between the files of src/platform/ui. Coordinates are in the 960x544 logical screen.

#include <stdbool.h>
#include <stdint.h>
#include "ui_locale.h"

#define UI_SCREEN_W 960
#define UI_SCREEN_H 544

// ---------- Colour tokens (0xAABBGGRR as GL reads RGBA bytes) ----------
#define UI_RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))
#ifndef C_WHITE
#define C_WHITE UI_RGBA(255, 255, 255, 255)
#endif

typedef struct
{
    const char *name;
    uint32_t frameA, barTop, barBot, barLine, onFrame, onSoft, title;
    uint32_t paper0, paper1, paper2, paperEdge, paperShade, paperStrip, stripInk, stripSoft;
    uint32_t rowA, rowB, rowLine, ink, inkSoft;
    uint32_t selTop, selBot, selEdge, selInk, selArrow;
    uint32_t tabTop, tabBot, tabEdge, tabHi, tabInk, rail, railEdge;
    uint32_t keyTop, keyBot, keyLight, keyDark, keyInk;
    uint32_t capTop, capBot, capEdge, capHi;
    uint32_t darkTop, darkBot, darkEdge, darkHi;
    uint32_t winTop, winBot, winInk;
    uint32_t chipTop, chipBot, chipEdge, chipInk;
    uint32_t dlgEdge, dlgTop, dlgBot, dlgShade;
    uint32_t btnTop[3], btnBot[3], btnEdge, btnInk;
    uint32_t accent, gold, dim, shadowCol;
    // Battle HUD (F7b-2): healthbox plates, HP label and bar back, command buttons, prompt emphasis
    uint32_t plateTop, plateBot, plateEdge, plateShade, plateRing, lvInk, hpInk, gauge;
    uint32_t cmdTop[4], cmdBot[4], cmdEdge, cmdInk, em;
    uint8_t radius;      // corner size of boxes
    uint8_t selRadius;   // 0: flat selection bar; >0: rounded selection pill
    uint8_t edge;        // outline of paper panels
    uint8_t shadow, shadowR;
    bool chamfer;        // corners cut diagonally instead of rounded
    bool gloss;          // glass highlight on the upper half of bars, keys and buttons
    bool bevel;          // inner highlight / shade lines
    bool stripes;        // diagonal stripes on the full-screen frame
    bool gbaFrame;       // GBA Gen 3 stepped 9-slice window frame and scanlines
    uint8_t faceTitle;
    uint8_t faceBody;
    uint8_t faceSign;
    uint8_t gbaWindowFrame;
} UiTheme;

enum { UI_THEME_GBA = 0, UI_THEME_RED, UI_THEME_EMERALD, UI_THEME_CRYSTAL, UI_THEME_TROPICAL, UI_THEME_ROYAL, UI_THEME_COUNT };
extern const UiTheme *gUiTheme;   // Core 1
void UiTheme_Refresh(void);       // reads "ui_theme" extra at UiDraw_Begin

#define UI_COL_FRAME0_A   (gUiTheme->frameA)
#define UI_COL_FRAME1     (gUiTheme->capBot)
#define UI_COL_FRAME2     (gUiTheme->darkTop)
#define UI_COL_LINE_DARK  (gUiTheme->barLine)
#define UI_COL_ON_FRAME   (gUiTheme->onFrame)
#define UI_COL_ON_SOFT    (gUiTheme->onSoft)
#define UI_COL_TITLE      (gUiTheme->title)
#define UI_COL_GOLD       (gUiTheme->gold)
#define UI_COL_PAPER0     (gUiTheme->paper0)
#define UI_COL_PAPER1     (gUiTheme->paper1)
#define UI_COL_PAPER2     (gUiTheme->paper2)
#define UI_COL_INK        (gUiTheme->ink)
#define UI_COL_INK_SOFT   (gUiTheme->inkSoft)
#define UI_COL_SELECT     (gUiTheme->selBot)
#define UI_COL_SELECT_INK (gUiTheme->selInk)
#define UI_COL_MONEY      UI_RGBA(224, 122, 31, 255)
#define UI_COL_HP_HIGH    UI_RGBA(63, 183, 107, 255)
#define UI_COL_HP_MID     UI_RGBA(232, 183, 48, 255)
#define UI_COL_HP_LOW     UI_RGBA(217, 72, 59, 255)
#define UI_COL_EXP        UI_RGBA(74, 159, 224, 255)
#define UI_COL_BAR_BG     UI_RGBA(44, 58, 51, 255)
#define UI_COL_DIM        (gUiTheme->dim)

// ---------- Draw list (ui_draw.c) ----------
typedef struct
{
    float x, y, u, v;
    uint32_t rgba;
} UiGlVertex;

#define UI_MAX_QUADS 4096

typedef struct
{
    uint32_t vcount;           // vertices (6 per quad)
    uint32_t seq;
    float tickMs;              // Core 1 time spent building this list
    int8_t backdropKind;       // -1 = none, 0..6 = tiled 16x16 GBA backdrop
    UiGlVertex verts[UI_MAX_QUADS * 6];
} UiDrawList;

void UiDraw_Begin(void);
void UiDraw_SetBackdrop(uint8_t bgKind);
bool UiDraw_IsClipped(void);
void UiW_GbaBgPattern(uint8_t bgKind, uint32_t *out16x16);
void UiDraw_Quad2(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t u0, uint32_t v0, uint32_t u1, uint32_t v1, uint32_t top, uint32_t bottom);
void UiDraw_Quad(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t u0, uint32_t v0, uint32_t u1, uint32_t v1, uint32_t rgba);
void UiDraw_Rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba);
void UiDraw_RectV(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t top, uint32_t bottom);
void UiDraw_Frame(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint32_t rgba);
void UiDraw_Sprite(uint16_t id, int32_t x, int32_t y, int32_t scale);
void UiDraw_Poly4(const float *xs, const float *ys, uint32_t rgba);
void UiDraw_Line(float x0, float y0, float x1, float y1, float width, uint32_t rgba);
void UiDraw_Tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgba);
void UiDraw_PushClip(int32_t x, int32_t y, int32_t w, int32_t h);
void UiDraw_PopClip(void);
void UiDraw_Darken(int32_t num, int32_t den);
void UiDraw_Publish(float tickMs);
uint32_t UiDraw_Count(void);
const UiDrawList *UiDraw_Acquire(void);

// ---------- Atlas and text (ui_assets.c) ----------
enum { UI_FACE_PS2P = 0, UI_FACE_VT323 = 1 };
#define UI_BODY_PX   32
#define UI_DIALOG_PX 40
#define UI_FOOT_PX   16
#include "ui_sprite_ids.h"

#define UI_TEX_W 1024
#define UI_TEX_H 2048
#define UI_FIXED_MAX_H 1280
#define UI_CELL 64
#define UI_CELL_COLS (UI_TEX_W / UI_CELL)
#define UI_CELL_COUNT_MIN (UI_CELL_COLS * ((UI_TEX_H - UI_FIXED_MAX_H) / UI_CELL))
#define UI_CELL_COUNT_MAX (UI_CELL_COLS * (UI_TEX_H / UI_CELL))

static inline uint32_t Ui_UnpackTexel(const uint16_t v)
{
    if (v == 0)
        return 0;
    if ((v & 1) == 0)
        return UI_RGBA(255, 255, 255, (v >> 1) & 0xFFu);
    const uint32_t r5 = (v >> 11) & 31u;
    const uint32_t g5 = (v >> 6) & 31u;
    const uint32_t b5 = (v >> 1) & 31u;
    const uint32_t r = (r5 << 3) | (r5 >> 2);
    const uint32_t g = (g5 << 3) | (g5 >> 2);
    const uint32_t b = (b5 << 3) | (b5 >> 2);
    return UI_RGBA(r, g, b, 255);
}

bool UiAssets_Load(void);
void UiAssets_Unload(void);
bool UiAssets_Ready(void);
const uint16_t *UiAssets_Pixels(uint16_t *outW, uint16_t *outH);
uint32_t UiAssets_CellsY(void);
uint32_t UiAssets_CellCount(void);
void UiAssets_TexSize(uint16_t *outW, uint16_t *outH);
void UiAssets_White(uint16_t *x, uint16_t *y);
bool UiAssets_Sprite(uint16_t id, uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h);
extern const char gVitaBuildVersion[];
int32_t UiText_Width(uint8_t face, uint8_t px, const char *s);
int32_t UiText_Draw(uint8_t face, uint8_t px, int32_t x, int32_t y, const char *s, uint32_t rgba);
int32_t UiText_LineHeight(uint8_t face, uint8_t px);
int32_t UiText_CapMid(uint8_t face, uint8_t px);
int32_t UiText_InkWidth(uint8_t face, uint8_t px, const char *s);
int32_t UiText_DrawCentred(uint8_t face, uint8_t px, int32_t boxX, int32_t boxY, int32_t boxW, int32_t boxH, const char *s, uint32_t rgba);
int32_t UiText_DrawMid(uint8_t face, uint8_t px, int32_t x, int32_t boxY, int32_t boxH, const char *s, uint32_t rgba);

// ---------- Run-time sprites (ui_sprites.c) ----------
bool UiSpr_MonIcon(uint16_t species, uint8_t frame, int32_t x, int32_t y, int32_t scale);
bool UiSpr_MonFront(uint16_t species, bool shiny, int32_t x, int32_t y, int32_t scale);
bool UiSpr_ItemIcon(uint16_t itemId, int32_t x, int32_t y, int32_t scale);
bool UiSpr_Footprint(uint16_t species, int32_t x, int32_t y, int32_t scale, uint32_t rgba);
bool UiSpr_Wallpaper(uint8_t wallpaperId, int32_t x, int32_t y, int32_t scale);
bool UiSpr_ZonePlate(uint8_t theme, bool underwater, int32_t x, int32_t y, int32_t w);
bool UiSpr_MailStationery(uint8_t mailType, int32_t x, int32_t y, int32_t scale);
bool UiSpr_Trainer(uint16_t trainerPic, int32_t x, int32_t y, int32_t scale);
bool UiSpr_MonFrontTint(uint16_t species, int32_t x, int32_t y, int32_t scale, uint32_t rgba);
bool UiSpr_MonFrontSized(uint16_t species, int32_t x, int32_t y, int32_t w, int32_t h);
bool UiSpr_TrainerSized(uint16_t pic, int32_t x, int32_t y, int32_t w, int32_t h);
void UiDraw_SpriteTint(uint16_t id, int32_t x, int32_t y, int32_t scale, uint32_t rgba);
void UiSpr_BeginTick(void);
void UiSpr_FlushUploads(void);
uint32_t UiSpr_DecodesLastTick(void);

// ---------- Input (ui_core.c) ----------
typedef struct
{
    uint32_t held, pressed, repeat;
    bool touching, touchDown, tap;
    int32_t tx, ty;
    int32_t downX, downY;
    int32_t dragDy;
    bool dragging;
} UiInput;

static inline bool UiHit(const UiInput *const in, const int32_t x, const int32_t y, const int32_t w, const int32_t h)
{
    return in->tap && in->tx >= x && in->tx < x + w && in->ty >= y && in->ty < y + h;
}

// ---------- Widgets (ui_widgets.c) ----------
enum { UI_GLYPH_CROSS = 0, UI_GLYPH_CIRCLE = 1, UI_GLYPH_TRIANGLE = 2, UI_GLYPH_SQUARE = 3 };
enum { UI_BTN_GREEN = 0, UI_BTN_AMBER = 1, UI_BTN_RED = 2 };

void UiW_FullFrame(void);
void UiW_FooterBar(void);
int32_t UiW_Capsule(int32_t x, int32_t y, int32_t w, int32_t h);
void UiW_HeaderTitle(const char *title);
void UiW_HeaderValue(const char *text, uint32_t colour);
int32_t UiW_Tabs(int32_t x, const char *const *labels, int32_t count, int32_t current, int32_t *tabX, int32_t *tabW);
int32_t UiW_TabsPad(int32_t x, const char *const *labels, int32_t count, int32_t current, int32_t *tabX, int32_t *tabW, int32_t pad);
int32_t UiW_TextWrapped(int32_t x, int32_t y, int32_t w, int32_t lineH, const char *text, uint32_t colour, int32_t maxLines);
int32_t UiW_TextWrappedF(uint8_t face, uint8_t px, int32_t x, int32_t y, int32_t w, int32_t lineH, const char *text, uint32_t colour, int32_t maxLines);
void UiW_FooterGlyph(int32_t *x, uint8_t glyph, const char *label);
void UiW_FooterKey(int32_t *x, const char *key, const char *label);
void UiW_FooterClock(const char *clock);
void UiW_GbaPageStart(int32_t valueX);
void UiW_GbaPageFlush(void);
void UiW_GbaPageReset(void);
void UiW_Glyph(int32_t x, int32_t y, uint8_t glyph);
void UiW_Panel(int32_t x, int32_t y, int32_t w, int32_t h);
void UiW_DialogBox(int32_t x, int32_t y, int32_t w, int32_t h);
void UiW_ThemeBox(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint32_t edge, uint32_t top, uint32_t bottom,
                  uint32_t hi, uint32_t shade, int32_t shadow, bool pill, uint32_t ring);
void UiW_Bevel(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint32_t edge, uint32_t top, uint32_t bottom,
               int32_t b, uint32_t hi, uint32_t shade, int32_t shadow);
void UiW_Inset(int32_t x, int32_t y, int32_t w, int32_t h);
void UiW_PanelDark(int32_t x, int32_t y, int32_t w, int32_t h);
void UiW_PanelHead(int32_t x, int32_t y, int32_t w, const char *label, const char *right);
void UiW_Window(int32_t x, int32_t y, int32_t w, int32_t h, const char *title);
void UiW_Row(int32_t x, int32_t y, int32_t w, int32_t h, bool selected, int32_t index);
int32_t UiW_Chip(int32_t x, int32_t y, const char *label, bool amber);
void UiW_Bar(int32_t x, int32_t y, int32_t w, int32_t h, int32_t percent, uint32_t top, uint32_t bottom);
void UiW_Button(int32_t x, int32_t y, int32_t w, const char *label, uint8_t colour, bool pressed);
void UiW_Strip3(int32_t x, int32_t y, int32_t w, const char *const labels[3], const char *const values[3]);
void UiW_Arrow(int32_t x, int32_t y, uint32_t colour);
void UiW_Stepper(int32_t x, int32_t y, int32_t value, int32_t *minusX, int32_t *plusX);
void UiW_ScrollBar(int32_t x, int32_t y, int32_t h, int32_t first, int32_t visible, int32_t total);
void UiW_Toast(const char *text);

enum UiGbaBgKind
{
    UI_GBA_BG_PARTY = 0,
    UI_GBA_BG_BAG,
    UI_GBA_BG_SUMMARY,
    UI_GBA_BG_POKEDEX,
    UI_GBA_BG_POKENAV,
    UI_GBA_BG_CARD,
    UI_GBA_BG_OPTIONS,
};
enum UiGbaWinKind
{
    UI_GBA_WIN_DEFAULT = 0,
    UI_GBA_WIN_GOLD,
    UI_GBA_WIN_LAVENDER,
    UI_GBA_WIN_DARK,
};
enum UiGbaTagColor
{
    UI_GBA_TAG_BLUE = 0,
    UI_GBA_TAG_TEAL,
    UI_GBA_TAG_GOLD,
    UI_GBA_TAG_GREEN,
    UI_GBA_TAG_MAGENTA,
    UI_GBA_TAG_CRIMSON,
    UI_GBA_TAG_SLATE,
};

#define UI_GBA_SHAD_LIGHT UI_RGBA(213, 213, 205, 255)
#define UI_GBA_SHAD_DARK  UI_RGBA(20, 26, 28, 255)

void UiW_GbaTopBar(uint8_t bgKind, uint16_t iconSpr, const char *secTitle, int32_t dotCount, int32_t dotActive, bool dotAllGreen, const char *pagerText);
#define UI_GBA_PAGER_W_MIN 160
#define UI_GBA_PAGER_W_MAX 304
void UiW_GbaTopBarPages(uint8_t bgKind, uint16_t iconSpr, const char *secTitle, const char *const *pages, int32_t count, int32_t cur);
int32_t UiW_GbaPagerWidth(const char *const *pages, int32_t count, int32_t dotCount);
int32_t UiW_GbaPagerLastWidth(void);
int32_t UiW_GbaPagerHit(const UiInput *in, bool hasSecPill, int32_t dotCount);
void UiW_GbaHelpBadge(int32_t *rx, const char *key, const char *action, bool gold);
void UiW_GbaHelpBadgeY(int32_t *rx, int32_t y, const char *key, const char *action, bool gold);
void UiW_GbaKeyCap(int32_t x, int32_t y, int32_t h, const char *key, bool gold);
int32_t UiW_GbaKeyCapWidth(const char *key, int32_t h);
void UiW_GbaWin(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t kind);
void UiW_GbaMsg(int32_t x, int32_t y, int32_t w, int32_t h, bool battleDark, bool redArrow);
void UiW_GbaBox(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t border,
                uint32_t top, uint32_t bot, uint32_t innerTop, int32_t shadowY);
void UiW_GbaRingBox(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint32_t outerRing,
                    uint32_t innerRing, uint32_t top, uint32_t bot, uint32_t innerHi, int32_t shadowY);
int32_t UiW_GbaText(uint8_t face, uint8_t px, int32_t x, int32_t y, const char *utf8, uint32_t rgba, uint32_t shadowRgba);
int32_t UiW_GbaTextMid(uint8_t face, uint8_t px, int32_t x, int32_t boxY, int32_t boxH, const char *utf8, uint32_t rgba, uint32_t shadowRgba);
void UiW_GbaTextCentred(uint8_t face, uint8_t px, int32_t boxX, int32_t boxY, int32_t boxW, int32_t boxH, const char *utf8, uint32_t rgba, uint32_t shadowRgba);
int32_t UiW_GbaTextWrapped(uint8_t face, uint8_t px, int32_t x, int32_t y, int32_t maxW, int32_t lineStep, const char *utf8, uint32_t rgba, uint32_t shadowRgba, int32_t maxLines);
void UiW_GbaSectionStrip(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t tagKind, const char *label, const char *rightSub);
void UiW_GbaHpBar(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t hp, uint16_t maxHp);
void UiW_GbaExpBar(int32_t x, int32_t y, int32_t w, int32_t pct, const char *nextText);
void UiW_GbaArrowH(int32_t x, int32_t y, bool right, uint32_t colour);
void UiW_GbaArrowV(int32_t x, int32_t y, bool down, uint32_t colour);
void UiW_GbaStar(int32_t x, int32_t y, uint32_t colour);
void UiW_GbaMark(int32_t x, int32_t y, uint8_t shape, uint32_t colour);

// ---------- Screen kit (ui_kit.c) ----------
void UiKit_Toast(const char *fmt, ...);
void UiKit_DrawToast(void);
void UiKit_ClearToast(void);

typedef struct { int32_t sel, first, count, rows, dragPx; } UiList;
void UiList_Reset(UiList *l, int32_t count, int32_t visibleRows);
int32_t UiList_Update(UiList *l, const UiInput *in, int32_t x, int32_t y, int32_t w, int32_t rowH, bool wrap);

#define UI_MENU_MAX 8
#define UI_MENU_NONE (-1)
#define UI_MENU_CANCEL (-2)
typedef struct
{
    bool open;
    int32_t count, x, y, w;
    char title[24];
    char labels[UI_MENU_MAX][24];
    int32_t ids[UI_MENU_MAX];
    UiList list;
} UiMenu;
void UiMenu_Open(UiMenu *m, const char *title, int32_t x, int32_t y, int32_t w);
void UiMenu_Add(UiMenu *m, const char *label, int32_t id);
int32_t UiMenu_Update(UiMenu *m, const UiInput *in);
void UiMenu_Draw(const UiMenu *m);

typedef struct { bool open; int32_t value, max, minusX, plusX, okX, stepY; char title[24]; } UiQty;
void UiQty_Open(UiQty *q, const char *title, int32_t max);
int32_t UiQty_Update(UiQty *q, const UiInput *in);
void UiQty_Draw(UiQty *q, uint16_t itemId);
int32_t UiKit_Header(const char *title, const char *const *tabs, int32_t n, int32_t cur, int32_t *tabX, int32_t *tabW, int32_t pad);
int32_t UiKit_HeaderAs(uint8_t gbaBg, uint16_t gbaIcon, const char *title, const char *const *tabs, int32_t n, int32_t cur,
                       int32_t *tabX, int32_t *tabW, int32_t pad);
int32_t UiKit_TabHit(const UiInput *in, const int32_t *tabX, const int32_t *tabW, int32_t n);
void UiKit_FooterClock(void);
int32_t UiKit_GenderShiny(int32_t x, int32_t y, uint8_t gender, bool shiny);
void UiKit_HeaderMoney(void);

// ---------- Screens ----------
typedef struct
{
    void (*enter)(void);
    bool (*update)(const UiInput *in);
    void (*build)(void);
    void (*resume)(void);
} UiScreen;

void UiNav_Push(const UiScreen *screen);
void UiNav_Replace(const UiScreen *screen);
void UiNav_Pop(void);
void UiNav_Close(void (*afterClose)(void));
void UiCore_OpenScreen(const UiScreen *screen);
void UiCore_CloseToScene(void (*start)(void));
void UiCore_CloseToSceneInWindow(void (*start)(void), const char *title);
void UiCore_ReopenAfterField(void (*prepare)(void));
bool UiCore_TryRestoreAfterScene(void);
void UiCore_OpenHosted(const UiScreen *screen, void (*afterClose)(void));
bool UiCore_IsHostCallback(void);
void UiCore_OpenFromScript(const UiScreen *screen, void (*afterClose)(void));

bool UiSign_Draw(void);
void UiSign_Reset(void);
bool UiMirror_Draw(void);

extern const UiScreen gUiHubScreen;
extern const UiScreen gUiOptionsScreen;
extern const UiScreen gUiNuzlockeScreen;
extern const UiScreen gUiPartyScreen;
extern const UiScreen gUiCardScreen;
extern const UiScreen gUiBagScreen;
extern const UiScreen gUiTargetScreen;
extern const UiScreen gUiBerryScreen;
extern const UiScreen gUiMartScreen;
extern const UiScreen gUiDexScreen;
extern const UiScreen gUiDexDetailScreen;
extern const UiScreen gUiMapScreen;
extern const UiScreen gUiFlyScreen;
extern const UiScreen gUiChooseScreen;
extern const UiScreen gUiForgetMoveScreen;
extern const UiScreen gUiPcScreen;
extern const UiScreen gUiPlayerPcScreen;
extern const UiScreen gUiMailReadScreen;
extern const UiScreen gUiNamingScreen;
extern const UiScreen gUiEasyChatScreen;
extern volatile bool gUiVoxelFrame;

struct Mail;
void UiMailRead_Set(const struct Mail *mail);
void UiMailRead_Open(const struct Mail *mail, const char *header);
void UiMail_Draw(const struct Mail *mail, int32_t x, int32_t y, int32_t scale);

enum { UI_TARGET_USE = 0, UI_TARGET_GIVE, UI_TARGET_TM, UI_TARGET_BATTLE };
void UiTarget_Set(int32_t mode, uint16_t itemId);
void UiBag_SetGiveTo(int32_t partySlot);
void UiFly_Set(uint8_t partySlot);
void UiMap_DrawArea(uint8_t region, const uint8_t *areas, uint32_t frame);
void UiMap_OpenFromBag(void);

void UiParty_PushSummary(int32_t slot);
int32_t UiParty_LastViewed(void);

float UiCore_LastRenderMs(void);
float UiCore_AvgTickMs(void);
void UiCore_SetLastRenderMs(float ms);

void UiStall_Tick(const UiScreen *top, float lastTickMs);
void UiStall_Published(uint32_t seq);
void UiStall_Closed(void);
void UiStall_Render(uint32_t drawnSeq);

void UiMirror_Inject(uint16_t keys);
bool UiMirror_Tap(int32_t *x, int32_t *y);
void UiMirror_VisibleLines(const uint8_t *start, const uint8_t *end, char out[2][160]);

// Battle HUD (F7b-2)
bool UiBattle_Active(void);
void UiBattle_Tick(void);
bool UiBattle_Draw(void);
void UiBattle_Reset(void);

#endif // UI_INTERNAL_H
