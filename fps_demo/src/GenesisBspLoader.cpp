#include "GenesisBspLoader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <tuple>
#include <unordered_map>

#include "Manager.hpp"
#include "Pixmap.hpp"
#include "genesis/GenesisUtils.hpp"

namespace
{
struct GenesisFace
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

struct GenesisTexInfo
{
    glm::vec3 vecs[2] = {};
    float shift[2] = {};
    float drawScale[2] = {1.0f, 1.0f};
    int32_t texture = -1;
};

struct GenesisTexture
{
    std::string name;
    int32_t width = 0;
    int32_t height = 0;
    int32_t offset = 0;
    int32_t paletteIndex = 0;
};

struct GenesisBspModel
{
    int32_t rootNode = 0;
    int32_t rootBNode = 0;
    int32_t firstFace = 0;
    int32_t numFaces = 0;
};

struct GenesisBspRuntime
{
    std::vector<GenesisFace> faces;
    std::vector<glm::vec3> verts;
    std::vector<int32_t> vertIndices;
    std::vector<GenesisTexInfo> texInfos;
    std::vector<GenesisTexture> textures;
    std::vector<uint8_t> texData;
    std::vector<uint8_t> lightData;
    std::vector<uint8_t> palettes;
    std::vector<uint8_t> entData;
    std::vector<GenesisBspModel> models;
    std::vector<GenesisBspBNode> bnodes;
    std::vector<GenesisBspBNode> nodes;
    std::vector<int32_t> leafContents;
    std::vector<int32_t> leafFirstSides;
    std::vector<int32_t> leafNumSides;
    std::vector<GenesisBspLeafSide> leafSides;
    std::vector<GenesisBspPlane> planes;
    int32_t rootNode = 0;
    int32_t rootBNode = 0;
};

struct GenesisEntityRuntime
{
    std::unordered_map<std::string, std::string> kv;
};

constexpr int32_t GBSP_CHUNK_HEADER = 0;
constexpr int32_t GBSP_CHUNK_MODELS = 1;
constexpr int32_t GBSP_CHUNK_NODES = 2;
constexpr int32_t GBSP_CHUNK_BNODES = 3;
constexpr int32_t GBSP_CHUNK_LEAFS = 4;
constexpr int32_t GBSP_CHUNK_LEAF_SIDES = 8;
constexpr int32_t GBSP_CHUNK_PLANES = 10;
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

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
    {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int32_t readI32Raw(const std::vector<uint8_t> &bytes, size_t offset)
{
    uint32_t v = 0;
    std::memcpy(&v, bytes.data() + offset, sizeof(v));
    return static_cast<int32_t>(v);
}

float readF32Raw(const std::vector<uint8_t> &bytes, size_t offset)
{
    uint32_t raw = 0;
    std::memcpy(&raw, bytes.data() + offset, sizeof(raw));
    float v = 0.0f;
    std::memcpy(&v, &raw, sizeof(v));
    return v;
}

std::string readFixedStringRaw(const std::vector<uint8_t> &bytes, size_t offset, size_t len)
{
    std::string out;
    for (size_t i = 0; i < len; ++i)
    {
        const char c = static_cast<char>(bytes[offset + i]);
        if (c == '\0')
            break;
        out.push_back(c);
    }
    return out;
}

bool parseGenesisBspRuntime(const std::string &path, GenesisBspRuntime &outBsp, std::string &error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "nao consegui ler o ficheiro BSP.";
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0)
    {
        error = "ficheiro BSP vazio.";
        return false;
    }
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char *>(bytes.data()), size))
    {
        error = "erro a ler bytes do BSP.";
        return false;
    }

    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "IBSP", 4) == 0)
    {
        error = "este BSP e estilo Quake (IBSP), nao Genesis GBSP.";
        return false;
    }

    size_t cursor = 0;
    bool sawHeader = false;
    while (cursor + 12 <= bytes.size())
    {
        const int32_t type = readI32Raw(bytes, cursor + 0);
        const int32_t elemSize = readI32Raw(bytes, cursor + 4);
        const int32_t count = readI32Raw(bytes, cursor + 8);
        cursor += 12;

        if (type == GBSP_CHUNK_END)
            break;
        if (elemSize < 0 || count < 0)
        {
            error = "chunk GBSP invalido (size/count negativos).";
            return false;
        }

        const size_t dataSize = static_cast<size_t>(elemSize) * static_cast<size_t>(count);
        if (cursor + dataSize > bytes.size())
        {
            error = "chunk GBSP truncado.";
            return false;
        }

        switch (type)
        {
        case GBSP_CHUNK_HEADER:
            if (dataSize >= 5 && std::memcmp(bytes.data() + cursor, "GBSP", 4) == 0)
                sawHeader = true;
            break;
        case GBSP_CHUNK_MODELS:
            outBsp.models.clear();
            outBsp.models.reserve(static_cast<size_t>(count));
            if (elemSize >= 8 && count > 0)
            {
                for (int32_t i = 0; i < count; ++i)
                {
                    const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                    GenesisBspModel model;
                    model.rootNode = readI32Raw(bytes, o + 0);
                    model.rootBNode = readI32Raw(bytes, o + 4);
                    if (elemSize >= 16)
                    {
                        model.firstFace = readI32Raw(bytes, o + 8);
                        model.numFaces = readI32Raw(bytes, o + 12);
                    }
                    outBsp.models.push_back(model);
                }
                outBsp.rootNode = outBsp.models[0].rootNode;
                outBsp.rootBNode = outBsp.models[0].rootBNode;
            }
            break;
        case GBSP_CHUNK_NODES:
            if (elemSize < 44)
            {
                error = "chunk nodes invalido.";
                return false;
            }
            outBsp.nodes.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                GenesisBspBNode n;
                n.children[0] = readI32Raw(bytes, o + 0);
                n.children[1] = readI32Raw(bytes, o + 4);
                n.planeNum = readI32Raw(bytes, o + 16);
                outBsp.nodes.push_back(n);
            }
            break;
        case GBSP_CHUNK_LEAFS:
            if (elemSize < 60)
            {
                error = "chunk leafs invalido.";
                return false;
            }
            outBsp.leafContents.reserve(static_cast<size_t>(count));
            outBsp.leafFirstSides.reserve(static_cast<size_t>(count));
            outBsp.leafNumSides.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                outBsp.leafContents.push_back(readI32Raw(bytes, o + 0));
                outBsp.leafFirstSides.push_back(readI32Raw(bytes, o + 52));
                outBsp.leafNumSides.push_back(readI32Raw(bytes, o + 56));
            }
            break;
        case GBSP_CHUNK_LEAF_SIDES:
            if (elemSize < 8)
            {
                error = "chunk leaf_sides invalido.";
                return false;
            }
            outBsp.leafSides.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                GenesisBspLeafSide s;
                s.planeNum = readI32Raw(bytes, o + 0);
                s.planeSide = readI32Raw(bytes, o + 4);
                outBsp.leafSides.push_back(s);
            }
            break;
        case GBSP_CHUNK_BNODES:
            if (elemSize < 12)
            {
                error = "chunk bnodes invalido.";
                return false;
            }
            outBsp.bnodes.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                GenesisBspBNode n;
                n.children[0] = readI32Raw(bytes, o + 0);
                n.children[1] = readI32Raw(bytes, o + 4);
                n.planeNum = readI32Raw(bytes, o + 8);
                outBsp.bnodes.push_back(n);
            }
            break;
        case GBSP_CHUNK_PLANES:
            if (elemSize < 16)
            {
                error = "chunk planes invalido.";
                return false;
            }
            outBsp.planes.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                GenesisBspPlane p;
                p.normal = glm::vec3(readF32Raw(bytes, o + 0), readF32Raw(bytes, o + 4), readF32Raw(bytes, o + 8));
                p.dist = readF32Raw(bytes, o + 12);
                outBsp.planes.push_back(p);
            }
            break;
        case GBSP_CHUNK_FACES:
            if (elemSize < 36)
            {
                error = "chunk de faces invalido.";
                return false;
            }
            outBsp.faces.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                GenesisFace face;
                face.firstVert = readI32Raw(bytes, o + 0);
                face.numVerts = readI32Raw(bytes, o + 4);
                face.planeNum = readI32Raw(bytes, o + 8);
                face.planeSide = readI32Raw(bytes, o + 12);
                face.texInfo = readI32Raw(bytes, o + 16);
                face.lightOfs = readI32Raw(bytes, o + 20);
                face.lightWidth = readI32Raw(bytes, o + 24);
                face.lightHeight = readI32Raw(bytes, o + 28);
                outBsp.faces.push_back(face);
            }
            break;
        case GBSP_CHUNK_VERT_INDEX:
            if (elemSize != 4)
            {
                error = "chunk de indices invalido.";
                return false;
            }
            outBsp.vertIndices.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
                outBsp.vertIndices.push_back(readI32Raw(bytes, cursor + static_cast<size_t>(i) * 4u));
            break;
        case GBSP_CHUNK_VERTS:
            if (elemSize != 12)
            {
                error = "chunk de vertices invalido.";
                return false;
            }
            outBsp.verts.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * 12u;
                outBsp.verts.push_back(glm::vec3(readF32Raw(bytes, o + 0),
                                                 readF32Raw(bytes, o + 4),
                                                 readF32Raw(bytes, o + 8)));
            }
            break;
        case GBSP_CHUNK_TEXINFO:
            if (elemSize < 64)
            {
                error = "chunk texinfo invalido.";
                return false;
            }
            outBsp.texInfos.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                GenesisTexInfo info;
                info.vecs[0] = glm::vec3(readF32Raw(bytes, o + 0), readF32Raw(bytes, o + 4), readF32Raw(bytes, o + 8));
                info.vecs[1] = glm::vec3(readF32Raw(bytes, o + 12), readF32Raw(bytes, o + 16), readF32Raw(bytes, o + 20));
                info.shift[0] = readF32Raw(bytes, o + 24);
                info.shift[1] = readF32Raw(bytes, o + 28);
                info.drawScale[0] = readF32Raw(bytes, o + 32);
                info.drawScale[1] = readF32Raw(bytes, o + 36);
                info.texture = readI32Raw(bytes, o + 60);
                outBsp.texInfos.push_back(info);
            }
            break;
        case GBSP_CHUNK_TEXTURES:
            if (elemSize < 52)
            {
                error = "chunk textures invalido.";
                return false;
            }
            outBsp.textures.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i)
            {
                const size_t o = cursor + static_cast<size_t>(i) * static_cast<size_t>(elemSize);
                GenesisTexture tex;
                tex.name = readFixedStringRaw(bytes, o, 32);
                tex.width = readI32Raw(bytes, o + 36);
                tex.height = readI32Raw(bytes, o + 40);
                tex.offset = readI32Raw(bytes, o + 44);
                tex.paletteIndex = readI32Raw(bytes, o + 48);
                outBsp.textures.push_back(std::move(tex));
            }
            break;
        case GBSP_CHUNK_TEXDATA:
            outBsp.texData.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                                  bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dataSize));
            break;
        case GBSP_CHUNK_LIGHTDATA:
            outBsp.lightData.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                                    bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dataSize));
            break;
        case GBSP_CHUNK_PALETTES:
            outBsp.palettes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                                   bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dataSize));
            break;
        case GBSP_CHUNK_ENTDATA:
            outBsp.entData.assign(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                                  bytes.begin() + static_cast<std::ptrdiff_t>(cursor + dataSize));
            break;
        default:
            break;
        }

        cursor += dataSize;
    }

    if (!sawHeader)
    {
        error = "cabecalho GBSP nao encontrado.";
        return false;
    }
    if (outBsp.faces.empty() || outBsp.verts.empty() || outBsp.vertIndices.empty())
    {
        error = "bsp sem geometria renderizavel.";
        return false;
    }

    return true;
}

