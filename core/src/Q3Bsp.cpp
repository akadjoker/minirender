#include "Q3Bsp.hpp"

#include "Manager.hpp"
#include "RenderPipeline.hpp"
#include "Utils.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
constexpr int32_t kQ3Ident = 1347633737; // "IBSP"
constexpr int32_t kQ3Version = 46;

constexpr int kNumLumps = 17;
constexpr int kLumpEntities = 0;
constexpr int kLumpTextures = 1;
constexpr int kLumpPlanes = 2;
constexpr int kLumpNodes = 3;
constexpr int kLumpLeaves = 4;
constexpr int kLumpLeafFaces = 5;
constexpr int kLumpVertices = 10;
constexpr int kLumpMeshVerts = 11;
constexpr int kLumpFaces = 13;
constexpr int kLumpLightmaps = 14;

constexpr int kFacePolygon = 1;
constexpr int kFacePatch = 2;
constexpr int kFaceMesh = 3;

constexpr size_t kTextureSize = 72;
constexpr size_t kVertexSize = 44;
constexpr size_t kFaceSize = 104;
constexpr size_t kPlaneSize = 16;
constexpr size_t kNodeSize = 36;
constexpr size_t kLeafSize = 48;
constexpr size_t kLightmapBytes = 128 * 128 * 3;

constexpr float kEpsilon = 1e-6f;

bool readFileBytes(const std::string &path, std::vector<uint8_t> &out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;

    in.seekg(0, std::ios::end);
    const std::streamoff sz = in.tellg();
    if (sz <= 0)
        return false;

    out.resize(static_cast<size_t>(sz));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char *>(out.data()), sz);
    return in.good() || in.eof();
}

int32_t readI32(const std::vector<uint8_t> &bytes, size_t offset)
{
    uint32_t raw = 0;
    std::memcpy(&raw, bytes.data() + offset, sizeof(raw));
    raw = SDL_SwapLE32(raw);
    return static_cast<int32_t>(raw);
}

float readF32(const std::vector<uint8_t> &bytes, size_t offset)
{
    uint32_t raw = 0;
    std::memcpy(&raw, bytes.data() + offset, sizeof(raw));
    raw = SDL_SwapLE32(raw);
    float f = 0.0f;
    std::memcpy(&f, &raw, sizeof(f));
    return f;
}

std::string readFixedString(const std::vector<uint8_t> &bytes, size_t offset, size_t len)
{
    std::string s;
    s.reserve(len);
    for (size_t i = 0; i < len; ++i)
    {
        const char c = static_cast<char>(bytes[offset + i]);
        if (c == '\0')
            break;
        s.push_back(c);
    }
    return s;
}

bool getLumpInfo(const std::vector<Q3BspMap::Lump> &lumps,
                 int idx,
                 size_t elemSize,
                 size_t fileSize,
                 size_t &base,
                 size_t &count)
{
    if (idx < 0 || idx >= static_cast<int>(lumps.size()))
        return false;

    const auto &l = lumps[idx];
    if (l.length <= 0)
    {
        base = 0;
        count = 0;
        return true;
    }

    if (l.offset < 0 || l.length < 0)
        return false;

    const size_t begin = static_cast<size_t>(l.offset);
    const size_t len = static_cast<size_t>(l.length);
    if (begin + len > fileSize)
        return false;

    if (elemSize > 0 && (len % elemSize) != 0)
    {
        LogWarning("[Q3BSP] Lump %d has non-multiple length %zu (elem=%zu)",
                   idx, len, elemSize);
    }

    base = begin;
    count = (elemSize > 0) ? (len / elemSize) : 0;
    return true;
}

bool parseQuoted(const std::string &text, size_t &pos, std::string &out)
{
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
        ++pos;
    if (pos >= text.size() || text[pos] != '"')
        return false;

    ++pos; // skip opening quote
    out.clear();
    while (pos < text.size())
    {
        char c = text[pos++];
        if (c == '"')
            return true;
        if (c == '\\' && pos < text.size())
        {
            const char esc = text[pos++];
            switch (esc)
            {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case '\\': out.push_back('\\'); break;
            case '"': out.push_back('"'); break;
            default: out.push_back(esc); break;
            }
        }
        else
        {
            out.push_back(c);
        }
    }

    return false;
}

bool parseVec3(const std::string &s, glm::vec3 &out)
{
    std::istringstream iss(s);
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (!(iss >> x >> y >> z))
        return false;
    out = glm::vec3(x, y, z);
    return true;
}

float parseFloatOr(const std::string *s, float fallback)
{
    if (!s || s->empty())
        return fallback;
    char *end = nullptr;
    const float v = std::strtof(s->c_str(), &end);
    return (end == s->c_str()) ? fallback : v;
}

int parseIntOr(const std::string *s, int fallback)
{
    if (!s || s->empty())
        return fallback;
    char *end = nullptr;
    const long v = std::strtol(s->c_str(), &end, 10);
    return (end == s->c_str()) ? fallback : static_cast<int>(v);
}

int parseBrushModelIndex(const std::string &model)
{
    if (model.size() < 2 || model[0] != '*')
        return -1;
    char *end = nullptr;
    const long v = std::strtol(model.c_str() + 1, &end, 10);
    if (end == model.c_str() + 1)
        return -1;
    return static_cast<int>(v);
}

std::string sanitizeKey(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '/' || c == '-')
        {
            out.push_back(c);
        }
        else
        {
            out.push_back('_');
        }
    }
    return out;
}

struct GroupKey
{
    int tex = -1;
    int lm = -1;

    bool operator==(const GroupKey &o) const
    {
        return tex == o.tex && lm == o.lm;
    }
};

struct GroupKeyHash
{
    size_t operator()(const GroupKey &k) const
    {
        const uint64_t a = static_cast<uint64_t>(static_cast<uint32_t>(k.tex));
        const uint64_t b = static_cast<uint64_t>(static_cast<uint32_t>(k.lm));
        return static_cast<size_t>((a << 32) ^ b);
    }
};

} // namespace

