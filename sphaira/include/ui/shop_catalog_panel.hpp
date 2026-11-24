// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/shop_style.hpp"
#include "ui/shop_layout.hpp"
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
    size_t sidebar_index{}, sidebar_offset{};
    bool sidebar_focused{}, detail_focused{}, loading{}, can_update{true}, list_view{};
};

inline void CatalogLabel(NVGcontext* vg, Rect bounds, const std::string& title, float size,
        NVGcolor ink = White, bool bold = true) {
    if (title.empty()) return;
    nvgSave(vg);
    nvgIntersectScissor(vg,bounds.x,bounds.y,bounds.w,bounds.h);
    nvgFontFace(vg,bold && nvgFindFont(vg,"ShopBold")>=0 ? "ShopBold" : "Standard");
    nvgFontSize(vg,size); nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE); nvgFillColor(vg,ink);
    const auto y = bounds.y + bounds.h * .5f;
    if(nvgTextBounds(vg,0,0,title.c_str(),nullptr,nullptr) <= bounds.w) {
        nvgText(vg,bounds.x,y,title.c_str(),nullptr);
    } else {
        const auto dots = nvgTextBounds(vg,0,0,"…",nullptr,nullptr);
        NVGtextRow row{};
        if(nvgTextBreakLines(vg,title.c_str(),nullptr,bounds.w-dots,&row,1)) {
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
        const auto scale = std::min(area.w / width, area.h / height);
        const auto w = width * scale, h = height * scale;
        if (grid && area.w - w > 6.f) {
            // Carry the cover's colour into the side margins while keeping the complete artwork visible.
            ImageCover(vg,area,item.image,4.f);
            Fill(vg,area,nvgRGBA(3,12,24,184),4.f);
        }
        ImageCover(vg,{area.x+(area.w-w)*.5f,area.y+(area.h-h)*.5f,w,h},item.image,grid?4.f:2.f);
        return;
    }
    const auto icon = item.directory ? Icon::Folder : Icon::Queue;
    const auto side = grid ? 28.f : 19.f;
    const auto y = area.y + (area.h-side)*.5f - (grid&&!item.directory?7.f:0.f);
    DrawIcon(vg,icon,area.x+(area.w-side)*.5f,y,side,item.directory?Cyan:Muted);
    if (grid && !item.directory)
        Text(vg,area.x+area.w*.5f,y+side+11.f,13.f,Muted,item.loading?"Cargando…":"Sin carátula",NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
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
    const auto sidebar_button = [&](Rect bounds, size_t index, const char* text, bool active, Icon icon, bool show_icon, float size) {
        Button(vg,bounds,text,false,active,icon,show_icon,size);
        if (state.sidebar_focused && state.sidebar_index == index) sidebar_focus = bounds;
    };
    for(size_t row=0;row<visible&&offset+row<count;++row) {
        const auto index=offset+row;
        const auto& label=state.platforms[index];
        sidebar_button(layout::PlatformRow(row),index,label=="__unknown__"?"Sin clasificar":label.c_str(),
            state.platform==label,Icon::Folder,false,16);
    }
    if(offset)Text(vg,214,159,13,Cyan,"↑",NVG_ALIGN_RIGHT|NVG_ALIGN_MIDDLE);
    if(offset+visible<count)Text(vg,214,430,13,Cyan,"↓",NVG_ALIGN_RIGHT|NVG_ALIGN_MIDDLE);
    Line(vg,38,437,214,437,Alpha(Cyan,.55f));
    sidebar_button(layout::Downloads,count,"Descargas",false,Icon::Download,true,18);
    sidebar_button(layout::Settings,count+1,"Ajustes",false,Icon::Settings,true,18);
    if (state.can_update) sidebar_button(layout::UpdateApp,count+2,"Actualizar app",false,Icon::Download,true,16);
    if (sidebar_focus.w > 0) FocusRing(vg,sidebar_focus);
    Line(vg,38,561,214,561,Alpha(Border,.9f));
    DrawIcon(vg,Icon::Card,39,578,23,Muted);
    nvgSave(vg);nvgIntersectScissor(vg,69,570,146,35);Text(vg,70,590,13,Muted,state.storage.c_str());nvgRestore(vg);

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
        const auto focused=item.selected&&!state.sidebar_focused&&!state.detail_focused;
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
    Text(vg,layout::ListCard.x+layout::ListCard.w*.5f,625,14,Muted,state.page.c_str(),NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
}
}
