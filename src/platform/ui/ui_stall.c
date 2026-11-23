#include <stdio.h>
#include "ui_internal.h"
#include "platform/vita/vita_platform.h"

#define STALL_WARN_MS 8.0f

static uint32_t sStallCount;
static uint32_t sLastPubSeq;
static volatile uint32_t sLastDrawnSeq;
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
            const uint32_t drawn = __atomic_load_n(&sLastDrawnSeq, __ATOMIC_RELAXED);
            fprintf(fp, "stall #%u screen=%p tick=%.2f ms pubSeq=%u drawnSeq=%u\n",
                    sStallCount, (const void *)top,
                    lastTickMs, sLastPubSeq, drawn);
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
    __atomic_store_n(&sLastDrawnSeq, drawnSeq, __ATOMIC_RELAXED);
}