std::string readGenesisEntityString(const std::vector<uint8_t> &bytes, size_t &cursor, std::string &error)
{
    if (cursor + 4 > bytes.size())
    {
        error = "entidade truncada";
        return {};
    }
    const int32_t size = readI32Raw(bytes, cursor);
    cursor += 4;
    if (size < 0 || cursor + static_cast<size_t>(size) > bytes.size())
    {
        error = "string de entidade invalida";
        return {};
    }
    std::string out;
    if (size > 0)
    {
        out.assign(reinterpret_cast<const char *>(bytes.data() + cursor),
                   reinterpret_cast<const char *>(bytes.data() + cursor + static_cast<size_t>(size)));
        if (!out.empty() && out.back() == '\0')
            out.pop_back();
    }
    cursor += static_cast<size_t>(size);
    return out;
}

std::vector<GenesisEntityRuntime> parseGenesisEntitiesRuntime(const std::vector<uint8_t> &entData, std::string &error)
{
    std::vector<GenesisEntityRuntime> entities;
    if (entData.empty())
        return entities;

    size_t cursor = 0;
    if (cursor + 4 > entData.size())
        return entities;
    const int32_t count = readI32Raw(entData, cursor);
    cursor += 4;
    if (count < 0 || count > 100000)
    {
        error = "contador de entidades invalido";
        return {};
    }

    entities.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; ++i)
    {
        if (cursor + 4 > entData.size())
        {
            error = "entidade truncada";
            return {};
        }
        const int32_t pairs = readI32Raw(entData, cursor);
        cursor += 4;
        if (pairs < 0 || pairs > 10000)
        {
            error = "pares de entidade invalidos";
            return {};
        }

        GenesisEntityRuntime entity;
        for (int32_t p = 0; p < pairs; ++p)
        {
            std::string key = readGenesisEntityString(entData, cursor, error);
            if (!error.empty())
                return {};
            std::string value = readGenesisEntityString(entData, cursor, error);
            if (!error.empty())
                return {};
            entity.kv[toLower(key)] = value;
        }
        entities.push_back(std::move(entity));
    }
    return entities;
}

