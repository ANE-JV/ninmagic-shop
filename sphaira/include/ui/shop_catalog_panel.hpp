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
    bool sidebar_focused{}, detail_focused{}, loading{}, can_update{true};
};

inline void CatalogTitle(NVGcontext* vg, Rect card, const std::string& title) {
    nvgSave(vg);
    nvgIntersectScissor(vg,card.x+5,card.y+148,card.w-10,35);
    nvgFontFace(vg,nvgFindFont(vg,"ShopBold")>=0?"ShopBold":"Standard");
    nvgFontSize(vg,13.f);
    NVGtextRow rows[3]{};
    const auto count=nvgTextBreakLines(vg,title.c_str(),nullptr,card.w-10,rows,3);
    nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_TOP);
    nvgFillColor(vg,White);
    for(int row=0;row<std::min(count,2);++row) {
        std::string line(rows[row].start,rows[row].end);
        if(row==1&&count>2) {
            while(!line.empty()&&nvgTextBounds(vg,0,0,(line+"…").c_str(),nullptr,nullptr)>card.w-10) {
                auto last=line.size()-1;
                while(last&&(static_cast<unsigned char>(line[last])&0xc0)==0x80)--last;
                line.resize(last);
            }
            line+="…";
        }
        nvgText(vg,card.x+card.w*.5f,card.y+149+float(row)*16,line.c_str(),nullptr);
    }
    nvgRestore(vg);
}

// The console and desktop QA execute the same renderer with real caller state.
inline void DrawCatalogPanel(NVGcontext* vg, const CatalogPanelState& state) {
    Panel(vg,layout::Sidebar,Cyan);
    Text(vg,38,133,21,Muted,"CONSOLAS",NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE,true);
    Line(vg,38,153,214,153,Alpha(Cyan,.45f));
    const auto count=state.platforms.size();
    const auto visible=layout::VisiblePlatforms;
    const auto offset=std::min(state.sidebar_offset,count>visible?count-visible:0);
    for(size_t row=0;row<visible&&offset+row<count;++row) {
        const auto index=offset+row;
        const auto& label=state.platforms[index];
        Button(vg,layout::PlatformRow(row),label=="__unknown__"?"Sin clasificar":label.c_str(),
            state.sidebar_focused&&state.sidebar_index==index,state.platform==label,Icon::Folder,false,16);
    }
    if(offset)Text(vg,214,159,13,Cyan,"↑",NVG_ALIGN_RIGHT|NVG_ALIGN_MIDDLE);
    if(offset+visible<count)Text(vg,214,430,13,Cyan,"↓",NVG_ALIGN_RIGHT|NVG_ALIGN_MIDDLE);
    Line(vg,38,437,214,437,Alpha(Cyan,.55f));
    Button(vg,layout::Downloads,"Descargas",state.sidebar_focused&&state.sidebar_index==count,false,Icon::Download,true,18);
    Button(vg,layout::Settings,"Ajustes",state.sidebar_focused&&state.sidebar_index==count+1,false,Icon::Settings,true,18);
    if (state.can_update) Button(vg,layout::UpdateApp,"Actualizar app",state.sidebar_focused&&state.sidebar_index==count+2,false,Icon::Download,true,16);
    Line(vg,38,561,214,561,Alpha(Border,.9f));
    DrawIcon(vg,Icon::Card,39,578,23,Muted);
    nvgSave(vg);nvgIntersectScissor(vg,69,570,146,35);Text(vg,70,590,13,Muted,state.storage.c_str());nvgRestore(vg);

    Line(vg,249,112,240,140,Cyan,7);
    nvgSave(vg);nvgIntersectScissor(vg,266,108,600,42);
    Text(vg,267,129,29,White,state.heading.c_str(),NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE,true);nvgRestore(vg);
    nvgSave(vg);nvgIntersectScissor(vg,267,145,599,24);Text(vg,268,157,16,Muted,state.location.c_str());nvgRestore(vg);
    Panel(vg,layout::Search,Border);DrawIcon(vg,Icon::Search,258,186,23);
    nvgSave(vg);nvgIntersectScissor(vg,290,181,400,32);
    Text(vg,292,198,17,Muted,state.search.empty()?state.search_hint.c_str():state.search.c_str());nvgRestore(vg);
    Button(vg,layout::Sections,"Secciones",false,false,Icon::Folder,true,16);

    if(state.cards.empty()) {
        Panel(vg,{244,226,622,380},Border);
        DrawIcon(vg,Icon::Search,531,325,48,Cyan);
        nvgSave(vg);nvgIntersectScissor(vg,254,390,602,83);
        Text(vg,555,410,18,Muted,state.loading?"Cargando juegos...":state.empty_text.c_str(),NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
        Text(vg,555,441,15,Muted,"X Buscar · L / R Cambiar consola",NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);nvgRestore(vg);
    }
    nvgSave(vg);nvgIntersectScissor(vg,layout::Grid.x,layout::Grid.y,layout::Grid.w,layout::Grid.h);
    for(size_t index=0;index<state.cards.size();++index) {
        const auto& item=state.cards[index];
        const Rect card=item.bounds.w>0?item.bounds:layout::CardAt(index);
        Panel(vg,card,item.selected?Cyan:Border,item.selected&&!state.sidebar_focused&&!state.detail_focused);
        if(item.directory)DrawIcon(vg,Icon::Folder,card.x+34,card.y+47,50,Cyan);
        else if(item.image) {
            int width{},height{};nvgImageSize(vg,item.image,&width,&height);
            if(width>0&&height>0) {
                const auto scale=std::min((card.w-4)/width,144.f/height);
                const auto w=width*scale,h=height*scale;
                ImageCover(vg,{card.x+(card.w-w)*.5f,card.y+2+(144-h)*.5f,w,h},item.image,5);
            }
        } else {
            DrawIcon(vg,Icon::Queue,card.x+42,card.y+43,34,Muted);
            Text(vg,card.x+card.w*.5f,card.y+111,12,Muted,item.loading?"Cargando...":"Sin carátula",NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
        }
        Fill(vg,{card.x+2,card.y+147,card.w-4,37},nvgRGBA(6,20,36,245),3);
        CatalogTitle(vg,card,item.title);
        if(item.selected)Outline(vg,card,Cyan,3);
        if(item.favorite) {
            Fill(vg,{card.x+card.w-27,card.y+7,21,22},Amber,5);
            Text(vg,card.x+card.w-16.5f,card.y+18,18,Navy,"★",NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
        }
    }
    nvgRestore(vg);
    Text(vg,555,624,14,Muted,state.page.c_str(),NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
}
}
