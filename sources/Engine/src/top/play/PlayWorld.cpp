#include "top/Play.h"
#include "top/MonsterAI.h"
#include "top/HistoricalSkillExecution.h"

#include <algorithm>
#include <cmath>

namespace top {
void Play::snapshotWorldMonsters() {
    for (const Foe& foe : foes_)
        if (foe.spawnKey != 0)
            worldMonsters_.save(worldMap_, foe.spawnKey, worldClock_, foe.hp,
                                foe.x, foe.y, foe.yaw, foe.deadFor, foe.respawnSeconds);
}

void Play::setWorldMap(const std::string& mapName) {
    if (worldMap_ == mapName) return;
    snapshotWorldMonsters();
    worldMap_ = mapName;
}

bool Play::rebindWorld(const ActorSkin& actors) {
    if (root_.empty()) return false;
    snapshotWorldMonsters();
    // Troca efetiva de personagem usa bind limpo, nao herda efeitos de outro.
    if (actors.playerId() != player_.id) return bind(root_, actors);
    const std::string keepRoot = root_;
    const int keepProfileId = profileId_;
    const std::string keepProfileName = profileName_;
    // O bind é útil para substituir monstros, mas nao deve zerar estados
    // temporarios do jogador nem as recargas de habilidades.
    const float keepSp = player_.homeY;
    const int keepHp = player_.hp;
    const float keepDeath = deadFor_;
    const int keepArmedSkill = armedSkill_;
    const int keepRequestedSkill = requestedSkill_;
    const bool keepBasicStrike = pendingBasicStrike_;
    const float keepCasting = castingFor_;
    const int keepCastingAction = castingActionBase_;
    const unsigned keepAttackSequence = attackSequence_;
    const float keepBerserk = berserkFor_;
    const float keepBerserkAttackSpeed = berserkAttackSpeedBonus_;
    const float keepHarden = hardenFor_;
    const int keepHardenDefense = hardenDefense_;
    const float keepWillOfSteel = willOfSteelFor_;
    const int keepWillOfSteelDefense = willOfSteelDefense_;
    const float keepGeneric = genericBuffFor_;
    const int keepBuffAttack = genericBuffAttack_;
    const int keepBuffDefense = genericBuffDefense_;
    const float keepRegen = regen_;
    const float keepPlayerCooldown = player_.cdLeft;
    const float keepBoatCooldown = boatCannonCd_;
    const bool keepSitting = sitting_;
    std::array<float, 8> keepSkills{};
    std::copy(std::begin(skillCd_),std::end(skillCd_),keepSkills.begin());
    save(savePath(keepRoot));
    if (!bind(keepRoot, actors, keepProfileId, keepProfileName)) return false;
    player_.homeY = std::clamp(keepSp, 0.f, player_.homeX);
    player_.hp = std::clamp(keepHp, 0, player_.maxHp);
    deadFor_ = keepDeath;
    armedSkill_ = keepArmedSkill;
    requestedSkill_ = keepRequestedSkill;
    pendingBasicStrike_ = keepBasicStrike;
    castingFor_ = keepCasting;
    castingActionBase_ = keepCastingAction;
    attackSequence_ = keepAttackSequence;
    berserkFor_ = keepBerserk;
    berserkAttackSpeedBonus_ = keepBerserkAttackSpeed;
    hardenFor_ = keepHarden;
    hardenDefense_ = keepHardenDefense;
    willOfSteelFor_ = keepWillOfSteel;
    willOfSteelDefense_ = keepWillOfSteelDefense;
    genericBuffFor_ = keepGeneric;
    genericBuffAttack_ = keepBuffAttack;
    genericBuffDefense_ = keepBuffDefense;
    regen_ = keepRegen;
    player_.cdLeft = keepPlayerCooldown;
    boatCannonCd_ = keepBoatCooldown;
    sitting_ = keepSitting;
    for (int i = 0; i < 8; ++i) skillCd_[i] = keepSkills[static_cast<std::size_t>(i)];
    return true;
}

void Play::pickFoe(int index) {
    if(index<0 || index>=static_cast<int>(foes_.size()) || foes_[static_cast<std::size_t>(index)].hp<=0) return;
    target_=index;
    requestedLoot_=-1;
    // Skill hotkey + enemy click only sets an explicit request. The movement
    // and attack loop will commit once the actor is within historical range.
    if (requestedSkill_ >= 0) {
        pendingBasicStrike_ = false;
    } else if (armedSkill_ < 0) {
        pendingBasicStrike_ = true; // explicit, one-shot basic attack
    }
    say("Alvo: "+foes_[static_cast<std::size_t>(index)].name);
}

bool Play::selectedMonster(float& x, float& y, float& radius) const {
    if (target_ < 0 || target_ >= static_cast<int>(foes_.size())) return false;
    const Foe& foe = foes_[static_cast<std::size_t>(target_)];
    if (foe.hp <= 0) return false;
    x = foe.x;
    y = foe.y;
    radius = std::clamp(foe.radius + .54f, .85f, 3.f);
    return true;
}

screen_pick::Hit Play::rayHit(const screen_pick::Ray& ray, float terrainT, const MapFile* map) const {
    screen_pick::Hit hit;
    for(int i=0;i<static_cast<int>(foes_.size());++i) {
        const Foe& foe=foes_[static_cast<std::size_t>(i)];
        if(foe.hp<=0) continue;
        // O modelo pode ser maior que o circulo no terreno: selecionar o corpo inteiro.
        const float t=screen_pick::actor(ray,foe.x,map?map->heightAt(foe.x,foe.y):0.f,foe.y,std::max(.58f,foe.radius+.32f),2.1f);
        if(t<hit.t && screen_pick::visible(t,terrainT)) hit={1,i,t};
    }
    for(int i=0;i<static_cast<int>(loots_.size());++i) {
        const Loot& loot=loots_[static_cast<std::size_t>(i)];
        const float t=screen_pick::sphere(ray,loot.x,(map?map->heightAt(loot.x,loot.y):0.f)+.60f,loot.y,.85f);
        if(t<hit.t && screen_pick::visible(t,terrainT)) hit={2,i,t};
    }
    return hit;
}

bool Play::requestLoot(int index,float px,float py,float& tx,float& ty) {
    if(index<0 || index>=static_cast<int>(loots_.size())) return false;
    target_=-1;
    pendingBasicStrike_ = false;
    armedSkill_ = -1;
    requestedSkill_ = -1;
    requestedLoot_=index;
    const Loot& loot=loots_[static_cast<std::size_t>(index)];
    tx=loot.x; ty=loot.y;
    collectRequested(px,py);
    return true;
}

void Play::pick(float x, float y) {
    int best=-1;
    float bestD=2.2f;
    for(int i=0;i<static_cast<int>(foes_.size());++i) {
        const Foe& foe=foes_[static_cast<std::size_t>(i)];
        if(foe.hp<=0) continue;
        const float d=std::hypot(foe.x-x,foe.y-y);
        if(d<bestD) { bestD=d; best=i; }
    }
    target_=best;
    pendingBasicStrike_ = false;
    if (best < 0) { armedSkill_ = -1; requestedSkill_ = -1; }
    requestedLoot_=-1;
    if(best>=0) say("Alvo: "+foes_[static_cast<std::size_t>(best)].name);
}

int Play::cursorOver(float x, float y) const {
    for (const Loot& loot : loots_) {
        const float dx = loot.x - x;
        const float dy = loot.y - y;
        if (dx * dx + dy * dy < 1.15f * 1.15f) return 2;
    }
    float best = 2.2f;
    bool hit = false;
    for (const Foe& mob : foes_) {
        if (mob.hp <= 0) continue;
        const float dx = mob.x - x;
        const float dy = mob.y - y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < best) {
            best = dist;
            hit = true;
        }
    }
    return hit ? 1 : 0;
}

