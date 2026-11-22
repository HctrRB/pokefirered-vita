#ifndef GUARD_FOLLOWER_MON_H
#define GUARD_FOLLOWER_MON_H

#include "global.h"

/*
 * HGSS Follower Pokemon System:
 * The party's lead Pokemon follows the player in the overworld (2D and 3D).
 * Uses HeartGold/SoulSilver 8-frame directional sprites and text banks from
 * app0:followers/followers.pak, with fallback to standard Pokemon menu icons
 * and built-in emotional reactions.
 *
 * Implemented as a synthetic object event using a reserved localId and graphicsId,
 * omitted from SaveObjectEvents to maintain 100% save-file compatibility.
 */

#define OBJ_EVENT_GFX_FOLLOWER NUM_OBJ_EVENT_GFX   /* 152: reserved id no map object uses */
#define LOCALID_FOLLOWER       254

struct ObjectEvent;
struct ObjectEventGraphicsInfo;
struct Sprite;

void FollowerMon_Update(void);
const struct ObjectEventGraphicsInfo *FollowerMon_GraphicsInfo(void);
void FollowerMon_BindSprite(struct Sprite *sprite);
bool8 FollowerMon_IsFollower(const struct ObjectEvent *objectEvent);
const u8 *FollowerMon_TalkScript(void);
void FollowerMon_CameraShift(s16 dx, s16 dy);

/* Door warp handlers: follower walks in behind the player before door closes */
void FollowerMon_DoorBegin(void);
bool8 FollowerMon_DoorDone(void);

/* Palette settling check: ensure fade/weather has completed */
bool8 FollowerMon_PalettesSettled(void);

/* Visible wild Pokemon HGSS overworld sprites */
u8 FollowerMon_WildSprite(u32 slot, u16 species, u32 personality, bool8 shiny);
void FollowerMon_WildPrefetch(u16 species);
void FollowerMon_WildFace(struct Sprite *sprite, s8 dx, s8 dy, bool8 walking);
void FollowerMon_WildFree(u32 slot, struct Sprite *sprite);

/* Cleanly releases follower resources on reset/warp */
void FollowerMon_ReleaseAll(void);

#endif // GUARD_FOLLOWER_MON_H
