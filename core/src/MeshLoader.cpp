#include "MeshLoader.hpp"
#include "Manager.hpp"
#include "Utils.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

namespace
{
bool isBufferSubChunk(uint32_t id)
{
    return id == CHUNK_VRTS || id == CHUNK_IDXS || id == CHUNK_SKIN || id == CHUNK_SURF;
}

std::string serializedTextureRef(const Texture *tex)
{
    if (!tex)
        return std::string();
    return tex->name;
}

bool isAbsolutePath(const std::string &path)
{
    return !path.empty() &&
           (path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':'));
}

std::string resolveSerializedTexturePath(const std::string &textureRef,
                                        const std::string &textureDir)
{
    if (textureRef.empty() || isAbsolutePath(textureRef))
        return textureRef;
    if (textureDir.empty())
        return textureRef;

    std::string resolved = ResolveTexturePath(textureDir, textureRef);
    if (!resolved.empty())
        return resolved;

    // Some exported assets reference legacy ".dds" names while files on disk are png/jpg.
    const std::string stem = PathStem(textureRef);
    if (!stem.empty() && stem != textureRef)
    {
        resolved = ResolveTexturePath(textureDir, stem);
        if (!resolved.empty())
            return resolved;
    }

    return textureRef;
}
}


// ============================================================
//  MeshWriter helpers
// ============================================================
void MeshWriter::beginChunk(uint32_t id, Sint64 *startOut)
{
    s_->writeU32(id);
    s_->writeU32(0);         // placeholder length
    *startOut = s_->tell();
}

void MeshWriter::endChunk(Sint64 start)
{
    Sint64 cur = s_->tell();
    uint32_t len = (uint32_t)(cur - start);
    s_->seek(start - 4);
    s_->writeU32(len);
    s_->seek(cur);
}

void MeshWriter::writeMaterials(const std::vector<Material *> &mats)
{
    Sint64 pos;
    beginChunk(CHUNK_MATS, &pos);

    s_->writeU32((uint32_t)mats.size());
    for (auto *mat : mats)
    {
        s_->writeStr(mat ? mat->name : "");

        glm::vec3 col = mat ? mat->getVec3("u_color", glm::vec3(0.8f)) : glm::vec3(0.8f);
        s_->writeF32(col.r); s_->writeF32(col.g); s_->writeF32(col.b);

        Texture *tex = mat ? mat->getTexture("u_albedo") : nullptr;
        s_->writeStr(serializedTextureRef(tex));
    }

    endChunk(pos);
}

void MeshWriter::writeSkeleton(const std::vector<Bone> &bones)
{
    Sint64 pos;
    beginChunk(CHUNK_SKEL, &pos);

    s_->writeU32((uint32_t)bones.size());
    for (const auto &b : bones)
    {
        s_->writeStr(b.name);
        s_->writeI32(b.parent);
        const float *local = glm::value_ptr(b.localPose);
        for (int i = 0; i < 16; i++) s_->writeF32(local[i]);
        const float *m = glm::value_ptr(b.offset);
        for (int i = 0; i < 16; i++) s_->writeF32(m[i]);
    }

    endChunk(pos);
}

void MeshWriter::writeSurfaces(const std::vector<Surface> &surfs)
{
    Sint64 pos;
    beginChunk(CHUNK_SURF, &pos);

    s_->writeU32((uint32_t)surfs.size());
    for (const auto &s : surfs)
    {
        s_->writeU32(s.index_start);
        s_->writeU32(s.index_count);
        s_->writeI32(s.material_index);
    }

    endChunk(pos);
}

