// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/shop_style.hpp"
#include "ui/shop_layout.hpp"
#include "ui/shop_console_logos.hpp"
#include <string>
#include <vector>

namespace sphaira::ui::shop {
struct CatalogCard {
    std::string title;
    int image{};
    bool directory{}, favorite{}, loading{}, selected{};
    Rect bounds{};
};
struct CatalogPanelState {
    std::vector<std::string> platforms;
    std::vector<CatalogCard> cards;
    std::string platform, heading, location, search, storage, page, empty_text;
    std::string search_hint{"Buscar juegos..."};
    size_t sidebar_index{}, sidebar_offset{}, toolbar_index{};
    int platform_logos{};
    bool sidebar_focused{}, detail_focused{}, toolbar_focused{}, loading{}, can_update{true}, list_view{};
    bool update_available{}, has_active_install{};
};

inline void CatalogLabel(NVGcontext* vg, Rect bounds, std::string_view title, float size,
        NVGcolor ink = White, bool bold = true, float minimum_size = 0.f) {
    if (title.empty()) return;
    nvgSave(vg);
    nvgIntersectScissor(vg,bounds.x,bounds.y,bounds.w,bounds.h);
    nvgFontFace(vg,bold && nvgFindFont(vg,"ShopBold")>=0 ? "ShopBold" : "Standard");
    nvgFontSize(vg,size); nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE); nvgFillColor(vg,ink);
    const auto y = bounds.y + bounds.h * .5f;
    const auto* end = title.data() + title.size();
    auto width = nvgTextBounds(vg,0,0,title.data(),end,nullptr);
    if (width > bounds.w && minimum_size > 0.f) {
        nvgFontSize(vg, std::max(minimum_size, size * (bounds.w - 4.f) / width));
        width = nvgTextBounds(vg,0,0,title.data(),end,nullptr);
    }
    if(width <= bounds.w) {
        nvgText(vg,bounds.x,y,title.data(),end);
    } else {
        const auto dots = nvgTextBounds(vg,0,0,"…",nullptr,nullptr);
        NVGtextRow row{};
        if(nvgTextBreakLines(vg,title.data(),end,bounds.w-dots,&row,1)) {
            const auto x = nvgText(vg,bounds.x,y,row.start,row.end);
            nvgText(vg,x,y,"…",nullptr);
        }
    }
    nvgRestore(vg);
}

inline void CatalogArtwork(NVGcontext* vg, Rect area, const CatalogCard& item, bool grid) {
    int width{}, height{};
    if (item.image > 0) nvgImageSize(vg,item.image,&width,&height);
    if (!item.directory && width > 0 && height > 0) {
        if (grid) {
            ImageCover(vg, area, item.image, 4.f);
            return;
        }
        const auto scale = std::min(area.w / width, area.h / height);
        const auto w = width * scale, h = height * scale;
        ImageCover(vg, {area.x + (area.w - w) * .5f, area.y + (area.h - h) * .5f, w, h}, item.image, 2.f);
        return;
    }
    const auto icon = item.directory ? Icon::Folder : Icon::Queue;
    const auto side = grid ? 28.f : 19.f;
    const auto y = area.y + (area.h-side)*.5f - (grid&&!item.directory?7.f:0.f);
    DrawIcon(vg,icon,area.x+(area.w-side)*.5f,y,side,item.directory?Cyan:Muted);
    if (grid && !item.directory)
        Text(vg,area.x+area.w*.5f,y+side+11.f,13.f,Muted,item.loading?"Cargando…":"Sin carátula",NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
}

inline void ConsoleRow(NVGcontext* vg, Rect bounds, std::string_view platform, int logos, bool active) {
    if (active) {
        const auto tint = nvgLinearGradient(vg, bounds.x, bounds.y, bounds.x + bounds.w, bounds.y,
            nvgRGB(4, 88, 108), nvgRGB(8, 46, 66));
        nvgBeginPath(vg);
        nvgRoundedRect(vg, bounds.x, bounds.y, bounds.w, bounds.h, 7.f);
        nvgFillPaint(vg, tint);
        nvgFill(vg);
        Outline(vg, bounds, Cyan, 1.2f, 7.f);
        nvgBeginPath(vg);
        nvgCircle(vg, bounds.x + bounds.w - 9.f, bounds.y + bounds.h * .5f, 2.5f);
        nvgFillColor(vg, Cyan);
        nvgFill(vg);
    } else {
        Fill(vg, bounds, Alpha(PanelTop, .64f), 7.f);
    }
    const Rect badge{bounds.x + 6.f, bounds.y + 3.f, 34.f, bounds.h - 6.f};
    Fill(vg, badge, Alpha(Navy, .84f), 5.f);
    const auto side = badge.h;
    DrawConsoleLogo(vg, {badge.x + (badge.w - side) * .5f, badge.y, side, side}, logos, platform);
    CatalogLabel(vg, {bounds.x + 48.f, bounds.y, bounds.w - 61.f, bounds.h},
        platform == "__unknown__" ? "Sin clasificar" : platform, 17.f, White, true, 12.5f);
}

inline void DrawAppUpdateIndicator(NVGcontext* vg, Rect bounds) {
    nvgBeginPath(vg);
    nvgCircle(vg, bounds.x + bounds.w - 10.f, bounds.y + bounds.h * .5f, 4.f);
    nvgFillColor(vg, nvgRGB(60, 149, 255));
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGB(150, 209, 255));
    nvgStrokeWidth(vg, 1.f);
    nvgStroke(vg);
}