glm::vec3 parseVec3String(const std::string &value, const glm::vec3 &fallback)
{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (std::sscanf(value.c_str(), "%f %f %f", &x, &y, &z) == 3)
        return glm::vec3(x, y, z);
    return fallback;
}

glm::vec3 parseSpawnForwardEngine(const std::unordered_map<std::string, std::string> &kv)
{
    const auto anglesIt = kv.find("angles");
    if (anglesIt != kv.end() && !anglesIt->second.empty())
    {
        const glm::vec3 gdir = mini_genesis::genesisAnglesToDir(parseVec3String(anglesIt->second, glm::vec3(0.0f)));
        return mini_genesis::genesisDirToEngine(gdir);
    }

    const auto angleIt = kv.find("angle");
    if (angleIt != kv.end() && !angleIt->second.empty())
    {
        float yaw = 0.0f;
        if (std::sscanf(angleIt->second.c_str(), "%f", &yaw) == 1)
        {
            const glm::vec3 gdir = mini_genesis::genesisAnglesToDir(glm::vec3(0.0f, yaw, 0.0f));
            return mini_genesis::genesisDirToEngine(gdir);
        }
    }

    return glm::vec3(0.0f, 0.0f, -1.0f);
}

bool isMoverClassname(const std::string &classname)
{
    if (classname == "func_door" || classname == "func_door_rotating" || classname == "func_plat")
        return true;
    return classname.find("door") != std::string::npos ||
           classname.find("plat") != std::string::npos ||
           classname.find("lift") != std::string::npos ||
           classname.find("elevator") != std::string::npos;
}

