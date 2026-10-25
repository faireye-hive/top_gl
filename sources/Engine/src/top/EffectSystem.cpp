#include "top/EffectSystem.h"
#include "top/TexturePaths.h"
#include "top/LegacyParticleCurves.h"
#include "top/LegacyEffectInspector.h"
#include "top/LegacyEffectGeometry.h"
#include "top/LegacyExternalEffectMesh.h"
#include "top/HistoricalBlend.h"
#include "top/HistoricalTextureFilter.h"
#include "top/HistoricalMagicMotion.h"
#include "top/LegacyStripSystem.h"
#include "top/HistoricalParticlePool.h"
#include "top/HistoricalParticleMotion.h"
#include "top/HistoricalParticleRandom.h"
#include "top/LegacyEffectTextureSelection.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace top {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Legacy shader/mode selection for the explicit compatibility path. With
// historical rendering enabled, the authored factor pair is applied by GL;
// mode 8 is not a translation of D3D factor 7 (which is DESTALPHA).
int historicBlendMode(const legacy_eff::Subeffect& part) {
    return part.sourceBlend == 5 && part.destBlend == 7 ? 8 : 4;
}

// Historical factors affect only effect batches. UI, terrain, actor and
// transparent selection-ring state remain untouched.
void setEffectBlend(EffectBatch& batch, int source, int destination) {
    const auto effective = historic_blend::effective(source, destination);
    batch.sourceBlend = effective.source;
    batch.destBlend = effective.destination;
}
bool sameEffectBlend(const EffectBatch& batch, int source, int destination) {
    const auto effective = historic_blend::effective(source, destination);
    return batch.sourceBlend == effective.source && batch.destBlend == effective.destination;
}
// Sampling is per .par system, not per texture. The same image can be
// referenced from two systems with different point/linear settings.
void setEffectFilter(EffectBatch& batch, int min, int mag) {
    const auto pair = historical_filter::effective(min, mag);
    batch.minFilter = pair.min;
    batch.magFilter = pair.mag;
}
bool sameEffectFilter(const EffectBatch& batch, int min, int mag) {
    return historical_filter::same({batch.minFilter, batch.magFilter},
                                   historical_filter::effective(min, mag));
}

// Temporary visual calibration for Frozen Arrow's historical flight meshes.
// Applies to all its authored models, not to unrelated spells. The asset's
// authored size/animation remain intact; this only changes world units.
float frozenArrowMultiplier(int skillId, int effectId) {
    if (skillId != 93 || (effectId != 1020 && effectId != 296)) return 1.f;
    float multiplier=1.f; // original-sized Frozen Arrow; do not mask missing result FX
    if (const char* raw=std::getenv("TOP_FROZEN_ARROW_FX_SCALE")) {
        char* end=nullptr;
        const float value=std::strtof(raw,&end);
        if (end!=raw && *end=='\0' && std::isfinite(value) && value>=.5f && value<=6.f)
            multiplier=value;
    }
    return multiplier;
}

bool effectModelName(const std::string& name) {
    if(name.size()<5) return false;
    std::string suffix=name.substr(name.size()-4);
    std::transform(suffix.begin(),suffix.end(),suffix.begin(),
        [](unsigned char c){return static_cast<char>(std::tolower(c));});
    return suffix==".eff";
}

bool readLegacy(const std::string& path, std::vector<std::string>& names) {
    LegacyEffectSummary parsed;
    if (!inspectLegacyEff(path, parsed)) return false;
    if (parsed.structured) {
        names = parsed.verifiedResources;
    } else {
        // Conservative fallback for historical variants not yet decoded:
        // keep only recognizable filenames, never random binary fragments.
        for (const auto& item : parsed.candidates) {
            const std::string& candidate = item.candidate;
            if (candidate.size() < 4 || !MagicEffectCatalog::safeName(candidate) ||
                !std::isalnum(static_cast<unsigned char>(candidate.front()))) continue;
            const bool hexOnly = std::all_of(candidate.begin(), candidate.end(), [](unsigned char c) {
                return std::isxdigit(c) != 0;
            });
            if (hexOnly) continue;
            const auto dot = candidate.rfind('.');
            if (dot != std::string::npos) {
                const std::string ext = candidate.substr(dot);
                if (ext != ".eff" && ext != ".par" && ext != ".tga" &&
                    ext != ".dds" && ext != ".png") continue;
            }
            names.push_back(candidate);
        }
    }
    return true;
}

struct BinaryReader {
    const std::vector<unsigned char>& data;
    std::size_t at = 0;
    template <typename T> bool get(T& out) {
        if (at + sizeof(T) > data.size()) return false;
        std::memcpy(&out, data.data() + at, sizeof(T));
        at += sizeof(T);
        return true;
    }
    bool skip(std::size_t bytes) {
        if (at + bytes > data.size()) return false;
        at += bytes;
        return true;
    }
    bool name(std::string& out) {
        if (at + 32 > data.size()) return false;
        const char* text = reinterpret_cast<const char*>(data.data() + at);
        std::size_t size = 0;
        while (size < 32 && text[size]) ++size;
        out.assign(text, size);
        at += 32;
        return true;
    }
};

bool loadParticles(const std::string& path, const std::string& root, std::vector<EffectSystem::ParticleProfile>& out,
                   std::vector<legacy_direct_strip::Definition>* direct = nullptr) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);
    const long length = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (length < 44 || length > 8 * 1024 * 1024) { std::fclose(file); return false; }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    const bool read = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    if (!read) return false;
    BinaryReader in{bytes};
    unsigned version = 0;
    std::string controller;
    int systems = 0;
    float controllerLife = 0.f;
    if (!in.get(version) || version < 2 || version > 15 || !in.name(controller) || !in.get(systems) ||
        systems < 0 || systems > 100 || (version >= 3 && !in.get(controllerLife))) return false;
    std::vector<EffectSystem::ParticleProfile> decoded;
    for (int system = 0; system < systems; ++system) {
        EffectSystem::ParticleProfile profile;
        std::string systemName, textureName, modelName, unusedModelRangeName;
        unsigned short frames = 0;
        if (!in.get(profile.type) || !in.name(systemName) || !in.get(profile.count) || !in.name(textureName) ||
            !in.name(modelName) || !in.get(profile.range[0]) || !in.get(profile.range[1]) || !in.get(profile.range[2]) ||
            !in.get(frames) || frames == 0 || frames > 256) return false;
        profile.sizes.resize(frames);
        profile.frameCount = static_cast<int>(frames);
        for (float& size : profile.sizes) if (!in.get(size)) return false;
        profile.rotations.resize(static_cast<std::size_t>(frames) * 3);
        for (float& angle : profile.rotations) if (!in.get(angle)) return false;
        profile.colors.resize(static_cast<std::size_t>(frames) * 4);
        for (float& color : profile.colors) if (!in.get(color)) return false;
        unsigned char flag = 0;
        if (!in.get(flag)) return false;
        profile.billboard=flag!=0;
        if (!in.get(profile.sourceBlend) || !in.get(profile.destBlend)) return false;
        // D3DTEXF minification/magnification: preserve the authored pair.
        if (!in.get(profile.minFilter) || !in.get(profile.magFilter)) return false;
        if (!in.get(profile.life) || !in.get(profile.velocity) || !in.get(profile.direction[0]) ||
            !in.get(profile.direction[1]) || !in.get(profile.direction[2]) || !in.get(profile.acceleration[0]) ||
            !in.get(profile.acceleration[1]) || !in.get(profile.acceleration[2]) || !in.get(profile.step)) return false;
        bool modelRange = false;
        if (version > 3 && (!in.get(flag) || !in.name(unusedModelRangeName))) return false;
        if (version > 3) modelRange = flag != 0;
        if (version > 4 && (!in.get(profile.offset[0]) || !in.get(profile.offset[1]) ||
                            !in.get(profile.offset[2]))) return false;
        if (version > 5 && (!in.get(profile.delayTime) || !in.get(profile.playTime))) return false;
        if (version > 8) {
            if (!in.get(flag)) return false;
            if (flag) {
                int points = 0;
                if (!in.get(points) || !in.get(profile.pathVelocity) || points < 0 || points > 200) return false;
                profile.pathPoints.resize(static_cast<std::size_t>(points));
                for (auto& point : profile.pathPoints)
                    for (float& coordinate : point) if (!in.get(coordinate)) return false;
                if (points >= 2) {
                    profile.pathDirections.resize(static_cast<std::size_t>(points - 1));
                    profile.pathDistances.resize(static_cast<std::size_t>(points - 1));
                    for (std::size_t segment = 0; segment + 1 < profile.pathPoints.size(); ++segment) {
                        for (float& coordinate : profile.pathDirections[segment])
                            if (!in.get(coordinate)) return false;
                        // CMPPartSys writes the distance as a 12-byte vector;
                        // only X is consumed by CEffPath::FrameMove. Read all
                        // three floats to keep subsequent v10+ fields aligned.
                        float distanceData[3]{};
                        for (float& value : distanceData) if (!in.get(value)) return false;
                        profile.pathDistances[segment] = distanceData[0];
                    }
                }
            }
        }
        if (version > 9 && !in.get(flag)) return false;
        if (version > 10 && !in.skip(32)) return false;
        if (version > 11 && modelRange) {
            unsigned short points = 0;
            if (!in.get(points) || !in.skip(static_cast<std::size_t>(points) * 12)) return false;
        }
        int serializedDivisor = 0;
        if (version > 12 && !in.get(serializedDivisor)) return false;
        profile.randomDivisor = historical_particle_random::decodedDivisor(
            version, profile.type, serializedDivisor);
        if (version > 13 && !in.skip(1)) return false;
        if (version > 14 && !in.skip(1)) return false;
        // Rejeitar sequencias NaN/infinitas antes de liberar dados para
        // a renderizacao. A leitura deve ser atomica: nunca devolver metade
        // de um controlador truncado como se ele estivesse valido.
        auto valid = [](const std::vector<float>& values) {
            return std::all_of(values.begin(), values.end(), [](float v){ return std::isfinite(v); });
        };
        if (!valid(profile.sizes) || !valid(profile.rotations) || !valid(profile.colors)) return false;
        profile.modelName = modelName;
        auto finite3 = [](const float (&v)[3]) {
            return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
        };
        if (!std::isfinite(profile.life) || !std::isfinite(profile.velocity) ||
            !std::isfinite(profile.step) || !std::isfinite(profile.delayTime) ||
            !std::isfinite(profile.playTime) || !std::isfinite(profile.pathVelocity) || !finite3(profile.range) ||
            !finite3(profile.offset) ||
            !finite3(profile.direction) || !finite3(profile.acceleration)) return false;
        for (const auto& point : profile.pathPoints)
            if (!std::all_of(point.begin(), point.end(), [](float v){ return std::isfinite(v); })) return false;
        for (const auto& direction : profile.pathDirections)
            if (!std::all_of(direction.begin(), direction.end(), [](float v){ return std::isfinite(v); })) return false;
        for (float distance : profile.pathDistances)
            if (!std::isfinite(distance) || distance <= 0.f) return false;
        profile.count = std::max(1, std::min(profile.count, 100));
        if (profile.life <= 0.f) profile.life = controllerLife > 0.f && std::isfinite(controllerLife) ? controllerLife : 1.f;
        profile.life = std::min(profile.life, 60.f);
        std::transform(textureName.begin(), textureName.end(), textureName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (textureName.find('.') == std::string::npos) textureName += ".tga";
        if (MagicEffectCatalog::safeName(textureName))
            profile.texture = resolveTexturePath(root + "/texture/effect/" + textureName);
        decoded.push_back(std::move(profile));
    }
    std::vector<legacy_direct_strip::Definition> decodedStrips;
    if (!legacy_direct_strip::decodeTail(bytes,in.at,version,decodedStrips)) return false;
    for (auto& strip : decodedStrips) {
        if (!MagicEffectCatalog::safeName(strip.texture))return false;
        std::string name=strip.texture;
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
        std::string resolved=resolveTexturePath(root+"/texture/effect/"+name+".tga");
        if(resolved.empty())resolved=resolveTexturePath(root+"/texture/effect/"+name+".dds");
        strip.texture=std::move(resolved);
    }
    if (decoded.empty() && decodedStrips.empty()) return false;
    if(direct)direct->insert(direct->end(),std::make_move_iterator(decodedStrips.begin()),
                             std::make_move_iterator(decodedStrips.end()));
    out.insert(out.end(), std::make_move_iterator(decoded.begin()), std::make_move_iterator(decoded.end()));
    return true;
}

