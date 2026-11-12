// The start screen's background (launcher_backdrop.h): a flat dark window, with the player's splash.png as the main
// page's picture when there is one. Split from launcher.cpp, which keeps the pages.
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "imgui.h"
#include "ui/launcher/launcher_backdrop.h"
#include "ui/launcher/launcher_look.h"

extern "C" {
#include "platform/platform.h"
}

static SDL_Texture *g_splash;   // splash.png next to the executable, the player's own picture
static int g_splash_w, g_splash_h;

void backdrop_load(SDL_Renderer *ren, const char *exe_dir) {
    char path[1100];
    snprintf(path, sizeof path, "%s/splash.png", exe_dir);
    if (!plat_file_exists(path)) return;
    SDL_Surface *s = SDL_LoadPNG(path);
    if (!s) return;
    g_splash = SDL_CreateTextureFromSurface(ren, s);
    g_splash_w = s->w, g_splash_h = s->h;
    SDL_DestroySurface(s);
}

void backdrop_free(void) {
    if (g_splash) SDL_DestroyTexture(g_splash);
    g_splash = nullptr;
}

bool backdrop_has_picture(void) { return g_splash != nullptr; }

// The logo's chequered flag (tools/build/icon.py) as the main page's picture: the cloth large, past the window's right
// and lower edges, in the dark tones of the page, waving slowly. The checks are quads whose corners follow the wave;
// the folds are lighter where the cloth faces up. Only the shape of the port's own logo: no art of the game.
static void flag(ImDrawList *dl, float w, float h) {
    // The wave's phase goes on only while the window has the focus: in the background the picture holds still (no
    // motion the player is not looking at, and no work for it). A shot: the same picture every run.
    static const bool still = getenv("MKW_LAUNCHER_SHOT") != nullptr;
    static float phase = 1.2f;
    if (!still && !ImGui::GetIO().AppFocusLost) phase += ImGui::GetIO().DeltaTime * 0.55f;
    float t = phase;
    const int kCols = 9, kRows = 7, kSub = 4;   // checks across and down; strips per check along the wave
    float cell = h * 0.18f, amp = cell * 0.42f, rot = -0.16f, cr = cosf(rot), sr = sinf(rot);
    ImVec2 pivot(w * 0.80f, h * 0.48f), o(w * 0.80f - cell * kCols * 0.42f, h * 0.48f - cell * kRows * 0.5f);
    auto at = [&](float u, float v) {   // a point of the cloth, u across and v down in checks
        float x = o.x + u * cell, y = o.y + v * cell + amp * sinf(u * 0.62f - t) * (0.35f + u / kCols);
        float dx = x - pivot.x, dy = y - pivot.y;
        return ImVec2(pivot.x + dx * cr - dy * sr, pivot.y + dx * sr + dy * cr);
    };
    const ImU32 dark = IM_COL32(15, 17, 23, 255);   // the light checks are worked out per strip, from the fold
    for (int j = 0; j < kRows; j++)
        for (int i = 0; i < kCols; i++)
            for (int k = 0; k < kSub; k++) {
                float u0 = i + (float)k / kSub, u1 = i + (float)(k + 1) / kSub;
                float face = 0.5f + 0.5f * cosf((u0 + u1) * 0.31f - t);   // the slope of the wave: lit where it faces up
                ImU32 c = (i + j) % 2 ? dark : ImGui::ColorConvertFloat4ToU32(ImVec4(
                    (28 + 12 * face) / 255.0f, (32 + 14 * face) / 255.0f, (42 + 20 * face) / 255.0f, 1));
                dl->AddQuadFilled(at(u0, (float)j), at(u1 + 0.02f, (float)j), at(u1 + 0.02f, j + 1.0f), at(u0, j + 1.0f), c);
            }
    // the left of the window back to the page's colour, under the menu, through a long fade
    ImU32 bg = LOOK_BG, none = IM_COL32(18, 21, 27, 0);
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(w * 0.34f, h), bg);
    dl->AddRectFilledMultiColor(ImVec2(w * 0.34f - 1, 0), ImVec2(w * 0.66f, h), bg, none, none, bg);
    // and the bottom, under the state and the version: their text on the page's colour, not on a check
    dl->AddRectFilledMultiColor(ImVec2(0, h * 0.70f), ImVec2(w, h * 0.86f), none, none, bg, bg);
    dl->AddRectFilled(ImVec2(0, h * 0.86f - 1), ImVec2(w, h), bg);
}

// ---- the background: the window's flat colour (no art of the game ships with the port). picture (the main page): the
// player's splash.png over the whole window, darkened on the left under the menu; without one, the logo's flag.
void backdrop_draw(ImDrawList *dl, float w, float h, bool picture) {
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), LOOK_BG);
    if (!picture) return;
    if (!g_splash) {
        flag(dl, w, h);
        return;
    }
    float k = fmaxf(w / g_splash_w, h / g_splash_h), sw = g_splash_w * k, sh = g_splash_h * k;
    dl->AddImage((ImTextureID)(intptr_t)g_splash, ImVec2((w - sw) / 2, (h - sh) / 2), ImVec2((w + sw) / 2, (h + sh) / 2));
    ImU32 dark = IM_COL32(18, 21, 27, 235), none = IM_COL32(18, 21, 27, 0);
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w * 0.62f, h), dark, none, none, dark);
    dl->AddRectFilledMultiColor(ImVec2(0, h * 0.75f), ImVec2(w, h), none, none, dark, dark);
}
