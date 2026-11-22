#include <stdio.h>
#include "ui_internal.h"
#include "platform/vita/vita_platform.h"

#define STALL_WARN_MS 8.0f

static uint32_t sStallCount;
static uint32_t sLastPubSeq;
static float sMaxTickMs;

void UiStall_Tick(const UiScreen *const top, const float lastTickMs)
{
    if (lastTickMs <= sMaxTickMs)
        return;
    sMaxTickMs = lastTickMs;
    if (lastTickMs > STALL_WARN_MS)
    {
        sStallCount++;
        FILE *const fp = fopen(VITA_DATA_DIR "ui_stall.log", "a");
        if (fp != NULL)
        {
            fprintf(fp, "stall #%u screen=%p tick=%.2f ms seq=%u\n",
                    sStallCount, (const void *)top,
                    lastTickMs, sLastPubSeq);
            fclose(fp);
        }
    }
}

void UiStall_Published(const uint32_t seq)
{
    sLastPubSeq = seq;
}

void UiStall_Closed(void)
{
    sMaxTickMs = 0.0f;
}

void UiStall_Render(const uint32_t drawnSeq)
{
    (void)drawnSeq;
}