void Q3BspMap::MapEntity::setString(const std::string &key, const std::string &value)
{
    PropertyValue &p = properties[key];
    p.type = PropertyValueType::String;
    p.s = value;
}

void Q3BspMap::MapEntity::setInt(const std::string &key, int value)
{
    PropertyValue &p = properties[key];
    p.type = PropertyValueType::Int;
    p.i = value;
}

void Q3BspMap::MapEntity::setFloat(const std::string &key, float value)
{
    PropertyValue &p = properties[key];
    p.type = PropertyValueType::Float;
    p.f = value;
}

void Q3BspMap::MapEntity::setVec3(const std::string &key, const glm::vec3 &value)
{
    PropertyValue &p = properties[key];
    p.type = PropertyValueType::Vec3;
    p.v3 = value;
}

const PropertyValue *Q3BspMap::MapEntity::findProperty(const std::string &key) const
{
    std::unordered_map<std::string, PropertyValue>::const_iterator it = properties.find(key);
    return (it != properties.end()) ? &it->second : nullptr;
}

bool Q3BspMap::MapEntity::hasProperty(const std::string &key) const
{
    return findProperty(key) != nullptr;
}

const std::string *Q3BspMap::MapEntity::findString(const std::string &key) const
{
    const PropertyValue *p = findProperty(key);
    if (!p || p->type != PropertyValueType::String)
        return nullptr;
    return &p->s;
}

int Q3BspMap::MapEntity::getInt(const std::string &key, int fallback) const
{
    const PropertyValue *p = findProperty(key);
    if (!p)
        return fallback;
    if (p->type == PropertyValueType::Int)
        return p->i;
    if (p->type == PropertyValueType::Float)
        return static_cast<int>(p->f);
    return fallback;
}

float Q3BspMap::MapEntity::getFloat(const std::string &key, float fallback) const
{
    const PropertyValue *p = findProperty(key);
    if (!p)
        return fallback;
    if (p->type == PropertyValueType::Float)
        return p->f;
    if (p->type == PropertyValueType::Int)
        return static_cast<float>(p->i);
    return fallback;
}

bool Q3BspMap::MapEntity::getVec3(const std::string &key, glm::vec3 &out) const
{
    const PropertyValue *p = findProperty(key);
    if (!p || p->type != PropertyValueType::Vec3)
        return false;
    out = p->v3;
    return true;
}

bool Q3BspMap::load(const std::string &bspPath,
                    const std::string &texturesBaseDir,
                    Shader *shader,
                    float scale,
                    float lightmapBrightness)
{
    clear();

    if (!shader)
    {
        LogError("[Q3BSP] load failed: shader is null");
        return false;
    }

    scale_ = (scale > 0.0f) ? scale : 0.03f;

    mapKey_ = PathStem(bspPath);
    if (mapKey_.empty())
        mapKey_ = "q3map";
    mapKey_ = sanitizeKey(mapKey_);

    std::vector<uint8_t> bytes;
    if (!readFileBytes(bspPath, bytes))
    {
        LogError("[Q3BSP] failed to read file: %s", bspPath.c_str());
        return false;
    }

    if (!parse(bytes, lightmapBrightness))
        return false;

    rebuildObjects();

    resolveTextures(texturesBaseDir);

    if (!buildGroups(shader))
    {
        LogError("[Q3BSP] failed to build mesh groups");
        return false;
    }

    ready_ = !groups_.empty();

    LogInfo("[Q3BSP] loaded: faces=%d verts=%d groups=%d textures=%d lightmaps=%d entities=%zu",
            faceCount(),
            vertexCount(),
            static_cast<int>(groups_.size()),
            textureCount(),
            lightmapCount(),
            entities_.size());

    return ready_;
}

void Q3BspMap::setLightmapStrength(float mul)
{
    lightmapMul_ = std::clamp(mul, 0.0f, 16.0f);
    applyLightmapParamsToMaterials();
}

void Q3BspMap::setLightmapGamma(float gamma)
{
    lightmapGamma_ = std::clamp(gamma, 0.05f, 4.0f);
    applyLightmapParamsToMaterials();
}

void Q3BspMap::applyLightmapParamsToMaterials()
{
    for (auto &g : groups_)
    {
        if (!g.material)
            continue;
        g.material->setFloat("u_lightmapMul", lightmapMul_);
        g.material->setFloat("u_lightmapGamma", lightmapGamma_);
    }
}

void Q3BspMap::clear()
{
    auto &meshMgr = MeshManager::instance();
    auto &matMgr = MaterialManager::instance();
    auto &texMgr = TextureManager::instance();

    for (const auto &name : ownedMeshNames_)
        meshMgr.unload(name);
    for (const auto &name : ownedMaterialNames_)
        matMgr.unload(name);
    for (const auto &name : ownedTextureNames_)
        texMgr.unload(name);

    ownedMeshNames_.clear();
    ownedMaterialNames_.clear();
    ownedTextureNames_.clear();

    lumps_.clear();
    rawEntities_.clear();
    entities_.clear();
    textures_.clear();
    vertices_.clear();
    meshVerts_.clear();
    faces_.clear();
    lightmaps_.clear();
    planes_.clear();
    nodes_.clear();
    leaves_.clear();
    leafFaces_.clear();
    groups_.clear();
    faceToGroup_.clear();

    mapKey_.clear();
    ready_ = false;
}

bool Q3BspMap::parse(const std::vector<uint8_t> &bytes, float lightmapBrightness)
{
    return readHeader(bytes) &&
           readEntities(bytes) &&
           readTextures(bytes) &&
           readVertices(bytes) &&
           readMeshVerts(bytes) &&
           readFaces(bytes) &&
           readLightmaps(bytes, lightmapBrightness) &&
           readPlanes(bytes) &&
           readNodes(bytes) &&
           readLeaves(bytes) &&
           readLeafFaces(bytes);
}