// ── static buffer ─────────────────────────────────────────────
void MeshWriter::writeBuffer(const MeshBuffer &buf,
                              const std::vector<Surface> &surfs)
{
    Sint64 pos;
    beginChunk(CHUNK_BUFF, &pos);

    uint32_t flags = BUFFER_FLAG_TANGENTS;
    s_->writeU32(flags);

    // VRTS
    {
        Sint64 vp;
        beginChunk(CHUNK_VRTS, &vp);
        s_->writeU32((uint32_t)buf.vertices.size());
        for (const auto &v : buf.vertices)
        {
            s_->writeF32(v.position.x); s_->writeF32(v.position.y); s_->writeF32(v.position.z);
            s_->writeF32(v.normal.x);   s_->writeF32(v.normal.y);   s_->writeF32(v.normal.z);
            s_->writeF32(v.tangent.x);  s_->writeF32(v.tangent.y);
            s_->writeF32(v.tangent.z);  s_->writeF32(v.tangent.w);
            s_->writeF32(v.uv.x);       s_->writeF32(v.uv.y);
        }
        endChunk(vp);
    }

    // IDXS
    {
        Sint64 ip;
        beginChunk(CHUNK_IDXS, &ip);
        s_->writeU32((uint32_t)buf.indices.size());
        for (auto idx : buf.indices) s_->writeU32(idx);
        endChunk(ip);
    }

    writeSurfaces(surfs);
    endChunk(pos);
}

// ── animated buffer ───────────────────────────────────────────
void MeshWriter::writeBuffer(const AnimatedMeshBuffer &buf,
                              const std::vector<Surface> &surfs)
{
    Sint64 pos;
    beginChunk(CHUNK_BUFF, &pos);

    uint32_t flags = BUFFER_FLAG_SKINNED | BUFFER_FLAG_TANGENTS;
    s_->writeU32(flags);

    // VRTS
    {
        Sint64 vp;
        beginChunk(CHUNK_VRTS, &vp);
        s_->writeU32((uint32_t)buf.vertices.size());
        for (const auto &v : buf.vertices)
        {
            s_->writeF32(v.position.x); s_->writeF32(v.position.y); s_->writeF32(v.position.z);
            s_->writeF32(v.normal.x);   s_->writeF32(v.normal.y);   s_->writeF32(v.normal.z);
            s_->writeF32(v.tangent.x);  s_->writeF32(v.tangent.y);
            s_->writeF32(v.tangent.z);  s_->writeF32(v.tangent.w);
            s_->writeF32(v.uv.x);       s_->writeF32( v.uv.y);
        }
        endChunk(vp);
    }

    // IDXS
    {
        Sint64 ip;
        beginChunk(CHUNK_IDXS, &ip);
        s_->writeU32((uint32_t)buf.indices.size());
        for (auto idx : buf.indices) s_->writeU32(idx);
        endChunk(ip);
    }

    // SKIN
    {
        Sint64 sp;
        beginChunk(CHUNK_SKIN, &sp);
        s_->writeU32((uint32_t)buf.vertices.size());
        for (const auto &v : buf.vertices)
        {
            s_->writeU8((uint8_t)v.boneIds.x);
            s_->writeU8((uint8_t)v.boneIds.y);
            s_->writeU8((uint8_t)v.boneIds.z);
            s_->writeU8((uint8_t)v.boneIds.w);
            s_->writeF32(v.boneWeights.x);
            s_->writeF32(v.boneWeights.y);
            s_->writeF32(v.boneWeights.z);
            s_->writeF32(v.boneWeights.w);
        }
        endChunk(sp);
    }

    writeSurfaces(surfs);
    endChunk(pos);
}

// ============================================================
//  MeshWriter::save
// ============================================================
bool MeshWriter::save(const Mesh *mesh, const std::string &path)
{
    BinaryStream stream(path, "wb");
    if (!stream.isOpen()) return false;
    s_ = &stream;

    s_->writeU32(MESH_MAGIC);
    s_->writeU32(MESH_VERSION);

    writeMaterials(mesh->materials);
    writeBuffer(mesh->buffer, mesh->surfaces);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[MeshWriter] Saved '%s'  verts=%zu  surfs=%zu  mats=%zu",
                path.c_str(),
                mesh->buffer.vertices.size(),
                mesh->surfaces.size(),
                mesh->materials.size());
    return true;
}

bool MeshWriter::save(const AnimatedMesh *mesh, const std::string &path)
{
    BinaryStream stream(path, "wb");
    if (!stream.isOpen()) return false;
    s_ = &stream;

    s_->writeU32(MESH_MAGIC);
    s_->writeU32(MESH_VERSION);

    writeMaterials(mesh->materials);
    writeSkeleton(mesh->bones);
    writeBuffer(mesh->buffer, mesh->surfaces);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[MeshWriter] Saved animated '%s'  verts=%zu  bones=%zu  surfs=%zu",
                path.c_str(),
                mesh->buffer.vertices.size(),
                mesh->bones.size(),
                mesh->surfaces.size());
    return true;
}