int parseBrushModelIndex(const std::string &modelValue)
{
    if (modelValue.size() < 2 || modelValue[0] != '*')
        return -1;
    try
    {
        return std::stoi(modelValue.substr(1));
    }
    catch (...)
    {
        return -1;
    }
}

struct FaceTexAdjust
{
    float shiftU = 0.0f;
    float shiftV = 0.0f;
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
    int32_t pageSize = 512;
    std::vector<std::vector<uint8_t>> pages;
    std::vector<FaceLightmapRect> rects;
};

LightmapAtlas buildLightmapAtlas(const GenesisBspRuntime &bsp)
{
    LightmapAtlas atlas;
    atlas.rects.resize(bsp.faces.size());
    if (bsp.lightData.empty())
        return atlas;

    constexpr int32_t kPadding = 1;
    atlas.pages.emplace_back(static_cast<size_t>(atlas.pageSize) * static_cast<size_t>(atlas.pageSize) * 3u, 0u);

    int32_t page = 0;
    int32_t cursorX = kPadding;
    int32_t cursorY = kPadding;
    int32_t shelfHeight = 0;

    for (size_t faceIndex = 0; faceIndex < bsp.faces.size(); ++faceIndex)
    {
        const GenesisFace &face = bsp.faces[faceIndex];
        if (face.lightOfs < 0 || face.lightWidth <= 0 || face.lightHeight <= 0)
            continue;
        if (face.numVerts <= 0 || face.firstVert < 0 ||
            static_cast<size_t>(face.firstVert + face.numVerts) > bsp.vertIndices.size())
            continue;
        if (face.texInfo < 0 || face.texInfo >= static_cast<int32_t>(bsp.texInfos.size()))
            continue;

        const size_t lightOffset = static_cast<size_t>(face.lightOfs) + 1u;
        const size_t dataSize = static_cast<size_t>(face.lightWidth) * static_cast<size_t>(face.lightHeight) * 3u;
        if (lightOffset + dataSize > bsp.lightData.size())
            continue;

        if (face.lightWidth + kPadding * 2 > atlas.pageSize ||
            face.lightHeight + kPadding * 2 > atlas.pageSize)
            continue;

        if (cursorX + face.lightWidth + kPadding > atlas.pageSize)
        {
            cursorX = kPadding;
            cursorY += shelfHeight + kPadding;
            shelfHeight = 0;
        }
        if (cursorY + face.lightHeight + kPadding > atlas.pageSize)
        {
            atlas.pages.emplace_back(static_cast<size_t>(atlas.pageSize) * static_cast<size_t>(atlas.pageSize) * 3u, 0u);
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

        const GenesisTexInfo &tex = bsp.texInfos[static_cast<size_t>(face.texInfo)];
        float minU = 999999.0f;
        float minV = 999999.0f;
        for (int32_t i = 0; i < face.numVerts; ++i)
        {
            const int32_t srcIndex = bsp.vertIndices[static_cast<size_t>(face.firstVert + i)];
            if (srcIndex < 0 || srcIndex >= static_cast<int32_t>(bsp.verts.size()))
                continue;
            const glm::vec3 &position = bsp.verts[static_cast<size_t>(srcIndex)];
            minU = std::min(minU, glm::dot(position, tex.vecs[0]));
            minV = std::min(minV, glm::dot(position, tex.vecs[1]));
        }
        rect.minU = std::floor(minU / 16.0f) * 16.0f;
        rect.minV = std::floor(minV / 16.0f) * 16.0f;

        std::vector<uint8_t> &pagePixels = atlas.pages[static_cast<size_t>(rect.page)];
        for (int32_t y = 0; y < rect.height; ++y)
        {
            const size_t src = lightOffset + static_cast<size_t>(y) * static_cast<size_t>(rect.width) * 3u;
            const size_t dst = (static_cast<size_t>(rect.y + y) * static_cast<size_t>(atlas.pageSize) +
                                static_cast<size_t>(rect.x)) * 3u;
            std::memcpy(pagePixels.data() + dst, bsp.lightData.data() + src, static_cast<size_t>(rect.width) * 3u);
        }

        cursorX += face.lightWidth + kPadding;
        shelfHeight = std::max(shelfHeight, face.lightHeight);
    }

    return atlas;
}
} // namespace

