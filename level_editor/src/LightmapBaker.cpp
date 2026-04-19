#include "LightmapBaker.hpp"
#include "stb_rect_pack.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <random>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//  Helpers ─────────────────────────────────────────────────────────────────

namespace {

static glm::mat4 buildLightmapModelMatrix(const LevelMeshObject& obj)
{
    return glm::translate(glm::mat4(1.0f), obj.position)
        * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotationEuler.y), glm::vec3(0,1,0))
        * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotationEuler.x), glm::vec3(1,0,0))
        * glm::rotate(glm::mat4(1.0f), glm::radians(obj.rotationEuler.z), glm::vec3(0,0,1))
        * glm::scale(glm::mat4(1.0f), obj.scale);
}

static float halton(int index, int base)
{
    float result = 0.0f;
    float fraction = 1.0f / static_cast<float>(base);
    while (index > 0)
    {
        result += fraction * static_cast<float>(index % base);
        index /= base;
        fraction /= static_cast<float>(base);
    }
    return result;
}

struct Triangle
{
    glm::vec3 v0, v1, v2;
    glm::vec3 normal;
};

// Collect all world-space triangles from the scene
static std::vector<Triangle> collectTriangles(const LevelEditorScene& scene)
{
    std::vector<Triangle> tris;
    for (const auto& obj : scene.meshObjects())
    {
        if (!obj.visible)
            continue;

        const glm::mat4 model = buildLightmapModelMatrix(obj);
        const auto& verts = obj.mesh.vertices();
        for (const auto& face : obj.mesh.faces())
        {
            if (face.indices.size() < 3) continue;
            const glm::vec3 p0 = glm::vec3(model * glm::vec4(verts[(size_t)face.indices[0]].position, 1.0f));
            for (std::size_t t = 1; t + 1 < face.indices.size(); ++t)
            {
                Triangle tri;
                tri.v0 = p0;
                tri.v1 = glm::vec3(model * glm::vec4(verts[(size_t)face.indices[t]].position, 1.0f));
                tri.v2 = glm::vec3(model * glm::vec4(verts[(size_t)face.indices[t+1]].position, 1.0f));
                glm::vec3 e1 = tri.v1 - tri.v0;
                glm::vec3 e2 = tri.v2 - tri.v0;
                glm::vec3 n = glm::cross(e1, e2);
                tri.normal = glm::length(n) > 1e-8f ? glm::normalize(n) : glm::vec3(0, 1, 0);
                tris.push_back(tri);
            }
        }
    }
    return tris;
}

