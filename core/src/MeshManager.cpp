
#define CGLTF_IMPLEMENTATION

#include "cgltf.h"
#include "Animation.hpp"
#include "Manager.hpp"
#include "MeshLoader.hpp"
#include "Utils.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

Vertex make_vertex(const glm::vec3 &position, const glm::vec3 &normal, const glm::vec2 &uv)
{
    return {position, normal, glm::vec4(1, 0, 0, 1), uv};
}

void finalize_procedural_mesh(Mesh *mesh)
{
    if (!mesh->buffer.indices.empty() && !mesh->buffer.vertices.empty())
        mesh->compute_tangents();
    mesh->upload();
    mesh->add_surface(0, (uint32_t)mesh->buffer.indices.size());
    // Ensure at least one material slot exists — gatherNode skips items with mat==nullptr
    if (mesh->materials.empty())
        mesh->add_material(MaterialManager::instance().create(mesh->name));
}

std::string dirFromPath(const std::string &path)
{
    return PathDirectory(path);
}

bool startsWithInsensitive(const std::string &s, const std::string &prefix)
{
    if (s.size() < prefix.size())
        return false;

    for (size_t i = 0; i < prefix.size(); ++i)
    {
        const unsigned char a = static_cast<unsigned char>(s[i]);
        const unsigned char b = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(a) != std::tolower(b))
            return false;
    }
    return true;
}

std::string resolveGltfTexturePath(const std::string &uri,
                                   const std::string &meshPath,
                                   const std::string &textureDir)
{
    if (uri.empty())
        return {};

    if (startsWithInsensitive(uri, "data:"))
        return {};

    if (PathIsAbsolute(uri) && FileExists(uri))
        return uri;

    const std::string base = textureDir.empty() ? dirFromPath(meshPath) : textureDir;
    const std::string c0 = PathJoin(base, uri);
    if (FileExists(c0))
        return c0;

    if (FileExists(uri))
        return uri;

    return {};
}

glm::mat4 cgltfMatToGlm(const float m[16])
{
    return glm::make_mat4(m);
}

bool posKeyLess(const PosKey &a, const PosKey &b) { return a.time < b.time; }
bool rotKeyLess(const RotKey &a, const RotKey &b) { return a.time < b.time; }
bool scaleKeyLess(const ScaleKey &a, const ScaleKey &b) { return a.time < b.time; }

}

// ============================================================
//  MeshManager
// ============================================================
MeshManager &MeshManager::instance()
{
    static MeshManager inst;
    return inst;
}

Mesh *MeshManager::create(const std::string &name)
{

    if (has(name))
    {
        LogWarning("[MeshManager] '%s' already exists, returning existing",
                    name.c_str());
        return get(name);
    }

    auto *m = new Mesh();
    m->name = name;
    cache[name] = m;
    return m;
}

AnimatedMesh *MeshManager::createAnimated(const std::string &name)
{
    auto it = animatedCache_.find(name);
    if (it != animatedCache_.end())
    {
        LogWarning("[MeshManager] animated '%s' already exists, returning existing",
                    name.c_str());
        return it->second;
    }

    auto *m = new AnimatedMesh();
    m->name = name;
    animatedCache_[name] = m;
    return m;
}

AnimatedMesh *MeshManager::getAnimated(const std::string &name) const
{
    auto it = animatedCache_.find(name);
    return (it != animatedCache_.end()) ? it->second : nullptr;
}

bool MeshManager::hasAnimated(const std::string &name) const
{
    return animatedCache_.count(name) > 0;
}

void MeshManager::unloadAnimated(const std::string &name)
{
    auto it = animatedCache_.find(name);
    if (it == animatedCache_.end())
        return;

    delete it->second;
    animatedCache_.erase(it);
}

void MeshManager::unloadAll()
{
    ResourceManager<Mesh>::unloadAll();

    for (auto &it : animatedCache_)
        delete it.second;
    animatedCache_.clear();
}

Mesh *MeshManager::load(const std::string &name, const std::string &path,
                        const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    auto dot = path.rfind('.');
    if (dot == std::string::npos)
    {
        LogWarning("[MeshManager] No extension in path: %s", path.c_str());
        return nullptr;
    }
    std::string ext = path.substr(dot + 1);
    for (size_t i = 0; i < ext.size(); ++i)
        ext[i] = (char)std::tolower((unsigned char)ext[i]);

    if (ext == "obj")
        return load_obj(name, path, texture_dir);
    if (ext == "3ds")
        return load_3ds(name, path, texture_dir);
    if (ext == "gltf" || ext == "glb")
        return load_gltf(name, path, texture_dir);
    if (ext == "h3d" || ext == "mesh")
        return load_h3d(name, path, texture_dir);

    LogWarning("[MeshManager] Unknown mesh format '%s': %s", ext.c_str(), path.c_str());
    return nullptr;
}


// ============================================================
//  OBJ loader  — custom streaming parser 
//  Single pass, per-material vertex cache 
// ============================================================
Mesh *MeshManager::load_obj(const std::string &name, const std::string &path,
                             const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    // ── Read OBJ into memory ──────────────────────────────────────────────
    SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw)
    {
        LogError("[MeshManager] Cannot open OBJ: %s  (%s)", path.c_str(), SDL_GetError());
        return nullptr;
    }
    Sint64 fsize = SDL_RWsize(rw);
    std::string buf(fsize, '\0');
    SDL_RWread(rw, buf.data(), 1, fsize);
    SDL_RWclose(rw);

    LogInfo("[MeshManager] Parsing OBJ: %s  (%.1f KB)", path.c_str(), buf.size() / 1024.f);

    std::string obj_dir;
    {
        auto slash = path.rfind('/');
        if (slash != std::string::npos)
            obj_dir = path.substr(0, slash + 1);
    }
    std::string base_dir = texture_dir.empty() ? obj_dir
                         : (texture_dir.back() == '/' ? texture_dir : texture_dir + '/');

    // ── MTL loader (reads via SDL, parses into local structs) ─────────────
    struct OBJMat
    {
        std::string name;
        glm::vec3 diffuse;
        std::string tex;
        float opacity;

        OBJMat() : diffuse(0.8f, 0.8f, 0.8f), opacity(1.0f) {}
    };
    std::vector<OBJMat>                     objMats;
    std::unordered_map<std::string, int>    matNameToIdx;

    auto loadMTL = [&](const std::string &mtlFile)
    {
        std::string mtlPath = obj_dir + mtlFile;
        SDL_RWops *mrw = SDL_RWFromFile(mtlPath.c_str(), "rb");
        if (!mrw) { LogWarning("[MeshManager] Cannot open MTL: %s", mtlPath.c_str()); return; }
        Sint64 msz = SDL_RWsize(mrw);
        std::string mbuf(msz, '\0');
        SDL_RWread(mrw, mbuf.data(), 1, msz);
        SDL_RWclose(mrw);

        OBJMat *cur = nullptr;
        std::istringstream ss(mbuf);
        std::string line;
        while (std::getline(ss, line))
        {
            if (line.empty() || line[0] == '#') continue;
            // strip \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::istringstream ls(line);
            std::string cmd; ls >> cmd;
            if (cmd == "newmtl")
            {
                std::string n; ls >> n;
                OBJMat m; m.name = n;
                matNameToIdx[n] = (int)objMats.size();
                objMats.push_back(m);
                cur = &objMats.back();
            }
            else if (cur)
            {
                if (cmd == "Kd")        { ls >> cur->diffuse.r >> cur->diffuse.g >> cur->diffuse.b; }
                else if (cmd == "d")   { ls >> cur->opacity; }
                else if (cmd == "Tr")
                {
                    float tr = 0.0f;
                    ls >> tr;
                    cur->opacity = 1.0f - tr;
                }
                else if (cmd == "map_Kd" || cmd == "map_Ka")
                {
                    std::string t; std::getline(ls >> std::ws, t);
                    if (!t.empty() && t.back() == '\r') t.pop_back();
                    cur->tex = t;
                }
            }
        }
        LogInfo("[MeshManager] MTL loaded: %zu materials from %s",
                    objMats.size(), mtlFile.c_str());
    };

    // ── Temporary geometry arrays ─────────────────────────────────────────
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texcoords;
    positions.reserve(8192);
    normals.reserve(8192);
    texcoords.reserve(8192);

    // ── Per-surface state ─────────────────────────────────────────────────
    struct VKey { int v, vt, vn;
                  bool operator==(const VKey &o) const { return v==o.v&&vt==o.vt&&vn==o.vn; } };
    struct VKeyHash {
        size_t operator()(const VKey &k) const {
            size_t h = (uint32_t)k.v;
            h ^= (uint32_t)k.vt * 2654435761u;
            h ^= (uint32_t)k.vn * 2246822519u;
            return h;
        }
    };

    auto *mesh = new Mesh();
    mesh->name = name;

    // One SurfaceWork per material — accumulates ALL faces for that material
    struct SurfaceWork {
        int matIdx = -1;
        std::unordered_map<VKey,uint32_t,VKeyHash> cache;
        std::vector<uint32_t> localIndices;
    };
    // keyed by matIdx; groupOrder preserves first-seen order
    std::unordered_map<int, SurfaceWork> matGroups;
    std::vector<int> groupOrder;

    auto getGroup = [&](int matIdx) -> SurfaceWork &
    {
        auto it = matGroups.find(matIdx);
        if (it != matGroups.end()) return it->second;
        SurfaceWork sw;
        sw.matIdx = matIdx;
        sw.cache.reserve(512);
        matGroups.emplace(matIdx, std::move(sw));
        groupOrder.push_back(matIdx);
        return matGroups[matIdx];
    };
    getGroup(-1); // ensure default group exists

    int currentMatIdx = -1;

    // ── Single streaming pass ─────────────────────────────────────────────
    std::istringstream ss(buf);
    std::string line;
    while (std::getline(ss, line))
    {
        if (line.empty() || line[0] == '#') continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        const char *p = line.c_str();
        while (*p == ' ' || *p == '\t') ++p;

        if (p[0] == 'v' && p[1] == ' ')          // vertex position
        {
            glm::vec3 v;
            sscanf(p + 2, "%f %f %f", &v.x, &v.y, &v.z);
            positions.push_back(v);
        }
        else if (p[0] == 'v' && p[1] == 'n')     // vertex normal
        {
            glm::vec3 n;
            sscanf(p + 3, "%f %f %f", &n.x, &n.y, &n.z);
            normals.push_back(n);
        }
        else if (p[0] == 'v' && p[1] == 't')     // tex coord
        {
            glm::vec2 uv;
            sscanf(p + 3, "%f %f", &uv.x, &uv.y);
            texcoords.push_back(uv);
        }
        else if (strncmp(p, "mtllib ", 7) == 0)
        {
            std::string mtlFile(p + 7);
            if (!mtlFile.empty() && mtlFile.back() == '\r') mtlFile.pop_back();
            loadMTL(mtlFile);
        }
        else if (strncmp(p, "usemtl ", 7) == 0)
        {
            std::string matName(p + 7);
            if (!matName.empty() && matName.back() == '\r') matName.pop_back();
            auto it = matNameToIdx.find(matName);
            int idx = (it != matNameToIdx.end()) ? it->second : -1;
            currentMatIdx = idx; // group is created lazily on first face
        }
        else if (p[0] == 'f' && p[1] == ' ')     // face
        {
            // Parse all vertex refs on this line, then triangulate as fan
            const char *fp = p + 2;
            std::vector<VKey> faceVerts;
            faceVerts.reserve(4);
            while (*fp)
            {
                while (*fp == ' ' || *fp == '\t') ++fp;
                if (!*fp) break;
                VKey k{0,0,0};
                // v/vt/vn  or  v/vt  or  v//vn  or  v
                int consumed = 0;
                if (sscanf(fp, "%d/%d/%d%n", &k.v, &k.vt, &k.vn, &consumed) == 3) {}
                else if (sscanf(fp, "%d//%d%n", &k.v, &k.vn, &consumed) == 2) { k.vt = 0; }
                else if (sscanf(fp, "%d/%d%n",  &k.v, &k.vt, &consumed) == 2) { k.vn = 0; }
                else if (sscanf(fp, "%d%n",     &k.v, &consumed)         == 1) { k.vt = k.vn = 0; }
                else break;
                // Convert OBJ 1-based (negative = relative) to 0-based
                if (k.v  < 0) k.v  = (int)positions.size()  + k.v  + 1;
                if (k.vt < 0) k.vt = (int)texcoords.size()  + k.vt + 1;
                if (k.vn < 0) k.vn = (int)normals.size()    + k.vn + 1;
                faceVerts.push_back(k);
                fp += consumed;
            }

            SurfaceWork &sw = getGroup(currentMatIdx);
            // Resolve vertices into global vertex buffer, fan-triangulate
            std::vector<uint32_t> fi;
            fi.reserve(faceVerts.size());
            for (auto &k : faceVerts)
            {
                auto it = sw.cache.find(k);
                if (it != sw.cache.end()) { fi.push_back(it->second); }
                else
                {
                    uint32_t idx = (uint32_t)mesh->buffer.vertices.size();
                    sw.cache[k] = idx;
                    fi.push_back(idx);

                    Vertex vx{};
                    vx.tangent = {1,0,0,1};
                    if (k.v > 0 && k.v <= (int)positions.size())
                        vx.position = positions[k.v - 1];
                    if (k.vt > 0 && k.vt <= (int)texcoords.size())
                    {
                        vx.uv.x =      texcoords[k.vt - 1].x;
                        vx.uv.y = 1.f - texcoords[k.vt - 1].y;
                    }
                    if (k.vn > 0 && k.vn <= (int)normals.size())
                        vx.normal = normals[k.vn - 1];
                    mesh->buffer.vertices.push_back(vx);
                }
            }
            for (size_t i = 1; i + 1 < fi.size(); i++)
            {
                sw.localIndices.push_back(fi[0]);
                sw.localIndices.push_back(fi[i]);
                sw.localIndices.push_back(fi[i+1]);
            }
        }
    }

    // ── Build surfaces + register materials ───────────────────────────────
    auto &matMgr = MaterialManager::instance();
    auto &texMgr = TextureManager::instance();

    // Default material (used when no MTL or matIdx == -1)
    std::string defMatName = name + "/__default";
    Material *defMat = matMgr.has(defMatName) ? matMgr.get(defMatName) : matMgr.create(defMatName);
    defMat->setVec3("u_color", {0.8f, 0.8f, 0.8f});
    defMat->setVec3("u_albedoTint", {0.8f, 0.8f, 0.8f});
    defMat->setFloat("u_opacity", 1.0f);

    // Build MaterialManager entries for each objMat — index matches objMats[]
    std::vector<Material*> matPtrs;
    for (auto &om : objMats)
    {
        std::string mn = name + "/" + om.name;
        Material *mat = matMgr.has(mn) ? matMgr.get(mn) : matMgr.create(mn);
        mat->setVec3("u_color", {om.diffuse.r, om.diffuse.g, om.diffuse.b});
        mat->setVec3("u_albedoTint", {om.diffuse.r, om.diffuse.g, om.diffuse.b});
        mat->setFloat("u_opacity", om.opacity);
        if (om.opacity < 0.999f)
        {
            mat->setBlend(true);
            mat->setDepthWrite(false);
            mat->setCullFace(false);
        }
        if (!om.tex.empty())
        {
            std::string tp = base_dir + om.tex;
            if (Texture *tex = texMgr.load(mn + "_diffuse", tp))
                mat->setTexture("u_albedo", tex);
        }
        matPtrs.push_back(mat);
    }

    // mesh->materials[0] = default, [1..n] = per-material
    mesh->materials.push_back(defMat);
    for (auto *m : matPtrs) mesh->materials.push_back(m);

    // Commit one surface per material — concatenate each group's localIndices
    for (int matIdx : groupOrder)
    {
        auto &sw = matGroups[matIdx];
        if (sw.localIndices.empty()) continue;
        uint32_t indexStart = (uint32_t)mesh->buffer.indices.size();
        mesh->buffer.indices.insert(mesh->buffer.indices.end(),
                                    sw.localIndices.begin(), sw.localIndices.end());
        // slot 0 = default, slot i+1 = matPtrs[i]
        int slot = (sw.matIdx >= 0 && sw.matIdx < (int)matPtrs.size()) ? sw.matIdx + 1 : 0;
        mesh->add_surface(indexStart, (uint32_t)sw.localIndices.size(), slot);

        // Compute per-surface local-space AABB from the indices we just committed
        BoundingBox &saabb = mesh->surfaces.back().aabb;
        for (uint32_t idx : sw.localIndices)
            if (idx < (uint32_t)mesh->buffer.vertices.size())
                saabb.expand(mesh->buffer.vertices[idx].position);
    }

    if (normals.empty())
        mesh->compute_normals();

    mesh->upload();

    LogInfo("[MeshManager] OBJ loaded: %s  verts=%zu  idx=%zu  surfaces=%zu  materials=%zu",
                name.c_str(), mesh->buffer.vertices.size(), mesh->buffer.indices.size(),
                mesh->surfaces.size(), mesh->materials.size());

    // Fill in default shader + fallback texture for any material that lacks them
    matMgr.applyDefaults();

    cache[name] = mesh;
    return mesh;
}

