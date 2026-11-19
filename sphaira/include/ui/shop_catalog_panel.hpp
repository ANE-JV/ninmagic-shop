// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ui/shop_style.hpp"
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
    size_t sidebar_index{}, sidebar_offset{};
    bool sidebar_focused{}, detail_focused{}, loading{};
};
// The console and the desktop QA renderer execute this same drawing function.
// Artwork IDs and all labels come from the caller's real catalog state.
inline void DrawCatalogPanel(NVGcontext* vg, const CatalogPanelState& state) {
    Panel(vg, {30,108,240,508}, Cyan);
    Text(vg,48,133,22,Muted,"CONSOLAS",NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE,true);
    Line(vg,49,153,253,153,Alpha(Cyan,.45f));
    const auto count=state.platforms.size();
    const auto offset=std::min(state.sidebar_offset,count>6?count-6:0);
    for(size_t row=0;row<6&&offset+row<count;++row) {
        const auto index=offset+row;
        const auto& label=state.platforms[index];
        Button(vg,{49,169+float(row)*43,214,37},label=="__unknown__"?"Sin clasificar":label.c_str(),
            state.sidebar_focused&&state.sidebar_index==index,state.platform==label,Icon::Folder,true,16);
    }
    if(offset)Text(vg,253,159,13,Cyan,"↑",NVG_ALIGN_RIGHT|NVG_ALIGN_MIDDLE);
    if(offset+6<count)Text(vg,253,430,13,Cyan,"↓",NVG_ALIGN_RIGHT|NVG_ALIGN_MIDDLE);
    Line(vg,49,437,253,437,Alpha(Cyan,.55f));
    Button(vg,{50,454,213,40},"Descargas",state.sidebar_focused&&state.sidebar_index==count,false,Icon::Download,true,20);
    Button(vg,{50,501,213,40},"Ajustes",state.sidebar_focused&&state.sidebar_index==count+1,false,Icon::Settings,true,20);
    Line(vg,49,561,253,561,Alpha(Border,.9f));
    DrawIcon(vg,Icon::Card,58,578,23,Muted);
    nvgSave(vg);nvgIntersectScissor(vg,90,570,166,35);Text(vg,92,590,14,Muted,state.storage.c_str());nvgRestore(vg);

    Line(vg,301,112,290,140,Cyan,8);
    nvgSave(vg);nvgIntersectScissor(vg,325,108,503,42);
    Text(vg,325,129,32,White,state.heading.c_str(),NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE,true);nvgRestore(vg);
    nvgSave(vg);nvgIntersectScissor(vg,325,145,503,24);Text(vg,326,157,17,Muted,state.location.c_str());nvgRestore(vg);
    Panel(vg,{298,178,370,38},Border);DrawIcon(vg,Icon::Search,314,186,23);
    nvgSave(vg);nvgIntersectScissor(vg,348,181,308,32);
    Text(vg,349,198,17,Muted,state.search.empty()?"Buscar juegos...":state.search.c_str());nvgRestore(vg);
    Button(vg,{682,178,146,38},"Carpetas",false,false,Icon::Folder,true,17);

    if(state.cards.empty()) {
        Panel(vg,{298,231,530,374},Border);
        DrawIcon(vg,Icon::Search,529,330,48,Cyan);
        nvgSave(vg);nvgIntersectScissor(vg,308,390,510,83);
        Text(vg,563,410,18,Muted,state.loading?"Cargando catálogo...":state.empty_text.c_str(),NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
        Text(vg,563,441,15,Muted,"X Buscar · Carpetas para cambiar de sección",NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);nvgRestore(vg);
    }
    nvgSave(vg);nvgIntersectScissor(vg,294,226,540,386);
    for(size_t index=0;index<state.cards.size();++index) {
        const auto& item=state.cards[index];
        const Rect card=item.bounds.w>0?item.bounds:Rect{298+float(index%2)*272,230+float(index/2)*192,258,181};
        Panel(vg,card,item.selected?Cyan:Border,item.selected&&!state.sidebar_focused&&!state.detail_focused);
        if(item.directory)DrawIcon(vg,Icon::Folder,card.x+90,card.y+42,70,Cyan);
        else if(item.image)ImageCover(vg,{card.x+2,card.y+2,card.w-4,146},item.image,8);
        else {
            DrawIcon(vg,Icon::Queue,card.x+109,card.y+43,40,Muted);
            Text(vg,card.x+card.w*.5f,card.y+113,14,Muted,item.loading?"Cargando carátula...":"Sin carátula",NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
        }
        Fill(vg,{card.x+2,card.y+148,card.w-4,30},nvgRGBA(6,20,36,245),3);
        nvgSave(vg);nvgIntersectScissor(vg,card.x+6,card.y+149,card.w-12,29);
        nvgFontFace(vg,nvgFindFont(vg,"ShopBold")>=0?"ShopBold":"Standard");
        nvgFontSize(vg,15.f);
        const auto available=card.w-12.f;
        const auto width=nvgTextBounds(vg,0,0,item.title.c_str(),nullptr,nullptr);
        const auto font_size=width>available?std::max(11.5f,15.f*available/width):15.f;
        nvgFontSize(vg,font_size);
        const bool fits=nvgTextBounds(vg,0,0,item.title.c_str(),nullptr,nullptr)<=available+.25f;
        Text(vg,fits?card.x+card.w*.5f:card.x+6,card.y+163,font_size,White,item.title.c_str(),
            (fits?NVG_ALIGN_CENTER:NVG_ALIGN_LEFT)|NVG_ALIGN_MIDDLE,true);nvgRestore(vg);
        if(item.selected)Outline(vg,card,Cyan,3);
        if(item.favorite) {
            Fill(vg,{card.x+card.w-38,card.y+9,29,29},Amber,6);
            Text(vg,card.x+card.w-24,card.y+23,23,Navy,"★",NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
        }
    }
    nvgRestore(vg);
    Text(vg,563,624,14,Muted,state.page.c_str(),NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
}
}
