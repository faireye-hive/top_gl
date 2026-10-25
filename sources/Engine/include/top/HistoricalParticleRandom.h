#pragma once
// SK2.17-A: CMPPartSys::Load in the original DX9 MPParticleSys.cpp.
// _iRoadom selects [life/divisor, life] and SNOW's velocity interval.
// Before .par v13 it was inferred from the system type; newer files store it.
namespace top { namespace historical_particle_random {
inline int defaultDivisor(int type) {
    return type == 1 || type == 2 || type == 16 ? 4 : 2;
}
inline int decodedDivisor(unsigned version, int type, int serialized = 0) {
    if (version <= 12) return defaultDivisor(type);
    // A zero/negative value would divide by zero in the original client.
    // Treat an invalid field as the constructor default (2), not as 1.
    return serialized > 0 && serialized <= 256 ? serialized : 2;
}
}} // namespace top::historical_particle_random
