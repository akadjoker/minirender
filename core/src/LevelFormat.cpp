#include "LevelFormat.hpp"
#include "MeshLoader.hpp"
#include "Manager.hpp"
#include "Utils.hpp"
#include <SDL2/SDL.h>

namespace
{
std::string resolveTexturePath(const std::string& ref, const std::string& dir)
{
    if (ref.empty() || dir.empty()) return ref;
    if (ref[0] == '/' || ref[0] == '\\' || (ref.size() > 1 && ref[1] == ':'))
        return ref;
    std::string resolved = ResolveTexturePath(dir, ref);
    return resolved.empty() ? ref : resolved;
}
} // anonymous namespace

// ============================================================
//  LevelMeshBuffer
// ============================================================
void LevelMeshBuffer::upload()
{
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(LevelVertex), vertices.data(), GL_STATIC_DRAW);

    // 0: position  vec3
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LevelVertex), (void*)offsetof(LevelVertex, position));
    glEnableVertexAttribArray(0);
    // 1: normal    vec3
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LevelVertex), (void*)offsetof(LevelVertex, normal));
    glEnableVertexAttribArray(1);
    // 2: tangent   vec4
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(LevelVertex), (void*)offsetof(LevelVertex, tangent));
    glEnableVertexAttribArray(2);
    // 3: uv        vec2
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(LevelVertex), (void*)offsetof(LevelVertex, uv));
    glEnableVertexAttribArray(3);
    // 4: lightmapUv vec2
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(LevelVertex), (void*)offsetof(LevelVertex, lightmapUv));
    glEnableVertexAttribArray(4);

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);
}

void LevelMeshBuffer::free()
{
    if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
    if (ibo) { glDeleteBuffers(1, &ibo); ibo = 0; }
    if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
}

void LevelMeshBuffer::draw() const
{
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void LevelMeshBuffer::drawRange(uint32_t start, uint32_t count) const
{
    glBindVertexArray(vao);
    const void* offset = reinterpret_cast<const void*>(static_cast<uintptr_t>(start * sizeof(uint32_t)));
    glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_INT, offset);
    glBindVertexArray(0);
}

// ============================================================
//  LevelMesh
// ============================================================
void LevelMesh::upload()
{
    buffer.upload();
    computeAABB();
}

void LevelMesh::computeAABB()
{
    if (buffer.vertices.empty()) return;
    glm::vec3 mn = buffer.vertices[0].position;
    glm::vec3 mx = mn;
    for (const auto& v : buffer.vertices)
    {
        mn = glm::min(mn, v.position);
        mx = glm::max(mx, v.position);
    }
    aabb.min = mn;
    aabb.max = mx;
}

void LevelMesh::drawSurface(int surfaceIndex) const
{
    if (surfaceIndex < 0 || surfaceIndex >= (int)surfaces.size()) return;
    const auto& s = surfaces[surfaceIndex];
    buffer.drawRange(s.index_start, s.index_count);
}

void LevelMesh::add_surface(uint32_t start, uint32_t count, int matIndex)
{
    surfaces.push_back({start, count, matIndex});
}

LevelMesh::~LevelMesh()
{
    for (auto* m : materials) delete m;
}

PickResult LevelMesh::pick(const Ray& worldRay, const glm::mat4& model) const
{
    PickResult best;
    best.hit = false;
    best.distance = 1e30f;

    // AABB reject
    glm::vec3 invDir = 1.0f / worldRay.direction;
    glm::vec3 t0 = (aabb.min - worldRay.origin) * invDir;
    glm::vec3 t1 = (aabb.max - worldRay.origin) * invDir;
    glm::vec3 tmin = glm::min(t0, t1);
    glm::vec3 tmax = glm::max(t0, t1);
    float enter = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
    float exit  = glm::min(glm::min(tmax.x, tmax.y), tmax.z);
    if (enter > exit || exit < 0.0f) return best;

    // Möller–Trumbore per triangle
    for (size_t i = 0; i + 2 < buffer.indices.size(); i += 3)
    {
        const glm::vec3& v0 = buffer.vertices[buffer.indices[i    ]].position;
        const glm::vec3& v1 = buffer.vertices[buffer.indices[i + 1]].position;
        const glm::vec3& v2 = buffer.vertices[buffer.indices[i + 2]].position;

        glm::vec3 e1 = v1 - v0, e2 = v2 - v0;
        glm::vec3 h = glm::cross(worldRay.direction, e2);
        float a = glm::dot(e1, h);
        if (a > -1e-6f && a < 1e-6f) continue;
        float f = 1.0f / a;
        glm::vec3 s = worldRay.origin - v0;
        float u = f * glm::dot(s, h);
        if (u < 0.0f || u > 1.0f) continue;
        glm::vec3 q = glm::cross(s, e1);
        float v = f * glm::dot(worldRay.direction, q);
        if (v < 0.0f || u + v > 1.0f) continue;
        float t = f * glm::dot(e2, q);
        if (t > 1e-6f && t < best.distance)
        {
            best.hit = true;
            best.distance = t;
            best.point = worldRay.origin + t * worldRay.direction;
            best.normal = glm::normalize(glm::cross(e1, e2));
        }
    }
    return best;
}

