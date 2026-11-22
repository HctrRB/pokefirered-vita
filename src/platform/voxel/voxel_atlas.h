/*
 * Metatile atlas for the voxel overworld.
 *
 * Packed 512x256 pages per tileset pair, with 504 metatiles and eight solid
 * slots per page. Pages are allocated lazily from a shared VRAM budget. The
 * id-to-slot table preserves assignments when a map needs more materials.
 * Composition matches the reference's two layers of four 8x8 subtiles;
 * see NOTICE.md for the reference renderer's MIT attribution.
 */
#ifndef CTR_VOXEL_ATLAS_H
#define CTR_VOXEL_ATLAS_H

#include <stdbool.h>
#include <stdint.h>

#include "voxel_world.h"

#define VOXEL_ATLAS_W         512u
#define VOXEL_ATLAS_H         256u
#define VOXEL_ATLAS_SLOT      16u
#define VOXEL_ATLAS_COLUMNS   (VOXEL_ATLAS_W / VOXEL_ATLAS_SLOT)
#define VOXEL_ATLAS_ROWS      (VOXEL_ATLAS_H / VOXEL_ATLAS_SLOT)
#define VOXEL_ATLAS_MAX_SLOTS (VOXEL_ATLAS_COLUMNS * VOXEL_ATLAS_ROWS)
#define VOXEL_ATLAS_PIXELS    (VOXEL_ATLAS_W * VOXEL_ATLAS_H)

/*
 * The reference draws furniture legs, table sides and television bodies as
 * untextured `glColor3f` geometry. The last few atlas slots hold those flat
 * colours so textured faces can sample them directly.
 */
typedef enum
{
    VOXEL_SOLID_WOOD = 0,   /* table sides */
    VOXEL_SOLID_WOOD_DARK,  /* table legs */
    VOXEL_SOLID_GREY,       /* television stand */
    VOXEL_SOLID_BLACK,      /* television body */
    VOXEL_SOLID_SHEET,      /* bedding */
    VOXEL_SOLID_COUNT
} VoxelSolidColor;

#define VOXEL_SOLID_SLOTS 8u
#define VOXEL_SOLID_BASE  (VOXEL_ATLAS_MAX_SLOTS - VOXEL_SOLID_SLOTS)

/* Texture coordinates of one of the flat colours. */
void VoxelAtlas_SolidUV(VoxelSolidColor color, float *u0, float *v0, float *u1, float *v1);

/*
 * One entry per metatile id. 0 means this atlas has never seen the id, which
 * is the renderer's cue that a script placed something new and the atlas needs
 * rebuilding once. VOXEL_SLOT_ABSENT means it was seen and has no pixels.
 * VOXEL_SLOT_PENDING requests a retry; other entries are page*512 + slot + 1.
 */
#define VOXEL_METATILE_REAL 1024u
/*
 * After the real ids, the ground variants (voxel_building.h): a metatile less
 * the quarters of its upper layer a model stands for - the water round a rock
 * in the sea, without the rock. Id VOXEL_METATILE_REAL + i is variant i of
 * buildings.bin; it is composed, extended and animated like any other id.
 */
#define VOXEL_VARIANTS 160u
/*
 * Then the cut tiles (voxel_relief.h): a rock's tile with the ground drawn
 * behind it clear, for its relief to stand over the ground drawn flat. Id
 * VOXEL_CUT_FIRST + i is cut variant i of relief.bin.
 */
#define VOXEL_CUTS 512u
#define VOXEL_CUT_FIRST (VOXEL_METATILE_REAL + VOXEL_VARIANTS)
/*
 * Then the bare tiles: id VOXEL_BARE_FIRST + m is metatile m without its
 * upper layer - the ground under a standing prop or wood card (voxel_tree.c),
 * so it is not drawn twice. Composed only where a map asks for one.
 */
#define VOXEL_BARE_FIRST   (VOXEL_CUT_FIRST + VOXEL_CUTS)
#define VOXEL_METATILE_IDS (VOXEL_BARE_FIRST + VOXEL_METATILE_REAL)
#define VOXEL_SLOT_ABSENT  0xFFFFu
#define VOXEL_SLOT_PENDING 0xFFFEu
/* Capped at 4 pages to match VOXEL_MAX_ATLAS_PAGES (2016 composed slots per pair). */
#define VOXEL_ATLAS_PAGES  4u

#define VOXEL_ATLAS_MAX_ANIM_METATILES 128u

typedef struct VoxelAtlasMap
{
    uint16_t slotOf[VOXEL_METATILE_IDS];
    uint8_t tileMask[VOXEL_ATLAS_PAGES][128]; /* source tiles referenced per page */
    uint16_t animMetatiles[VOXEL_ATLAS_PAGES][VOXEL_ATLAS_MAX_ANIM_METATILES];
    uint8_t animMetatileCount[VOXEL_ATLAS_PAGES];
    uint32_t used;      /* slots actually composed */
    bool overflowed;    /* the map referenced more ids than the atlas holds */
} VoxelAtlasMap;

bool VoxelAtlas_Build(const VoxelMapInstance *inst, uint16_t *dest, VoxelAtlasMap *map,
                      bool extend);

