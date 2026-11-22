/*
 * The sprite atlas's slots (voxel_entities.c), the part of it that needs
 * nothing of the game or the SDK, so a host test can check it on its own.
 *
 * Origin: derived from pokeemerald-vita-3ds (src/platform/voxel/voxel_sprite_slots.h,
 * MIT); see NOTICE.md.
 */
#ifndef CTR_VOXEL_SPRITE_SLOTS_H
#define CTR_VOXEL_SPRITE_SLOTS_H

#include <stdbool.h>
#include <stdint.h>

/* Texels across a slot that a picture `width` wide takes: itself up to the
 * slot's width, half of it (every second column) up to twice that, else 0
 * (it does not fit). */
uint32_t VoxelSlots_AtlasWidth(uint32_t width, uint32_t slotDim);

/* Whether a picture fits a slot: at most twice the slot's width, at most its
 * height, whole tiles, and its source tiles in `maxBytes`. */
bool VoxelSlots_Fits(uint32_t width, uint32_t height, bool color256,
                     uint32_t slotDim, uint32_t maxBytes);

/* The picture's column that atlas column `atlasX` of `atlasWidth` shows, the
 * picture mirrored or not. */
uint32_t VoxelSlots_SourceColumn(uint32_t atlasX, uint32_t atlasWidth, uint32_t width, bool flipX);

/* The colour index at (x, y) of a picture `width` wide held as whole 8x8
 * tiles in rows (1D mapping), 4bpp or 8bpp. */
uint32_t VoxelSlots_TilePixel(const uint8_t *tiles, uint32_t width, uint32_t x, uint32_t y,
                              bool color256);

/*
 * A door's frame as field_door.c lays it (DrawCurrentDoorAnimFrame): a
 * column of two metatiles a cell across, the top one's four 4bpp tiles (TL TR
 * BL BR) then the bottom one's, 8 tiles a column and the columns one after
 * the other; the column's tile i in palette palettes[i]. The colour index at
 * (x, y) of its picture, 16 pixels a column across and 32 down, and in
 * `*palette` that pixel's palette.
 */
uint32_t VoxelSlots_DoorPixel(const uint8_t *tiles, const uint8_t *palettes,
                              uint32_t x, uint32_t y, uint32_t *palette);

/*
 * Where row `v` of a door's drawing goes against its wall at depth `wall`
 * (world tiles, south +z), by the GBA's 45-degree rule that put every model
 * where its drawing is: a row north of the wall stands up it, `wall - v`
 * high; one south of it lies on the ground in front, at its own depth.
 */
void VoxelSlots_DoorRow(float v, float wall, float *height, float *depth);

/* Lower first. */
enum
{
    VOXEL_SLOT_PRIORITY_WILD = 0,      /* a visible wild Pokemon */
    VOXEL_SLOT_PRIORITY_EFFECT = 1,    /* any other field effect */
    VOXEL_SLOT_PRIORITY_DECOR = 2      /* grass rustling on its tile */
};

typedef struct
{
    uint8_t id;              /* the sprite, < 255 */
    uint8_t priority;
    bool inRange;            /* the camera can see it: only then does it take a slot */
    int32_t slot;            /* out: its slot, or -1 */
} VoxelSlotRequest;

/*
 * Gives `requests` (in any order) the slots of `slots` that `claimed` leaves:
 * as many as there are, in priority order, ties by their order in the array.
 * `owner[s]` is the id + 1 of the sprite slot s showed (0: none); a request
 * still wanted keeps its slot, one no longer wanted lets it go, and a new one
 * takes a free slot, from `firstShared` on before one under it. Every slot
 * whose owner changes is set in `reset` (its picture must be decoded again);
 * every slot given out is set in `claimed`. Returns how many in-range
 * requests were left without one.
 */
uint32_t VoxelSlots_Assign(VoxelSlotRequest *requests, uint32_t count, uint8_t *owner,
                           bool *claimed, bool *reset, uint32_t slots, uint32_t firstShared);

/*
 * How far a card whose feet stand at depth `cz` comes towards the camera to
 * be `clear` in front of a model's face at depth `front` (world tiles, south
 * is +z): a walker's card before furniture (voxel_entities.c, ModelPush).
 */
bool VoxelSlots_ModelFacePush(float front, float cz, float clear, float maxFace, float *push);

#endif
