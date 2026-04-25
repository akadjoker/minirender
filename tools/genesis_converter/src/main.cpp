#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace
{
constexpr uint32_t MRLV_MAGIC = 0x564c524dU;
constexpr uint32_t MRLV_VERSION = 1;
constexpr uint32_t CHUNK_MATS = 0x4d415453U;
constexpr uint32_t CHUNK_BUFF = 0x42554646U;
constexpr uint32_t CHUNK_VRTS = 0x56525453U;
constexpr uint32_t CHUNK_IDXS = 0x49445853U;
constexpr uint32_t CHUNK_SURF = 0x53555246U;
constexpr uint32_t CHUNK_ENTS = 0x454e5453U;
constexpr uint32_t CHUNK_LMAP = 0x4c4d4150U;
constexpr uint32_t BUFFER_FLAG_TANGENTS = 1 << 1;
constexpr uint32_t BUFFER_FLAG_LIGHTMAP = 1 << 3;
constexpr uint32_t LEVEL_ENTITY_PLAYER_START = 0;
constexpr uint32_t LEVEL_ENTITY_LIGHT = 1;
constexpr uint32_t LIGHT_TYPE_POINT = 0;

constexpr int32_t GBSP_CHUNK_HEADER = 0;
constexpr int32_t GBSP_CHUNK_FACES = 11;
constexpr int32_t GBSP_CHUNK_VERT_INDEX = 13;
constexpr int32_t GBSP_CHUNK_VERTS = 14;
constexpr int32_t GBSP_CHUNK_ENTDATA = 16;
constexpr int32_t GBSP_CHUNK_TEXINFO = 17;
constexpr int32_t GBSP_CHUNK_TEXTURES = 18;
constexpr int32_t GBSP_CHUNK_TEXDATA = 19;
constexpr int32_t GBSP_CHUNK_LIGHTDATA = 20;
constexpr int32_t GBSP_CHUNK_PALETTES = 23;
constexpr int32_t GBSP_CHUNK_END = 0xffff;

struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec4
{
    float x = 1.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct Face
{
    int32_t firstVert = 0;
    int32_t numVerts = 0;
    int32_t planeNum = 0;
    int32_t planeSide = 0;
    int32_t texInfo = 0;
    int32_t lightOfs = -1;
    int32_t lightWidth = 0;
    int32_t lightHeight = 0;
};

struct TexInfo
{
    Vec3 vecs[2];
    float shift[2] = {};
    float drawScale[2] = {1.0f, 1.0f};
    int32_t flags = 0;
    int32_t texture = -1;
};

struct Texture
{
    std::string name;
    uint32_t flags = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t offset = 0;
    int32_t paletteIndex = 0;
};

struct LevelVertex
{
    Vec3 position;
    Vec3 normal;
    Vec4 tangent;
    Vec2 uv;
    Vec2 lightmapUv;
};

struct FaceTexAdjust
{
    float shiftU = 0.0f;
    float shiftV = 0.0f;
};

struct Surface
{
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
    int32_t material = 0;
    int32_t lightmap = -1;
};

struct GenesisBsp
{
    std::vector<Face> faces;
    std::vector<Vec3> verts;
    std::vector<int32_t> vertIndices;
    std::vector<TexInfo> texInfos;
    std::vector<Texture> textures;
    std::vector<uint8_t> texData;
    std::vector<uint8_t> lightData;
    std::vector<uint8_t> palettes;
    std::vector<uint8_t> entData;
};

struct FaceLightmapRect
{
    bool valid = false;
    int32_t page = -1;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    float minU = 0.0f;
    float minV = 0.0f;
};

struct LightmapAtlas
{
    int32_t channels = 3;
    int32_t pageSize = 512;
    std::vector<std::vector<uint8_t>> pages;
    std::vector<FaceLightmapRect> rects;
    uint32_t count = 0;
};

struct GenesisEntity
{
    std::vector<std::pair<std::string, std::string>> pairs;
    std::unordered_map<std::string, std::string> kv;
};

struct MrlvlPlayerStart
{
    std::string name;
    Vec3 position;
    Vec3 direction;
};

struct MrlvlLight
{
    std::string name;
    Vec3 position;
    Vec3 color = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float radius = 512.0f;
};

struct Chunk
{
    int32_t type = 0;
    int32_t size = 0;
    int32_t elements = 0;
};

uint32_t readU32(const std::vector<uint8_t> &bytes, size_t offset)
{
    uint32_t v = 0;
    std::memcpy(&v, bytes.data() + offset, sizeof(v));
    return v;
}

int32_t readI32(const std::vector<uint8_t> &bytes, size_t offset)
{
    return static_cast<int32_t>(readU32(bytes, offset));
}

float readF32(const std::vector<uint8_t> &bytes, size_t offset)
{
    uint32_t raw = readU32(bytes, offset);
    float v = 0.0f;
    std::memcpy(&v, &raw, sizeof(v));
    return v;
}

std::string readFixedString(const std::vector<uint8_t> &bytes, size_t offset, size_t len)
{
    std::string out;
    for (size_t i = 0; i < len; ++i)
    {
        char c = static_cast<char>(bytes[offset + i]);
        if (c == '\0')
            break;
        out.push_back(c);
    }
    return out;
}

std::string toLower(std::string value)
{
    for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string readEntityString(const std::vector<uint8_t> &bytes, size_t &cursor, std::string &error)
{
    if (cursor + 4 > bytes.size())
    {
        error = "entity string size is truncated";
        return {};
    }

    int32_t size = readI32(bytes, cursor);
    cursor += 4;
    if (size < 0 || cursor + static_cast<size_t>(size) > bytes.size())
    {
        error = "entity string extends past end of entdata";
        return {};
    }

    std::string out;
    if (size > 0)
    {
        const char *text = reinterpret_cast<const char *>(bytes.data() + cursor);
        out.assign(text, text + size);
        if (!out.empty() && out.back() == '\0')
            out.pop_back();
    }
    cursor += static_cast<size_t>(size);
    return out;
}

std::vector<GenesisEntity> parseGenesisEntities(const std::vector<uint8_t> &entData, std::string &error)
{
    std::vector<GenesisEntity> entities;
    if (entData.empty())
        return entities;
    if (entData.size() < 4)
    {
        error = "entdata is truncated";
        return {};
    }

    size_t cursor = 0;
    int32_t count = readI32(entData, cursor);
    cursor += 4;
    if (count < 0 || count > 100000)
    {
        error = "entdata has an invalid entity count";
        return {};
    }

    entities.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i)
    {
        if (cursor + 4 > entData.size())
        {
            error = "entity epair count is truncated";
            return {};
        }

        int32_t pairCount = readI32(entData, cursor);
        cursor += 4;
        if (pairCount < 0 || pairCount > 10000)
        {
            error = "entity has an invalid epair count";
            return {};
        }

        GenesisEntity entity;
        for (int32_t p = 0; p < pairCount; ++p)
        {
            std::string key = readEntityString(entData, cursor, error);
            if (!error.empty())
                return {};
            std::string value = readEntityString(entData, cursor, error);
            if (!error.empty())
                return {};
            entity.pairs.emplace_back(key, value);
            entity.kv[toLower(key)] = value;
        }
        entities.push_back(std::move(entity));
    }

    return entities;
}

const std::string *findValue(const GenesisEntity &entity, const std::string &key)
{
    auto it = entity.kv.find(key);
    if (it == entity.kv.end())
        return nullptr;
    return &it->second;
}

std::string valueOr(const GenesisEntity &entity, const std::string &key, const std::string &fallback = {})
{
    const std::string *value = findValue(entity, key);
    return value ? *value : fallback;
}

bool parseVec3(const std::string &text, Vec3 &out)
{
    std::istringstream in(text);
    return static_cast<bool>(in >> out.x >> out.y >> out.z);
}

float parseFloat(const std::string &text, float fallback)
{
    char *end = nullptr;
    float value = std::strtof(text.c_str(), &end);
    return end != text.c_str() ? value : fallback;
}

bool hasOrigin(const GenesisEntity &entity, Vec3 &out)
{
    const std::string *origin = findValue(entity, "origin");
    return origin && parseVec3(*origin, out);
}

Vec3 directionFromAngle(float degrees)
{
    constexpr float kPi = 3.14159265358979323846f;
    float radians = degrees * (kPi / 180.0f);
    return {std::cos(radians), 0.0f, std::sin(radians)};
}

void convertGenesisEntities(const GenesisBsp &bsp,
                            std::vector<MrlvlPlayerStart> &playerStarts,
                            std::vector<MrlvlLight> &lights,
                            size_t &rawEntityCount)
{
    std::string entityError;
    std::vector<GenesisEntity> entities = parseGenesisEntities(bsp.entData, entityError);
    if (!entityError.empty())
    {
        std::cerr << "Warning: could not parse Genesis entities: " << entityError << "\n";
        return;
    }

    rawEntityCount = entities.size();
    uint32_t unnamedLight = 0;
    uint32_t unnamedStart = 0;

    for (const GenesisEntity &entity : entities)
    {
        std::string className = toLower(valueOr(entity, "classname"));
        Vec3 position;
        if (!hasOrigin(entity, position))
            continue;

        if (className == "light")
        {
            MrlvlLight light;
            light.position = position;
            light.name = valueOr(entity, "targetname");
            if (light.name.empty())
                light.name = "genesis_light_" + std::to_string(unnamedLight++);

            const std::string lightValueText = valueOr(entity, "light", valueOr(entity, "_light", "300"));
            const float lightValue = std::max(1.0f, parseFloat(lightValueText, 300.0f));
            light.intensity = std::max(0.25f, lightValue / 300.0f);
            light.radius = std::max(64.0f, lightValue * 2.0f);

            Vec3 color;
            if (parseVec3(valueOr(entity, "color", valueOr(entity, "_color")), color))
            {
                if (color.x > 1.0f || color.y > 1.0f || color.z > 1.0f)
                {
                    color.x /= 255.0f;
                    color.y /= 255.0f;
                    color.z /= 255.0f;
                }
                light.color = color;
            }

            lights.push_back(light);
        }
        else if (className.find("player") != std::string::npos ||
                 className.find("start") != std::string::npos)
        {
            MrlvlPlayerStart start;
            start.position = position;
            start.name = valueOr(entity, "targetname");
            if (start.name.empty())
                start.name = "genesis_start_" + std::to_string(unnamedStart++);
            start.direction = directionFromAngle(parseFloat(valueOr(entity, "angle"), 0.0f));
            playerStarts.push_back(start);
        }
    }
}

void exportGenesisEntityDump(const GenesisBsp &bsp, const std::filesystem::path &outputPath)
{
    std::string entityError;
    std::vector<GenesisEntity> entities = parseGenesisEntities(bsp.entData, entityError);
    if (!entityError.empty())
    {
        std::cerr << "Warning: could not dump Genesis entities: " << entityError << "\n";
        return;
    }

    std::filesystem::path txtPath = outputPath;
    txtPath.replace_extension("");
    txtPath += "_entities.txt";

    std::ofstream out(txtPath);
    if (!out)
    {
        std::cerr << "Warning: could not write entity dump: " << txtPath << "\n";
        return;
    }

    out << "# Genesis3D entity dump\n";
    out << "# Source entities: " << entities.size() << "\n\n";

    for (size_t i = 0; i < entities.size(); ++i)
    {
        const GenesisEntity &entity = entities[i];
        out << "entity " << i << "\n";
        out << "{\n";
        for (const auto &pair : entity.pairs)
            out << "  \"" << pair.first << "\" \"" << pair.second << "\"\n";
        out << "}\n\n";
    }
}

float dot(const Vec3 &a, const Vec3 &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 sub(const Vec3 &a, const Vec3 &b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 cross(const Vec3 &a, const Vec3 &b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float lengthSq(const Vec3 &v)
{
    return dot(v, v);
}

Vec3 normalize(const Vec3 &v)
{
    float len2 = lengthSq(v);
    if (len2 <= 1.0e-12f)
        return {0.0f, 1.0f, 0.0f};
    float inv = 1.0f / std::sqrt(len2);
    return {v.x * inv, v.y * inv, v.z * inv};
}

template <typename T>
void writePod(std::ofstream &out, T value)
{
    out.write(reinterpret_cast<const char *>(&value), sizeof(T));
}

void writeU32(std::ofstream &out, uint32_t v)
{
    writePod(out, v);
}

void writeI32(std::ofstream &out, int32_t v)
{
    writePod(out, v);
}

void writeF32(std::ofstream &out, float v)
{
    writePod(out, v);
}

void writeStr(std::ofstream &out, const std::string &value)
{
    out.write(value.c_str(), static_cast<std::streamsize>(value.size() + 1));
}

std::streampos beginChunk(std::ofstream &out, uint32_t id)
{
    writeU32(out, id);
    writeU32(out, 0);
    return out.tellp();
}

void endChunk(std::ofstream &out, std::streampos start)
{
    std::streampos end = out.tellp();
    out.seekp(start - std::streamoff(4));
    writeU32(out, static_cast<uint32_t>(end - start));
    out.seekp(end);
}

bool readFile(const std::string &path, std::vector<uint8_t> &out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size < 0)
        return false;
    out.resize(static_cast<size_t>(size));
    in.read(reinterpret_cast<char *>(out.data()), size);
    return in.good() || in.eof();
}

bool parseGenesisBsp(const std::string &path, GenesisBsp &bsp, std::string &error)
{
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes))
    {
        error = "could not read input file";
        return false;
    }
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "IBSP", 4) == 0)
    {
        error = "this is a Quake-style IBSP file, not a Genesis3D GBSP file";
        return false;
    }
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "GBSP", 4) == 0)
    {
        error = "this looks like raw GBSP header data, but Genesis BSP files are expected to be chunked";
        return false;
    }

    size_t cursor = 0;
    bool sawHeader = false;
    while (cursor + 12 <= bytes.size())
    {
        Chunk chunk;
        chunk.type = readI32(bytes, cursor + 0);
        chunk.size = readI32(bytes, cursor + 4);
        chunk.elements = readI32(bytes, cursor + 8);
        cursor += 12;

        if (chunk.type == GBSP_CHUNK_END)
            break;
        if (chunk.size < 0 || chunk.elements < 0)
        {
            error = "negative chunk size/count";
            return false;
        }

        const size_t elemSize = static_cast<size_t>(chunk.size);
        const size_t count = static_cast<size_t>(chunk.elements);
        const size_t dataSize = elemSize * count;
        if (cursor + dataSize > bytes.size())
        {
            error = sawHeader ? "chunk extends past end of file" : "not a Genesis3D GBSP chunked file";
            return false;
        }

        switch (chunk.type)
        {
        case GBSP_CHUNK_HEADER:
            if (dataSize >= 5 && std::memcmp(bytes.data() + cursor, "GBSP", 4) == 0)
                sawHeader = true;
            break;
        case GBSP_CHUNK_FACES:
            if (elemSize < 36)
            {
                error = "GFX_Face chunk has unexpected element size";
                return false;
            }
            bsp.faces.reserve(count);
            for (size_t i = 0; i < count; ++i)
            {
                size_t o = cursor + i * elemSize;
                Face f;
                f.firstVert = readI32(bytes, o + 0);
                f.numVerts = readI32(bytes, o + 4);
                f.planeNum = readI32(bytes, o + 8);
                f.planeSide = readI32(bytes, o + 12);
                f.texInfo = readI32(bytes, o + 16);
                f.lightOfs = readI32(bytes, o + 20);
                f.lightWidth = readI32(bytes, o + 24);
                f.lightHeight = readI32(bytes, o + 28);
                bsp.faces.push_back(f);
            }
            break;
        case GBSP_CHUNK_VERT_INDEX:
            if (elemSize != 4)
            {
                error = "vertex index chunk has unexpected element size";
                return false;
            }
            bsp.vertIndices.reserve(count);
            for (size_t i = 0; i < count; ++i)
                bsp.vertIndices.push_back(readI32(bytes, cursor + i * 4));
            break;
        case GBSP_CHUNK_VERTS:
            if (elemSize != 12)
            {
                error = "vertex chunk has unexpected element size";
                return false;
            }
            bsp.verts.reserve(count);
            for (size_t i = 0; i < count; ++i)
            {
                size_t o = cursor + i * 12;
                bsp.verts.push_back({readF32(bytes, o + 0), readF32(bytes, o + 4), readF32(bytes, o + 8)});
            }
            break;
        case GBSP_CHUNK_TEXINFO:
            if (elemSize < 64)
            {
                error = "texinfo chunk has unexpected element size";
                return false;
            }
            bsp.texInfos.reserve(count);
            for (size_t i = 0; i < count; ++i)
            {
                size_t o = cursor + i * elemSize;
                TexInfo t;
                t.vecs[0] = {readF32(bytes, o + 0), readF32(bytes, o + 4), readF32(bytes, o + 8)};
                t.vecs[1] = {readF32(bytes, o + 12), readF32(bytes, o + 16), readF32(bytes, o + 20)};
                t.shift[0] = readF32(bytes, o + 24);
                t.shift[1] = readF32(bytes, o + 28);
                t.drawScale[0] = readF32(bytes, o + 32);
                t.drawScale[1] = readF32(bytes, o + 36);
                t.flags = readI32(bytes, o + 40);
                t.texture = readI32(bytes, o + 60);
                bsp.texInfos.push_back(t);
            }
            break;
        case GBSP_CHUNK_TEXTURES:
            if (elemSize < 52)
            {
                error = "texture chunk has unexpected element size";
                return false;
            }
            bsp.textures.reserve(count);
            for (size_t i = 0; i < count; ++i)
            {
                size_t o = cursor + i * elemSize;
                Texture t;
                t.name = readFixedString(bytes, o, 32);
                t.flags = readU32(bytes, o + 32);
                t.width = readI32(bytes, o + 36);
                t.height = readI32(bytes, o + 40);
                t.offset = readI32(bytes, o + 44);
                t.paletteIndex = readI32(bytes, o + 48);
                bsp.textures.push_back(std::move(t));
            }
            break;
        case GBSP_CHUNK_ENTDATA:
            bsp.entData.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                               bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dataSize));
            break;
        case GBSP_CHUNK_TEXDATA:
            if (elemSize != 1)
            {
                error = "texture data chunk has unexpected element size";
                return false;
            }
            bsp.texData.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                               bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dataSize));
            break;
        case GBSP_CHUNK_LIGHTDATA:
            if (elemSize != 1)
            {
                error = "light data chunk has unexpected element size";
                return false;
            }
            bsp.lightData.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                                 bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dataSize));
            break;
        case GBSP_CHUNK_PALETTES:
            if (elemSize != 256 * 3)
            {
                error = "palette chunk has unexpected element size";
                return false;
            }
            bsp.palettes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                                bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dataSize));
            break;
        default:
            break;
        }

        cursor += dataSize;
    }

    if (!sawHeader)
    {
        error = "missing GBSP header";
        return false;
    }
    if (bsp.faces.empty() || bsp.verts.empty() || bsp.vertIndices.empty())
    {
        error = "input has no renderable face/vertex data";
        return false;
    }
    return true;
}

