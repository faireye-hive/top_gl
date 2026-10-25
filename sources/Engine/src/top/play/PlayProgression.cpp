#include "top/Play.h"
#include "top/PlayCombatRules.h"
#include "top/JobNames.h"

#include <algorithm>
#include <cmath>

namespace top {
using namespace play_combat;
namespace {
const char* jobWeapon(int job) {
    switch (job) {
    case 1: case 3: case 8: case 9: case 10: return "espada";
    case 2: case 12: return "arco";
    case 4: case 16: return "adaga";
    case 5: case 13: case 14: return "cajado";
    case 6: case 7: case 11: case 15: case 17: case 18: return "arma da classe";
    default: return "";
    }
}

// Coeficientes de ExAttrCheck definidos em AttrCalculate.lua (JobType 0..18).
struct JobRad {
    float hp1, hp2, hplv;
    float sp1, sp2, splv;
    float str1, str2, dex1, dex2;
    float def1, def2;
    float hit, flee;
    float hrecHp, hrecCon;
    float srecSp, srecSta;
    float aspd;
};

JobRad jobRad(int job) {
    if (job == 1) {
        return {5.f, 7.f, 25.f, 1.f, 0.f, 3.f, 1.5f, 0.4f, 0.f, 0.f, 0.2f, 0.2f, 0.6f, 0.6f, 1.f / 180.f, 1.f / 8.f,
                1.f / 100.f, 1.f / 12.f, 1.1f};
    }
    if (job == 2) {
        return {3.f, 3.f, 25.f, 1.f, 0.f, 3.f, 0.f, 0.f, 1.7f, 0.4f, 0.14f, 0.1f, 0.7f, 0.7f, 1.f / 180.f, 1.f / 8.f,
                1.f / 100.f, 1.f / 12.f, 1.2f};
    }
    if (job == 4) {
        return {5.f, 5.f, 25.f, 2.f, 1.5f, 5.f, 1.5f, 0.4f, 0.f, 0.f, 0.13f, 0.1f, 0.6f, 0.6f, 1.f / 180.f, 1.f / 8.f,
                1.f / 100.f, 1.f / 12.f, 1.1f};
    }
    if (job == 5) {
        return {5.f, 5.f, 25.f, 2.f, 1.5f, 5.f, 1.5f, 0.4f, 0.f, 0.f, 0.13f, 0.1f, 0.6f, 0.6f,
                1.f / 180.f, 1.f / 8.f, 1.f / 100.f, 1.f / 12.f, 1.1f};
    }
    if (job == 3) { // Sailor
        return {3.f, 2.f, 15.f, .5f, .5f, 1.f, .9f, .9f, 0.f, 0.f, .45f, .45f, .31f, .31f,
                1.f/200.f, 1.f/100.f, 1.f/200.f, 1.f/120.f, 1.1f};
    }
    if (job == 6 || job == 7 || job == 11 || job == 15 || job == 17 || job == 18) {
        // Artisan/Merchant/Animal Tamer/Captain/Upstart/Engineer share these
        // coefficients, except for the listed evasion and attack-speed values.
        const float flee = job == 11 ? .5f : .31f;
        const float aspd = job == 11 ? 1.2f : 1.1f;
        return {2.f, 2.f, 10.f, .5f, .5f, 1.f, .8f, .8f, 0.f, 0.f, .5f, .5f, .31f, flee,
                1.f/200.f, 1.f/100.f, 1.f/200.f, 1.f/120.f, aspd};
    }
    if (job == 8) { // Champion
        return {6.f, 7.f, 40.f, 1.f, 0.f, 3.f, 1.f, .4f, 0.f, 0.f, .2f, .7f, .7f, .4f,
                1.f/180.f, 1.f/8.f, 1.f/100.f, 1.f/12.f, 1.f};
    }
    if (job == 9) { // Crusader
        return {3.5f, 3.5f, 30.f, 1.f, 0.f, 3.f, 1.5f, .45f, 0.f, 0.f, .2f, .2f, .6f, .75f,
                1.f/180.f, 1.f/8.f, 1.f/100.f, 1.f/12.f, 1.25f};
    }
    if (job == 10) { // White Knight
        return {5.f, 5.f, 30.f, 3.f, 1.5f, 5.f, 1.5f, .4f, 0.f, 0.f, .15f, .1f, .6f, .6f,
                1.f/180.f, 1.f/8.f, 1.f/100.f, 1.f/12.f, 1.1f};
    }
    if (job == 12) { // Sharpshooter
        return {5.5f, 5.5f, 30.f, 1.f, 0.f, 3.f, 0.f, 0.f, 2.f, .5f, .2f, .2f, .6f, .85f,
                1.f/180.f, 1.f/8.f, 1.f/100.f, 1.f/12.f, 1.2f};
    }
    if (job == 13) { // Cleric
        return {3.5f, 3.5f, 30.f, 3.f, 1.5f, 5.f, 1.5f, .4f, 0.f, 0.f, .15f, .15f, .6f, .85f,
                1.f/180.f, 1.f/8.f, 1.f/100.f, 1.f/12.f, 1.1f};
    }
    if (job == 14) { // Seal Master
        return {5.5f, 5.5f, 30.f, 3.f, 1.5f, 5.f, 1.5f, .4f, 0.f, 0.f, .2f, .3f, .6f, .75f,
                1.f/180.f, 1.f/8.f, 1.f/100.f, 1.f/12.f, 1.1f};
    }
    if (job == 16) { // Voyager
        return {6.f, 6.f, 35.f, 3.f, 1.5f, 5.f, 1.5f, .4f, 0.f, 0.f, .2f, .4f, .6f, .7f,
                1.f/180.f, 1.f/8.f, 1.f/100.f, 1.f/12.f, 1.1f};
    }
    return {3.f, 2.f, 15.f, 1.f, 0.f, 3.f, 1.5f, 0.4f, 0.f, 0.f, 0.1f, 0.1f, 0.6f, 0.6f, 1.f / 200.f, 1.f / 8.f,
            1.f / 100.f, 1.f / 12.f, 1.1f};
}

// AttrCalculate.lua: Crt_luk_rad is 0.25 for Hunter (job 2), 0.31 for
// every other supported job.
float criticalLuckRad(int job) { return job == 2 ? .25f : .31f; }

int formulaHp(const JobRad& rad, int con, int level) {
    const double quad = static_cast<double>(rad.hp2) * std::pow(std::floor(con * 3.0 / 20.0), 2.0);
    return static_cast<int>(std::floor(con * 3.0 * rad.hp1 + quad + level * static_cast<double>(rad.hplv) + 40.0));
}

int formulaSp(const JobRad& rad, int sta, int level) {
    const double quad = static_cast<double>(rad.sp2) * std::pow(std::floor(sta * 3.0 / 20.0), 2.0);
    return static_cast<int>(std::floor(sta * 3.0 * rad.sp1 + quad + level * static_cast<double>(rad.splv) + 5.0));
}

int formulaAtk(float primary, float quadRad, int stat) {
    const double quad = static_cast<double>(quadRad) * std::pow(std::floor(stat * 4.0 / 20.0), 2.0);
    return static_cast<int>(std::floor(stat * static_cast<double>(primary) + quad));
}

int formulaDef(const JobRad& rad, int con) {
    const double quad = static_cast<double>(rad.def2) * std::floor(std::pow(con * 3.0 / 20.0, 2.0));
    return static_cast<int>(std::floor(con * 5.0 * rad.def1 + quad));
}

} // namespace

void Play::recomputeGear() {
    const auto gear = gearBonuses();
    const int baseMinimum = applyAttackCoefficient(baseMin_ + attrMin_, gear.minAttackCoefficient);
    const int baseMaximum = applyAttackCoefficient(baseMax_ + attrMax_, gear.maxAttackCoefficient);
    player_.minAtk = std::max(0, baseMinimum + gear.minAttack);
    player_.maxAtk = std::max(player_.minAtk, baseMaximum + gear.maxAttack);
    player_.def = std::max(0, baseDef_ + attrDef_ + gear.defense);
}

void Play::applyAttrs(bool fillGain) {
    if (!sheetReady_) return;
    const auto gear = gearBonuses();
    const JobRad rad = jobRad(job_);
    const int level = std::max(1, player_.level);
    // Bônus primários de equipamento entram na fórmula ANTES do cálculo dos
    // atributos derivados, mas não alteram os pontos permanentes do save.
    const int str = std::max(0, bstr_ + gear.str);
    const int agi = std::max(0, bagi_ + gear.agi);
    const int dex = std::max(0, bdex_ + gear.dex);
    const int con = std::max(0, bcon_ + gear.con);
    const int sta = std::max(0, bsta_ + gear.sta);
    // characterinfo.txt supplies actor/NPC template stats (e.g. 40 HP and
    // 4-5 attack for the race models), not a playable character's base stats.
    // Recompute both the Newbie baseline and the selected class from the
    // historical AttrCalculate formulas instead of adding a class delta to
    // those unrelated template values. Keeping the baseline fields also lets
    // the existing Q-save format migrate transparently when it is loaded.
    const JobRad newbie = jobRad(0);
    sheetHp_ = formulaHp(newbie, con, level);
    sheetSp_ = formulaSp(newbie, sta, level);
    baseMin_ = formulaAtk(newbie.str1, newbie.str2, str) + formulaAtk(newbie.dex1, newbie.dex2, dex);
    baseMax_ = baseMin_;
    baseDef_ = formulaDef(newbie, con);
    const int dHp = formulaHp(rad, con, level) - sheetHp_;
    const int dSp = formulaSp(rad, sta, level) - sheetSp_;
    const int classAttack = formulaAtk(rad.str1, rad.str2, str) + formulaAtk(rad.dex1, rad.dex2, dex);
    const int dMin = classAttack - baseMin_;
    const int dMax = dMin;
    const int dDef = formulaDef(rad, con) - baseDef_;
    const int oldHp = player_.maxHp;
    const float oldSp = player_.homeX;
    player_.maxHp = std::max(1, sheetHp_ + dHp + gear.hp);
    player_.homeX = static_cast<float>(std::max(1, sheetSp_ + dSp + gear.sp));
    if (fillGain && player_.maxHp > oldHp) player_.hp += player_.maxHp - oldHp;
    player_.hp = std::clamp(player_.hp, 0, player_.maxHp);
    if (fillGain && player_.homeX > oldSp) player_.homeY += player_.homeX - oldSp;
    player_.homeY = std::clamp(player_.homeY, 0.f, player_.homeX);
    attrMin_ = dMin;
    attrMax_ = dMax;
    attrDef_ = dDef;
    recomputeGear();
    player_.resist = std::clamp(baseResist_ + gear.resistance, 0, 1000);
    const int luck = std::max(0, baseLuck_ + gear.luk);
    player_.crit = std::clamp(11 + static_cast<int>(std::floor(luck * 3.f * criticalLuckRad(job_))) +
                              gear.critical, 0, 100);
    const int formHp = std::max(1, formulaHp(rad, con, level));
    const int formSp = std::max(1, formulaSp(rad, sta, level));
    const double hrec = std::max(2.0 * formHp * rad.hrecHp + con * 3.0 * rad.hrecCon, 1.0);
    const double srec = std::max((formSp * rad.srecSp + sta * 3.0 * rad.srecSta) / 2.0, 1.0);
    hrecGain_ = std::max(1, static_cast<int>(std::lround(hrec)) + gear.hpRegen);
    srecGain_ = std::max(1, static_cast<int>(std::lround(srec)) + gear.spRegen);
    hit_ = std::max(0, static_cast<int>(std::floor(dex * static_cast<double>(rad.hit))) + level * 2 + 5 + gear.hit);
    flee_ = std::max(0, static_cast<int>(std::floor(agi * static_cast<double>(rad.flee))) + level * 2 + 5 + gear.dodge);
    player_.hit = hit_;
    player_.dodge = flee_;
    // MSPD em centimetros por segundo, como em characterinfo/ItemInfo.
    player_.speed = std::clamp(baseMoveSpeed_ + gear.moveSpeed / 100.f, 0.25f, 16.f);
    const float baseAspd = 65.f + 5.f * rad.aspd;
    const float nowAspd = 65.f + static_cast<float>(agi) * rad.aspd;
    // ASPD do iteminfo e coeficiente aditivo por equipamento (radix=1000),
    // seguindo AspdIa/Aspd_final do functions.lua. Duas espadas acumulam.
    const float gearAspd = std::clamp(1.f + static_cast<float>(gear.aspdCoefficient) / 1000.f, .25f, 4.f);
    player_.cd = std::clamp(0.85f * baseAspd / std::max(1.f, nowAspd * gearAspd), 0.23f, 1.7f);
}

void Play::spendAttr(int which) {
    if (deadFor_ > 0.f) return;
    int* slot = nullptr;
    const char* name = "";
    if (which == 0) {
        slot = &bstr_;
        name = "FOR";
    } else if (which == 1) {
        slot = &bagi_;
        name = "AGI";
    } else if (which == 2) {
        slot = &bcon_;
        name = "CON";
    } else if (which == 3) {
        slot = &bsta_;
        name = "STA";
    } else if (which == 4) {
        slot = &bdex_;
        name = "DEX";
    }
    if (!slot) return;
    if (ap_ < 1) {
        say("Sem pontos de atributo.");
        return;
    }
    if (*slot >= 200) return;
    --ap_;
    ++*slot;
    applyAttrs(true);
    char line[96];
    std::snprintf(line, sizeof line, "%s %d. Pontos %d.", name, *slot, ap_);
    say(line);
    save(savePath());
}

void Play::setJob(int job) {
    if (deadFor_ > 0.f) return;
    if (job != 1 && job != 2 && job != 4 && job != 5) return;
    if (job_ != 0) {
        say("Voce ja concluiu a primeira promocao de classe.");
        return;
    }
    // NpcSdk.lua ClassChange(Type=1): primeira promocao a partir do nivel 9,
    // com custo de 2.500 ouro e apenas uma troca definitiva.
    constexpr int firstClassLevel = 9;
    constexpr int firstClassCost = 2500;
    if (player_.level < firstClassLevel) {
        say("Precisa do nivel " + std::to_string(firstClassLevel) + " para a promocao.");
        return;
    }
    if (gold_ < firstClassCost) {
        say("Precisa de " + std::to_string(firstClassCost) + " ouro para a promocao.");
        return;
    }
    const int race = player_.id;
    const bool compatible = (job == 1 && race != 4) ||
                            (job == 2 && race != 2 && race != 4) ||
                            (job == 4 && race != 2) || (job == 5 && (race == 3 || race == 4));
    if (!compatible) {
        say(player_.name + " nao pode seguir essa classe.");
        return;
    }
    gold_ -= firstClassCost;
    job_ = job;
    applyAttrs(true);
    if (job_ == 2) say("Hunter. A arma e o arco.");
    else say(std::string(jobName(job_)) + ". A arma e a " + jobWeapon(job_) + ".");
    save(savePath());
}

// Cada pilha guarda no maximo 99. Quando o item nao cabe, devolve false e nao
// mexe em nada: antes a pilha era cortada em 99 e o excesso sumia, mas a
// funcao dizia que tinha dado certo (a compra gastava o ouro sem entregar).
// Atomic: simulate the complete insertion before mutating the inventory.
} // namespace top
