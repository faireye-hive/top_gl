#pragma once
// SK2.16-D: DX9 CMPModelEff::Render() binds the subeffect's SetTexture().
// External .lgo materials provide a fallback, not precedence over the
// explicitly-authored .eff texture. Animated .eff frame texture wins first.
#include <cstdlib>
#include <string>

namespace top { namespace legacy_effect_texture {

enum class Origin { None, Animated, Effect, Material };
struct Choice {
    std::string path;
    Origin origin = Origin::None;
};
inline bool historicalEnabled() {
    const char* value = std::getenv("TOP_HISTORICAL_MODEL_TEXTURE");
    return value && value[0] == '1' && value[1] == '\0';
}
inline Choice choose(const std::string& effectTexture,
                     const std::string& materialTexture,
                     const std::string& animatedFrame,
                     bool historical) {
    if (!animatedFrame.empty()) return {animatedFrame, Origin::Animated};
    if (historical && !effectTexture.empty()) return {effectTexture, Origin::Effect};
    if (!materialTexture.empty()) return {materialTexture, Origin::Material};
    if (!effectTexture.empty()) return {effectTexture, Origin::Effect};
    return {};
}
}} // namespace top::legacy_effect_texture