Mesh *MeshManager::create_screen_quad(const std::string &name)
{
    if (auto *existing = get(name))
        return existing;

    auto *mesh = new Mesh();
    mesh->name = name;
    mesh->buffer.vertices = {
        make_vertex({-1.0f, -1.0f, 0.0f}, {0, 0, 1}, {0, 0}),
        make_vertex({1.0f, -1.0f, 0.0f}, {0, 0, 1}, {1, 0}),
        make_vertex({1.0f, 1.0f, 0.0f}, {0, 0, 1}, {1, 1}),
        make_vertex({-1.0f, 1.0f, 0.0f}, {0, 0, 1}, {0, 1}),
    };
    mesh->buffer.indices = {0, 1, 2, 0, 2, 3};

    finalize_procedural_mesh(mesh);
    cache[name] = mesh;
    return mesh;
}

Mesh *MeshManager::create_sphere(const std::string &name, float radius, int segments)
{
    if (auto *existing = get(name))
        return existing;

    radius = std::max(std::abs(radius), 0.0001f);
    const int latSegments = std::max(2, segments);
    const int lonSegments = std::max(3, segments * 2);
    const int stride = lonSegments + 1;

    auto *mesh = new Mesh();
    mesh->name = name;
    mesh->buffer.vertices.reserve((latSegments + 1) * stride);
    mesh->buffer.indices.reserve(latSegments * lonSegments * 6);

    for (int y = 0; y <= latSegments; ++y)
    {
        float v = static_cast<float>(y) / static_cast<float>(latSegments);
        float phi = v * kPi;
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);

        for (int x = 0; x <= lonSegments; ++x)
        {
            float u = static_cast<float>(x) / static_cast<float>(lonSegments);
            float theta = u * kTwoPi;
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            glm::vec3 normal(cosTheta * sinPhi, cosPhi, sinTheta * sinPhi);
            glm::vec3 position = normal * radius;
            mesh->buffer.vertices.push_back(make_vertex(position, normal, {u, v}));
        }
    }

    for (int y = 0; y < latSegments; ++y)
    {
        for (int x = 0; x < lonSegments; ++x)
        {
            uint32_t a = y * stride + x;
            uint32_t b = (y + 1) * stride + x;
            uint32_t c = a + 1;
            uint32_t d = b + 1;

            mesh->buffer.indices.push_back(a);
            mesh->buffer.indices.push_back(b);
            mesh->buffer.indices.push_back(c);
            mesh->buffer.indices.push_back(c);
            mesh->buffer.indices.push_back(b);
            mesh->buffer.indices.push_back(d);
        }
    }

    finalize_procedural_mesh(mesh);
    cache[name] = mesh;
    return mesh;
}

Mesh *MeshManager::create_cylinder(const std::string &name, float radius, float height, int segments)
{
    if (auto *existing = get(name))
        return existing;

    radius = std::max(std::abs(radius), 0.0001f);
    height = std::max(std::abs(height), 0.0001f);
    segments = std::max(3, segments);
    float halfHeight = height * 0.5f;

    auto *mesh = new Mesh();
    mesh->name = name;
    mesh->buffer.vertices.reserve((segments + 1) * 4 + 2);
    mesh->buffer.indices.reserve(segments * 12);

    const uint32_t sideStart = (uint32_t)mesh->buffer.vertices.size();
    for (int i = 0; i <= segments; ++i)
    {
        float u = static_cast<float>(i) / static_cast<float>(segments);
        float theta = u * kTwoPi;
        float c = std::cos(theta);
        float s = std::sin(theta);
        glm::vec3 normal(c, 0.0f, s);
        mesh->buffer.vertices.push_back(make_vertex({radius * c, -halfHeight, radius * s}, normal, {u, 0}));
        mesh->buffer.vertices.push_back(make_vertex({radius * c, halfHeight, radius * s}, normal, {u, 1}));
    }

    for (int i = 0; i < segments; ++i)
    {
        uint32_t a = sideStart + i * 2;
        uint32_t b = a + 1;
        uint32_t c = a + 2;
        uint32_t d = a + 3;

        mesh->buffer.indices.push_back(a);
        mesh->buffer.indices.push_back(b);
        mesh->buffer.indices.push_back(c);
        mesh->buffer.indices.push_back(c);
        mesh->buffer.indices.push_back(b);
        mesh->buffer.indices.push_back(d);
    }

    const uint32_t bottomCenter = (uint32_t)mesh->buffer.vertices.size();
    mesh->buffer.vertices.push_back(make_vertex({0, -halfHeight, 0}, {0, -1, 0}, {0.5f, 0.5f}));
    for (int i = 0; i <= segments; ++i)
    {
        float u = static_cast<float>(i) / static_cast<float>(segments);
        float theta = u * kTwoPi;
        float c = std::cos(theta);
        float s = std::sin(theta);
        mesh->buffer.vertices.push_back(make_vertex(
            {radius * c, -halfHeight, radius * s},
            {0, -1, 0},
            {0.5f + 0.5f * c, 0.5f + 0.5f * s}));
    }

    for (int i = 0; i < segments; ++i)
    {
        mesh->buffer.indices.push_back(bottomCenter);
        mesh->buffer.indices.push_back(bottomCenter + i + 2);
        mesh->buffer.indices.push_back(bottomCenter + i + 1);
    }

    const uint32_t topCenter = (uint32_t)mesh->buffer.vertices.size();
    mesh->buffer.vertices.push_back(make_vertex({0, halfHeight, 0}, {0, 1, 0}, {0.5f, 0.5f}));
    for (int i = 0; i <= segments; ++i)
    {
        float u = static_cast<float>(i) / static_cast<float>(segments);
        float theta = u * kTwoPi;
        float c = std::cos(theta);
        float s = std::sin(theta);
        mesh->buffer.vertices.push_back(make_vertex(
            {radius * c, halfHeight, radius * s},
            {0, 1, 0},
            {0.5f + 0.5f * c, 0.5f + 0.5f * s}));
    }

    for (int i = 0; i < segments; ++i)
    {
        mesh->buffer.indices.push_back(topCenter);
        mesh->buffer.indices.push_back(topCenter + i + 1);
        mesh->buffer.indices.push_back(topCenter + i + 2);
    }

    finalize_procedural_mesh(mesh);
    cache[name] = mesh;
    return mesh;
}

