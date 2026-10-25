#pragma once
// SK2.16-B: position evaluator for the historical CMPPartSys SNOW/FIRE/BLAST.
// Coordinate order here is D3D (X,Y horizontal, Z up). The caller converts
// to GL (X,Z horizontal, Y up) at the boundary.
//
// This is deliberately opt-in: original Randf(), random acceleration signs and
// emitter stepping are frame-dependent. Our seeded samples permit repeatable
// comparisons without pretending to reproduce an unknown DX9 rand() stream.
#include "top/HistoricalParticlePool.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace top { namespace historical_particle_motion {
inline bool enabled() {
    const char* env = std::getenv("TOP_HISTORICAL_PARTICLE_MOTION");
    return env && env[0]=='1' && env[1]=='\0';
}
using V3 = std::array<float,3>;
struct Pose {
    V3 position{{0.f,0.f,0.f}}; // local DX9 displacement, includes spawn range
    V3 velocity{{0.f,0.f,0.f}};
};
inline float sample(std::uint32_t seed, unsigned channel) {
    return historical_particle_pool::rand01(seed ^ (0x9e3779b9u * (channel+1)));
}
inline float sign(std::uint32_t seed, unsigned channel) {
    return sample(seed,channel)<0.5f?-1.f:1.f;
}
inline V3 unit(V3 v) {
    const float n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(n<=1e-8f || !std::isfinite(n))return {{0.f,0.f,0.f}};
    for(float& x:v)x/=n;
    return v;
}
// Authored range is Randf(range[i]) in the DX9 constructor, NOT centered
// around the origin. Randf returns a value in [0, range].
inline Pose at(int type, const V3& range, const V3& direction,
               const V3& acceleration, float speed, float age,
               std::uint32_t seed, int slot, unsigned generation=0,
               int divisor=2) {
    Pose p;
    if(type!=1 && type!=2 && type!=3)return p;
    if(!(std::isfinite(age) && std::isfinite(speed) && age>=0.f))return p;
    const std::uint32_t particleSeed=seed ^ (0x85ebca6bu*std::uint32_t(slot+1)) ^
                                   (0xc2b2ae35u*generation);
    for(unsigned c=0;c<3;++c)
        p.position[c]=range[c]*sample(particleSeed,c);
    if(type==2) {
        // _CreateFire normalizes _vDir; _FrameMoveFire translates by the
        // normalized direction, not velocity * 0.2 or a hardwired rise.
        p.velocity=unit(direction);
    } else if(type==1) {
        // _CreateSnow samples [speed/_iRoadom,speed] per axis, with a sign
        // flip on horizontal axes. The divisor comes from the .par version.
        const float lower=1.f/static_cast<float>(divisor>0?divisor:2);
        for(unsigned c=0;c<3;++c) {
            const float magnitude=speed*(lower+(1.f-lower)*sample(particleSeed,3+c));
            p.velocity[c]=magnitude*direction[c]*(c==2?1.f:sign(particleSeed,6+c));
        }
        // A zero vertical component makes _CreateSnow reject the particle.
        // The caller can decide whether to suppress the sample entirely.
    } else { // type 3 BLAST
        // _CreateBlast: Randf(speed), -Randf(speed), Randf(speed), then
        // direction components and random horizontal signs.
        for(unsigned c=0;c<3;++c) {
            const float v=speed*sample(particleSeed,3+c);
            p.velocity[c]=v*direction[c]*(c==2?1.f:(c==1?-1.f:1.f)*sign(particleSeed,6+c));
        }
    }
    for(unsigned c=0;c<3;++c) {
        p.position[c]+=p.velocity[c]*age;
        if(type==3) {
            // _FrameMoveBlast integrates vOldPos += acceleration*dt.
            // Its continuous-time equivalent is 0.5 * a * t^2.
            p.position[c]+=0.5f*acceleration[c]*age*age;
        }
        // FIRE/SNOW choose a new random +/- acceleration EVERY FRAME in DX9.
        // Its expected displacement is zero, so adding a fixed quadratic
        // acceleration here would invent a persistent directional force.
    }
    return p;
}
}} // namespace top::historical_particle_motion