inline void DrawAppUpdateButton(NVGcontext* vg, Rect bounds, bool available) {
    Button(vg, bounds, "Actualizar app", false, false, Icon::Download, true, 17.f);
    if (available) DrawAppUpdateIndicator(vg, bounds);
}

// The console and desktop QA execute the same renderer with real caller state.
inline void DrawCatalogPanel(NVGcontext* vg, const CatalogPanelState& state) {
    Panel(vg,layout::Sidebar,Cyan);
    Text(vg,38,133,21,Muted,"CONSOLAS",NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE,true);
    Line(vg,38,153,214,153,Alpha(Cyan,.45f));
    const auto count=state.platforms.size();
    const auto visible=layout::VisiblePlatforms;
    const auto offset=std::min(state.sidebar_offset,count>visible?count-visible:0);
    Rect sidebar_focus{};
    for(size_t row=0;row<visible&&offset+row<count;++row) {
        const auto index=offset+row;
        const auto& label=state.platforms[index];
        const auto bounds = layout::PlatformRow(row);
        ConsoleRow(vg, bounds, label, state.platform_logos, state.platform == label);
        if (state.sidebar_focused && state.sidebar_index == index) sidebar_focus = bounds;
    }
    if (count > visible) {
        const Rect track{219, layout::PlatformViewport.y, 3, layout::PlatformViewport.h};
        const auto thumb = track.h * static_cast<float>(visible) / static_cast<float>(count);
        const auto y = track.y + (track.h - thumb) * static_cast<float>(offset) / static_cast<float>(count - visible);
        Fill(vg, track, Alpha(Cyan,.22f), 1.5f);
        Fill(vg, {track.x,y,track.w,thumb}, Cyan, 1.5f);
    }
    if (sidebar_focus.w > 0) FocusRing(vg,sidebar_focus);

    using layout::ToolbarAction;
    const auto toolbar_button = [&](ToolbarAction action, const char* label, Icon icon) {
        const auto index = static_cast<size_t>(action);
        const auto bounds = layout::ToolbarButtons[index];
        const auto active = action == ToolbarAction::InProgress && state.has_active_install;
        const auto update_available = action == ToolbarAction::UpdateApp && state.update_available;
        const auto ink = active ? Cyan : White;
        Panel(vg, bounds, active ? Cyan : Border, false, 8.f);
        DrawIcon(vg, icon, bounds.x + 13.f, bounds.y + 6.f, 20.f, ink);
        CatalogLabel(vg, {bounds.x + 42.f, bounds.y, bounds.w - (update_available ? 59.f : 50.f), bounds.h},
            label, 17.f, ink, true, 14.f);
        if (update_available) DrawAppUpdateIndicator(vg, bounds);
        if (state.toolbar_focused && state.toolbar_index == index) FocusRing(vg, bounds);
    };
    toolbar_button(ToolbarAction::Downloads, "Descargas", Icon::Download);
    toolbar_button(ToolbarAction::InProgress, state.has_active_install ? "En curso (1)" : "En curso", Icon::Queue);
    toolbar_button(ToolbarAction::Settings, "Ajustes", Icon::Settings);
    if (state.can_update) toolbar_button(ToolbarAction::UpdateApp, "Actualizar app", Icon::Download);
    DrawIcon(vg,Icon::Card,layout::Storage.x,layout::Storage.y+5,22,Muted);
    CatalogLabel(vg,{layout::Storage.x+30,layout::Storage.y,layout::Storage.w-30,layout::Storage.h},
        state.storage,14,Muted,false);

    Line(vg,251,104,242,130,Cyan,6);
    CatalogLabel(vg,{266,97,354,40},state.heading,29);
    CatalogLabel(vg,{627,103,295,31},state.location,15,Muted,false);
    Panel(vg,layout::Search,Border);
    DrawIcon(vg,Icon::Search,257,148,22);
    CatalogLabel(vg,{289,141,339,35},state.search.empty()?state.search_hint:state.search,17,Muted,false);
    Panel(vg,layout::Sections,Border);
    DrawIcon(vg,Icon::Folder,658,149,19);
    Text(vg,687,159,16,White,"Secciones",NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE,true);
    Panel(vg,layout::ViewMode,Cyan);
    Fill(vg,{786,148,27,21},Cyan,5);
    Text(vg,799.5f,158.5f,14,Navy,"R3",NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE,true);
    Text(vg,820,159,16,White,state.list_view?"Ver carátulas":"Ver lista",NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE,true);

    if(state.cards.empty()) {
        Panel(vg,{layout::Card.x,layout::Card.y,layout::ListCard.w,layout::Grid.h-9},Border);
        const auto center = layout::ListCard.x + layout::ListCard.w *.5f;
        DrawIcon(vg,Icon::Search,center-21,318,42,Cyan);
        Text(vg,center,389,18,Muted,state.loading?"Cargando juegos…":state.empty_text.c_str(),NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
        Text(vg,center,417,15,Muted,"X Buscar · L / R Cambiar consola",NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
    }
    nvgSave(vg);nvgIntersectScissor(vg,layout::Grid.x,layout::Grid.y,layout::Grid.w,layout::Grid.h);
    for(size_t index=0;index<state.cards.size();++index) {
        const auto& item=state.cards[index];
        const Rect card=item.bounds.w>0?item.bounds:state.list_view?layout::ListAt(index):layout::CardAt(index);
        const auto focused=item.selected&&!state.sidebar_focused&&!state.detail_focused&&!state.toolbar_focused;
        Panel(vg,card,item.selected?Cyan:Border,false,6.f);
        if(state.list_view) {
            const Rect art{card.x+8,card.y+3,44,card.h-6};
            CatalogArtwork(vg,art,item,false);
            CatalogLabel(vg,{card.x+66,card.y,card.w-108,card.h},item.title,17,White,item.selected);
            if(item.selected) Fill(vg,{card.x+1,card.y+6,3,card.h-12},Cyan,1);
        } else {
            const Rect art{card.x+2,card.y+2,card.w-4,card.h-27};
            CatalogArtwork(vg,art,item,true);
            CatalogLabel(vg,{card.x+7,card.y+card.h-25,card.w-14,24},item.title,14);
        }
        if(item.favorite) {
            const float y=state.list_view?card.y+(card.h-18)*.5f:card.y+5;
            Fill(vg,{card.x+card.w-23,y,18,18},Amber,4);
            Text(vg,card.x+card.w-14,y+9,14,Navy,"★",NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
        }
        if(focused) FocusRing(vg,card,6.f);
    }
    nvgRestore(vg);
    Text(vg,layout::ListCard.x+layout::ListCard.w*.5f,590,14,Muted,state.page.c_str(),NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
}
}