Mesh *MeshManager::create_capsule(const std::string &name, float radius, float height, int segments)
{
    if (auto *existing = get(name))
        return existing;

    radius = std::max(std::abs(radius), 0.0001f);
    height = std::max(std::abs(height), radius * 2.0f);
    segments = std::max(3, segments);

    if (height <= radius * 2.0f + 0.0001f)
        return create_sphere(name, radius, segments);

    const int hemiSegments = std::max(2, segments);
    const int radialSegments = std::max(3, segments * 2);
    const int stride = radialSegments + 1;
    const float halfCylinder = (height * 0.5f) - radius;

    std::vector<float> ringY;
    std::vector<float> ringRadius;
    std::vector<float> ringNormalY;
    std::vector<float> ringNormalRadius;
    ringY.reserve((hemiSegments + 1) * 2);
    ringRadius.reserve((hemiSegments + 1) * 2);
    ringNormalY.reserve((hemiSegments + 1) * 2);
    ringNormalRadius.reserve((hemiSegments + 1) * 2);

    for (int i = 0; i <= hemiSegments; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(hemiSegments);
        float angle = -kPi * 0.5f + t * (kPi * 0.5f);
        ringY.push_back(-halfCylinder + std::sin(angle) * radius);
        ringRadius.push_back(std::cos(angle) * radius);
        ringNormalY.push_back(std::sin(angle));
        ringNormalRadius.push_back(std::cos(angle));
    }

    for (int i = 0; i <= hemiSegments; ++i)
    {
        float t = static_cast<float>(i) / static_cast<float>(hemiSegments);
        float angle = t * (kPi * 0.5f);
        ringY.push_back(halfCylinder + std::sin(angle) * radius);
        ringRadius.push_back(std::cos(angle) * radius);
        ringNormalY.push_back(std::sin(angle));
        ringNormalRadius.push_back(std::cos(angle));
    }

    auto *mesh = new Mesh();
    mesh->name = name;
    mesh->buffer.vertices.reserve(ringY.size() * stride);
    mesh->buffer.indices.reserve((ringY.size() - 1) * radialSegments * 6);

    for (size_t ring = 0; ring < ringY.size(); ++ring)
    {
        float v = (ringY[ring] + height * 0.5f) / height;
        for (int seg = 0; seg <= radialSegments; ++seg)
        {
            float u = static_cast<float>(seg) / static_cast<float>(radialSegments);
            float theta = u * kTwoPi;
            float c = std::cos(theta);
            float s = std::sin(theta);

            glm::vec3 position(ringRadius[ring] * c, ringY[ring], ringRadius[ring] * s);
            glm::vec3 normal(ringNormalRadius[ring] * c, ringNormalY[ring], ringNormalRadius[ring] * s);
            mesh->buffer.vertices.push_back(make_vertex(position, glm::normalize(normal), {u, v}));
        }
    }

    for (size_t ring = 0; ring + 1 < ringY.size(); ++ring)
    {
        for (int seg = 0; seg < radialSegments; ++seg)
        {
            uint32_t a = (uint32_t)(ring * stride + seg);
            uint32_t b = (uint32_t)((ring + 1) * stride + seg);
            uint32_t c = a + 1;
            uint32_t d = b + 1;

            mesh->buffer.indices.push_back(a);
            mesh->buffer.indices.push_back(b);
            mesh->buffer.indices.push_back(c);
            mesh->buffer.indices.push_back(c);
            mesh->buffer.indices.push_back(b);
            mesh->buffer.indices.push_back(d);
        }
    }

    finalize_procedural_mesh(mesh);
    cache[name] = mesh;
    return mesh;
}

Mesh *MeshManager::create_torus(const std::string &name, float radius, float tubeRadius, int segments, int tubeSegments)
{
    if (auto *existing = get(name))
        return existing;

    radius = std::max(std::abs(radius), 0.0001f);
    tubeRadius = std::max(std::abs(tubeRadius), 0.0001f);
    segments = std::max(3, segments);
    tubeSegments = std::max(3, tubeSegments);
    const int stride = tubeSegments + 1;

    auto *mesh = new Mesh();
    mesh->name = name;
    mesh->buffer.vertices.reserve((segments + 1) * stride);
    mesh->buffer.indices.reserve(segments * tubeSegments * 6);

    for (int i = 0; i <= segments; ++i)
    {
        float u = static_cast<float>(i) / static_cast<float>(segments);
        float theta = u * kTwoPi;
        float ct = std::cos(theta);
        float st = std::sin(theta);

        for (int j = 0; j <= tubeSegments; ++j)
        {
            float v = static_cast<float>(j) / static_cast<float>(tubeSegments);
            float phi = v * kTwoPi;
            float cp = std::cos(phi);
            float sp = std::sin(phi);

            float ring = radius + tubeRadius * cp;
            glm::vec3 position(ring * ct, tubeRadius * sp, ring * st);
            glm::vec3 normal(ct * cp, sp, st * cp);
            mesh->buffer.vertices.push_back(make_vertex(position, glm::normalize(normal), {u, v}));
        }
    }

    for (int i = 0; i < segments; ++i)
    {
        for (int j = 0; j < tubeSegments; ++j)
        {
            uint32_t a = i * stride + j;
            uint32_t b = (i + 1) * stride + j;
            uint32_t c = a + 1;
            uint32_t d = b + 1;

            mesh->buffer.indices.push_back(a);
            mesh->buffer.indices.push_back(b);
            mesh->buffer.indices.push_back(c);
            mesh->buffer.indices.push_back(c);
            mesh->buffer.indices.push_back(b);
            mesh->buffer.indices.push_back(d);
        }
    }

    finalize_procedural_mesh(mesh);
    cache[name] = mesh;
    return mesh;
}

Mesh *MeshManager::create_quad(const std::string &name, float width, float height)
{
    if (auto *existing = get(name))
        return existing;

    width = std::max(std::abs(width), 0.0001f);
    height = std::max(std::abs(height), 0.0001f);
    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;

    auto *mesh = new Mesh();
    mesh->name = name;
    mesh->buffer.vertices = {
        make_vertex({-halfWidth, -halfHeight, 0.0f}, {0, 0, 1}, {0, 0}),
        make_vertex({halfWidth, -halfHeight, 0.0f}, {0, 0, 1}, {1, 0}),
        make_vertex({halfWidth, halfHeight, 0.0f}, {0, 0, 1}, {1, 1}),
        make_vertex({-halfWidth, halfHeight, 0.0f}, {0, 0, 1}, {0, 1}),
    };
    mesh->buffer.indices = {0, 1, 2, 0, 2, 3};

    finalize_procedural_mesh(mesh);
    cache[name] = mesh;
    return mesh;
}

Mesh *MeshManager::create_circle(const std::string &name, float radius, int segments)
{
    if (auto *existing = get(name))
        return existing;

    radius = std::max(std::abs(radius), 0.0001f);
    segments = std::max(3, segments);

    auto *mesh = new Mesh();
    mesh->name = name;
    mesh->buffer.vertices.reserve(segments + 2);
    mesh->buffer.indices.reserve(segments * 3);

    mesh->buffer.vertices.push_back(make_vertex({0, 0, 0}, {0, 0, 1}, {0.5f, 0.5f}));
    for (int i = 0; i <= segments; ++i)
    {
        float u = static_cast<float>(i) / static_cast<float>(segments);
        float theta = u * kTwoPi;
        float c = std::cos(theta);
        float s = std::sin(theta);
        mesh->buffer.vertices.push_back(make_vertex(
            {radius * c, radius * s, 0.0f},
            {0, 0, 1},
            {0.5f + 0.5f * c, 0.5f + 0.5f * s}));
    }

    for (int i = 0; i < segments; ++i)
    {
        mesh->buffer.indices.push_back(0);
        mesh->buffer.indices.push_back(i + 1);
        mesh->buffer.indices.push_back(i + 2);
    }

    finalize_procedural_mesh(mesh);
    cache[name] = mesh;
    return mesh;
}

Mesh *MeshManager::create_cone(const std::string &name, float radius, float height, int segments)
{
    if (auto *existing = get(name))
        return existing;

    radius = std::max(std::abs(radius), 0.0001f);
    height = std::max(std::abs(height), 0.0001f);
    segments = std::max(3, segments);
    float halfHeight = height * 0.5f;
    float slope = radius / height;

    auto *mesh = new Mesh();
    mesh->name = name;
    mesh->buffer.vertices.reserve((segments + 1) * 3 + 1);
    mesh->buffer.indices.reserve(segments * 6);

    const uint32_t sideStart = (uint32_t)mesh->buffer.vertices.size();
    for (int i = 0; i <= segments; ++i)
    {
        float u = static_cast<float>(i) / static_cast<float>(segments);
        float theta = u * kTwoPi;
        float c = std::cos(theta);
        float s = std::sin(theta);
        glm::vec3 normal = glm::normalize(glm::vec3(c, slope, s));

        mesh->buffer.vertices.push_back(make_vertex({radius * c, -halfHeight, radius * s}, normal, {u, 0}));
        mesh->buffer.vertices.push_back(make_vertex({0.0f, halfHeight, 0.0f}, normal, {u, 1}));
    }

    for (int i = 0; i < segments; ++i)
    {
        uint32_t base = sideStart + i * 2;
        mesh->buffer.indices.push_back(base);
        mesh->buffer.indices.push_back(base + 1);
        mesh->buffer.indices.push_back(base + 2);
    }

    const uint32_t capCenter = (uint32_t)mesh->buffer.vertices.size();
    mesh->buffer.vertices.push_back(make_vertex({0, -halfHeight, 0}, {0, -1, 0}, {0.5f, 0.5f}));
    for (int i = 0; i <= segments; ++i)
    {
        float u = static_cast<float>(i) / static_cast<float>(segments);
        float theta = u * kTwoPi;
        float c = std::cos(theta);
        float s = std::sin(theta);
        mesh->buffer.vertices.push_back(make_vertex(
            {radius * c, -halfHeight, radius * s},
            {0, -1, 0},
            {0.5f + 0.5f * c, 0.5f + 0.5f * s}));
    }

    for (int i = 0; i < segments; ++i)
    {
        mesh->buffer.indices.push_back(capCenter);
        mesh->buffer.indices.push_back(capCenter + i + 2);
        mesh->buffer.indices.push_back(capCenter + i + 1);
    }

    finalize_procedural_mesh(mesh);
    cache[name] = mesh;
    return mesh;
}

Mesh *MeshManager::create_arrow(const std::string &name, float shaftRadius, float headRadius, float headLength, int segments)
{
    if (auto *existing = get(name))
        return existing;

    shaftRadius = std::max(std::abs(shaftRadius), 0.0001f);
    headRadius = std::max(std::abs(headRadius), shaftRadius);
    headLength = std::clamp(std::abs(headLength), 0.0001f, 0.95f);
    segments = std::max(3, segments);

    const float totalLength = 1.0f;
    const float shaftLength = totalLength - headLength;

    auto *mesh = new Mesh();
    mesh->name = name;
    mesh->buffer.vertices.reserve((segments + 1) * 5 + 3);
    mesh->buffer.indices.reserve(segments * 9);

    const uint32_t shaftStart = (uint32_t)mesh->buffer.vertices.size();
    for (int i = 0; i <= segments; ++i)
    {
        float u = static_cast<float>(i) / static_cast<float>(segments);
        float theta = u * kTwoPi;
        float c = std::cos(theta);
        float s = std::sin(theta);
        glm::vec3 normal(c, 0.0f, s);

        mesh->buffer.vertices.push_back(make_vertex({shaftRadius * c, 0.0f, shaftRadius * s}, normal, {u, 0}));
        mesh->buffer.vertices.push_back(make_vertex({shaftRadius * c, shaftLength, shaftRadius * s}, normal, {u, 1}));
    }

    for (int i = 0; i < segments; ++i)
    {
        uint32_t a = shaftStart + i * 2;
        uint32_t b = a + 1;
        uint32_t c = a + 2;
        uint32_t d = a + 3;

        mesh->buffer.indices.push_back(a);
        mesh->buffer.indices.push_back(b);
        mesh->buffer.indices.push_back(c);
        mesh->buffer.indices.push_back(c);
        mesh->buffer.indices.push_back(b);
        mesh->buffer.indices.push_back(d);
    }

    const uint32_t shaftCapCenter = (uint32_t)mesh->buffer.vertices.size();
    mesh->buffer.vertices.push_back(make_vertex({0, 0, 0}, {0, -1, 0}, {0.5f, 0.5f}));
    for (int i = 0; i <= segments; ++i)
    {
        float u = static_cast<float>(i) / static_cast<float>(segments);
        float theta = u * kTwoPi;
        float c = std::cos(theta);
        float s = std::sin(theta);
        mesh->buffer.vertices.push_back(make_vertex(
            {shaftRadius * c, 0.0f, shaftRadius * s},
            {0, -1, 0},
            {0.5f + 0.5f * c, 0.5f + 0.5f * s}));
    }

    for (int i = 0; i < segments; ++i)
    {
        mesh->buffer.indices.push_back(shaftCapCenter);
        mesh->buffer.indices.push_back(shaftCapCenter + i + 2);
        mesh->buffer.indices.push_back(shaftCapCenter + i + 1);
    }

    const uint32_t headStart = (uint32_t)mesh->buffer.vertices.size();
    float slope = headRadius / headLength;
    for (int i = 0; i <= segments; ++i)
    {
        float u = static_cast<float>(i) / static_cast<float>(segments);
        float theta = u * kTwoPi;
        float c = std::cos(theta);
        float s = std::sin(theta);
        glm::vec3 normal = glm::normalize(glm::vec3(c, slope, s));

        mesh->buffer.vertices.push_back(make_vertex({headRadius * c, shaftLength, headRadius * s}, normal, {u, 0}));
        mesh->buffer.vertices.push_back(make_vertex({0.0f, totalLength, 0.0f}, normal, {u, 1}));
    }

    for (int i = 0; i < segments; ++i)
    {
        uint32_t base = headStart + i * 2;
        mesh->buffer.indices.push_back(base);
        mesh->buffer.indices.push_back(base + 1);
        mesh->buffer.indices.push_back(base + 2);
    }

    finalize_procedural_mesh(mesh);
    cache[name] = mesh;
    return mesh;
}

