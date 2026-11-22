/*
 * fr_vita_ui.h - the FireRed 3DS touch UI (src/platform/fr3ds/fr_bottom_ui.c) on the Vita.
 *
 * The 3DS shows that UI on its bottom screen. The Vita has one screen, so the UI's 320x240
 * canvas is drawn over the game at 2x (640x480, centred), with the pages' background colour
 * translucent so the world shows through. It is shown when the player opens it (START in the
 * field or in a battle) and whenever the UI takes the game's menu (a native page, the zoomed
 * region map, or a game menu whose picture the UI frames).
 *
 * Threads: FrVitaUi_Init before the game thread starts; FrVitaUi_ScanInput,
 * FrVitaUi_FilterGameKeys, FrVitaUi_AfterDraw and FrVitaUi_FrameDone on the game thread
 * (Core 1); FrVitaUi_InitVideo and FrVitaUi_Render on Core 0.
 *
 * Origin: written for this port. Licence: MIT.
 */
#ifndef GUARD_FR_VITA_UI_H
#define GUARD_FR_VITA_UI_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Loads the settings and initialises the UI. Main thread, before the game thread. */
void FrVitaUi_Init(void);

/** @brief Creates the canvas texture. Core 0, after vitaGL is up. */
void FrVitaUi_InitVideo(void);

/** @brief Reads buttons and touch for this frame and runs the UI's frame. Core 1, once per
 * frame, after the frame was drawn and before the game reads its keys. */
void FrVitaUi_ScanInput(void);

/** @brief The game keys of this frame, after the UI took what it uses (START that opens or
 * closes it, the field's keys while it covers the field) and added the keys it presses. */
uint16_t FrVitaUi_GameKeys(void);

/** @brief Whether Triangle (fast forward) is held this frame. */
bool FrVitaUi_FastForwardHeld(void);

/** @brief Updates whether the touch overlay is visible for Core 0. Core 1. */
void FrVitaUi_SyncShown(void);

/** @brief Whether the touch overlay is currently visible to Core 0 (lock-free atomic read). */
bool FrVitaUi_IsShown(void);

/** @brief Counts a game frame (the UI times its pages in frames). Core 1. */
void FrVitaUi_FrameDone(void);

/** @brief A soft reset happened (the UI goes back to its title). Core 1. */
void FrVitaUi_NoteReset(void);

/** @brief Draws the UI over the frame just drawn, if it is shown. Core 0. */
void FrVitaUi_Render(void);

#endif // GUARD_FR_VITA_UI_H