std::string sanitizeTextureName(const std::string &name, size_t fallbackIndex)
{
    std::string out;
    for (char c : name)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')
        {
            out.push_back(c);
        }
        else
        {
            out.push_back('_');
        }
    }
    if (out.empty())
        out = "texture_" + std::to_string(fallbackIndex);
    return out;
}

std::vector<std::string> exportEmbeddedTextures(const GenesisBsp &bsp,
                                                const std::filesystem::path &outputPath)
{
    std::vector<std::string> refs(bsp.textures.size());
    if (bsp.textures.empty() || bsp.texData.empty() || bsp.palettes.empty())
        return refs;

    const std::string dirName = outputPath.stem().string() + "_textures";
    const std::filesystem::path textureDir = outputPath.parent_path() / dirName;
    std::error_code ec;
    std::filesystem::create_directories(textureDir, ec);
    if (ec)
    {
        std::cerr << "Warning: could not create texture directory: " << textureDir << "\n";
        return refs;
    }

    for (size_t i = 0; i < bsp.textures.size(); ++i)
    {
        const Texture &texture = bsp.textures[i];
        if (texture.width <= 0 || texture.height <= 0 || texture.offset < 0 ||
            texture.paletteIndex < 0)
        {
            continue;
        }

        const size_t pixelCount = static_cast<size_t>(texture.width) * static_cast<size_t>(texture.height);
        const size_t texOffset = static_cast<size_t>(texture.offset);
        const size_t palOffset = static_cast<size_t>(texture.paletteIndex) * 256u * 3u;
        if (texOffset + pixelCount > bsp.texData.size() || palOffset + 256u * 3u > bsp.palettes.size())
            continue;

        std::vector<uint8_t> rgb(pixelCount * 3u);
        for (size_t p = 0; p < pixelCount; ++p)
        {
            const uint8_t index = bsp.texData[texOffset + p];
            const size_t po = palOffset + static_cast<size_t>(index) * 3u;
            rgb[p * 3u + 0u] = bsp.palettes[po + 0u];
            rgb[p * 3u + 1u] = bsp.palettes[po + 1u];
            rgb[p * 3u + 2u] = bsp.palettes[po + 2u];
        }

        const std::string filename = sanitizeTextureName(texture.name, i) + ".png";
        const std::filesystem::path pngPath = textureDir / filename;
        if (stbi_write_png(pngPath.string().c_str(), texture.width, texture.height, 3,
                           rgb.data(), texture.width * 3) != 0)
        {
            refs[i] = (std::filesystem::path(dirName) / filename).generic_string();
        }
    }

    return refs;
}

