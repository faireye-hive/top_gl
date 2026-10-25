#include "top/Play.h"
#include "top/ProgressionPoints.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <utility>

namespace top {
void Play::dropLoot(const Foe& foe) {
    for (const Foe::Drop& drop : foe.drops) {
        if (drop.id <= 0 || drop.rate <= 0 || !items_.count(drop.id)) continue;
        int chance = drop.rate;
        if (chance > 10000) chance = 10000;
        if (chance < 10000 && roll(10000) >= chance) continue;
        if (loots_.size() >= 24) {
            say("O chao nao cabe mais loot.");
            return;
        }
        const float ang = static_cast<float>(roll(628)) * 0.01f;
        const float rad = 0.4f + static_cast<float>(roll(5)) * 0.08f;
        Loot loot;
        loot.id = drop.id;
        loot.x = foe.x + std::sin(ang) * rad;
        loot.y = foe.y + std::cos(ang) * rad;
        loots_.push_back(loot);
    }
}

void Play::collectRequested(float px,float py) {
    if(requestedLoot_<0 || requestedLoot_>=static_cast<int>(loots_.size())) { requestedLoot_=-1; return; }
    const Loot& loot=loots_[static_cast<std::size_t>(requestedLoot_)];
    if(std::hypot(loot.x-px,loot.y-py)>1.6f) return;
    if(!collectInstance(loot.instance.uid > 0 ? loot.instance : Stack{loot.id,1})) {
        say("Bolsa cheia. Venda ou descarte itens."); requestedLoot_=-1; return;
    }
    const auto found=items_.find(loot.id);
    if(found!=items_.end()) say("Pegou "+found->second.name);
    onHistoricalEvent(MissionEventKind::GetItem,loot.id,
                      std::max(1,loot.instance.count));
    loots_.erase(loots_.begin()+requestedLoot_);
    requestedLoot_=-1;
    auto checkItemMission = [&](HistoricalProgress& m) {
        if(m.active && !m.turnInRequired && m.targetId<=0 && m.targetX<0.f && m.targetY<0.f && missionItemsReady(m))
            completeHistoricalMission(m);
    };
    checkItemMission(historical_);
    for(auto& m:additionalMissions_)checkItemMission(m);
}

void Play::gather(float px, float py) {
    for (std::size_t i = 0; i < loots_.size();) {
        const float dx = loots_[i].x - px;
        const float dy = loots_[i].y - py;
        if (dx * dx + dy * dy > 1.15f * 1.15f) {
            ++i;
            continue;
        }
        if (!collectInstance(loots_[i].instance.uid > 0 ? loots_[i].instance : Stack{loots_[i].id,1})) {
            // O loot fica no chao. O aviso nao se repete a cada quadro, e os
            // outros itens ao alcance continuam sendo testados.
            if (log_.empty() || log_.back() != "Bolsa cheia.") say("Bolsa cheia.");
            ++i;
            continue;
        }
        const auto it = items_.find(loots_[i].id);
        if (it != items_.end()) say("Pegou " + it->second.name);
        const int collectedId=loots_[i].id;
        const int collectedCount=std::max(1,loots_[i].instance.count);
        loots_.erase(loots_.begin() + static_cast<std::ptrdiff_t>(i));
        onHistoricalEvent(MissionEventKind::GetItem,collectedId,collectedCount);
        // Item-only historical missions finish as soon as the last required
        // stack is collected, instead of waiting for the next movement tick.
        auto checkItemMission = [&](HistoricalProgress& m) {
            if(m.active && !m.turnInRequired && m.targetId<=0 && m.targetX<0.f && m.targetY<0.f && missionItemsReady(m))
                completeHistoricalMission(m);
        };
        checkItemMission(historical_);
        for(auto& m:additionalMissions_)checkItemMission(m);
    }
}

void Play::setVendor(float x, float y, const std::string& name, const std::vector<int>& stock) {
    vendorX_ = x;
    vendorY_ = y;
    vendorOn_ = true;
    vendorName_ = name.empty() ? "Vendedor" : name;
    vendorStock_ = stock;
    if (vendorStock_.empty()) vendorStock_.push_back(8);
    // A tabela inteira ja foi lida no bind. So volta ao arquivo se o estoque
    // tiver algum item que ainda nao esta carregado.
    bool missing = false;
    for (int id : vendorStock_) {
        if (!items_.count(id)) {
            missing = true;
            break;
        }
    }
    if (missing) loadItems(root_ + "/scripts/table/iteminfo.txt", vendorStock_);
    shopOffset_ = 0;
    shopOpen_ = false;
}

void Play::clearVendor() {
    vendorOn_ = false;
    vendorName_ = "Vendedor";
    vendorStock_.clear();
    shopOpen_ = false;
    shopOffset_ = 0;
}

void Play::interact(float px, float py) {
    if (deadFor_ > 0.f) return;
    if (!vendorOn_) return;
    const float dx = vendorX_ - px;
    const float dy = vendorY_ - py;
    if (dx * dx + dy * dy > 2.4f * 2.4f) {
        say("Chegue perto de " + vendorName_ + ".");
        return;
    }
    shopOpen_ = !shopOpen_;
    if (shopOpen_) npcTalkOpen_ = false;
    say(shopOpen_ ? "Loja de " + vendorName_ + " aberta." : "Loja fechada.");
}

int Play::shopHit(int x, int y, int screenW, int) const {
    if (!shopOpen_) return -1;
    const int left = screenW - 354;
    const int top = 64;
    if (x < left || x >= left + 198 || y < top || y >= top + 394) return -1;
    const int lx = x - left;
    const int ly = y - top;
    if (lx >= 172 && ly >= 2 && lx < 196 && ly < 18) return -2;
    static const int cols[] = {23, 59, 96, 133};
    static const int rows[] = {62, 98, 134, 170, 206, 242, 278, 314};
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 4; ++col) {
            if (lx < cols[col] || ly < rows[row] || lx >= cols[col] + 32 || ly >= rows[row] + 32) continue;
            const int index = shopOffset_ + row * 4 + col;
            return index < static_cast<int>(vendorStock_.size()) ? index : -1;
        }
    }
    return -1;
}

