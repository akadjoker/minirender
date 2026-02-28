
#define CGLTF_IMPLEMENTATION

#include "cgltf.h"
#include "Manager.hpp"
#include "MeshLoader.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <unordered_map>

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
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[MeshManager] '%s' already exists, returning existing",
                    name.c_str());
        return get(name);
    }

    auto *m = new Mesh();
    m->name = name;
    cache[name] = m;
    return m;
}

Mesh *MeshManager::load(const std::string &name, const std::string &path,
                        const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    auto dot = path.rfind('.');
    if (dot == std::string::npos)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[MeshManager] No extension in path: %s", path.c_str());
        return nullptr;
    }
    std::string ext = path.substr(dot + 1);

    if (ext == "obj")
        return load_obj(name, path, texture_dir);
    if (ext == "gltf" || ext == "glb")
        return load_gltf(name, path, texture_dir);
    if (ext == "h3d" || ext == "mesh")
        return load_h3d(name, path, texture_dir);

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[MeshManager] Unknown mesh format '%s': %s", ext.c_str(), path.c_str());
    return nullptr;
}

// ============================================================
//  OBJ loader  — custom streaming parser (no tinyobj for geometry)
//  Single pass, per-material vertex cache, SDL file I/O
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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[MeshManager] Cannot open OBJ: %s  (%s)", path.c_str(), SDL_GetError());
        return nullptr;
    }
    Sint64 fsize = SDL_RWsize(rw);
    std::string buf(fsize, '\0');
    SDL_RWread(rw, buf.data(), 1, fsize);
    SDL_RWclose(rw);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[MeshManager] Parsing OBJ: %s  (%.1f KB)", path.c_str(), buf.size() / 1024.f);

    std::string obj_dir;
    {
        auto slash = path.rfind('/');
        if (slash != std::string::npos)
            obj_dir = path.substr(0, slash + 1);
    }
    std::string base_dir = texture_dir.empty() ? obj_dir
                         : (texture_dir.back() == '/' ? texture_dir : texture_dir + '/');

    // ── MTL loader (reads via SDL, parses into local structs) ─────────────
    struct OBJMat { std::string name; glm::vec3 diffuse{0.8f,0.8f,0.8f}; std::string tex; };
    std::vector<OBJMat>                     objMats;
    std::unordered_map<std::string, int>    matNameToIdx;

    auto loadMTL = [&](const std::string &mtlFile)
    {
        std::string mtlPath = obj_dir + mtlFile;
        SDL_RWops *mrw = SDL_RWFromFile(mtlPath.c_str(), "rb");
        if (!mrw) { SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "[MeshManager] Cannot open MTL: %s", mtlPath.c_str()); return; }
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
                else if (cmd == "map_Kd" || cmd == "map_Ka")
                {
                    std::string t; std::getline(ls >> std::ws, t);
                    if (!t.empty() && t.back() == '\r') t.pop_back();
                    cur->tex = t;
                }
            }
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "[MeshManager] MTL loaded: %zu materials from %s",
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

    // Build MaterialManager entries for each objMat — index matches objMats[]
    std::vector<Material*> matPtrs;
    for (auto &om : objMats)
    {
        std::string mn = name + "/" + om.name;
        Material *mat = matMgr.has(mn) ? matMgr.get(mn) : matMgr.create(mn);
        mat->setVec3("u_color", {om.diffuse.r, om.diffuse.g, om.diffuse.b});
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

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[MeshManager] OBJ loaded: %s  verts=%zu  idx=%zu  surfaces=%zu  materials=%zu",
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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[MeshManager] Failed to parse GLTF: %s", path.c_str());
        return nullptr;
    }
    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[MeshManager] Failed to load GLTF buffers: %s", path.c_str());
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
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[MeshManager] GLTF has no geometry: %s", path.c_str());
        delete mesh_out;
        cgltf_free(data);
        return nullptr;
    }

    if (mesh_out->surfaces[0].index_count > 0)
        mesh_out->compute_tangents();

    mesh_out->upload();
    cgltf_free(data);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[MeshManager] GLTF loaded: %s  verts=%zu  idx=%zu  surfaces=%zu",
                name.c_str(), mesh_out->buffer.vertices.size(),
                mesh_out->buffer.indices.size(), mesh_out->surfaces.size());

    cache[name] = mesh_out;
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
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[MeshManager] Failed to load h3d '%s': %s",
                     name.c_str(), path.c_str());
        delete mesh;
        return nullptr;
    }

    MaterialManager::instance().applyDefaults();

    cache[name] = mesh;
    return mesh;
}
 
