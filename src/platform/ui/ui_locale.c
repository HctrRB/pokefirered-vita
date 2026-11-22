#include "ui_locale.h"

const char *UiLocale_Lookup(const char *const en)
{
    return (en != 0) ? en : "";
}

const char *UiLocale_Format(const char *const key, const char *const fallback)
{
    return (fallback != 0) ? fallback : ((key != 0) ? key : "");
}

const char *UiLocale_Number(const char *const en)
{
    return (en != 0) ? en : "";
}