// Möller–Trumbore ray-triangle intersection
static bool rayTriangle(const glm::vec3& orig, const glm::vec3& dir, const Triangle& tri, float maxT)
{
    const float EPSILON = 1e-6f;
    const glm::vec3 e1 = tri.v1 - tri.v0;
    const glm::vec3 e2 = tri.v2 - tri.v0;
    const glm::vec3 h = glm::cross(dir, e2);
    const float a = glm::dot(e1, h);
    if (a > -EPSILON && a < EPSILON) return false;
    const float f = 1.0f / a;
    const glm::vec3 s = orig - tri.v0;
    const float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    const glm::vec3 q = glm::cross(s, e1);
    const float v = f * glm::dot(dir, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    const float t = f * glm::dot(e2, q);
    return (t > EPSILON && t < maxT - EPSILON);
}

//  Simple BVH for accelerated shadow ray traversal ─────────────────────────

struct AABB {
    glm::vec3 mn{1e18f}, mx{-1e18f};
    void expand(const glm::vec3& p) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
    void expand(const AABB& o) { mn = glm::min(mn, o.mn); mx = glm::max(mx, o.mx); }
    int longestAxis() const {
        const glm::vec3 d = mx - mn;
        return (d.x > d.y) ? (d.x > d.z ? 0 : 2) : (d.y > d.z ? 1 : 2);
    }
};

static bool rayAABB(const glm::vec3& orig, const glm::vec3& invDir, const AABB& box, float maxT)
{
    const glm::vec3 t1 = (box.mn - orig) * invDir;
    const glm::vec3 t2 = (box.mx - orig) * invDir;
    const glm::vec3 tmin = glm::min(t1, t2);
    const glm::vec3 tmax = glm::max(t1, t2);
    const float enter = std::max({tmin.x, tmin.y, tmin.z});
    const float exit  = std::min({tmax.x, tmax.y, tmax.z});
    return enter <= exit && exit >= 0.0f && enter < maxT;
}

struct BVHNode {
    AABB bounds;
    int left = -1, right = -1;  // child indices, -1 = leaf
    int triStart = 0, triCount = 0;  // leaf data
};

struct BVH {
    std::vector<BVHNode> nodes;
    std::vector<int> triIndices;  // reordered triangle indices
    const std::vector<Triangle>* tris = nullptr;

    void build(const std::vector<Triangle>& triangles)
    {
        tris = &triangles;
        if (triangles.empty()) return;
        triIndices.resize(triangles.size());
        for (int i = 0; i < static_cast<int>(triangles.size()); ++i) triIndices[i] = i;

        // Pre-compute centroids and bounds
        std::vector<glm::vec3> centroids(triangles.size());
        std::vector<AABB> triBounds(triangles.size());
        for (int i = 0; i < static_cast<int>(triangles.size()); ++i)
        {
            triBounds[i].expand(triangles[i].v0);
            triBounds[i].expand(triangles[i].v1);
            triBounds[i].expand(triangles[i].v2);
            centroids[i] = (triangles[i].v0 + triangles[i].v1 + triangles[i].v2) / 3.0f;
        }

        nodes.reserve(triangles.size() * 2);
        buildRecursive(0, static_cast<int>(triangles.size()), centroids, triBounds);
    }

    int buildRecursive(int start, int end, const std::vector<glm::vec3>& centroids,
                       const std::vector<AABB>& triBounds)
    {
        const int idx = static_cast<int>(nodes.size());
        nodes.emplace_back();

        // Compute bounds of all triangles in range
        AABB bounds;
        for (int i = start; i < end; ++i)
            bounds.expand(triBounds[triIndices[i]]);
        nodes[idx].bounds = bounds;

        const int count = end - start;
        if (count <= 4)  // leaf
        {
            nodes[idx].triStart = start;
            nodes[idx].triCount = count;
            return idx;
        }

        // Split along longest axis at midpoint
        const int axis = bounds.longestAxis();
        const float mid = (bounds.mn[axis] + bounds.mx[axis]) * 0.5f;

        // Partition
        int split = start;
        for (int i = start; i < end; ++i)
        {
            if (centroids[triIndices[i]][axis] < mid)
                std::swap(triIndices[i], triIndices[split++]);
        }
        // Fallback if all on one side
        if (split == start || split == end)
            split = start + count / 2;

        nodes[idx].left = buildRecursive(start, split, centroids, triBounds);
        nodes[idx].right = buildRecursive(split, end, centroids, triBounds);
        return idx;
    }

    bool anyHit(const glm::vec3& orig, const glm::vec3& dir, float maxT) const
    {
        if (nodes.empty()) return false;
        const glm::vec3 invDir = glm::vec3(
            std::abs(dir.x) > 1e-8f ? 1.0f / dir.x : 1e8f * (dir.x >= 0 ? 1.0f : -1.0f),
            std::abs(dir.y) > 1e-8f ? 1.0f / dir.y : 1e8f * (dir.y >= 0 ? 1.0f : -1.0f),
            std::abs(dir.z) > 1e-8f ? 1.0f / dir.z : 1e8f * (dir.z >= 0 ? 1.0f : -1.0f)
        );
        // Iterative traversal with explicit stack
        std::vector<int> stack;
        stack.reserve(std::max<std::size_t>(128, nodes.size()));
        stack.push_back(0);
        while (!stack.empty())
        {
            const auto& node = nodes[stack.back()];
            stack.pop_back();
            if (!rayAABB(orig, invDir, node.bounds, maxT)) continue;
            if (node.left == -1)  // leaf
            {
                for (int i = node.triStart; i < node.triStart + node.triCount; ++i)
                {
                    if (rayTriangle(orig, dir, (*tris)[triIndices[i]], maxT))
                        return true;
                }
            }
            else
            {
                stack.push_back(node.left);
                stack.push_back(node.right);
            }
        }
        return false;
    }
};

// Test if a point is in shadow from a light (BVH-accelerated)
static bool isOccluded(const glm::vec3& point, const glm::vec3& lightPos,
                        const glm::vec3& surfaceNormal,
                        const BVH& bvh, float bias)
{
    glm::vec3 dir = lightPos - point;
    const float dist = glm::length(dir);
    if (dist < 1e-6f) return false;
    dir /= dist;
    // Bias along surface normal + extra along ray at grazing angles to prevent self-shadowing
    const float grazingFactor = 1.0f - std::abs(glm::dot(surfaceNormal, dir));
    const glm::vec3 origin = point + surfaceNormal * bias + dir * (bias * grazingFactor);
    const float searchDist = dist - bias * 2.0f;
    if (searchDist < 0.01f) return false;
    return bvh.anyHit(origin, dir, searchDist);
}

} // namespace