LightmapAtlas buildLightmapAtlas(const GenesisBsp &bsp)
{
    LightmapAtlas atlas;
    atlas.rects.resize(bsp.faces.size());
    if (bsp.lightData.empty())
        return atlas;

    constexpr int32_t kPageSize = 512;
    constexpr int32_t kPadding = 1;
    atlas.pageSize = kPageSize;
    atlas.pages.emplace_back(static_cast<size_t>(kPageSize) * static_cast<size_t>(kPageSize) * 3u, 0);
    int32_t page = 0;
    int32_t cursorX = kPadding;
    int32_t cursorY = kPadding;
    int32_t shelfHeight = 0;

    for (size_t faceIndex = 0; faceIndex < bsp.faces.size(); ++faceIndex)
    {
        const Face &face = bsp.faces[faceIndex];
        if (face.lightOfs < 0 || face.lightWidth <= 0 || face.lightHeight <= 0)
            continue;
        if (face.numVerts <= 0 || face.firstVert < 0 ||
            static_cast<size_t>(face.firstVert + face.numVerts) > bsp.vertIndices.size())
            continue;
        if (face.texInfo < 0 || face.texInfo >= static_cast<int32_t>(bsp.texInfos.size()))
            continue;

        const size_t lightOffset = static_cast<size_t>(face.lightOfs) + 1u;
        const size_t dataSize = static_cast<size_t>(face.lightWidth) *
                                static_cast<size_t>(face.lightHeight) * 3u;
        if (lightOffset + dataSize > bsp.lightData.size())
            continue;

        if (face.lightWidth + kPadding * 2 > kPageSize ||
            face.lightHeight + kPadding * 2 > kPageSize)
            continue;

        if (cursorX + face.lightWidth + kPadding > kPageSize)
        {
            cursorX = kPadding;
            cursorY += shelfHeight + kPadding;
            shelfHeight = 0;
        }
        if (cursorY + face.lightHeight + kPadding > kPageSize)
        {
            atlas.pages.emplace_back(static_cast<size_t>(kPageSize) * static_cast<size_t>(kPageSize) * 3u, 0);
            page = static_cast<int32_t>(atlas.pages.size()) - 1;
            cursorX = kPadding;
            cursorY = kPadding;
            shelfHeight = 0;
        }

        FaceLightmapRect &rect = atlas.rects[faceIndex];
        rect.valid = true;
        rect.page = page;
        rect.x = cursorX;
        rect.y = cursorY;
        rect.width = face.lightWidth;
        rect.height = face.lightHeight;

        const TexInfo &tex = bsp.texInfos[static_cast<size_t>(face.texInfo)];
        float minU = 999999.0f;
        float minV = 999999.0f;
        for (int32_t i = 0; i < face.numVerts; ++i)
        {
            int32_t srcIndex = bsp.vertIndices[static_cast<size_t>(face.firstVert + i)];
            if (srcIndex < 0 || srcIndex >= static_cast<int32_t>(bsp.verts.size()))
                continue;
            const Vec3 &position = bsp.verts[static_cast<size_t>(srcIndex)];
            minU = std::min(minU, dot(position, tex.vecs[0]));
            minV = std::min(minV, dot(position, tex.vecs[1]));
        }
        rect.minU = std::floor(minU / 16.0f) * 16.0f;
        rect.minV = std::floor(minV / 16.0f) * 16.0f;

        cursorX += face.lightWidth + kPadding;
        shelfHeight = std::max(shelfHeight, face.lightHeight);
        atlas.count++;

        const size_t srcBase = lightOffset;
        std::vector<uint8_t> &pagePixels = atlas.pages[static_cast<size_t>(rect.page)];
        for (int32_t y = 0; y < rect.height; ++y)
        {
            const size_t src = srcBase + static_cast<size_t>(y) *
                                           static_cast<size_t>(rect.width) * 3u;
            const size_t dst = (static_cast<size_t>(rect.y + y) *
                                    static_cast<size_t>(atlas.pageSize) +
                                static_cast<size_t>(rect.x)) *
                               3u;
            std::memcpy(pagePixels.data() + dst, bsp.lightData.data() + src,
                        static_cast<size_t>(rect.width) * 3u);
        }
    }

    if (atlas.count == 0)
        atlas.pages.clear();

    return atlas;
}

