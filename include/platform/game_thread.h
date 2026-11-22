#ifndef GUARD_PLATFORM_GAME_THREAD_H
#define GUARD_PLATFORM_GAME_THREAD_H

/*
 * The game runs on one thread: AgbMain, on Core 1 (docs/reference/THREADS.md). The other cores see it
 * only through the game frame (include/platform/game_frame.h), and tools/core_boundary checks it on
 * every build.
 *
 * This is the run-time tripwire behind that check for indirect/callback paths: mutable state primitives
 * (the Pokemon data cipher, flags, vars) call GAME_THREAD_ONLY(), logging any off-Core-1 caller once
 * per call site to ux0:data/FRVITA/thread_violations.log. Before AgbMain starts nothing is checked.
 */

#include <stdint.h>

// Must match the game thread stack size in SDL_CreateThreadWithStackSize (vita_main.c).
#define GAME_THREAD_STACK_SIZE    (4 * 1024 * 1024)
#define GAME_THREAD_STACK_CHECKED (GAME_THREAD_STACK_SIZE - 64 * 1024)

extern volatile uintptr_t gGameThreadStackTop;   // 0 until the game thread registers

void GameThread_Register(void);
void GameThread_Violation(const void *caller, const char *what);

static inline uintptr_t GameThread_StackPointer(void)
{
    uintptr_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

#define GAME_THREAD_ONLY()                                                                  \
    do                                                                                      \
    {                                                                                       \
        const uintptr_t top_ = gGameThreadStackTop;                                         \
        if (__builtin_expect(top_ != 0                                                      \
                             && top_ - GameThread_StackPointer() >= GAME_THREAD_STACK_CHECKED, 0)) \
            GameThread_Violation(__builtin_return_address(0),                               \
                                 "game state changed off the game thread");                 \
    } while (0)

#endif // GUARD_PLATFORM_GAME_THREAD_H
