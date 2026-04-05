#include "Manager.hpp"
#include "Animation.hpp"
#include "BinaryStream.hpp"
#include "Utils.hpp"

#include <SDL2/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
struct ChunkStack
{
    std::vector<Sint64> ends;

    void push(BinaryStream &s, uint32_t len) { ends.push_back(s.tell() + static_cast<Sint64>(len)); }

    void pop(BinaryStream &s)
    {
        if (ends.empty())
            return;
        s.seek(ends.back());
        ends.pop_back();
    }

    Sint64 remaining(const BinaryStream &s) const
    {
        if (ends.empty())
            return 0;
        return ends.back() - s.tell();
    }
};

struct TextureEntry
{
    std::string name;
};

struct BrushEntry
{
    int tex0 = -1;
};

struct PendingSurface
{
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;
    int brushId = -1;
};

struct BoneAnimData
{
    glm::vec3 bindPos = glm::vec3(0.0f);
    glm::quat bindRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 bindScale = glm::vec3(1.0f);

    std::vector<PosKey> pos;
    std::vector<RotKey> rot;
    std::vector<ScaleKey> scale;
};

struct WeightSlots
{
    int ids[4] = {-1, -1, -1, -1};
    float w[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    void add(int boneId, float weight)
    {
        if (boneId < 0 || weight <= 0.0f)
            return;

        if (weight > w[0])
        {
            ids[3] = ids[2]; w[3] = w[2];
            ids[2] = ids[1]; w[2] = w[1];
            ids[1] = ids[0]; w[1] = w[0];
            ids[0] = boneId; w[0] = weight;
        }
        else if (weight > w[1])
        {
            ids[3] = ids[2]; w[3] = w[2];
            ids[2] = ids[1]; w[2] = w[1];
            ids[1] = boneId; w[1] = weight;
        }
        else if (weight > w[2])
        {
            ids[3] = ids[2]; w[3] = w[2];
            ids[2] = boneId; w[2] = weight;
        }
        else if (weight > w[3])
        {
            ids[3] = boneId; w[3] = weight;
        }
    }
};

std::string readTag(BinaryStream &s)
{
    char tag[4] = {};
    s.readRaw(tag, sizeof(tag));
    return std::string(tag, 4);
}

std::string readCString(BinaryStream &s)
{
    std::string out;
    out.reserve(64);
    while (true)
    {
        const uint8_t c = s.readU8();
        if (c == 0)
            break;
        out.push_back(static_cast<char>(c));
    }
    return out;
}

glm::mat4 makeTRS(const glm::vec3 &t, const glm::quat &r, const glm::vec3 &s)
{
    glm::mat4 m = glm::translate(glm::mat4(1.0f), t);
    m *= glm::toMat4(r);
    return glm::scale(m, s);
}

int addBone(AnimatedMesh &mesh,
            std::vector<BoneAnimData> &boneAnim,
            const std::string &name,
            int parent,
            const glm::vec3 &pos,
            const glm::vec3 &scale,
            const glm::quat &rot)
{
    Bone bone;
    bone.name = name;
    bone.parent = parent;
    bone.localPose = makeTRS(pos, rot, scale);
    bone.offset = glm::mat4(1.0f);

    const int idx = static_cast<int>(mesh.bones.size());
    mesh.bones.push_back(bone);

    BoneAnimData data;
    data.bindPos = pos;
    data.bindScale = scale;
    data.bindRot = rot;
    boneAnim.push_back(data);
    return idx;
}

void parseStaticNodeChunk(BinaryStream &s,
                          ChunkStack &stack,
                          Mesh &mesh,
                          std::vector<PendingSurface> &surfaces,
                          const glm::mat4 &parentTransform)
{
    readCString(s);
    const glm::vec3 pos(s.readF32(), s.readF32(), s.readF32());
    const glm::vec3 scale(s.readF32(), s.readF32(), s.readF32());

    const float rw = s.readF32();
    const float rx = s.readF32();
    const float ry = s.readF32();
    const float rz = s.readF32();

    const glm::quat rot = glm::normalize(glm::quat(-rw, rx, ry, rz));
    const glm::mat4 nodeTransform = parentTransform * makeTRS(pos, rot, scale);
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(nodeTransform)));

    int nodeVertexStart = 0;

    while (stack.remaining(s) > 0)
    {
        const std::string tag = readTag(s);
        const uint32_t len = static_cast<uint32_t>(s.readI32());
        stack.push(s, len);

        if (tag == "MESH")
        {
            nodeVertexStart = static_cast<int>(mesh.buffer.vertices.size());
            s.readI32();

            while (stack.remaining(s) > 0)
            {
                const std::string mtag = readTag(s);
                const uint32_t mlen = static_cast<uint32_t>(s.readI32());
                stack.push(s, mlen);

                if (mtag == "VRTS")
                {
                    const int flags = s.readI32();
                    const int numUV = s.readI32();
                    const int uvSize = s.readI32();

                    int stride = 12;
                    if (flags & 1) stride += 12;
                    if (flags & 2) stride += 16;
                    stride += numUV * uvSize * 4;

                    const int count = (stride > 0) ? static_cast<int>(stack.remaining(s) / stride) : 0;
                    for (int i = 0; i < count; ++i)
                    {
                        Vertex v;
                        glm::vec3 localPos(s.readF32(), s.readF32(), s.readF32());
                        glm::vec3 localNormal(0.0f, 1.0f, 0.0f);
                        if (flags & 1)
                            localNormal = glm::vec3(s.readF32(), s.readF32(), s.readF32());
                        if (flags & 2)
                        {
                            s.readF32();
                            s.readF32();
                            s.readF32();
                            s.readF32();
                        }

                        v.uv = glm::vec2(0.0f);
                        for (int t = 0; t < numUV; ++t)
                        {
                            float u = 0.0f;
                            float vv = 0.0f;
                            if (uvSize >= 1) u = s.readF32();
                            if (uvSize >= 2) vv = s.readF32();
                            for (int k = 2; k < uvSize; ++k)
                                s.readF32();
                            if (t == 0)
                                v.uv = glm::vec2(u, vv);
                        }

                        v.position = glm::vec3(nodeTransform * glm::vec4(localPos, 1.0f));
                        v.normal = glm::normalize(normalMatrix * localNormal);
                        v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                        mesh.buffer.vertices.push_back(v);
                    }
                }
                else if (mtag == "TRIS")
                {
                    const int brushId = s.readI32();
                    PendingSurface surface;
                    surface.brushId = brushId;
                    surface.indexStart = static_cast<uint32_t>(mesh.buffer.indices.size());

                    const int triCount = static_cast<int>(stack.remaining(s) / 12);
                    for (int i = 0; i < triCount; ++i)
                    {
                        const int i0 = s.readI32();
                        const int i1 = s.readI32();
                        const int i2 = s.readI32();
                        mesh.buffer.indices.push_back(static_cast<uint32_t>(nodeVertexStart + i0));
                        mesh.buffer.indices.push_back(static_cast<uint32_t>(nodeVertexStart + i1));
                        mesh.buffer.indices.push_back(static_cast<uint32_t>(nodeVertexStart + i2));
                    }

                    surface.indexCount = static_cast<uint32_t>(mesh.buffer.indices.size()) - surface.indexStart;
                    if (surface.indexCount > 0)
                        surfaces.push_back(surface);
                }

                stack.pop(s);
            }
        }
        else if (tag == "NODE")
        {
            parseStaticNodeChunk(s, stack, mesh, surfaces, nodeTransform);
        }

        stack.pop(s);
    }
}