// ============================================================
//  GLTF / GLB loader  (cgltf — static geometry)
// ============================================================
Mesh *MeshManager::load_gltf(const std::string &name, const std::string &path,
                              const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    cgltf_options opts{};
    cgltf_data   *data = nullptr;

    if (cgltf_parse_file(&opts, path.c_str(), &data) != cgltf_result_success)
    {
        LogError("[MeshManager] Failed to parse GLTF: %s", path.c_str());
        return nullptr;
    }
    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success)
    {
        LogError("[MeshManager] Failed to load GLTF buffers: %s", path.c_str());
        cgltf_free(data);
        return nullptr;
    }

    auto *mesh_out = new Mesh();
    mesh_out->name = name;

    for (cgltf_size mi = 0; mi < data->meshes_count; mi++)
    {
        cgltf_mesh &gm = data->meshes[mi];

        for (cgltf_size pi = 0; pi < gm.primitives_count; pi++)
        {
            cgltf_primitive &prim = gm.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles)
                continue;

            // Locate attribute accessors
            cgltf_accessor *pos_acc = nullptr;
            cgltf_accessor *nor_acc = nullptr;
            cgltf_accessor *uv_acc  = nullptr;

            for (cgltf_size ai = 0; ai < prim.attributes_count; ai++)
            {
                cgltf_attribute &attr = prim.attributes[ai];
                if (attr.type == cgltf_attribute_type_position)
                    pos_acc = attr.data;
                else if (attr.type == cgltf_attribute_type_normal)
                    nor_acc = attr.data;
                else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0)
                    uv_acc = attr.data;
            }
            if (!pos_acc)
                continue;

            uint32_t vert_offset  = (uint32_t)mesh_out->buffer.vertices.size();
            uint32_t index_start  = (uint32_t)mesh_out->buffer.indices.size();
            cgltf_size vc = pos_acc->count;

            mesh_out->buffer.vertices.resize(vert_offset + vc);

            for (cgltf_size vi = 0; vi < vc; vi++)
            {
                Vertex &v = mesh_out->buffer.vertices[vert_offset + vi];
                v.tangent = {1.0f, 0.0f, 0.0f, 1.0f};

                float tmp[4]{};
                cgltf_accessor_read_float(pos_acc, vi, tmp, 3);
                v.position = {tmp[0], tmp[1], tmp[2]};

                if (nor_acc)
                {
                    cgltf_accessor_read_float(nor_acc, vi, tmp, 3);
                    v.normal = {tmp[0], tmp[1], tmp[2]};
                }
                if (uv_acc)
                {
                    cgltf_accessor_read_float(uv_acc, vi, tmp, 2);
                    v.uv = {tmp[0], tmp[1]};
                }
            }

            // Build index buffer
            if (prim.indices)
            {
                cgltf_size ic = prim.indices->count;
                for (cgltf_size ii = 0; ii < ic; ii++)
                    mesh_out->buffer.indices.push_back(
                        vert_offset + (uint32_t)cgltf_accessor_read_index(prim.indices, ii));
                mesh_out->add_surface(index_start, (uint32_t)ic);
            }
            else
            {
                for (cgltf_size vi = 0; vi < vc; vi++)
                    mesh_out->buffer.indices.push_back(vert_offset + (uint32_t)vi);
                mesh_out->add_surface(index_start, (uint32_t)vc);
            }
        }
    }

    if (mesh_out->buffer.vertices.empty())
    {
        LogWarning("[MeshManager] GLTF has no geometry: %s", path.c_str());
        delete mesh_out;
        cgltf_free(data);
        return nullptr;
    }

    if (mesh_out->surfaces[0].index_count > 0)
        mesh_out->compute_tangents();

    mesh_out->upload();
    cgltf_free(data);

    LogInfo("[MeshManager] GLTF loaded: %s  verts=%zu  idx=%zu  surfaces=%zu",
                name.c_str(), mesh_out->buffer.vertices.size(),
                mesh_out->buffer.indices.size(), mesh_out->surfaces.size());

    cache[name] = mesh_out;
    return mesh_out;
}

// ============================================================
//  MeshManager::extract_submesh
// ============================================================
Mesh *MeshManager::extract_submesh(const std::string &new_name,
                                   Mesh              *src,
                                   const std::string &surface_name)
{
    if (!src || surface_name.empty())
        return nullptr;

    // Collect matching surfaces
    std::vector<const Surface *> matched;
    for (const Surface &s : src->surfaces)
        if (s.name == surface_name)
            matched.push_back(&s);

    if (matched.empty())
        return nullptr;

    Mesh *out = create(new_name);
    out->materials = src->materials; // share material pointers (not owned)

    // Build a compact vertex/index buffer from the matched surfaces.
    // Each surface's indices reference src->buffer.vertices; we remap them
    // into a fresh contiguous vertex array.
    std::unordered_map<uint32_t, uint32_t> remap; // old index -> new index

    for (const Surface *s : matched)
    {
        const uint32_t indexStart = (uint32_t)out->buffer.indices.size();
        const uint32_t end        = s->index_start + s->index_count;

        for (uint32_t i = s->index_start; i < end; ++i)
        {
            const uint32_t oldIdx = src->buffer.indices[i];
            auto it = remap.find(oldIdx);
            uint32_t newIdx;
            if (it == remap.end())
            {
                newIdx = (uint32_t)out->buffer.vertices.size();
                remap[oldIdx] = newIdx;
                out->buffer.vertices.push_back(src->buffer.vertices[oldIdx]);
            }
            else
            {
                newIdx = it->second;
            }
            out->buffer.indices.push_back(newIdx);
        }

        out->add_surface(indexStart,
                         (uint32_t)out->buffer.indices.size() - indexStart,
                         s->material_index,
                         surface_name);
    }

    out->compute_aabb();
    out->compute_surface_aabbs();
    out->buffer.upload();

    LogInfo("[MeshManager] Extracted submesh '%s' from '%s': verts=%d tris=%d surfaces=%d",
            new_name.c_str(), src->name.c_str(),
            (int)out->buffer.vertices.size(),
            (int)(out->buffer.indices.size() / 3),
            (int)out->surfaces.size());
    return out;
}

Mesh *MeshManager::extract_submesh(const std::string &new_name, Mesh *src, int surfceIndex)
{
    if (!src || surfceIndex < 0 || (size_t)surfceIndex >= src->surfaces.size())
        return nullptr;

    const Surface &s = src->surfaces[(size_t)surfceIndex];

    Mesh *out = create(new_name);
    out->materials = src->materials; // share material pointers (not owned)

    std::unordered_map<uint32_t, uint32_t> remap; // old index -> new index

    const uint32_t indexStart = (uint32_t)out->buffer.indices.size();
    const uint32_t end        = s.index_start + s.index_count;

    for (uint32_t i = s.index_start; i < end; ++i)
    {
        const uint32_t oldIdx = src->buffer.indices[i];
        auto it = remap.find(oldIdx);
        uint32_t newIdx;
        if (it == remap.end())
        {
            newIdx = (uint32_t)out->buffer.vertices.size();
            remap[oldIdx] = newIdx;
            out->buffer.vertices.push_back(src->buffer.vertices[oldIdx]);
        }
        else
        {
            newIdx = it->second;
        }
        out->buffer.indices.push_back(newIdx);
    }

    out->add_surface(indexStart,
                     (uint32_t)out->buffer.indices.size() - indexStart,
                     s.material_index,
                     s.name);

    out->compute_aabb();
    out->compute_surface_aabbs();
    out->buffer.upload();

    LogInfo("[MeshManager] Extracted submesh '%s' from '%s': verts=%d tris=%d surface='%s'",
            new_name.c_str(), src->name.c_str(),
            (int)out->buffer.vertices.size(),
            (int)(out->buffer.indices.size() / 3),
            s.name.c_str());
    return out;
}


