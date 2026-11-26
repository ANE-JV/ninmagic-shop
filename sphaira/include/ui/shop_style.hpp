// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "nanovg.h"
#include <algorithm>
#include <chrono>
#include <cmath>

// Shared by the actual Switch screens and the desktop rendering harness.
namespace sphaira::ui::shop {
struct Rect {
    float x{}, y{}, w{}, h{};
    constexpr Rect() = default;
    constexpr Rect(float x_, float y_, float w_, float h_) : x{x_}, y{y_}, w{w_}, h{h_} {}
    template<class T> constexpr Rect(const T& r) : x{r.x}, y{r.y}, w{r.w}, h{r.h} {}
};
inline const NVGcolor Navy = nvgRGB(3, 12, 24);
inline const NVGcolor PanelTop = nvgRGB(11, 25, 43);
inline const NVGcolor PanelBottom = nvgRGB(5, 14, 27);
inline const NVGcolor Cyan = nvgRGB(0, 238, 252);
inline const NVGcolor Violet = nvgRGB(153, 73, 255);
inline const NVGcolor Amber = nvgRGB(255, 200, 0);
inline const NVGcolor White = nvgRGB(244, 248, 255);
inline const NVGcolor Muted = nvgRGB(171, 190, 213);
inline const NVGcolor Border = nvgRGB(62, 91, 121);
inline const NVGcolor Red = nvgRGB(255, 48, 76);
inline const NVGcolor Green = nvgRGB(76, 244, 32);

inline NVGcolor Alpha(NVGcolor c, float a) { c.a = a; return c; }
inline void Text(NVGcontext* vg, float x, float y, float size, NVGcolor colour,
        const char* text, int align = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, bool bold = false) {
    nvgFontFace(vg, bold && nvgFindFont(vg, "ShopBold") >= 0 ? "ShopBold" : "Standard");
    nvgFontSize(vg, size); nvgTextAlign(vg, align); nvgFillColor(vg, colour);
    nvgText(vg, x, y, text, nullptr);
    nvgFontFace(vg, "Standard");
}
inline void Line(NVGcontext* vg, float x1, float y1, float x2, float y2, NVGcolor colour, float width = 1.f) {
    nvgBeginPath(vg); nvgMoveTo(vg, x1, y1); nvgLineTo(vg, x2, y2);
    nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, width); nvgStroke(vg);
}
inline void Fill(NVGcontext* vg, Rect r, NVGcolor colour, float radius = 8.f) {
    nvgBeginPath(vg); nvgRoundedRect(vg, r.x, r.y, r.w, r.h, radius);
    nvgFillColor(vg, colour); nvgFill(vg);
}
inline void Outline(NVGcontext* vg, Rect r, NVGcolor colour, float width = 1.f, float radius = 8.f) {
    nvgBeginPath(vg); nvgRoundedRect(vg, r.x, r.y, r.w, r.h, radius);
    nvgStrokeWidth(vg, width); nvgStrokeColor(vg, colour); nvgStroke(vg);
}
inline void ImageCover(NVGcontext* vg, Rect r, int image, float radius = 8.f) {
    int w{}, h{}; nvgImageSize(vg, image, &w, &h);
    if (w <= 0 || h <= 0) return;
    const auto ratio = std::max(r.w / w, r.h / h);
    const auto dw = w * ratio, dh = h * ratio;
    nvgBeginPath(vg); nvgRoundedRect(vg, r.x, r.y, r.w, r.h, radius);
    nvgFillPaint(vg, nvgImagePattern(vg, r.x + (r.w - dw) * .5f, r.y + (r.h - dh) * .5f,
        dw, dh, 0.f, image, 1.f)); nvgFill(vg);
}
inline void Glow(NVGcontext* vg, Rect r, NVGcolor colour, float strength = .28f, float radius = 9.f) {
    const auto paint = nvgBoxGradient(vg, r.x, r.y, r.w, r.h, radius, 17.f,
        Alpha(colour, strength), Alpha(colour, 0.f));
    nvgBeginPath(vg); nvgRect(vg, r.x - 20, r.y - 20, r.w + 40, r.h + 40);
    nvgRoundedRect(vg, r.x + 1, r.y + 1, r.w - 2, r.h - 2, radius);
    nvgPathWinding(vg, NVG_HOLE); nvgFillPaint(vg, paint); nvgFill(vg);
}
inline float FocusPulse() {
    using namespace std::chrono;
    constexpr int period_ms = 1800;
    const auto time = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() % period_ms;
    return .5f - .5f * std::cos(float(time) * (6.2831853f / period_ms));
}