void parseNodeChunk(BinaryStream &s,
                    ChunkStack &stack,
                    AnimatedMesh &mesh,
                    std::vector<BoneAnimData> &boneAnim,
                    std::vector<WeightSlots> &weights,
                    std::vector<PendingSurface> &surfaces,
                    int &animFrames,
                    float &animFps,
                    int parentBone)
{
    const std::string nodeName = readCString(s);
    const glm::vec3 pos(s.readF32(), s.readF32(), s.readF32());
    const glm::vec3 scale(s.readF32(), s.readF32(), s.readF32());

    const float rw = s.readF32();
    const float rx = s.readF32();
    const float ry = s.readF32();
    const float rz = s.readF32();

    const glm::quat rot = glm::normalize(glm::quat(-rw, rx, ry, rz));
    const int myBone = addBone(mesh, boneAnim, nodeName, parentBone, pos, scale, rot);

    int nodeVertexStart = 0;

    while (stack.remaining(s) > 0)
    {
        const std::string tag = readTag(s);
        const uint32_t len = static_cast<uint32_t>(s.readI32());
        stack.push(s, len);

        if (tag == "MESH")
        {
            nodeVertexStart = static_cast<int>(mesh.buffer.vertices.size());
            s.readI32();

            while (stack.remaining(s) > 0)
            {
                const std::string mtag = readTag(s);
                const uint32_t mlen = static_cast<uint32_t>(s.readI32());
                stack.push(s, mlen);

                if (mtag == "VRTS")
                {
                    const int flags = s.readI32();
                    const int numUV = s.readI32();
                    const int uvSize = s.readI32();

                    int stride = 12;
                    if (flags & 1) stride += 12;
                    if (flags & 2) stride += 16;
                    stride += numUV * uvSize * 4;

                    const int count = (stride > 0) ? static_cast<int>(stack.remaining(s) / stride) : 0;
                    for (int i = 0; i < count; ++i)
                    {
                        AnimatedVertex v;
                        v.position = glm::vec3(s.readF32(), s.readF32(), s.readF32());
                        v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                        if (flags & 1)
                            v.normal = glm::vec3(s.readF32(), s.readF32(), s.readF32());
                        if (flags & 2)
                        {
                            s.readF32();
                            s.readF32();
                            s.readF32();
                            s.readF32();
                        }

                        v.uv = glm::vec2(0.0f);
                        for (int t = 0; t < numUV; ++t)
                        {
                            float u = 0.0f;
                            float vv = 0.0f;
                            if (uvSize >= 1) u = s.readF32();
                            if (uvSize >= 2) vv = s.readF32();
                            for (int k = 2; k < uvSize; ++k)
                                s.readF32();
                            if (t == 0)
                                v.uv = glm::vec2(u, vv);
                        }

                        v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                        v.boneIds = glm::ivec4(0, 0, 0, 0);
                        v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                        mesh.buffer.vertices.push_back(v);
                        weights.push_back(WeightSlots());
                    }
                }
                else if (mtag == "TRIS")
                {
                    const int brushId = s.readI32();
                    PendingSurface surface;
                    surface.brushId = brushId;
                    surface.indexStart = static_cast<uint32_t>(mesh.buffer.indices.size());

                    const int triCount = static_cast<int>(stack.remaining(s) / 12);
                    for (int i = 0; i < triCount; ++i)
                    {
                        const int i0 = s.readI32();
                        const int i1 = s.readI32();
                        const int i2 = s.readI32();
                        mesh.buffer.indices.push_back(static_cast<uint32_t>(nodeVertexStart + i0));
                        mesh.buffer.indices.push_back(static_cast<uint32_t>(nodeVertexStart + i1));
                        mesh.buffer.indices.push_back(static_cast<uint32_t>(nodeVertexStart + i2));
                    }

                    surface.indexCount = static_cast<uint32_t>(mesh.buffer.indices.size()) - surface.indexStart;
                    if (surface.indexCount > 0)
                        surfaces.push_back(surface);
                }

                stack.pop(s);
            }
        }
        else if (tag == "BONE")
        {
            while (stack.remaining(s) > 0)
            {
                const int localVertex = s.readI32();
                const float weight = s.readF32();
                const int globalVertex = nodeVertexStart + localVertex;
                if (globalVertex >= 0 && globalVertex < static_cast<int>(weights.size()))
                    weights[static_cast<size_t>(globalVertex)].add(myBone, weight);
            }
        }
        else if (tag == "ANIM")
        {
            s.readI32();
            animFrames = s.readI32();
            animFps = s.readF32();
            if (animFps <= 0.0f)
                animFps = 25.0f;
        }
        else if (tag == "KEYS")
        {
            const int flags = s.readI32();
            BoneAnimData &data = boneAnim[static_cast<size_t>(myBone)];
            while (stack.remaining(s) > 0)
            {
                const float t = static_cast<float>(s.readI32());
                if (flags & 1)
                    data.pos.push_back({t, glm::vec3(s.readF32(), s.readF32(), s.readF32())});
                if (flags & 2)
                    data.scale.push_back({t, glm::vec3(s.readF32(), s.readF32(), s.readF32())});
                if (flags & 4)
                {
                    const float qw = s.readF32();
                    const float qx = s.readF32();
                    const float qy = s.readF32();
                    const float qz = s.readF32();
                    glm::quat q(-qw, qx, qy, qz);
                    const float len = glm::length(q);
                    if (len > 1e-8f)
                        q /= len;
                    data.rot.push_back({t, q});
                }
            }
        }
        else if (tag == "NODE")
        {
            parseNodeChunk(s, stack, mesh, boneAnim, weights, surfaces, animFrames, animFps, myBone);
        }

        stack.pop(s);
    }
}