AnimatedMesh *MeshManager::load_gltf_animated(const std::string &name,
                                              const std::string &path,
                                              const std::string &texture_dir,
                                              std::vector<Animation *> *outAnimations)
{
    AnimatedMesh *existing = getAnimated(name);
    if (existing)
    {
        if (outAnimations)
            outAnimations->clear();
        return existing;
    }

    if (outAnimations)
        outAnimations->clear();

    cgltf_options opts{};
    cgltf_data *data = nullptr;
    if (cgltf_parse_file(&opts, path.c_str(), &data) != cgltf_result_success)
    {
        LogError("[MeshManager] Failed to parse animated GLTF: %s", path.c_str());
        return nullptr;
    }
    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success)
    {
        LogError("[MeshManager] Failed to load animated GLTF buffers: %s", path.c_str());
        cgltf_free(data);
        return nullptr;
    }

    cgltf_skin *selectedSkin = nullptr;
    std::vector<cgltf_node *> sourceNodes;
    sourceNodes.reserve(data->nodes_count);

    for (cgltf_size i = 0; i < data->nodes_count; ++i)
    {
        cgltf_node &n = data->nodes[i];
        if (!n.mesh)
            continue;
        if (!n.skin || n.skin->joints_count == 0)
            continue;

        if (!selectedSkin)
            selectedSkin = n.skin;

        if (n.skin == selectedSkin)
            sourceNodes.push_back(&n);
    }

    if (!selectedSkin && data->skins_count > 0 && data->skins[0].joints_count > 0)
        selectedSkin = &data->skins[0];

    if (selectedSkin && sourceNodes.empty())
    {
        for (cgltf_size i = 0; i < data->nodes_count; ++i)
        {
            cgltf_node &n = data->nodes[i];
            if (n.mesh && n.skin == selectedSkin)
                sourceNodes.push_back(&n);
        }
    }

    if (!selectedSkin || selectedSkin->joints_count == 0)
    {
        LogError("[MeshManager] Animated GLTF has no valid skin/joints: %s", path.c_str());
        cgltf_free(data);
        return nullptr;
    }

    auto *mesh_out = new AnimatedMesh();
    mesh_out->name = name;

    const int jointCount = static_cast<int>(selectedSkin->joints_count);
    mesh_out->bones.resize(static_cast<size_t>(jointCount));

    std::unordered_map<const cgltf_node *, int> boneByNode;
    boneByNode.reserve(static_cast<size_t>(jointCount));
    std::vector<glm::mat4> globalBind(static_cast<size_t>(jointCount), glm::mat4(1.0f));

    for (int i = 0; i < jointCount; ++i)
    {
        const cgltf_node *jointNode = selectedSkin->joints[static_cast<cgltf_size>(i)];
        if (!jointNode)
            continue;
        boneByNode[jointNode] = i;

        float m[16] = {0};
        cgltf_node_transform_world(jointNode, m);
        globalBind[static_cast<size_t>(i)] = cgltfMatToGlm(m);
    }

    for (int i = 0; i < jointCount; ++i)
    {
        const cgltf_node *jointNode = selectedSkin->joints[static_cast<cgltf_size>(i)];
        Bone b;
        b.name = (jointNode && jointNode->name) ? jointNode->name : ("joint_" + std::to_string(i));

        int parentIndex = -1;
        if (jointNode)
        {
            const cgltf_node *p = jointNode->parent;
            while (p)
            {
                auto it = boneByNode.find(p);
                if (it != boneByNode.end())
                {
                    parentIndex = it->second;
                    break;
                }
                p = p->parent;
            }
        }
        b.parent = parentIndex;

        const glm::mat4 g = globalBind[static_cast<size_t>(i)];
        if (parentIndex >= 0)
        {
            const glm::mat4 gp = globalBind[static_cast<size_t>(parentIndex)];
            b.localPose = glm::inverse(gp) * g;
        }
        else
            b.localPose = g;

        b.offset = glm::inverse(g);
        mesh_out->bones[static_cast<size_t>(i)] = std::move(b);
    }

    if (selectedSkin->inverse_bind_matrices &&
        selectedSkin->inverse_bind_matrices->count >= selectedSkin->joints_count)
    {
        for (int i = 0; i < jointCount; ++i)
        {
            float ibm[16] = {0};
            if (cgltf_accessor_read_float(selectedSkin->inverse_bind_matrices,
                                          static_cast<cgltf_size>(i),
                                          ibm, 16))
            {
                mesh_out->bones[static_cast<size_t>(i)].offset = cgltfMatToGlm(ibm);
            }
        }
    }

    const int maxShaderBones = 100;
    const bool clampBoneIds = static_cast<int>(mesh_out->bones.size()) > maxShaderBones;
    if (clampBoneIds)
    {
        LogWarning("[MeshManager] GLTF '%s' has %zu bones, shader supports %d; clamping IDs",
                    path.c_str(), mesh_out->bones.size(), maxShaderBones);
    }

    Texture *white = TextureManager::instance().getWhite();
    int surfaceCounter = 0;

    for (cgltf_node *node : sourceNodes)
    {
        if (!node || !node->mesh)
            continue;
        if (node->skin && node->skin != selectedSkin)
            continue;

        cgltf_mesh &gm = *node->mesh;
        for (cgltf_size pi = 0; pi < gm.primitives_count; ++pi)
        {
            cgltf_primitive &prim = gm.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles)
                continue;

            cgltf_accessor *posAcc = nullptr;
            cgltf_accessor *norAcc = nullptr;
            cgltf_accessor *uvAcc = nullptr;
            cgltf_accessor *jointsAcc = nullptr;
            cgltf_accessor *weightsAcc = nullptr;

            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai)
            {
                cgltf_attribute &attr = prim.attributes[ai];
                if (attr.type == cgltf_attribute_type_position)
                    posAcc = attr.data;
                else if (attr.type == cgltf_attribute_type_normal)
                    norAcc = attr.data;
                else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0)
                    uvAcc = attr.data;
                else if (attr.type == cgltf_attribute_type_joints && attr.index == 0)
                    jointsAcc = attr.data;
                else if (attr.type == cgltf_attribute_type_weights && attr.index == 0)
                    weightsAcc = attr.data;
            }
            if (!posAcc)
                continue;

            const cgltf_size vertexCount = posAcc->count;
            const uint32_t vertOffset = static_cast<uint32_t>(mesh_out->buffer.vertices.size());
            const uint32_t indexStart = static_cast<uint32_t>(mesh_out->buffer.indices.size());
            mesh_out->buffer.vertices.resize(static_cast<size_t>(vertOffset) + static_cast<size_t>(vertexCount));

            for (cgltf_size vi = 0; vi < vertexCount; ++vi)
            {
                AnimatedVertex v{};
                v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                v.uv = glm::vec2(0.0f);
                v.boneIds = glm::ivec4(0);
                v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);

                float tmp4[4] = {0.f, 0.f, 0.f, 1.f};
                cgltf_accessor_read_float(posAcc, vi, tmp4, 3);
                v.position = glm::vec3(tmp4[0], tmp4[1], tmp4[2]);

                if (norAcc && cgltf_accessor_read_float(norAcc, vi, tmp4, 3))
                {
                    glm::vec3 n(tmp4[0], tmp4[1], tmp4[2]);
                    const float n2 = glm::dot(n, n);
                    v.normal = (n2 > 1e-10f) ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
                }

                if (uvAcc && cgltf_accessor_read_float(uvAcc, vi, tmp4, 2))
                    v.uv = glm::vec2(tmp4[0], tmp4[1]);

                if (jointsAcc && weightsAcc)
                {
                    cgltf_uint j[4] = {0, 0, 0, 0};
                    float w[4] = {0.f, 0.f, 0.f, 0.f};
                    cgltf_accessor_read_uint(jointsAcc, vi, j, 4);
                    cgltf_accessor_read_float(weightsAcc, vi, w, 4);

                    int b[4] = {0, 0, 0, 0};
                    for (int k = 0; k < 4; ++k)
                    {
                        const uint32_t ji = static_cast<uint32_t>(j[k]);
                        b[k] = (ji < static_cast<uint32_t>(jointCount)) ? static_cast<int>(ji) : 0;
                        if (clampBoneIds)
                            b[k] = std::min(b[k], maxShaderBones - 1);
                    }

                    v.boneIds = glm::ivec4(b[0], b[1], b[2], b[3]);
                    glm::vec4 ww(w[0], w[1], w[2], w[3]);
                    const float sum = ww.x + ww.y + ww.z + ww.w;
                    if (sum > 1e-8f)
                        v.boneWeights = ww / sum;
                }

                mesh_out->buffer.vertices[static_cast<size_t>(vertOffset) + static_cast<size_t>(vi)] = v;
            }

            if (prim.indices)
            {
                const cgltf_size ic = prim.indices->count;
                for (cgltf_size ii = 0; ii < ic; ++ii)
                {
                    const uint32_t idx = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, ii));
                    mesh_out->buffer.indices.push_back(vertOffset + idx);
                }
            }
            else
            {
                for (cgltf_size vi = 0; vi < vertexCount; ++vi)
                    mesh_out->buffer.indices.push_back(vertOffset + static_cast<uint32_t>(vi));
            }

            const uint32_t indexCount = static_cast<uint32_t>(mesh_out->buffer.indices.size()) - indexStart;

            const std::string matId = name + "::gltf_mat_" + std::to_string(surfaceCounter++);
            Material *mat = MaterialManager::instance().create(matId);
            mat->setTexture("u_albedo", white);
            mat->setVec4("u_color", glm::vec4(1.0f));

            if (prim.material)
            {
                mat->setCullFace(!prim.material->double_sided);

                const auto &pbr = prim.material->pbr_metallic_roughness;
                mat->setVec4("u_color",
                             glm::vec4(pbr.base_color_factor[0],
                                       pbr.base_color_factor[1],
                                       pbr.base_color_factor[2],
                                       pbr.base_color_factor[3]));

                if (prim.material->alpha_mode == cgltf_alpha_mode_blend)
                {
                    mat->setBlend(true);
                    mat->setDepthWrite(false);
                }

                if (pbr.base_color_texture.texture &&
                    pbr.base_color_texture.texture->image &&
                    pbr.base_color_texture.texture->image->uri)
                {
                    const std::string resolved =
                        resolveGltfTexturePath(pbr.base_color_texture.texture->image->uri,
                                               path, texture_dir);
                    if (!resolved.empty())
                    {
                        Texture *tex = TextureManager::instance().load(matId + "::albedo", resolved);
                        if (tex)
                            mat->setTexture("u_albedo", tex);
                    }
                }
            }

            const int matIndex = mesh_out->add_material(mat);
            if (indexCount > 0)
                mesh_out->add_surface(indexStart, indexCount, matIndex);
        }
    }

    if (mesh_out->buffer.vertices.empty() || mesh_out->buffer.indices.empty() || mesh_out->surfaces.empty())
    {
        LogError("[MeshManager] Animated GLTF has no skinned geometry: %s", path.c_str());
        delete mesh_out;
        cgltf_free(data);
        return nullptr;
    }

    if (!mesh_out->buffer.indices.empty() && !mesh_out->buffer.vertices.empty())
        mesh_out->compute_tangents();

    mesh_out->finalMatrices.resize(mesh_out->bones.size(), glm::mat4(1.0f));
    mesh_out->upload();
    MaterialManager::instance().applyDefaults();

    if (outAnimations)
    {
        for (cgltf_size ai = 0; ai < data->animations_count; ++ai)
        {
            cgltf_animation &ga = data->animations[ai];

            auto *anim = new Animation();
            anim->name = (ga.name && ga.name[0]) ? ga.name : ("anim_" + std::to_string(ai));
            anim->ticksPerSecond = 1.0f; // GLTF key times are in seconds.
            anim->duration = 0.0f;
            anim->channels.resize(mesh_out->bones.size());

            std::vector<glm::vec3> defaultPosByBone(mesh_out->bones.size(), glm::vec3(0.0f));
            std::vector<glm::vec3> defaultScaleByBone(mesh_out->bones.size(), glm::vec3(1.0f));
            std::vector<glm::quat> defaultRotByBone(mesh_out->bones.size(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));

            for (size_t bi = 0; bi < mesh_out->bones.size(); ++bi)
            {
                AnimationChannel &ch = anim->channels[bi];
                ch.boneName = mesh_out->bones[bi].name;

                glm::vec3 basePos(0.0f);
                glm::vec3 baseScale(1.0f);
                glm::quat baseRot(1.0f, 0.0f, 0.0f, 0.0f);
                glm::vec3 skew;
                glm::vec4 persp;
                if (!glm::decompose(mesh_out->bones[bi].localPose,
                                    baseScale, baseRot, basePos, skew, persp))
                {
                    basePos = glm::vec3(0.0f);
                    baseScale = glm::vec3(1.0f);
                    baseRot = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                }
                else
                {
                    baseRot = glm::conjugate(baseRot);
                }

                defaultPosByBone[bi] = basePos;
                defaultScaleByBone[bi] = baseScale;
                defaultRotByBone[bi] = glm::normalize(baseRot);
            }

            for (cgltf_size ci = 0; ci < ga.channels_count; ++ci)
            {
                const cgltf_animation_channel &gch = ga.channels[ci];
                if (!gch.target_node || !gch.sampler)
                    continue;
                if (gch.target_path == cgltf_animation_path_type_weights)
                    continue;

                auto itBone = boneByNode.find(gch.target_node);
                if (itBone == boneByNode.end())
                    continue;
                const int boneIndex = itBone->second;
                if (boneIndex < 0 || boneIndex >= static_cast<int>(anim->channels.size()))
                    continue;

                const cgltf_animation_sampler *sampler = gch.sampler;
                const cgltf_accessor *inAcc = sampler->input;
                const cgltf_accessor *outAcc = sampler->output;
                if (!inAcc || !outAcc || inAcc->count == 0)
                    continue;

                cgltf_size keyCount = inAcc->count;
                if (sampler->interpolation == cgltf_interpolation_type_cubic_spline)
                    keyCount = std::min(keyCount, outAcc->count / 3u);
                else
                    keyCount = std::min(keyCount, outAcc->count);

                AnimationChannel &dst = anim->channels[static_cast<size_t>(boneIndex)];

                for (cgltf_size k = 0; k < keyCount; ++k)
                {
                    float t = 0.0f;
                    if (!cgltf_accessor_read_float(inAcc, k, &t, 1))
                        continue;
                    anim->duration = std::max(anim->duration, t);

                    const cgltf_size sampleIdx =
                        (sampler->interpolation == cgltf_interpolation_type_cubic_spline) ? (k * 3u + 1u) : k;

                    float v4[4] = {0.f, 0.f, 0.f, 1.f};
                    if (gch.target_path == cgltf_animation_path_type_translation)
                    {
                        if (cgltf_accessor_read_float(outAcc, sampleIdx, v4, 3))
                            dst.posKeys.push_back({t, glm::vec3(v4[0], v4[1], v4[2])});
                    }
                    else if (gch.target_path == cgltf_animation_path_type_rotation)
                    {
                        if (cgltf_accessor_read_float(outAcc, sampleIdx, v4, 4))
                        {
                            glm::quat q(v4[3], v4[0], v4[1], v4[2]); // glTF is xyzw
                            const float q2 = glm::dot(q, q);
                            if (q2 > 1e-10f)
                                q = glm::normalize(q);
                            else
                                q = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                            dst.rotKeys.push_back({t, q});
                        }
                    }
                    else if (gch.target_path == cgltf_animation_path_type_scale)
                    {
                        if (cgltf_accessor_read_float(outAcc, sampleIdx, v4, 3))
                            dst.scaleKeys.push_back({t, glm::vec3(v4[0], v4[1], v4[2])});
                    }
                }
            }

            for (AnimationChannel &ch : anim->channels)
            {
                std::sort(ch.posKeys.begin(), ch.posKeys.end(), posKeyLess);
                std::sort(ch.rotKeys.begin(), ch.rotKeys.end(), rotKeyLess);
                std::sort(ch.scaleKeys.begin(), ch.scaleKeys.end(), scaleKeyLess);
            }

            for (size_t bi = 0; bi < anim->channels.size(); ++bi)
            {
                AnimationChannel &ch = anim->channels[bi];
                // Keep importer deterministic and avoid "extra frame"/pop at clip start:
                // never inject bind-pose keys when real keys already exist at t=0.

                if (ch.posKeys.empty())
                {
                    ch.posKeys.push_back({0.0f, defaultPosByBone[bi]});
                }
                else if (ch.posKeys.front().time > 0.0f)
                {
                    ch.posKeys.insert(ch.posKeys.begin(), {0.0f, ch.posKeys.front().value});
                }

                if (ch.rotKeys.empty())
                {
                    ch.rotKeys.push_back({0.0f, defaultRotByBone[bi]});
                }
                else if (ch.rotKeys.front().time > 0.0f)
                {
                    ch.rotKeys.insert(ch.rotKeys.begin(), {0.0f, ch.rotKeys.front().value});
                }

                if (ch.scaleKeys.empty())
                {
                    ch.scaleKeys.push_back({0.0f, defaultScaleByBone[bi]});
                }
                else if (ch.scaleKeys.front().time > 0.0f)
                {
                    ch.scaleKeys.insert(ch.scaleKeys.begin(), {0.0f, ch.scaleKeys.front().value});
                }
            }

            if (anim->duration <= 0.0f)
                anim->duration = 1.0f;

            outAnimations->push_back(anim);
        }
    }

    cgltf_free(data);

    animatedCache_[name] = mesh_out;

    LogInfo("[MeshManager] Animated GLTF loaded: %s  verts=%zu  idx=%zu  bones=%zu  surfs=%zu  anims=%zu",
                name.c_str(),
                mesh_out->buffer.vertices.size(),
                mesh_out->buffer.indices.size(),
                mesh_out->bones.size(),
                mesh_out->surfaces.size(),
                outAnimations ? outAnimations->size() : 0u);

    return mesh_out;
}