void Play::closeShop() { shopOpen_ = false; }

void Play::clickShop(int index) {
    if (!shopOpen_ || index < 0 || index >= static_cast<int>(vendorStock_.size())) return;
    const int id = vendorStock_[static_cast<std::size_t>(index)];
    const auto it = items_.find(id);
    if (it == items_.end()) {
        say("Item sem ficha no estoque.");
        return;
    }
    if (gold_ < it->second.price) {
        say("Ouro insuficiente para " + it->second.name + ".");
        return;
    }
    if (!addBag(id, 1)) {
        say("Bolsa cheia.");
        return;
    }
    gold_ -= it->second.price;
    say("Comprou " + it->second.name + " de " + vendorName_ + ".");
    save(savePath());
}

void Play::scrollShop(int rows) {
    constexpr int visible = 32;
    constexpr int step = 4;
    if (!shopOpen_ || static_cast<int>(vendorStock_.size()) <= visible) return;
    shopOffset_ += rows * step;
    int maximum = std::max(0, static_cast<int>(vendorStock_.size()) - visible);
    if (maximum % step) maximum += step - (maximum % step);
    if (shopOffset_ < 0) shopOffset_ = 0;
    if (shopOffset_ > maximum) shopOffset_ = maximum;
}

bool Play::increaseSkill(int id) {
    const auto known=learnedSkills_.find(id);
    const auto rule=skillRules_.find(id);
    if(known==learnedSkills_.end() || rule==skillRules_.end()) { say("Skill nao aprendida."); return false; }
    if(known->second>=10) { say("Skill no nivel maximo offline (10)."); return false; }
    if(tp_<=0) { say("Sem pontos de habilidade."); return false; }
    if(rule->second.level>player_.level) { say("Nivel do personagem insuficiente."); return false; }
    --tp_;
    ++known->second;
    say(rule->second.name+" nivel "+std::to_string(known->second)+". TP restante "+std::to_string(tp_));
    save(savePath());
    return true;
}

