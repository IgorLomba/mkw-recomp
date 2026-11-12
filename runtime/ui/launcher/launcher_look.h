// The start screen's own look (ui/launcher/launcher_look.cpp): its palette, its ImGui style and the colours it gives the
// settings rows it shares with the in-game menu (ui/menu/option_rows.h). The in-game menu keeps its own (menu_look.h).
// ImGui's thread only.
#pragma once
#include "imgui.h"
#include "ui/menu/option_rows.h"

// A dark application (blue-grey surfaces, flat, square corners) with one blue, the blue of the port's logo
// (tools/build/icon.py). The blue is for the one main action of a page, the selected tab and progress; the selection is
// a white border, so a page never shows two blues competing. White on LOOK_ACCENT: 5.4:1. Text on LOOK_PANEL: LOOK_TEXT
// 13:1, LOOK_HEADING 7.5:1, LOOK_DIM 5.6:1 (5.1:1 on a settings row's fill; WCAG wants 4.5:1).
#define LOOK_BG IM_COL32(18, 21, 27, 255)       // the window behind everything
#define LOOK_PANEL IM_COL32(26, 30, 39, 255)    // a page, a bar
#define LOOK_RAISED IM_COL32(37, 42, 54, 255)   // a button, a field
#define LOOK_HOVER IM_COL32(50, 57, 72, 255)
#define LOOK_ACCENT IM_COL32(44, 100, 216, 255)
#define LOOK_ACCENT_HOT IM_COL32(62, 118, 232, 255)
#define LOOK_ACCENT_DOWN IM_COL32(36, 84, 196, 255)
#define LOOK_TEXT IM_COL32(230, 233, 238, 255)
#define LOOK_HEADING IM_COL32(169, 177, 189, 255)
#define LOOK_DIM IM_COL32(141, 150, 164, 255)
#define LOOK_OFF IM_COL32(88, 96, 110, 255)     // a row or a button that takes no input
#define LOOK_FOCUS IM_COL32(255, 255, 255, 235)
#define LOOK_ROW IM_COL32(255, 255, 255, 9)     // a row's fill and its border, at rest and under the cursor
#define LOOK_ROW_HOT IM_COL32(255, 255, 255, 24)
#define LOOK_LINE IM_COL32(255, 255, 255, 24)
#define LOOK_OK IM_COL32(91, 201, 138, 255)
#define LOOK_WARN IM_COL32(230, 166, 74, 255)
#define LOOK_BAD IM_COL32(240, 118, 107, 255)

void launcher_look_style(ImGuiStyle &s);   // at scale 1: the caller ScaleAllSizes
extern const OptLook launcher_look_rows;   // for opt_look: the settings rows in this palette