// ============================================================
//  MeshManager — primitivas
// ============================================================
Mesh *MeshManager::create_cube(const std::string &name, float s)
{
    if (auto *existing = get(name))
        return existing;

    float h = s * 0.5f;

    struct Face
    {
        glm::vec3 n;
        glm::vec3 c[4];
    };
    Face faces[6] = {
        {{0, 0, 1}, {{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}}},
        {{0, 0, -1}, {{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}}},
        {{0, 1, 0}, {{-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}}},
        {{0, -1, 0}, {{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}}},
        {{1, 0, 0}, {{h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}}},
        {{-1, 0, 0}, {{-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}}},
    };
    glm::vec2 fuvs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    auto *mesh = new Mesh();
    mesh->name = name;

    mesh->buffer.vertices.resize(24);
    mesh->buffer.indices.resize(36);

    for (int f = 0; f < 6; f++)
    {
        for (int v = 0; v < 4; v++)
        {
            int i = f * 4 + v;
            mesh->buffer.vertices[i] = {
                faces[f].c[v],
                faces[f].n,
                glm::vec4(1, 0, 0, 1),
                fuvs[v]};
        }
        uint32_t b = f * 4;
        int idx = f * 6;
        mesh->buffer.indices[idx + 0] = b;
        mesh->buffer.indices[idx + 1] = b + 1;
        mesh->buffer.indices[idx + 2] = b + 2;
        mesh->buffer.indices[idx + 3] = b;
        mesh->buffer.indices[idx + 4] = b + 2;
        mesh->buffer.indices[idx + 5] = b + 3;
    }

    mesh->buffer.upload();
    mesh->compute_aabb();
    mesh->add_surface(0, 36);
    mesh->add_material(MaterialManager::instance().create(name));

    cache[name] = mesh;
    return mesh;
}

Mesh *MeshManager::create_wire_cube(const std::string &name, float s)
{
    if (auto *existing = get(name))
        return existing;

    float h = s * 0.5f;
    // 8 cantos
    glm::vec3 pos[8] = {
        {-h, -h, -h}, { h, -h, -h}, { h,  h, -h}, {-h,  h, -h}, // Back
        {-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h}  // Front
    };

    auto *mesh = new Mesh();
    mesh->name = name;
    mesh->buffer.mode = GL_LINES; // Importante: desenhar linhas
    mesh->buffer.vertices.resize(8);
    
    for(int i=0; i<8; ++i) {
        mesh->buffer.vertices[i].position = pos[i];
        mesh->buffer.vertices[i].normal = glm::vec3(0,1,0); // Dummy
        mesh->buffer.vertices[i].uv = glm::vec2(0);
    }

    // 12 arestas (2 indices por aresta)
    mesh->buffer.indices = {
        0,1, 1,2, 2,3, 3,0, // Back face
        4,5, 5,6, 6,7, 7,4, // Front face
        0,4, 1,5, 2,6, 3,7  // Connecting lines
    };

    mesh->buffer.upload();
    mesh->compute_aabb(); // AABB do próprio wireframe
    mesh->add_surface(0, 24);

    cache[name] = mesh;
    return mesh;
}

Mesh *MeshManager::create_plane(const std::string &name, float width, float depth, int subdivs)
{
    if (auto *existing = get(name))
        return existing;

    subdivs = glm::max(1, subdivs);
    int verts_x = subdivs + 1;
    int verts_z = subdivs + 1;
    float step_x = width / subdivs;
    float step_z = depth / subdivs;
    float half_x = width * 0.5f;
    float half_z = depth * 0.5f;

    int vert_count = verts_x * verts_z;
    int idx_count = subdivs * subdivs * 6;

    auto *mesh = new Mesh();
    mesh->name = name;
    mesh->buffer.vertices.resize(vert_count);
    mesh->buffer.indices.resize(idx_count);

    // vértices
    for (int z = 0; z < verts_z; z++)
    {
        for (int x = 0; x < verts_x; x++)
        {
            int i = z * verts_x + x;
            mesh->buffer.vertices[i] = {
                {-half_x + x * step_x, 0.0f, -half_z + z * step_z},
                {0, 1, 0},
                {1, 0, 0, 1},
                {(float)x / subdivs, (float)z / subdivs}};
        }
    }

    // índices
    int idx = 0;
    for (int z = 0; z < subdivs; z++)
    {
        for (int x = 0; x < subdivs; x++)
        {
            uint32_t tl = z * verts_x + x;
            uint32_t tr = tl + 1;
            uint32_t bl = tl + verts_x;
            uint32_t br = bl + 1;
            mesh->buffer.indices[idx++] = tl;
            mesh->buffer.indices[idx++] = bl;
            mesh->buffer.indices[idx++] = tr;
            mesh->buffer.indices[idx++] = tr;
            mesh->buffer.indices[idx++] = bl;
            mesh->buffer.indices[idx++] = br;
        }
    }

    mesh->buffer.upload();
    mesh->compute_aabb();
    mesh->add_surface(0, (uint32_t)idx_count);
    mesh->add_material(MaterialManager::instance().create(name));

    cache[name] = mesh;
    return mesh;
}

// ============================================================
//  H3D / .mesh — binary format via MeshReader
// ============================================================
Mesh *MeshManager::load_h3d(const std::string &name, const std::string &path,
                             const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    auto *mesh = new Mesh();
    mesh->name = name;

    MeshReader reader;
    reader.textureDir = texture_dir;

    if (!reader.load(path, mesh))
    {
        LogError("[MeshManager] Failed to load h3d '%s': %s",
                     name.c_str(), path.c_str());
        delete mesh;
        return nullptr;
    }

    MaterialManager::instance().applyDefaults();

    cache[name] = mesh;
    return mesh;
 }
 