bool Play::skillArmed() const { return requestedSkill_ >= 0 || armedSkill_ >= 0; }

void Play::clearTarget() {
    target_ = -1;
    pendingBasicStrike_ = false;
    armedSkill_ = -1;
    requestedSkill_ = -1;
}

bool Play::aim(float px, float py, float& yaw) const {
    if (target_ < 0 || target_ >= static_cast<int>(foes_.size())) return false;
    const Foe& foe = foes_[static_cast<std::size_t>(target_)];
    if (foe.hp <= 0) return false;
    const float dx = foe.x - px;
    const float dy = foe.y - py;
    if (dx * dx + dy * dy < 1e-6f) return false;
    yaw = std::atan2(dx, dy);
    return true;
}

bool Play::chasePoint(float px, float py, float& outX, float& outY) const {
    if (target_ < 0 || target_ >= static_cast<int>(foes_.size())) return false;
    const Foe& foe = foes_[static_cast<std::size_t>(target_)];
    if (foe.hp <= 0) return false;
    const float dx = foe.x - px;
    const float dy = foe.y - py;
    const float dist = std::sqrt(dx * dx + dy * dy);
    // Selecting a foe never begins automatic combat. A manual attack or an
    // explicitly armed skill may approach its own historical range. Attack
    // logic consumes the command once, without repeating after the cast.
    if (!pendingBasicStrike_ && armedSkill_ < 0 && requestedSkill_ < 0) return false;
    const float reach = attackReach(foe);
    if (dist <= reach || dist < 1e-3f) {
        outX = px;
        outY = py;
        return true;
    }
    // Para dentro do alcance. Parar na borda deixa o corpo a um passo
    // curto e o golpe, que exige dist <= alcance, nunca sai.
    const float stop = historical_skill::approachStopMeters(reach);
    outX = foe.x - dx / dist * stop;
    outY = foe.y - dy / dist * stop;
    return true;
}

bool Play::lockMove() const { return deadFor_ > 0.f || playerStunFor_ > 0.f; }

int Play::playerPose() const {
    if (deadFor_ > 0.f) return 3;
    if (swing_ > 0.f) return 2;
    if (sitting_) return 4;
    if (castingFor_ > 0.f && castingActionBase_ > 0) return 5;
    return 0;
}

void Play::toggleSit() {
    if (deadFor_ > 0.f) return;
    sitting_ = !sitting_;
    say(sitting_ ? "Sentou. HP e SP sobem mais rapido." : "Levantou.");
}

} // namespace top
