#include "Manager.hpp"
#include "Utils.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
constexpr int32_t kQ3Ident = 1347633737;
constexpr int32_t kQ3Version = 46;

constexpr int kNumLumps = 17;
constexpr int kLumpTextures = 1;
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
constexpr size_t kLightmapBytes = 128 * 128 * 3;
constexpr float kEpsilon = 1e-6f;
constexpr int kPatchTess = 5;

struct Lump
{
    int32_t offset = 0;
    int32_t length = 0;
};

struct TextureEntry
{
    std::string name;
    Texture *texture = nullptr;
};

struct BspVertex
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec2 uv = glm::vec2(0.0f);
    glm::vec2 lmUv = glm::vec2(0.0f);
};

struct Face
{
    int textureIdx = -1;
    int effectIdx = -1;
    int faceType = 0;
    int firstVert = 0;
    int numVerts = 0;
    int firstMeshVert = 0;
    int numMeshVerts = 0;
    int lmIdx = -1;
    int patchW = 0;
    int patchH = 0;
};

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

bool getLumpInfo(const std::vector<Lump> &lumps,
                 int idx,
                 size_t elemSize,
                 size_t fileSize,
                 size_t &base,
                 size_t &count)
{
    if (idx < 0 || idx >= static_cast<int>(lumps.size()))
        return false;

    const Lump &l = lumps[idx];
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

    base = begin;
    count = (elemSize > 0) ? (len / elemSize) : 0;
    return true;
}

Vertex makeVertex(const BspVertex &src)
{
    Vertex dst{};
    dst.position = src.position;
    dst.normal = (glm::dot(src.normal, src.normal) > kEpsilon) ? glm::normalize(src.normal) : glm::vec3(0.0f, 1.0f, 0.0f);
    dst.uv = src.uv;
    dst.tangent = glm::vec4(src.lmUv.x, src.lmUv.y, 0.0f, 1.0f);
    return dst;
}

void patchBasis(float t, float &b0, float &b1, float &b2)
{
    const float it = 1.0f - t;
    b0 = it * it;
    b1 = 2.0f * t * it;
    b2 = t * t;
}

float evalScalar(float p00, float p10, float p20,
                 float p01, float p11, float p21,
                 float p02, float p12, float p22,
                 float bu0, float bu1, float bu2,
                 float bv0, float bv1, float bv2)
{
    const float r0 = p00 * bu0 + p10 * bu1 + p20 * bu2;
    const float r1 = p01 * bu0 + p11 * bu1 + p21 * bu2;
    const float r2 = p02 * bu0 + p12 * bu1 + p22 * bu2;
    return r0 * bv0 + r1 * bv1 + r2 * bv2;
}

BspVertex evalPatchVertex(const BspVertex &c00, const BspVertex &c10, const BspVertex &c20,
                          const BspVertex &c01, const BspVertex &c11, const BspVertex &c21,
                          const BspVertex &c02, const BspVertex &c12, const BspVertex &c22,
                          float u, float v)
{
    float bu0 = 0.0f, bu1 = 0.0f, bu2 = 0.0f;
    float bv0 = 0.0f, bv1 = 0.0f, bv2 = 0.0f;
    patchBasis(u, bu0, bu1, bu2);
    patchBasis(v, bv0, bv1, bv2);

    BspVertex out{};
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
        out.normal = glm::vec3(0.0f, 1.0f, 0.0f);

    return out;
}
}

