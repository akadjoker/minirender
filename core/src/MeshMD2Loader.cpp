#include "Manager.hpp"
#include "BinaryStream.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace
{
struct Md2Header
{
    int ident = 0;
    int version = 0;
    int skinWidth = 0;
    int skinHeight = 0;
    int frameSize = 0;
    int numSkins = 0;
    int numVerts = 0;
    int numUV = 0;
    int numTris = 0;
    int numGLCmds = 0;
    int numFrames = 0;
    int ofsSkins = 0;
    int ofsUV = 0;
    int ofsTris = 0;
    int ofsFrames = 0;
    int ofsGLCmds = 0;
    int ofsEnd = 0;
};

struct Md2Tri
{
    uint16_t vi[3] = {};
    uint16_t ti[3] = {};
};

struct Md2UV
{
    int16_t u = 0;
    int16_t v = 0;
};

struct Md2VertexKey
{
    uint16_t vi = 0;
    uint16_t ti = 0;

    bool operator==(const Md2VertexKey &other) const
    {
        return vi == other.vi && ti == other.ti;
    }
};

struct Md2VertexKeyHash
{
    std::size_t operator()(const Md2VertexKey &key) const
    {
        return (std::size_t(key.vi) << 16u) ^ std::size_t(key.ti);
    }
};

constexpr int kMd2Ident = 844121161;
constexpr int kMd2Version = 8;

glm::vec3 md2_to_engine_space(const glm::vec3 &p)
{
    return glm::vec3(p.x, p.z, p.y);
}
}

VertexAnimatedMesh *VertexAnimatedMeshManager::load_md2(const std::string &name,
                                                        const std::string &path,
                                                        const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    BinaryStream s(path, "rb");
    if (!s.isOpen())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[MD2] Failed to open %s", path.c_str());
        return nullptr;
    }

    Md2Header h;
    h.ident = s.readI32();
    h.version = s.readI32();
    h.skinWidth = s.readI32();
    h.skinHeight = s.readI32();
    h.frameSize = s.readI32();
    h.numSkins = s.readI32();
    h.numVerts = s.readI32();
    h.numUV = s.readI32();
    h.numTris = s.readI32();
    h.numGLCmds = s.readI32();
    h.numFrames = s.readI32();
    h.ofsSkins = s.readI32();
    h.ofsUV = s.readI32();
    h.ofsTris = s.readI32();
    h.ofsFrames = s.readI32();
    h.ofsGLCmds = s.readI32();
    h.ofsEnd = s.readI32();

    if (h.ident != kMd2Ident || h.version != kMd2Version ||
        h.numFrames <= 0 || h.numVerts <= 0 || h.numTris <= 0 || h.numUV <= 0 ||
        h.skinWidth <= 0 || h.skinHeight <= 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[MD2] Invalid MD2 header: %s", path.c_str());
        return nullptr;
    }

    std::string firstSkin;
    if (h.numSkins > 0)
    {
        s.seek(h.ofsSkins);
        char skinName[64] = {};
        s.readRaw(skinName, sizeof(skinName));
        firstSkin = skinName;
    }

    std::vector<Md2UV> uvs(h.numUV);
    s.seek(h.ofsUV);
    for (int i = 0; i < h.numUV; ++i)
    {
        uvs[i].u = s.readI16();
        uvs[i].v = s.readI16();
    }

    std::vector<Md2Tri> tris(h.numTris);
    s.seek(h.ofsTris);
    for (int i = 0; i < h.numTris; ++i)
    {
        for (int k = 0; k < 3; ++k)
            tris[i].vi[k] = s.readU16();
        for (int k = 0; k < 3; ++k)
            tris[i].ti[k] = s.readU16();
    }

    auto *mesh = new VertexAnimatedMesh();
    mesh->name = name;
    mesh->buffer.vertices.reserve(std::size_t(h.numTris) * 3);
    mesh->buffer.indices.reserve(std::size_t(h.numTris) * 3);

    std::unordered_map<Md2VertexKey, uint16_t, Md2VertexKeyHash> vertexMap;
    std::vector<uint16_t> sourceVertexForRenderVertex;
    vertexMap.reserve(std::size_t(h.numTris) * 3);
    sourceVertexForRenderVertex.reserve(std::size_t(h.numTris) * 3);

    for (const Md2Tri &tri : tris)
    {
        for (int k = 0; k < 3; ++k)
        {
            Md2VertexKey key{tri.vi[k], tri.ti[k]};
            auto it = vertexMap.find(key);
            uint16_t renderIndex;

            if (it == vertexMap.end())
            {
                renderIndex = (uint16_t)mesh->buffer.vertices.size();
                vertexMap.emplace(key, renderIndex);
                sourceVertexForRenderVertex.push_back(key.vi);

                VertexAnimVertex v{};
                v.uv = glm::vec2((float)uvs[key.ti].u / (float)h.skinWidth,
                                 (float)uvs[key.ti].v / (float)h.skinHeight);
                mesh->buffer.vertices.push_back(v);
            }
            else
            {
                renderIndex = it->second;
            }

            mesh->buffer.indices.push_back(renderIndex);
        }
    }

    mesh->frameNames.resize(h.numFrames);
    mesh->buffer.positions.resize(mesh->buffer.vertices.size());
    mesh->framePositions.resize(std::size_t(h.numFrames) * mesh->buffer.vertices.size());

    std::vector<glm::vec3> frameVerts(h.numVerts);
    s.seek(h.ofsFrames);
    for (int frame = 0; frame < h.numFrames; ++frame)
    {
        glm::vec3 scale;
        glm::vec3 translate;
        scale.x = s.readF32(); scale.y = s.readF32(); scale.z = s.readF32();
        translate.x = s.readF32(); translate.y = s.readF32(); translate.z = s.readF32();

        char frameName[16] = {};
        s.readRaw(frameName, sizeof(frameName));
        mesh->frameNames[frame] = frameName;

        for (int v = 0; v < h.numVerts; ++v)
        {
            const uint8_t x = s.readU8();
            const uint8_t y = s.readU8();
            const uint8_t z = s.readU8();
            (void)s.readU8();
            const glm::vec3 raw = glm::vec3((float)x, (float)y, (float)z) * scale + translate;
            frameVerts[v] = md2_to_engine_space(raw);
        }

        for (std::size_t i = 0; i < sourceVertexForRenderVertex.size(); ++i)
        {
            const glm::vec3 p = frameVerts[sourceVertexForRenderVertex[i]];
            if (frame == 0)
            {
                mesh->buffer.vertices[i].position = p;
                mesh->buffer.positions[i] = p;
            }
            mesh->framePositions[std::size_t(frame) * sourceVertexForRenderVertex.size() + i] = p;
        }
    }

    mesh->compute_normals();
    mesh->add_surface(0, (uint32_t)mesh->buffer.indices.size(), 0);

    auto *mat = new Material();
    mat->name = name;
    mat->type = MaterialType::Custom;
    if (!firstSkin.empty())
    {
        std::string texPath;
        if (!texture_dir.empty())
            texPath = (texture_dir.back() == '/' || texture_dir.back() == '\\') ? texture_dir + firstSkin : texture_dir + "/" + firstSkin;
        else
        {
            const auto slash = path.find_last_of("/\\");
            texPath = (slash == std::string::npos) ? firstSkin : path.substr(0, slash + 1) + firstSkin;
        }

        Texture *tex = TextureManager::instance().load(firstSkin, texPath);
        if (tex)
            mat->setTexture("u_albedo", tex);
    }
    mesh->materials.push_back(mat);

    mesh->upload();
    cache[name] = mesh;
    return mesh;
}