bool loadB3DAnimated(const std::string &name,
                     const std::string &path,
                     const std::string &textureDir,
                     AnimatedMesh *outMesh)
{
    if (!outMesh)
        return false;

    outMesh->name = name;
    outMesh->buffer.vertices.clear();
    outMesh->buffer.indices.clear();
    outMesh->surfaces.clear();
    outMesh->release_materials();
    outMesh->releaseAnimations();
    outMesh->bones.clear();
    outMesh->finalMatrices.clear();

    BinaryStream s(path, "rb");
    if (!s.isOpen())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[B3D] Failed to open %s", path.c_str());
        return false;
    }

    const std::string rootTag = readTag(s);
    if (rootTag != "BB3D")
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[B3D] Invalid root tag '%s' in %s", rootTag.c_str(), path.c_str());
        return false;
    }

    ChunkStack stack;
    stack.push(s, static_cast<uint32_t>(s.readI32()));
    s.readI32();

    std::vector<TextureEntry> textures;
    std::vector<BrushEntry> brushes;
    std::vector<PendingSurface> surfaces;
    std::vector<BoneAnimData> boneAnim;
    std::vector<WeightSlots> weights;

    int animFrames = 0;
    float animFps = 25.0f;
    (void)animFrames;
    (void)animFps;

    while (stack.remaining(s) > 0)
    {
        const std::string tag = readTag(s);
        const uint32_t len = static_cast<uint32_t>(s.readI32());
        stack.push(s, len);

        if (tag == "TEXS")
        {
            while (stack.remaining(s) > 0)
            {
                TextureEntry te;
                te.name = readCString(s);
                s.readI32();
                s.readI32();
                s.readF32(); s.readF32();
                s.readF32(); s.readF32();
                s.readF32();
                textures.push_back(std::move(te));
            }
        }
        else if (tag == "BRUS")
        {
            const int nTex = s.readI32();
            while (stack.remaining(s) > 0)
            {
                BrushEntry be;
                readCString(s);
                s.readF32(); s.readF32(); s.readF32(); s.readF32();
                s.readF32();
                s.readI32();
                s.readI32();
                for (int i = 0; i < nTex; ++i)
                {
                    const int tid = s.readI32();
                    if (i == 0)
                        be.tex0 = tid;
                }
                brushes.push_back(be);
            }
        }
        else if (tag == "NODE")
        {
            parseNodeChunk(s, stack, *outMesh, boneAnim, weights, surfaces, animFrames, animFps, -1);
        }

        stack.pop(s);
    }
    stack.pop(s);

    if (outMesh->buffer.vertices.empty() || outMesh->buffer.indices.empty())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[B3D] No geometry in %s", path.c_str());
        return false;
    }

    const int maxShaderBones = 100;
    if (!outMesh->bones.empty())
    {
        const int n = static_cast<int>(outMesh->bones.size());
        std::vector<glm::mat4> global(n, glm::mat4(1.0f));
        for (int i = 0; i < n; ++i)
        {
            const int parent = outMesh->bones[i].parent;
            if (parent >= 0 && parent < n)
                global[i] = global[parent] * outMesh->bones[i].localPose;
            else
                global[i] = outMesh->bones[i].localPose;
        }
        for (int i = 0; i < n; ++i)
            outMesh->bones[i].offset = glm::inverse(global[i]);
    }

    for (size_t i = 0; i < outMesh->buffer.vertices.size(); ++i)
    {
        AnimatedVertex &v = outMesh->buffer.vertices[i];
        const WeightSlots ws = (i < weights.size()) ? weights[i] : WeightSlots{};
        const float sum = ws.w[0] + ws.w[1] + ws.w[2] + ws.w[3];

        if (sum <= 1e-8f)
        {
            v.boneIds = glm::ivec4(0, 0, 0, 0);
            v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            continue;
        }

        int b0 = (ws.ids[0] >= 0) ? ws.ids[0] : 0;
        int b1 = (ws.ids[1] >= 0) ? ws.ids[1] : b0;
        int b2 = (ws.ids[2] >= 0) ? ws.ids[2] : b0;
        int b3 = (ws.ids[3] >= 0) ? ws.ids[3] : b0;

        b0 = std::clamp(b0, 0, maxShaderBones - 1);
        b1 = std::clamp(b1, 0, maxShaderBones - 1);
        b2 = std::clamp(b2, 0, maxShaderBones - 1);
        b3 = std::clamp(b3, 0, maxShaderBones - 1);

        v.boneIds = glm::ivec4(b0, b1, b2, b3);
        v.boneWeights = glm::vec4(ws.w[0], ws.w[1], ws.w[2], ws.w[3]) / sum;
    }

    const std::string baseDir = textureDir.empty() ? PathDirectory(path) : textureDir;
    std::vector<Texture *> textureSlots(textures.size(), nullptr);
    for (size_t i = 0; i < textures.size(); ++i)
    {
        std::string texPath = ResolveTexturePath(baseDir, textures[i].name);
        if (texPath.empty())
            texPath = ResolveTexturePath(baseDir, PathFilename(textures[i].name));
        if (!texPath.empty())
            textureSlots[i] = TextureManager::instance().load(outMesh->name + "::tex_" + std::to_string(i), texPath);
    }

    Texture *white = TextureManager::instance().getWhite();
    if (surfaces.empty())
        surfaces.push_back({0, static_cast<uint32_t>(outMesh->buffer.indices.size()), -1});

    for (size_t i = 0; i < surfaces.size(); ++i)
    {
        Texture *albedo = white;
        if (surfaces[i].brushId >= 0 && surfaces[i].brushId < static_cast<int>(brushes.size()))
        {
            const int texSlot = brushes[surfaces[i].brushId].tex0;
            if (texSlot >= 0 && texSlot < static_cast<int>(textureSlots.size()) && textureSlots[texSlot])
                albedo = textureSlots[texSlot];
        }

        Material *mat = new Material();
        mat->name = outMesh->name + "::surf_" + std::to_string(i);
        mat->setTexture("u_albedo", albedo);
        mat->setVec4("u_color", glm::vec4(1.0f));

        const int matIdx = outMesh->add_material(mat);
        outMesh->add_surface(surfaces[i].indexStart, surfaces[i].indexCount, matIdx);
    }

    if (!outMesh->buffer.indices.empty() && !outMesh->buffer.vertices.empty())
        outMesh->compute_tangents();

    if (outMesh->bones.empty())
    {
        outMesh->finalMatrices.resize(1, glm::mat4(1.0f));
        for (AnimatedVertex &v : outMesh->buffer.vertices)
        {
            v.boneIds = glm::ivec4(0, 0, 0, 0);
            v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        }
    }
    else
    {
        outMesh->finalMatrices.resize(outMesh->bones.size(), glm::mat4(1.0f));
    }

    if (!outMesh->bones.empty())
    {
        auto *animation = new Animation();
        animation->name = "default";
        animation->ticksPerSecond = (animFps > 0.0f) ? animFps : 25.0f;
        animation->duration = (animFrames > 1) ? static_cast<float>(animFrames - 1) : 1.0f;
        animation->channels.resize(outMesh->bones.size());

        for (size_t i = 0; i < outMesh->bones.size(); ++i)
        {
            AnimationChannel &channel = animation->channels[i];
            BoneAnimData &src = boneAnim[i];
            channel.boneName = outMesh->bones[i].name;

            if (!src.pos.empty())
            {
                std::sort(src.pos.begin(), src.pos.end(),
                          [](const PosKey &a, const PosKey &b) { return a.time < b.time; });
                channel.posKeys = src.pos;
            }
            else
            {
                channel.posKeys.push_back({0.0f, src.bindPos});
            }

            if (!src.rot.empty())
            {
                std::sort(src.rot.begin(), src.rot.end(),
                          [](const RotKey &a, const RotKey &b) { return a.time < b.time; });
                channel.rotKeys = src.rot;
            }
            else
            {
                channel.rotKeys.push_back({0.0f, src.bindRot});
            }

            if (!src.scale.empty())
            {
                std::sort(src.scale.begin(), src.scale.end(),
                          [](const ScaleKey &a, const ScaleKey &b) { return a.time < b.time; });
                channel.scaleKeys = src.scale;
            }
            else
            {
                channel.scaleKeys.push_back({0.0f, src.bindScale});
            }
        }

        outMesh->animations.push_back(animation);
    }

    outMesh->upload();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[B3D] Loaded '%s': verts=%zu tris=%zu bones=%zu surfs=%zu",
                path.c_str(),
                outMesh->buffer.vertices.size(),
                outMesh->buffer.indices.size() / 3u,
                outMesh->bones.size(),
                outMesh->surfaces.size());
    return true;
}

