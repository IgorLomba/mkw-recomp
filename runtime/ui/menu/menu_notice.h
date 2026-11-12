// menu_notice (menu.h): the notice line the overlay draws, for menu.cpp.
#pragma once

bool menu_notice_shown(void);                         // a notice is up now
void menu_notice_draw(float width, float scale);      // in the current ImGui frame, at the top middle