bool Q3BspMap::readHeader(const std::vector<uint8_t> &bytes)
{
    if (bytes.size() < 8u + static_cast<size_t>(kNumLumps) * 8u)
    {
        LogError("[Q3BSP] file too small for header");
        return false;
    }

    const int32_t ident = readI32(bytes, 0);
    const int32_t version = readI32(bytes, 4);
    if (ident != kQ3Ident || version != kQ3Version)
    {
        LogError("[Q3BSP] bad header ident=%d version=%d", ident, version);
        return false;
    }

    lumps_.assign(kNumLumps, {});
    size_t off = 8;
    for (int i = 0; i < kNumLumps; ++i)
    {
        lumps_[i].offset = readI32(bytes, off + 0);
        lumps_[i].length = readI32(bytes, off + 4);
        off += 8;
    }

    return true;
}

Q3BspMap::EntityKind Q3BspMap::classifyClassname(const std::string &classname)
{
    if (classname == "worldspawn") return EntityKind::Worldspawn;
    if (classname == "light") return EntityKind::Light;
    if (classname == "info_player_start") return EntityKind::InfoPlayerStart;
    if (classname == "info_player_deathmatch") return EntityKind::InfoPlayerDeathmatch;
    if (classname == "info_player_intermission") return EntityKind::InfoPlayerIntermission;
    if (classname == "func_door") return EntityKind::FuncDoor;
    if (classname == "func_plat") return EntityKind::FuncPlat;
    if (classname == "func_train") return EntityKind::FuncTrain;
    if (classname == "func_button") return EntityKind::FuncButton;
    if (classname == "trigger_multiple") return EntityKind::TriggerMultiple;
    if (classname == "trigger_push") return EntityKind::TriggerPush;
    if (classname == "trigger_teleport") return EntityKind::TriggerTeleport;
    if (classname == "target_position") return EntityKind::TargetPosition;
    if (classname == "target_speaker") return EntityKind::TargetSpeaker;
    if (classname == "misc_model") return EntityKind::MiscModel;
    return EntityKind::Unknown;
}

void Q3BspMap::rebuildObjects()
{
    entities_.clear();
    entities_.reserve(rawEntities_.size());

    for (size_t i = 0; i < rawEntities_.size(); ++i)
    {
        const RawEntity &e = rawEntities_[i];

        MapEntity entity;
        entity.rawEntityIndex = i;
        entity.raw = &e;
        {
            std::unordered_map<std::string, std::string>::const_iterator it = e.properties.begin();
            for (; it != e.properties.end(); ++it)
                entity.setString(it->first, it->second);
        }

        entity.kind = classifyClassname(e.value("classname"));

        const std::string *model = e.find("model");
        if (model)
            entity.setInt("modelBrushIndex", parseBrushModelIndex(*model));

        const std::string *spawnflags = e.find("spawnflags");
        if (spawnflags)
            entity.setInt("spawnflags", parseIntOr(spawnflags, 0));

        const char *floatKeys[] = {"angle", "speed", "wait", "lip", "dmg", "light"};
        const int floatKeyCount = static_cast<int>(sizeof(floatKeys) / sizeof(floatKeys[0]));
        for (int k = 0; k < floatKeyCount; ++k)
        {
            const std::string *value = e.find(floatKeys[k]);
            if (value)
                entity.setFloat(floatKeys[k], parseFloatOr(value, 0.0f));
        }

        glm::vec3 originQ3;
        if (parseVec3(e.value("origin"), originQ3))
        {
            entity.setVec3("originQ3", originQ3);
            entity.setVec3("originLocal", q3ToLocal(originQ3));
        }

        glm::vec3 color;
        const std::string *c1 = e.find("_color");
        const std::string *c2 = e.find("color");
        if ((c1 && parseVec3(*c1, color)) || (c2 && parseVec3(*c2, color)))
            entity.setVec3("color", glm::clamp(color, glm::vec3(0.0f), glm::vec3(8.0f)));

        entities_.push_back(entity);
    }
}

bool Q3BspMap::readEntities(const std::vector<uint8_t> &bytes)
{
    size_t base = 0;
    size_t count = 0;
    if (!getLumpInfo(lumps_, kLumpEntities, 1u, bytes.size(), base, count))
        return false;

    entities_.clear();
    if (count == 0)
        return true;

    const std::string text(reinterpret_cast<const char *>(bytes.data() + base), count);

    size_t pos = 0;
    while (pos < text.size())
    {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
            ++pos;
        if (pos >= text.size())
            break;
        if (text[pos] != '{')
        {
            ++pos;
            continue;
        }
        ++pos; // skip '{'

        RawEntity e;
        std::string key;
        std::string value;

        while (pos < text.size())
        {
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
                ++pos;
            if (pos >= text.size())
                break;

            if (text[pos] == '}')
            {
                ++pos;
                break;
            }

            if (!parseQuoted(text, pos, key))
            {
                ++pos;
                continue;
            }

            if (!parseQuoted(text, pos, value))
                value.clear();

            if (!key.empty())
                e.properties[key] = value;
        }

        if (!e.properties.empty())
            rawEntities_.push_back(std::move(e));
    }

    return true;
}

bool Q3BspMap::readTextures(const std::vector<uint8_t> &bytes)
{
    size_t base = 0;
    size_t count = 0;
    if (!getLumpInfo(lumps_, kLumpTextures, kTextureSize, bytes.size(), base, count))
        return false;

    textures_.clear();
    textures_.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        const size_t o = base + i * kTextureSize;

        TextureEntry t;
        t.name = readFixedString(bytes, o + 0, 64);
        t.flags = readI32(bytes, o + 64);
        t.contents = readI32(bytes, o + 68);
        textures_.push_back(std::move(t));
    }

    return true;
}