// ============================================================
//  MeshReader helpers
// ============================================================
ChunkHeader MeshReader::readHeader()
{
    ChunkHeader h;
    h.id     = s_->readU32();
    h.length = s_->readU32();
    return h;
}

void MeshReader::skipChunk(const ChunkHeader &h)
{
    s_->seek(s_->tell() + h.length);
}

void MeshReader::readMaterials(const ChunkHeader &h,
                                std::vector<Material *> &mats)
{
    Sint64 end = s_->tell() + h.length;
    uint32_t count = s_->readU32();
    auto &texMgr = TextureManager::instance();

    for (uint32_t i = 0; i < count; i++)
    {
        std::string name = s_->readStr();

        // diffuse
        glm::vec3 col;
        col.r = s_->readF32(); col.g = s_->readF32(); col.b = s_->readF32();
        std::string textureRef;

        if (version_ >= 101)
        {
            textureRef = s_->readStr();
        }
        else
        {
            // Legacy exporter layout:
            // diffuse rgb, specular rgb, shininess, textureCount, texture paths...
            s_->readF32(); s_->readF32(); s_->readF32();
            s_->readF32();

            const uint8_t textureCount = s_->readU8();
            for (uint8_t texIdx = 0; texIdx < textureCount; ++texIdx)
            {
                std::string candidate = s_->readStr();
                if (texIdx == 0)
                    textureRef = candidate;
            }
        }

        std::string key = name.empty() ? ("__mat_" + std::to_string(i)) : name;
        Material *mat = new Material();
        mat->name = key;
        mat->setVec3("u_color", col);

        if (!textureRef.empty())
        {
            const std::string resolvedPath = resolveSerializedTexturePath(textureRef, textureDir);
            Texture *tex = texMgr.load(textureRef, resolvedPath);

            if (tex)
            {
                mat->setTexture("u_albedo", tex);
            }
            else
            {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "[MeshReader] Texture not found for material '%s': %s",
                            key.c_str(),
                            textureRef.c_str());
            }
        }

        mats.push_back(mat);
    }

    if (s_->tell() < end)
        s_->seek(end);
}

void MeshReader::readSkeleton(const ChunkHeader &h, std::vector<Bone> &bones)
{
    Sint64 end = s_->tell() + h.length;
    uint32_t count = s_->readU32();
    bones.resize(count);

    const Sint64 bytesRemaining = end - s_->tell();
    const Sint64 legacyStride = 4 + 64;
    const bool hasLocalPoseInChunk =
        count > 0 &&
        bytesRemaining >= count * legacyStride &&
        (bytesRemaining - (count * legacyStride)) >= (count * 64);

    for (uint32_t i = 0; i < count; i++)
    {
        bones[i].name   = s_->readStr();
        bones[i].parent = s_->readI32();

        float m[16];
        if (version_ >= 101 || hasLocalPoseInChunk)
        {
            float lm[16];
            for (int j = 0; j < 16; j++) lm[j] = s_->readF32();
            bones[i].localPose = glm::make_mat4(lm);

            for (int j = 0; j < 16; j++) m[j] = s_->readF32();
            bones[i].offset = glm::make_mat4(m);
        }
        else
        {
            for (int j = 0; j < 16; j++) m[j] = s_->readF32();
            bones[i].offset = glm::make_mat4(m);
            bones[i].localPose = glm::mat4(1.f);
        }
    }

    if (s_->tell() < end)
        s_->seek(end);
}

void MeshReader::readSurfaces(const ChunkHeader &h, std::vector<Surface> &surfs)
{
    uint32_t count = s_->readU32();
    surfs.resize(count);
    for (uint32_t i = 0; i < count; i++)
    {
        surfs[i].index_start    = s_->readU32();
        surfs[i].index_count    = s_->readU32();
        surfs[i].material_index = s_->readI32();
    }
}