// ============================================================
//  3DS loader — static mesh, materials + UVs
// ============================================================
Mesh *MeshManager::load_3ds(const std::string &name, const std::string &path,
                            const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    struct Mat3DS
    {
        std::string name;
        std::string textureFile;
        glm::vec3 diffuse;
        float opacity;
        Mat3DS() : diffuse(0.8f, 0.8f, 0.8f), opacity(1.0f) {}
    };

    struct Face3DS
    {
        uint16_t a, b, c;
        std::string matName;
    };

    struct Obj3DS
    {
        struct FaceGroup
        {
            std::string matName;
            std::vector<uint16_t> faceIndices;
        };

        std::string name;
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec2> uvs;
        std::vector<Face3DS> faces;
        std::vector<FaceGroup> faceGroups;
        float transform[12];
        bool hasTransform;

        Obj3DS() : hasTransform(false)
        {
            for (int i = 0; i < 12; ++i)
                transform[i] = 0.0f;
            transform[0] = 1.0f;
            transform[4] = 1.0f;
            transform[8] = 1.0f;
        }
    };

    struct Chunk3DS
    {
        uint16_t id;
        uint32_t length;
        Sint64 end;
    };

    const uint16_t CHUNK_MAIN      = 0x4D4D;
    const uint16_t CHUNK_EDIT      = 0x3D3D;
    const uint16_t CHUNK_KEYF3DS   = 0xB000;
    const uint16_t CHUNK_OBJECT    = 0x4000;
    const uint16_t CHUNK_TRIMESH   = 0x4100;
    const uint16_t CHUNK_VERTLIST  = 0x4110;
    const uint16_t CHUNK_FACELIST  = 0x4120;
    const uint16_t CHUNK_FACEMAT   = 0x4130;
    const uint16_t CHUNK_MAPLIST   = 0x4140;
    const uint16_t CHUNK_TRMATRIX  = 0x4160;
    const uint16_t CHUNK_OBJECT_TAG = 0xB002;
    const uint16_t CHUNK_KF_NODE_HDR = 0xB010;
    const uint16_t CHUNK_KF_NODE_ID  = 0xB030;
    const uint16_t CHUNK_PIVOTPOINT = 0xB013;
    const uint16_t CHUNK_KF_POS    = 0xB020;
    const uint16_t CHUNK_KF_ROT    = 0xB021;
    const uint16_t CHUNK_KF_SCL    = 0xB022;
    const uint16_t CHUNK_MATERIAL  = 0xAFFF;
    const uint16_t CHUNK_MATNAME   = 0xA000;
    const uint16_t CHUNK_DIFFUSE   = 0xA020;
    const uint16_t CHUNK_OPACITY   = 0xA050;
    const uint16_t CHUNK_TEXTURE   = 0xA200;
    const uint16_t CHUNK_MAPFILE   = 0xA300;
    const uint16_t CHUNK_RGBF      = 0x0010;
    const uint16_t CHUNK_RGBB      = 0x0011;
    const uint16_t CHUNK_PERCENTI  = 0x0030;
    const uint16_t CHUNK_PERCENTF  = 0x0031;

    SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw)
    {
        LogError("[MeshManager] Cannot open 3DS: %s (%s)", path.c_str(), SDL_GetError());
        return nullptr;
    }

    auto readU16 = [&](uint16_t *out) -> bool
    {
        uint16_t v = 0;
        if (SDL_RWread(rw, &v, sizeof(v), 1) != 1)
            return false;
        *out = SDL_SwapLE16(v);
        return true;
    };

    auto readU32 = [&](uint32_t *out) -> bool
    {
        uint32_t v = 0;
        if (SDL_RWread(rw, &v, sizeof(v), 1) != 1)
            return false;
        *out = SDL_SwapLE32(v);
        return true;
    };

    auto readF32 = [&](float *out) -> bool
    {
        uint32_t bits = 0;
        if (!readU32(&bits))
            return false;
        std::memcpy(out, &bits, sizeof(float));
        return true;
    };

    auto readString = [&](std::string *out) -> bool
    {
        out->clear();
        while (1)
        {
            char c = 0;
            if (SDL_RWread(rw, &c, 1, 1) != 1)
                return false;
            if (c == '\0')
                break;
            out->push_back(c);
        }
        
        return true;
    };

    auto readChunk = [&](Chunk3DS *ch) -> bool
    {
        Sint64 start = SDL_RWtell(rw);
        if (start < 0)
            return false;
        if (!readU16(&ch->id) || !readU32(&ch->length))
            return false;
        ch->end = start + (Sint64)ch->length;
        return true;
    };

    auto skipTo = [&](Sint64 pos)
    {
        SDL_RWseek(rw, pos, RW_SEEK_SET);
    };

    auto parseColor = [&](Sint64 chunkEnd, glm::vec3 *outColor)
    {
        while (SDL_RWtell(rw) < chunkEnd)
        {
            Chunk3DS c;
            if (!readChunk(&c))
                break;
            if (c.id == CHUNK_RGBF)
            {
                float r = 0.f, g = 0.f, b = 0.f;
                if (readF32(&r) && readF32(&g) && readF32(&b))
                    *outColor = glm::vec3(r, g, b);
            }
            else if (c.id == CHUNK_RGBB)
            {
                unsigned char rgb[3] = {0, 0, 0};
                if (SDL_RWread(rw, rgb, 1, 3) == 3)
                    *outColor = glm::vec3(rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f);
            }
            skipTo(c.end);
        }
    };

    auto parseTexture = [&](Sint64 chunkEnd, std::string *texFile)
    {
        while (SDL_RWtell(rw) < chunkEnd)
        {
            Chunk3DS c;
            if (!readChunk(&c))
                break;
            if (c.id == CHUNK_MAPFILE)
                readString(texFile);
            skipTo(c.end);
        }
    };

    auto parsePercentage = [&](Sint64 chunkEnd, float *outValue)
    {
        while (SDL_RWtell(rw) < chunkEnd)
        {
            Chunk3DS c;
            if (!readChunk(&c))
                break;

            if (c.id == CHUNK_PERCENTI)
            {
                uint16_t v = 0;
                if (readU16(&v))
                    *outValue = static_cast<float>(v) / 100.0f;
            }
            else if (c.id == CHUNK_PERCENTF)
            {
                float v = 0.0f;
                if (readF32(&v))
                    *outValue = v;
            }

            skipTo(c.end);
        }
    };

    struct KFNode3DS
    {
        glm::vec3 pivot  = glm::vec3(0.f);
        glm::vec3 pos    = glm::vec3(0.f);
        glm::quat rot    = glm::quat(1.f, 0.f, 0.f, 0.f);
        glm::vec3 scale  = glm::vec3(1.f);
        int       nodeId = -1;
        int       parentId = -1;
        bool      hasPos = false; // true se POS track presente e não vazio
    };

    std::vector<Mat3DS> materials3ds;
    std::vector<Obj3DS> objects3ds;
    std::unordered_map<std::string, std::vector<KFNode3DS>> kfNodes;

    auto parseFaceList = [&](Sint64 chunkEnd, Obj3DS &obj)
    {
        uint16_t faceCount = 0;
        if (!readU16(&faceCount))
            return;

        obj.faces.resize(faceCount);
        for (uint16_t i = 0; i < faceCount; ++i)
        {
            uint16_t a = 0, b = 0, c = 0, flags = 0;
            if (!readU16(&a) || !readU16(&b) || !readU16(&c) || !readU16(&flags))
                break;
            obj.faces[i].a = a;
            obj.faces[i].b = b;
            obj.faces[i].c = c;
        }

        while (SDL_RWtell(rw) < chunkEnd)
        {
            Chunk3DS c;
            if (!readChunk(&c))
                break;
            if (c.id == CHUNK_FACEMAT)
            {
                std::string matName;
                if (!readString(&matName))
                {
                    skipTo(c.end);
                    continue;
                }

                uint16_t cnt = 0;
                if (!readU16(&cnt))
                {
                    skipTo(c.end);
                    continue;
                }

                Obj3DS::FaceGroup group;
                group.matName = matName;
                group.faceIndices.reserve(cnt);

                for (uint16_t i = 0; i < cnt; ++i)
                {
                    uint16_t faceIndex = 0;
                    if (!readU16(&faceIndex))
                        break;
                    if (faceIndex < obj.faces.size())
                    {
                        obj.faces[faceIndex].matName = matName;
                        group.faceIndices.push_back(faceIndex);
                    }
                }

                if (!group.faceIndices.empty())
                    obj.faceGroups.push_back(group);
            }
            skipTo(c.end);
        }
    };

    auto parseTriMesh = [&](Sint64 chunkEnd, Obj3DS &obj)
    {
        while (SDL_RWtell(rw) < chunkEnd)
        {
            Chunk3DS c;
            if (!readChunk(&c))
                break;

            if (c.id == CHUNK_VERTLIST)
            {
                uint16_t count = 0;
                if (!readU16(&count))
                {
                    skipTo(c.end);
                    continue;
                }

                obj.positions.resize(count);
                for (uint16_t i = 0; i < count; ++i)
                {
                    float x = 0.f, y = 0.f, z = 0.f;
                    if (!readF32(&x) || !readF32(&y) || !readF32(&z))
                        break;
                    // Keep raw 3DS local coordinates; convert after optional TRIMATRIX.
                    obj.positions[i] = glm::vec3(x, y, z);
                }
            }
            else if (c.id == CHUNK_MAPLIST)
            {
                uint16_t count = 0;
                if (!readU16(&count))
                {
                    skipTo(c.end);
                    continue;
                }

                obj.uvs.resize(count);
                for (uint16_t i = 0; i < count; ++i)
                {
                    float u = 0.f, v = 0.f;
                    if (!readF32(&u) || !readF32(&v))
                        break;
                    obj.uvs[i] = glm::vec2(u, 1.0f - v);
                }
            }
            else if (c.id == CHUNK_FACELIST)
            {
                parseFaceList(c.end, obj);
            }
            else if (c.id == CHUNK_TRMATRIX)
            {
                float tm[12];
                for (int i = 0; i < 12; ++i)
                {
                    if (!readF32(&tm[i]))
                        break;
                }

                for (int i = 0; i < 12; ++i)
                    obj.transform[i] = tm[i];
                obj.hasTransform = true;
            }

            skipTo(c.end);
        }

    };

    auto parseMaterial = [&](Sint64 chunkEnd)
    {
        Mat3DS mat;
        while (SDL_RWtell(rw) < chunkEnd)
        {
            Chunk3DS c;
            if (!readChunk(&c))
                break;
            if (c.id == CHUNK_MATNAME)
                readString(&mat.name);
            else if (c.id == CHUNK_DIFFUSE)
                parseColor(c.end, &mat.diffuse);
            else if (c.id == CHUNK_OPACITY)
            {
                float transparency = 0.0f;
                parsePercentage(c.end, &transparency);
                mat.opacity = 1.0f - transparency;
            }
            else if (c.id == CHUNK_TEXTURE)
                parseTexture(c.end, &mat.textureFile);

            skipTo(c.end);
        }

        materials3ds.push_back(mat);
    };

    auto parseObjectTag = [&](Sint64 chunkEnd)
    {
        std::string nodeName;
        KFNode3DS node;

        while (SDL_RWtell(rw) < chunkEnd)
        {
            Chunk3DS c;
            if (!readChunk(&c))
                break;

            if (c.id == CHUNK_KF_NODE_HDR)
            {
                readString(&nodeName);
                uint32_t flags = 0;
                uint16_t parent = 0xFFFF;
                readU32(&flags);
                readU16(&parent);
                node.parentId = (parent == 0xFFFF) ? -1 : static_cast<int>(parent);
            }
            else if (c.id == CHUNK_KF_NODE_ID)
            {
                uint16_t id = 0xFFFF;
                if (readU16(&id))
                    node.nodeId = (id == 0xFFFF) ? -1 : static_cast<int>(id);
            }
            else if (c.id == CHUNK_PIVOTPOINT)
            {
                float x = 0.f, y = 0.f, z = 0.f;
                if (readF32(&x) && readF32(&y) && readF32(&z))
                    node.pivot = glm::vec3(x, y, z);
            }
            else if (c.id == CHUNK_KF_POS)
            {
                SDL_RWseek(rw, 10, RW_SEEK_CUR);
                uint32_t keyNum = 0;
                if (readU32(&keyNum) && keyNum > 0)
                {
                    uint32_t frame = 0; readU32(&frame);
                    uint16_t fl = 0; readU16(&fl);
                    // Align with the reference loader: if spline flags are present,
                    // there is a single extra float before the vector payload.
                    if (fl != 0) { float _d = 0.f; readF32(&_d); }
                    float x=0.f, y=0.f, z=0.f;
                    if (readF32(&x) && readF32(&y) && readF32(&z))
                    {
                        node.pos    = glm::vec3(x, y, z);
                        node.hasPos = true;
                    }
                }
            }
            else if (c.id == CHUNK_KF_ROT)
            {
                SDL_RWseek(rw, 10, RW_SEEK_CUR);
                uint32_t keyNum = 0;
                if (readU32(&keyNum) && keyNum > 0)
                {
                    uint32_t frame = 0; readU32(&frame);
                    uint16_t fl = 0; readU16(&fl);
                    if (fl != 0) { float _d = 0.f; readF32(&_d); }
                    float angle=0.f, ax=0.f, ay=0.f, az=0.f;
                    if (readF32(&angle) && readF32(&ax) && readF32(&ay) && readF32(&az))
                    {
                        float len = std::sqrt(ax*ax + ay*ay + az*az);
                        if (len > 1e-6f)
                        {
                            float omega = angle * -0.5f;
                            float si = std::sin(omega) / len;
                            node.rot = glm::quat(std::cos(omega), si*ax, si*ay, si*az);
                        }
                    }
                }
            }
            else if (c.id == CHUNK_KF_SCL)
            {
                SDL_RWseek(rw, 10, RW_SEEK_CUR);
                uint32_t keyNum = 0;
                if (readU32(&keyNum) && keyNum > 0)
                {
                    uint32_t frame = 0; readU32(&frame);
                    uint16_t fl = 0; readU16(&fl);
                    if (fl != 0) { float _d = 0.f; readF32(&_d); }
                    float x=0.f, y=0.f, z=0.f;
                    if (readF32(&x) && readF32(&y) && readF32(&z))
                        node.scale = glm::vec3(x, y, z);
                }
            }

            skipTo(c.end);
        }

        if (!nodeName.empty())
            kfNodes[nodeName].push_back(node);
    };

    auto parseKeyframer = [&](Sint64 chunkEnd)
    {
        while (SDL_RWtell(rw) < chunkEnd)
        {
            Chunk3DS c;
            if (!readChunk(&c))
                break;

            if (c.id == CHUNK_OBJECT_TAG)
                parseObjectTag(c.end);

            skipTo(c.end);
        }
    };

    Sint64 fileSize = SDL_RWsize(rw);
    if (fileSize <= 0)
    {
        SDL_RWclose(rw);
        LogError("[MeshManager] Invalid 3DS size: %s", path.c_str());
        return nullptr;
    }

    std::vector<Sint64> parseEnds;
    parseEnds.push_back(fileSize);

    while (!parseEnds.empty())
    {
        Sint64 currentEnd = parseEnds.back();
        Sint64 pos = SDL_RWtell(rw);
        if (pos < 0 || pos >= currentEnd)
        {
            parseEnds.pop_back();
            continue;
        }

        Chunk3DS c;
        if (!readChunk(&c))
            break;

        if (c.id == CHUNK_MAIN || c.id == CHUNK_EDIT)
        {
            parseEnds.push_back(c.end);
            continue;
        }

        if (c.id == CHUNK_KEYF3DS)
        {
            parseKeyframer(c.end);
            skipTo(c.end);
            continue;
        }

        if (c.id == CHUNK_MATERIAL)
        {
            parseMaterial(c.end);
            skipTo(c.end);
            continue;
        }

        if (c.id == CHUNK_OBJECT)
        {
            Obj3DS obj;
            if (!readString(&obj.name))
            {
                skipTo(c.end);
                continue;
            }

            while (SDL_RWtell(rw) < c.end)
            {
                Chunk3DS sub;
                if (!readChunk(&sub))
                    break;
                if (sub.id == CHUNK_TRIMESH)
                    parseTriMesh(sub.end, obj);
                skipTo(sub.end);
            }

            if (!obj.positions.empty() && !obj.faces.empty())
                objects3ds.push_back(obj);

            skipTo(c.end);
            continue;
        }

        skipTo(c.end);
    }
    SDL_RWclose(rw);

    if (objects3ds.empty())
    {
        LogError("[MeshManager] 3DS has no geometry: %s", path.c_str());
        return nullptr;
    }

    auto computeObjectNormals = [&](Obj3DS &obj)
    {
        obj.normals.assign(obj.positions.size(), glm::vec3(0.0f));

        for (const Face3DS &f : obj.faces)
        {
            if ((size_t)f.a >= obj.positions.size() ||
                (size_t)f.b >= obj.positions.size() ||
                (size_t)f.c >= obj.positions.size())
            {
                continue;
            }

            const glm::vec3 &p0 = obj.positions[(size_t)f.a];
            const glm::vec3 &p1 = obj.positions[(size_t)f.b];
            const glm::vec3 &p2 = obj.positions[(size_t)f.c];
            const glm::vec3 n = glm::cross(p1 - p0, p2 - p0);

            obj.normals[(size_t)f.a] += n;
            obj.normals[(size_t)f.b] += n;
            obj.normals[(size_t)f.c] += n;
        }

        for (glm::vec3 &n : obj.normals)
        {
            if (glm::length(n) > 1e-6f)
                n = glm::normalize(n);
            else
                n = glm::vec3(0.0f, 1.0f, 0.0f);
        }
    };

    // Pipeline 3DS:
    //   SEM KF node: aplica TRMATRIX directamente
    //   COM KF node: inv(TRMATRIX) normaliza a mesh + KF hierarchy posiciona
    {
        auto make3DSMatrix = [&](const float *m) -> glm::mat4
        {
            glm::mat4 M(1.0f);
            M[0] = glm::vec4(m[0],  m[1],  m[2],  0.0f);
            M[1] = glm::vec4(m[3],  m[4],  m[5],  0.0f);
            M[2] = glm::vec4(m[6],  m[7],  m[8],  0.0f);
            M[3] = glm::vec4(m[9],  m[10], m[11], 1.0f);
            return M;
        };

        std::unordered_set<std::string> meshObjectNames;
        meshObjectNames.reserve(objects3ds.size());
        for (const Obj3DS &obj : objects3ds)
            meshObjectNames.insert(obj.name);

        auto buildNodeMatrix = [&](const std::string &nodeName,
                                   const KFNode3DS  &node) -> glm::mat4
        {
            glm::mat4 matrix = glm::translate(glm::mat4(1.0f), node.pos) *
                               glm::mat4_cast(node.rot) *
                               glm::scale(glm::mat4(1.0f), node.scale);

            // Match the BU/JS reference loader: pivot is only baked into nodes
            // that actually own a mesh. Dummy parents should not inject pivot.
            if (meshObjectNames.count(nodeName) > 0)
                matrix = matrix * glm::translate(glm::mat4(1.0f), -node.pivot);

            return matrix;
        };

        std::vector<Obj3DS> bakedObjects;
        bakedObjects.reserve(objects3ds.size() * 2);

        std::unordered_map<int, std::pair<std::string, int>> kfById;
        for (auto &entry : kfNodes)
        {
            for (int i = 0; i < (int)entry.second.size(); ++i)
            {
                const KFNode3DS &node = entry.second[i];
                if (node.nodeId >= 0)
                    kfById[node.nodeId] = std::make_pair(entry.first, i);
            }
        }

        for (const Obj3DS &srcObj : objects3ds)
        {
            auto kfIt = kfNodes.find(srcObj.name);
            Obj3DS phase1 = srcObj;
            if (phase1.hasTransform)
            {
                glm::mat4 M = make3DSMatrix(phase1.transform);

                bool mirrored = (glm::determinant(M) < 0.0f);
                if (mirrored)
                    // JS/BU use row-vectors. In GLM's column-vector convention,
                    // the equivalent local-axis mirror is post-multiplication.
                    M = M * glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, 1.0f, 1.0f));

                if (std::abs(glm::determinant(M)) > 1e-6f)
                {
                    glm::mat4 invM = glm::inverse(M);
                    for (auto &p : phase1.positions)
                        p = glm::vec3(invM * glm::vec4(p, 1.0f));
                }
                if (mirrored)
                    for (auto &f : phase1.faces)
                        std::swap(f.a, f.b);
            }

            if (kfIt == kfNodes.end())
            {
                for (auto &p : phase1.positions)
                    p = glm::vec3(p.x, p.z, -p.y);
                bakedObjects.push_back(std::move(phase1));
                continue;
            }

            for (int inst = 0; inst < (int)kfIt->second.size(); ++inst)
            {
                Obj3DS obj = phase1;
                const KFNode3DS &kf = kfIt->second[inst];
                glm::mat4 worldNodeMat = buildNodeMatrix(srcObj.name, kf);

                // Compose parent hierarchy for KF nodes. Wheel/door nodes are often
                // relative to parent dummies, and flattening requires parent * child.
                int parentId = kf.parentId;
                int guard = 64;
                while (parentId >= 0 && guard-- > 0)
                {
                    auto parentIt = kfById.find(parentId);
                    if (parentIt == kfById.end())
                        break;

                    auto listIt = kfNodes.find(parentIt->second.first);
                    if (listIt == kfNodes.end())
                        break;

                    const int parentIndex = parentIt->second.second;
                    if (parentIndex < 0 || parentIndex >= (int)listIt->second.size())
                        break;

                    const KFNode3DS &parent = listIt->second[parentIndex];
                    glm::mat4 parentMat = buildNodeMatrix(parentIt->second.first, parent);

                    worldNodeMat = parentMat * worldNodeMat;
                    parentId = parent.parentId;
                }

                for (auto &p : obj.positions)
                    p = glm::vec3(worldNodeMat * glm::vec4(p, 1.0f));

                // Z-up (3DS) → Y-up (OpenGL)
                for (auto &p : obj.positions)
                    p = glm::vec3(p.x, p.z, -p.y);

                bakedObjects.push_back(std::move(obj));
            }
        }

        objects3ds = std::move(bakedObjects);
    }

    for (Obj3DS &obj : objects3ds)
        computeObjectNormals(obj);

    Mesh *mesh = new Mesh();
    mesh->name = name;

    MaterialManager &matMgr = MaterialManager::instance();
    TextureManager &texMgr = TextureManager::instance();
    std::string baseDir = texture_dir.empty() ? dirFromPath(path) : texture_dir;

    std::unordered_map<std::string, int> matSlotByName;

    for (size_t i = 0; i < materials3ds.size(); ++i)
    {
        const Mat3DS &src = materials3ds[i];
        std::string key = name + "_3ds_mat_" + std::to_string(i);
        if (!src.name.empty())
            key += "_" + src.name;

        Material *mat = matMgr.has(key) ? matMgr.get(key) : matMgr.create(key);
        mat->setVec3("u_color", src.diffuse);
        mat->setVec3("u_albedoTint", src.diffuse);
        mat->setFloat("u_opacity", src.opacity);
     //   mat->setCullFace(false);
        if (src.opacity < 0.999f)
        {
            mat->setBlend(true);
            mat->setDepthWrite(false);
        }

        if (!src.textureFile.empty())
        {
            std::string texPath = ResolveTexturePath(baseDir, src.textureFile);
            if (!texPath.empty())
            {
                std::string texName = name + "_3ds_tex_" + std::to_string(i);
                Texture *tex = texMgr.load(texName, texPath);
                if (tex)
                    mat->setTexture("u_albedo", tex);
            }
            else
            {
                LogWarning("[MeshManager] 3DS texture not found: %s (base=%s)",
                           src.textureFile.c_str(), baseDir.c_str());
            }
        }

        int slot = mesh->add_material(mat);
        if (!src.name.empty())
            matSlotByName[src.name] = slot;
    }

    if (mesh->materials.empty())
    {
        Material *fallback = matMgr.create(name + "_3ds_default");
        fallback->setVec3("u_color", glm::vec3(0.8f, 0.8f, 0.8f));
        fallback->setVec3("u_albedoTint", glm::vec3(0.8f, 0.8f, 0.8f));
        fallback->setFloat("u_opacity", 1.0f);
       // fallback->setCullFace(false);
        mesh->add_material(fallback);
    }

    const int defaultMatSlot = 0;
    mesh->buffer.indices.clear();
    mesh->surfaces.clear();

    for (size_t o = 0; o < objects3ds.size(); ++o)
    {
        const Obj3DS &obj = objects3ds[o];
        bool runActive = false;
        int runMatSlot = defaultMatSlot;
        uint32_t runStart = 0;

        for (size_t i = 0; i < obj.faces.size(); ++i)
        {
            const Face3DS &f = obj.faces[i];
            if ((size_t)f.a >= obj.positions.size() ||
                (size_t)f.b >= obj.positions.size() ||
                (size_t)f.c >= obj.positions.size())
            {
                continue;
            }

            int matSlot = defaultMatSlot;
            if (!f.matName.empty())
            {
                std::unordered_map<std::string, int>::const_iterator it = matSlotByName.find(f.matName);
                if (it != matSlotByName.end())
                    matSlot = it->second;
            }

            if (!runActive)
            {
                runActive = true;
                runMatSlot = matSlot;
                runStart = static_cast<uint32_t>(mesh->buffer.indices.size());
            }
            else if (matSlot != runMatSlot)
            {
                const uint32_t runCount = static_cast<uint32_t>(mesh->buffer.indices.size()) - runStart;
                if (runCount > 0)
                    mesh->add_surface(runStart, runCount, runMatSlot);
                runMatSlot = matSlot;
                runStart = static_cast<uint32_t>(mesh->buffer.indices.size());
            }

            const uint32_t baseVertex = (uint32_t)mesh->buffer.vertices.size();

            Vertex va;
            va.position = obj.positions[(size_t)f.a];
            va.normal = ((size_t)f.a < obj.normals.size()) ? obj.normals[(size_t)f.a] : glm::vec3(0.f, 1.f, 0.f);
            va.tangent = glm::vec4(1.f, 0.f, 0.f, 1.f);
            va.uv = ((size_t)f.a < obj.uvs.size()) ? obj.uvs[(size_t)f.a] : glm::vec2(0.f, 0.f);

            Vertex vb;
            vb.position = obj.positions[(size_t)f.b];
            vb.normal = ((size_t)f.b < obj.normals.size()) ? obj.normals[(size_t)f.b] : glm::vec3(0.f, 1.f, 0.f);
            vb.tangent = glm::vec4(1.f, 0.f, 0.f, 1.f);
            vb.uv = ((size_t)f.b < obj.uvs.size()) ? obj.uvs[(size_t)f.b] : glm::vec2(0.f, 0.f);

            Vertex vc;
            vc.position = obj.positions[(size_t)f.c];
            vc.normal = ((size_t)f.c < obj.normals.size()) ? obj.normals[(size_t)f.c] : glm::vec3(0.f, 1.f, 0.f);
            vc.tangent = glm::vec4(1.f, 0.f, 0.f, 1.f);
            vc.uv = ((size_t)f.c < obj.uvs.size()) ? obj.uvs[(size_t)f.c] : glm::vec2(0.f, 0.f);

            mesh->buffer.vertices.push_back(va);
            mesh->buffer.vertices.push_back(vb);
            mesh->buffer.vertices.push_back(vc);

            mesh->buffer.indices.push_back(baseVertex + 0u);
            mesh->buffer.indices.push_back(baseVertex + 1u);
            mesh->buffer.indices.push_back(baseVertex + 2u);
        }

        if (runActive)
        {
            const uint32_t runCount = static_cast<uint32_t>(mesh->buffer.indices.size()) - runStart;
            if (runCount > 0)
                mesh->add_surface(runStart, runCount, runMatSlot);
        }
    }

    if (mesh->buffer.indices.empty())
    {
        delete mesh;
        LogError("[MeshManager] 3DS generated no indices: %s", path.c_str());
        return nullptr;
    }

    {
        std::vector<std::vector<uint32_t> > indicesByMaterial(mesh->materials.size());
        if (indicesByMaterial.empty())
            indicesByMaterial.resize(1);

        for (size_t s = 0; s < mesh->surfaces.size(); ++s)
        {
            const Surface &surf = mesh->surfaces[s];
            int matSlot = surf.material_index;
            if (matSlot < 0)
                matSlot = 0;
            if ((size_t)matSlot >= indicesByMaterial.size())
                indicesByMaterial.resize((size_t)matSlot + 1);

            const uint32_t start = surf.index_start;
            const uint32_t end = std::min<uint32_t>(surf.index_start + surf.index_count,
                                                    static_cast<uint32_t>(mesh->buffer.indices.size()));
            for (uint32_t i = start; i < end; ++i)
                indicesByMaterial[(size_t)matSlot].push_back(mesh->buffer.indices[i]);
        }

        mesh->buffer.indices.clear();
        mesh->surfaces.clear();

        for (size_t matSlot = 0; matSlot < indicesByMaterial.size(); ++matSlot)
        {
            std::vector<uint32_t> &group = indicesByMaterial[matSlot];
            if (group.empty())
                continue;

            const uint32_t start = static_cast<uint32_t>(mesh->buffer.indices.size());
            mesh->buffer.indices.insert(mesh->buffer.indices.end(), group.begin(), group.end());
            mesh->add_surface(start, static_cast<uint32_t>(group.size()), static_cast<int>(matSlot));
        }
    }

    mesh->compute_tangents();
    mesh->upload();
    mesh->compute_surface_aabbs();

    MaterialManager::instance().applyDefaults();

    cache[name] = mesh;
    LogInfo("[MeshManager] Loaded 3DS '%s': objects=%d verts=%d tris=%d mats=%d",
            path.c_str(),
            (int)objects3ds.size(),
            (int)mesh->buffer.vertices.size(),
            (int)(mesh->buffer.indices.size() / 3),
            (int)mesh->materials.size());
    return mesh;
}