bool Q3BspMap::readVertices(const std::vector<uint8_t> &bytes)
{
    size_t base = 0;
    size_t count = 0;
    if (!getLumpInfo(lumps_, kLumpVertices, kVertexSize, bytes.size(), base, count))
        return false;

    vertices_.clear();
    vertices_.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        const size_t o = base + i * kVertexSize;

        const float x = readF32(bytes, o + 0);
        const float y = readF32(bytes, o + 4);
        const float z = readF32(bytes, o + 8);

        const float nx = readF32(bytes, o + 28);
        const float ny = readF32(bytes, o + 32);
        const float nz = readF32(bytes, o + 36);

        BspVertex v;
        // Quake->engine axis remap 
        v.position = {x, z, y};
        v.normal = {nx, nz, ny};
        v.uv = {readF32(bytes, o + 12), readF32(bytes, o + 16)};
        v.lmUv = {readF32(bytes, o + 20), readF32(bytes, o + 24)};
        v.color = {
            static_cast<float>(bytes[o + 40]) / 255.0f,
            static_cast<float>(bytes[o + 41]) / 255.0f,
            static_cast<float>(bytes[o + 42]) / 255.0f,
            static_cast<float>(bytes[o + 43]) / 255.0f};

        vertices_.push_back(v);
    }

    return true;
}

bool Q3BspMap::readMeshVerts(const std::vector<uint8_t> &bytes)
{
    size_t base = 0;
    size_t count = 0;
    if (!getLumpInfo(lumps_, kLumpMeshVerts, sizeof(int32_t), bytes.size(), base, count))
        return false;

    meshVerts_.clear();
    meshVerts_.reserve(count);

    for (size_t i = 0; i < count; ++i)
        meshVerts_.push_back(readI32(bytes, base + i * sizeof(int32_t)));

    return true;
}

bool Q3BspMap::readFaces(const std::vector<uint8_t> &bytes)
{
    size_t base = 0;
    size_t count = 0;
    if (!getLumpInfo(lumps_, kLumpFaces, kFaceSize, bytes.size(), base, count))
        return false;

    faces_.clear();
    faces_.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        const size_t o = base + i * kFaceSize;

        Face f;
        f.textureIdx = readI32(bytes, o + 0);
        f.effectIdx = readI32(bytes, o + 4);
        f.faceType = readI32(bytes, o + 8);
        f.firstVert = readI32(bytes, o + 12);
        f.numVerts = readI32(bytes, o + 16);
        f.firstMeshVert = readI32(bytes, o + 20);
        f.numMeshVerts = readI32(bytes, o + 24);
        f.lmIdx = readI32(bytes, o + 28);
        f.patchW = readI32(bytes, o + 96);
        f.patchH = readI32(bytes, o + 100);

        faces_.push_back(f);
    }

    return true;
}

bool Q3BspMap::readLightmaps(const std::vector<uint8_t> &bytes, float brightness)
{
    size_t base = 0;
    size_t count = 0;
    if (!getLumpInfo(lumps_, kLumpLightmaps, kLightmapBytes, bytes.size(), base, count))
        return false;

    lightmaps_.clear();
    lightmaps_.reserve(count);

    auto &texMgr = TextureManager::instance();

    for (size_t i = 0; i < count; ++i)
    {
        const size_t o = base + i * kLightmapBytes;

        std::vector<uint8_t> rgba(128u * 128u * 4u, 255u);
        for (size_t px = 0; px < 128u * 128u; ++px)
        {
            const size_t src = o + px * 3u;
            const size_t dst = px * 4u;

            const int r = static_cast<int>(std::round(static_cast<float>(bytes[src + 0]) * brightness));
            const int g = static_cast<int>(std::round(static_cast<float>(bytes[src + 1]) * brightness));
            const int b = static_cast<int>(std::round(static_cast<float>(bytes[src + 2]) * brightness));

            rgba[dst + 0] = static_cast<uint8_t>(std::clamp(r, 0, 255));
            rgba[dst + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
            rgba[dst + 2] = static_cast<uint8_t>(std::clamp(b, 0, 255));
            rgba[dst + 3] = 255;
        }

        const std::string texName = mapKey_ + "/lm_" + std::to_string(i);
        Texture *t = texMgr.createFromMemory(texName,
                                             128,
                                             128,
                                             PixelType::RGBA,
                                             rgba.data(),
                                             rgba.size());

        if (t)
        {
            glBindTexture(GL_TEXTURE_2D, t->id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);

            ownedTextureNames_.push_back(texName);
        }

        LightmapEntry lm;
        lm.texture = t;
        lightmaps_.push_back(lm);
    }

    return true;
}

bool Q3BspMap::readPlanes(const std::vector<uint8_t> &bytes)
{
    size_t base = 0;
    size_t count = 0;
    if (!getLumpInfo(lumps_, kLumpPlanes, kPlaneSize, bytes.size(), base, count))
        return false;

    planes_.clear();
    planes_.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        const size_t o = base + i * kPlaneSize;

        const float nx = readF32(bytes, o + 0);
        const float ny = readF32(bytes, o + 4);
        const float nz = readF32(bytes, o + 8);

        Plane p;
        p.normal = {nx, nz, ny};
        p.dist = readF32(bytes, o + 12);
        planes_.push_back(p);
    }

    return true;
}

bool Q3BspMap::readNodes(const std::vector<uint8_t> &bytes)
{
    size_t base = 0;
    size_t count = 0;
    if (!getLumpInfo(lumps_, kLumpNodes, kNodeSize, bytes.size(), base, count))
        return false;

    nodes_.clear();
    nodes_.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        const size_t o = base + i * kNodeSize;

        const int32_t minX = readI32(bytes, o + 12);
        const int32_t minY = readI32(bytes, o + 16);
        const int32_t minZ = readI32(bytes, o + 20);
        const int32_t maxX = readI32(bytes, o + 24);
        const int32_t maxY = readI32(bytes, o + 28);
        const int32_t maxZ = readI32(bytes, o + 32);

        Node n;
        n.plane = readI32(bytes, o + 0);
        n.front = readI32(bytes, o + 4);
        n.back = readI32(bytes, o + 8);
        n.mins = {minX, minZ, minY};
        n.maxs = {maxX, maxZ, maxY};
        nodes_.push_back(n);
    }

    return true;
}

