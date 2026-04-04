#include "Md2Loader.hpp"

#include "BinaryStream.hpp"
#include "Manager.hpp"
#include "Utils.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

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

constexpr int kMd2Ident = 844121161; // IDP2
constexpr int kMd2Version = 8;

uint16_t readU16(BinaryStream &s)
{
    uint16_t v = 0;
    s.readRaw(&v, sizeof(v));
    return SDL_SwapLE16(v);
}

int16_t readI16(BinaryStream &s)
{
    return static_cast<int16_t>(readU16(s));
}

std::string stripFilePrefix(const std::string &path)
{
    if (path.rfind("file:", 0) == 0)
        return path.substr(5);
    return path;
}

Texture *loadTextureWithFallback(const std::string &path)
{
    if (path.empty())
        return nullptr;

    auto &texMgr = TextureManager::instance();
    const std::string rel = stripFilePrefix(path);

    const std::string tries[4] = {
        rel,
        std::string("bin/") + rel,
        std::string("../") + rel,
        std::string("../../") + rel
    };

    for (int i = 0; i < 4; ++i)
    {
        const std::string &p = tries[i];
        Texture *t = texMgr.load(std::string("md2:") + p, p);
        if (t)
            return t;
    }

    return nullptr;
}

} // namespace

glm::vec3 Md2RuntimeData::framePos(int frame, uint32_t baseVertex) const
{
    if (numFrames <= 0 || numBaseVertices <= 0)
        return glm::vec3(0.0f);

    frame = std::clamp(frame, 0, numFrames - 1);
    if (baseVertex >= static_cast<uint32_t>(numBaseVertices))
        return glm::vec3(0.0f);

    const size_t idx = static_cast<size_t>(frame) * static_cast<size_t>(numBaseVertices) +
                       static_cast<size_t>(baseVertex);
    if (idx >= framePositions.size())
        return glm::vec3(0.0f);
    return framePositions[idx];
}