typedef struct
{
    bool active, extend, ok;
    const void *primary, *secondary;
    int32_t mapGroup, mapNum;
    uint16_t *dest;
    VoxelAtlasMap map;
    uint8_t used[VOXEL_METATILE_IDS];
    uint8_t *primaryTiles, *secondaryTiles;
    bool primaryLoaded, secondaryLoaded;
    VoxelTileLoad tileLoad;
    uint32_t phase, next, priority;
    uint32_t added;     /* ids given a slot of their own by this job */
    uint32_t page;      /* current staging page; slotOf encodes page*512+slot+1 */
    bool pageReady;     /* CPU must not touch dest until its GPU upload retires */
} VoxelAtlasJob;

/* False with job->ok means base already covers the request: no buffers/upload. */
bool VoxelAtlas_JobBegin(VoxelAtlasJob *job, const VoxelMapInstance *inst, uint16_t *dest,
                         const VoxelAtlasMap *base);
bool VoxelAtlas_JobStep(VoxelAtlasJob *job, uint32_t metatiles);
void VoxelAtlas_JobCancel(VoxelAtlasJob *job);
uint32_t VoxelAtlas_PageCount(const VoxelAtlasMap *map);
bool VoxelAtlas_JobNextPage(VoxelAtlasJob *job);

uint32_t VoxelAtlas_RefreshAnimated(const VoxelMapInstance *inst, const VoxelAtlasMap *map,
                                    uint16_t *dest, const uint8_t *liveTiles,
                                    const uint8_t dirtyTiles[128]);
uint32_t VoxelAtlas_RefreshAnimatedPage(const VoxelMapInstance *inst, const VoxelAtlasMap *map,
                                        uint32_t page, uint16_t *dest, const uint8_t *liveTiles,
                                        const uint8_t dirtyTiles[128]);

#define VOXEL_ATLAS_ROW_BYTES (VOXEL_ATLAS_SLOT * VOXEL_ATLAS_W * 2u)
uint32_t VoxelAtlas_RefreshAnimatedPageRows(const VoxelMapInstance *inst, const VoxelAtlasMap *map,
                                            uint32_t page, uint16_t *dest, const uint8_t *liveTiles,
                                            const uint8_t dirtyTiles[128], uint32_t *rows);
uint32_t VoxelAtlas_RefreshAnimatedPrimaryRows(const void *primaryTileset, const void *secondaryTileset,
                                               const VoxelAtlasMap *map, uint32_t page, uint16_t *dest,
                                               const uint8_t *liveTiles, const uint8_t dirtyTiles[128],
                                               uint32_t *rows);

bool VoxelAtlas_IsVoid(const VoxelMapInstance *inst, int32_t metatile);

#define VOXEL_ANIM_PAIRS   8u
#define VOXEL_ANIM_TARGETS 1024u
#define VOXEL_ANIM_BLOCKS  1024u
#define VOXEL_ANIM_HASH    2048u
#define VOXEL_ANIM_RUN_MAX VOXEL_ATLAS_COLUMNS

typedef struct
{
    uint16_t slot;      /* within its page */
    uint16_t id;        /* atlas id */
    uint8_t pair, page;
    uint16_t block[4];  /* its quadrants among the pass's blocks */
} VoxelAnimTarget;

typedef struct
{
    uint16_t lower, upper;  /* the quadrant's entries; a cut slot's id in `lower` */
    uint8_t pair;           /* the pair it is composed from */
    uint8_t flags;
} VoxelAnimBlock;

typedef struct
{
    uint8_t work[128];
    const void *primary[VOXEL_ANIM_PAIRS], *secondary[VOXEL_ANIM_PAIRS];
    bool shared[VOXEL_ANIM_PAIRS];
    VoxelAnimTarget target[VOXEL_ANIM_TARGETS];
    uint32_t targets;
    VoxelAnimBlock block[VOXEL_ANIM_BLOCKS];
    uint16_t pixels[VOXEL_ANIM_BLOCKS][64];
    uint16_t (*out)[64];    /* the composed blocks: `pixels`, or a kept state */
    uint16_t hash[VOXEL_ANIM_HASH];
    uint32_t key[2];        /* the written tiles' content the pass composes */
    int32_t state;          /* the kept state it composes into, or -1 */
    bool kept;              /* taken from a kept state, not composed */
    uint32_t blocks, composed;
    bool full;              /* slots left out: they wait for the next write */
} VoxelAnimPass;

void VoxelAnim_Begin(VoxelAnimPass *pass, const uint8_t work[128]);
void VoxelAnim_Again(VoxelAnimPass *pass);
bool VoxelAnim_AddAtlas(VoxelAnimPass *pass, uint32_t pair, const VoxelMapInstance *tilesets,
                        bool shared, const VoxelAtlasMap *map, const uint8_t *liveTiles);
bool VoxelAnim_Compose(VoxelAnimPass *pass, const uint8_t *liveTiles,
                       bool (*stillTime)(void));
uint32_t VoxelAnim_Run(const VoxelAnimPass *pass, uint32_t first);
void VoxelAnim_WriteRun(const VoxelAnimPass *pass, uint32_t first, uint32_t count, uint16_t *out);
uint32_t VoxelAnim_RunOffset(const VoxelAnimPass *pass, uint32_t first);

void VoxelAtlas_SlotUV(uint32_t slot, float *u0, float *v0, float *u1, float *v1);

#endif