bool Q3BspMap::readLeaves(const std::vector<uint8_t> &bytes)
{
    size_t base = 0;
    size_t count = 0;
    if (!getLumpInfo(lumps_, kLumpLeaves, kLeafSize, bytes.size(), base, count))
        return false;

    leaves_.clear();
    leaves_.reserve(count);

    for (size_t i = 0; i < count; ++i)
    {
        const size_t o = base + i * kLeafSize;

        const int32_t minX = readI32(bytes, o + 8);
        const int32_t minY = readI32(bytes, o + 12);
        const int32_t minZ = readI32(bytes, o + 16);
        const int32_t maxX = readI32(bytes, o + 20);
        const int32_t maxY = readI32(bytes, o + 24);
        const int32_t maxZ = readI32(bytes, o + 28);

        Leaf l;
        l.cluster = readI32(bytes, o + 0);
        l.area = readI32(bytes, o + 4);
        l.mins = {minX, minZ, minY};
        l.maxs = {maxX, maxZ, maxY};
        l.firstLeafFace = readI32(bytes, o + 32);
        l.numLeafFaces = readI32(bytes, o + 36);
        l.firstLeafBrush = readI32(bytes, o + 40);
        l.numLeafBrushes = readI32(bytes, o + 44);
        leaves_.push_back(l);
    }

    return true;
}

bool Q3BspMap::readLeafFaces(const std::vector<uint8_t> &bytes)
{
    size_t base = 0;
    size_t count = 0;
    if (!getLumpInfo(lumps_, kLumpLeafFaces, sizeof(int32_t), bytes.size(), base, count))
        return false;

    leafFaces_.clear();
    leafFaces_.reserve(count);

    for (size_t i = 0; i < count; ++i)
        leafFaces_.push_back(readI32(bytes, base + i * sizeof(int32_t)));

    return true;
}

void Q3BspMap::resolveTextures(const std::string &texturesBaseDir)
{
    auto &texMgr = TextureManager::instance();

    for (size_t i = 0; i < textures_.size(); ++i)
    {
        auto &tx = textures_[i];

        const std::string path = ResolveTexturePath(texturesBaseDir, tx.name);
        if (path.empty())
        {
            LogWarning("[Q3BSP] missing texture: %s", tx.name.c_str());
            tx.texture = nullptr;
            continue;
        }

        const std::string texName = mapKey_ + "/tex_" + std::to_string(i) + "_" + sanitizeKey(tx.name);
        tx.texture = texMgr.load(texName, path);
        if (tx.texture)
            ownedTextureNames_.push_back(texName);
    }
}