bool Md2Loader::load(const std::string &modelPath,
                     const Options &options,
                     Mesh *outMesh,
                     Md2RuntimeData *outRuntime) const
{
    if (!outMesh)
    {
        LogError("[Md2Loader] outMesh is null");
        return false;
    }

    BinaryStream s(modelPath, "rb");
    if (!s.isOpen())
    {
        LogError("[Md2Loader] Failed to open %s", modelPath.c_str());
        return false;
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
        LogError("[Md2Loader] Invalid MD2 header in %s", modelPath.c_str());
        return false;
    }

    std::vector<Md2UV> uvs(static_cast<size_t>(h.numUV));
    s.seek(h.ofsUV);
    for (int i = 0; i < h.numUV; ++i)
    {
        uvs[static_cast<size_t>(i)].u = readI16(s);
        uvs[static_cast<size_t>(i)].v = readI16(s);
    }

    std::vector<Md2Tri> tris(static_cast<size_t>(h.numTris));
    s.seek(h.ofsTris);
    for (int i = 0; i < h.numTris; ++i)
    {
        for (int k = 0; k < 3; ++k)
            tris[static_cast<size_t>(i)].vi[k] = readU16(s);
        for (int k = 0; k < 3; ++k)
            tris[static_cast<size_t>(i)].ti[k] = readU16(s);
    }

    Md2RuntimeData localRuntime;
    localRuntime.numFrames = h.numFrames;
    localRuntime.numBaseVertices = h.numVerts;
    localRuntime.framePositions.assign(static_cast<size_t>(h.numFrames) * static_cast<size_t>(h.numVerts),
                                       glm::vec3(0.0f));

    s.seek(h.ofsFrames);
    for (int f = 0; f < h.numFrames; ++f)
    {
        glm::vec3 scale;
        glm::vec3 translate;
        scale.x = s.readF32(); scale.y = s.readF32(); scale.z = s.readF32();
        translate.x = s.readF32(); translate.y = s.readF32(); translate.z = s.readF32();

        char frameName[16] = {};
        s.readRaw(frameName, sizeof(frameName));

        for (int v = 0; v < h.numVerts; ++v)
        {
            const uint8_t x = s.readU8();
            const uint8_t y = s.readU8();
            const uint8_t z = s.readU8();
            (void)s.readU8(); // normal index (unused)

            const glm::vec3 p = glm::vec3(static_cast<float>(x),
                                          static_cast<float>(y),
                                          static_cast<float>(z)) * scale + translate;

            const size_t idx = static_cast<size_t>(f) * static_cast<size_t>(h.numVerts) +
                               static_cast<size_t>(v);
            localRuntime.framePositions[idx] = p;
            localRuntime.aabb.expand(p);
        }
    }

    outMesh->name = options.meshName.empty() ? outMesh->name : options.meshName;
    outMesh->buffer.dynamic = true;
    outMesh->buffer.vertices.clear();
    outMesh->buffer.indices.clear();
    outMesh->surfaces.clear();
    outMesh->materials.clear();
    localRuntime.cornerToBaseVertex.clear();

    outMesh->buffer.vertices.reserve(static_cast<size_t>(h.numTris) * 3u);
    outMesh->buffer.indices.reserve(static_cast<size_t>(h.numTris) * 3u);
    localRuntime.cornerToBaseVertex.reserve(static_cast<size_t>(h.numTris) * 3u);

    for (int t = 0; t < h.numTris; ++t)
    {
        for (int k = 0; k < 3; ++k)
        {
            const uint16_t baseVi = tris[static_cast<size_t>(t)].vi[k];
            const uint16_t uvI = tris[static_cast<size_t>(t)].ti[k];
            if (baseVi >= static_cast<uint16_t>(h.numVerts) || uvI >= static_cast<uint16_t>(h.numUV))
                continue;

            const glm::vec3 p = localRuntime.framePositions[static_cast<size_t>(baseVi)];
            const glm::vec2 uv(static_cast<float>(uvs[static_cast<size_t>(uvI)].u) / static_cast<float>(h.skinWidth),
                               static_cast<float>(uvs[static_cast<size_t>(uvI)].v) / static_cast<float>(h.skinHeight));

            Vertex vx;
            vx.position = p;
            vx.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            vx.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            vx.uv = uv;

            outMesh->buffer.vertices.push_back(vx);
            outMesh->buffer.indices.push_back(static_cast<uint32_t>(outMesh->buffer.vertices.size() - 1));
            localRuntime.cornerToBaseVertex.push_back(static_cast<uint32_t>(baseVi));
        }
    }

    outMesh->compute_normals();
    outMesh->buffer.upload();

    outMesh->add_surface(0, static_cast<uint32_t>(outMesh->buffer.indices.size()), 0).aabb = localRuntime.aabb;
    outMesh->aabb = localRuntime.aabb;

    auto &matMgr = MaterialManager::instance();
    auto &texMgr = TextureManager::instance();

    std::string matName = options.materialName;
    if (matName.empty())
        matName = outMesh->name.empty() ? "md2_material" : (outMesh->name + "_material");

    Material *mat = matMgr.create(matName);
    if (mat)
    {
        if (options.shader)
            mat->setShader(options.shader);

        mat->setCullFace(!options.disableBackfaceCulling ? true : false);

        Texture *albedo = loadTextureWithFallback(options.texturePath);
        if (!albedo)
            albedo = texMgr.getPattern();

        mat->setTexture("u_albedo", albedo);
        mat->setVec3("u_color", glm::vec3(1.0f));
    }

    outMesh->materials.clear();
    outMesh->materials.push_back(mat);

    if (outRuntime)
        *outRuntime = std::move(localRuntime);

    LogInfo("[Md2Loader] loaded: %s frames=%d baseVerts=%d tris=%d",
            modelPath.c_str(),
            h.numFrames,
            h.numVerts,
            h.numTris);
    return true;
}

Mesh *Md2Loader::load(const std::string &modelPath,
                      const Options &options,
                      Md2RuntimeData *outRuntime) const
{
    const std::string meshName = options.meshName.empty() ? "md2_mesh" : options.meshName;
    Mesh *mesh = MeshManager::instance().create(meshName);
    if (!mesh)
        return nullptr;

    if (!load(modelPath, options, mesh, outRuntime))
        return nullptr;

    return mesh;
}