// ── static buffer ─────────────────────────────────────────────
void MeshReader::readBuffer(const ChunkHeader &h, Mesh *mesh)
{
    Sint64   end            = s_->tell() + h.length;
    uint32_t firstWord      = s_->readU32();
    uint32_t secondWord     = (s_->tell() + 4 <= end) ? s_->readU32() : 0;
    uint32_t materialIndex  = 0;
    uint32_t flags          = firstWord;
    bool     hasTangents    = (flags & BUFFER_FLAG_TANGENTS) != 0;
    bool     hasLightmapUV  = (flags & BUFFER_FLAG_LIGHTMAP) != 0;
    bool     hasSurfaceChunk = false;

    if (isBufferSubChunk(secondWord))
    {
        s_->seek(s_->tell() - 4);
    }
    else
    {
        materialIndex = firstWord;
        flags = secondWord;
        hasTangents = (flags & BUFFER_FLAG_TANGENTS) != 0;
        hasLightmapUV = (flags & BUFFER_FLAG_LIGHTMAP) != 0;
    }

    uint32_t vertexBase  = (uint32_t)mesh->buffer.vertices.size();
    uint32_t indexStart  = (uint32_t)mesh->buffer.indices.size();
    uint32_t indexCount  = 0;

    while (s_->tell() + 8 <= end)
    {
        auto sub    = readHeader();
        Sint64 subEnd = s_->tell() + sub.length;

        if (sub.id == CHUNK_VRTS)
        {
            uint32_t count = s_->readU32();
            mesh->buffer.vertices.reserve(vertexBase + count);
            for (uint32_t i = 0; i < count; i++)
            {
                Vertex v{};
                v.position.x = s_->readF32(); v.position.y = s_->readF32(); v.position.z = s_->readF32();
                v.normal.x   = s_->readF32(); v.normal.y   = s_->readF32(); v.normal.z   = s_->readF32();
                if (hasTangents) {
                    v.tangent.x = s_->readF32(); v.tangent.y = s_->readF32();
                    v.tangent.z = s_->readF32(); v.tangent.w = s_->readF32();
                }
                v.uv.x = s_->readF32(); v.uv.y = s_->readF32();
                if (hasLightmapUV) {
                    v.tangent.x = s_->readF32(); // lightmap U
                    v.tangent.y = s_->readF32(); // lightmap V
                }
                mesh->buffer.vertices.push_back(v);
            }
        }
        else if (sub.id == CHUNK_IDXS)
        {
            uint32_t count = s_->readU32();
            indexCount = count;
            mesh->buffer.indices.reserve(indexStart + count);
            for (uint32_t i = 0; i < count; i++)
                mesh->buffer.indices.push_back(s_->readU32() + vertexBase);
        }
        else if (sub.id == CHUNK_SURF)
        {
            hasSurfaceChunk = true;
            const size_t baseSurfaceCount = mesh->surfaces.size();
            readSurfaces(sub, mesh->surfaces);
            for (size_t i = baseSurfaceCount; i < mesh->surfaces.size(); ++i)
            {
                mesh->surfaces[i].index_start += indexStart;
            }
        }
        else skipChunk(sub);

        if (s_->tell() < subEnd) s_->seek(subEnd);
    }

    if (!hasSurfaceChunk && indexCount > 0)
        mesh->add_surface(indexStart, indexCount, (int)materialIndex);
}