bool Q3BspMap::buildGroups(Shader *shader)
{
    groups_.clear();
    faceToGroup_.assign(faces_.size(), -1);

    std::unordered_map<GroupKey, int, GroupKeyHash> keyToGroup;
    std::vector<std::vector<int>> groupFaces;

    const auto isSupportedFace = [&](const Face &f) -> bool {
        if (f.faceType == kFacePolygon || f.faceType == kFaceMesh)
            return f.numMeshVerts > 0;
        if (f.faceType == kFacePatch)
            return f.patchW >= 3 && f.patchH >= 3;
        return false;
    };

    for (int fi = 0; fi < static_cast<int>(faces_.size()); ++fi)
    {
        const Face &f = faces_[fi];
        if (!isSupportedFace(f))
            continue;

        const GroupKey key{f.textureIdx, f.lmIdx};
        auto it = keyToGroup.find(key);
        int gi = -1;
        if (it == keyToGroup.end())
        {
            gi = static_cast<int>(groups_.size());
            keyToGroup.emplace(key, gi);

            MeshGroup g;
            g.textureIndex = f.textureIdx;
            g.lightmapIndex = f.lmIdx;
            groups_.push_back(g);
            groupFaces.emplace_back();
        }
        else
        {
            gi = it->second;
        }

        faceToGroup_[fi] = gi;
        groupFaces[gi].push_back(fi);
    }

    auto &meshMgr = MeshManager::instance();
    auto &matMgr = MaterialManager::instance();
    auto &texMgr = TextureManager::instance();

    Texture *white = texMgr.getWhite();
    const int tess = std::max(1, patchTess_);
    const int row = tess + 1;

    const auto makeVertex = [&](const BspVertex &src) -> Vertex {
        Vertex dst;
        dst.position = src.position * scale_;

        if (glm::dot(src.normal, src.normal) > kEpsilon)
            dst.normal = glm::normalize(src.normal);
        else
            dst.normal = {0.0f, 1.0f, 0.0f};

        // Store lm uv in tangent.xy for the q3bsp shader.
        dst.tangent = {src.lmUv.x, src.lmUv.y, 0.0f, 1.0f};

        // Replicate stbi vertical flip used by the .bu reference.
        dst.uv = {src.uv.x,  src.uv.y};
        return dst;
    };

    const auto patchBasis = [](float t, float &b0, float &b1, float &b2) {
        const float it = 1.0f - t;
        b0 = it * it;
        b1 = 2.0f * t * it;
        b2 = t * t;
    };

    const auto evalScalar = [](float p00, float p10, float p20,
                               float p01, float p11, float p21,
                               float p02, float p12, float p22,
                               float bu0, float bu1, float bu2,
                               float bv0, float bv1, float bv2) -> float {
        const float r0 = p00 * bu0 + p10 * bu1 + p20 * bu2;
        const float r1 = p01 * bu0 + p11 * bu1 + p21 * bu2;
        const float r2 = p02 * bu0 + p12 * bu1 + p22 * bu2;
        return r0 * bv0 + r1 * bv1 + r2 * bv2;
    };

    const auto evalPatchVertex = [&](const BspVertex &c00, const BspVertex &c10, const BspVertex &c20,
                                     const BspVertex &c01, const BspVertex &c11, const BspVertex &c21,
                                     const BspVertex &c02, const BspVertex &c12, const BspVertex &c22,
                                     float u, float v) -> BspVertex {
        float bu0 = 0.0f, bu1 = 0.0f, bu2 = 0.0f;
        float bv0 = 0.0f, bv1 = 0.0f, bv2 = 0.0f;
        patchBasis(u, bu0, bu1, bu2);
        patchBasis(v, bv0, bv1, bv2);

        BspVertex out;
        out.position.x = evalScalar(c00.position.x, c10.position.x, c20.position.x,
                                    c01.position.x, c11.position.x, c21.position.x,
                                    c02.position.x, c12.position.x, c22.position.x,
                                    bu0, bu1, bu2, bv0, bv1, bv2);
        out.position.y = evalScalar(c00.position.y, c10.position.y, c20.position.y,
                                    c01.position.y, c11.position.y, c21.position.y,
                                    c02.position.y, c12.position.y, c22.position.y,
                                    bu0, bu1, bu2, bv0, bv1, bv2);
        out.position.z = evalScalar(c00.position.z, c10.position.z, c20.position.z,
                                    c01.position.z, c11.position.z, c21.position.z,
                                    c02.position.z, c12.position.z, c22.position.z,
                                    bu0, bu1, bu2, bv0, bv1, bv2);

        out.uv.x = evalScalar(c00.uv.x, c10.uv.x, c20.uv.x,
                              c01.uv.x, c11.uv.x, c21.uv.x,
                              c02.uv.x, c12.uv.x, c22.uv.x,
                              bu0, bu1, bu2, bv0, bv1, bv2);
        out.uv.y = evalScalar(c00.uv.y, c10.uv.y, c20.uv.y,
                              c01.uv.y, c11.uv.y, c21.uv.y,
                              c02.uv.y, c12.uv.y, c22.uv.y,
                              bu0, bu1, bu2, bv0, bv1, bv2);

        out.lmUv.x = evalScalar(c00.lmUv.x, c10.lmUv.x, c20.lmUv.x,
                                c01.lmUv.x, c11.lmUv.x, c21.lmUv.x,
                                c02.lmUv.x, c12.lmUv.x, c22.lmUv.x,
                                bu0, bu1, bu2, bv0, bv1, bv2);
        out.lmUv.y = evalScalar(c00.lmUv.y, c10.lmUv.y, c20.lmUv.y,
                                c01.lmUv.y, c11.lmUv.y, c21.lmUv.y,
                                c02.lmUv.y, c12.lmUv.y, c22.lmUv.y,
                                bu0, bu1, bu2, bv0, bv1, bv2);

        out.normal.x = evalScalar(c00.normal.x, c10.normal.x, c20.normal.x,
                                  c01.normal.x, c11.normal.x, c21.normal.x,
                                  c02.normal.x, c12.normal.x, c22.normal.x,
                                  bu0, bu1, bu2, bv0, bv1, bv2);
        out.normal.y = evalScalar(c00.normal.y, c10.normal.y, c20.normal.y,
                                  c01.normal.y, c11.normal.y, c21.normal.y,
                                  c02.normal.y, c12.normal.y, c22.normal.y,
                                  bu0, bu1, bu2, bv0, bv1, bv2);
        out.normal.z = evalScalar(c00.normal.z, c10.normal.z, c20.normal.z,
                                  c01.normal.z, c11.normal.z, c21.normal.z,
                                  c02.normal.z, c12.normal.z, c22.normal.z,
                                  bu0, bu1, bu2, bv0, bv1, bv2);

        if (glm::dot(out.normal, out.normal) > kEpsilon)
            out.normal = glm::normalize(out.normal);
        else
            out.normal = {0.0f, 1.0f, 0.0f};

        out.color = glm::vec4(1.0f);
        return out;
    };

    for (size_t gi = 0; gi < groups_.size(); ++gi)
    {
        const std::string meshName = mapKey_ + "/mesh_" + std::to_string(gi);
        const std::string matName = mapKey_ + "/mat_" + std::to_string(gi);

        Mesh *mesh = meshMgr.create(meshName);
        Material *mat = matMgr.create(matName);
        if (!mesh || !mat)
            continue;

        ownedMeshNames_.push_back(meshName);
        ownedMaterialNames_.push_back(matName);

        mesh->buffer.vertices.clear();
        mesh->buffer.indices.clear();
        mesh->surfaces.clear();
        mesh->materials.clear();

        size_t reserveVerts = 0;
        size_t reserveIndices = 0;
        for (int fi : groupFaces[gi])
        {
            const Face &f = faces_[fi];
            if (f.faceType == kFacePolygon || f.faceType == kFaceMesh)
            {
                const size_t n = static_cast<size_t>(std::max(0, f.numMeshVerts));
                reserveVerts += n;
                reserveIndices += n;
            }
            else if (f.faceType == kFacePatch)
            {
                const int segX = (f.patchW - 1) / 2;
                const int segY = (f.patchH - 1) / 2;
                if (segX > 0 && segY > 0)
                {
                    const size_t segCount = static_cast<size_t>(segX * segY);
                    reserveVerts += segCount * static_cast<size_t>(row * row);
                    reserveIndices += segCount * static_cast<size_t>(tess * tess * 6);
                }
            }
        }
        mesh->buffer.vertices.reserve(reserveVerts);
        mesh->buffer.indices.reserve(reserveIndices);

        for (int fi : groupFaces[gi])
        {
            const Face &f = faces_[fi];
            if (f.faceType == kFacePolygon || f.faceType == kFaceMesh)
            {
                for (int t = 0; t < f.numMeshVerts; ++t)
                {
                    const int mvi = f.firstMeshVert + t;
                    if (mvi < 0 || mvi >= static_cast<int>(meshVerts_.size()))
                        continue;

                    const int mv = meshVerts_[mvi];
                    const int vIdx = f.firstVert + mv;
                    if (vIdx < 0 || vIdx >= static_cast<int>(vertices_.size()))
                        continue;

                    mesh->buffer.vertices.push_back(makeVertex(vertices_[vIdx]));
                    mesh->buffer.indices.push_back(static_cast<uint32_t>(mesh->buffer.vertices.size() - 1));
                }
            }
            else if (f.faceType == kFacePatch)
            {
                for (int by = 0; by + 2 < f.patchH; by += 2)
                {
                    for (int bx = 0; bx + 2 < f.patchW; bx += 2)
                    {
                        const int i00 = f.firstVert + (by + 0) * f.patchW + (bx + 0);
                        const int i10 = f.firstVert + (by + 0) * f.patchW + (bx + 1);
                        const int i20 = f.firstVert + (by + 0) * f.patchW + (bx + 2);
                        const int i01 = f.firstVert + (by + 1) * f.patchW + (bx + 0);
                        const int i11 = f.firstVert + (by + 1) * f.patchW + (bx + 1);
                        const int i21 = f.firstVert + (by + 1) * f.patchW + (bx + 2);
                        const int i02 = f.firstVert + (by + 2) * f.patchW + (bx + 0);
                        const int i12 = f.firstVert + (by + 2) * f.patchW + (bx + 1);
                        const int i22 = f.firstVert + (by + 2) * f.patchW + (bx + 2);

                        if (i00 < 0 || i00 >= static_cast<int>(vertices_.size()) ||
                            i10 < 0 || i10 >= static_cast<int>(vertices_.size()) ||
                            i20 < 0 || i20 >= static_cast<int>(vertices_.size()) ||
                            i01 < 0 || i01 >= static_cast<int>(vertices_.size()) ||
                            i11 < 0 || i11 >= static_cast<int>(vertices_.size()) ||
                            i21 < 0 || i21 >= static_cast<int>(vertices_.size()) ||
                            i02 < 0 || i02 >= static_cast<int>(vertices_.size()) ||
                            i12 < 0 || i12 >= static_cast<int>(vertices_.size()) ||
                            i22 < 0 || i22 >= static_cast<int>(vertices_.size()))
                        {
                            continue;
                        }

                        const BspVertex &c00 = vertices_[i00];
                        const BspVertex &c10 = vertices_[i10];
                        const BspVertex &c20 = vertices_[i20];
                        const BspVertex &c01 = vertices_[i01];
                        const BspVertex &c11 = vertices_[i11];
                        const BspVertex &c21 = vertices_[i21];
                        const BspVertex &c02 = vertices_[i02];
                        const BspVertex &c12 = vertices_[i12];
                        const BspVertex &c22 = vertices_[i22];

                        const uint32_t baseVert = static_cast<uint32_t>(mesh->buffer.vertices.size());

                        for (int iy = 0; iy <= tess; ++iy)
                        {
                            const float fv = static_cast<float>(iy) / static_cast<float>(tess);
                            for (int ix = 0; ix <= tess; ++ix)
                            {
                                const float fu = static_cast<float>(ix) / static_cast<float>(tess);
                                mesh->buffer.vertices.push_back(
                                    makeVertex(evalPatchVertex(c00, c10, c20,
                                                               c01, c11, c21,
                                                               c02, c12, c22,
                                                               fu, fv)));
                            }
                        }

                        for (int iy = 0; iy < tess; ++iy)
                        {
                            for (int ix = 0; ix < tess; ++ix)
                            {
                                const uint32_t i0 = baseVert + static_cast<uint32_t>(iy * row + ix);
                                const uint32_t i1 = i0 + 1;
                                const uint32_t i2 = i0 + static_cast<uint32_t>(row);
                                const uint32_t i3 = i2 + 1;
                                mesh->buffer.indices.push_back(i0);
                                mesh->buffer.indices.push_back(i2);
                                mesh->buffer.indices.push_back(i1);
                                mesh->buffer.indices.push_back(i1);
                                mesh->buffer.indices.push_back(i2);
                                mesh->buffer.indices.push_back(i3);
                            }
                        }
                    }
                }
            }
        }

        if (mesh->buffer.indices.empty())
            continue;

        Texture *albedo = nullptr;
        if (groups_[gi].textureIndex >= 0 && groups_[gi].textureIndex < static_cast<int>(textures_.size()))
            albedo = textures_[groups_[gi].textureIndex].texture;

        Texture *lm = nullptr;
        if (groups_[gi].lightmapIndex >= 0 && groups_[gi].lightmapIndex < static_cast<int>(lightmaps_.size()))
            lm = lightmaps_[groups_[gi].lightmapIndex].texture;

        if (!albedo)
            albedo = white;
        if (!lm)
            lm = white;

        const bool useLm = (groups_[gi].lightmapIndex >= 0) && (lm != nullptr);

        mat->setShader(shader)
            ->setTexture("u_albedo", albedo)
            ->setTexture("u_lightmap", lm)
            ->setInt("u_useLightmap", useLm ? 1 : 0)
            ->setFloat("u_lightmapMul", lightmapMul_)
            ->setFloat("u_lightmapGamma", lightmapGamma_)
            ->setVec3("u_color", {1.0f, 1.0f, 1.0f})
            ->setCullFace(true)
            ->setBlend(false)
            ->setDepthTest(true)
            ->setDepthWrite(true);

        mesh->add_material(mat);
        mesh->add_surface(0, static_cast<uint32_t>(mesh->buffer.indices.size()), 0);
        mesh->upload();

        groups_[gi].mesh = mesh;
        groups_[gi].material = mat;
        groups_[gi].localAabb = mesh->aabb;
    }

    return !groups_.empty();
}