Play::WorkbenchPreview Play::previewWorkbench(int mode,int a,int b,int c) const {
    WorkbenchPreview p;
    if(a<0||b<0||c<0||a==b||a==c||b==c||
       a>=int(bag_.size())||b>=int(bag_.size())||c>=int(bag_.size())){
        p.message="Selecione tres itens diferentes";return p;
    }
    const Stack& x=bag_[std::size_t(a)], &y=bag_[std::size_t(b)], &z=bag_[std::size_t(c)];
    const auto xi=items_.find(x.id),yi=items_.find(y.id),zi=items_.find(z.id);
    if(xi==items_.end()||yi==items_.end()||zi==items_.end()){
        p.message="Item nao encontrado no catalogo";return p;
    }
    if(mode==2) {
        p.cost=socketCost(a);
        if(p.cost<0){p.message="Equipamento nao aceita encaixe";return p;}
        if(!socket_rules::materials(y.id,z.id)||y.count<1||z.count<1){
            p.message="Precisa estabilizador 890 e catalisador 891";return p;
        }
        p.chance=socket_rules::successChancePer10000(x.socketCount);
        if(p.chance<=0){p.message="Nivel de encaixe indisponivel";return p;}
    }else if(mode==0){
        if(x.count!=1||y.count!=1||z.count!=1||yi->second.type!=49||zi->second.type!=50){
            p.message="Equipamento, gema comum e Refining Gem";return p;
        }
        const auto rule=gemCatalog_.find(y.id);
        if(rule==gemCatalog_.end()||!rule->second.compatible(xi->second.type)){
            p.message="Gema incompativel com equipamento";return p;
        }
        if(y.stoneLevel<1||y.stoneLevel>rule->second.maxLevel||z.stoneLevel<1||z.stoneLevel>9){
            p.message="Nivel de gema ou refiner invalido";return p;
        }
        p.cost=forgeCost(a);
        if(p.cost<0){p.message="Abra um encaixe antes de forjar";return p;}
        int slot=-1;
        for(int i=0;i<x.socketCount;++i)if(x.gemIds[i]==y.id){slot=i;break;}
        if(slot<0)for(int i=0;i<x.socketCount;++i)if(x.gemIds[i]==0){slot=i;break;}
        if(slot<0){p.message="Sem encaixe livre";return p;}
        const int next=x.gemLevels[slot]+1;
        if(next>rule->second.maxLevel||y.stoneLevel<next||z.stoneLevel<next){
            p.message="Gema ou refiner abaixo do nivel necessario";return p;
        }
        p.chance=forge_rules::forgingChancePer10000(next);
        if(p.chance<=0){p.message="Nivel de forja indisponivel";return p;}
    }else if(mode==1){
        if(xi->second.type!=47 || (yi->second.type!=49&&yi->second.type!=50)||
           yi->second.type!=zi->second.type||y.id!=z.id || x.count<1||y.count!=1||z.count!=1||
           y.stoneLevel!=z.stoneLevel||y.stoneLevel<1||y.stoneLevel>9){
            p.message="Pergaminho e duas gemas iguais";return p;
        }
        int cap=9;
        if(yi->second.type==49){
            const auto rule=gemCatalog_.find(y.id);
            if(rule==gemCatalog_.end()){p.message="Gema desconhecida";return p;}
            cap=rule->second.maxLevel;
        }
        if(y.stoneLevel>=cap){p.message="Gema no nivel maximo";return p;}
        p.cost=5000;
        p.chance=forge_rules::combiningChancePer10000(y.stoneLevel,yi->second.type);
        if(p.chance<=0){p.message="Combinacao indisponivel";return p;}
    }else {p.message="Modo desconhecido";return p;}
    if(gold_<p.cost){p.message="Ouro insuficiente";return p;}
    p.valid=true;
    p.message="Pronto para revisar operacao";
    return p;
}

int Play::socketCost(int equipmentIndex) const {
    if (equipmentIndex<0 || equipmentIndex>=static_cast<int>(bag_.size())) return -1;
    const Stack& equipment=bag_[static_cast<std::size_t>(equipmentIndex)];
    const auto rule=items_.find(equipment.id);
    if (rule==items_.end() || equipment.count!=1 ||
        !forge_rules::validSockets(equipment.socketCount,equipment.gemIds,equipment.gemLevels)) return -1;
    return socket_rules::cost(socketConfig_,equipment.socketCount,rule->second.maxSockets,
                              rule->second.maxDurability);
}