glm::vec3 GenesisBspLoader::pointToEngine(const glm::vec3 &point)
{
    return mini_genesis::genesisPointToEngine(point);
}

bool GenesisBspLoader::load(const std::string &path,
                            Mesh &mesh,
                            GenesisBspCollider &collider,
                            GenesisLoadResult &out) const
{
    GenesisBspRuntime gbsp;
    std::string parseError;
    if (!parseGenesisBspRuntime(path, gbsp, parseError))
    {
        out.error = "falha no parser GBSP: " + parseError;
        out.status.clear();
        return false;
    }

    mesh.release_materials();
    mesh.materials.clear();
    mesh.surfaces.clear();
    mesh.buffer.vertices.clear();
    mesh.buffer.indices.clear();

    std::vector<Texture *> decodedTextures(gbsp.textures.size(), nullptr);
    for (size_t i = 0; i < gbsp.textures.size(); ++i)
    {
        const GenesisTexture &src = gbsp.textures[i];
        Texture *tex = nullptr;
        if (src.width > 0 && src.height > 0 && src.offset >= 0 && src.paletteIndex >= 0)
        {
            const size_t pixelCount = static_cast<size_t>(src.width) * static_cast<size_t>(src.height);
            const size_t texOffset = static_cast<size_t>(src.offset);
            const size_t palOffset = static_cast<size_t>(src.paletteIndex) * 256u * 3u;
            if (texOffset + pixelCount <= gbsp.texData.size() && palOffset + 256u * 3u <= gbsp.palettes.size())
            {
                Pixmap pix(src.width, src.height, 3);
                for (int y = 0; y < src.height; ++y)
                {
                    for (int x = 0; x < src.width; ++x)
                    {
                        const size_t p = static_cast<size_t>(y) * static_cast<size_t>(src.width) + static_cast<size_t>(x);
                        const uint8_t idx = gbsp.texData[texOffset + p];
                        const size_t po = palOffset + static_cast<size_t>(idx) * 3u;
                        pix.SetPixel(static_cast<u32>(x),
                                     static_cast<u32>(y),
                                     gbsp.palettes[po + 0u],
                                     gbsp.palettes[po + 1u],
                                     gbsp.palettes[po + 2u],
                                     255);
                    }
                }
                const std::string texName = "genesis_emb_tex_" + std::to_string(i);
                tex = TextureManager::instance().createFromPixmap(texName, pix);
            }
        }

        decodedTextures[i] = tex;
    }

    const LightmapAtlas lightmapAtlas = buildLightmapAtlas(gbsp);
    std::vector<Texture *> lightmapTextures(lightmapAtlas.pages.size(), nullptr);
    for (size_t i = 0; i < lightmapAtlas.pages.size(); ++i)
    {
        const std::vector<uint8_t> &rgb = lightmapAtlas.pages[i];
        if (rgb.empty())
            continue;

        std::vector<uint8_t> rgba(static_cast<size_t>(lightmapAtlas.pageSize) * static_cast<size_t>(lightmapAtlas.pageSize) * 4u, 255u);
        for (size_t p = 0; p < static_cast<size_t>(lightmapAtlas.pageSize) * static_cast<size_t>(lightmapAtlas.pageSize); ++p)
        {
            rgba[p * 4u + 0u] = rgb[p * 3u + 0u];
            rgba[p * 4u + 1u] = rgb[p * 3u + 1u];
            rgba[p * 4u + 2u] = rgb[p * 3u + 2u];
            rgba[p * 4u + 3u] = 255u;
        }
        lightmapTextures[i] = TextureManager::instance().createFromMemory(
            "genesis_emb_lm_" + std::to_string(i),
            lightmapAtlas.pageSize,
            lightmapAtlas.pageSize,
            PixelType::RGBA,
            rgba.data(),
            rgba.size());
    }

    std::string entityError;
    const std::vector<GenesisEntityRuntime> entities = parseGenesisEntitiesRuntime(gbsp.entData, entityError);
    std::vector<uint8_t> excludeFace(gbsp.faces.size(), 0u);
    if (!gbsp.models.empty() && !entities.empty())
    {
        for (const GenesisEntityRuntime &entity : entities)
        {
            const auto clsIt = entity.kv.find("classname");
            const std::string classname = (clsIt != entity.kv.end()) ? toLower(clsIt->second) : std::string();
            if (!isMoverClassname(classname))
                continue;

            const auto modelIt = entity.kv.find("model");
            if (modelIt == entity.kv.end())
                continue;
            const int modelIndex = parseBrushModelIndex(modelIt->second);
            if (modelIndex < 0 || modelIndex >= static_cast<int>(gbsp.models.size()))
                continue;

            const GenesisBspModel &model = gbsp.models[static_cast<size_t>(modelIndex)];
            if (model.numFaces <= 0 || model.firstFace < 0)
                continue;
            const int start = model.firstFace;
            const int end = std::min<int>(start + model.numFaces, static_cast<int>(excludeFace.size()));
            for (int f = start; f < end; ++f)
                excludeFace[static_cast<size_t>(f)] = 1u;
        }
    }

    std::vector<FaceTexAdjust> faceTexAdjusts(gbsp.faces.size());
    for (size_t faceIndex = 0; faceIndex < gbsp.faces.size(); ++faceIndex)
    {
        if (excludeFace[faceIndex])
            continue;
        const GenesisFace &face = gbsp.faces[faceIndex];
        if (face.texInfo < 0 || face.texInfo >= static_cast<int32_t>(gbsp.texInfos.size()))
            continue;
        const GenesisTexInfo &tex = gbsp.texInfos[static_cast<size_t>(face.texInfo)];
        if (tex.texture < 0 || tex.texture >= static_cast<int32_t>(gbsp.textures.size()))
            continue;
        const GenesisTexture &texture = gbsp.textures[static_cast<size_t>(tex.texture)];
        if (texture.width <= 0 || texture.height <= 0)
            continue;
        if (face.numVerts <= 0 || face.firstVert < 0 ||
            static_cast<size_t>(face.firstVert + face.numVerts) > gbsp.vertIndices.size())
            continue;

        float minU = 99999.0f;
        float minV = 99999.0f;
        for (int32_t i = 0; i < face.numVerts; ++i)
        {
            const int32_t srcIndex = gbsp.vertIndices[static_cast<size_t>(face.firstVert + i)];
            if (srcIndex < 0 || srcIndex >= static_cast<int32_t>(gbsp.verts.size()))
                continue;
            const glm::vec3 &position = gbsp.verts[static_cast<size_t>(srcIndex)];
            minU = std::min(minU, glm::dot(position, tex.vecs[0]));
            minV = std::min(minV, glm::dot(position, tex.vecs[1]));
        }

        const float scaleU = tex.drawScale[0] != 0.0f ? (1.0f / tex.drawScale[0]) : 1.0f;
        const float scaleV = tex.drawScale[1] != 0.0f ? (1.0f / tex.drawScale[1]) : 1.0f;
        const float au = static_cast<float>(static_cast<int32_t>((minU * scaleU + tex.shift[0]) / texture.width)) * static_cast<float>(texture.width);
        const float av = static_cast<float>(static_cast<int32_t>((minV * scaleV + tex.shift[1]) / texture.height)) * static_cast<float>(texture.height);
        faceTexAdjusts[faceIndex].shiftU = tex.shift[0] - au;
        faceTexAdjusts[faceIndex].shiftV = tex.shift[1] - av;
    }

    std::map<std::tuple<int, int>, std::vector<size_t>> facesByGroup;
    for (size_t faceIndex = 0; faceIndex < gbsp.faces.size(); ++faceIndex)
    {
        if (excludeFace[faceIndex])
            continue;
        const GenesisFace &face = gbsp.faces[faceIndex];
        int texIndex = -1;
        if (face.texInfo >= 0 && face.texInfo < static_cast<int>(gbsp.texInfos.size()))
            texIndex = gbsp.texInfos[static_cast<size_t>(face.texInfo)].texture;
        int lmPage = -1;
        if (faceIndex < lightmapAtlas.rects.size() && lightmapAtlas.rects[faceIndex].valid)
            lmPage = lightmapAtlas.rects[faceIndex].page;
        facesByGroup[std::make_tuple(texIndex, lmPage)].push_back(faceIndex);
    }

    for (const auto &entry : facesByGroup)
    {
        const int texIndex = std::get<0>(entry.first);
        const int lmPage = std::get<1>(entry.first);
        const uint32_t surfaceStart = static_cast<uint32_t>(mesh.buffer.indices.size());

        for (size_t faceIndex : entry.second)
        {
            const GenesisFace &face = gbsp.faces[faceIndex];
            if (face.numVerts < 3 || face.firstVert < 0)
                continue;
            if (static_cast<size_t>(face.firstVert + face.numVerts) > gbsp.vertIndices.size())
                continue;

            const GenesisTexInfo *texInfo = nullptr;
            const GenesisTexture *texture = nullptr;
            if (face.texInfo >= 0 && face.texInfo < static_cast<int>(gbsp.texInfos.size()))
            {
                texInfo = &gbsp.texInfos[static_cast<size_t>(face.texInfo)];
                if (texInfo->texture >= 0 && texInfo->texture < static_cast<int>(gbsp.textures.size()))
                    texture = &gbsp.textures[static_cast<size_t>(texInfo->texture)];
            }

            const uint32_t baseVertex = static_cast<uint32_t>(mesh.buffer.vertices.size());
            std::vector<glm::vec3> positions;
            positions.reserve(static_cast<size_t>(face.numVerts));

            for (int32_t i = 0; i < face.numVerts; ++i)
            {
                const int32_t srcIndex = gbsp.vertIndices[static_cast<size_t>(face.firstVert + i)];
                if (srcIndex < 0 || srcIndex >= static_cast<int32_t>(gbsp.verts.size()))
                    continue;

                const glm::vec3 srcPos = gbsp.verts[static_cast<size_t>(srcIndex)];
                const glm::vec3 pos = pointToEngine(srcPos);
                positions.push_back(pos);

                Vertex v{};
                v.position = pos;
                v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                if (texInfo && texture && texture->width > 0 && texture->height > 0)
                {
                    const float su = texInfo->drawScale[0] != 0.0f ? texInfo->drawScale[0] : 1.0f;
                    const float sv = texInfo->drawScale[1] != 0.0f ? texInfo->drawScale[1] : 1.0f;
                    const FaceTexAdjust &adjust = faceTexAdjusts[faceIndex];
                    v.uv.x = (glm::dot(srcPos, texInfo->vecs[0]) * su + adjust.shiftU) / static_cast<float>(texture->width);
                    v.uv.y = (glm::dot(srcPos, texInfo->vecs[1]) * sv + adjust.shiftV) / static_cast<float>(texture->height);
                }
                if (texInfo && faceIndex < lightmapAtlas.rects.size())
                {
                    const FaceLightmapRect &rect = lightmapAtlas.rects[faceIndex];
                    if (rect.valid && rect.page >= 0)
                    {
                        const float localU = (glm::dot(srcPos, texInfo->vecs[0]) - rect.minU) / 16.0f;
                        const float localV = (glm::dot(srcPos, texInfo->vecs[1]) - rect.minV) / 16.0f;
                        v.tangent.x = (static_cast<float>(rect.x) + localU + 0.5f) / static_cast<float>(lightmapAtlas.pageSize);
                        v.tangent.y = (static_cast<float>(rect.y) + localV + 0.5f) / static_cast<float>(lightmapAtlas.pageSize);
                        v.tangent.z = 0.0f;
                        v.tangent.w = 1.0f;
                    }
                }
                mesh.buffer.vertices.push_back(v);
            }

            if (positions.size() < 3)
                continue;

            glm::vec3 normal = glm::normalize(glm::cross(positions[1] - positions[0], positions[2] - positions[0]));
            if (face.planeSide)
                normal = -normal;

            const uint32_t faceVertCount = static_cast<uint32_t>(positions.size());
            for (uint32_t i = 0; i < faceVertCount; ++i)
                mesh.buffer.vertices[baseVertex + i].normal = normal;

            for (uint32_t i = 1; i + 1 < faceVertCount; ++i)
            {
                mesh.buffer.indices.push_back(baseVertex);
                mesh.buffer.indices.push_back(baseVertex + i);
                mesh.buffer.indices.push_back(baseVertex + i + 1);
            }
        }

        const uint32_t indexCount = static_cast<uint32_t>(mesh.buffer.indices.size()) - surfaceStart;
        if (indexCount > 0)
        {
            Material *mat = new Material();
            mat->name = "genesis_mat_" + std::to_string(mesh.materials.size());
            mat->setCullFace(false);
            mat->setVec4("u_color", glm::vec4(1.0f));

            Texture *albedo = nullptr;
            if (texIndex >= 0 && texIndex < static_cast<int>(decodedTextures.size()))
                albedo = decodedTextures[static_cast<size_t>(texIndex)];
            if (!albedo)
                albedo = TextureManager::instance().getWhite();
            mat->setTexture("u_albedo", albedo);
            mat->setInt("u_hasAlbedo", albedo ? 1 : 0);

            Texture *lmTex = TextureManager::instance().getWhite();
            int hasLm = 0;
            if (lmPage >= 0 && lmPage < static_cast<int>(lightmapTextures.size()) &&
                lightmapTextures[static_cast<size_t>(lmPage)])
            {
                lmTex = lightmapTextures[static_cast<size_t>(lmPage)];
                hasLm = 1;
            }
            mat->setTexture("u_lightmap", lmTex);
            mat->setInt("u_hasLightmap", hasLm);

            const int matIndex = mesh.add_material(mat);
            mesh.add_surface(surfaceStart, indexCount, matIndex);
        }
    }

    if (mesh.buffer.vertices.empty() || mesh.buffer.indices.empty())
    {
        out.error = "GBSP sem triangulos validos.";
        out.status.clear();
        return false;
    }

    mesh.upload();

    std::vector<GenesisBspPlane> enginePlanes = gbsp.planes;
    for (GenesisBspPlane &plane : enginePlanes)
        plane.normal = pointToEngine(plane.normal);
    if (!gbsp.nodes.empty() && !gbsp.leafContents.empty())
        collider.setNodeTree(gbsp.nodes,
                             enginePlanes,
                             gbsp.leafContents,
                             gbsp.leafFirstSides,
                             gbsp.leafNumSides,
                             gbsp.leafSides,
                             gbsp.rootNode);
    else
        collider.setTree(gbsp.bnodes, enginePlanes, gbsp.rootBNode);

    out.playerStarts.clear();
    out.playerStartForwards.clear();
    for (const GenesisEntityRuntime &entity : entities)
    {
        const auto clsIt = entity.kv.find("classname");
        const std::string classname = (clsIt != entity.kv.end()) ? toLower(clsIt->second) : std::string();
        if (classname == "info_player_start" ||
            classname == "deathmatchstart" ||
            classname == "playerstart" ||
            classname == "player_start")
        {
            const auto originIt = entity.kv.find("origin");
            const glm::vec3 genesisSpawn = parseVec3String(originIt != entity.kv.end() ? originIt->second : "", glm::vec3(0.0f));
            out.playerStarts.push_back(pointToEngine(genesisSpawn));
            out.playerStartForwards.push_back(parseSpawnForwardEngine(entity.kv));
        }
    }

    out.bounds = mesh.aabb;
    out.status = "GBSP carregado nativamente (runtime, sem conversao).";
    out.error = entityError.empty() ? std::string() : ("entidades: " + entityError);
    return true;
}
