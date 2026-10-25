#pragma once
// SK2.16-A: deterministic snapshot evaluation of CMPPartSys pool births/lifetimes.
// The original SNOW/FIRE create one particle at a time and recycle dead slots,
// while BLAST spawns a whole group and (when Play(0) loops) restarts that group.
// DX9 uses rand(), frame-dependent emit timing and random acceleration; this
// evaluator reconstructs the lifecycle, NOT an exact RNG or movement replay.
// Opt-in only until capture-based visual validation against the D3D client.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace top { namespace historical_particle_pool {
inline bool enabled() {
    const char* e = std::getenv("TOP_HISTORICAL_PARTICLE_POOL");
    return e && e[0] == '1' && e[1] == '\0';
}
inline float rand01(std::uint32_t k) {
    k ^= k >> 16; k *= 0x7feb352du; k ^= k >> 15;
    k *= 0x846ca68bu; k ^= k >> 16;
    return static_cast<float>(k & 0x00ffffffu) / 16777216.f;
}
struct Sample {
    bool alive = false;
    float age = 0.f;
    float life = 0.f;
    float normalized = 0.f;
    unsigned generation = 0;
};
inline float individualLife(float life, std::uint32_t seed, int slot,
                            unsigned generation, int divisor = 2) {
    // DX9 chooses Randf(life / _iRoadom, life). In older .par versions
    // _iRoadom is type-dependent, and v13+ records it explicitly.
    const float lower = 1.f / static_cast<float>(divisor > 0 ? divisor : 2);
    return life * (lower + (1.f - lower) * rand01(seed ^ (0x9e3779b9u *
                static_cast<std::uint32_t>(slot + 1)) ^ (generation * 0x85ebca6bu)));
}
// stopAt is measured relative to the end of _fDelayTime, and -1 means
// no explicit emission stop. Once stopped, existing particles finish their life.
inline Sample at(int type, int slot, int count, float seconds, float authoredLife,
                 float step, float stopAt, std::uint32_t seed,
                 int divisor = 2) {
    Sample empty;
    if (slot < 0 || count < 1 || slot >= count ||
        !(std::isfinite(seconds) && std::isfinite(authoredLife) &&
          std::isfinite(step) && std::isfinite(stopAt)) ||
        seconds < 0.f || authoredLife <= 0.f) return empty;
    const float life = std::max(authoredLife, 0.0001f);
    const float interval = std::max(0.f, step);
    auto stopped = [&](float birth) { return stopAt >= 0.f && birth >= stopAt; };
    if (type == 3) { // BLAST: entire pool born at once, Play(0) loops the group.
        float maxLife = 0.f;
        for (int n = 0; n < count; ++n)
            maxLife = std::max(maxLife, individualLife(life,seed,n,0,divisor));
        if (!(maxLife > 0.f)) return empty;
        const unsigned gen = static_cast<unsigned>(std::min(100000.0,
                                          std::floor(static_cast<double>(seconds)/maxLife)));
        const float birth = static_cast<float>(gen) * maxLife;
        if (stopped(birth)) return empty;
        const float myLife = individualLife(life,seed,slot,gen,divisor);
        const float age = seconds-birth;
        if (age >= myLife) return empty; // dead slot awaits the next group cycle
        return {true,age,myLife,age/myLife,gen};
    }
    if (type != 1 && type != 2) return empty;
    // SNOW/FIRE: individual slots. A first pass emits across the pool,
    // subsequent generations recycle individual slots after death. The D3D
    // _fStep accumulator depends on frame cadence; this is an FPS-independent
    // approximation that preserves staggered births and per-particle curves.
    float birth = static_cast<float>(slot) * interval;
    unsigned gen = 0;
    if (seconds < birth || stopped(birth)) return empty;
    for (; gen < 10000; ++gen) {
        const float myLife = individualLife(life,seed,slot,gen,divisor);
        if (seconds < birth + myLife)
            return {true,seconds-birth,myLife,(seconds-birth)/myLife,gen};
        const float next = birth + std::max(myLife,interval * static_cast<float>(count));
        if (!std::isfinite(next) || next <= birth || next > seconds || stopped(next))
            return empty;
        birth = next;
    }
    return empty;
}
}} // namespace top::historical_particle_pool