bool Play::socketEquipment(int equipmentIndex,int stabilizerIndex,int catalystIndex){
    if(equipmentIndex<0||stabilizerIndex<0||catalystIndex<0 ||
       equipmentIndex==stabilizerIndex||equipmentIndex==catalystIndex||stabilizerIndex==catalystIndex||
       equipmentIndex>=static_cast<int>(bag_.size())||
       stabilizerIndex>=static_cast<int>(bag_.size())||
       catalystIndex>=static_cast<int>(bag_.size())){
        say("Perfuração: indices da bolsa invalidos.");return false;
    }
    const Stack& original=bag_[static_cast<std::size_t>(equipmentIndex)];
    const Stack& stabilizer=bag_[static_cast<std::size_t>(stabilizerIndex)];
    const Stack& catalyst=bag_[static_cast<std::size_t>(catalystIndex)];
    const int price=socketCost(equipmentIndex);
    if(price<0){say("Perfuração: equipamento sem capacidade de encaixes ou apparel.");return false;}
    if(!socket_rules::materials(stabilizer.id,catalyst.id) ||
       stabilizer.count<1 || catalyst.count<1){
        say("Perfuração: precisa de Equipment Stabilizer (890) e Equipment Catalyst (891).");return false;
    }
    if(gold_<price){say("Perfuração: ouro insuficiente ("+std::to_string(price)+").");return false;}
    if(socket_rules::successChancePer10000(original.socketCount)!=10000)return false;
    // Transacao totalmente validada antes de cobrar/remover materiais.
    // UID e gemas permanecem associados ao mesmo equipamento.
    const int equipUid=original.uid;
    gold_-=price;
    ++bag_[static_cast<std::size_t>(equipmentIndex)].socketCount;
    for(int index : {std::max(stabilizerIndex,catalystIndex),std::min(stabilizerIndex,catalystIndex)}){
        Stack& consumed=bag_[static_cast<std::size_t>(index)];
        if(--consumed.count<=0)bag_.erase(bag_.begin()+index);
    }
    const auto equipped=std::find_if(bag_.begin(),bag_.end(),[&](const Stack& s){return s.uid==equipUid;});
    if(equipped==bag_.end()){say("Perfuração: erro de identidade.");return false;}
    say("Perfuração: " + std::to_string(equipped->socketCount) + " encaixe(s) aberto(s).");
    save(savePath());
    return true;
}

int Play::forgeCost(int index) const {
    if(index<0||index>=static_cast<int>(bag_.size()))return -1;
    const Stack& item=bag_[static_cast<std::size_t>(index)];
    const auto found=items_.find(item.id);
    if(found==items_.end()||item.count!=1||item.socketCount<1||item.socketCount>3)return -1;
    if(!forge_rules::validSockets(item.socketCount,item.gemIds,item.gemLevels))return -1;
    return forge_rules::forgingCost(item.gemLevels);
}