// Preserve a model's own texture when it is available. Never infer a
// filename from an unrelated skill ID or a missing legacy binary.
std::string textureFromName(const std::string& root, const std::string& name) {
    if (!MagicEffectCatalog::safeName(name)) return {};
    std::string base = name;
    const auto dot = base.rfind('.');
    if (dot != std::string::npos) base.resize(dot);
    for (const std::string& candidate : {name, base + ".tga", base + ".dds", base + ".png"}) {
        const std::string path = resolveTexturePath(root + "/texture/effect/" + candidate);
        if (!path.empty()) return path;
    }
    return {};
}

float noise(unsigned seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return static_cast<float>(seed & 0xffffu) / 65535.f;
}

std::array<float, 3> pathPosition(const EffectSystem::ParticleProfile& profile, float age) {
    if (profile.pathPoints.size() < 2 ||
        profile.pathDirections.size() + 1 != profile.pathPoints.size() ||
        profile.pathDistances.size() != profile.pathDirections.size()) return {0.f, 0.f, 0.f};
    float remaining = std::max(0.f, profile.pathVelocity * std::max(0.f, age));
    std::size_t segment = 0;
    // CEffPath loops back to point zero after its final segment.
    while (remaining >= profile.pathDistances[segment]) {
        remaining -= profile.pathDistances[segment];
        segment = (segment + 1) % profile.pathDistances.size();
    }
    std::array<float, 3> result{};
    for (std::size_t axis = 0; axis < result.size(); ++axis)
        result[axis] = profile.pathPoints[segment][axis] +
                       profile.pathDirections[segment][axis] * remaining;
    return result;
}

MeshVertex vertex(float x, float y, float z, float r, float g, float b, float a, float u, float v) {
    MeshVertex out{};
    out.x = x;
    out.y = y;
    out.z = z;
    out.r = r;
    out.g = g;
    out.b = b;
    out.a = a;
    out.u = u;
    out.v = v;
    return out;
}

void quad(EffectBatch& batch, float x, float y, float z, float size,
          const EffectViewBasis& view, float r, float g, float b, float alpha) {
    auto corner=[&](float horizontal,float vertical,float u,float v) {
        return vertex(x+size*(horizontal*view.right[0]+vertical*view.up[0]),
                      y+size*(horizontal*view.right[1]+vertical*view.up[1]),
                      z+size*(horizontal*view.right[2]+vertical*view.up[2]),r,g,b,alpha,u,v);
    };
    const auto a=corner(-1,-1,0,1), e=corner(1,-1,1,1);
    const auto d=corner(-1,1,0,0), c=corner(1,1,1,0);
    batch.verts.insert(batch.verts.end(), {a, e, d, e, c, d});
}

// Rotate within the inverse-view basis, preserving the authored aspect ratio.
void quadRotated(EffectBatch& batch, float x, float y, float z, float size,
                 const EffectViewBasis& view, float rotation,
                 float r, float g, float b, float alpha,
                 const legacy_eff::Subeffect::QuadUV* uv = nullptr,
                 float verticalSize = std::numeric_limits<float>::quiet_NaN()) {
    const float height=std::isfinite(verticalSize)?verticalSize:size;
    if (std::fabs(rotation) < 0.00001f && !uv && height==size) {
        quad(batch, x, y, z, size, view, r, g, b, alpha);
        return;
    }
    const auto fallback = legacy_eff::defaultQuadUV();
    const auto& tex = uv ? *uv : fallback;
    const float c = std::cos(rotation);
    const float s = std::sin(rotation);
    auto corner = [&](float right, float up, float u, float v) {
        const float horizontal = right * size * c - up * height * s;
        const float vertical = right * size * s + up * height * c;
        return vertex(x + view.right[0]*horizontal + view.up[0]*vertical,
                      y + view.right[1]*horizontal + view.up[1]*vertical,
                      z + view.right[2]*horizontal + view.up[2]*vertical, r, g, b, alpha, u, v);
    };
    const MeshVertex a = corner(-1.f, -1.f, tex[0][0], tex[0][1]);
    const MeshVertex e = corner( 1.f, -1.f, tex[3][0], tex[3][1]);
    const MeshVertex d = corner(-1.f,  1.f, tex[1][0], tex[1][1]);
    const MeshVertex c1 = corner(1.f,  1.f, tex[2][0], tex[2][1]);
    batch.verts.insert(batch.verts.end(), {a, e, d, e, c1, d});
}

void groundQuad(EffectBatch& batch, float x, float y, float z, float size, float r, float g, float b, float alpha) {
    const MeshVertex a = vertex(x - size, y, z - size, r, g, b, alpha, 0.f, 1.f);
    const MeshVertex c = vertex(x + size, y, z + size, r, g, b, alpha, 1.f, 0.f);
    const MeshVertex d = vertex(x - size, y, z + size, r, g, b, alpha, 0.f, 0.f);
    const MeshVertex e = vertex(x + size, y, z - size, r, g, b, alpha, 1.f, 1.f);
    batch.verts.insert(batch.verts.end(), {a, e, d, e, c, d});
}

void ribbon(EffectBatch& batch, float x0, float y0, float z0, float x1, float y1, float z1, float width,
            float r, float g, float b, float alpha) {
    const MeshVertex a = vertex(x0, y0 - width, z0, r, g, b, alpha, 0.f, 1.f);
    const MeshVertex d = vertex(x0, y0 + width, z0, r, g, b, alpha, 0.f, 0.f);
    const MeshVertex e = vertex(x1, y1 - width, z1, r, g, b, alpha, 1.f, 1.f);
    const MeshVertex c = vertex(x1, y1 + width, z1, r, g, b, alpha, 1.f, 0.f);
    batch.verts.insert(batch.verts.end(), {a, e, d, e, c, d});
}

}  // namespace

bool EffectSystem::open(const std::string& clientRoot) {
    if (const char* trace=std::getenv("TOP_TRACE_SKILL_FX")) {
        if (*trace && *trace!='0')
            std::fprintf(stderr, "[eff-timeline] mode=%s | source=DX9 CMPModelEff::FrameMove\n",
                         legacy_eff::historicalTimeMode() ? "duration-dx9" : "absolute-legacy-gl");
    }
    root_ = clientRoot;
    texture_.clear(); // open() repetido nao deve reutilizar textura do mapa anterior
    resourceCache_.clear();
    magicEffects_.load(root_ + "/scripts/table/magicsingleinfo.txt");
    magicGroups_.load(root_ + "/scripts/table/magicgroupinfo.txt");
    sceneEffects_.load(root_ + "/scripts/table/sceneffectinfo.txt");
    live_.clear();
    batches_.clear();
    report_ = {};
    castParticles_.clear();
    hitParticles_.clear();
    skillVisuals_.load(root_ + "/scripts/table/skillinfo.txt");
    report_.skillVisualRecords = static_cast<int>(skillVisuals_.size());
    report_.magicEffectRecords = static_cast<int>(magicEffects_.size());
    report_.magicGroupRecords = static_cast<int>(magicGroups_.size());
    report_.sceneEffectRecords = static_cast<int>(sceneEffects_.size());
    const auto* defaultEffect = magicEffects_.find(38);
    const std::string effectName = defaultEffect && !defaultEffect->models.empty() ? defaultEffect->models.front() : std::string{};
    report_.tableLoaded = !effectName.empty();
    report_.legacyFile = effectName;
    if (effectName.empty()) return false;

    std::string path = root_ + "/effect/" + effectName;
    std::vector<std::string> refs;
    report_.legacyLoaded = readLegacy(path, refs);
    report_.references = static_cast<int>(refs.size());

    // O .eff guarda nomes de subefeitos e, em algumas versoes, o nome-base
    // da textura sem extensao. Resolve primeiro esses nomes e conserva um
    // fallback do mesmo conjunto visual do Spiritual Bolt.
    for (const std::string& ref : refs) {
        std::string base = ref;
        const std::size_t dot = base.find('.');
        if (dot != std::string::npos) base.resize(dot);
        const std::string candidates[] = {base + ".tga", base + ".TGA", base + ".dds", base + ".png"};
        for (const std::string& name : candidates) {
            const std::string candidate = resolveTexturePath(root_ + "/texture/effect/" + name);
            if (!candidate.empty()) {
                texture_ = candidate;
                break;
            }
        }
        if (!texture_.empty()) break;
    }
    if (texture_.empty()) {
        const char* fallback[] = {"hpush01.tga", "firelighting02.tga", "eff0212.tga"};
        for (const char* name : fallback) {
            const std::string candidate = resolveTexturePath(root_ + "/texture/effect/" + name);
            if (!candidate.empty()) {
                texture_ = candidate;
                break;
            }
        }
    }
    report_.textureFile = texture_;
    auto particlePath = [&](const char* name) {
        std::string path = root_ + "/effect/" + name;
        return path;
    };
    if (loadParticles(particlePath("hpushf.par"), root_, castParticles_)) ++report_.particleFiles;
    if (loadParticles(particlePath("hpusht.par"), root_, hitParticles_)) ++report_.particleFiles;
    report_.particleSystems = static_cast<int>(castParticles_.size() + hitParticles_.size());
    return report_.legacyLoaded && !texture_.empty();
}

// Lazy resource resolution: common effects load only when first used.
// Missing or partially converted historical files intentionally fall back
// to the generic 05-F billboards instead of hiding the ability animation.
const EffectSystem::VisualResource& EffectSystem::resourceFor(int visualId) {
    const auto existing = resourceCache_.find(visualId);
    if (existing != resourceCache_.end()) return existing->second;
    // Bounded cache for modded/invalid effect IDs, not an unbounded set.
    if (resourceCache_.size() >= 128) resourceCache_.clear();
    VisualResource resolved;
    if (const SceneEffectDefinition* sceneEffect = sceneEffects_.find(visualId)) {
        std::vector<ParticleProfile> particles;
        if (loadParticles(root_ + "/effect/" + sceneEffect->particle, root_, particles, &resolved.directStrips)) {
            for (auto& profile : particles) {
                if (resolved.particles.size() >= 64) break;
                if (!profile.texture.empty() || (effectModelName(profile.modelName) &&
                    MagicEffectCatalog::safeName(profile.modelName)))
                    resolved.particles.push_back(std::move(profile));
            }
        }
    }
    // skillinfo stores magicsingleinfo references as 1000 + table ID;
    // sceneffectinfo references retain their own unprefixed namespace.
    const int magicId = visualId >= 1000 && visualId < 1100 ? visualId - 1000 : visualId;
    const MagicEffectDefinition* definition = magicEffects_.find(magicId);
    if (definition) {
        for (const std::string& model : definition->models) {
            if (!MagicEffectCatalog::safeName(model)) continue;
            LegacyEffectSummary parsed;
            if (!inspectLegacyEff(root_ + "/effect/" + model, parsed)) continue;
            std::vector<std::string> refs;
            if (parsed.structured) {
                refs = parsed.verifiedResources;
                float cycleLength = 0.f;
                for (const auto& part : parsed.document.parts)
                    cycleLength = std::max(cycleLength, legacy_eff::duration(part));
                for (const auto& part : parsed.document.parts) {
                    // Only native billboard effect tracks can be represented
                    // by a textured OpenGL quad. Other historical meshes
                    // require the separate DX model geometry converter.
                    if (part.keys.empty()) continue;
                    const bool native = !part.billboard && legacy_mesh::recognized(part.model);
                    if (part.billboard && resolved.billboards.size() >= 16) continue;
                    if (native && resolved.nativeMeshes.size() >= 16) continue;
                    // External effect models are lwGeomObjInfo .lgo files.
                    // Only load existing, valid meshes from model/effect.
                    const bool external = !part.billboard && !native &&
                        !legacy_external::filename(part.model).empty();
                    if (!part.billboard && !native && !external) continue;
                    if (external) {
                        if (resolved.externalMeshes.size() >= 8) continue;
                        Model decoded;
                        if (!legacy_external::load(root_, part.model, decoded)) continue;
                        std::vector<std::string> frames;
                        frames.reserve(part.frames.size());
                        for (const auto& frame : part.frames)
                            frames.push_back(textureFromName(root_, frame));
                        resolved.externalMeshes.push_back({part, std::move(decoded),
                            textureFromName(root_, part.texture), std::move(frames), cycleLength});
                        continue;
                    }
                    if (native) {
                        legacy_mesh::Primitive validation;
                        if (!legacy_mesh::make(part.model, legacy_mesh::paramsAt(part, 0.f), validation))
                            continue; // unsafe mesh dimensions or unsupported source
                    }
                    std::string resolvedTexture = textureFromName(root_, part.texture);
                    if (resolvedTexture.empty()) {
                        for (const auto& frame : part.frames) {
                            resolvedTexture = textureFromName(root_, frame);
                            if (!resolvedTexture.empty()) break;
                        }
                    }
                    if (resolvedTexture.empty()) continue;
                    std::vector<std::string> resolvedFrames;
                    resolvedFrames.reserve(part.frames.size());
                    for (const auto& frame : part.frames)
                        resolvedFrames.push_back(textureFromName(root_, frame));
                    if (native) resolved.nativeMeshes.push_back({part, std::move(resolvedTexture), std::move(resolvedFrames), cycleLength});
                    else resolved.billboards.push_back({part, std::move(resolvedTexture), std::move(resolvedFrames), cycleLength});
                }
            } else {
                readLegacy(root_ + "/effect/" + model, refs);
            }
            for (const auto& ref : refs) {
                if (resolved.texture.empty()) resolved.texture = textureFromName(root_, ref);
            }
            if (resolved.texture.empty()) resolved.texture = textureFromName(root_, model);
            // The table can list several *simultaneous* .eff models for one
            // magic ID (for example Frozen Arrow and Black Dragon's Breath).
            // The previous early break discarded every model after the first.
            // Each track vector has an independent hard cap above.
        }
        auto tryPart = [&](const std::string& name) {
            if (!MagicEffectCatalog::safeName(name)) return;
            std::string filename = name;
            if (filename.size() < 4 || filename.substr(filename.size()-4) != ".par") filename += ".par";
            std::vector<ParticleProfile> parsed;
            if (!loadParticles(root_ + "/effect/" + filename, root_, parsed, &resolved.directStrips)) return;
            for (auto& profile : parsed) {
                if (resolved.particles.size() >= 64) break;
                if (!profile.texture.empty() || (effectModelName(profile.modelName) &&
                    MagicEffectCatalog::safeName(profile.modelName)))
                    resolved.particles.push_back(std::move(profile));
            }
        };
        for (const std::string& part : definition->particles) tryPart(part);
        // Result is played by CMagicCtrl::Stop(), at the projectile's
        // arrival. It must not be added to the in-flight emitters here.
    }
    resolveNestedModels(resolved);
    return resourceCache_.emplace(visualId, std::move(resolved)).first->second;
}

