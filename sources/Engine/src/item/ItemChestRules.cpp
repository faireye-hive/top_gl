#include "top/ItemChestRules.h"

namespace top::item_chests {
bool characterRestrictedBox(int id, const std::string& callback) {
    switch (id) {
        case 209: return callback == "ItemUse_SCBoxYXTZ";
        case 210: return callback == "ItemUse_SCBoxLSTZ";
        case 211: return callback == "ItemUse_SCBoxHYTZ";
        case 212: return callback == "ItemUse_SCBoxFYSTZ";
        case 213: return callback == "ItemUse_SCBoxSZZTZ";
        case 214: return callback == "ItemUse_SCBoxHHSTZ";
        default: return false;
    }
}

std::optional<Plan> planFor(int itemId, const std::string& callback, int characterType) {
    // ItemEffect.lua: Chest of Kylin. O quarto item pertence somente ao tipo 4.
    if (itemId == 192 && callback == "ItemUse_SaintCloth") {
        Plan plan;
        plan.requiredFreeSlots = 4;
        if (characterType == 4) plan.rewards.push_back({828, 1});
        plan.rewards.push_back({825, 1});
        plan.rewards.push_back({826, 1});
        plan.rewards.push_back({827, 1});
        return plan;
    }
    // 05-AY: caixas Relic de armadura, com exigencia e tipo historicos.
    // A variante do tipo 4 pode adicionar um quarto equipamento.
    switch (itemId) {
        case 209: if (callback == "ItemUse_SCBoxYXTZ" && characterType == 1)
            return Plan{3, {{395,1}, {587,1}, {747,1}}}; break;
        case 210: if (callback == "ItemUse_SCBoxLSTZ" && characterType == 2)
            return Plan{4, {{397,1}, {829,1}, {603,1}}}; break;
        case 211: if (callback == "ItemUse_SCBoxHYTZ" && (characterType == 1 || characterType == 3))
            return Plan{3, {{399,1}, {589,1}, {749,1}}}; break;
        case 212: if (callback == "ItemUse_SCBoxFYSTZ") {
            if (characterType == 3) return Plan{4, {{401,1},{591,1},{751,1}}};
            if (characterType == 4) return Plan{4, {{403,1},{593,1},{753,1},{2218,1}}};
            break;
        }
        case 213: if (callback == "ItemUse_SCBoxSZZTZ") {
            if (characterType == 3) return Plan{4, {{405,1},{595,1},{755,1}}};
            if (characterType == 4) return Plan{4, {{407,1},{597,1},{757,1},{2220,1}}};
            break;
        }
        case 214: if (callback == "ItemUse_SCBoxHHSTZ") {
            if (characterType == 1 || characterType == 3)
                return Plan{4, {{409,1},{599,1},{759,1}}};
            if (characterType == 4)
                return Plan{4, {{412,1},{601,1},{761,1},{2222,1}}};
            break;
        }
        default: break;
    }
    // ItemEffect.lua: sete Relic Weapon Boxes sem sorteio, com uma GiveItem
    // por callback e exigencia historica de 1 slot livre ANTES da abertura.
    switch (itemId) {
        case 215: if (callback == "ItemUse_SCBoxRYBZ") return Plan{1, {{109, 1}}}; break;
        case 216: if (callback == "ItemUse_SCBoxXMC") return Plan{1, {{111, 1}}}; break;
        case 217: if (callback == "ItemUse_SCBoxLQJ") return Plan{1, {{113, 1}}}; break;
        case 218: if (callback == "ItemUse_SCBoxLYJ") return Plan{1, {{115, 1}}}; break;
        case 219: if (callback == "ItemUse_SCBoxFHG") return Plan{1, {{117, 1}}}; break;
        case 220: if (callback == "ItemUse_SCBoxLXHZ") return Plan{1, {{119, 1}}}; break;
        case 221: if (callback == "ItemUse_SCBoxYCJ") return Plan{1, {{150, 1}}}; break;
        default: break;
    }
    return std::nullopt;
}
} // namespace top::item_chests
