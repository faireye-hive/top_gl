#pragma once
#include <optional>
#include <string>
#include <vector>

namespace top::item_chests {
// Regras deterministicas auditadas em ItemEffect.lua (05-AX).
// Nunca interpreta Lua; o par (item ID, callback) deve bater com iteminfo.txt.
struct Grant { int itemId=0; int count=0; };
struct Plan {
    int requiredFreeSlots = 0; // GetChaFreeBagGridNum ANTES de consumir
    std::vector<Grant> rewards;
};
std::optional<Plan> planFor(int itemId, const std::string& callback, int characterType);
// True somente para armaduras Relic conhecidas, bloqueadas por tipo.
bool characterRestrictedBox(int itemId, const std::string& callback);
} // namespace top::item_chests
