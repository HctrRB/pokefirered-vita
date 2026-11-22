// Catalog of the native UI's strings for a non-English build (ui_locale.h).
//
// app0:ui2/locale.bin, written by tools/locales/compile_ui.py (little endian):
//     char magic[4] = "UILC"
//     u16  version = 1, u16 reserved = 0
//     u32  count
//     count x { u32 hash, u32 offset }   sorted by hash; hash = FNV-1a 32 of the English UTF-8 key
//     blob of NUL-terminated UTF-8 translations; offset is from the start of the blob
// The compiler refuses two keys with the same hash, so a hash names one English string.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ui_locale.h"
#include "platform/vita/vita_platform.h"

#ifdef UI_LOCALE_CATALOG

#define LOCALE_REL_PATH "ui2/locale.bin"
#define LOCALE_HEADER_SIZE 12U

typedef struct
{
    uint32_t hash;
    uint32_t offset;
} UiLocaleEntry;

_Static_assert(sizeof(UiLocaleEntry) == 8, "UiLocaleEntry must match the UILC table stride");

static uint8_t *sFile;                      // the whole file, kept for the process lifetime
static const UiLocaleEntry *sEntries;
static const char *sBlob;
static uint32_t sCount;

static uint32_t Rd32(const uint8_t *const p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t Fnv1a(const char *const s)
{
    uint32_t h = 2166136261u;
    for (const uint8_t *p = (const uint8_t *)s; *p != 0U; p++)
        h = (h ^ *p) * 16777619u;
    return h;
}

// Every offset inside the blob, the blob NUL-terminated (so every string ends inside it), and
// hashes strictly increasing (binary search and one entry per hash).
static bool Valid(const uint8_t *const data, const size_t size, const uint32_t count)
{
    if (count > (size - LOCALE_HEADER_SIZE) / 8U)
        return false;
    const size_t blobAt = LOCALE_HEADER_SIZE + (size_t)count * 8U;
    if (blobAt >= size || data[size - 1] != 0U)
        return false;
    const size_t blobSize = size - blobAt;
    for (uint32_t i = 0; i < count; i++)
    {
        const uint8_t *const e = data + LOCALE_HEADER_SIZE + (size_t)i * 8U;
        if (Rd32(e + 4) >= blobSize || (i > 0U && Rd32(e) <= Rd32(e - 8)))
            return false;
    }
    return true;
}

bool UiLocale_Load(void)
{
    if (sFile != NULL)
        return true;
    size_t size = 0;
    uint8_t *const data = VitaPlatform_LoadAsset(LOCALE_REL_PATH, &size);
    if (data == NULL || size < LOCALE_HEADER_SIZE || memcmp(data, "UILC", 4) != 0
        || data[4] != 1U || data[5] != 0U)
    {
        VITA_LOG("[UI2] %s missing or not version 1: the UI stays in English\n", LOCALE_REL_PATH);
        free(data);
        return false;
    }
    const uint32_t count = Rd32(data + 8);
    if (!Valid(data, size, count))
    {
        VITA_LOG("[UI2] %s damaged: the UI stays in English\n", LOCALE_REL_PATH);
        free(data);
        return false;
    }
    // The table sits at file offset 12 (4-byte aligned) and malloc returns aligned memory.
    sFile = data;
    sEntries = (const UiLocaleEntry *)(data + LOCALE_HEADER_SIZE);
    sBlob = (const char *)(data + LOCALE_HEADER_SIZE + (size_t)count * 8U);
    sCount = count;
    VITA_LOG("[UI2] UI catalog: %u strings\n", (unsigned)count);
    return true;
}

const char *UiLocale_Lookup(const char *const english)
{
    if (english == NULL)
        return "";
    if (sCount == 0U)
        return english;
    const uint32_t h = Fnv1a(english);
    uint32_t lo = 0;
    uint32_t hi = sCount;
    while (lo < hi)
    {
        const uint32_t mid = lo + (hi - lo) / 2U;
        if (sEntries[mid].hash < h)
            lo = mid + 1U;
        else
            hi = mid;
    }
    return (lo < sCount && sEntries[lo].hash == h) ? sBlob + sEntries[lo].offset : english;
}

#else

bool UiLocale_Load(void)
{
    return true;                            // English: the strings in the code are the UI's text
}

#endif // UI_LOCALE_CATALOG
