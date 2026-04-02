#include "MeshLoader.hpp"
#include "Manager.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <cstring>


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
        s_->writeStr(tex ? tex->name : "");
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
    uint32_t count = s_->readU32();
    auto &matMgr = MaterialManager::instance();
    auto &texMgr = TextureManager::instance();

    for (uint32_t i = 0; i < count; i++)
    {
        std::string name = s_->readStr();

        // diffuse
        glm::vec3 col;
        col.r = s_->readF32(); col.g = s_->readF32(); col.b = s_->readF32();

        // specular + shininess (original format — not used by current shaders)
        s_->readF32(); s_->readF32(); s_->readF32(); // specular
        s_->readF32();                               // shininess

        uint8_t numLayers = s_->readU8();

        std::string key = name.empty() ? ("__mat_" + std::to_string(i)) : name;
        Material *mat = matMgr.has(key) ? matMgr.get(key) : matMgr.create(key);
        mat->setVec3("u_color", col);

        for (uint8_t j = 0; j < numLayers; j++)
        {
            std::string texName = s_->readStr();
            if (texName.empty()) continue;

            // Only assign the first layer as u_albedo
            if (j == 0)
            {
                std::string texPath = texName;
                if (!textureDir.empty())
                    texPath = textureDir + "/" + texName;
                Texture *tex = texMgr.load(texName, texPath);
                if (tex) mat->setTexture("u_albedo", tex);
            }
        }

        mats.push_back(mat);
    }
}

void MeshReader::readSkeleton(const ChunkHeader &h, std::vector<Bone> &bones)
{
    uint32_t count = s_->readU32();
    bones.resize(count);

    for (uint32_t i = 0; i < count; i++)
    {
        bones[i].name   = s_->readStr();
        bones[i].parent = s_->readI32();

        // localPose — rest pose in parent-local space
        float lm[16];
        for (int j = 0; j < 16; j++) lm[j] = s_->readF32();
        bones[i].localPose = glm::make_mat4(lm);

        // inverseBindPose = what we store as offset
        float m[16];
        for (int j = 0; j < 16; j++) m[j] = s_->readF32();
        bones[i].offset = glm::make_mat4(m);
    }
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
    uint32_t materialIndex  = s_->readU32();  // original: materialIndex before flags
    uint32_t flags          = s_->readU32();
    bool     hasTangents    = (flags & BUFFER_FLAG_TANGENTS) != 0;

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
        else skipChunk(sub);

        if (s_->tell() < subEnd) s_->seek(subEnd);
    }

    // Each BUFF in the original format = one surface
    if (indexCount > 0)
        mesh->add_surface(indexStart, indexCount, (int)materialIndex);
}

// ── animated buffer ───────────────────────────────────────────
void MeshReader::readBuffer(const ChunkHeader &h, AnimatedMesh *mesh)
{
    Sint64   end           = s_->tell() + h.length;
    uint32_t materialIndex = s_->readU32();  // original: materialIndex before flags
    uint32_t flags         = s_->readU32();
    bool     skinned       = (flags & BUFFER_FLAG_SKINNED)  != 0;
    bool     hasTangents   = (flags & BUFFER_FLAG_TANGENTS) != 0;

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
                v.uv.x = s_->readF32(); v.uv.y = 1.0f - s_->readF32();
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
        else skipChunk(sub);

        if (s_->tell() < subEnd) s_->seek(subEnd);
    }

    if (indexCount > 0)
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
    uint32_t ver = s_->readU32();
    if (ver > MESH_VERSION)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[MeshReader] Newer version %u in: %s", ver, path.c_str());

    Sint64 fileSize = s_->size();
    while (s_->tell() + 8 <= fileSize)
    {
        auto   h   = readHeader();
        Sint64 end = s_->tell() + h.length;

        if      (h.id == CHUNK_MATS) readMaterials(h, mesh->materials);
        else if (h.id == CHUNK_BUFF) readBuffer(h, mesh);
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
    s_->readU32(); // version

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