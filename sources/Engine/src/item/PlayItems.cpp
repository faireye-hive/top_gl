#include "top/Play.h"
#include "top/JobNames.h"
#include "top/ItemUseRules.h"
#include "top/ItemChestRules.h"
#include "top/ItemTicketRules.h"
#include "top/ItemNewbieChestRules.h"
#include "top/ItemInstantiationRules.h"
#include "top/SavePaths.h"
#include "top/InventoryStackRules.h"
#include "top/InventoryCapacity.h"
#include <algorithm>
#include <unordered_set>
#include <string>

namespace top {
bool Play::addStack(std::vector<Stack>& stacks, int id, int count, int limit, int slots) {
    return inventory_rules::insert(stacks, id, count, limit, slots);
}
void Play::assignItemUids() {
    // UID unico entre bolsa principal e temporaria; dados corrompidos nunca
    // podem equipar duas instancias usando o mesmo identificador.
    std::unordered_set<int> used;
    auto fix = [&](std::vector<Stack>& stacks) {
        for (Stack& stack : stacks) {
            if (stack.uid <= 0 || stack.uid > 1000000000 || !used.insert(stack.uid).second) {
                while (used.count(nextItemUid_) && nextItemUid_ < 1000000000) ++nextItemUid_;
                stack.uid = nextItemUid_++;
                used.insert(stack.uid);
            }
            if (stack.uid >= nextItemUid_) nextItemUid_ = stack.uid + 1;
        }
    };
    fix(bag_);
    fix(temp_);
}

bool Play::addBag(int id, int count) {
    const auto it=items_.find(id);
    const int cap=it==items_.end()?1:it->second.maxStack;
    if (!addStack(bag_,id,count,cap,bagCapacity_)) return false;
    assignItemUids();
    return true;
}

bool Play::collectInstance(const Stack& instance) {
    const auto it = items_.find(instance.id);
    const int maxStack = it == items_.end() ? 1 : it->second.maxStack;
    if (instance.id <= 0 || instance.count != 1 || instance.uid <= 0 || maxStack > 1)
        return addBag(instance.id, instance.count);
    if (bag_.size() >= static_cast<std::size_t>(bagCapacity_)) return false;
    if (std::any_of(bag_.begin(), bag_.end(), [&](const Stack& s) { return s.uid == instance.uid; }))
        return false;
    bag_.push_back(instance);
    assignItemUids();
    return true;
}

// Associate identical non-stackable items with separate equipped slots.
int Play::equippedSlotAtBagIndex(int index) const {
    if (index < 0 || index >= static_cast<int>(bag_.size())) return -1;
    const Stack& current = bag_[static_cast<std::size_t>(index)];
    for (const auto& entry : wornUid_) {
        if (entry.second == current.uid && worn_.count(entry.first) && worn_.at(entry.first) == current.id)
            return entry.first;
    }
    // Legado: slots que nao possuem UID continuam identificados por ordem.
    const int id=current.id;
    int previous=0;
    for(int i=0;i<index;++i) if(bag_[static_cast<std::size_t>(i)].id==id) ++previous;
    std::vector<int> slots;
    for(const auto& entry:worn_) if(entry.second==id && !wornUid_.count(entry.first)) slots.push_back(entry.first);
    std::sort(slots.begin(),slots.end());
    // Sem fallback se todos os slots desse ID ja tem UID proprio.
    return previous<static_cast<int>(slots.size())?slots[static_cast<std::size_t>(previous)]:-1;
}

int Play::bagCount(int id) const {
    int total = 0;
    for (const Stack& stack : bag_) if (stack.id == id) total += stack.count;
    return total;
}

void Play::removeBag(int id, int count) {
    for (std::size_t i = 0; i < bag_.size() && count > 0;) {
        Stack& stack = bag_[i];
        if (stack.id != id) {
            ++i;
            continue;
        }
        const int used = std::min(count, stack.count);
        stack.count -= used;
        count -= used;
        if (stack.count == 0) bag_.erase(bag_.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
    }
}

bool Play::hasBag(int id) const {
    for (const Stack& stack : bag_) {
        if (stack.id == id && stack.count > 0) return true;
    }
    return false;
}

bool Play::hasBankItem(int id) const {
    return std::any_of(bank_.begin(), bank_.end(), [id](const Stack& stack) {
        return stack.id == id && stack.count > 0;
    });
}

Play::StarActionResult Play::grantSennaUnityChest() {
    if (player_.level >= 41 || hasBag(1033) || hasBag(1034) ||
        hasBankItem(1033) || hasBankItem(1034) || isWorn(1033) || isWorn(1034)) {
        say("Senna: voce nao atende aos requisitos do evento Star of Unity.");
        return StarActionResult::Ineligible;
    }
    const auto item = items_.find(1033);
    if (item == items_.end()) {
        say("Senna: dados locais do Star of Unity Chest indisponiveis.");
        return StarActionResult::DataMissing;
    }
    std::vector<Stack> preview = bag_;
    if (!inventory_rules::insert(preview, 1033, 1, item->second.maxStack, bagCapacity_)) {
        say("Senna: libere espaco na bolsa para receber o Star of Unity Chest.");
        return StarActionResult::NoSpace;
    }
    auto reward = std::find_if(preview.rbegin(), preview.rend(), [](const Stack& stack) { return stack.id == 1033; });
    if (reward == preview.rend()) return StarActionResult::DataMissing;
    reward->quality = 4; // TriggerAction(1, GiveItem, 1033, 1, 4)
    bag_.swap(preview);
    assignItemUids();
    onHistoricalEvent(MissionEventKind::GetItem, 1033, 1);
    say("Senna entregou um Star of Unity Chest.");
    save(savePath());
    return StarActionResult::Success;
}

Play::StarActionResult Play::redeemTeamStar() {
    // Transfer_TeamStar in calculate/forge.lua: this is an explicit second-
    // advancement exchange, not a general-purpose script callback.
    if (player_.level < 41 || bagCount(1034) != 1 || hasBankItem(1034) || isWorn(1034)) {
        say("Resgate exige nivel 41+, exatamente 1 Star of Unity na bolsa e nenhum no cofre/equipamento.");
        return StarActionResult::Ineligible;
    }
    static const std::unordered_map<int, int> rewards = {
        {8, 1382}, {9, 1392}, {12, 1409}, {13, 1433}, {14, 1467}, {16, 1419}
    };
    const auto rewardId = rewards.find(job_);
    if (rewardId == rewards.end()) {
        say("Star of Unity so pode ser resgatado por uma classe de segunda promocao.");
        return StarActionResult::UnsupportedClass;
    }
    const auto item = items_.find(rewardId->second);
    if (item == items_.end() || item->second.maxStack != 1) {
        say("Arma historica desta classe indisponivel nos dados locais.");
        return StarActionResult::DataMissing;
    }
    std::vector<Stack> preview = bag_;
    for (std::size_t i = 0; i < preview.size(); ++i) {
        if (preview[i].id == 1034) { preview.erase(preview.begin() + static_cast<std::ptrdiff_t>(i)); break; }
    }
    if (!inventory_rules::insert(preview, rewardId->second, 1, item->second.maxStack, bagCapacity_)) {
        say("Bolsa sem espaco para a arma resgatada; Star of Unity preservado.");
        return StarActionResult::NoSpace;
    }
    auto reward = std::find_if(preview.rbegin(), preview.rend(), [&](const Stack& stack) {
        return stack.id == rewardId->second;
    });
    if (reward == preview.rend()) return StarActionResult::DataMissing;
    reward->quality = 22; // GiveItem(..., itemId, 1, 22) no callback original.
    bag_.swap(preview);
    assignItemUids();
    onHistoricalEvent(MissionEventKind::GetItem, rewardId->second, 1);
    say("Star of Unity trocado pela arma historica da classe.");
    save(savePath());
    return StarActionResult::Success;
}

bool Play::isWorn(int id) const {
    for (const auto& worn : worn_) {
        if (worn.second == id) return true;
    }
    return false;
}


std::optional<Play::TicketTravel> Play::takeTicketTravelRequest() {
    auto result = pendingTicketTravel_;
    pendingTicketTravel_.reset();
    return result;
}
bool Play::consumeTicketAfterTravel(int itemUid,int itemId) {
    if (itemUid <= 0 || itemId <= 0) return false;
    const auto it = std::find_if(bag_.begin(),bag_.end(),[&](const Stack& s) {
        return s.uid == itemUid && s.id == itemId && s.count > 0;
    });
    if (it == bag_.end()) return false;
    const auto definition = items_.find(itemId);
    if (definition == items_.end() || definition->second.scriptAction != "Ticket" ||
        !item_tickets::destinationFor(itemId, definition->second.scriptAction)) return false;
    if (--it->count == 0) bag_.erase(it);
    say("Ticket utilizado. Chegada confirmada.");
    save(savePath());
    return true;
}
void Play::reportTicketTravelFailed() {
    pendingTicketTravel_.reset();
    say("Viagem indisponivel: mapa de destino ausente ou invalido. Ticket preservado.");
}

void Play::clickBag(int index) {
    if (index < 0 || index >= static_cast<int>(bag_.size())) return;
    Stack& stack = bag_[static_cast<std::size_t>(index)];
    const auto it = items_.find(stack.id);
    if (it == items_.end()) return;
    const ItemInfo& item = it->second;
    const int newCapacity = inventory_rules::upgradeTarget(item.id);
    if (newCapacity > 0) {
        if (!inventory_rules::canUpgrade(item.id,bagCapacity_)) {
            say("Expansao exige bolsa com " + std::to_string(newCapacity-4) + " espacos.");
            return;
        }
        bagCapacity_ = newCapacity;
        --stack.count;
        if (stack.count <= 0) bag_.erase(bag_.begin()+index);
        say("Bolsa ampliada para " + std::to_string(bagCapacity_) + " espacos.");
        save(savePath());
        return;
    }
    if (item.id == 1033 && item.scriptAction == "ItemUse_TunDuiZhiXing") {
        const auto starInfo = items_.find(1034);
        if (starInfo == items_.end() || starInfo->second.maxStack != 1) {
            say("Star of Unity Chest: item 1034 ausente ou invalido; caixa preservada.");
            return;
        }
        std::vector<Stack> preview = bag_;
        Stack& consumed = preview[static_cast<std::size_t>(index)];
        if (--consumed.count <= 0) preview.erase(preview.begin() + index);
        if (!inventory_rules::insert(preview, 1034, 1, starInfo->second.maxStack, bagCapacity_)) {
            say("Libere espaco na bolsa para abrir o Star of Unity Chest.");
            return;
        }
        auto star = std::find_if(preview.rbegin(), preview.rend(), [](const Stack& value) { return value.id == 1034; });
        if (star == preview.rend()) return;
        star->quality = 4;
        star->durability = 100;   // SetItemAttr(ITEMATTR_URE, 100)
        star->maxDurability = 20100; // SetItemAttr(ITEMATTR_MAXURE, 20100)
        bag_.swap(preview);
        assignItemUids();
        onHistoricalEvent(MissionEventKind::GetItem, 1034, 1);
        say("Star of Unity Chest aberto: voce recebeu um Star of Unity.");
        save(savePath());
        return;
    }
    const auto book = skillBooks_.find(item.id);
    if (book != skillBooks_.end()) {
        const int skillId = book->second;
        const auto ruleIt = skillRules_.find(skillId);
        if (ruleIt == skillRules_.end()) {
            say("Livro sem regra de skill: " + item.name + ".");
            return;
        }
        const SkillRule& rule = ruleIt->second;
        if (learnedSkills_.count(skillId)) {
            say("Voce ja aprendeu " + rule.name + ".");
            return;
        }
        if (!rule.jobs.empty() && std::find(rule.jobs.begin(), rule.jobs.end(), job_) == rule.jobs.end()) {
            say(rule.name + " nao pertence a classe " + std::string(jobName(job_)) + ".");
            return;
        }
        if (player_.level < rule.level) {
            say(rule.name + " exige nivel " + std::to_string(rule.level) + ".");
            return;
        }
        // find, nao operator[]: o operator[] criava uma entrada de nivel 0 para
        // o pre-requisito, e castSkill (que testa count) passava a tratar a
        // skill como aprendida sem o livro.
        if (rule.prerequisite > 0) {
            const auto have = learnedSkills_.find(rule.prerequisite);
            if (have == learnedSkills_.end() || have->second < rule.prerequisiteLevel) {
                const auto prior = skillRules_.find(rule.prerequisite);
                const std::string priorName = prior == skillRules_.end() ? std::to_string(rule.prerequisite) : prior->second.name;
                say("Primeiro aprenda " + priorName + " nivel " + std::to_string(rule.prerequisiteLevel) + ".");
                return;
            }
        }
        if (tp_ < rule.points) {
            say("Faltam pontos de skill para " + rule.name + ".");
            return;
        }
        tp_ -= rule.points;
        learnedSkills_[skillId] = 1;
        stack.count -= 1;
        if (stack.count <= 0) bag_.erase(bag_.begin() + index);
        say("Aprendeu " + rule.name + ".");
        // Learning is separate from equipping a hotkey; the player drags it.
        save(savePath());
        return;
    }
    // 05-AY: ticket = transicao de mapa, nunca se comporta como bau.
    // O item so sai da bolsa APOS o frontend realizar a transicao.
    if (item.scriptAction == "Ticket") {
        const auto destination = item_tickets::destinationFor(item.id,item.scriptAction);
        if (!destination) {
            say("Ticket sem destino historico validado: " + item.name + ".");
            return;
        }
        const item_tickets::Conditions conditions{player_.level,
            static_cast<float>(player_.hp),static_cast<float>(player_.maxHp),
            player_.homeY,player_.homeX,usingTicketBoat_,worldMap_};
        switch (item_tickets::check(conditions)) {
            case item_tickets::Rejection::Boat:
                say("Nao use tickets enquanto estiver no barco."); return;
            case item_tickets::Rejection::Health:
                say("Ticket exige pelo menos 50% de HP e SP."); return;
            case item_tickets::Rejection::ProhibitedMap:
                say("Este mapa nao permite usar tickets."); return;
            case item_tickets::Rejection::InvalidLevel:
                say("Nivel invalido para utilizar ticket."); return;
            case item_tickets::Rejection::None: break;
        }
        pendingTicketTravel_ = TicketTravel{stack.uid,stack.id,
            destination->city,destination->map,destination->x,destination->y};
        say("Ticket: viajando para " + pendingTicketTravel_->city + ".");
        return;
    }
    // 05-AZ: caixas de progressao do NewbieChest historico (436..442).
    // Somente executa transacao apos validar nivel, profissao, personagem,
    // barco, vagas e cada recompensa. 443 usa MakeItem especial nao migrado.
    if (item_newbie_chests::isHistoricalNewbieChest(item.id,item.scriptAction)) {
        const auto result=item_newbie_chests::evaluate(item.id,item.scriptAction,
                                                      player_.level,job_,player_.id,usingTicketBoat_);
        if (!result) {
            using Failure=item_newbie_chests::Failure;
            switch (result.failure) {
                case Failure::Boat: say("Nao abra Newbie Chest enquanto estiver no barco.");break;
                case Failure::Level: say(item.name + ": nivel insuficiente.");break;
                case Failure::Job: say(item.name + ": exige promocao de classe compativel.");break;
                case Failure::CharacterType: say(item.name + ": profissao/tipo nao compativel com recompensa historica.");break;
                default: say(item.name + ": efeito historico especial ainda nao suportado; item preservado.");break;
            }
            return;
        }
        const auto& plan=result.plan;
        const int freeSlots=bagCapacity_-static_cast<int>(bag_.size());
        if (freeSlots<plan.minimumFreeSlots) {
            say(item.name + ": exige ao menos " + std::to_string(plan.minimumFreeSlots) + " espacos livres.");
            return;
        }
        std::vector<Stack> preview=bag_;
        auto& consumed=preview[static_cast<std::size_t>(index)];
        if (--consumed.count<=0) preview.erase(preview.begin()+index);
        std::vector<std::size_t> generatedSlots;
        for (const auto& reward:plan.rewards) {
            const auto found=items_.find(reward.itemId);
            if (found==items_.end() || reward.quantity<=0 ||
                !inventory_rules::insert(preview,reward.itemId,reward.quantity,
                                         found->second.maxStack,bagCapacity_)) {
                say(item.name + ": recompensa indisponivel ou espaco insuficiente; caixa preservada.");
                return;
            }
            if (reward.itemEvent==11 || reward.itemEvent==95) {
                if (reward.quantity!=1 || !found->second.chInstance || found->second.maxStack!=1 ||
                    preview.empty()) {
                    say(item.name + ": instancia historica indisponivel; caixa preservada.");
                    return;
                }
                generatedSlots.push_back(preview.size()-1);
            }
        }
        // Os UIDs temporários são calculados antes do commit para que uma
        // recompensa inválida jamais consuma a caixa. O mesmo UID semeia a
        // geração offline e fica salvo junto dos atributos resultantes.
        std::unordered_set<int> usedUids;
        for (const Stack& stack:preview) if(stack.uid>0) usedUids.insert(stack.uid);
        for (const Stack& stack:temp_) if(stack.uid>0) usedUids.insert(stack.uid);
        int candidateUid=std::max(1,nextItemUid_);
        std::size_t generatedAt=0;
        for (const auto& reward:plan.rewards) {
            if (reward.itemEvent!=11 && reward.itemEvent!=95) continue;
            const std::size_t slot=generatedSlots[generatedAt++];
            Stack& stack=preview[slot];
            while(candidateUid<=1000000000 && usedUids.count(candidateUid)) ++candidateUid;
            if(candidateUid<=0 || candidateUid>1000000000) {
                say(item.name + ": sem UID livre para a recompensa; caixa preservada.");
                return;
            }
            stack.uid=candidateUid++;
            usedUids.insert(stack.uid);
            const ItemInfo& info=items_.at(reward.itemId);
            const auto event=static_cast<item_instantiation::HistoricalEvent>(reward.itemEvent);
            const auto instance=item_instantiation::materialize(event,info.type,info.level,stack.uid,
                                                                 info.stats,info.statsMaximum,
                                                                 info.maxDurability,info.maxDurabilityRange,
                                                                 info.maxEnergy,info.maxEnergyRange);
            if(!instance.valid) {
                say(item.name + ": gerador historico ainda nao cobre uma recompensa; caixa preservada.");
                return;
            }
            stack.quality=instance.quality;
            stack.maxDurability=instance.maxDurability;
            stack.durability=instance.maxDurability;
            stack.maxEnergy=instance.maxEnergy;
            stack.energy=instance.maxEnergy;
            stack.extra+=instance.bonus;
        }
        // A transacao ja foi validada: ouro e itens so mudam juntos.
        bag_.swap(preview);
        assignItemUids();
        gold_=static_cast<int>(std::min(1000000000LL,static_cast<long long>(gold_)+plan.gold));
        for (const auto& reward:plan.rewards)
            onHistoricalEvent(MissionEventKind::GetItem,reward.itemId,reward.quantity);
        say("Abriu " + item.name + " e recebeu " + std::to_string(plan.gold) + " ouro.");
        save(savePath());
        return;
    }
    // 05-AX: baus com recompensa deterministica auditada em ItemEffect.lua.
    // A transacao inteira e simulada em uma copia antes de tocar na bolsa real:
    // nao consome bau se faltar espaco, se o item historico nao estiver no
    // catalogo, ou se qualquer uma das recompensas nao couber.
    if (const auto chest = item_chests::planFor(item.id, item.scriptAction, player_.id)) {
        const int freeSlots = bagCapacity_ - static_cast<int>(bag_.size());
        if (freeSlots < chest->requiredFreeSlots) {
            say("Precisa de " + std::to_string(chest->requiredFreeSlots) + " espacos livres para abrir " + item.name + ".");
            return;
        }
        std::vector<Stack> preview = bag_;
        Stack& consumed = preview[static_cast<std::size_t>(index)];
        if (--consumed.count <= 0) preview.erase(preview.begin() + index);
        for (const auto& grant : chest->rewards) {
            const auto found = items_.find(grant.itemId);
            if (found == items_.end() || grant.count <= 0 ||
                !inventory_rules::insert(preview, grant.itemId, grant.count,
                                         found->second.maxStack, bagCapacity_)) {
                say("Nao foi possivel abrir " + item.name + ": recompensa sem dados ou bolsa cheia.");
                return;
            }
        }
        bag_.swap(preview);
        assignItemUids();
        for (const auto& grant : chest->rewards)
            onHistoricalEvent(MissionEventKind::GetItem, grant.itemId, grant.count);
        say("Abriu " + item.name + ".");
        save(savePath());
        return;
    }
    if (item_chests::characterRestrictedBox(item.id,item.scriptAction)) {
        say(item.name + ": baú Relic incompatível com o tipo deste personagem.");
        return; // Sem consumo e sem recompensas.
    }
    const bool gear = item_rules::isEquipment(item.slot,item.equipSlots,item.minAtk,item.maxAtk,item.def);
    if (item.healHp > 0 || item.healSp > 0) {
        const bool hpFull = player_.hp >= player_.maxHp || item.healHp <= 0;
        const bool spFull = player_.homeY >= player_.homeX || item.healSp <= 0;
        if (hpFull && spFull) {
            say(item.name + " nao adianta agora.");
            return;
        }
        player_.hp = std::min(player_.maxHp, player_.hp + item.healHp);
        player_.homeY = std::min(player_.homeX, player_.homeY + static_cast<float>(item.healSp));
        say("Usou " + item.name + ".");
    } else if (gear) {
        equipBagToSlot(index, -1);
        return;
    } else {
        if (item_rules::hasUnsupportedScriptAction(item.scriptAction))
            say(item.name + ": funcao historica " + item.scriptAction + " ainda nao implementada offline.");
        else
            say("Item guardado. Arraste para loja ou para fora da bolsa e confirme.");
        return;
    }
    stack.count -= 1;
    if (stack.count <= 0) bag_.erase(bag_.begin() + index);
    save(savePath());
}

bool Play::equipBagToSlot(int index, int preferredSlot) {
    if (index < 0 || index >= static_cast<int>(bag_.size())) return false;
    Stack& stack = bag_[static_cast<std::size_t>(index)];
    const auto found = items_.find(stack.id);
    if (found == items_.end()) return false;
    const ItemInfo& item = found->second;
    if (!item_rules::isEquipment(item.slot,item.equipSlots,item.minAtk,item.maxAtk,item.def)) return false;
    // JobType 8 (Champion) explicitly gives up shields for two-handed combat
    // in the historical StringSet. ItemInfo type 11 is the shield category.
    if (job_ == 8 && item.type == 11) {
        say("Champion nao pode usar escudos.");
        return false;
    }
    auto allowed = [](const std::vector<int>& list, int value) {
        return list.empty() || std::find(list.begin(),list.end(),-1)!=list.end() ||
               std::find(list.begin(),list.end(),value)!=list.end();
    };
    if (!allowed(item.races,player_.id)) {
        say(item.name + " nao serve em " + player_.name + "."); return false;
    }
    if (!allowed(item.jobs,job_)) {
        say(item.name + " nao pertence a classe " + std::string(jobName(job_)) + "."); return false;
    }
    if (item.level > player_.level) {
        say("Precisa do nivel " + std::to_string(item.level) + "."); return false;
    }
    const int already=equippedSlotAtBagIndex(index);
    if (already>=0) {
        // A mesma instancia permanece unica, nao migra implicitamente entre maos.
        worn_.erase(already); wornUid_.erase(already);
        say("Tirou " + item.name + ".");
    } else {
        std::vector<int> locations=item.equipSlots;
        if (locations.empty() && item.slot>=0) locations.push_back(item.slot);
        if (locations.empty()) { say(item.name + " nao tem encaixe."); return false; }
        int selected=-1;
        if (preferredSlot >= 0) {
            if (!item_rules::canEquipSlot(item.slot,item.equipSlots,preferredSlot))return false;
            selected=preferredSlot;
        } else if (item.requiredEquipSlots.size() > 1 &&
                   std::find(locations.begin(), locations.end(), item.slot) != locations.end()) {
            // ItemInfo col. 29 marks two-hand equipment (e.g. bows and the
            // Two Handed Sword). Keep its authored primary hand, not whichever
            // hand happens to be empty.
            selected=item.slot;
        } else {
            for(int location:locations)if(!worn_.count(location)){selected=location;break;}
            if(selected<0)selected=locations.front();
        }
        std::vector<int> displaced;
        for (const auto& equipped : worn_) {
            if (equipped.first == selected ||
                std::find(item.requiredEquipSlots.begin(), item.requiredEquipSlots.end(), equipped.first) !=
                    item.requiredEquipSlots.end()) {
                displaced.push_back(equipped.first);
                continue;
            }
            const auto existing = items_.find(equipped.second);
            if (existing != items_.end() &&
                std::find(existing->second.requiredEquipSlots.begin(), existing->second.requiredEquipSlots.end(),
                          selected) != existing->second.requiredEquipSlots.end())
                displaced.push_back(equipped.first);
        }
        for (int slot : displaced) {
            const auto removed = worn_.find(slot);
            if (removed == worn_.end()) continue;
            const auto removedItem = items_.find(removed->second);
            if (removedItem != items_.end()) say("Desequipou " + removedItem->second.name + ".");
            worn_.erase(removed);
            wornUid_.erase(slot);
        }
        worn_[selected]=item.id;
        wornUid_[selected]=stack.uid;
        say("Equipou " + item.name + ".");
    }
    if (sheetReady_) applyAttrs(false); else recomputeGear();
    save(savePath());
    return true;
}

int Play::bagItemUid(int index) const {
    return index<0||index>=static_cast<int>(bag_.size()) ? 0 : bag_[static_cast<std::size_t>(index)].uid;
}
int Play::bagItemId(int index) const {
    return index < 0 || index >= static_cast<int>(bag_.size()) ? 0 : bag_[static_cast<std::size_t>(index)].id;
}
std::string Play::bagItemName(int index) const {
    const int id=bagItemId(index);
    const auto item=items_.find(id);
    return item == items_.end() ? std::to_string(id) : item->second.name;
}
bool Play::sellBag(int index) {
    if(!shopOpen_ || index<0 || index>=static_cast<int>(bag_.size())) return false;
    if(equippedSlotAtBagIndex(index)>=0) { say("Desequipe antes de vender."); return false; }
    Stack& stack=bag_[static_cast<std::size_t>(index)];
    const auto found=items_.find(stack.id);
    if(found==items_.end() || found->second.price<=0) { say("Item sem preco de venda."); return false; }
    const int price=std::max(1,found->second.price/2);
    gold_=std::min(1000000000,gold_+price);
    say("Vendeu "+found->second.name+" por "+std::to_string(price)+" ouro.");
    if(--stack.count<=0) bag_.erase(bag_.begin()+index);
    save(savePath());
    return true;
}

bool Play::discardBag(int index) {
    if(index<0 || index>=static_cast<int>(bag_.size())) return false;
    if(equippedSlotAtBagIndex(index)>=0) { say("Desequipe antes de descartar."); return false; }
    if(loots_.size()>=24) { say("Muito loot no chao."); return false; }
    Stack& stack=bag_[static_cast<std::size_t>(index)];
    const int id=stack.id;
    Stack dropped=stack;
    dropped.count=1; // pilha com 20 unidades descarta so uma; nunca duplica 20 no loot
    loots_.push_back({id,hereX_+.60f,hereY_+.60f,dropped});
    if(--stack.count<=0) bag_.erase(bag_.begin()+index);
    const auto found=items_.find(id);
    say("Descartou "+(found==items_.end()?std::to_string(id):found->second.name)+" no chao.");
    save(savePath());
    return true;
}


void Play::clickEquip(int slot) {
    const auto it = worn_.find(slot);
    if (it == worn_.end()) return;
    const int id=it->second;
    worn_.erase(it);
    wornUid_.erase(slot);
    if (sheetReady_) applyAttrs(false); else recomputeGear();
    const auto found=items_.find(id);
    say("Tirou " + (found==items_.end()?std::to_string(id):found->second.name) + ".");
    save(savePath());
}

void Play::stashBag(int index) {
    if (index < 0 || index >= static_cast<int>(bag_.size())) return;
    const Stack stack = bag_[static_cast<std::size_t>(index)];
    if (equippedSlotAtBagIndex(index)>=0) {
        say("Tira o item antes de guardar na bolsa temporaria.");
        return;
    }
    const auto found = items_.find(stack.id);
    const int cap = found==items_.end()?1:found->second.maxStack;
    if (cap == 1 && stack.count == 1) {
        if (temp_.size() >= 16) { say("Bolsa temporaria cheia."); return; }
        temp_.push_back(stack);
    } else if (!addStack(temp_,stack.id,stack.count,cap,16)) {
        say("Bolsa temporaria cheia.");
        return;
    }
    bag_.erase(bag_.begin() + index);
    assignItemUids();
    say("Guardou na bolsa temporaria.");
    save(savePath());
}

void Play::takeTemp(int index) {
    if (index < 0 || index >= static_cast<int>(temp_.size())) return;
    const Stack stack = temp_[static_cast<std::size_t>(index)];
    const auto found = items_.find(stack.id);
    const int cap = found==items_.end()?1:found->second.maxStack;
    if (cap == 1 && stack.count == 1) {
        if (bag_.size() >= static_cast<std::size_t>(bagCapacity_)) {
            say("Bolsa cheia."); return;
        }
        bag_.push_back(stack);
    } else if (!addBag(stack.id, stack.count)) {
        say("Bolsa cheia."); return;
    }
    temp_.erase(temp_.begin() + index);
    assignItemUids();
    say("Voltou para a bolsa.");
    save(savePath());
}

bool Play::openBank() {
    if (bankOpen_) return true;
    constexpr int accessFee = 200; // NpcScript01.lua: HasMoney/TakeMoney -> OpenBank
    if (gold_ < accessFee) { say("O banco cobra 200G; ouro insuficiente."); return false; }
    const int oldGold = gold_;
    const bool wasOpen = bankOpen_;
    gold_ -= accessFee;
    bankOpen_ = true;
    if (!save(savePath())) {
        gold_ = oldGold;
        bankOpen_ = wasOpen;
        return false;
    }
    say("Cofre aberto. Taxa de acesso: 200G.");
    return true;
}

void Play::depositBank(int bagIndex) {
    if (!bankOpen_ || bagIndex < 0 || bagIndex >= static_cast<int>(bag_.size())) return;
    if (equippedSlotAtBagIndex(bagIndex) >= 0) { say("Desequipe o item antes de depositar."); return; }
    const Stack stack = bag_[static_cast<std::size_t>(bagIndex)];
    const auto info = items_.find(stack.id);
    const int cap = info == items_.end() ? 1 : info->second.maxStack;
    const auto oldBag = bag_;
    const auto oldBank = bank_;
    const auto oldWornUids = wornUid_;
    const int oldNextUid = nextItemUid_;
    if (cap == 1 && stack.count == 1) {
        if (bank_.size() >= 80) { say("Cofre cheio."); return; }
        bank_.push_back(stack);
    } else if (!addStack(bank_, stack.id, stack.count, cap, 80)) {
        say("Cofre cheio."); return;
    }
    bag_.erase(bag_.begin() + bagIndex);
    assignItemUids();
    if (!save(savePath())) {
        bag_ = oldBag;
        bank_ = oldBank;
        wornUid_ = oldWornUids;
        nextItemUid_ = oldNextUid;
        return;
    }
    say("Item depositado no cofre.");
}

void Play::withdrawBank(int bankIndex) {
    if (!bankOpen_ || bankIndex < 0 || bankIndex >= static_cast<int>(bank_.size())) return;
    const Stack stack = bank_[static_cast<std::size_t>(bankIndex)];
    const auto info = items_.find(stack.id);
    const int cap = info == items_.end() ? 1 : info->second.maxStack;
    const auto oldBag = bag_;
    const auto oldBank = bank_;
    const auto oldWornUids = wornUid_;
    const int oldNextUid = nextItemUid_;
    if (cap == 1 && stack.count == 1) {
        if (bag_.size() >= static_cast<std::size_t>(bagCapacity_)) { say("Bolsa cheia."); return; }
        bag_.push_back(stack);
    } else if (!addBag(stack.id, stack.count)) { say("Bolsa cheia."); return; }
    bank_.erase(bank_.begin() + bankIndex);
    assignItemUids();
    if (!save(savePath())) {
        bag_ = oldBag;
        bank_ = oldBank;
        wornUid_ = oldWornUids;
        nextItemUid_ = oldNextUid;
        return;
    }
    say("Item retirado do cofre.");
}

void Play::dragInventory(int source, int target) {
    if (source >= 100 && source < 200) {
        if (target == 250) clickEquip(source - 100);
        return;
    }
    if (source >= 300 && source < 400) {
        if (target == 250) {
            if (bankOpen_) withdrawBank(source - 300);
            else takeTemp(source - 300);
        }
        return;
    }
    if (source < 200 || source >= 300) return;
    const int index = source - 200;
    if (index < 0 || index >= static_cast<int>(bag_.size())) return;
    if (target == 350) {
        if (bankOpen_) depositBank(index);
        else stashBag(index);
        return;
    }
    if (target < 100 || target >= 200) return;
    const auto item = items_.find(bag_[static_cast<std::size_t>(index)].id);
    if (item == items_.end() || !item_rules::canEquipSlot(item->second.slot,item->second.equipSlots,target - 100)) {
        say("Esse item nao encaixa nesse espaco.");
        return;
    }
    equipBagToSlot(index,target - 100);
}


} // namespace top