// ============================================================
//  LevelReader::load
// ============================================================
bool LevelReader::load(const std::string& path, LevelData* out)
{
    BinaryStream stream(path, "rb");
    if (!stream.isOpen()) return false;

    if (stream.readU32() != MRLV_MAGIC)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[LevelReader] Invalid magic: %s", path.c_str());
        return false;
    }
    uint32_t version = stream.readU32();
    if (version > MRLV_VERSION)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[LevelReader] Newer version %u in: %s", version, path.c_str());

    LevelMesh& mesh = out->mesh;
    Sint64 fileSize = stream.size();

    while (stream.tell() + 8 <= fileSize)
    {
        uint32_t chunkId  = stream.readU32();
        uint32_t chunkLen = stream.readU32();
        Sint64   chunkEnd = stream.tell() + chunkLen;

        if (chunkId == CHUNK_MATS)
        {
            uint32_t count = stream.readU32();
            auto& texMgr = TextureManager::instance();
            for (uint32_t i = 0; i < count; i++)
            {
                std::string name = stream.readStr();
                glm::vec3 col;
                col.r = stream.readF32(); col.g = stream.readF32(); col.b = stream.readF32();
                std::string textureRef = stream.readStr();

                std::string key = name.empty() ? ("__mat_" + std::to_string(i)) : name;
                Material* mat = new Material();
                mat->name = key;
                mat->setVec3("u_color", col);

                if (!textureRef.empty())
                {
                    const std::string resolved = resolveTexturePath(textureRef, textureDir);
                    Texture* tex = texMgr.load(textureRef, resolved);
                    if (tex)
                        mat->setTexture("u_albedo", tex);
                }
                mesh.materials.push_back(mat);
            }
        }
        else if (chunkId == CHUNK_BUFF)
        {
            Sint64 buffEnd = chunkEnd;
            uint32_t flags = stream.readU32();
            bool hasTangents   = (flags & BUFFER_FLAG_TANGENTS) != 0;
            bool hasLightmapUV = (flags & BUFFER_FLAG_LIGHTMAP) != 0;

            uint32_t vertexBase = (uint32_t)mesh.buffer.vertices.size();
            uint32_t indexStart = (uint32_t)mesh.buffer.indices.size();
            bool hasSurfaceChunk = false;

            while (stream.tell() + 8 <= buffEnd)
            {
                uint32_t subId  = stream.readU32();
                uint32_t subLen = stream.readU32();
                Sint64   subEnd = stream.tell() + subLen;

                if (subId == CHUNK_VRTS)
                {
                    uint32_t count = stream.readU32();
                    mesh.buffer.vertices.reserve(vertexBase + count);
                    for (uint32_t i = 0; i < count; i++)
                    {
                        LevelVertex v{};
                        v.position.x = stream.readF32(); v.position.y = stream.readF32(); v.position.z = stream.readF32();
                        v.normal.x   = stream.readF32(); v.normal.y   = stream.readF32(); v.normal.z   = stream.readF32();
                        if (hasTangents) {
                            v.tangent.x = stream.readF32(); v.tangent.y = stream.readF32();
                            v.tangent.z = stream.readF32(); v.tangent.w = stream.readF32();
                        }
                        v.uv.x = stream.readF32(); v.uv.y = stream.readF32();
                        if (hasLightmapUV) {
                            v.lightmapUv.x = stream.readF32();
                            v.lightmapUv.y = stream.readF32();
                        }
                        mesh.buffer.vertices.push_back(v);
                    }
                }
                else if (subId == CHUNK_IDXS)
                {
                    uint32_t count = stream.readU32();
                    mesh.buffer.indices.reserve(indexStart + count);
                    for (uint32_t i = 0; i < count; i++)
                        mesh.buffer.indices.push_back(stream.readU32() + vertexBase);
                }
                else if (subId == CHUNK_SURF)
                {
                    hasSurfaceChunk = true;
                    uint32_t count = stream.readU32();
                    for (uint32_t i = 0; i < count; i++)
                    {
                        Surface s;
                        s.index_start    = stream.readU32() + indexStart;
                        s.index_count    = stream.readU32();
                        s.material_index = stream.readI32();
                        mesh.surfaces.push_back(s);
                    }
                }

                if (stream.tell() < subEnd) stream.seek(subEnd);
            }

            if (!hasSurfaceChunk)
            {
                uint32_t totalIdx = (uint32_t)mesh.buffer.indices.size() - indexStart;
                if (totalIdx > 0)
                    mesh.add_surface(indexStart, totalIdx, 0);
            }
        }
        else if (chunkId == CHUNK_LMAP)
        {
            mesh.lightmap.width    = stream.readI32();
            mesh.lightmap.height   = stream.readI32();
            mesh.lightmap.channels = stream.readI32();
            uint32_t dataSize = mesh.lightmap.width * mesh.lightmap.height * mesh.lightmap.channels;
            mesh.lightmap.pixels.resize(dataSize);
            stream.readRaw(mesh.lightmap.pixels.data(), dataSize);
        }
        else if (chunkId == CHUNK_ENTS)
        {
            uint32_t count = stream.readU32();
            for (uint32_t i = 0; i < count; i++)
            {
                std::string name = stream.readStr();
                auto type = static_cast<LevelEntityType>(stream.readU32());
                glm::vec3 pos;
                pos.x = stream.readF32(); pos.y = stream.readF32(); pos.z = stream.readF32();

                switch (type)
                {
                case LevelEntityType::PlayerStart: {
                    LevelPlayerStart e;
                    e.name = std::move(name); e.position = pos;
                    e.direction.x = stream.readF32(); e.direction.y = stream.readF32(); e.direction.z = stream.readF32();
                    out->playerStarts.push_back(std::move(e));
                } break;
                case LevelEntityType::Light: {
                    LevelLight e;
                    e.name = std::move(name); e.position = pos;
                    e.lightType    = static_cast<LightType>(stream.readU32());
                    e.color.x = stream.readF32(); e.color.y = stream.readF32(); e.color.z = stream.readF32();
                    e.intensity    = stream.readF32();
                    e.radius       = stream.readF32();
                    e.direction.x  = stream.readF32(); e.direction.y = stream.readF32(); e.direction.z = stream.readF32();
                    e.spotAngle    = stream.readF32();
                    e.spotSoftness = stream.readF32();
                    out->lights.push_back(std::move(e));
                } break;
                case LevelEntityType::Door: {
                    LevelDoor e;
                    e.name = std::move(name); e.position = pos;
                    e.doorType      = static_cast<LevelDoorType>(stream.readU32());
                    e.direction.x   = stream.readF32(); e.direction.y = stream.readF32(); e.direction.z = stream.readF32();
                    e.distance      = stream.readF32();
                    e.speed         = stream.readF32();
                    e.startOpen     = stream.readU8() != 0;
                    e.linkedMesh    = stream.readI32();
                    out->doors.push_back(std::move(e));
                } break;
                case LevelEntityType::Elevator: {
                    LevelElevator e;
                    e.name = std::move(name); e.position = pos;
                    e.endPosition.x = stream.readF32(); e.endPosition.y = stream.readF32(); e.endPosition.z = stream.readF32();
                    e.speed      = stream.readF32();
                    e.waitTime   = stream.readF32();
                    e.linkedMesh = stream.readI32();
                    out->elevators.push_back(std::move(e));
                } break;
                case LevelEntityType::Platform: {
                    LevelPlatform e;
                    e.name = std::move(name); e.position = pos;
                    e.endPosition.x = stream.readF32(); e.endPosition.y = stream.readF32(); e.endPosition.z = stream.readF32();
                    e.speed      = stream.readF32();
                    e.waitTime   = stream.readF32();
                    e.linkedMesh = stream.readI32();
                    out->platforms.push_back(std::move(e));
                } break;
                case LevelEntityType::Placement: {
                    LevelPlacement e;
                    e.name = std::move(name); e.position = pos;
                    e.itemType  = stream.readI32();
                    e.rotationY = stream.readF32();
                    out->placements.push_back(std::move(e));
                } break;
                case LevelEntityType::Trigger: {
                    LevelTrigger e;
                    e.name = std::move(name); e.position = pos;
                    e.radius     = stream.readF32();
                    e.targetName = stream.readStr();
                    out->triggers.push_back(std::move(e));
                } break;
                case LevelEntityType::Teleporter: {
                    LevelTeleporter e;
                    e.name = std::move(name); e.position = pos;
                    e.target.x = stream.readF32(); e.target.y = stream.readF32(); e.target.z = stream.readF32();
                    out->teleporters.push_back(std::move(e));
                } break;
                case LevelEntityType::SoundEmitter: {
                    LevelSoundEmitter e;
                    e.name = std::move(name); e.position = pos;
                    e.soundPath = stream.readStr();
                    e.radius    = stream.readF32();
                    e.volume    = stream.readF32();
                    e.looping   = stream.readU8() != 0;
                    out->soundEmitters.push_back(std::move(e));
                } break;
                }
            }
        }

        if (stream.tell() < chunkEnd) stream.seek(chunkEnd);
    }

    mesh.upload();
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[LevelReader] '%s'  verts=%zu  idx=%zu  surfs=%zu  mats=%zu  entities=%d%s",
                path.c_str(),
                mesh.buffer.vertices.size(),
                mesh.buffer.indices.size(),
                mesh.surfaces.size(),
                mesh.materials.size(),
                out->entityCount(),
                mesh.lightmap.empty() ? "" : "  +lightmap");
    return true;
}
