#pragma once
// SK2.17-B: opt-in deterministic positions/lifetimes for less common CMPPartSys
// modes, transcribed from the creation and frame movement routines of the
// original DX9 MPParticleSys.cpp. All local positions are DX9 (Z-up).
// This is NOT full DX9 simulation: native RNG, per-frame wind/step cadence,
// nested emitter orientation and RANGE2 collision callbacks remain different.
#include "top/HistoricalParticlePool.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace top { namespace historical_particle_extended {
using V3=std::array<float,3>;
constexpr float pi=3.14159265358979323846f;
inline bool enabled() {
    const char* p=std::getenv("TOP_HISTORICAL_PARTICLE_EXTENDED");
    return p && p[0]=='1' && p[1]=='\0';
}
inline bool supported(int type) {
    return type==7 || type==8 || type==9 || type==11 || type==14 || type==15;
}
struct Sample {
    bool alive=false;
    float age=0.f;
    float normalized=0.f;
    V3 position{{0.f,0.f,0.f}};
};
inline float random01(std::uint32_t seed,int slot,int channel) {
    return historical_particle_pool::rand01(seed ^ (0x9e3779b9u*std::uint32_t(slot+1)) ^
                                             (0x85ebca6bu*std::uint32_t(channel+1)));
}
// All positional values below are LOCAL; the EffectSystem adds the effect
// origin, authored offset and historical path exactly once.
inline Sample at(int type,int slot,int count,float seconds,float life,float step,
                 float stopAt,const V3& range,const V3& direction,
                 const V3& acceleration,float speed,std::uint32_t seed) {
    Sample s;
    if(!supported(type) || count<=0 || slot<0 || slot>=count ||
       !std::isfinite(seconds) || !std::isfinite(life) ||
       !std::isfinite(speed) || !std::isfinite(step) ||
       !std::isfinite(stopAt) || seconds<0.f || life<=0.f) return s;
    // The old Play() creates ROUND, ARRAW, BLAST3 and RANGE immediately.
    // WIND and RANGE2 instead advance _fCurTime until _fStep, emitting
    // one available slot at a time; the exact DX9 schedule depends on FPS.
    const bool staggered=(type==7 || type==15);
    // Zero step emits one per frame in DX9, not an infinite number at t=0.
    // Use a nominal 60 Hz for repeatable inspection of this ambiguous case.
    const float interval=std::max(step,1.f/60.f);
    const float birth=staggered?interval*float(slot+1):0.f;
    if(seconds<birth || (staggered && stopAt>=0.f && birth>=stopAt)) return s;
    float age=seconds-birth;
    if(staggered) {
        // A dead slot is recycled in WIND; RANGE2 has a controller stop and
        // collision-triggered death, which this static evaluator cannot see.
        if(type==7) {
            const float cycle=std::max(life,interval*float(count));
            age=std::fmod(age,cycle);
            if(age>=life) return s;
            if(stopAt>=0.f && birth+std::floor((seconds-birth)/cycle)*cycle>=stopAt) return s;
        } else if(age>=life) return s;
    } else if(type==9 || type==8) {
        // ROUND and ARRAW restart their size/color frames when they end.
        age=std::fmod(age,life);
    } else if(type==11 && age>=life) return s;
    // RANGE retains its static positions throughout the active controller.
    s.alive=true; s.age=age; s.normalized=std::clamp(age/life,0.f,1.f);
    auto randomPosition=[&] {
        for(int i=0;i<3;++i)s.position[i]=range[i]*random01(seed,slot,i);
    };
    switch(type) {
    case 7: { // WIND: _CreateWind, _FrameMoveWind
        randomPosition();
        // m_vOldPos is the spawn position, m_vAccel.y/z grow linearly;
        // its XY acceleration vector rotates by angular velocity _fVecl.
        const float theta=speed*age;
        const float ay=direction[1]*age;
        s.position[0]-=ay*std::sin(theta);
        s.position[1]+=ay*std::cos(theta);
        s.position[2]+=direction[2]*age;
        break;
    }
    case 8: // ARRAW (the original code calls it ARRAW)
        for(int i=0;i<3;++i)s.position[i]=range[i]*.5f;
        break;
    case 9: { // ROUND: orbit rotating (0,range.y,0) around DX9 Z
        const float theta=2.f*pi*float(slot)/float(count) + speed*seconds;
        s.position[0]=range[0]*.5f-range[1]*std::sin(theta);
        s.position[1]=range[1]*.5f+range[1]*std::cos(theta);
        s.position[2]=range[2]*.5f;
        break;
    }
    case 11: { // BLAST3: group born at once, radial vector plus _vDir
        const float theta=2.f*pi*float(slot)/float(count);
        const V3 velocity{{std::cos(theta)-std::sin(theta)+direction[0],
                           std::sin(theta)+std::cos(theta)+direction[1],
                           -1.f+direction[2]}};
        for(int i=0;i<3;++i)
            s.position[i]=speed*(velocity[i]*age+.5f*acceleration[i]*age*age);
        break;
    }
    case 14: // RANGE: initial Randf(range), then position is held fixed
        randomPosition();
        break;
    case 15: { // RANGE2: authored Z is fixed (not Randf); motion uses unit _vDir
        s.position[0]=range[0]*random01(seed,slot,0);
        s.position[1]=range[1]*random01(seed,slot,1);
        s.position[2]=range[2];
        float norm=std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);
        if(norm>1e-8f)for(int i=0;i<3;++i)s.position[i]+=direction[i]*(speed*age/norm);
        break;
    }
    }
    return s;
}
}} // namespace top::historical_particle_extended