bool Play::forgeBag(int equipmentIndex,int gemIndex,int refinerIndex){
    // Identidade de instancia e IDs, nao so IDs de tipos: consumiveis nunca
    // sao confundidos com uma segunda copia da mesma espada.
    if(equipmentIndex<0||gemIndex<0||refinerIndex<0 ||
       equipmentIndex==gemIndex||equipmentIndex==refinerIndex||gemIndex==refinerIndex||
       equipmentIndex>=static_cast<int>(bag_.size())||
       gemIndex>=static_cast<int>(bag_.size())||refinerIndex>=static_cast<int>(bag_.size())){
        say("Forja: indices da bolsa invalidos.");return false;
    }
    Stack& equipment=bag_[static_cast<std::size_t>(equipmentIndex)];
    const Stack gem=bag_[static_cast<std::size_t>(gemIndex)];
    const Stack refiner=bag_[static_cast<std::size_t>(refinerIndex)];
    const auto ei=items_.find(equipment.id), gi=items_.find(gem.id), ri=items_.find(refiner.id);
    if(ei==items_.end()||gi==items_.end()||ri==items_.end()||
       gi->second.type!=49||ri->second.type!=50||gem.count!=1||refiner.count!=1||
       equipment.count!=1){say("Forja: precisa de equipamento, gema tipo 49 e refiner tipo 50.");return false;}
    const auto rule=gemCatalog_.find(gem.id);
    if(rule==gemCatalog_.end()||!rule->second.compatible(ei->second.type)){
        say("Forja: gema nao compativel com este equipamento.");return false;
    }
    if(gem.stoneLevel<1||gem.stoneLevel>rule->second.maxLevel||
       refiner.stoneLevel<1||refiner.stoneLevel>9){say("Forja: nivel de pedra invalido.");return false;}
    const int price=forgeCost(equipmentIndex);
    if(price<0){say("Forja: equipamento sem encaixes abertos.");return false;}
    int slot=-1;
    for(int i=0;i<equipment.socketCount;++i){
        if(equipment.gemIds[i]==gem.id){slot=i;break;}
    }
    if(slot<0){
        for(int i=0;i<equipment.socketCount;++i)
            if(equipment.gemIds[i]==0){slot=i;break;}
    }
    if(slot<0){say("Forja: nenhum encaixe livre.");return false;}
    const int nextLevel=equipment.gemLevels[slot]+1;
    if(nextLevel>rule->second.maxLevel || gem.stoneLevel<nextLevel || refiner.stoneLevel<nextLevel){
        say("Forja: nivel da gema ou refiner insuficiente.");return false;
    }
    if(price>gold_){say("Forja: ouro insuficiente ("+std::to_string(price)+").");return false;}
    const int chance=forge_rules::forgingChancePer10000(nextLevel);
    if(chance<=0){say("Forja: nivel nao suportado.");return false;}
    // Transacao: cobra o ouro, consome UMA gema e UM refiner tanto em
    // sucesso quanto falha. Equipamento nunca e destruido pelo sorteio.
    gold_-=price;
    const bool success=roll(10000)<chance;
    if(success){
        equipment.gemIds[slot]=gem.id;
        equipment.gemLevels[slot]=nextLevel;
        int total=0;
        for(int lv:equipment.gemLevels)total+=lv;
        equipment.forgeLevel=std::min(15,total); // teto herdado do campo Y1
    }
    for(int index : {std::max(gemIndex,refinerIndex),std::min(gemIndex,refinerIndex)}){
        Stack& consumed=bag_[static_cast<std::size_t>(index)];
        if(--consumed.count<=0)bag_.erase(bag_.begin()+index);
    }
    if(sheetReady_)applyAttrs(false);else recomputeGear();
    say(success?"Forja: gema aplicada (nivel "+std::to_string(nextLevel)+").":"Forja falhou: equipamento preservado; pedras consumidas.");
    save(savePath());
    return success;
}

