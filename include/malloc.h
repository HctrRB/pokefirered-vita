#ifndef GUARD_MALLOC_H
#define GUARD_MALLOC_H

#include "global.h"

#define HEAP_SIZE 0x1C000
// The game's own code allocates from gHeap under the C library's names. The
// 3DS backend (3ds_port/src, compiled with CTR_NATIVE_MALLOC) includes game
// headers but must keep the real allocator: its caches would otherwise be
// carved out of the game's heap, before the game has even set it up.
#ifndef CTR_NATIVE_MALLOC
#define malloc Alloc
#define calloc(ct, sz) AllocZeroed((ct) * (sz))
#define free Free
#endif

#define FREE_AND_SET_NULL(ptr)          \
{                                       \
    free(ptr);                          \
    ptr = NULL;                         \
}

#define TRY_FREE_AND_SET_NULL(ptr) if (ptr != NULL) FREE_AND_SET_NULL(ptr)

extern u8 gHeap[];
void *Alloc(u32 size);
void *AllocZeroed(u32 size);
void Free(void *pointer);
void InitHeap(void *pointer, u32 size);

#endif // GUARD_MALLOC_H