//  Main bake function ──────────────────────────────────────────────────────

LightmapResult BakeLightmaps(const LevelEditorScene& scene, const LightmapSettings& settings,
                              std::atomic<float>* progress)
{
    if (progress) progress->store(0.0f);
    LightmapResult result;
    const int atlasSize = settings.resolution;

    // Collect lights
    struct Light {
        LightType type;
        glm::vec3 position;
        glm::vec3 color;
        float intensity;
        float radius;
        glm::vec3 direction; // normalized (for directional/spot)
        float spotCosInner;  // cos(innerAngle) for soft edge
        float spotCosOuter;  // cos(outerAngle)
    };
    std::vector<Light> lights;
    for (const auto& ent : scene.entities())
    {
        if (ent.type == LevelEntityType::Light)
        {
            Light l;
            l.type = ent.lightType;
            l.position = ent.position;
            l.color = ent.color;
            l.intensity = ent.intensity;
            l.radius = ent.radius;
            l.direction = glm::length(ent.direction) > 1e-6f ? glm::normalize(ent.direction) : glm::vec3(0,-1,0);
            const float outerRad = glm::radians(ent.spotAngle);
            const float innerRad = outerRad * (1.0f - std::clamp(ent.spotSoftness, 0.0f, 1.0f));
            l.spotCosOuter = std::cos(outerRad);
            l.spotCosInner = std::cos(innerRad);
            lights.push_back(l);
        }
    }

    // Collect all triangles for shadow testing and build BVH
    const auto allTris = collectTriangles(scene);
    BVH shadowBVH;
    shadowBVH.build(allTris);
    printf("[Lightmap] BVH: %d tris, %d nodes\n",
           static_cast<int>(allTris.size()), static_cast<int>(shadowBVH.nodes.size()));

    // Build lightmap UV rects — one per face per mesh
    struct FaceEntry {
        int meshIdx;
        int faceIdx;
        int triCount;
        // World-space corners for baking
        std::vector<glm::vec3> worldPositions;
        std::vector<glm::vec3> worldNormals;
        // Precomputed tangent frame (shared between rect sizing and baking)
        glm::vec3 faceNormal;
        glm::vec3 tangentU, tangentV;
        glm::vec3 faceOrigin;
        glm::vec2 localMin, localMax;
    };
    std::vector<FaceEntry> faceEntries;
    struct PackedFaceRect
    {
        stbrp_rect rect{};
        int atlasIndex = 0;
    };
    std::vector<PackedFaceRect> packRects_vec;
    std::vector<std::pair<int, int>> packRectSizes;

    for (int mi = 0; mi < static_cast<int>(scene.meshObjects().size()); ++mi)
    {
        const auto& obj = scene.meshObjects()[mi];
        if (!obj.visible)
            continue;

        const glm::mat4 model = buildLightmapModelMatrix(obj);
        const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));

        const auto& verts = obj.mesh.vertices();
        for (int fi = 0; fi < static_cast<int>(obj.mesh.faceCount()); ++fi)
        {
            const auto& face = obj.mesh.faces()[fi];
            if (face.indices.size() < 3) continue;

            FaceEntry entry;
            entry.meshIdx = mi;
            entry.faceIdx = fi;
            entry.triCount = static_cast<int>(face.indices.size()) - 2;

            for (int idx : face.indices)
            {
                entry.worldPositions.push_back(glm::vec3(model * glm::vec4(verts[(size_t)idx].position, 1.0f)));
                glm::vec3 n = normalMat * verts[(size_t)idx].normal;
                entry.worldNormals.push_back(glm::length(n) > 1e-6f ? glm::normalize(n) : glm::vec3(0,1,0));
            }

            // Compute face bounding box in tangent space to get proper aspect ratio
            const int nVerts2 = static_cast<int>(entry.worldPositions.size());
            // Face normal via Newell's method
            glm::vec3 fN(0.0f);
            for (int i = 0; i < nVerts2; ++i)
            {
                const auto& cur = entry.worldPositions[i];
                const auto& nxt = entry.worldPositions[(i + 1) % nVerts2];
                fN.x += (cur.y - nxt.y) * (cur.z + nxt.z);
                fN.y += (cur.z - nxt.z) * (cur.x + nxt.x);
                fN.z += (cur.x - nxt.x) * (cur.y + nxt.y);
            }
            if (glm::length(fN) > 1e-6f) fN = glm::normalize(fN);
            else fN = glm::vec3(0, 1, 0);

            // Orient by averaged vertex normals
            glm::vec3 avgVN(0.0f);
            for (int i = 0; i < nVerts2; ++i) avgVN += entry.worldNormals[i];
            if (glm::dot(fN, avgVN) < 0.0f) fN = -fN;

            // Build tangent frame
            glm::vec3 tU, tV;
            {
                glm::vec3 ref = (std::abs(fN.y) > 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                tU = glm::normalize(glm::cross(ref, fN));
                tV = glm::normalize(glm::cross(fN, tU));
            }

            // Project vertices to 2D tangent space and find bounding box
            glm::vec2 lMin(1e18f), lMax(-1e18f);
            for (int i = 0; i < nVerts2; ++i)
            {
                float pu = glm::dot(entry.worldPositions[i], tU);
                float pv = glm::dot(entry.worldPositions[i], tV);
                lMin.x = std::min(lMin.x, pu); lMax.x = std::max(lMax.x, pu);
                lMin.y = std::min(lMin.y, pv); lMax.y = std::max(lMax.y, pv);
            }

            // Store tangent frame in entry for baking
            entry.faceNormal = fN;
            entry.tangentU = tU;
            entry.tangentV = tV;
            entry.localMin = lMin;
            entry.localMax = lMax;
            // Anchor the bake plane to an actual face vertex to avoid drifting off-plane.
            const float normalDist = glm::dot(fN, entry.worldPositions.front());
            entry.faceOrigin = tU * lMin.x + tV * lMin.y + fN * normalDist;

            float faceW = lMax.x - lMin.x;
            float faceH = lMax.y - lMin.y;

            // Map to texel count proportional to face dimensions
            const float texelsPerUnit = static_cast<float>(atlasSize) / 1024.0f;
            int rw = std::max(2, static_cast<int>(faceW * texelsPerUnit));
            int rh = std::max(2, static_cast<int>(faceH * texelsPerUnit));
            rw = std::min(rw, atlasSize / 2);
            rh = std::min(rh, atlasSize / 2);

            stbrp_rect pr;
            memset(&pr, 0, sizeof(pr));
            pr.w = rw;
            pr.h = rh;
            pr.id = static_cast<int>(faceEntries.size());
            packRects_vec.push_back({pr, 0});
            packRectSizes.push_back({rw, rh});

            faceEntries.push_back(std::move(entry));
        }
    }

    // Pack rects into as many atlas pages as needed.
    {
        const int numNodes = atlasSize * 2;
        const int padding = 2; // 1px border on each side
        std::vector<int> remaining;
        remaining.reserve(packRects_vec.size());
        for (int rectIndex = 0; rectIndex < static_cast<int>(packRects_vec.size()); ++rectIndex)
            remaining.push_back(rectIndex);

        int atlasIndex = 0;
        while (!remaining.empty())
        {
            std::vector<stbrp_rect> pageRects;
            pageRects.reserve(remaining.size());
            for (int rectIndex : remaining)
            {
                stbrp_rect rect = packRects_vec[rectIndex].rect;
                rect.w = packRectSizes[rectIndex].first + padding;
                rect.h = packRectSizes[rectIndex].second + padding;
                rect.was_packed = 0;
                pageRects.push_back(rect);
            }

            std::vector<stbrp_node> nodes(numNodes);
            stbrp_context ctx;
            stbrp_init_target(&ctx, atlasSize, atlasSize, nodes.data(), numNodes);
            stbrp_pack_rects(&ctx, pageRects.data(), static_cast<int>(pageRects.size()));

            std::vector<int> nextRemaining;
            nextRemaining.reserve(remaining.size());
            bool packedAny = false;
            for (std::size_t i = 0; i < pageRects.size(); ++i)
            {
                const int rectIndex = remaining[i];
                if (!pageRects[i].was_packed)
                {
                    nextRemaining.push_back(rectIndex);
                    continue;
                }

                packedAny = true;
                packRects_vec[rectIndex].atlasIndex = atlasIndex;
                packRects_vec[rectIndex].rect = pageRects[i];
                packRects_vec[rectIndex].rect.x += padding / 2;
                packRects_vec[rectIndex].rect.y += padding / 2;
                packRects_vec[rectIndex].rect.w -= padding;
                packRects_vec[rectIndex].rect.h -= padding;
            }

            if (!packedAny)
            {
                printf("[Lightmap] Failed to pack any rect into atlas page %d\n", atlasIndex);
                break;
            }

            LightmapResult::Atlas atlas;
            atlas.width = atlasSize;
            atlas.height = atlasSize;
            atlas.pixels.resize(static_cast<std::size_t>(atlasSize) * static_cast<std::size_t>(atlasSize) * 3u, 0);
            result.atlases.push_back(std::move(atlas));
            remaining = std::move(nextRemaining);
            ++atlasIndex;
        }
    }

    // Build UV mapping result
    result.meshUVs.resize(scene.meshObjects().size());
    for (int mi = 0; mi < static_cast<int>(scene.meshObjects().size()); ++mi)
    {
        result.meshUVs[mi].faceVertexUVs.resize(scene.meshObjects()[mi].mesh.faceCount());
        result.meshUVs[mi].faceAtlasIndices.assign(scene.meshObjects()[mi].mesh.faceCount(), 0);
    }

    if (result.atlases.empty())
    {
        LightmapResult::Atlas atlas;
        atlas.width = atlasSize;
        atlas.height = atlasSize;
        atlas.pixels.resize(static_cast<std::size_t>(atlasSize) * static_cast<std::size_t>(atlasSize) * 3u, 0);
        result.atlases.push_back(std::move(atlas));
    }

    const uint8_t amb = static_cast<uint8_t>(std::min(255.0f, settings.ambient * 255.0f));
    for (auto& atlas : result.atlases)
        std::fill(atlas.pixels.begin(), atlas.pixels.end(), amb);

    // Occupancy masks — only set when we actually write a pixel (for correct dilation)
    std::vector<std::vector<uint8_t>> occupiedMaps(
        result.atlases.size(),
        std::vector<uint8_t>(static_cast<std::size_t>(atlasSize) * static_cast<std::size_t>(atlasSize), 0));

    // Bake each face
    const int totalFaces = static_cast<int>(packRects_vec.size());
    int bakedFaces = 0;
    int dbgLitFaces = 0, dbgNotPacked = 0;
    for (const auto& packedRect : packRects_vec)
    {
        const auto& pr = packedRect.rect;
        if (!pr.was_packed) { bakedFaces++; dbgNotPacked++; continue; }
        if (progress) progress->store(static_cast<float>(bakedFaces) / static_cast<float>(std::max(1, totalFaces)));
        const auto& entry = faceEntries[pr.id];
        const int atlasPage = std::clamp(packedRect.atlasIndex, 0, static_cast<int>(result.atlases.size()) - 1);
        auto& atlasPixels = result.atlases[static_cast<std::size_t>(atlasPage)].pixels;
        auto& occupied = occupiedMaps[static_cast<std::size_t>(atlasPage)];
        const int nVerts = static_cast<int>(entry.worldPositions.size());
        if (nVerts < 3) continue;

        // Use precomputed tangent frame from rect sizing
        const glm::vec3& faceNormal = entry.faceNormal;
        const glm::vec3& tangentU = entry.tangentU;
        const glm::vec3& tangentV = entry.tangentV;
        const glm::vec3& faceOrigin = entry.faceOrigin;
        const glm::vec2& localMin = entry.localMin;
        const glm::vec2& localMax = entry.localMax;
        const glm::vec2 localSize = localMax - localMin;
        if (localSize.x < 1e-6f || localSize.y < 1e-6f) continue;

        // Project vertices into 2D local space (for point-in-polygon test)
        std::vector<glm::vec2> localPts(nVerts);
        for (int v = 0; v < nVerts; ++v)
        {
            float pu = glm::dot(entry.worldPositions[v], tangentU);
            float pv = glm::dot(entry.worldPositions[v], tangentV);
            localPts[v] = glm::vec2(pu, pv);
        }

        // Debug: test face center lighting
        bool faceGotLight = false;

        const int sampleCount = std::max(1, settings.samplesPerTexel);

        auto computeLightingAtPoint = [&](const glm::vec3& worldP, bool& outLit) -> glm::vec3
        {
            glm::vec3 lighting(settings.ambient);

            for (const auto& light : lights)
            {
                glm::vec3 lightDir;   // direction FROM surface TO light
                float NdotL;
                float atten = 1.0f;
                glm::vec3 shadowTarget;

                if (light.type == LightType::Directional)
                {
                    // Sun: parallel rays, no distance falloff
                    lightDir = -light.direction; // direction points toward light
                    NdotL = std::max(0.0f, glm::dot(faceNormal, lightDir));
                    if (NdotL < 1e-4f) continue;

                    // Shadow: cast ray very far in light direction
                    shadowTarget = worldP + lightDir * 10000.0f;
                    if (isOccluded(worldP, shadowTarget, faceNormal, shadowBVH, settings.bias))
                        continue;

                    atten = 1.0f; // no distance falloff for sun
                }
                else // Point or Spot
                {
                    const glm::vec3 toLight = light.position - worldP;
                    const float dist = glm::length(toLight);
                    if (dist > light.radius || dist < 1e-4f) continue;

                    lightDir = toLight / dist;
                    NdotL = std::max(0.0f, glm::dot(faceNormal, lightDir));
                    if (NdotL < 1e-4f) continue;

                    // Spot cone check
                    if (light.type == LightType::Spot)
                    {
                        // lightDir is surface→light, light.direction is light→target
                        const float cosAngle = glm::dot(-lightDir, light.direction);
                        if (cosAngle < light.spotCosOuter) continue; // outside cone
                        // Smooth falloff between inner and outer cone
                        if (cosAngle < light.spotCosInner)
                        {
                            const float t = (cosAngle - light.spotCosOuter) /
                                            std::max(1e-6f, light.spotCosInner - light.spotCosOuter);
                            atten *= t * t; // quadratic smooth
                        }
                    }

                    // Shadow test
                    if (isOccluded(worldP, light.position, faceNormal, shadowBVH, settings.bias))
                        continue;

                    // Distance attenuation: smooth quadratic falloff
                    const float ratio = dist / light.radius;
                    atten *= std::max(0.0f, 1.0f - ratio * ratio);
                }

                const float contribution = NdotL * atten * light.intensity;
                lighting += light.color * contribution;
                if (contribution > 0.001f)
                    outLit = true;
            }

            return lighting;
        };

        // For each texel in this face's atlas rect, compute world position and light
        for (int ty = 0; ty < pr.h; ++ty)
        {
            for (int tx = 0; tx < pr.w; ++tx)
            {
                glm::vec3 accumulated(0.0f);
                int validSamples = 0;

                for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
                {
                    const float sampleOffsetX = halton(sampleIndex + 1, 2);
                    const float sampleOffsetY = halton(sampleIndex + 1, 3);

                    // Normalized position within the face rect
                    const float u = (static_cast<float>(tx) + sampleOffsetX) / static_cast<float>(pr.w);
                    const float v = (static_cast<float>(ty) + sampleOffsetY) / static_cast<float>(pr.h);

                    // Map to world space: faceOrigin is the point at localMin
                    const glm::vec3 worldP = faceOrigin + tangentU * (u * localSize.x) + tangentV * (v * localSize.y);

                    // localP in absolute tangent-space (for point-in-polygon)
                    const glm::vec2 localP = localMin + glm::vec2(u, v) * localSize;

                    // Check if the sample lies inside the projected face polygon.
                    {
                        bool inside = false;
                        for (int i = 0, j = nVerts - 1; i < nVerts; j = i++)
                        {
                            if ((localPts[i].y > localP.y) != (localPts[j].y > localP.y) &&
                                localP.x < (localPts[j].x - localPts[i].x) * (localP.y - localPts[i].y) / (localPts[j].y - localPts[i].y) + localPts[i].x)
                            {
                                inside = !inside;
                            }
                        }
                        if (!inside)
                            continue;
                    }

                    bool sampleLit = false;
                    accumulated += computeLightingAtPoint(worldP, sampleLit);
                    faceGotLight = faceGotLight || sampleLit;
                    ++validSamples;
                }

                if (validSamples == 0)
                    continue;

                const glm::vec3 lighting = accumulated / static_cast<float>(validSamples);

                // Write to atlas
                const int px = pr.x + tx;
                const int py = pr.y + ty;
                if (px >= 0 && px < atlasSize && py >= 0 && py < atlasSize)
                {
                    const int idx = (py * atlasSize + px) * 3;
                    atlasPixels[idx + 0] = static_cast<uint8_t>(std::min(255.0f, lighting.r * 255.0f));
                    atlasPixels[idx + 1] = static_cast<uint8_t>(std::min(255.0f, lighting.g * 255.0f));
                    atlasPixels[idx + 2] = static_cast<uint8_t>(std::min(255.0f, lighting.b * 255.0f));
                    occupied[py * atlasSize + px] = 1;
                }
            }
        }

        // Store lightmap UVs for this face's vertices
        auto& meshUVs = result.meshUVs[entry.meshIdx].faceVertexUVs[entry.faceIdx];
        result.meshUVs[entry.meshIdx].faceAtlasIndices[entry.faceIdx] = atlasPage;
        meshUVs.resize(nVerts);
        for (int v = 0; v < nVerts; ++v)
        {
            // Map local vertex position to atlas UV
            const glm::vec2 normalized = (localPts[v] - localMin) / localSize;
            const float uvx = (static_cast<float>(pr.x) + normalized.x * static_cast<float>(pr.w) + 0.5f) / static_cast<float>(atlasSize);
            const float uvy = (static_cast<float>(pr.y) + normalized.y * static_cast<float>(pr.h) + 0.5f) / static_cast<float>(atlasSize);
            meshUVs[v] = glm::vec2(uvx, uvy);
        }
        if (faceGotLight) dbgLitFaces++;
        else if (dbgLitFaces == 0 && bakedFaces < 5 && !lights.empty())
        {
            // Debug: why didn't this face get light?
            glm::vec3 center(0.0f);
            for (int v = 0; v < nVerts; ++v) center += entry.worldPositions[v];
            center /= static_cast<float>(nVerts);
            const auto& l0 = lights[0];
            glm::vec3 toLight = (l0.type == LightType::Directional) ? -l0.direction : glm::normalize(l0.position - center);
            float ndl = glm::dot(faceNormal, toLight);
            float dist = glm::length(l0.position - center);
            bool occ = isOccluded(center, (l0.type == LightType::Directional) ? center + toLight * 10000.0f : l0.position,
                                  faceNormal, shadowBVH, settings.bias);
            printf("[Lightmap] UNLIT face %d (mesh %d): center=(%.1f,%.1f,%.1f) normal=(%.2f,%.2f,%.2f) NdotL=%.3f dist=%.1f occ=%d rect=%dx%d\n",
                   entry.faceIdx, entry.meshIdx,
                   center.x, center.y, center.z,
                   faceNormal.x, faceNormal.y, faceNormal.z,
                   ndl, dist, occ ? 1 : 0, pr.w, pr.h);
        }
        bakedFaces++;
    }

    if (progress) progress->store(1.0f);

    //  Dilation pass: expand lit pixels into empty neighbors to prevent black seams 
    {
        for (std::size_t atlasIndex = 0; atlasIndex < result.atlases.size(); ++atlasIndex)
        {
            std::vector<uint8_t> pixels_copy(result.atlases[atlasIndex].pixels);
            auto occupied = occupiedMaps[atlasIndex];
            for (int pass = 0; pass < 4; ++pass)
            {
                std::vector<uint8_t> newOccupied(occupied);
                std::vector<uint8_t> newPixels(pixels_copy);
                for (int y = 0; y < atlasSize; ++y)
                {
                    for (int x = 0; x < atlasSize; ++x)
                    {
                        if (occupied[y * atlasSize + x]) continue; // already filled
                        int count = 0;
                        int sumR = 0, sumG = 0, sumB = 0;
                        for (int dy = -1; dy <= 1; ++dy)
                        {
                            for (int dx = -1; dx <= 1; ++dx)
                            {
                                const int nx = x + dx, ny = y + dy;
                                if (nx < 0 || nx >= atlasSize || ny < 0 || ny >= atlasSize) continue;
                                if (!occupied[ny * atlasSize + nx]) continue;
                                const int ni = (ny * atlasSize + nx) * 3;
                                sumR += pixels_copy[ni + 0];
                                sumG += pixels_copy[ni + 1];
                                sumB += pixels_copy[ni + 2];
                                count++;
                            }
                        }
                        if (count > 0)
                        {
                            const int idx = (y * atlasSize + x) * 3;
                            newPixels[idx + 0] = static_cast<uint8_t>(sumR / count);
                            newPixels[idx + 1] = static_cast<uint8_t>(sumG / count);
                            newPixels[idx + 2] = static_cast<uint8_t>(sumB / count);
                            newOccupied[y * atlasSize + x] = 1;
                        }
                    }
                }
                occupied = std::move(newOccupied);
                pixels_copy = std::move(newPixels);
            }
            result.atlases[atlasIndex].pixels = std::move(pixels_copy);
        }
    }

    // Diagnostic: find max pixel value and count lit/shadowed texels
    {
        uint8_t maxVal = 0;
        int litTexels = 0;
        int totalTexels = 0;
        for (const auto& atlas : result.atlases)
        {
            for (int i = 0; i < static_cast<int>(atlas.pixels.size()); i += 3)
            {
                const uint8_t mx = std::max({atlas.pixels[i], atlas.pixels[i+1], atlas.pixels[i+2]});
                if (mx > maxVal) maxVal = mx;
                if (mx > amb) litTexels++;
                totalTexels++;
            }
        }
        printf("[Lightmap] Atlases=%d page=%dx%d, %d lights, %d faces, %d tris\n",
               static_cast<int>(result.atlases.size()), atlasSize, atlasSize, static_cast<int>(lights.size()),
               totalFaces, static_cast<int>(allTris.size()));
        printf("[Lightmap] Lit faces: %d / %d, not packed: %d\n",
               dbgLitFaces, totalFaces, dbgNotPacked);
        printf("[Lightmap] Max pixel: %d, lit texels: %d / %d (%.1f%%)\n",
               maxVal, litTexels, totalTexels,
               totalTexels > 0 ? 100.0f * litTexels / totalTexels : 0.0f);
        for (const auto& l : lights)
        {
            const char* typeStr = l.type == LightType::Point ? "Point" : (l.type == LightType::Directional ? "Dir" : "Spot");
            printf("[Lightmap]   %s light at (%.1f, %.1f, %.1f) int=%.2f rad=%.0f dir=(%.2f,%.2f,%.2f)\n",
                   typeStr, l.position.x, l.position.y, l.position.z, l.intensity, l.radius,
                   l.direction.x, l.direction.y, l.direction.z);
        }
    }

    // Keep legacy page 0 fields for editor preview and existing code paths.
    result.savedPath = settings.outputPath;
    result.width = result.atlases.front().width;
    result.height = result.atlases.front().height;
    result.pixels = result.atlases.front().pixels;

    return result;
}