// ── animated buffer ───────────────────────────────────────────
void MeshReader::readBuffer(const ChunkHeader &h, AnimatedMesh *mesh)
{
    Sint64   end           = s_->tell() + h.length;
    uint32_t firstWord     = s_->readU32();
    uint32_t secondWord    = (s_->tell() + 4 <= end) ? s_->readU32() : 0;
    uint32_t materialIndex = 0;
    uint32_t flags         = firstWord;
    bool     skinned       = (flags & BUFFER_FLAG_SKINNED)  != 0;
    bool     hasTangents   = (flags & BUFFER_FLAG_TANGENTS) != 0;
    bool     hasSurfaceChunk = false;

    if (isBufferSubChunk(secondWord))
    {
        s_->seek(s_->tell() - 4);
    }
    else
    {
        materialIndex = firstWord;
        flags = secondWord;
        skinned = (flags & BUFFER_FLAG_SKINNED) != 0;
        hasTangents = (flags & BUFFER_FLAG_TANGENTS) != 0;
    }

    uint32_t vertexBase = (uint32_t)mesh->buffer.vertices.size();
    uint32_t indexStart = (uint32_t)mesh->buffer.indices.size();
    uint32_t indexCount = 0;

    while (s_->tell() + 8 <= end)
    {
        auto   sub    = readHeader();
        Sint64 subEnd = s_->tell() + sub.length;

        if (sub.id == CHUNK_VRTS)
        {
            uint32_t count = s_->readU32();
            mesh->buffer.vertices.reserve(vertexBase + count);
            for (uint32_t i = 0; i < count; i++)
            {
                AnimatedVertex v{};
                v.position.x = s_->readF32(); v.position.y = s_->readF32(); v.position.z = s_->readF32();
                v.normal.x   = s_->readF32(); v.normal.y   = s_->readF32(); v.normal.z   = s_->readF32();
                if (hasTangents) {
                    v.tangent.x = s_->readF32(); v.tangent.y = s_->readF32();
                    v.tangent.z = s_->readF32(); v.tangent.w = s_->readF32();
                }
                v.uv.x = s_->readF32(); v.uv.y = s_->readF32();
                mesh->buffer.vertices.push_back(v);
            }
        }
        else if (sub.id == CHUNK_IDXS)
        {
            uint32_t count = s_->readU32();
            indexCount = count;
            mesh->buffer.indices.reserve(indexStart + count);
            for (uint32_t i = 0; i < count; i++)
                mesh->buffer.indices.push_back(s_->readU32() + vertexBase);
        }
        else if (sub.id == CHUNK_SKIN && skinned)
        {
            uint32_t count = s_->readU32();
            for (uint32_t i = 0; i < count; i++)
            {
                uint32_t vi = vertexBase + i;
                if (vi >= mesh->buffer.vertices.size()) break;
                auto &v = mesh->buffer.vertices[vi];
                v.boneIds.x = s_->readU8(); v.boneIds.y = s_->readU8();
                v.boneIds.z = s_->readU8(); v.boneIds.w = s_->readU8();
                v.boneWeights.x = s_->readF32(); v.boneWeights.y = s_->readF32();
                v.boneWeights.z = s_->readF32(); v.boneWeights.w = s_->readF32();
            }
        }
        else if (sub.id == CHUNK_SURF)
        {
            hasSurfaceChunk = true;
            const size_t baseSurfaceCount = mesh->surfaces.size();
            readSurfaces(sub, mesh->surfaces);
            for (size_t i = baseSurfaceCount; i < mesh->surfaces.size(); ++i)
            {
                mesh->surfaces[i].index_start += indexStart;
            }
        }
        else skipChunk(sub);

        if (s_->tell() < subEnd) s_->seek(subEnd);
    }

    if (!hasSurfaceChunk && indexCount > 0)
        mesh->add_surface(indexStart, indexCount, (int)materialIndex);
}

// ============================================================
//  MeshReader::load
// ============================================================
bool MeshReader::load(const std::string &path, Mesh *mesh)
{
    BinaryStream stream(path, "rb");
    if (!stream.isOpen()) return false;
    s_ = &stream;

    if (s_->readU32() != MESH_MAGIC)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[MeshReader] Invalid magic: %s", path.c_str());
        return false;
    }
    version_ = s_->readU32();
    if (version_ > MESH_VERSION)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[MeshReader] Newer version %u in: %s", version_, path.c_str());

    Sint64 fileSize = s_->size();
    while (s_->tell() + 8 <= fileSize)
    {
        auto   h   = readHeader();
        Sint64 end = s_->tell() + h.length;

        if      (h.id == CHUNK_MATS) readMaterials(h, mesh->materials);
        else if (h.id == CHUNK_BUFF) readBuffer(h, mesh);
        else if (h.id == CHUNK_LMAP)
        {
            mesh->lightmap.width    = s_->readI32();
            mesh->lightmap.height   = s_->readI32();
            mesh->lightmap.channels = s_->readI32();
            uint32_t dataSize = mesh->lightmap.width * mesh->lightmap.height * mesh->lightmap.channels;
            mesh->lightmap.pixels.resize(dataSize);
            s_->readRaw(mesh->lightmap.pixels.data(), dataSize);
        }
        else                         skipChunk(h);

        if (s_->tell() < end) s_->seek(end);
    }

    mesh->upload();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[MeshReader] '%s'  verts=%zu  idx=%zu  surfs=%zu  mats=%zu",
                path.c_str(),
                mesh->buffer.vertices.size(),
                mesh->buffer.indices.size(),
                mesh->surfaces.size(),
                mesh->materials.size());
    return true;
}