Mesh *MeshManager::load_bsp(const std::string &name,
                            const std::string &path,
                            const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[BSP] Failed to read %s", path.c_str());
        return nullptr;
    }

    if (bytes.size() < 8u + static_cast<size_t>(kNumLumps) * 8u)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[BSP] File too small: %s", path.c_str());
        return nullptr;
    }

    const int32_t ident = readI32(bytes, 0);
    const int32_t version = readI32(bytes, 4);
    if (ident != kQ3Ident || version != kQ3Version)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[BSP] Invalid header in %s", path.c_str());
        return nullptr;
    }

    std::vector<Lump> lumps(kNumLumps);
    size_t off = 8;
    for (int i = 0; i < kNumLumps; ++i)
    {
        lumps[i].offset = readI32(bytes, off + 0);
        lumps[i].length = readI32(bytes, off + 4);
        off += 8;
    }

    std::vector<TextureEntry> textures;
    {
        size_t base = 0, count = 0;
        if (!getLumpInfo(lumps, kLumpTextures, kTextureSize, bytes.size(), base, count))
            return nullptr;
        textures.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            const size_t o = base + i * kTextureSize;
            TextureEntry t;
            t.name = readFixedString(bytes, o + 0, 64);
            textures.push_back(std::move(t));
        }
    }

    std::vector<BspVertex> vertices;
    {
        size_t base = 0, count = 0;
        if (!getLumpInfo(lumps, kLumpVertices, kVertexSize, bytes.size(), base, count))
            return nullptr;
        vertices.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            const size_t o = base + i * kVertexSize;
            BspVertex v;
            const float x = readF32(bytes, o + 0);
            const float y = readF32(bytes, o + 4);
            const float z = readF32(bytes, o + 8);
            const float nx = readF32(bytes, o + 28);
            const float ny = readF32(bytes, o + 32);
            const float nz = readF32(bytes, o + 36);
            v.position = glm::vec3(x, z, y);
            v.normal = glm::vec3(nx, nz, ny);
            v.uv = glm::vec2(readF32(bytes, o + 12), readF32(bytes, o + 16));
            v.lmUv = glm::vec2(readF32(bytes, o + 20), readF32(bytes, o + 24));
            vertices.push_back(v);
        }
    }

    std::vector<int32_t> meshVerts;
    {
        size_t base = 0, count = 0;
        if (!getLumpInfo(lumps, kLumpMeshVerts, sizeof(int32_t), bytes.size(), base, count))
            return nullptr;
        meshVerts.reserve(count);
        for (size_t i = 0; i < count; ++i)
            meshVerts.push_back(readI32(bytes, base + i * sizeof(int32_t)));
    }

    std::vector<Face> faces;
    {
        size_t base = 0, count = 0;
        if (!getLumpInfo(lumps, kLumpFaces, kFaceSize, bytes.size(), base, count))
            return nullptr;
        faces.reserve(count);
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
            faces.push_back(f);
        }
    }

    std::vector<Texture *> lightmaps;
    {
        size_t base = 0, count = 0;
        if (!getLumpInfo(lumps, kLumpLightmaps, kLightmapBytes, bytes.size(), base, count))
            return nullptr;
        lightmaps.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            const size_t o = base + i * kLightmapBytes;
            std::vector<uint8_t> rgba(128u * 128u * 4u, 255u);
            for (size_t px = 0; px < 128u * 128u; ++px)
            {
                const size_t src = o + px * 3u;
                const size_t dst = px * 4u;
                rgba[dst + 0] = bytes[src + 0];
                rgba[dst + 1] = bytes[src + 1];
                rgba[dst + 2] = bytes[src + 2];
            }

            const std::string texName = name + "::lightmap_" + std::to_string(i);
            Texture *t = TextureManager::instance().createFromMemory(
                texName, 128, 128, PixelType::RGBA, rgba.data(), rgba.size());
            if (t)
            {
                glBindTexture(GL_TEXTURE_2D, t->id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            lightmaps.push_back(t);
        }
    }

    const std::string baseDir = texture_dir.empty() ? std::string() : texture_dir;
    for (TextureEntry &tx : textures)
    {
        const std::string texPath = ResolveTexturePath(baseDir, tx.name);
        if (!texPath.empty())
            tx.texture = TextureManager::instance().load(name + "::" + tx.name, texPath);
    }

    auto *mesh = new Mesh();
    mesh->name = name;

    std::unordered_map<GroupKey, std::vector<int>, GroupKeyHash> groups;
    std::vector<GroupKey> groupOrder;
    for (int fi = 0; fi < (int)faces.size(); ++fi)
    {
        const Face &f = faces[fi];
        const bool supported =
            ((f.faceType == kFacePolygon || f.faceType == kFaceMesh) && f.numMeshVerts > 0) ||
            (f.faceType == kFacePatch && f.patchW >= 3 && f.patchH >= 3);
        if (!supported)
            continue;

        GroupKey key{f.textureIdx, f.lmIdx};
        if (groups.find(key) == groups.end())
            groupOrder.push_back(key);
        groups[key].push_back(fi);
    }

    Texture *white = TextureManager::instance().getWhite();
    const int row = kPatchTess + 1;

    for (const GroupKey &key : groupOrder)
    {
        const uint32_t surfaceStart = (uint32_t)mesh->buffer.indices.size();

        for (int fi : groups[key])
        {
            const Face &f = faces[fi];
            if (f.faceType == kFacePolygon || f.faceType == kFaceMesh)
            {
                for (int t = 0; t < f.numMeshVerts; ++t)
                {
                    const int mvi = f.firstMeshVert + t;
                    if (mvi < 0 || mvi >= (int)meshVerts.size())
                        continue;

                    const int mv = meshVerts[mvi];
                    const int vIdx = f.firstVert + mv;
                    if (vIdx < 0 || vIdx >= (int)vertices.size())
                        continue;

                    mesh->buffer.vertices.push_back(makeVertex(vertices[vIdx]));
                    mesh->buffer.indices.push_back((uint32_t)mesh->buffer.vertices.size() - 1u);
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

                        if (i00 < 0 || i22 >= (int)vertices.size())
                            continue;

                        const BspVertex &c00 = vertices[i00];
                        const BspVertex &c10 = vertices[i10];
                        const BspVertex &c20 = vertices[i20];
                        const BspVertex &c01 = vertices[i01];
                        const BspVertex &c11 = vertices[i11];
                        const BspVertex &c21 = vertices[i21];
                        const BspVertex &c02 = vertices[i02];
                        const BspVertex &c12 = vertices[i12];
                        const BspVertex &c22 = vertices[i22];

                        const uint32_t baseVert = (uint32_t)mesh->buffer.vertices.size();
                        for (int iy = 0; iy <= kPatchTess; ++iy)
                        {
                            const float fv = float(iy) / float(kPatchTess);
                            for (int ix = 0; ix <= kPatchTess; ++ix)
                            {
                                const float fu = float(ix) / float(kPatchTess);
                                mesh->buffer.vertices.push_back(
                                    makeVertex(evalPatchVertex(c00, c10, c20,
                                                               c01, c11, c21,
                                                               c02, c12, c22,
                                                               fu, fv)));
                            }
                        }

                        for (int iy = 0; iy < kPatchTess; ++iy)
                        {
                            for (int ix = 0; ix < kPatchTess; ++ix)
                            {
                                const uint32_t i0 = baseVert + (uint32_t)(iy * row + ix);
                                const uint32_t i1 = i0 + 1u;
                                const uint32_t i2 = i0 + (uint32_t)row;
                                const uint32_t i3 = i2 + 1u;
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

        const uint32_t surfaceCount = (uint32_t)mesh->buffer.indices.size() - surfaceStart;
        if (surfaceCount == 0)
            continue;

        Material *mat = new Material();
        mat->name = name + "::surf_" + std::to_string(mesh->surfaces.size());
        mat->type = MaterialType::Custom;
        mat->setVec4("u_color", glm::vec4(1.0f));

        Texture *albedo = (key.tex >= 0 && key.tex < (int)textures.size() && textures[key.tex].texture)
            ? textures[key.tex].texture : white;
        Texture *lm = (key.lm >= 0 && key.lm < (int)lightmaps.size() && lightmaps[key.lm])
            ? lightmaps[key.lm] : white;

        mat->setTexture("u_albedo", albedo);
        mat->setTexture("u_lightmap", lm);
        mat->setInt("u_useLightmap", (key.lm >= 0 && key.lm < (int)lightmaps.size()) ? 1 : 0);

        const int matIndex = mesh->add_material(mat);
        mesh->add_surface(surfaceStart, surfaceCount, matIndex);
    }

    if (mesh->buffer.indices.empty() || mesh->buffer.vertices.empty())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[BSP] No supported geometry in %s", path.c_str());
        delete mesh;
        return nullptr;
    }

    mesh->upload();
    cache[name] = mesh;
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[BSP] Loaded '%s': verts=%zu tris=%zu surfaces=%zu lightmaps=%zu",
                path.c_str(),
                mesh->buffer.vertices.size(),
                mesh->buffer.indices.size() / 3u,
                mesh->surfaces.size(),
                lightmaps.size());
    return mesh;
}
