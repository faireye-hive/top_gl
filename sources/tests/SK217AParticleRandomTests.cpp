#include "top/HistoricalParticleRandom.h"
#include "top/HistoricalParticlePool.h"
#include "top/HistoricalParticleMotion.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace top;
#define CHECK(c) do { if (!(c)) throw std::runtime_error(#c); } while (0)
int main() {
    using historical_particle_random::decodedDivisor;
    // DX9 MPParticleSys::Load selects 4 for v2-v12 snow/fire/dummy.
    for(unsigned version:{2u,6u,12u}) {
        for(int type:{1,2,16}) CHECK(decodedDivisor(version,type)==4);
        for(int type:{3,5,6}) CHECK(decodedDivisor(version,type)==2);
    }
    // v13+ reads the actual serialized field, not the old type default.
    for(unsigned version:{13u,14u,15u}) {
        CHECK(decodedDivisor(version,1,1)==1);
        CHECK(decodedDivisor(version,2,3)==3);
        CHECK(decodedDivisor(version,3,4)==4);
        CHECK(decodedDivisor(version,1,0)==2); // corrupt or unsafe divisor
        CHECK(decodedDivisor(version,1,-5)==2);
    }
    using historical_particle_pool::individualLife;
    for(int slot=0; slot<64; ++slot) {
        float oldLife=individualLife(2.f,42,slot,0,2);
        float newLife=individualLife(2.f,42,slot,0,4);
        CHECK(oldLife>=1.f && oldLife<=2.f);
        CHECK(newLife>=.5f && newLife<=2.f);
        CHECK(newLife<=oldLife);
        CHECK(individualLife(2.f,42,slot,0,1)==2.f);
    }
    // Different divisors must change the actual pool's live-age curve.
    bool different=false;
    for(int slot=0; slot<20; ++slot) {
        auto a=historical_particle_pool::at(1,slot,20,2.f,2.f,.01f,-1.f,42,2);
        auto b=historical_particle_pool::at(1,slot,20,2.f,2.f,.01f,-1.f,42,4);
        if(a.alive!=b.alive || std::fabs(a.age-b.age)>.0001f) different=true;
    }
    CHECK(different);
    using historical_particle_motion::V3;
    const V3 none{{0.f,0.f,0.f}}, up{{0.f,0.f,1.f}};
    for(int slot=0;slot<50;++slot) {
        const auto v2=historical_particle_motion::at(1,none,up,none,2.f,0.f,72,slot,0,2).velocity[2];
        const auto v4=historical_particle_motion::at(1,none,up,none,2.f,0.f,72,slot,0,4).velocity[2];
        CHECK(v2>=1.f && v2<=2.f && v4>=.5f && v4<=v2);
        CHECK(historical_particle_motion::at(1,none,up,none,2.f,0.f,72,slot,0,1).velocity[2]==2.f);
    }
    std::cout<<"SK2.17-A: DX9 serialized divisor, lifetime and SNOW velocity verified\n";
}