bool loadB3DStatic(const std::string &name,
                   const std::string &path,
                   const std::string &textureDir,
                   Mesh *outMesh)
{
    if (!outMesh)
        return false;

    outMesh->name = name;
    outMesh->buffer.vertices.clear();
    outMesh->buffer.indices.clear();
    outMesh->surfaces.clear();
    outMesh->release_materials();

    BinaryStream s(path, "rb");
    if (!s.isOpen())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[B3D] Failed to open %s", path.c_str());
        return false;
    }

    const std::string rootTag = readTag(s);
    if (rootTag != "BB3D")
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[B3D] Invalid root tag '%s' in %s", rootTag.c_str(), path.c_str());
        return false;
    }

    ChunkStack stack;
    stack.push(s, static_cast<uint32_t>(s.readI32()));
    s.readI32();

    std::vector<TextureEntry> textures;
    std::vector<BrushEntry> brushes;
    std::vector<PendingSurface> surfaces;

    while (stack.remaining(s) > 0)
    {
        const std::string tag = readTag(s);
        const uint32_t len = static_cast<uint32_t>(s.readI32());
        stack.push(s, len);

        if (tag == "TEXS")
        {
            while (stack.remaining(s) > 0)
            {
                TextureEntry te;
                te.name = readCString(s);
                s.readI32();
                s.readI32();
                s.readF32(); s.readF32();
                s.readF32(); s.readF32();
                s.readF32();
                textures.push_back(std::move(te));
            }
        }
        else if (tag == "BRUS")
        {
            const int nTex = s.readI32();
            while (stack.remaining(s) > 0)
            {
                BrushEntry be;
                readCString(s);
                s.readF32(); s.readF32(); s.readF32(); s.readF32();
                s.readF32();
                s.readI32();
                s.readI32();
                for (int i = 0; i < nTex; ++i)
                {
                    const int tid = s.readI32();
                    if (i == 0)
                        be.tex0 = tid;
                }
                brushes.push_back(be);
            }
        }
        else if (tag == "NODE")
        {
            parseStaticNodeChunk(s, stack, *outMesh, surfaces, glm::mat4(1.0f));
        }

        stack.pop(s);
    }
    stack.pop(s);

    if (outMesh->buffer.vertices.empty() || outMesh->buffer.indices.empty())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[B3D] No geometry in %s", path.c_str());
        return false;
    }

    const std::string baseDir = textureDir.empty() ? PathDirectory(path) : textureDir;
    std::vector<Texture *> textureSlots(textures.size(), nullptr);
    for (size_t i = 0; i < textures.size(); ++i)
    {
        std::string texPath = ResolveTexturePath(baseDir, textures[i].name);
        if (texPath.empty())
            texPath = ResolveTexturePath(baseDir, PathFilename(textures[i].name));
        if (!texPath.empty())
            textureSlots[i] = TextureManager::instance().load(outMesh->name + "::tex_" + std::to_string(i), texPath);
    }

    Texture *white = TextureManager::instance().getWhite();
    if (surfaces.empty())
        surfaces.push_back({0, static_cast<uint32_t>(outMesh->buffer.indices.size()), -1});

    for (size_t i = 0; i < surfaces.size(); ++i)
    {
        Texture *albedo = white;
        if (surfaces[i].brushId >= 0 && surfaces[i].brushId < static_cast<int>(brushes.size()))
        {
            const int texSlot = brushes[surfaces[i].brushId].tex0;
            if (texSlot >= 0 && texSlot < static_cast<int>(textureSlots.size()) && textureSlots[texSlot])
                albedo = textureSlots[texSlot];
        }

        Material *mat = new Material();
        mat->name = outMesh->name + "::surf_" + std::to_string(i);
        mat->setTexture("u_albedo", albedo);
        mat->setVec4("u_color", glm::vec4(1.0f));

        const int matIdx = outMesh->add_material(mat);
        outMesh->add_surface(surfaces[i].indexStart, surfaces[i].indexCount, matIdx);
    }

    outMesh->compute_tangents();
    outMesh->upload();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[B3D] Loaded static '%s': verts=%zu tris=%zu surfs=%zu",
                path.c_str(),
                outMesh->buffer.vertices.size(),
                outMesh->buffer.indices.size() / 3u,
                outMesh->surfaces.size());
    return true;
}
}

Mesh *MeshManager::load_b3d(const std::string &name,
                            const std::string &path,
                            const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    auto *mesh = new Mesh();
    if (!loadB3DStatic(name, path, texture_dir, mesh))
    {
        delete mesh;
        return nullptr;
    }
    cache[name] = mesh;
    return mesh;
}

AnimatedMesh *AnimatedMeshManager::load_b3d(const std::string &name,
                                            const std::string &path,
                                            const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    auto *mesh = new AnimatedMesh();
    if (!loadB3DAnimated(name, path, texture_dir, mesh))
    {
        delete mesh;
        return nullptr;
    }

    cache[name] = mesh;
    return mesh;
}