void EffectSystem::resolveNestedModels(VisualResource& resource) {
    // MindPower model-rendering .par systems embed an I_Effect, which can in turn
    // contain primitive geometry, billboards or external .lgo mesh tracks.
    // Reuse the existing .lgo decoder used by standalone .eff tracks.
    std::unordered_map<std::string,std::shared_ptr<const Model>> externalModels;
    for (ParticleProfile& profile : resource.particles) {
        // SHADE uses CMPShadeCtrl::Render, bypassing the model branch.
        if(profile.type==13)continue;
        if (!MagicEffectCatalog::safeName(profile.modelName) ||
            !effectModelName(profile.modelName)) continue;
        const char* traceText = std::getenv("TOP_TRACE_SKILL_FX");
        const bool traceNested = traceText && traceText[0] && traceText[0] != '0';
        LegacyEffectSummary document;
        if (!inspectLegacyEff(root_ + "/effect/" + profile.modelName, document) ||
            !document.structured) {
            if (traceNested) std::fprintf(stderr,
                "[skill-fx] nested-eff=%s status=missing_or_unreadable\n",
                profile.modelName.c_str());
            continue;
        }
        float cycleLength=0.f;
        for(const auto& part:document.document.parts)
            cycleLength=std::max(cycleLength,legacy_eff::duration(part));
        for (const auto& part : document.document.parts) {
            if (profile.modelTracks.size() >= 8) break;
            if (part.keys.empty()) continue;
            const bool primitive = legacy_mesh::recognized(part.model);
            const std::string externalName = !part.billboard && !primitive ?
                legacy_external::filename(part.model) : std::string{};
            if (!part.billboard && !primitive && externalName.empty()) continue;
            std::shared_ptr<const Model> external;
            if (!externalName.empty()) {
                const auto it = externalModels.find(externalName);
                if (it != externalModels.end()) {
                    external = it->second;
                } else {
                    Model decoded;
                    if (!legacy_external::load(root_, externalName, decoded)) {
                        if (traceNested) std::fprintf(stderr,
                            "[skill-fx] nested-eff=%s external=%s status=missing_or_unsupported_lgo\n",
                            profile.modelName.c_str(), externalName.c_str());
                        continue;
                    }
                    external = std::make_shared<const Model>(std::move(decoded));
                    externalModels.emplace(externalName, external);
                }
            }
            const std::string tex = textureFromName(root_, part.texture);
            std::vector<std::string> frames;
            for (const auto& frame : part.frames)
                frames.push_back(textureFromName(root_, frame));
            bool usable = !tex.empty();
            for (const auto& frame : frames) usable |= !frame.empty();
            if (external) {
                for (const auto& geom : external->geoms)
                    for (const auto& subset : geom.subsets)
                        usable |= !textureFromName(root_, subset.texture).empty();
            }
            if (!usable) {
                if (traceNested) std::fprintf(stderr,
                    "[skill-fx] nested-eff=%s model=%s status=texture_unresolved\n",
                    profile.modelName.c_str(), part.model.c_str());
                continue; // no invented texture or white placeholder
            }
            if (traceNested && external) std::fprintf(stderr,
                "[skill-fx] nested-eff=%s external=%s status=loaded geoms=%zu\n",
                profile.modelName.c_str(), externalName.c_str(), external->geoms.size());
            profile.modelTracks.push_back({part, tex, std::move(frames), std::move(external), cycleLength});
        }
        if (profile.type == 6 && !profile.modelTracks.empty()) {
            if (const char* trace = std::getenv("TOP_TRACE_STRIP_SYSTEM")) {
                if (trace[0] && trace[0] != '0')
                    std::fprintf(stderr,
                        "[strip-system] type=6 model=%s tracks=%zu cycle=%.3f replay=%d\n",
                        profile.modelName.c_str(),profile.modelTracks.size(),
                        profile.modelTracks.front().cycleLength,
                        legacy_strip_system::historicalCyclesEnabled() ? 1 : 0);
            }
        }
    }
}

const EffectSystem::VisualResource& EffectSystem::resultForMagic(int magicId) {
    // Negative keys isolate result controllers from scene IDs and magic IDs.
    // For example, result for magic 20 has key -1020, never visual 1020.
    const int cacheKey = -1000 - magicId;
    const auto cached = resourceCache_.find(cacheKey);
    if (cached != resourceCache_.end()) return cached->second;
    if (resourceCache_.size() >= 128) resourceCache_.clear();
    VisualResource resolved;
    const auto* magic = magicEffects_.find(magicId);
    if (magic && !magic->result.empty() && MagicEffectCatalog::safeName(magic->result)) {
        std::string filename = magic->result;
        if (filename.size() < 4 || filename.substr(filename.size()-4) != ".par")
            filename += ".par";
        std::vector<ParticleProfile> profiles;
        bool loaded = loadParticles(root_ + "/effect/" + filename, root_, profiles, &resolved.directStrips);
        // Some source tables omit the leading zero in numeric part names:
        // 1030006 -> 01030006.par. Try the literal name first so mods that
        // ship their own seven-digit file retain priority.
        if (!loaded) {
            const auto stem = filename.substr(0, filename.size()-4);
            const bool numeric = !stem.empty() && stem.size() < 8 &&
                std::all_of(stem.begin(), stem.end(), [](unsigned char c) {
                    return std::isdigit(c) != 0;
                });
            if (numeric) {
                const std::string padded = std::string(8-stem.size(), '0') + stem + ".par";
                loaded = loadParticles(root_ + "/effect/" + padded, root_, profiles, &resolved.directStrips);
            }
        }
        if (loaded) {
            for (auto& profile : profiles) {
                if (resolved.particles.size() >= 64) break;
                if (!profile.texture.empty() || (effectModelName(profile.modelName) &&
                    MagicEffectCatalog::safeName(profile.modelName)))
                    resolved.particles.push_back(std::move(profile));
            }
        }
    }
    resolveNestedModels(resolved);
    if (magicId == 20) {
        const char* trace = std::getenv("TOP_TRACE_FROZEN_RESULT");
        if (trace && trace[0]=='1' && trace[1]=='\0') {
            std::fprintf(stderr,"[frozen-result] magic=20 par=%s systems=%zu\n",
                magic?magic->result.c_str():"<missing>",resolved.particles.size());
            for (const auto& p : resolved.particles) {
                std::fprintf(stderr,
                    "[frozen-result] type=%d model=%s tracks=%zu tex=%s delay=%.3f play=%.3f life=%.3f\n",
                    p.type,p.modelName.c_str(),p.modelTracks.size(),
                    p.texture.c_str(),p.delayTime,p.playTime,p.life);
            }
        }
    }
    return resourceCache_.emplace(cacheKey, std::move(resolved)).first->second;
}

bool EffectSystem::selfTest() {
    EffectBatch batch;
    quad(batch, 1.f, 2.f, 3.f, 0.5f, EffectViewBasis{}, 1.f, 1.f, 1.f, 1.f);
    if (batch.verts.size() != 6) return false;
    return std::fabs(batch.verts[0].x - 0.5f) < 0.001f && std::fabs(batch.verts[2].y - 2.5f) < 0.001f;
}

void EffectSystem::setPlayerDummyMatrices(
        std::unordered_map<int, std::array<float, 16>> matrices) {
    playerDummyMatrices_ = std::move(matrices);
}

void EffectSystem::setPlayerStripBodyMatrices(
        std::unordered_map<int,std::array<float,16>> matrices){
    stripBodyMatrices_=std::move(matrices);
}
void EffectSystem::setPlayerStripItemMatrices(
        std::unordered_map<int,std::array<float,16>> matrices){
    stripItemMatrices_=std::move(matrices);
}

void EffectSystem::setPlayerCastAnchor(float worldX, float worldY, float feetHeight) {
    playerCastAnchorValid_ = std::isfinite(worldX) && std::isfinite(worldY) &&
        std::isfinite(feetHeight);
    if (!playerCastAnchorValid_) return;
    playerCastX_ = worldX;
    playerCastY_ = worldY;
    playerCastFeet_ = feetHeight;
}

