// The catalogs of ui_text.c, one file per menu language (ui_text_en.c, ui_text_ru.c, ui_text_es.c): the shared rows.
#pragma once
#include "ui_text.h"

// The language names are the same in every row.
#define LANG_NAMES \
    [UI_MENU_LANGNAME_EN] = "English", [UI_MENU_LANGNAME_JA] = "日本語", [UI_MENU_LANGNAME_FR] = "Français", \
    [UI_MENU_LANGNAME_DE] = "Deutsch", [UI_MENU_LANGNAME_IT] = "Italiano", [UI_MENU_LANGNAME_ES] = "Español", \
    [UI_MENU_LANGNAME_NL] = "Nederlands", [UI_MENU_LANGNAME_PT] = "Português (Portugal)", \
    [UI_MENU_LANGNAME_PT_BR] = "Português (Brasil)", [UI_MENU_LANGNAME_RU] = "Русский", \
    [UI_MENU_LANGNAME_ZH_HANS] = "简体中文", [UI_MENU_LANGNAME_ZH_HANT] = "繁體中文", [UI_MENU_LANGNAME_KO] = "한국어"

// SWKBD: the keyboard's DEFAULT_NAME (at most 9 UTF-16 units, no '@' '%' '\').
#define SWKBD(name) [UI_SWKBD_DEFAULT_NAME] = name, LANG_NAMES

extern const char *const text_en[UI_TEXT_COUNT], *const text_ru[UI_TEXT_COUNT], *const text_es[UI_TEXT_COUNT];
