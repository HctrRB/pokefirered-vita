// Text of the native UI in the build's language (docs/guides/ADDING_A_LANGUAGE.md, docs/reference/VITA_UI.md).
//
// Every string the UI shows is written in English in the code and goes through UI_TR (a literal)
// or UiLocale_Lookup (a string kept in a table, marked with UI_N for the extractor). The English
// text is the key of the build's catalog, app0:ui2/locale.bin, compiled from
// tools/locales/<code>/ui.json. The English build has no catalog: both return their argument and
// compile to nothing.
//
// Threads: the catalog is loaded once by Ui_Init on Core 0 in MainThread_Boot, before the boot
// snapshot and before the game thread starts, and never written again, so Core 0 and Core 1 may
// look strings up without locks; a SoftReset restores the snapshot, which keeps the same pointers.

#ifndef UI_LOCALE_H
#define UI_LOCALE_H

#include <stdbool.h>
#include <stddef.h>

// Loads the catalog of a non-English build. A missing or damaged file leaves the UI in English.
bool UiLocale_Load(void);

// Marks a literal kept in a table for the extractor (tools/locales/extract_ui.py); translate it
// where it is used, with UiLocale_Lookup.
#define UI_N(s) (s)

#ifdef UI_LOCALE_CATALOG
// The translation of `english`, or `english` itself when the catalog has no entry for it.
const char *UiLocale_Lookup(const char *english) __attribute__((format_arg(1)));

// The translation of a string literal, looked up once per call site: the catalog never changes
// after boot, so the first answer is kept. "" s "" only compiles for a literal, which is what
// makes one cache per call site correct. The relaxed atomics only keep the pointer whole; two
// cores racing to fill it store the same value.
#define UI_TR(s) (__extension__({                                                   \
    static const char *uiTrCache_;                                                  \
    const char *uiTr_ = __atomic_load_n(&uiTrCache_, __ATOMIC_RELAXED);             \
    if (uiTr_ == NULL)                                                              \
    {                                                                               \
        uiTr_ = UiLocale_Lookup("" s "");                                           \
        __atomic_store_n(&uiTrCache_, uiTr_, __ATOMIC_RELAXED);                     \
    }                                                                               \
    uiTr_;                                                                          \
}))
#else
static inline __attribute__((format_arg(1))) const char *UiLocale_Lookup(const char *english)
{
    return (english != NULL) ? english : "";
}
#define UI_TR(s) ("" s "")
#endif

#endif // UI_LOCALE_H