// The solid keyline stays legible over bright covers and cyan buttons. Only the halo breathes.
inline void FocusRing(NVGcontext* vg, Rect r, float radius = 8.f) {
    const auto pulse = FocusPulse();
    const auto lime = nvgRGB(202, 255, 58);
    const Rect edge{r.x + 1.f, r.y + 1.f, r.w - 2.f, r.h - 2.f};
    Glow(vg, r, lime, .27f + .48f * pulse, radius);
    Outline(vg, edge, nvgRGB(14, 26, 17), 6.f, radius);
    Outline(vg, edge, lime, 3.f, radius);
    // The leading marker identifies focus even when its colour is hard to distinguish.
    const auto marker_h = std::min(18.f, r.h * .44f);
    Fill(vg, {r.x, r.y + (r.h - marker_h) * .5f, 5.f, marker_h}, lime, 2.f);
}
inline void Panel(NVGcontext* vg, Rect r, NVGcolor accent = Border, bool focused = false, float radius = 10.f) {
    if (focused) Glow(vg, r, accent, .4f, radius);
    nvgBeginPath(vg); nvgRoundedRect(vg, r.x, r.y, r.w, r.h, radius);
    nvgFillPaint(vg, nvgLinearGradient(vg, r.x, r.y, r.x + r.w * .25f, r.y + r.h, PanelTop, PanelBottom));
    nvgFill(vg); Outline(vg, r, accent, focused ? 2.f : 1.f, radius);
}
enum class Icon { Folder, Search, Download, Settings, User, Plus, Queue, Card, Check, Back };
inline void DrawIcon(NVGcontext* vg, Icon icon, float x, float y, float size, NVGcolor colour = White) {
    nvgSave(vg); nvgTranslate(vg, x, y); nvgScale(vg, size / 24.f, size / 24.f);
    nvgStrokeColor(vg, colour); nvgStrokeWidth(vg, 1.8f); nvgLineCap(vg, NVG_ROUND); nvgLineJoin(vg, NVG_ROUND);
    nvgBeginPath(vg);
    switch (icon) {
        case Icon::Folder:
            nvgMoveTo(vg, 2, 7); nvgLineTo(vg, 2, 19); nvgQuadTo(vg, 2, 21, 4, 21);
            nvgLineTo(vg, 21, 21); nvgQuadTo(vg, 23, 21, 23, 19); nvgLineTo(vg, 23, 7);
            nvgQuadTo(vg, 23, 5, 21, 5); nvgLineTo(vg, 11, 5); nvgLineTo(vg, 9, 2);
            nvgLineTo(vg, 4, 2); nvgQuadTo(vg, 2, 2, 2, 4); nvgClosePath(vg); break;
        case Icon::Search:
            nvgCircle(vg, 10, 10, 7); nvgMoveTo(vg, 15, 15); nvgLineTo(vg, 22, 22); break;
        case Icon::Download:
            nvgMoveTo(vg, 12, 2); nvgLineTo(vg, 12, 16); nvgMoveTo(vg, 7, 11); nvgLineTo(vg, 12, 16); nvgLineTo(vg, 17, 11);
            nvgMoveTo(vg, 3, 16); nvgLineTo(vg, 3, 22); nvgLineTo(vg, 21, 22); nvgLineTo(vg, 21, 16); break;
        case Icon::Plus:
            nvgMoveTo(vg, 12, 3); nvgLineTo(vg, 12, 21); nvgMoveTo(vg, 3, 12); nvgLineTo(vg, 21, 12); break;
        case Icon::User:
            nvgCircle(vg, 12, 7, 4); nvgStroke(vg); nvgBeginPath(vg);
            nvgMoveTo(vg, 4, 22); nvgBezierTo(vg, 3, 12, 21, 12, 20, 22); nvgClosePath(vg); break;
        case Icon::Card:
            nvgMoveTo(vg, 5, 2); nvgLineTo(vg, 19, 2); nvgLineTo(vg, 21, 4); nvgLineTo(vg, 21, 22);
            nvgLineTo(vg, 3, 22); nvgLineTo(vg, 3, 5); nvgClosePath(vg);
            for (int i = 0; i < 4; ++i) { nvgMoveTo(vg, 7 + i * 3, 3); nvgLineTo(vg, 7 + i * 3, 8); } break;
        case Icon::Check:
            nvgMoveTo(vg, 4, 12); nvgLineTo(vg, 10, 18); nvgLineTo(vg, 21, 5); break;
        case Icon::Back:
            nvgMoveTo(vg, 14, 3); nvgLineTo(vg, 5, 12); nvgLineTo(vg, 14, 21); break;
        case Icon::Queue:
            for (int i = 0; i < 3; ++i) { nvgRoundedRect(vg, 2, 2 + i * 8, 20, 5, 1);
                nvgMoveTo(vg, 6, 4.5f + i * 8); nvgLineTo(vg, 7, 4.5f + i * 8); } break;
        case Icon::Settings:
            for (int i = 0; i < 12; ++i) {
                const auto a = i * 3.14159265f / 6.f;
                const auto radius = (i % 2) ? 8.f : 11.f;
                const auto xx = 12.f + std::cos(a) * radius, yy = 12.f + std::sin(a) * radius;
                if (!i) nvgMoveTo(vg, xx, yy); else nvgLineTo(vg, xx, yy);
            }
            nvgClosePath(vg); nvgCircle(vg, 12, 12, 4); break;
    }
    nvgStroke(vg); nvgRestore(vg);
}
inline void Button(NVGcontext* vg, Rect r, const char* label, bool focused = false, bool primary = false,
        Icon icon = Icon::Plus, bool show_icon = false, float text_size = 21.f) {
    if (primary) {
        Glow(vg, r, Cyan, focused ? .65f : .32f);
        nvgBeginPath(vg); nvgRoundedRect(vg, r.x, r.y, r.w, r.h, 9.f);
        nvgFillPaint(vg, nvgLinearGradient(vg, r.x, r.y, r.x, r.y + r.h,
            nvgRGB(0, 251, 255), nvgRGB(0, 208, 231))); nvgFill(vg);
        Outline(vg, r, focused ? White : Cyan, focused ? 2.f : 1.f);
    } else {
        Panel(vg, r, focused ? Cyan : Border, focused);
    }
    const auto ink = primary ? Navy : White;
    const auto text_x = r.x + r.w * .5f + (show_icon ? 15.f : 0.f);
    if (show_icon) DrawIcon(vg, icon, r.x + 22.f, r.y + (r.h - 24.f) * .5f, 24.f, ink);
    Text(vg, text_x, r.y + r.h * .5f, text_size, ink, label, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, true);
}
inline void Checkbox(NVGcontext* vg, Rect r, bool checked, bool available = true, bool focused = false) {
    const auto c = available ? Cyan : Border;
    if (focused) Glow(vg, r, c, .35f, 4.f);
    Outline(vg, r, c, focused ? 2.5f : 2.f, 4.f);
    if (checked) DrawIcon(vg, Icon::Check, r.x + 2, r.y + 2, r.w - 4, c);
}
inline void Background(NVGcontext* vg) {
    nvgBeginPath(vg); nvgRect(vg, 0, 0, 1280, 720);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, 1280, 720,
        nvgRGB(104, 43, 107), nvgRGB(49, 31, 81))); nvgFill(vg);
    nvgBeginPath(vg); nvgRect(vg, 0, 0, 1280, 720);
    nvgFillPaint(vg, nvgRadialGradient(vg, 350, 145, 30, 770,
        nvgRGBA(226, 98, 206, 38), nvgRGBA(226, 98, 206, 0))); nvgFill(vg);
    nvgBeginPath(vg); nvgRect(vg, 0, 0, 1280, 720);
    nvgFillPaint(vg, nvgRadialGradient(vg, 1125, 105, 35, 675,
        nvgRGBA(127, 174, 255, 45), nvgRGBA(127, 174, 255, 0))); nvgFill(vg);
}
inline constexpr Rect AccountButton{744.f, 25.f, 160.f, 38.f};
inline float ControllerHint(NVGcontext* vg, float x, float y, const char* key, const char* hint,
        NVGcolor colour, bool shoulder = false, const char* paired = nullptr, float scale = 1.f) {
    nvgSave(vg); nvgTranslate(vg, x, y); nvgScale(vg, scale, scale);
    const auto wide = shoulder && key[0] == 'Z';
    const auto width = paired ? 74.f : wide ? 40.f : 32.f;
    if (paired) {
        Fill(vg, {0, 3, 26, 25}, colour, 6.f); Fill(vg, {48, 3, 26, 25}, colour, 6.f);
        Text(vg, 13, 16, 18, Navy, paired, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, true);
        Text(vg, 37, 16, 19, White, "/", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        Text(vg, 61, 16, 18, Navy, key, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, true);
    } else {
        if (shoulder) Fill(vg, {0, 3, width, 25}, colour, 6.f);
        else { nvgBeginPath(vg); nvgCircle(vg, 16, 16, 16); nvgFillColor(vg, colour); nvgFill(vg); }
        Text(vg, width * .5f, 16, 23, Navy, key, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, true);
    }
    Text(vg, width + 10, 16, 18, White, hint, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, true);
    nvgFontFace(vg, nvgFindFont(vg, "ShopBold") >= 0 ? "ShopBold" : "Standard"); nvgFontSize(vg, 18);
    const auto text_w = nvgTextBounds(vg, 0, 0, hint, nullptr, nullptr);
    const auto advance = width + (text_w > 0 ? 10 + text_w : 0) + 32;
    Line(vg, advance - 16, 3, advance - 16, 29, Border);
    nvgRestore(vg);
    return advance * scale;
}
inline void Header(NVGcontext* vg, int logo, const char* status, bool connected,
        bool connecting, int battery, bool network, bool show_account = true) {
    Fill(vg, {0, 0, 1280, 88}, nvgRGB(5, 15, 29), 0.f);
    if (logo) {
        nvgBeginPath(vg); nvgRoundedRect(vg, 37, 9, 74, 74, 8);
        nvgFillPaint(vg, nvgImagePattern(vg, 37, 9, 74, 74, 0, logo, 1)); nvgFill(vg);
    }
    Line(vg, 123, 21, 123, 66, Cyan, 1.5f);
    Text(vg, 139, 46, 33, White, "NINMAGIC", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, true);
    nvgBeginPath(vg); nvgMoveTo(vg, 350, 32); nvgLineTo(vg, 431, 32);
    nvgLineTo(vg, 424, 61); nvgLineTo(vg, 343, 61); nvgClosePath(vg);
    nvgFillColor(vg, Amber); nvgFill(vg);
    Text(vg, 388, 46, 23, Navy, "SHOP", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, true);
    // The cyan/violet circuit strokes are part of the reference header.
    nvgBeginPath(vg); nvgMoveTo(vg, 30, 87); nvgLineTo(vg, 438, 87);
    nvgLineTo(vg, 447, 79); nvgLineTo(vg, 583, 79); nvgLineTo(vg, 603, 59);
    nvgLineTo(vg, 641, 59); nvgLineTo(vg, 675, 23);
    nvgStrokeWidth(vg, 1.5f); nvgStrokeColor(vg, Cyan); nvgStroke(vg);
    nvgBeginPath(vg); nvgMoveTo(vg, 587, 86); nvgLineTo(vg, 613, 66);
    nvgLineTo(vg, 651, 66); nvgLineTo(vg, 689, 24);
    nvgStrokeColor(vg, Violet); nvgStroke(vg);
    Line(vg, 30, 88, 1250, 88, Alpha(Border, .9f));
    if (show_account) {
        Panel(vg, AccountButton, Border, false, 20.f);
        DrawIcon(vg, Icon::User, AccountButton.x + 12, AccountButton.y + 6, 25, White);
        Text(vg, AccountButton.x + 48, 44, 19, White, "Mi cuenta", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, true);
        Line(vg, 923, 25, 923, 63, Border);
    }
    const auto status_colour = connected ? Green : connecting ? Amber : Muted;
    nvgBeginPath(vg); nvgCircle(vg, 942, 44, 5.f); nvgFillColor(vg, status_colour); nvgFill(vg);
    nvgSave(vg); nvgIntersectScissor(vg, 955, 23, 176, 43);
    Text(vg, 955, 44, 17, status_colour, status, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, true);
    nvgRestore(vg);
    Line(vg, 1140, 25, 1140, 63, Border);
    const auto wifi_colour = network ? White : Muted;
    for (int i = 0; i < 3; ++i) {
        nvgBeginPath(vg); nvgArc(vg, 1170, 58, 8.f + i * 6.f, -2.35f, -.79f, NVG_CW);
        nvgStrokeWidth(vg, 3.f); nvgStrokeColor(vg, wifi_colour); nvgStroke(vg);
    }
    nvgBeginPath(vg); nvgCircle(vg, 1170, 58, 2.f); nvgFillColor(vg, wifi_colour); nvgFill(vg);
    Outline(vg, {1200, 36, 34, 18}, White, 2.f, 3.f);
    Fill(vg, {1235, 41, 3, 8}, White, 1.f);
    if (battery >= 0) Fill(vg, {1203, 39, 28 * std::clamp(battery / 100.f, 0.f, 1.f), 12}, battery < 15 ? Red : Green, 1.f);
    else Text(vg, 1217, 45, 13, Muted, "?", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
}
} // namespace sphaira::ui::shop
