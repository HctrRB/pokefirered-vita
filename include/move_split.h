#ifndef GUARD_MOVE_SPLIT_H
#define GUARD_MOVE_SPLIT_H

/* The physical/special split (3DS port, ENHANCEMENTS > MOVE SPLIT): see
 * src/move_split.c. Battle only; nothing is saved. */

#define MOVE_CATEGORY_PHYSICAL 0
#define MOVE_CATEGORY_SPECIAL  1
#define MOVE_CATEGORY_STATUS   2

bool8 MoveSplit_Modern(void);
u8 MoveSplit_Category(u16 move);
bool8 MoveSplit_IsPhysical(u8 battlerAtk, u16 move, u8 type);
bool8 MoveSplit_IsSpecial(u8 battlerAtk, u16 move, u8 type);

#endif // GUARD_MOVE_SPLIT_H
