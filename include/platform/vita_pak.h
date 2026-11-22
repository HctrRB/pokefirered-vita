#ifndef GUARD_PLATFORM_VITA_PAK_H
#define GUARD_PLATFORM_VITA_PAK_H

#include <stdbool.h>
#include <stdint.h>

bool VitaPak_FollowerExists(const char *path);
void *VitaPak_LoadFollower(const char *path, uint32_t *outSize);
void VitaPak_Shutdown(void);

#endif // GUARD_PLATFORM_VITA_PAK_H