void EffectSystem::emit(const EffectEvent& event) {
    // Skillinfo may declare multiple actor/item effects at cast and distinct
    // target/ground/water effects at impact. The historical table's channels
    // are authoritative, not the old single-ID castId()/hitId() fallback.
    // The caller still schedules a single cast moment; timing individual
    // item keyframes and attaching dummies are follow-up migration steps.
    struct RequestedId { int id; bool surface; int dummy; bool actor = false; bool item = false; };
    std::vector<RequestedId> ids; // Retain each actor channel's historic dummy.
    const SkillVisual* selectedSkill = skillVisuals_.find(event.variant);
    if (selectedSkill) {
        const SkillVisual* visual = selectedSkill;
        if (event.kind == EffectEvent::Cast) {
            for (const auto& channel : visual->channels) {
                if (channel.kind == SkillVisualChannelKind::Actor ||
                    channel.kind == SkillVisualChannelKind::Item ||
                    channel.kind == SkillVisualChannelKind::ItemExtra)
                    if (channel.effectId > 0) ids.push_back({channel.effectId,false,
                        channel.kind==SkillVisualChannelKind::Actor ? channel.dummy : -1,
                        channel.kind==SkillVisualChannelKind::Actor,
                        channel.kind==SkillVisualChannelKind::Item ||
                        channel.kind==SkillVisualChannelKind::ItemExtra});
            }
            if (ids.empty() && visual->channels.empty() && visual->castId() > 0)
                ids.push_back({visual->castId(),false,-1});
        } else if (event.kind == EffectEvent::Flight) {
            if (visual->flightId() > 0) ids.push_back({visual->flightId(),false,-1});
        } else if (event.kind == EffectEvent::SkillHit) {
            for (const auto& channel : visual->hitChannels(event.surface))
                ids.push_back({channel.effectId, channel.kind==SkillVisualChannelKind::Ground ||
                    channel.kind==SkillVisualChannelKind::Water,-1});
            if (ids.empty() && visual->channels.empty() && visual->hitId() > 0)
                ids.push_back({visual->hitId(),false,-1});
        } else if (visual->hitId() > 0) {
            // Preserve the previous scalar mapping for legacy non-skill events.
            ids.push_back({visual->hitId(),false,-1});
        }
    }
    if (ids.empty() && selectedSkill && event.kind == EffectEvent::SkillHit)
        return; // result of a magic projectile is scheduled separately at arrival
    if (ids.empty()) ids.push_back({0,false,-1}); // Historical missing-data fallback.

    // For debugging the historic Illusion Slash independently of animation:
    // TOP_TRACE_ILLUSION_FX=1 logs the per-channel source and availability.
    // TOP_ILLUSION_HIDE_EFFECT_ID=1018 temporarily hides only that ID.
    // The switches never change the actor pose, movement or combat timings.
    const bool illusion = event.variant == 81;
    const char* traceIllusion = illusion ? std::getenv("TOP_TRACE_ILLUSION_FX") : nullptr;
    const bool trace = traceIllusion && traceIllusion[0] && traceIllusion[0] != '0';
    const char* allSkillTrace = std::getenv("TOP_TRACE_SKILL_FX");
    const bool traceSkill = allSkillTrace && allSkillTrace[0] && allSkillTrace[0] != '0';
    const char* hiddenIdText = illusion ? std::getenv("TOP_ILLUSION_HIDE_EFFECT_ID") : nullptr;
    const int hiddenId = hiddenIdText ? std::atoi(hiddenIdText) : 0;
    const char* eventName = event.kind == EffectEvent::Cast ? "cast" :
        event.kind == EffectEvent::Flight ? "flight" :
        event.kind == EffectEvent::SkillHit ? "hit" : "other";

    // Without a usable authored asset, multiple channel IDs would all draw
    // the same procedural placeholder. Keep at most one such fallback per
    // event, while allowing every channel with a real asset to play.
    bool fallbackAdded = false;
    std::vector<RequestedId> seen;
    // First resolve group magic (2000..2999) into their simple-magic members.
    // Part_sequence in the original DX9 client launches successive elements
    // at 0.2s intervals. Leaving ID 2002 unresolved rendered a fake effect.
    struct ScheduledId {int id; float delay; bool groupMember; float fanAngle; bool surface; int dummy; bool actor; bool item;};
    std::vector<ScheduledId> scheduled;
    for (const auto& requested : ids) {
        const int id=requested.id;
        if (std::any_of(seen.begin(),seen.end(),[&](const RequestedId& other) {
                return other.id==requested.id && other.surface==requested.surface &&
                    other.dummy==requested.dummy && other.actor==requested.actor && other.item==requested.item;
            })) continue;
        seen.push_back(requested);
        if (seen.size() > 8) break;
        if (id >= 2000 && id < 3000) {
            const auto* group = magicGroups_.find(id - 2000);
            if (group) {
                // DX9 Part_fan spreads member trajectories over 0.75 radians.
                // DX9 Part_sequence leaves the destination unchanged and sets
                // a 0.2s delay between consecutive elements.
                for (std::size_t i=0; i<group->members.size(); ++i) {
                    const float fanAngle = group->motion == 0 && group->members.size()>1 ?
                        -.375f + .75f*static_cast<float>(i)/
                            static_cast<float>(group->members.size()-1) : 0.f;
                    scheduled.push_back({1000 + group->members[i],
                        group->motion == 1 ? .2f*static_cast<float>(i) : 0.f,
                        true,fanAngle,requested.surface,requested.dummy,requested.actor,requested.item});
                }
                if (traceSkill) std::fprintf(stderr,
                    "[skill-fx] skill=%d kind=%s group=%d members=%zu motion=%d\n",
                    event.variant,eventName,id,group->members.size(),group->motion);
            }
            // No invented tornado if group metadata is missing or invalid.
            continue;
        }
        scheduled.push_back({id, 0.f, false, 0.f,requested.surface,requested.dummy,requested.actor,requested.item});
    }
    for (const auto& scheduledId : scheduled) {
        const int id = scheduledId.id;
        if (illusion && id > 0 && id == hiddenId) {
            if (trace) std::fprintf(stderr, "[illusion-fx] kind=%s id=%d hidden-by-user\n", eventName, id);
            continue;
        }

        const VisualResource* resource = nullptr;
        bool authored = false;
        if (id > 0 && !root_.empty()) {
            resource = &resourceFor(id);
            authored = !resource->texture.empty() || !resource->particles.empty() ||
                       !resource->billboards.empty() || !resource->nativeMeshes.empty() ||
                       !resource->externalMeshes.empty() || !resource->directStrips.empty();
        }
        // Having a texture filename is not sufficient: an unconverted .eff can
        // expose a texture without any mesh/particles that OpenGL can draw.
        const bool drawable = resource && (!resource->particles.empty() ||
            !resource->billboards.empty() || !resource->nativeMeshes.empty() ||
            !resource->externalMeshes.empty() || !resource->directStrips.empty());
        if (trace) {
            const SceneEffectDefinition* scene = sceneEffects_.find(id);
            const MagicEffectDefinition* magic = magicEffects_.find(
                id >= 1000 && id < 1100 ? id - 1000 : id);
            std::fprintf(stderr, "[illusion-fx] kind=%s id=%d drawable=%d texture=%d source=%s%s%s\n",
                eventName, id, int(drawable), int(resource && !resource->texture.empty()),
                scene ? "scene:" : magic ? "magic:" : "unknown",
                scene ? scene->particle.c_str() :
                    magic && !magic->models.empty() ? magic->models.front().c_str() : "none",
                drawable ? "" : " (historical geometry missing)");
        }
        if (traceSkill && event.kind == EffectEvent::Flight &&
            id>=1000 && id<2000) {
            const auto* magic=magicEffects_.find(id-1000);
            if(magic) std::fprintf(stderr,
                "[skill-motion] skill=%d magic=%d mode=%d speed=%.2f name=%s\n",
                event.variant,id-1000,magic->renderMode,magic->velocity,
                historic_magic::modeName(magic->renderMode));
        }
        if (traceSkill)
            std::fprintf(stderr,
                "[skill-fx] skill=%d kind=%s visual=%d drawable=%d particles=%zu "
                "native=%zu billboard=%zu external=%zu delay=%.2f\n",
                event.variant,eventName,id,int(drawable),
                resource ? resource->particles.size() : 0,
                resource ? resource->nativeMeshes.size() : 0,
                resource ? resource->billboards.size() : 0,
                resource ? resource->externalMeshes.size() : 0,
                scheduledId.delay);
        if(traceSkill && resource && !resource->directStrips.empty())
            std::fprintf(stderr,"[direct-strip] skill=%d visual=%d strips=%zu owner=%s\n",
                event.variant,id,resource->directStrips.size(),
                scheduledId.item ? "item" : scheduledId.actor ? "actor" : "unbound");
        if (trace && resource && illusion && id == 195) {
            std::fprintf(stderr, "[illusion-fx] impact particle-systems=%zu\n",
                         resource->particles.size());
            for (const auto& profile : resource->particles)
                std::fprintf(stderr,
                    "[illusion-fx] impact par tex=%s type=%d count=%d delay=%.2f play=%.2f life=%.2f\n",
                    profile.texture.c_str(), profile.type, profile.count,
                    profile.delayTime, profile.playTime, profile.life);
            for (const auto& profile : resource->particles)
                if (profile.type == 5 || profile.type == 6)
                    std::fprintf(stderr, "[illusion-fx] nested-model=%s tracks=%zu type=%d\n",
                        profile.modelName.c_str(), profile.modelTracks.size(), profile.type);
        }
        // Old fallback 'Cast' builds a rising *rotating column* of 12 quads.
        // On Illusion Slash that is a fake tornado, not its historical art.
        // Keep real 186/218/1018/195 where drawable; never invent the spiral
        // just because a historical resource is absent or not yet converted.
        // A historical skill/channel identifies a real asset. If it cannot
        // be converted, silence is preferable to fake spinning rings. Keep
        // placeholders only for events with no authored skill information.
        if (!drawable && id > 0 && (selectedSkill || scheduledId.groupMember)) continue;
        if (!authored) {
            if (fallbackAdded) continue;
            fallbackAdded = true;
        }

        Instance effect;
        effect.kind = event.kind;
        effect.age = -scheduledId.delay;
        effect.fromX = event.fromX;
        effect.fromY = event.fromY;
        effect.toX = event.toX;
        effect.toY = event.toY;
        if (scheduledId.fanAngle != 0.f) {
            const float dx = event.toX-event.fromX, dz = event.toY-event.fromY;
            const float c=std::cos(scheduledId.fanAngle),s=std::sin(scheduledId.fanAngle);
            effect.toX=event.fromX+dx*c-dz*s;
            effect.toY=event.fromY+dx*s+dz*c;
        }
        effect.variant = event.variant;
        effect.visualId = id;
        effect.groundAnchored=scheduledId.surface;
        effect.playerActorCast = event.kind==EffectEvent::Cast && event.sourceIsPlayer &&
            scheduledId.actor;
        effect.playerItemCast = event.kind==EffectEvent::Cast && event.sourceIsPlayer &&
            scheduledId.item;
        // Bind only player-owned ACTOR cast channels. Dummies on items and
        // target monsters are different coordinate spaces and remain pending.
        if (event.kind==EffectEvent::Cast && event.sourceIsPlayer &&
            scheduledId.dummy>=0 && scheduledId.dummy<=255)
            effect.actorDummy=scheduledId.dummy;
        effect.seed = serial_++ * 2654435761u;
        effect.life = event.kind == EffectEvent::Flight ?
                          std::max(0.0001f, event.duration > 0.f ? event.duration : 0.28f) :
                      event.kind == EffectEvent::Cast ? 0.75f :
                      event.kind == EffectEvent::SkillHit ? 0.55f :
                      event.kind == EffectEvent::Wake ? 0.8f : 0.45f;
        effect.expiresAfter = effect.life;
        if (resource) {
            effect.directTrails.resize(resource->directStrips.size());
            for (const ParticleProfile& profile : resource->particles) {
                const float emission = profile.delayTime + std::max(0.f, profile.playTime);
                effect.expiresAfter = std::max(effect.expiresAfter, emission + profile.life);
            }
            auto includeTracks = [&](const std::vector<VisualResource::BillboardTrack>& tracks) {
                for (const auto& track : tracks)
                    effect.expiresAfter = std::max(effect.expiresAfter, legacy_eff::duration(track.subeffect));
            };
            includeTracks(resource->billboards);
            includeTracks(resource->nativeMeshes);
            for (const auto& track : resource->externalMeshes)
                effect.expiresAfter = std::max(effect.expiresAfter, legacy_eff::duration(track.subeffect));
            // CMPStrip::IsPlaying is governed by controller play/pose, not a
            // 100*step artificial tail. Keep the existing action life here.
            // The DX9 CMagicCtrl::Part_fly/Part_dist stops the projectile on
            // reaching its target. The authored .eff track length describes
            // the reusable visual clip, not extra time hovering at impact.
            // Preserve particle-system tails, which are emitted separately.
            if (effect.kind == EffectEvent::Flight && resource->particles.empty() &&
                (!resource->nativeMeshes.empty() || !resource->billboards.empty() ||
                 !resource->externalMeshes.empty()))
                effect.expiresAfter = effect.life;
        }
        if (effect.kind == EffectEvent::Flight && id >= 1000 && id < 2000) {
            const auto* magic = magicEffects_.find(id-1000);
            if (magic && historic_magic::knownMode(magic->renderMode)) {
                effect.absoluteHeight = std::isfinite(event.fromHeight) && std::isfinite(event.toHeight);
                effect.path.mode = magic->renderMode;
                effect.path.from = {effect.fromX, effect.absoluteHeight?event.fromHeight:0.f, effect.fromY};
                effect.path.to = {effect.toX, effect.absoluteHeight?event.toHeight:0.f, effect.toY};
                // Character::SkyEffect -> SetVel(sSkySpd/100) overrides nVel.
                // event.duration already represents that skill speed in Play.
                const float planar = std::hypot(effect.toX-effect.fromX,effect.toY-effect.fromY);
                effect.path.speed = event.duration>0.f && planar>1e-7f
                    ? planar/event.duration : magic->velocity;
                if(resource) {
                    for(const auto& t:resource->billboards) effect.path.clipLength=std::max(effect.path.clipLength,t.cycleLength);
                    for(const auto& t:resource->nativeMeshes) effect.path.clipLength=std::max(effect.path.clipLength,t.cycleLength);
                    for(const auto& t:resource->externalMeshes) effect.path.clipLength=std::max(effect.path.clipLength,t.cycleLength);
                }
                if(effect.path.speed>0.f || effect.path.mode==5)
                    effect.life=std::max(.0001f,effect.path.duration());
                else effect.path.mode=-1; // invalid stationary movement retains legacy fallback
                // Stop() terminates every flight component before playing Result.
                effect.expiresAfter=effect.life;
            }
        }
        live_.push_back(effect);
    }
}