bool Play::combineGems(int scrollIndex,int leftIndex,int rightIndex){
    if(scrollIndex<0||leftIndex<0||rightIndex<0||
       scrollIndex==leftIndex||scrollIndex==rightIndex||leftIndex==rightIndex||
       scrollIndex>=static_cast<int>(bag_.size())||
       leftIndex>=static_cast<int>(bag_.size())||rightIndex>=static_cast<int>(bag_.size())){
        say("Combinar: indices da bolsa invalidos.");return false;
    }
    const Stack scroll=bag_[static_cast<std::size_t>(scrollIndex)];
    const Stack left=bag_[static_cast<std::size_t>(leftIndex)];
    const Stack right=bag_[static_cast<std::size_t>(rightIndex)];
    const auto si=items_.find(scroll.id),li=items_.find(left.id),ri=items_.find(right.id);
    if(si==items_.end()||li==items_.end()||ri==items_.end()||
       si->second.type!=47 ||
       (li->second.type!=49 && li->second.type!=50) ||
       li->second.type!=ri->second.type ||left.id!=right.id ||
       scroll.count<1||left.count!=1||right.count!=1 ||
       left.stoneLevel!=right.stoneLevel ||left.stoneLevel<1 || left.stoneLevel>9){
        say("Combinar: precisa de pergaminho e duas gemas iguais no mesmo nivel.");return false;
    }
    int cap=9;
    if(li->second.type==49){
        const auto rule=gemCatalog_.find(left.id);
        if(rule==gemCatalog_.end()){
            say("Combinar: tipo de gema desconhecido.");return false;
        }
        cap=rule->second.maxLevel;
    }
    if(left.stoneLevel>=cap){say("Combinar: gema ja no nivel maximo.");return false;}
    if(gold_<5000){say("Combinar: faltam 5000 de ouro.");return false;}
    const int chance=forge_rules::combiningChancePer10000(left.stoneLevel,li->second.type);
    if(chance<=0)return false;
    gold_-=5000;
    const bool success=roll(10000)<chance;
    const int leftUid=left.uid;
    const int newLevel=left.stoneLevel+1;
    // Em falha forge.lua remove os dois cristais e o pergaminho.
    // Em sucesso a copia da esquerda conserva o UID e recebe +1 nivel.
    std::array<int,3> removals{scrollIndex,rightIndex,success?-1:leftIndex};
    std::sort(removals.begin(),removals.end(),std::greater<int>());
    for(int index:removals){
        if(index<0)continue;
        auto& item=bag_[static_cast<std::size_t>(index)];
        if(--item.count<=0)bag_.erase(bag_.begin()+index);
    }
    if(success){
        const auto found=std::find_if(bag_.begin(),bag_.end(),[&](const Stack& x){return x.uid==leftUid;});
        if(found!=bag_.end())found->stoneLevel=newLevel;
    }
    say(success?"Combinacao bem-sucedida: gema nivel "+std::to_string(newLevel)+".":
        "Combinacao falhou: gemas e pergaminho consumidos.");
    save(savePath());
    return success;
}

bool Play::gmOpenSockets(int index,int sockets){
    if(index<0||index>=static_cast<int>(bag_.size())||sockets<1||sockets>3)return false;
    Stack& item=bag_[static_cast<std::size_t>(index)];
    const auto it=items_.find(item.id);
    if(it==items_.end()||item.count!=1)return false;
    // Classes de equipamento permitidas em Item_CanJinglian_ID, variable.lua.
    static const int types[]={1,2,3,4,7,11,20,22,23,24,27,9,25,26,81,82,83,88};
    if(std::find(std::begin(types),std::end(types),it->second.type)==std::end(types))return false;
    if(sockets<item.socketCount)return false;
    item.socketCount=sockets;
    say("GM offline: "+std::to_string(sockets)+" encaixes de teste abertos em "+it->second.name);
    save(savePath());return true;
}
void Play::gmAddGold(int amount){
    if(amount<=0)return;
    gold_=static_cast<int>(std::min(1000000000LL,static_cast<long long>(gold_)+amount));
    say("GM offline: +"+std::to_string(amount)+" ouro.");
    save(savePath());
}

void Play::gmAddExperience(int amount) {
    if(amount<=0) return;
    exp_=std::min(1000000000,exp_+amount);
    while(expNext_>0 && exp_>=expNext_ && player_.level<100) {
        exp_-=expNext_;
        ++player_.level;
        ap_+=progression::attrPointAt(player_.level);
        tp_+=progression::skillPointAt(player_.level);
        if(sheetReady_) applyAttrs(false);
        player_.hp=player_.maxHp;
        player_.homeY=player_.homeX;
        expNext_=experience_.needed(player_.level);
    }
    say("GM offline: +"+std::to_string(amount)+" XP; nivel "+std::to_string(player_.level));
    save(savePath());
}

bool Play::gmGiveItem(int id,int count) {
    if(id<=0 || count<1 || count>99 || !items_.count(id)) { say("GM offline: ID de item invalido."); return false; }
    if(!addBag(id,count)) { say("GM offline: bolsa cheia."); return false; }
    say("GM offline: +"+std::to_string(count)+" "+items_.at(id).name);
    save(savePath());
    return true;
}

void Play::gmHeal() {
    player_.hp=player_.maxHp;
    player_.homeY=player_.homeX;
    deadFor_=0.f;
    say("GM offline: HP/SP restaurados.");
    save(savePath());
}

} // namespace top