bool MeshReader::load(const std::string &path, AnimatedMesh *mesh)
{
    BinaryStream stream(path, "rb");
    if (!stream.isOpen()) return false;
    s_ = &stream;

    if (s_->readU32() != MESH_MAGIC)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[MeshReader] Invalid magic: %s", path.c_str());
        return false;
    }
    version_ = s_->readU32();

    Sint64 fileSize = s_->size();
    while (s_->tell() + 8 <= fileSize)
    {
        auto   h   = readHeader();
        Sint64 end = s_->tell() + h.length;

        if      (h.id == CHUNK_MATS) readMaterials(h, mesh->materials);
        else if (h.id == CHUNK_SKEL) readSkeleton (h, mesh->bones);
        else if (h.id == CHUNK_BUFF) readBuffer   (h, mesh);
        else                         skipChunk(h);

        if (s_->tell() < end) s_->seek(end);
    }

    mesh->finalMatrices.resize(mesh->bones.size(), glm::mat4(1.f));
    mesh->upload();

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[MeshReader] Animated '%s'  verts=%zu  bones=%zu  surfs=%zu",
                path.c_str(),
                mesh->buffer.vertices.size(),
                mesh->bones.size(),
                mesh->surfaces.size());
    return true;
}
 
// ============================================================
//  AnimWriter helpers
// ============================================================
void AnimWriter::beginChunk(uint32_t id, Sint64 *startOut)
{
    s_->writeU32(id);
    s_->writeU32(0);        // placeholder
    *startOut = s_->tell();
}

void AnimWriter::endChunk(Sint64 start)
{
    Sint64 cur = s_->tell();
    uint32_t len = (uint32_t)(cur - start);
    s_->seek(start - 4);
    s_->writeU32(len);
    s_->seek(cur);
}

// ============================================================
//  AnimWriter::save
//
//  Formato (compatível com o original):
//  [ANIM_MAGIC] [ANIM_VERSION]
//  [ANIM_CHUNK_INFO]
//    name(64) duration(f32) tps(f32) numChannels(u32)
//  [ANIM_CHUNK_CHAN] × N
//    boneName(cstr)   ← sem boneIndex, sem sub-chunk KEYS
//    numKeys(u32)
//    per key: time(f32) px py pz  rx ry rz rw  sx sy sz
// ============================================================
bool AnimWriter::save(const Animation *anim, const std::string &path)
{
    BinaryStream stream(path, "wb");
    if (!stream.isOpen())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[AnimWriter] Cannot open: %s", path.c_str());
        return false;
    }
    s_ = &stream;

    s_->writeU32(ANIM_MAGIC);
    s_->writeU32(ANIM_VERSION);

    // ── INFO ─────────────────────────────────────────────────
    {
        Sint64 pos;
        beginChunk(ANIM_CHUNK_INFO, &pos);

        char name[64] = {};
        strncpy(name, anim->name.c_str(), 63);
        s_->writeRaw(name, 64);

        s_->writeF32(anim->duration);
        s_->writeF32(anim->ticksPerSecond);
        s_->writeU32((uint32_t)anim->channels.size());

        endChunk(pos);
    }

    // ── CHAN × N ─────────────────────────────────────────────
    for (const auto &ch : anim->channels)
    {
        Sint64 chanPos;
        beginChunk(ANIM_CHUNK_CHAN, &chanPos);

        s_->writeStr(ch.boneName);   // null-terminated, sem boneIndex

        // Número de keys — usa o maior dos três vectores
        uint32_t numKeys = (uint32_t)std::max({
            ch.posKeys.size(),
            ch.rotKeys.size(),
            ch.scaleKeys.size()
        });

        s_->writeU32(numKeys);

        for (uint32_t i = 0; i < numKeys; i++)
        {
            float t = 0.f;
            if (i < ch.posKeys.size())      t = ch.posKeys[i].time;
            else if (i < ch.rotKeys.size()) t = ch.rotKeys[i].time;

            glm::vec3 pos   = (i < ch.posKeys.size())   ? ch.posKeys[i].value   : glm::vec3(0.f);
            glm::quat rot   = (i < ch.rotKeys.size())   ? ch.rotKeys[i].value   : glm::quat(1,0,0,0);
            glm::vec3 scale = (i < ch.scaleKeys.size()) ? ch.scaleKeys[i].value : glm::vec3(1.f);

            s_->writeF32(t);
            s_->writeF32(pos.x);   s_->writeF32(pos.y);   s_->writeF32(pos.z);
            s_->writeF32(rot.x);   s_->writeF32(rot.y);   s_->writeF32(rot.z);   s_->writeF32(rot.w);
            s_->writeF32(scale.x); s_->writeF32(scale.y); s_->writeF32(scale.z);
        }

        endChunk(chanPos);
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[AnimWriter] Saved '%s'  channels=%zu  duration=%.2f  tps=%.1f",
                path.c_str(),
                anim->channels.size(),
                anim->duration,
                anim->ticksPerSecond);
    return true;
}