int Q3BspMap::findLeaf(const glm::vec3 &q3Pos) const
{
    if (nodes_.empty() || leaves_.empty() || planes_.empty())
        return -1;

    int nodeIdx = 0;
    while (nodeIdx >= 0)
    {
        if (nodeIdx >= static_cast<int>(nodes_.size()))
            return -1;

        const Node &n = nodes_[nodeIdx];
        if (n.plane < 0 || n.plane >= static_cast<int>(planes_.size()))
            return -1;

        const Plane &p = planes_[n.plane];
        const float dist = glm::dot(p.normal, q3Pos) - p.dist;
        nodeIdx = (dist >= 0.0f) ? n.front : n.back;
    }

    const int leafIdx = -(nodeIdx + 1);
    if (leafIdx < 0 || leafIdx >= static_cast<int>(leaves_.size()))
        return -1;
    return leafIdx;
}

int Q3BspMap::findLeafFromLocal(const glm::vec3 &localPos) const
{
    return findLeaf(localPos / scale_);
}

std::vector<int> Q3BspMap::collectVisibleGroupIndices(const glm::vec3 &cameraLocalPos) const
{
    std::vector<int> out;

    if (groups_.empty())
        return out;

    const auto appendAll = [&]() {
        for (int gi = 0; gi < static_cast<int>(groups_.size()); ++gi)
        {
            const auto &g = groups_[gi];
            if (g.mesh && g.material)
                out.push_back(gi);
        }
    };

    if (nodes_.empty() || leaves_.empty() || leafFaces_.empty() || faces_.empty())
    {
        appendAll();
        return out;
    }

    std::vector<uint8_t> seenFaces(faces_.size(), 0);
    std::vector<uint8_t> seenGroups(groups_.size(), 0);

    const glm::vec3 qCamPos = cameraLocalPos / scale_;
    std::vector<int> nodeStack;
    nodeStack.reserve(nodes_.size() + leaves_.size());
    nodeStack.push_back(0);

    while (!nodeStack.empty())
    {
        const int nodeIdx = nodeStack.back();
        nodeStack.pop_back();

        if (nodeIdx < 0)
        {
            const int leafIdx = -(nodeIdx + 1);
            if (leafIdx < 0 || leafIdx >= static_cast<int>(leaves_.size()))
                continue;

            const Leaf &lf = leaves_[leafIdx];
            for (int i = 0; i < lf.numLeafFaces; ++i)
            {
                const int leafFaceIdx = lf.firstLeafFace + i;
                if (leafFaceIdx < 0 || leafFaceIdx >= static_cast<int>(leafFaces_.size()))
                    continue;

                const int faceIdx = leafFaces_[leafFaceIdx];
                if (faceIdx < 0 || faceIdx >= static_cast<int>(faces_.size()))
                    continue;

                if (seenFaces[faceIdx])
                    continue;
                seenFaces[faceIdx] = 1;

                const Face &f = faces_[faceIdx];
                const bool supported =
                    ((f.faceType == kFacePolygon || f.faceType == kFaceMesh) && f.numMeshVerts > 0) ||
                    (f.faceType == kFacePatch && f.patchW >= 3 && f.patchH >= 3);
                if (!supported)
                    continue;

                if (faceIdx < 0 || faceIdx >= static_cast<int>(faceToGroup_.size()))
                    continue;

                const int gi = faceToGroup_[faceIdx];
                if (gi < 0 || gi >= static_cast<int>(groups_.size()))
                    continue;
                if (seenGroups[gi])
                    continue;

                const MeshGroup &g = groups_[gi];
                if (!g.mesh || !g.material)
                    continue;

                seenGroups[gi] = 1;
                out.push_back(gi);
            }
            continue;
        }

        if (nodeIdx >= static_cast<int>(nodes_.size()))
            continue;

        const Node &n = nodes_[nodeIdx];
        if (n.plane < 0 || n.plane >= static_cast<int>(planes_.size()))
            continue;

        const Plane &p = planes_[n.plane];
        const float dist = glm::dot(p.normal, qCamPos) - p.dist;

        // Keep the same order used in the .bu reference.
        if (dist >= 0.0f)
        {
            nodeStack.push_back(n.front);
            nodeStack.push_back(n.back);
        }
        else
        {
            nodeStack.push_back(n.back);
            nodeStack.push_back(n.front);
        }
    }

    if (out.empty())
        appendAll();

    return out;
}

