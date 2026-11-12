// Notices (menu_notice in menu.h): one line at the top middle of the picture for some seconds, menu open or not; the
// overlay draws a frame while one is up (menu_overlay_frame).
// Why: the game went back to its title silently when it lacked controllers for the players chosen (#106, #160).
#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "imgui.h"
#include "ui/menu/menu_notice.h"

extern "C" {
#include "platform/platform.h"
#include "ui/menu/menu.h"
}

static PlatMutex g_lock = PLAT_MUTEX_INIT;
static char g_text[256];
static std::atomic<uint64_t> g_until;   // plat_now_ns() it hides at; 0 none

extern "C" void menu_notice(const char *text, int seconds) {
    plat_mutex_lock(&g_lock);
    snprintf(g_text, sizeof g_text, "%s", text);
    plat_mutex_unlock(&g_lock);
    g_until.store(plat_now_ns() + (uint64_t)seconds * 1000000000ull);
}

bool menu_notice_shown(void) {
    uint64_t until = g_until.load();
    return until && plat_now_ns() < until;
}

void menu_notice_draw(float width, float scale) {
    if (!menu_notice_shown()) return;
    char text[sizeof g_text];
    plat_mutex_lock(&g_lock);
    memcpy(text, g_text, sizeof text);
    plat_mutex_unlock(&g_lock);
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    ImVec2 sz = ImGui::CalcTextSize(text);
    float pad = 8 * scale;
    ImVec2 p((width - sz.x) / 2 - pad, 12 * scale);
    dl->AddRectFilled(p, ImVec2(p.x + sz.x + pad * 2, p.y + sz.y + pad * 2), IM_COL32(0, 0, 0, 180), 4 * scale);
    dl->AddText(ImVec2(p.x + pad, p.y + pad), IM_COL32(255, 255, 255, 245), text);
}
