#ifndef UI_LOCALE_H
#define UI_LOCALE_H

#include <stdint.h>

const char *UiLocale_Lookup(const char *en) __attribute__((format_arg(1)));
const char *UiLocale_Format(const char *key, const char *fallback);
const char *UiLocale_Number(const char *en);

#define UI_TR(s) UiLocale_Lookup(s)
#define UI_N(s)  (s)

#endif // UI_LOCALE_H