void exportLightmapAtlasPreview(const LightmapAtlas &atlas, const std::filesystem::path &outputPath)
{
    if (atlas.pages.empty())
        return;

    std::filesystem::path basePath = outputPath;
    basePath.replace_extension("");
    for (size_t i = 0; i < atlas.pages.size(); ++i)
    {
        std::filesystem::path pngPath = basePath;
        pngPath += "_lightmap_" + std::to_string(i) + ".png";
        if (stbi_write_png(pngPath.string().c_str(), atlas.pageSize, atlas.pageSize, atlas.channels,
                           atlas.pages[i].data(), atlas.pageSize * atlas.channels) == 0)
        {
            std::cerr << "Warning: could not write lightmap preview: " << pngPath << "\n";
        }
    }
}

std::string materialNameForFace(const GenesisBsp &bsp, const Face &face)
{
    if (face.texInfo >= 0 && face.texInfo < static_cast<int32_t>(bsp.texInfos.size()))
    {
        int32_t texture = bsp.texInfos[static_cast<size_t>(face.texInfo)].texture;
        if (texture >= 0 && texture < static_cast<int32_t>(bsp.textures.size()))
        {
            const std::string &name = bsp.textures[static_cast<size_t>(texture)].name;
            if (!name.empty())
                return name;
        }
    }
    return "__genesis_default";
}

