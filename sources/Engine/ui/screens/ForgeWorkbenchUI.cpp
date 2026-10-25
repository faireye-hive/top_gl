#include "ForgeWorkbenchUI.h"
#include "../UIRenderer.h"
#include "../PanelChrome.h"
#include "../HudDrawList.h"
namespace top::ui {
void ForgeWorkbenchUI::draw(UIRenderer& r,HudDrawList& d,int w,int h,const InventoryUI& inv) const {
        if(!visible_)return;
        const Rect b=bounds(w,h);
        chrome::panel(r,b.x,b.y,b.w,b.h);
        chrome::closeButton(r,b.x+288.f,b.y+4.f,18.f,17.f);
        d.addText("Blacksmith - Oficina",b.x+11.f,b.y+8.f,.06f,.15f,.20f);
        constexpr const char* tabs[]={"Forjar","Combinar","Encaixe"};
        for(int i=0;i<3;++i){
            const float x=b.x+8.f+i*101.f;
            chrome::button(r,x,b.y+36.f,96.f,28.f,int(mode_)==i);
            d.addText(tabs[i],x+12.f,b.y+43.f,.07f,.17f,.20f);
        }
        constexpr const char* roles[3][3]={{"Equipamento","Gema","Refiner"},
            {"Pergaminho","Gema A","Gema B"},{"Equipamento","Stabilizer","Catalyst"}};
        for(int i=0;i<3;++i){
            const float x=b.x+10.f+i*102.f;
            chrome::slot(r,x,b.y+81.f,96.f,61.f);
            d.addText(roles[int(mode_)][i],x+3.f,b.y+85.f,.07f,.16f,.20f);
            const int idx=index(i,inv);
            if(idx>=0){
                const auto it=std::find_if(inv.rows().begin(),inv.rows().end(),
                    [&](const BagView& row){return row.index==idx&&row.uid==selections_[std::size_t(i)].uid;});
                if(it!=inv.rows().end()){
                    if(!it->icon.empty())d.icons.push_back({it->icon,x+3.f,b.y+104.f,30.f,30.f});
                    d.addText(it->name.substr(0,11),x+35.f,b.y+111.f,.05f,.13f,.18f);
                    d.addText("x limpar",x+36.f,b.y+127.f,.35f,.18f,.18f);
                }
            }else if(selections_[std::size_t(i)].uid){
                d.addText("Item saiu",x+5.f,b.y+112.f,.6f,.18f,.14f);
            }else d.addText("Solte aqui",x+5.f,b.y+113.f,.32f,.39f,.40f);
        }
        d.addText("Clique na bolsa em ordem ou arraste para os slots.",b.x+10.f,b.y+152.f,.13f,.25f,.27f);
        d.addText("Ouro: "+std::to_string(inv.gold()),b.x+10.f,b.y+173.f,.16f,.22f,.22f);
        if(cost_>0)d.addText("Custo: "+std::to_string(cost_)+"  |  Chance: "+
            std::to_string(chance_/100)+"%",b.x+10.f,b.y+191.f,.17f,.23f,.22f);
        if(!status_.empty())d.addText(status_.substr(0,40),b.x+10.f,b.y+214.f,
             valid_?.10f:.65f,valid_?.38f:.20f,.17f);
        if(valid_ && mode_!=Mode::Socket)
            d.addText("Falha consome ouro e materiais!",b.x+10.f,b.y+230.f,.58f,.22f,.18f);
        chrome::button(r,b.x+16.f,b.y+247.f,282.f,36.f,valid_);
        d.addText(valid_?"Revisar e confirmar":"Selecione itens validos",b.x+48.f,b.y+256.f,
                  .06f,.15f,.17f);
    }
}