void EffectSystem::step(float dt) {
    if (!std::isfinite(dt) || dt < 0.f) return;
    std::vector<Instance> arrivals;
    for (std::size_t i = 0; i < live_.size();) {
        Instance& flight = live_[i];
        flight.age += dt;
        // DirectX CMagicCtrl::Stop() fires its .par Result when the missile
        // reaches the target. A damage HIT/MISS event is a separate channel.
        if (flight.kind == EffectEvent::Flight && !flight.resultStarted &&
            flight.age >= flight.life) {
            flight.resultStarted = true;
            // IDs 1000..1999 address magicsingleinfo; grouped magic already
            // expands to individual simple-magic IDs in EffectSystem::emit.
            if (flight.visualId >= 1000 && flight.visualId < 2000) {
                const int magicId = flight.visualId - 1000;
                const auto* magic = magicEffects_.find(magicId);
                if (magic && !magic->result.empty()) {
                    const VisualResource& resultResource = resultForMagic(magicId);
                    if (!resultResource.particles.empty()) {
                        Instance result;
                        result.kind = EffectEvent::SkillHit; // stationary at target
                        result.variant = flight.variant;
                        result.visualId = -1000 - magicId;
                        result.fromX = flight.fromX;
                        result.fromY = flight.fromY;
                        result.toX = flight.toX;
                        result.toY = flight.toY;
                        if (historic_magic::knownMode(flight.path.mode)) {
                            const auto arrival=flight.path.position(flight.life);
                            result.toX=arrival.x;
                            result.toY=arrival.z;
                            result.absoluteHeight=flight.absoluteHeight;
                            result.path.to=arrival;
                        }
                        result.life = 0.55f;
                        result.age = std::max(0.f, flight.age-flight.life);
                        result.seed = serial_++ * 2654435761u;
                        result.expiresAfter = result.life;
                        // SK2.16-C: opt-in diagnostic for the ice .lgo impact.
                        // The projectile's arrival height is not necessarily the
                        // terrain height. This is intentionally NOT the default:
                        // it lets users distinguish a height/occlusion problem
                        // from missing mesh, UV, texture or fragment output.
                        if (magicId == 20) {
                            if (const char* env = std::getenv("TOP_FROZEN_RESULT_GROUND")) {
                                if (env[0] == '1' && env[1] == '\0') {
                                    result.absoluteHeight = false;
                                    result.groundAnchored = true;
                                }
                            }
                        }
                        for (const auto& profile : resultResource.particles)
                            result.expiresAfter = std::max(result.expiresAfter,
                                profile.delayTime + std::max(0.f, profile.playTime) + profile.life);
                        // Duration of the embedded I_Effect models matters too.
                        for (const auto& profile : resultResource.particles)
                            for (const auto& track : profile.modelTracks)
                                result.expiresAfter = std::max(result.expiresAfter,
                                    profile.delayTime + legacy_eff::duration(track.subeffect));
                        if (result.age < result.expiresAfter) arrivals.push_back(result);
                        const char* trace = std::getenv("TOP_TRACE_SKILL_FX");
                        if (trace && trace[0] && trace[0] != '0') {
                            std::size_t nestedTracks = 0;
                            for (const auto& profile : resultResource.particles)
                                nestedTracks += profile.modelTracks.size();
                            std::fprintf(stderr,
                                "[skill-fx] skill=%d kind=result visual=%d magic=%d par=%s systems=%zu nested_tracks=%zu at=(%.2f,%.2f)\n",
                                flight.variant, result.visualId, magicId,
                                magic->result.c_str(), resultResource.particles.size(),
                                nestedTracks, result.toX, result.toY);
                        }
                    }
                }
            }
        }
        if (flight.age >= flight.expiresAfter)
            live_.erase(live_.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
    }
    // Append AFTER iteration: no invalidated references or recursive emission.
    live_.insert(live_.end(), arrivals.begin(), arrivals.end());
}

void EffectSystem::build(MapFile& map, float cameraRightX, float cameraRightY) {
    EffectViewBasis view;
    view.right={{cameraRightX,0.f,cameraRightY}};
    build(map,view);
}

void EffectSystem::build(MapFile& map, const EffectViewBasis& view) {
    batches_.clear();
    if (live_.empty()) return;
    const char* frozenTraceEnv = std::getenv("TOP_TRACE_FROZEN_GEOMETRY");
    const bool traceFrozen = frozenTraceEnv && frozenTraceEnv[0] == '1' &&
                             frozenTraceEnv[1] == '\0';
    // Per result, layer and quarter-second. Cap growth for long sessions.
    static std::set<std::tuple<unsigned,int,int>> frozenLogged;
    const auto movementFor = [&](const Instance& effect) -> int {
        if(effect.kind!=EffectEvent::Flight || effect.visualId<1000 ||
           effect.visualId>=2000) return -1;
        const auto* magic=magicEffects_.find(effect.visualId-1000);
        return magic ? magic->renderMode : -1;
    };
    const auto positionFor = [&](const Instance& effect) -> historic_magic::Vec3 {
        // Player actor cast channels track their real dummy at EVERY frame.
        // The matrix is DX row-vector XYZ (Z-up); GL world is X/height/Y.
        if (effect.playerActorCast) {
            if (effect.actorDummy>=0) {
                const auto found=playerDummyMatrices_.find(effect.actorDummy);
                if (found!=playerDummyMatrices_.end()) {
                    const auto& m=found->second;
                    return {m[12],m[14],m[13]};
                }
            }
            // SK2.13-A only followed dummies when an opt-in switch was set.
            // No dummy is not a reason to leave an actor aura at the old spot.
            if (playerCastAnchorValid_)
                return {playerCastX_,playerCastFeet_+.9f,playerCastY_};
        }
        historic_magic::Vec3 p;
        if(effect.kind==EffectEvent::Flight && historic_magic::knownMode(effect.path.mode))
            p=effect.path.position(effect.age);
        else {
            const float travel=effect.kind==EffectEvent::Flight?
                std::clamp(effect.age/std::max(.0001f,effect.life),0.f,1.f):
                (effect.kind==EffectEvent::SkillHit?1.f:0.f);
            p={effect.fromX+(effect.toX-effect.fromX)*travel,
               effect.absoluteHeight?effect.path.to.y:0.f,
               effect.fromY+(effect.toY-effect.fromY)*travel};
        }
        if(!effect.absoluteHeight) {
            const auto tile=map.at(static_cast<int>(p.x),static_cast<int>(p.z));
            p.y+=(tile.valid?map.heightAt(p.x,p.z):0.f)+(effect.groundAnchored?0.f:.9f);
        }
        return p;
    };
    const auto hasAuthoredVisual = [&](const Instance& effect) {
        const auto asset = resourceCache_.find(effect.visualId);
        if (asset == resourceCache_.end()) return false;
        const VisualResource& visual = asset->second;
        if (effect.kind == EffectEvent::Flight)
            return !visual.particles.empty() || !visual.billboards.empty() ||
                   !visual.nativeMeshes.empty() || !visual.externalMeshes.empty() ||
                   !visual.directStrips.empty();
        return !visual.particles.empty() || !visual.billboards.empty() ||
               !visual.nativeMeshes.empty() || !visual.externalMeshes.empty() ||
               !visual.directStrips.empty();
    };
    // SK2.15-A: CMPStrip trails are independent of normal type=6 CMPPartSys.
    // Source DX9 CMPStrip::GetTrack samples TWO real dummy matrices and keeps
    // previous pairs in world coordinates. No texture-only fake ribbon when
    // equipment dummy endpoints are unavailable.
    for (Instance& effect : live_) {
        if(effect.age<0.f || effect.kind!=EffectEvent::Cast ||
           (!effect.playerActorCast&&!effect.playerItemCast))continue;
        const auto asset=resourceCache_.find(effect.visualId);
        if(asset==resourceCache_.end())continue;
        const auto& definitions=asset->second.directStrips;
        const auto& matrices=effect.playerItemCast?stripItemMatrices_:stripBodyMatrices_;
        for(std::size_t i=0;i<definitions.size()&&i<effect.directTrails.size();++i){
            const auto& def=definitions[i];
            if(def.texture.empty())continue;
            const auto a=matrices.find(def.dummyA),b=matrices.find(def.dummyB);
            const char* trace=std::getenv("TOP_TRACE_DIRECT_STRIP");
            const bool traceOn=trace&&trace[0]=='1'&&trace[1]=='\0';
            if(a==matrices.end()||b==matrices.end()){
                if(traceOn&&!effect.directStripMissingLogged){
                    std::fprintf(stderr,"[direct-strip] skill=%d visual=%d owner=%s missing_dummies=%d,%d\n",
                        effect.variant,effect.visualId,effect.playerItemCast?"item":"actor",
                        def.dummyA,def.dummyB);
                    effect.directStripMissingLogged=true;
                }
                continue;
            }
            auto& history=effect.directTrails[i];
            legacy_direct_strip::append(history,def,effect.age,a->second,b->second);
            EffectBatch next;next.mode=4;next.texture=def.texture;
            setEffectBlend(next,def.srcBlend,def.dstBlend);
            legacy_direct_strip::emit(history,def,effect.age,next.verts);
            if(!next.verts.empty()){
                if(traceOn&&!effect.directStripLogged){
                    std::fprintf(stderr,"[direct-strip] skill=%d visual=%d owner=%s texture=%s pairs=%zu triangles=%zu dx9=(%d,%d)\n",
                        effect.variant,effect.visualId,effect.playerItemCast?"item":"actor",
                        def.texture.c_str(),history.pairs.size(),next.verts.size()/3,
                        def.srcBlend,def.dstBlend);
                    effect.directStripLogged=true;
                }
                batches_.push_back(std::move(next));
            }
        }
    }
    for (const Instance& effect : live_) {
        if (effect.age < 0.f) continue;
        // Keep the procedural appearance only when the original effect is
        // unavailable. Drawing both made valid skill .par/.eff resources look
        // like a fast generic ring/orb layered over the historical animation.
        if ((effect.kind == EffectEvent::Cast || effect.kind == EffectEvent::Flight ||
             effect.kind == EffectEvent::SkillHit) && hasAuthoredVisual(effect)) continue;
        EffectBatch batch;
        batch.mode = 4;
        const auto it = resourceCache_.find(effect.visualId);
        batch.texture = it != resourceCache_.end() && !it->second.texture.empty() ? it->second.texture : texture_;
        const float t = std::min(1.f, effect.age / effect.life);
        const float fade = std::min(1.f, (1.f - t) * 2.5f);
        if (effect.kind == EffectEvent::Cast) {
            const Tile tile = map.at(static_cast<int>(effect.fromX), static_cast<int>(effect.fromY));
            const float floor = tile.valid ? map.heightAt(effect.fromX, effect.fromY) : 0.f;
            for (int i = 0; i < 12; ++i) {
                const float angle = static_cast<float>(i) * (2.f * kPi / 12.f) + effect.age * 5.f;
                const float radius = 0.45f + t * 0.75f;
                const float x = effect.fromX + std::sin(angle) * radius;
                const float z = effect.fromY + std::cos(angle) * radius;
                const float y = floor + 0.25f + t * 1.45f + noise(effect.seed + static_cast<unsigned>(i)) * 0.35f;
                quad(batch, x, y, z, 0.16f + 0.1f * (1.f - t), view, 0.45f, 0.72f, 1.f, fade);
            }
        } else if (effect.kind == EffectEvent::Flight ||
                   (effect.kind == EffectEvent::SkillHit && effect.variant >= 900)) {
            // Cannonballs retain the previous projectile behavior. Skill
            // projectiles are emitted BEFORE the impact event, not after it.
            const unsigned visual = static_cast<unsigned>(effect.visualId > 0 ? effect.visualId : effect.variant);
            const float hue = static_cast<float>((visual * 37u) % 100u) / 100.f;
            const bool cannon = effect.variant >= 900;

            const float red = cannon ? 1.f : 0.35f + hue * 0.55f;
            const float green = cannon ? 0.3f : 0.45f + (1.f - hue) * 0.35f;
            const float blue = cannon ? 0.06f : 0.92f - hue * 0.35f;
            const float fly = std::min(1.f, effect.age / (cannon ? 0.28f : effect.life));
            const float x = effect.fromX + (effect.toX - effect.fromX) * fly;
            const float z = effect.fromY + (effect.toY - effect.fromY) * fly;
            const Tile from = map.at(static_cast<int>(effect.fromX), static_cast<int>(effect.fromY));
            const Tile to = map.at(static_cast<int>(effect.toX), static_cast<int>(effect.toY));
            const float y0 = (from.valid ? map.heightAt(effect.fromX, effect.fromY) : 0.f) + 1.25f;
            const float y1 = (to.valid ? map.heightAt(effect.toX, effect.toY) : 0.f) + 0.9f;
            const float y = y0 + (y1 - y0) * fly + std::sin(fly * kPi) * 0.45f;
            quad(batch, x, y, z, (cannon ? 0.55f : 0.42f) - 0.12f * fly, view, red, green, blue, fade);
            for (int i = 0; i < 5; ++i) {
                const float trail = std::max(0.f, fly - static_cast<float>(i + 1) * 0.055f);
                const float tx = effect.fromX + (effect.toX - effect.fromX) * trail;
                const float tz = effect.fromY + (effect.toY - effect.fromY) * trail;
                const float ty = y0 + (y1 - y0) * trail + std::sin(trail * kPi) * 0.45f;
                quad(batch, tx, ty, tz, 0.2f, view, red * .75f, green * .75f, blue, fade * 0.55f);
            }
            if (cannon && fly >= 0.95f) {
                for (int i = 0; i < 10; ++i) {
                    const float angle = noise(effect.seed + static_cast<unsigned>(i * 17)) * 2.f * kPi;
                    const float radius = (effect.age - 0.27f) * (1.2f + noise(effect.seed + static_cast<unsigned>(i)));
                    quad(batch, effect.toX + std::sin(angle) * radius, y1 + std::cos(angle) * radius,
                         effect.toY, cannon ? 0.2f : 0.13f, view,
                         cannon ? 1.f : 0.65f, cannon ? 0.35f : 0.85f, cannon ? 0.05f : 1.f, fade);
                }
            }
        } else if (effect.kind == EffectEvent::SkillHit) {
            // Burst at the target, synchronized with Play::landPlayerAttack.
            const Tile tile = map.at(static_cast<int>(effect.toX), static_cast<int>(effect.toY));
            const float floor = tile.valid ? map.heightAt(effect.toX, effect.toY) : 0.f;
            const unsigned visual = static_cast<unsigned>(effect.visualId > 0 ? effect.visualId : effect.variant);
            const float hue = static_cast<float>((visual * 37u) % 100u) / 100.f;
            for (int i = 0; i < 14; ++i) {
                const float a = static_cast<float>(i) * (2.f * kPi / 14.f);
                const float spread = t * (0.3f + noise(effect.seed + static_cast<unsigned>(i)) * 0.8f);
                quad(batch, effect.toX + std::sin(a) * spread,
                     floor + 0.9f + std::cos(a) * spread, effect.toY,
                     0.12f + 0.08f * (1.f - t), view,
                     0.4f + hue * 0.55f, 0.5f + (1.f - hue) * 0.35f, 0.9f - hue * 0.3f, fade);
            }
        } else if (effect.kind == EffectEvent::Wake) {
            const float dx = effect.toX - effect.fromX;
            const float dz = effect.toY - effect.fromY;
            const float length = std::sqrt(dx * dx + dz * dz);
            const float nx = length > 0.01f ? -dz / length : 1.f;
            const float nz = length > 0.01f ? dx / length : 0.f;
            const float x = effect.fromX;
            const float z = effect.fromY;
            const Tile tile = map.at(static_cast<int>(x), static_cast<int>(z));
            const float floor = tile.valid ? map.heightAt(x, z) : 0.f;
            for (int i = 0; i < 4; ++i) {
                const float spread = (static_cast<float>(i) - 1.5f) * 0.32f;
                const float age = std::min(1.f, effect.age / effect.life);
                const float length = 0.45f + age * 1.15f + static_cast<float>(i) * 0.12f;
                groundQuad(batch, x + nx * spread - dx * age * 0.16f, floor + 0.035f,
                           z + nz * spread - dz * age * 0.16f, length,
                           0.72f, 0.86f, 0.98f, (1.f - age) * 0.38f);
            }
        } else {
            const float x = effect.toX;
            const float z = effect.toY;
            const Tile tile = map.at(static_cast<int>(x), static_cast<int>(z));
            const float floor = tile.valid ? map.heightAt(x, z) : 0.f;
            const float red = effect.kind == EffectEvent::Hurt ? 1.f : 1.f;
            const float green = effect.kind == EffectEvent::Hurt ? 0.18f : 0.62f;
            for (int i = 0; i < 7; ++i) {
                const float angle = noise(effect.seed + static_cast<unsigned>(i * 31)) * 2.f * kPi;
                const float radius = t * (0.35f + noise(effect.seed + static_cast<unsigned>(i)) * 0.7f);
                quad(batch, x + std::sin(angle) * radius, floor + 0.7f + std::cos(angle) * radius, z, 0.12f,
                     view, red, green, 0.18f, fade);
            }
        }
        if (!batch.verts.empty()) batches_.push_back(std::move(batch));
    }

    // 05-I: native I_Effect billboard tracks with authored size, position,
    // alpha, color and Z-rotation keyframes. These are real values read
    // from the historic .eff; no attempt is made to guess missing DX meshes.
    for (const Instance& effect : live_) {
        if (effect.age < 0.f) continue;
        auto asset = resourceCache_.find(effect.visualId);
        if (asset == resourceCache_.end() || asset->second.billboards.empty()) continue;
        const auto origin=positionFor(effect);
        const float baseX=origin.x, baseZ=origin.z;
        const float floor=origin.y-.9f;
        for (const auto& track : asset->second.billboards) {
            const auto& part = track.subeffect;
            const float authoredTime = historic_magic::modelTime(effect.age, track.cycleLength, movementFor(effect));
            if (legacy_eff::framePhase(part, authoredTime).finished) continue;
            const auto key = legacy_eff::sample(part, authoredTime);
            // Billboard X/Y are independent authored scale channels. Do not
            // average them into a square or keep a visible minimum at zero.
            const float width=key.size[0], height=key.size[1];
            // CTexFrame switches texture files with its own authored period.
            // Missing individual frames do not suppress the effect.
            std::string selectedTexture = track.resolvedTexture;
            if (!track.resolvedFrames.empty()) {
                const std::size_t index = legacy_eff::textureFrameIndex(
                    authoredTime, part.textureFrameTime, track.resolvedFrames.size());
                if (!track.resolvedFrames[index].empty())
                    selectedTexture = track.resolvedFrames[index];
            }
            const int blendMode = historicBlendMode(part);
            EffectBatch* dest = nullptr;
            for (auto& existing : batches_) {
                if (existing.texture == selectedTexture && existing.mode == blendMode &&
                    sameEffectBlend(existing, part.sourceBlend, part.destBlend)) {
                    dest = &existing; break;
                }
            }
            if (!dest) {
                batches_.push_back({});
                dest = &batches_.back();
                dest->mode = blendMode;
                setEffectBlend(*dest, part.sourceBlend, part.destBlend);
                dest->texture = selectedTexture;
            }
            const auto uv = legacy_eff::sampleUV(part, authoredTime);
            // MindPower uses (x, y=ground, z=up); OpenGL world uses
            // (x, height, z=ground) as in main.cpp/terrain conversion.
            historic_magic::forEachModelCopy(movementFor(effect), effect.age,
                baseX,baseZ,effect.fromX,effect.fromY,effect.toX,effect.toY,
                [&](float x,float z,float size,float alpha) {
                    quadRotated(*dest, x+key.position[0], floor+.9f+key.position[2],
                        z+key.position[1], width*.3f*size*
                        frozenArrowMultiplier(effect.variant,effect.visualId),
                        view,key.angle[2],
                        std::clamp(key.color[0],0.f,1.f),std::clamp(key.color[1],0.f,1.f),
                        std::clamp(key.color[2],0.f,1.f),std::clamp(key.color[3]*alpha,0.f,1.f),&uv,
                        height*.3f*size*frozenArrowMultiplier(effect.variant,effect.visualId));
                });
        }
    }

    // 05-K: predefined CEffectModel shapes. The client registers these seven
    // meshes in CMPResManger::LoadTotalMesh(); they are NOT external .lgo
    // geometry. Transform positions in MindPower local space, then map Z-up
    // to Y-up, preserving the authored texture sequence and per-vertex UVs.
    for (const Instance& effect : live_) {
        if (effect.age < 0.f) continue;
        const auto asset = resourceCache_.find(effect.visualId);
        if (asset == resourceCache_.end()) continue;
        const auto origin=positionFor(effect);
        const float baseX=origin.x, baseZ=origin.z;
        const float floor=origin.y-.9f;
        for (const auto& track : asset->second.nativeMeshes) {
            const auto& part = track.subeffect;
            const float at = historic_magic::modelTime(effect.age, track.cycleLength, movementFor(effect));
            if (legacy_eff::framePhase(part, at).finished) continue;
            legacy_mesh::Primitive primitive;
            if (!legacy_mesh::make(part.model, legacy_mesh::paramsAt(part, at), primitive)) continue;
            std::string selected = track.resolvedTexture;
            if (!track.resolvedFrames.empty()) {
                const auto index = legacy_eff::textureFrameIndex(at, part.textureFrameTime,
                                                                 track.resolvedFrames.size());
                if (!track.resolvedFrames[index].empty()) selected = track.resolvedFrames[index];
            }
            if (selected.empty()) continue;
            // DX9 I_Effect::Render respects SRCBLEND/DESTBLEND from the .eff.
            // The mode below is only the old compatibility fallback; the
            // authored D3D factors are forwarded separately. Factor 7 is
            // DESTALPHA, while 6 is INVSRCALPHA.
            const int nativeBlendMode = historicBlendMode(part);
            EffectBatch* dest = nullptr;
            for (auto& batch : batches_) {
                if (batch.mode == nativeBlendMode && batch.texture == selected &&
                    sameEffectBlend(batch, part.sourceBlend, part.destBlend)) {dest = &batch;break;}
            }
            if (!dest) {
                batches_.push_back({});
                dest = &batches_.back();
                dest->mode = nativeBlendMode;
                setEffectBlend(*dest, part.sourceBlend, part.destBlend);
                dest->texture = selected;
            }
            std::vector<legacy_mesh::UV> defaultUV;
            defaultUV.reserve(primitive.points.size());
            for (const auto& point : primitive.points) defaultUV.push_back(point.uv);
            auto authoredUV = legacy_eff::sampleMeshUV(part, at, defaultUV);
            const int movement=movementFor(effect);
            const float heading=historic_magic::knownMode(effect.path.mode)?effect.path.yaw():0.f;
            const float pitch=historic_magic::knownMode(effect.path.mode)?effect.path.pitch():0.f;
            // Legacy 05-K's generic 0.3 world scale makes the 2x2 plane of
            // 04000009.eff only 0.6 m across. The original Illusion Slash
            // should read as a visible ranged wave, not a tiny speck.
            // Optional multiplier permits visual tuning without rebuilding.
            float worldScale = .3f;
            if (effect.kind == EffectEvent::Flight && effect.variant == 81 &&
                effect.visualId == 1018) {
                // DX9 Part_fshade itself performs Scaling(3) after the first
                // draw. Do not stack the earlier guessed 2.75x multiplier.
                float multiplier = movement == historic_magic::kFshade ? 1.f : 2.75f;
                if (const char* raw = std::getenv("TOP_ILLUSION_FX_SCALE")) {
                    char* end = nullptr;
                    const float requested = std::strtof(raw, &end);
                    if (end != raw && *end == '\0' && std::isfinite(requested) &&
                        requested >= 1.f && requested <= 6.f)
                        multiplier = requested;
                }
                worldScale *= multiplier;
            }
            worldScale *= frozenArrowMultiplier(effect.variant,effect.visualId);
            const auto key=legacy_eff::sample(part,at);
            historic_magic::forEachModelCopy(movement,effect.age,
                baseX,baseZ,effect.fromX,effect.fromY,effect.toX,effect.toY,
                [&](float x,float z,float size,float alpha) {
                    auto shaded=key;
                    shaded.color[3]*=alpha;
                    legacy_mesh::emit(primitive,shaded,authoredUV,x,floor+.9f,z,
                                      dest->verts,worldScale*size,heading,pitch);
                });
        }
    }

    // 05-L: geometry authored in model/effect/*.lgo. ModelFile is the same
    // decoder already used for scene/character .lgo objects, not a new parser.
    // Effect UV/keyframe animation is shared with 05-K. One batch per material
    // texture, retaining the generic 05-G fallback if the asset is unsupported.
    for (const Instance& effect : live_) {
        if (effect.age < 0.f) continue;
        const auto asset = resourceCache_.find(effect.visualId);
        if (asset == resourceCache_.end()) continue;
        const auto origin=positionFor(effect);
        const float baseX=origin.x, baseZ=origin.z;
        const float floor=origin.y-.9f;
        for (const auto& track : asset->second.externalMeshes) {
            if (track.model.geoms.empty()) continue;
            const auto& geom = track.model.geoms.front();
            const auto& part = track.subeffect;
            const float at = historic_magic::modelTime(effect.age, track.cycleLength, movementFor(effect));
            if (legacy_eff::framePhase(part, at).finished) continue;
            for (const auto& subset : geom.subsets) {
                legacy_mesh::Primitive primitive;
                if (!legacy_external::subsetPrimitive(geom, subset, at, primitive)) continue;
                const std::string materialTexture = textureFromName(root_, subset.texture);
                std::string frameTexture;
                if (!track.resolvedFrames.empty()) {
                    const auto frame = legacy_eff::textureFrameIndex(
                        at, part.textureFrameTime, track.resolvedFrames.size());
                    frameTexture = track.resolvedFrames[frame];
                }
                const std::string selected = legacy_effect_texture::choose(
                    track.fallbackTexture, materialTexture, frameTexture,
                    legacy_effect_texture::historicalEnabled()).path;
                if (selected.empty()) continue; // no guessed texture
                const int blendMode = historicBlendMode(part);
                EffectBatch* dest = nullptr;
                for (auto& batch : batches_) {
                    if (batch.mode == blendMode && batch.texture == selected &&
                        sameEffectBlend(batch, part.sourceBlend, part.destBlend)) {
                        dest = &batch; break;
                    }
                }
                if (!dest) {
                    batches_.push_back({});
                    dest = &batches_.back();
                    dest->mode = blendMode;
                    setEffectBlend(*dest, part.sourceBlend, part.destBlend);
                    dest->texture = selected;
                }
                std::vector<legacy_mesh::UV> defaultUV;
                defaultUV.reserve(primitive.points.size());
                for (const auto& point : primitive.points) defaultUV.push_back(point.uv);
                const auto animatedUV = legacy_eff::sampleMeshUV(part, at, defaultUV);
                const auto oldCount = dest->verts.size();
                const auto key=legacy_eff::sample(part,at);
                historic_magic::forEachModelCopy(movementFor(effect),effect.age,
                    baseX,baseZ,effect.fromX,effect.fromY,effect.toX,effect.toY,
                    [&](float x,float z,float size,float alpha) {
                        auto shaded=key;
                        shaded.color[3]*=alpha;
                        legacy_mesh::emit(primitive,shaded,animatedUV,
                            x,floor+.9f,z,dest->verts,.3f*size*
                            frozenArrowMultiplier(effect.variant,effect.visualId),
                            historic_magic::knownMode(effect.path.mode)?effect.path.yaw():0.f,
                            historic_magic::knownMode(effect.path.mode)?effect.path.pitch():0.f);
                    });
                // Blend subset material diffuse with the authored effect color.
                const float red = std::isfinite(subset.r) ? std::clamp(subset.r, 0.f, 1.f) : 1.f;
                const float green = std::isfinite(subset.g) ? std::clamp(subset.g, 0.f, 1.f) : 1.f;
                const float blue = std::isfinite(subset.b) ? std::clamp(subset.b, 0.f, 1.f) : 1.f;
                for (std::size_t j=oldCount; j<dest->verts.size(); ++j) {
                    dest->verts[j].r *= red;
                    dest->verts[j].g *= green;
                    dest->verts[j].b *= blue;
                }
            }
        }
    }

    // Sistemas binarios CMPPartCtrl do cliente antigo. Os parametros de
    // quantidade, faixa, vida, velocidade, tamanho e cor agora alimentam os
    // billboards OpenGL; cada textura historica conserva seu proprio lote.
    for (const Instance& effect : live_) {
        if (effect.age < 0.f) continue;
        const auto asset = resourceCache_.find(effect.visualId);
        // An authored .eff mesh without declared .par must remain mesh-only.
        // Previously the empty particle vector fell through to hpushf.par
        // (or hpusht.par), superimposing generic rings on the real flight
        // geometry. In particular, magicsingleinfo ID 18 (visual 1018)
        // declares 04000009.eff and ZERO particle controllers.
        if (asset != resourceCache_.end() && asset->second.particles.empty() &&
            (!asset->second.billboards.empty() || !asset->second.nativeMeshes.empty() ||
             !asset->second.externalMeshes.empty() || !asset->second.directStrips.empty())) continue;
        const std::vector<ParticleProfile>& profiles =
            (asset != resourceCache_.end() && !asset->second.particles.empty()) ? asset->second.particles :
            ((effect.kind == EffectEvent::Cast || effect.kind == EffectEvent::Flight) ? castParticles_ : hitParticles_);
        if (profiles.empty() || (effect.kind != EffectEvent::Cast && effect.kind != EffectEvent::Flight &&
                                 effect.kind != EffectEvent::SkillHit)) continue;
        for (const ParticleProfile& profile : profiles) {
            // CMPPartSys::Create uses an .eff model in every particle type,
            // not just MODEL/STRIP. A controller mask (notably black 1.tga)
            // is not that model's drawable texture. Never substitute it when
            // the declared geometry is missing or unsupported.
            if (effectModelName(profile.modelName) && profile.modelTracks.empty()) continue;
            if (profile.texture.empty() && profile.modelTracks.empty()) continue;
            if (effect.age < profile.delayTime) continue;
            // IsDelay() compares the controller clock with _fPlayTime;
            // MODEL/STRIP Stop() immediately sets _bPlay=false. In particular,
            // an end of 2.3 with delay .3 means 2 seconds, not 2.3 more seconds.
            if((profile.type==5 || profile.type==6) && profile.playTime>0.f &&
               effect.age>=profile.playTime)continue;
            const float particleAge = effect.age - profile.delayTime;
            if (profile.playTime > 0.f && particleAge > profile.playTime + profile.life) continue;
            const float pathAge = profile.playTime > 0.f ? std::min(particleAge, profile.playTime) : particleAge;
            const std::array<float, 3> authoredPath = pathPosition(profile, pathAge);
            EffectBatch* historical = nullptr;
            for (EffectBatch& candidate : batches_) {
                if (candidate.texture == profile.texture &&
                    sameEffectBlend(candidate, profile.sourceBlend, profile.destBlend) &&
                    sameEffectFilter(candidate, profile.minFilter, profile.magFilter)) {
                    historical = &candidate; break;
                }
            }
            if (!historical) {
                EffectBatch next;
                next.mode = 4;
                setEffectBlend(next, profile.sourceBlend, profile.destBlend);
                setEffectFilter(next, profile.minFilter, profile.magFilter);
                next.texture = profile.texture;
                batches_.push_back(std::move(next));
                historical = &batches_.back();
            }
            const bool singleModel=profile.type==5 || profile.type==6;
            const float curveAge=profile.type==6?0.f:
                (profile.type==5?std::fmod(particleAge,profile.life):particleAge);
            const float local = std::min(1.f, curveAge / std::max(0.0001f, profile.life));
            const legacyfx::CurveSample curve = legacyfx::sample(
                profile.sizes, profile.colors, profile.rotations, local);
            // 05-BH: the Illusion Slash's historical target impact (195)
            // was shrunk by the generic 0.12 coefficient as well. Keep the
            // authored curve; change only its final world-space scale.
            // Tune only the linked Illusion Slash impact; the same .eff
            // renderer is shared by many other historical skills. Preserve
            // authored curves and allow visual tuning without recompilation.
            float impactScale = 1.f;
            if (effect.kind == EffectEvent::SkillHit && effect.variant == 81 &&
                effect.visualId == 195) {
                impactScale = 2.5f; // SK2.8: user-confirmed provisional impact size
                if (const char* raw = std::getenv("TOP_ILLUSION_IMPACT_SCALE")) {
                    char* end = nullptr;
                    const float requested = std::strtof(raw, &end);
                    if (end != raw && *end == '\0' && std::isfinite(requested) &&
                        requested >= .5f && requested <= 12.f)
                        impactScale = requested;
                }
            }
            impactScale *= frozenArrowMultiplier(effect.variant,effect.visualId);
            const int count = singleModel ? 1 : std::min(profile.count, 24);
            // SK2.16-A: evaluate births/deaths for SNOW/FIRE/BLAST separately.
            // All other particle types, including the STRIP and MODEL fixes,
            // keep their existing timeline and geometry paths.
            const bool usePool = historical_particle_pool::enabled() &&
                                 (profile.type == 1 || profile.type == 2 || profile.type == 3);
            // Historical motion requires per-slot ages from SK2.16-A; neither
            // switch alone changes the legacy motion path.
            const bool useHistoricalMotion = usePool && historical_particle_motion::enabled();
            const float stopAt = profile.playTime > 0.f ?
                std::max(0.f,profile.playTime-profile.delayTime) : -1.f;
            const auto origin=positionFor(effect);
            const float baseX=origin.x+profile.offset[0]+authoredPath[0];
            const float baseZ=origin.z+profile.offset[1]+authoredPath[1];
            const float floor=origin.y-.9f+profile.offset[2]+authoredPath[2];
            for (int particle = 0; particle < count; ++particle) {
                const unsigned key = effect.seed + static_cast<unsigned>(particle * 101 + profile.type * 977);
                const float nx = noise(key) * 2.f - 1.f;
                const float ny = noise(key + 1u) * 2.f - 1.f;
                const float nz = noise(key + 2u) * 2.f - 1.f;
                const auto individual = usePool ? historical_particle_pool::at(
                    profile.type,particle,count,particleAge,profile.life,
                    profile.step,stopAt,effect.seed,profile.randomDivisor) : historical_particle_pool::Sample{};
                if (usePool && !individual.alive) continue;
                const float age = usePool ? individual.age :
                    particleAge + static_cast<float>(particle) * std::max(0.f, profile.step);
                float particleLocal = usePool ? individual.normalized : local;
                const legacyfx::CurveSample particleCurve = usePool ?
                    legacyfx::sample(profile.sizes,profile.colors,profile.rotations,
                                     particleLocal) : curve;
                const float size = particleCurve.size * 0.12f * impactScale;
                const float* color = particleCurve.color;
                float x = baseX + nx * profile.range[0] * 0.22f + profile.direction[0] * profile.velocity * age * 0.12f;
                float y = floor + 0.65f + nz * profile.range[2] * 0.18f +
                                profile.direction[2] * profile.velocity * age * 0.12f +
                                0.5f * profile.acceleration[2] * age * age * 0.08f;
                float z = baseZ + ny * profile.range[1] * 0.22f + profile.direction[1] * profile.velocity * age * 0.12f;
                if (useHistoricalMotion) {
                    // CMPPartSys authored positions use DX9 X/Y horizontal,
                    // Z vertical. No ad-hoc .12/.18/.22 speed/range factors.
                    const historical_particle_motion::V3 range{{profile.range[0],
                        profile.range[1],profile.range[2]}};
                    const historical_particle_motion::V3 direction{{profile.direction[0],
                        profile.direction[1],profile.direction[2]}};
                    const historical_particle_motion::V3 acceleration{{profile.acceleration[0],
                        profile.acceleration[1],profile.acceleration[2]}};
                    // Use the pool's generation when enabled, otherwise one
                    // stable seeded particle motion for the legacy lifetime.
                    const auto pose=historical_particle_motion::at(profile.type,
                        range,direction,acceleration,profile.velocity,age,
                        effect.seed,particle,usePool?individual.generation:0u,
                        profile.randomDivisor);
                    if (profile.type==1 && std::fabs(pose.velocity[2])<1e-8f)
                        continue; // _CreateSnow returns false for zero vertical motion
                    x=baseX+pose.position[0];
                    z=baseZ+pose.position[1];
                    y=floor+.9f+pose.position[2];
                }
                if(singleModel) {
                    // MoveTo subtracts range/2. MODEL adds it back in
                    // _FrameMoveModel; STRIP deliberately retains that corner.
                    const float corner=profile.type==6?.5f:0.f;
                    x=baseX-profile.range[0]*corner;
                    y=floor+.9f-profile.range[2]*corner;
                    z=baseZ-profile.range[1]*corner;
                }
                if(profile.type==4) {
                    // _CreateRipple samples the emitter range; _FrameMoveRipple
                    // animates size/color but does not translate the particle.
                    x=baseX+nx*profile.range[0]*.5f;
                    y=floor+.9f+nz*profile.range[2]*.5f;
                    z=baseZ+ny*profile.range[1]*.5f;
                }
                const float alpha = color[3]; // track RGBA historico inclui o fade
                if((singleModel || profile.type==4) && legacy_mesh::recognized(profile.modelName)) {
                    if(profile.billboard) {
                        // RectPlane spans -0.5..+0.5; quadRotated takes a half-size.
                        quadRotated(*historical,x,y,z,particleCurve.size*.15f*impactScale,
                            view,0.f,
                            color[0],color[1],color[2],alpha);
                    } else {
                        legacy_mesh::Primitive primitive;
                        if(legacy_mesh::make(profile.modelName,{},primitive)) {
                            legacy_eff::Key pose;
                            pose.size={particleCurve.size,particleCurve.size,particleCurve.size};
                            if(singleModel)for(int c=0;c<3;++c) pose.angle[c]=legacyfx::sampleTrack(
                                profile.rotations,3,c,particleLocal,0.f);
                            for(int c=0;c<4;++c)pose.color[c]=color[c];
                            legacy_mesh::emit(primitive,pose,{},x,y,z,historical->verts,.3f*impactScale);
                        }
                    }
                    continue;
                }
                // A particle referencing a nested .eff must render its
                // authored geometry+texture, rather than a quad using the
                // (potentially alpha-only) controller texture such as 1.tga.
                if (!profile.modelTracks.empty()) {
                    for (const auto& track : profile.modelTracks) {
                        const auto& part = track.subeffect;
                        const int iceLayer = static_cast<int>(&track - profile.modelTracks.data());
                        const bool traceIce = traceFrozen && effect.visualId == -1020 &&
                                              profile.type == 6 && iceLayer < 6;
                        const int iceTimeBin = std::clamp(
                            static_cast<int>(effect.age * 4.f), 0, 11);
                        bool traceIceNow = false;
                        if (traceIce && frozenLogged.size() < 250) {
                            traceIceNow = frozenLogged.emplace(
                                effect.seed, iceLayer, iceTimeBin).second;
                        }
                        // CMPModelEff restarts the entire .eff when its last
                        // subeffect stops. Short layers stay off until then.
                        // DX9 PARTTICLE_STRIP is a one-particle controller,
                        // but its nested CMPModelEff still animates and loops.
                        // The old `!singleModel` rule incorrectly finished the
                        // child after one cycle for cast/hit type-6 systems.
                        const float modelAge = legacy_strip_system::nestedModelAge(
                            profile.type, effect.kind==EffectEvent::Flight,
                            usePool ? age : particleAge, track.cycleLength,
                            legacy_strip_system::historicalCyclesEnabled());
                        if (legacy_eff::framePhase(part, modelAge).finished) {
                            if (traceIceNow) std::fprintf(stderr,
                                "[frozen-geometry] layer=%d age=%.2f status=finished\n",
                                iceLayer, effect.age);
                            continue;
                        }
                        auto keyframe = legacy_eff::sample(part, modelAge);
                        legacy_mesh::ParentTransform parent;
                        parent.scale=particleCurve.size*impactScale;
                        parent.billboard=profile.billboard;
                        parent.view=view;
                        if(profile.type!=4)for(int c=0;c<3;++c)
                            parent.angle[c]=legacyfx::sampleTrack(profile.rotations,3,c,local,0.f);
                        for (int channel = 0; channel < 4; ++channel)
                            keyframe.color[static_cast<std::size_t>(channel)] *= color[channel];
                        std::string selected = track.texture;
                        if (!track.frames.empty()) {
                            const auto index = legacy_eff::textureFrameIndex(modelAge,
                                part.textureFrameTime, track.frames.size());
                            if (!track.frames[index].empty()) selected = track.frames[index];
                        }
                        // External model tracks can use the material texture of
                        // each subset instead of the parent .eff texture.
                        if (track.externalModel) {
                            const auto& model = *track.externalModel;
                            for (const auto& geom : model.geoms) {
                                for (const auto& subset : geom.subsets) {
                                    legacy_mesh::Primitive primitive;
                                    if (!legacy_external::subsetPrimitive(geom,subset,modelAge,primitive)) {
                                        if (traceIceNow) std::fprintf(stderr,
                                            "[frozen-geometry] layer=%d age=%.2f status=invalid_subset\n",
                                            iceLayer, effect.age);
                                        continue;
                                    }
                                    const std::string materialTexture = textureFromName(root_, subset.texture);
                                    std::string frameTexture;
                                    if (!track.frames.empty()) {
                                        const auto frame=legacy_eff::textureFrameIndex(
                                            modelAge,part.textureFrameTime,track.frames.size());
                                        frameTexture=track.frames[frame];
                                    }
                                    const auto chosenTexture = legacy_effect_texture::choose(
                                        track.texture, materialTexture, frameTexture,
                                        legacy_effect_texture::historicalEnabled());
                                    const std::string& subsetTex = chosenTexture.path;
                                    if (subsetTex.empty()) {
                                        if (traceIceNow) std::fprintf(stderr,
                                            "[frozen-geometry] layer=%d age=%.2f status=missing_texture "
                                            "part_tex=%s subset_tex=%s\n",
                                            iceLayer, effect.age, part.texture.c_str(),
                                            subset.texture.c_str());
                                        continue;
                                    }
                                    const int mode=historicBlendMode(part);
                                    EffectBatch* target=nullptr;
                                    for (auto& candidate:batches_) {
                                        if (candidate.mode==mode && candidate.texture==subsetTex &&
                                            sameEffectBlend(candidate, part.sourceBlend, part.destBlend) &&
                                            sameEffectFilter(candidate, profile.minFilter, profile.magFilter)) {
                                            target=&candidate;break;
                                        }
                                    }
                                    if (!target) {
                                        batches_.push_back({});
                                        target=&batches_.back();
                                        target->mode=mode;
                                        setEffectBlend(*target, part.sourceBlend, part.destBlend);
                                        setEffectFilter(*target, profile.minFilter, profile.magFilter);
                                        target->texture=subsetTex;
                                    }
                                    std::vector<legacy_mesh::UV> defaultUV;
                                    defaultUV.reserve(primitive.points.size());
                                    for (const auto& point:primitive.points)
                                        defaultUV.push_back(point.uv);
                                    const auto uv=legacy_eff::sampleMeshUV(part,modelAge,defaultUV);
                                    const auto before=target->verts.size();
                                    // Keep historic scale/curves; no special
                                    // enlargement for Frozen Arrow.
                                    legacy_mesh::emit(primitive,keyframe,uv,x,y,z,
                                                      target->verts,.3f,0.f,0.f,&parent);
                                    const float red=std::isfinite(subset.r)?std::clamp(subset.r,0.f,1.f):1.f;
                                    const float green=std::isfinite(subset.g)?std::clamp(subset.g,0.f,1.f):1.f;
                                    const float blue=std::isfinite(subset.b)?std::clamp(subset.b,0.f,1.f):1.f;
                                    for (std::size_t j=before;j<target->verts.size();++j) {
                                        target->verts[j].r *= red;
                                        target->verts[j].g *= green;
                                        target->verts[j].b *= blue;
                                    }
                                    if (traceIceNow) {
                                        float minY = std::numeric_limits<float>::infinity();
                                        float maxY = -std::numeric_limits<float>::infinity();
                                        float minAlpha = 1.f, maxAlpha = 0.f;
                                        for (std::size_t j=before;j<target->verts.size();++j) {
                                            const auto& v=target->verts[j];
                                            minY=std::min(minY,v.y); maxY=std::max(maxY,v.y);
                                            minAlpha=std::min(minAlpha,v.a);
                                            maxAlpha=std::max(maxAlpha,v.a);
                                        }
                                        const auto tile=map.at(static_cast<int>(x),static_cast<int>(z));
                                        const float ground=tile.valid ? map.heightAt(x,z) : 0.f;
                                        std::fprintf(stderr,
                                            "[frozen-geometry] layer=%d age=%.2f model_age=%.2f "
                                            "triangles=%zu rgba_alpha=%.3f..%.3f "
                                            "height=%.3f..%.3f terrain=%.3f "
                                            "srcdst=%d,%d texture=%s ground_anchor=%d "
                                            "texture_source=%s eff_tex=%s material_tex=%s\n",
                                            iceLayer,effect.age,modelAge,
                                            (target->verts.size()-before)/3,
                                            minAlpha,maxAlpha,minY,maxY,ground,
                                            part.sourceBlend,part.destBlend,subsetTex.c_str(),
                                            effect.groundAnchored?1:0,
                                            chosenTexture.origin == legacy_effect_texture::Origin::Effect ? "eff" :
                                            chosenTexture.origin == legacy_effect_texture::Origin::Animated ? "frame" :
                                            chosenTexture.origin == legacy_effect_texture::Origin::Material ? "lgo" : "none",
                                            track.texture.c_str(), materialTexture.c_str());
                                    }
                                }
                            }
                            continue;
                        }
                        if (selected.empty()) continue;
                        const int mode = historicBlendMode(part);
                        EffectBatch* target = nullptr;
                        for (EffectBatch& candidate : batches_)
                            if (candidate.mode == mode && candidate.texture == selected &&
                                sameEffectBlend(candidate, part.sourceBlend, part.destBlend) &&
                                sameEffectFilter(candidate, profile.minFilter, profile.magFilter)) {
                                target = &candidate; break;
                            }
                        if (!target) {
                            batches_.push_back({});
                            target = &batches_.back();
                            target->mode = mode;
                            setEffectBlend(*target, part.sourceBlend, part.destBlend);
                            setEffectFilter(*target, profile.minFilter, profile.magFilter);
                            target->texture = selected;
                        }
                        if (part.billboard) {
                            const auto uv=legacy_eff::sampleUV(part,modelAge);
                            // BindingBone on billboard controllers replaces the
                            // child's translation with the binding position.
                            const auto pos=parent.apply(keyframe.position);
                            quadRotated(*target, x+(parent.billboard?0.f:pos[0]),
                                y+(parent.billboard?0.f:pos[2]),
                                z+(parent.billboard?0.f:pos[1]),
                                keyframe.size[0] * .3f * parent.scale,
                                view, keyframe.angle[2],
                                keyframe.color[0], keyframe.color[1],
                                keyframe.color[2], keyframe.color[3],&uv,keyframe.size[1]*.3f*parent.scale);
                        } else {
                            legacy_mesh::Primitive primitive;
                            if (!legacy_mesh::make(part.model,
                                legacy_mesh::paramsAt(part, modelAge), primitive)) continue;
                            std::vector<legacy_mesh::UV> defaultUV;
                            for (const auto& point : primitive.points) defaultUV.push_back(point.uv);
                            const auto uv = legacy_eff::sampleMeshUV(part, modelAge, defaultUV);
                            legacy_mesh::emit(primitive, keyframe, uv,
                                x, y, z, target->verts, .3f,0.f,0.f,&parent);
                        }
                    }
                    continue;
                }
                // Tipos do CMPPartCtrl: neve cai, fogo sobe, blast se afasta;
                // ripple/round/range ficam no plano do terreno e strip/arrow/
                // line conservam uma cauda orientada em vez de um billboard.
                if (!useHistoricalMotion) {
                    if (profile.type == 1) y -= profile.velocity * age * 0.18f;
                    if (profile.type == 2) y += std::fabs(profile.velocity) * age * 0.2f;
                }
                if (profile.type == 12) {
                    // CMPPartCtrl::SHRINK creates each particle in the authored
                    // range and moves it toward the range midpoint. It is the
                    // inverse of BLAST/BLAST3, not another radial burst.
                    // The historical pool emits particles at _fStep intervals;
                    // age them from deterministic staggered birth times.
                    const float shrinkAge = particleAge - static_cast<float>(particle) * std::max(0.f, profile.step);
                    if (shrinkAge < 0.f) continue;
                    const float dx = nx * profile.range[0] * 0.22f;
                    const float dy = nz * profile.range[2] * 0.18f;
                    const float dz = ny * profile.range[1] * 0.22f;
                    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
                    const float travel = std::min(distance,
                        std::fabs(profile.velocity) * shrinkAge * 0.12f);
                    const float fraction = distance > 0.0001f ? travel / distance : 1.f;
                    x = baseX + dx * (1.f - fraction);
                    y = floor + 0.65f + dy * (1.f - fraction);
                    z = baseZ + dz * (1.f - fraction);
                    particleLocal = std::min(1.f, shrinkAge / std::max(0.05f, profile.life));
                    if (fraction >= 1.f || distance <= 0.06f) continue;
                }
                if ((profile.type == 3 && !useHistoricalMotion) || profile.type == 11) {
                    const float radial = std::fabs(profile.velocity) * age * 0.1f;
                    x += nx * radial;
                    y += nz * radial;
                    z += ny * radial;
                }
                if (profile.type == 4 || profile.type == 10 || profile.type == 16 || profile.type == 17) {
                    const float spread = size * (1.f + particleLocal * 2.5f);
                    groundQuad(*historical, x, floor + 0.025f + static_cast<float>(particle) * 0.0005f, z,
                               spread, color[0], color[1], color[2], alpha);
                } else if (profile.type == 7 || profile.type == 9 || profile.type == 18) {
                    float dx = profile.direction[0];
                    float dz = profile.direction[1];
                    float length = std::sqrt(dx * dx + dz * dz);
                    if (length < 0.01f) { dx = effect.toX - effect.fromX; dz = effect.toY - effect.fromY; length = std::sqrt(dx * dx + dz * dz); }
                    if (length < 0.01f) { dx = 0.f; dz = 1.f; length = 1.f; }
                    dx /= length; dz /= length;
                    ribbon(*historical, x - dx * size * 4.f, y, z - dz * size * 4.f, x, y, z, size * .45f,
                           color[0], color[1], color[2], alpha);
                } else {
                    // O numero de keyframes do .par NAO descreve quantas
                    // colunas existem na textura. Nao recortar o atlas UV.
                    quadRotated(*historical, x, y, z, size,
                                view, particleCurve.rotation,
                                color[0], color[1], color[2], alpha);
                }
            }
        }
    }
}

}  // namespace top
