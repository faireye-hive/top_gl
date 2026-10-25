#pragma once
#include "top/EquipmentStats.h"
// 05-Y/05-BC: metadados opcionais de cada copia fisica de item.
// O formato id,count legado permanece inalterado antes dos campos Y1/Y2/T1/T2.
// "extra" registra atributos individuais, separadamente do bonus base do
// iteminfo.txt. Gemas e forja ficam armazenadas, mas sem bonus ficticios.
#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace top::item_instance {

inline std::array<int, 20> values(const equipment_stats::Bonus& b) {
    return {b.str,b.agi,b.dex,b.con,b.sta,b.minAttack,b.maxAttack,b.defense,
            b.hp,b.sp,b.dodge,b.hit,b.critical,b.hpRegen,b.spRegen,
            b.moveSpeed,b.resistance,b.aspdCoefficient,b.minAttackCoefficient,b.maxAttackCoefficient};
}
inline void setValues(equipment_stats::Bonus& b, const std::array<int, 20>& x) {
    b.str=x[0]; b.agi=x[1]; b.dex=x[2]; b.con=x[3]; b.sta=x[4];
    b.minAttack=x[5]; b.maxAttack=x[6]; b.defense=x[7]; b.hp=x[8]; b.sp=x[9];
    b.dodge=x[10]; b.hit=x[11]; b.critical=x[12]; b.hpRegen=x[13];
    b.spRegen=x[14]; b.moveSpeed=x[15]; b.resistance=x[16]; b.aspdCoefficient=x[17];
    b.minAttackCoefficient=x[18]; b.maxAttackCoefficient=x[19];
}
inline bool parseInt(const std::string& text, int& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE || value < INT_MIN || value > INT_MAX) return false;
    out = static_cast<int>(value);
    return true;
}
inline std::vector<int> parseRecord(const std::string& record) {
    std::vector<int> result;
    std::size_t start=0;
    while (start <= record.size()) {
        const std::size_t end=record.find(',',start);
        int val=0;
        if (!parseInt(record.substr(start, end==std::string::npos ? end : end-start),val)) return {};
        result.push_back(val);
        if (end==std::string::npos) break;
        start=end+1;
        if (result.size()>33) return {};
    }
    return result;
}

template <class Stack>
std::string encodeStacks(const std::vector<Stack>& stacks) {
    std::ostringstream out;
    for (std::size_t i=0; i<stacks.size(); ++i) {
        const Stack& s=stacks[i];
        if (i) out << ';';
        out << s.id << ',' << s.count << ',' << s.uid << ',' << s.forgeLevel;
        for (int gem : s.gemIds) out << ',' << gem;
        for (int value : values(s.extra)) out << ',' << value;
        out << ',' << s.quality << ',' << s.durability << ',' << s.maxDurability
            << ',' << s.energy << ',' << s.maxEnergy << ',' << (s.tradable ? 1 : 0);
    }
    return out.str();
}

template <class Stack>
void decodeStacks(const char* begin, std::vector<Stack>& stacks) {
    if (!begin) return;
    const std::string line(begin, std::strcspn(begin,"|\r\n"));
    std::size_t start=0;
    std::size_t index=0;
    while (start<line.size() && index<stacks.size()) {
        const auto end=line.find(';',start);
        const auto fields=parseRecord(line.substr(start,end==std::string::npos ? end : end-start));
        const bool legacy=fields.size()==25;
        const bool current=fields.size()==31;
        const bool extended=fields.size()==33;
        if ((legacy || current || extended) && fields[0]==stacks[index].id && fields[1]==stacks[index].count &&
            fields[2]>0 && fields[2]<=1000000000 && fields[3]>=0 && fields[3]<=15 &&
            fields[4]>=0 && fields[4]<=1000000 && fields[5]>=0 && fields[5]<=1000000 &&
            fields[6]>=0 && fields[6]<=1000000) {
            bool safe=true;
            std::array<int,20> bonus{};
            const int bonusCount = extended ? 20 : 18;
            for (int k=0;k<bonusCount;++k) {
                if (fields[7+k]<-1000000 || fields[7+k]>1000000) safe=false;
                bonus[static_cast<std::size_t>(k)]=fields[7+k];
            }
            const std::size_t tail = extended ? 27 : 25;
            if (safe && (!(current || extended) || (fields[tail]>=0 && fields[tail]<=10 &&
                fields[tail+1]>=0 && fields[tail+2]>=fields[tail+1] && fields[tail+2]<=1000000 &&
                fields[tail+3]>=0 && fields[tail+4]>=fields[tail+3] && fields[tail+4]<=1000000 &&
                (fields[tail+5]==0 || fields[tail+5]==1)))) {
                Stack& s=stacks[index];
                s.uid=fields[2]; s.forgeLevel=fields[3];
                s.gemIds={fields[4],fields[5],fields[6]};
                setValues(s.extra,bonus);
                if (current || extended) {
                    s.quality=fields[tail]; s.durability=fields[tail+1]; s.maxDurability=fields[tail+2];
                    s.energy=fields[tail+3]; s.maxEnergy=fields[tail+4]; s.tradable=fields[tail+5]!=0;
                }
            }
        }
        ++index;
        if (end==std::string::npos) break;
        start=end+1;
    }
}

inline std::string encodeWorn(const std::unordered_map<int,int>& worn) {
    std::ostringstream out;
    bool first=true;
    for (const auto& entry : worn) {
        if (!first) out << ';';
        first=false;
        out << entry.first << ',' << entry.second;
    }
    return out.str();
}
template <class Stack>
void decodeWorn(const char* begin, const std::unordered_map<int,int>& legacy,
                std::unordered_map<int,int>& result, const std::vector<Stack>& bag) {
    if (!begin) return;
    const std::string line(begin, std::strcspn(begin,"|\r\n"));
    std::unordered_set<int> used;
    std::size_t start=0;
    while (start<line.size()) {
        const auto end=line.find(';',start);
        const auto fields=parseRecord(line.substr(start,end==std::string::npos ? end : end-start));
        if (fields.size()==2 && fields[0]>=0 && fields[0]<=30 && fields[1]>0 &&
            legacy.count(fields[0]) && !result.count(fields[0]) && !used.count(fields[1])) {
            const int id=legacy.at(fields[0]);
            const auto it=std::find_if(bag.begin(),bag.end(),[&](const Stack& s){
                return s.uid==fields[1] && s.id==id;
            });
            if (it!=bag.end()) {
                used.insert(fields[1]);
                result[fields[0]]=fields[1];
            }
        }
        if (end==std::string::npos) break;
        start=end+1;
    }
}
} // namespace top::item_instance