std::vector<const Q3BspMap::RawEntity *> Q3BspMap::rawEntitiesByClassname(const std::string &classname) const
{
    std::vector<const RawEntity *> out;
    out.reserve(rawEntities_.size());

    for (const RawEntity &e : rawEntities_)
    {
        const std::string *cn = e.find("classname");
        if (cn && *cn == classname)
            out.push_back(&e);
    }
    return out;
}

std::vector<const Q3BspMap::MapEntity *> Q3BspMap::entitiesByKind(EntityKind kind) const
{
    std::vector<const MapEntity *> out;
    out.reserve(entities_.size());

    for (const MapEntity &entity : entities_)
    {
        if (entity.kind == kind)
            out.push_back(&entity);
    }
    return out;
}

glm::vec3 Q3BspMap::q3ToLocal(const glm::vec3 &q3Pos) const
{
    return glm::vec3(q3Pos.x, q3Pos.z, q3Pos.y) * scale_;
}

bool Q3BspMap::rawEntityOriginLocal(const RawEntity &e, glm::vec3 &outLocal) const
{
    const std::string *origin = e.find("origin");
    if (!origin)
        return false;

    glm::vec3 q3;
    if (!parseVec3(*origin, q3))
        return false;

    outLocal = q3ToLocal(q3);
    return true;
}

bool Q3BspMap::entityOriginLocal(const MapEntity &entity, glm::vec3 &outLocal) const
{
    if (entity.getVec3("originLocal", outLocal))
        return true;
    return entity.raw ? rawEntityOriginLocal(*entity.raw, outLocal) : false;
}

void Q3BspNode::gatherRenderItems(RenderQueue &queue, const FrameContext &ctx)
{
    if (!map || !map->ready())
        return;

    std::vector<int> visible;
    const auto &groups = map->groups();

    if (useBspTraversal && ctx.camera)
    {
        const glm::vec3 localCam = worldToLocalPoint(ctx.camera->position);
        visible = map->collectVisibleGroupIndices(localCam);
    }
    else
    {
        visible.reserve(groups.size());
        for (int i = 0; i < static_cast<int>(groups.size()); ++i)
            visible.push_back(i);
    }

    const glm::mat4 model = worldMatrix();
    const bool useFrustumCull = !ctx.frustum.isInfinite();

    for (int gi : visible)
    {
        if (gi < 0 || gi >= static_cast<int>(groups.size()))
            continue;

        const auto &g = groups[gi];
        if (!g.mesh || !g.material)
            continue;

        const BoundingBox worldAABB =
            g.localAabb.is_valid() ? g.localAabb.transformed(model) : BoundingBox{};

        if (useFrustumCull && worldAABB.is_valid() && !ctx.frustum.contains(worldAABB))
            continue;

        RenderItem item;
        item.drawable = g.mesh;
        item.material = g.material;
        item.model = model;
        item.passMask = passMask;
        item.indexStart = 0;
        item.indexCount = 0;
        item.worldAABB = worldAABB;
        queue.add(item);
    }
}