// ============================================================
//  AnimReader::load
//
//  Formato original:
//  [ANIM_MAGIC] [ANIM_VERSION]
//  [ANIM_CHUNK_INFO]  id+len  name(64) duration(f32) tps(f32) numChannels(u32)
//  [ANIM_CHUNK_CHAN]  id+len  boneName(cstr) numKeys(u32)
//                             per key: time(f32) px py pz  rx ry rz rw  sx sy sz
// ============================================================
bool AnimReader::load(const std::string &path, Animation *anim)
{
    BinaryStream stream(path, "rb");
    if (!stream.isOpen())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[AnimReader] Cannot open: %s", path.c_str());
        return false;
    }
    s_ = &stream;

    uint32_t magic = s_->readU32();
    if (magic != ANIM_MAGIC)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[AnimReader] Invalid magic: %s", path.c_str());
        return false;
    }
    uint32_t ver = s_->readU32();
    if (ver > ANIM_VERSION)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[AnimReader] Newer version %u: %s", ver, path.c_str());

    Sint64 fileSize = s_->size();

    while (s_->tell() + 8 <= fileSize)
    {
        uint32_t chunkId  = s_->readU32();
        uint32_t chunkLen = s_->readU32();
        Sint64   chunkEnd = s_->tell() + chunkLen;

        if (chunkId == ANIM_CHUNK_INFO)
        {
            char name[64] = {};
            s_->readRaw(name, 64);
            name[63]             = '\0';
            anim->name           = name;
            anim->duration       = s_->readF32();
            anim->ticksPerSecond = s_->readF32();
            uint32_t numCh       = s_->readU32();
            anim->channels.reserve(numCh);
        }
        else if (chunkId == ANIM_CHUNK_CHAN)
        {
            AnimationChannel ch;
            ch.boneName  = s_->readStr();   // null-terminated
            // NO boneIndex in original format — resolved later via bind()

            uint32_t numKeys = s_->readU32();
            ch.posKeys.reserve(numKeys);
            ch.rotKeys.reserve(numKeys);
            ch.scaleKeys.reserve(numKeys);

            for (uint32_t i = 0; i < numKeys; i++)
            {
                float t = s_->readF32();

                PosKey pk;   pk.time    = t;
                pk.value.x = s_->readF32(); pk.value.y = s_->readF32(); pk.value.z = s_->readF32();

                RotKey rk;   rk.time    = t;
                rk.value.x = s_->readF32(); rk.value.y = s_->readF32();
                rk.value.z = s_->readF32(); rk.value.w = s_->readF32();
                rk.value   = glm::normalize(rk.value);

                // scale is stored but not used in current shaders — read and discard
                s_->readF32(); s_->readF32(); s_->readF32();

                ch.posKeys.push_back(pk);
                ch.rotKeys.push_back(rk);
            }

            anim->channels.push_back(std::move(ch));
        }

        if (s_->tell() < chunkEnd) s_->seek(chunkEnd);
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[Animation] Loaded '%s'  channels=%zu  duration=%.2f  tps=%.1f",
                anim->name.c_str(),
                anim->channels.size(),
                anim->duration,
                anim->ticksPerSecond);
    return true;
}