bool convertToMrlvl(const GenesisBsp &bsp, const std::string &outputPath, bool includeLightmaps, std::string &error)
{
    const std::vector<std::string> textureRefs = exportEmbeddedTextures(bsp, outputPath);
    std::vector<MrlvlPlayerStart> playerStarts;
    std::vector<MrlvlLight> lights;
    size_t rawEntityCount = 0;
    convertGenesisEntities(bsp, playerStarts, lights, rawEntityCount);
    exportGenesisEntityDump(bsp, outputPath);
    const LightmapAtlas lightmapAtlas = includeLightmaps ? buildLightmapAtlas(bsp) : LightmapAtlas{};
    if (includeLightmaps)
        exportLightmapAtlasPreview(lightmapAtlas, outputPath);
    std::vector<FaceTexAdjust> faceTexAdjusts(bsp.faces.size());

    for (size_t faceIndex = 0; faceIndex < bsp.faces.size(); ++faceIndex)
    {
        const Face &face = bsp.faces[faceIndex];
        const TexInfo *tex = nullptr;
        const Texture *texture = nullptr;
        if (face.texInfo >= 0 && face.texInfo < static_cast<int32_t>(bsp.texInfos.size()))
        {
            tex = &bsp.texInfos[static_cast<size_t>(face.texInfo)];
            if (tex->texture >= 0 && tex->texture < static_cast<int32_t>(bsp.textures.size()))
                texture = &bsp.textures[static_cast<size_t>(tex->texture)];
        }

        if (!tex || !texture || texture->width <= 0 || texture->height <= 0 ||
            face.numVerts <= 0 || face.firstVert < 0 ||
            static_cast<size_t>(face.firstVert + face.numVerts) > bsp.vertIndices.size())
        {
            continue;
        }

        float minU = 99999.0f;
        float minV = 99999.0f;
        for (int32_t i = 0; i < face.numVerts; ++i)
        {
            int32_t srcIndex = bsp.vertIndices[static_cast<size_t>(face.firstVert + i)];
            if (srcIndex < 0 || srcIndex >= static_cast<int32_t>(bsp.verts.size()))
                continue;
            const Vec3 &position = bsp.verts[static_cast<size_t>(srcIndex)];
            minU = std::min(minU, dot(position, tex->vecs[0]));
            minV = std::min(minV, dot(position, tex->vecs[1]));
        }

        const float scaleU = tex->drawScale[0] != 0.0f ? (1.0f / tex->drawScale[0]) : 1.0f;
        const float scaleV = tex->drawScale[1] != 0.0f ? (1.0f / tex->drawScale[1]) : 1.0f;
        const float au = static_cast<float>(static_cast<int32_t>((minU * scaleU + tex->shift[0]) / texture->width)) *
                         static_cast<float>(texture->width);
        const float av = static_cast<float>(static_cast<int32_t>((minV * scaleV + tex->shift[1]) / texture->height)) *
                         static_cast<float>(texture->height);

        faceTexAdjusts[faceIndex].shiftU = tex->shift[0] - au;
        faceTexAdjusts[faceIndex].shiftV = tex->shift[1] - av;
    }

    std::vector<LevelVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Surface> surfaces;
    std::vector<std::string> materials;
    std::unordered_map<std::string, int32_t> materialMap;

    auto getMaterial = [&](const std::string &name) -> int32_t {
        auto it = materialMap.find(name);
        if (it != materialMap.end())
            return it->second;
        int32_t idx = static_cast<int32_t>(materials.size());
        materialMap[name] = idx;
        materials.push_back(name);
        return idx;
    };

    auto appendFace = [&](size_t faceIndex) -> bool
    {
        const Face &face = bsp.faces[faceIndex];
        if (face.numVerts < 3 || face.firstVert < 0)
            return false;
        if (static_cast<size_t>(face.firstVert + face.numVerts) > bsp.vertIndices.size())
            return false;

        const TexInfo *tex = nullptr;
        const Texture *texture = nullptr;
        if (face.texInfo >= 0 && face.texInfo < static_cast<int32_t>(bsp.texInfos.size()))
        {
            tex = &bsp.texInfos[static_cast<size_t>(face.texInfo)];
            if (tex->texture >= 0 && tex->texture < static_cast<int32_t>(bsp.textures.size()))
                texture = &bsp.textures[static_cast<size_t>(tex->texture)];
        }

        std::vector<uint32_t> faceVertexIds;
        faceVertexIds.reserve(static_cast<size_t>(face.numVerts));
        for (int32_t i = 0; i < face.numVerts; ++i)
        {
            int32_t srcIndex = bsp.vertIndices[static_cast<size_t>(face.firstVert + i)];
            if (srcIndex < 0 || srcIndex >= static_cast<int32_t>(bsp.verts.size()))
                continue;

            LevelVertex v;
            v.position = bsp.verts[static_cast<size_t>(srcIndex)];
            if (tex && texture && texture->width > 0 && texture->height > 0)
            {
                const float scaleU = tex->drawScale[0] != 0.0f ? tex->drawScale[0] : 1.0f;
                const float scaleV = tex->drawScale[1] != 0.0f ? tex->drawScale[1] : 1.0f;
                const FaceTexAdjust &adjust = faceTexAdjusts[faceIndex];
                v.uv.x = (dot(v.position, tex->vecs[0]) * scaleU + adjust.shiftU) /
                         static_cast<float>(texture->width);
                v.uv.y = (dot(v.position, tex->vecs[1]) * scaleV + adjust.shiftV) /
                         static_cast<float>(texture->height);
            }
            if (tex && faceIndex < lightmapAtlas.rects.size())
            {
                const FaceLightmapRect &rect = lightmapAtlas.rects[faceIndex];
                    if (rect.valid && rect.page >= 0 && !lightmapAtlas.pages.empty())
                    {
                        const float localU = (dot(v.position, tex->vecs[0]) - rect.minU) / 16.0f;
                        const float localV = (dot(v.position, tex->vecs[1]) - rect.minV) / 16.0f;
                        v.lightmapUv.x = (static_cast<float>(rect.x) + localU + 0.5f) /
                                         static_cast<float>(lightmapAtlas.pageSize);
                        v.lightmapUv.y = (static_cast<float>(rect.y) + localV + 0.5f) /
                                         static_cast<float>(lightmapAtlas.pageSize);
                    }
                }
            faceVertexIds.push_back(static_cast<uint32_t>(vertices.size()));
            vertices.push_back(v);
        }

        if (faceVertexIds.size() < 3)
            return false;

        Vec3 normal = normalize(cross(sub(vertices[faceVertexIds[1]].position, vertices[faceVertexIds[0]].position),
                                      sub(vertices[faceVertexIds[2]].position, vertices[faceVertexIds[0]].position)));
        if (face.planeSide)
            normal = {-normal.x, -normal.y, -normal.z};

        for (uint32_t id : faceVertexIds)
            vertices[id].normal = normal;

        for (size_t i = 1; i + 1 < faceVertexIds.size(); ++i)
        {
            indices.push_back(faceVertexIds[0]);
            indices.push_back(faceVertexIds[i]);
            indices.push_back(faceVertexIds[i + 1]);
        }

        return true;
    };

    struct FaceGroup
    {
        int32_t material = -1;
        int32_t lightmap = -1;
        std::vector<size_t> faces;
    };

    std::vector<FaceGroup> faceGroups;

    for (size_t faceIndex = 0; faceIndex < bsp.faces.size(); ++faceIndex)
    {
        int32_t material = getMaterial(materialNameForFace(bsp, bsp.faces[faceIndex]));
        int32_t lightmap = -1;
        if (faceIndex < lightmapAtlas.rects.size() && lightmapAtlas.rects[faceIndex].valid)
            lightmap = lightmapAtlas.rects[faceIndex].page;

        auto groupIt = std::find_if(faceGroups.begin(), faceGroups.end(),
                                    [&](const FaceGroup &group) {
                                        return group.material == material &&
                                               group.lightmap == lightmap;
                                    });
        if (groupIt == faceGroups.end())
        {
            FaceGroup group;
            group.material = material;
            group.lightmap = lightmap;
            faceGroups.push_back(std::move(group));
            groupIt = faceGroups.end() - 1;
        }

        groupIt->faces.push_back(faceIndex);
    }

    for (const FaceGroup &group : faceGroups)
    {
        Surface surface;
        surface.indexStart = static_cast<uint32_t>(indices.size());
        surface.material = group.material;
        surface.lightmap = group.lightmap;

        for (size_t faceIndex : group.faces)
            appendFace(faceIndex);

        surface.indexCount = static_cast<uint32_t>(indices.size()) - surface.indexStart;
        if (surface.indexCount > 0)
            surfaces.push_back(surface);
    }

    if (vertices.empty() || indices.empty())
    {
        error = "conversion produced no triangles";
        return false;
    }

    std::ofstream out(outputPath, std::ios::binary);
    if (!out)
    {
        error = "could not open output file";
        return false;
    }

    writeU32(out, MRLV_MAGIC);
    writeU32(out, MRLV_VERSION);

    {
        auto start = beginChunk(out, CHUNK_MATS);
        writeU32(out, static_cast<uint32_t>(materials.size()));
        for (const std::string &name : materials)
        {
            writeStr(out, name);
            writeF32(out, 0.8f);
            writeF32(out, 0.8f);
            writeF32(out, 0.8f);
            std::string textureRef;
            auto textureIt = std::find_if(bsp.textures.begin(), bsp.textures.end(),
                                          [&](const Texture &texture) { return texture.name == name; });
            if (textureIt != bsp.textures.end())
            {
                size_t textureIndex = static_cast<size_t>(std::distance(bsp.textures.begin(), textureIt));
                if (textureIndex < textureRefs.size())
                    textureRef = textureRefs[textureIndex];
            }
            writeStr(out, textureRef);
        }
        endChunk(out, start);
    }

    {
        auto buffStart = beginChunk(out, CHUNK_BUFF);
        uint32_t flags = BUFFER_FLAG_TANGENTS;
        if (!lightmapAtlas.pages.empty())
            flags |= BUFFER_FLAG_LIGHTMAP;
        writeU32(out, flags);

        {
            auto start = beginChunk(out, CHUNK_VRTS);
            writeU32(out, static_cast<uint32_t>(vertices.size()));
            for (const LevelVertex &v : vertices)
            {
                writeF32(out, v.position.x);
                writeF32(out, v.position.y);
                writeF32(out, v.position.z);
                writeF32(out, v.normal.x);
                writeF32(out, v.normal.y);
                writeF32(out, v.normal.z);
                writeF32(out, v.tangent.x);
                writeF32(out, v.tangent.y);
                writeF32(out, v.tangent.z);
                writeF32(out, v.tangent.w);
                writeF32(out, v.uv.x);
                writeF32(out, v.uv.y);
                if (!lightmapAtlas.pages.empty())
                {
                    writeF32(out, v.lightmapUv.x);
                    writeF32(out, v.lightmapUv.y);
                }
            }
            endChunk(out, start);
        }

        {
            auto start = beginChunk(out, CHUNK_IDXS);
            writeU32(out, static_cast<uint32_t>(indices.size()));
            for (uint32_t index : indices)
                writeU32(out, index);
            endChunk(out, start);
        }

        {
            auto start = beginChunk(out, CHUNK_SURF);
            writeU32(out, static_cast<uint32_t>(surfaces.size()));
            for (const Surface &surface : surfaces)
            {
                writeU32(out, surface.indexStart);
                writeU32(out, surface.indexCount);
                writeI32(out, surface.material);
                writeI32(out, surface.lightmap);
            }
            endChunk(out, start);
        }

        endChunk(out, buffStart);
    }

    for (const std::vector<uint8_t> &page : lightmapAtlas.pages)
    {
        auto start = beginChunk(out, CHUNK_LMAP);
        writeI32(out, lightmapAtlas.pageSize);
        writeI32(out, lightmapAtlas.pageSize);
        writeI32(out, lightmapAtlas.channels);
        out.write(reinterpret_cast<const char *>(page.data()),
                  static_cast<std::streamsize>(page.size()));
        endChunk(out, start);
    }

    {
        auto start = beginChunk(out, CHUNK_ENTS);
        writeU32(out, static_cast<uint32_t>(playerStarts.size() + lights.size()));
        for (const MrlvlPlayerStart &entity : playerStarts)
        {
            writeStr(out, entity.name);
            writeU32(out, LEVEL_ENTITY_PLAYER_START);
            writeF32(out, entity.position.x);
            writeF32(out, entity.position.y);
            writeF32(out, entity.position.z);
            writeF32(out, entity.direction.x);
            writeF32(out, entity.direction.y);
            writeF32(out, entity.direction.z);
        }
        for (const MrlvlLight &entity : lights)
        {
            writeStr(out, entity.name);
            writeU32(out, LEVEL_ENTITY_LIGHT);
            writeF32(out, entity.position.x);
            writeF32(out, entity.position.y);
            writeF32(out, entity.position.z);
            writeU32(out, LIGHT_TYPE_POINT);
            writeF32(out, entity.color.x);
            writeF32(out, entity.color.y);
            writeF32(out, entity.color.z);
            writeF32(out, entity.intensity);
            writeF32(out, entity.radius);
            writeF32(out, 0.0f);
            writeF32(out, -1.0f);
            writeF32(out, 0.0f);
            writeF32(out, 45.0f);
            writeF32(out, 0.1f);
        }
        endChunk(out, start);
    }

    std::cout << "Converted " << vertices.size() << " verts, " << (indices.size() / 3)
              << " tris, " << materials.size() << " materials, " << surfaces.size()
              << " surfaces, " << rawEntityCount << " raw entities, "
              << playerStarts.size() << " player starts, " << lights.size() << " lights, "
              << lightmapAtlas.count << " lightmaps";
    if (!lightmapAtlas.pages.empty())
        std::cout << " (" << lightmapAtlas.pages.size() << " pages " << lightmapAtlas.pageSize << "x" << lightmapAtlas.pageSize << ")";
    std::cout << "\n";
    return true;
}

void printUsage(const char *argv0)
{
    std::cout << "Usage:\n"
              << "  " << argv0 << " <input.gbsp|input.bsp> <output.mrlvl> [--with-lightmaps]\n\n"
              << "Converts compiled Genesis3D GBSP geometry/entities to MiniRender .mrlvl.\n"
              << "Entity key/value dumps are written next to the output as *_entities.txt.\n";
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 3 || argc > 4)
    {
        printUsage(argv[0]);
        return 1;
    }

    bool includeLightmaps = false;
    if (argc == 4)
    {
        if (std::string(argv[3]) != "--with-lightmaps")
        {
            printUsage(argv[0]);
            return 1;
        }
        includeLightmaps = true;
    }

    GenesisBsp bsp;
    std::string error;
    if (!parseGenesisBsp(argv[1], bsp, error))
    {
        std::cerr << "Failed to read Genesis BSP: " << error << "\n";
        return 1;
    }

    if (!convertToMrlvl(bsp, argv[2], includeLightmaps, error))
    {
        std::cerr << "Failed to write MRLVL: " << error << "\n";
        return 1;
    }

    return 0;
}
