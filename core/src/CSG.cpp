#include "CSG.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

namespace
{

constexpr float kPlaneEps = 1e-5f;

// ============================================================
//  CSGVertex — position + attributes for interpolation
// ============================================================
struct CSGVertex
{
    glm::vec3 position {0.f};
    glm::vec3 normal   {0.f, 1.f, 0.f};
    glm::vec2 uv       {0.f};

    CSGVertex interpolate(const CSGVertex& other, float t) const
    {
        CSGVertex result;
        result.position = glm::mix(position, other.position, t);
        const glm::vec3 mixedN = glm::mix(normal, other.normal, t);
        const float len2 = glm::length2(mixedN);
        result.normal = len2 > 1e-12f ? mixedN / std::sqrt(len2) : normal;
        result.uv       = glm::mix(uv, other.uv, t);
        return result;
    }
};

// ============================================================
//  CSGPlane — splitting plane (Hessian normal form)
// ============================================================
struct CSGPlane
{
    glm::vec3 normal {0.f, 1.f, 0.f};
    float     w = 0.f; // dot(normal, pointOnPlane)

    static CSGPlane fromPoints(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
    {
        CSGPlane plane;
        plane.normal = glm::normalize(glm::cross(b - a, c - a));
        plane.w = glm::dot(plane.normal, a);
        return plane;
    }

    void flip()
    {
        normal = -normal;
        w = -w;
    }

    float signedDistance(const glm::vec3& point) const
    {
        return glm::dot(normal, point) - w;
    }
};

// ============================================================
//  CSGPolygon — convex polygon with plane + material
// ============================================================
struct CSGPolygon
{
    std::vector<CSGVertex> vertices;
    CSGPlane plane;
    int matIndex = 0;

    void flip()
    {
        std::reverse(vertices.begin(), vertices.end());
        for (auto& v : vertices) v.normal = -v.normal;
        plane.flip();
    }

    bool isDegenerate() const
    {
        if (vertices.size() < 3) return true;
        const float area2 = glm::length2(glm::cross(
            vertices[1].position - vertices[0].position,
            vertices[2].position - vertices[0].position));
        return area2 < 1e-12f;
    }
};

// ============================================================
//  Classification
// ============================================================
enum class Side { Front, Back, Coplanar, Spanning };

Side classifyPoint(const CSGPlane& plane, const glm::vec3& point)
{
    const float d = plane.signedDistance(point);
    if (d > kPlaneEps) return Side::Front;
    if (d < -kPlaneEps) return Side::Back;
    return Side::Coplanar;
}

Side classifyPolygon(const CSGPlane& plane, const CSGPolygon& polygon)
{
    int numFront = 0, numBack = 0;
    for (const auto& v : polygon.vertices)
    {
        const Side s = classifyPoint(plane, v.position);
        if (s == Side::Front) ++numFront;
        else if (s == Side::Back) ++numBack;
    }
    if (numFront > 0 && numBack > 0) return Side::Spanning;
    if (numFront > 0) return Side::Front;
    if (numBack > 0) return Side::Back;
    return Side::Coplanar;
}

// ============================================================
//  splitPolygon — split a polygon by a plane
// ============================================================
void splitPolygon(const CSGPlane& plane, const CSGPolygon& polygon,
                  std::vector<CSGPolygon>& coplanarFront,
                  std::vector<CSGPolygon>& coplanarBack,
                  std::vector<CSGPolygon>& front,
                  std::vector<CSGPolygon>& back)
{
    const Side type = classifyPolygon(plane, polygon);

    switch (type)
    {
    case Side::Coplanar:
        if (glm::dot(plane.normal, polygon.plane.normal) > 0.f)
            coplanarFront.push_back(polygon);
        else
            coplanarBack.push_back(polygon);
        break;

    case Side::Front:
        front.push_back(polygon);
        break;

    case Side::Back:
        back.push_back(polygon);
        break;

    case Side::Spanning:
    {
        std::vector<CSGVertex> frontVerts, backVerts;
        const std::size_t count = polygon.vertices.size();

        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t j = (i + 1) % count;
            const CSGVertex& vi = polygon.vertices[i];
            const CSGVertex& vj = polygon.vertices[j];
            const Side si = classifyPoint(plane, vi.position);
            const Side sj = classifyPoint(plane, vj.position);

            if (si != Side::Back)
                frontVerts.push_back(vi);
            if (si != Side::Front)
                backVerts.push_back(vi);

            if ((si == Side::Front && sj == Side::Back) ||
                (si == Side::Back && sj == Side::Front))
            {
                const float di = plane.signedDistance(vi.position);
                const float dj = plane.signedDistance(vj.position);
                const float t = di / (di - dj);
                const CSGVertex mid = vi.interpolate(vj, glm::clamp(t, 0.f, 1.f));
                frontVerts.push_back(mid);
                backVerts.push_back(mid);
            }
        }

        if (frontVerts.size() >= 3)
        {
            CSGPolygon fp;
            fp.vertices = std::move(frontVerts);
            fp.plane = polygon.plane;
            fp.matIndex = polygon.matIndex;
            if (!fp.isDegenerate())
                front.push_back(std::move(fp));
        }

        if (backVerts.size() >= 3)
        {
            CSGPolygon bp;
            bp.vertices = std::move(backVerts);
            bp.plane = polygon.plane;
            bp.matIndex = polygon.matIndex;
            if (!bp.isDegenerate())
                back.push_back(std::move(bp));
        }
        break;
    }
    }
}

// ============================================================
//  BSPNode — BSP tree node
// ============================================================
struct BSPNode
{
    CSGPlane plane;
    std::vector<CSGPolygon> polygons; // coplanar polygons at this node
    std::unique_ptr<BSPNode> front;
    std::unique_ptr<BSPNode> back;

    BSPNode() = default;

    explicit BSPNode(const std::vector<CSGPolygon>& polys)
    {
        build(polys);
    }

    // Clone this entire tree
    std::unique_ptr<BSPNode> clone() const
    {
        auto node = std::make_unique<BSPNode>();
        node->plane = plane;
        node->polygons = polygons;
        if (front) node->front = front->clone();
        if (back) node->back = back->clone();
        return node;
    }

    // Invert the entire BSP tree (swap inside/outside)
    void invert()
    {
        plane.flip();
        for (auto& p : polygons) p.flip();
        std::swap(front, back);
        if (front) front->invert();
        if (back) back->invert();
    }

    // Collect all polygons from the tree
    std::vector<CSGPolygon> allPolygons() const
    {
        std::vector<CSGPolygon> result = polygons;
        if (front)
        {
            auto fp = front->allPolygons();
            result.insert(result.end(), fp.begin(), fp.end());
        }
        if (back)
        {
            auto bp = back->allPolygons();
            result.insert(result.end(), bp.begin(), bp.end());
        }
        return result;
    }

    // Remove all polygons in this BSP tree that are inside the other BSP tree.
    // If `invert` is true, removes polygons that are outside instead.
    void clipTo(const BSPNode& other)
    {
        polygons = other.clipPolygons(polygons);
        if (front) front->clipTo(other);
        if (back) back->clipTo(other);
    }

    // Recursively clip the given polygon list against this BSP tree.
    // Returns the fragments that survive (are in "front" / outside).
    std::vector<CSGPolygon> clipPolygons(const std::vector<CSGPolygon>& polys) const
    {
        std::vector<CSGPolygon> frontList, backList;
        std::vector<CSGPolygon> coFront, coBack;

        for (const auto& p : polys)
            splitPolygon(plane, p, coFront, coBack, frontList, backList);

        // Coplanar-front are kept (outside); coplanar-back are discarded (inside)
        frontList.insert(frontList.end(), coFront.begin(), coFront.end());

        if (front)
            frontList = front->clipPolygons(frontList);

        if (back)
            backList = back->clipPolygons(backList);
        else
            backList.clear(); // no back child = solid → discard

        frontList.insert(frontList.end(), backList.begin(), backList.end());
        return frontList;
    }

    // Build the BSP tree from a list of polygons
    void build(const std::vector<CSGPolygon>& polys)
    {
        if (polys.empty()) return;

        // Pick the best splitting plane (first polygon's plane, or use heuristic)
        // Simple approach: use the first polygon's plane
        plane = polys[0].plane;

        std::vector<CSGPolygon> frontList, backList;
        std::vector<CSGPolygon> coFront, coBack;

        for (const auto& p : polys)
            splitPolygon(plane, p, coFront, coBack, frontList, backList);

        // Coplanar polygons stay at this node
        polygons.insert(polygons.end(), coFront.begin(), coFront.end());
        polygons.insert(polygons.end(), coBack.begin(), coBack.end());

        if (!frontList.empty())
        {
            if (!front) front = std::make_unique<BSPNode>();
            front->build(frontList);
        }

        if (!backList.empty())
        {
            if (!back) back = std::make_unique<BSPNode>();
            back->build(backList);
        }
    }
};

// ============================================================
//  meshToPolygons — extract world-space polygons from a Mesh
// ============================================================
std::vector<CSGPolygon> meshToPolygons(const Mesh& mesh, const glm::mat4& transform)
{
    std::vector<CSGPolygon> polygons;
    const auto& verts = mesh.buffer.vertices;
    const auto& indices = mesh.buffer.indices;
    const glm::mat3 normalMat = glm::mat3(glm::inverseTranspose(transform));

    // Map each triangle to its surface/material
    // Build a lookup: for a given index offset, what's the material?
    auto findMaterial = [&](uint32_t triStart) -> int
    {
        for (const auto& surf : mesh.surfaces)
        {
            if (triStart >= surf.index_start && triStart < surf.index_start + surf.index_count)
                return surf.material_index;
        }
        return 0;
    };

    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const Vertex& va = verts[indices[i]];
        const Vertex& vb = verts[indices[i + 1]];
        const Vertex& vc = verts[indices[i + 2]];

        CSGPolygon poly;
        CSGVertex ca, cb, cc;

        ca.position = glm::vec3(transform * glm::vec4(va.position, 1.f));
        ca.normal   = glm::normalize(normalMat * va.normal);
        ca.uv       = va.uv;

        cb.position = glm::vec3(transform * glm::vec4(vb.position, 1.f));
        cb.normal   = glm::normalize(normalMat * vb.normal);
        cb.uv       = vb.uv;

        cc.position = glm::vec3(transform * glm::vec4(vc.position, 1.f));
        cc.normal   = glm::normalize(normalMat * vc.normal);
        cc.uv       = vc.uv;

        poly.vertices = {ca, cb, cc};

        // Compute plane from transformed positions
        const glm::vec3 edge1 = cb.position - ca.position;
        const glm::vec3 edge2 = cc.position - ca.position;
        const glm::vec3 faceNormal = glm::cross(edge1, edge2);
        if (glm::length2(faceNormal) < 1e-12f)
            continue; // degenerate triangle

        poly.plane = CSGPlane::fromPoints(ca.position, cb.position, cc.position);
        poly.matIndex = findMaterial(static_cast<uint32_t>(i));

        polygons.push_back(std::move(poly));
    }

    return polygons;
}

// ============================================================
//  buildMesh — reconstruct a Mesh from BSP polygons
// ============================================================
Mesh* buildResultMesh(const std::vector<CSGPolygon>& polygons,
                      const Mesh& meshA, const Mesh& meshB,
                      const CSG::Options& opts)
{
    if (polygons.empty()) return nullptr;

    auto* result = new Mesh();
    auto& verts = result->buffer.vertices;
    auto& indices = result->buffer.indices;

    // Group polygons by material index
    std::unordered_map<int, std::vector<const CSGPolygon*>> byMaterial;
    for (const auto& poly : polygons)
        byMaterial[poly.matIndex].push_back(&poly);

    // Copy materials from source meshes (avoid duplicates for single-mesh ops)
    for (const auto* mat : meshA.materials)
        result->materials.push_back(mat ? new Material(*mat) : nullptr);
    if (&meshA != &meshB)
    {
        for (const auto* mat : meshB.materials)
            result->materials.push_back(mat ? new Material(*mat) : nullptr);
    }

    for (auto& [matIdx, polys] : byMaterial)
    {
        const uint32_t surfStart = static_cast<uint32_t>(indices.size());

        for (const CSGPolygon* poly : polys)
        {
            if (poly->vertices.size() < 3) continue;

            // Fan triangulation
            const uint32_t base = static_cast<uint32_t>(verts.size());
            for (const auto& cv : poly->vertices)
            {
                Vertex v{};
                v.position = cv.position;
                v.normal = cv.normal;
                v.uv = cv.uv;
                v.tangent = glm::vec4(1.f, 0.f, 0.f, 1.f);
                verts.push_back(v);
            }

            for (uint32_t j = 1; j + 1 < static_cast<uint32_t>(poly->vertices.size()); ++j)
            {
                indices.push_back(base);
                indices.push_back(base + j);
                indices.push_back(base + j + 1);
            }
        }

        const uint32_t surfCount = static_cast<uint32_t>(indices.size()) - surfStart;
        if (surfCount > 0)
            result->add_surface(surfStart, surfCount, matIdx);
    }

    if (verts.empty())
    {
        delete result;
        return nullptr;
    }

    // Recompute normals and tangents
    if (opts.smoothNormals)
        result->compute_normals();
    result->compute_tangents();
    result->compute_aabb();
    result->buffer.upload();

    return result;
}

// ============================================================
//  CSG Operations via BSP trees
//
//  The algorithm (from the original csg.js by Evan Wallace):
//
//  Union(A, B):
//    a.clipTo(b)  — remove parts of A inside B
//    b.clipTo(a)  — remove parts of B inside A
//    b.clipTo(a)  — (second pass to clean coplanar faces)
//    a.build(b.allPolygons())  — merge
//
//  Subtract(A, B):
//    a.invert()
//    a.clipTo(b)
//    b.clipTo(a)
//    b.clipTo(a)
//    a.build(b.allPolygons())
//    a.invert()
//
//  Intersect(A, B):
//    a.invert()
//    b.clipTo(a)
//    b.invert()
//    a.clipTo(b)
//    b.clipTo(a)
//    a.build(b.allPolygons())
//    a.invert()
// ============================================================

Mesh* solveCSG(CSG::Operation op,
               const Mesh& A, const Mesh& B,
               const glm::mat4& matA, const glm::mat4& matB,
               const CSG::Options& opts)
{
    auto polysA = meshToPolygons(A, matA);
    auto polysB = meshToPolygons(B, matB);

    if (polysA.empty() || polysB.empty())
        return nullptr;

    auto a = std::make_unique<BSPNode>(polysA);
    auto b = std::make_unique<BSPNode>(polysB);

    switch (op)
    {
    case CSG::Operation::Union:
    {
        a->clipTo(*b);
        b->clipTo(*a);
        // Remove coplanar faces from B that are also in A
        b->invert();
        b->clipTo(*a);
        b->invert();
        a->build(b->allPolygons());
        break;
    }

    case CSG::Operation::Difference:
    {
        a->invert();
        a->clipTo(*b);
        b->clipTo(*a);
        b->invert();
        b->clipTo(*a);
        b->invert();
        a->build(b->allPolygons());
        a->invert();
        break;
    }

    case CSG::Operation::Intersection:
    {
        a->invert();
        b->clipTo(*a);
        b->invert();
        a->clipTo(*b);
        b->clipTo(*a);
        a->build(b->allPolygons());
        a->invert();
        break;
    }
    }

    auto resultPolygons = a->allPolygons();
    return buildResultMesh(resultPolygons, A, B, opts);
}

// ============================================================
//  splitByPlane — split a mesh's polygons into front and back
// ============================================================
std::pair<std::vector<CSGPolygon>, std::vector<CSGPolygon>>
splitPolygonsByPlane(const std::vector<CSGPolygon>& polygons,
                     const glm::vec3& planeNormal, float planeDist)
{
    CSGPlane splitPlane;
    splitPlane.normal = glm::normalize(planeNormal);
    splitPlane.w = planeDist;

    auto splitNode = std::make_unique<BSPNode>();
    // Create a minimal BSP with just this plane
    CSGPolygon dummyPoly;
    dummyPoly.plane = splitPlane;

    // Classify each polygon against the split plane
    std::vector<CSGPolygon> frontList, backList;
    std::vector<CSGPolygon> coFront, coBack;

    for (const auto& p : polygons)
        splitPolygon(splitPlane, p, coFront, coBack, frontList, backList);

    // Coplanar front → front, coplanar back → back
    frontList.insert(frontList.end(), coFront.begin(), coFront.end());
    backList.insert(backList.end(), coBack.begin(), coBack.end());

    return {frontList, backList};
}

// ============================================================
//  shrinkMeshPolygons — shrink polygons inward along normals
// ============================================================
std::vector<CSGPolygon> shrinkMeshPolygons(const std::vector<CSGPolygon>& polygons, float amount)
{
    // Compute per-vertex average normal from all polygons sharing that vertex
    // Then offset each vertex inward
    struct VtxInfo { glm::vec3 normalSum{0.f}; int count = 0; };
    std::unordered_map<size_t, VtxInfo> vertexNormals;

    // Simple spatial hash for vertex grouping
    auto hashPos = [](const glm::vec3& p) -> size_t {
        const auto hx = std::hash<float>{}(std::round(p.x * 1e4f) + 0.0f);
        const auto hy = std::hash<float>{}(std::round(p.y * 1e4f) + 0.0f);
        const auto hz = std::hash<float>{}(std::round(p.z * 1e4f) + 0.0f);
        size_t seed = hx;
        seed ^= hy + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hz + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    };

    // First pass: accumulate normals
    for (const auto& poly : polygons)
    {
        for (const auto& v : poly.vertices)
        {
            const size_t h = hashPos(v.position);
            vertexNormals[h].normalSum += poly.plane.normal;
            vertexNormals[h].count++;
        }
    }

    // Second pass: offset vertices
    std::vector<CSGPolygon> result;
    result.reserve(polygons.size());
    for (const auto& poly : polygons)
    {
        CSGPolygon newPoly = poly;
        for (auto& v : newPoly.vertices)
        {
            const size_t h = hashPos(v.position);
            const auto& info = vertexNormals[h];
            const glm::vec3 sumN = info.normalSum / static_cast<float>(info.count);
            const float len2 = glm::length2(sumN);
            if (len2 < 1e-12f) continue; // skip degenerate vertex (opposing normals cancel)
            const glm::vec3 avgNormal = sumN / std::sqrt(len2);
            v.position -= avgNormal * amount;
        }

        // Recompute the polygon plane
        if (newPoly.vertices.size() >= 3)
        {
            const glm::vec3 e1 = newPoly.vertices[1].position - newPoly.vertices[0].position;
            const glm::vec3 e2 = newPoly.vertices[2].position - newPoly.vertices[0].position;
            if (glm::length2(glm::cross(e1, e2)) > 1e-12f)
            {
                newPoly.plane = CSGPlane::fromPoints(
                    newPoly.vertices[0].position,
                    newPoly.vertices[1].position,
                    newPoly.vertices[2].position);
            }
        }

        if (!newPoly.isDegenerate())
            result.push_back(std::move(newPoly));
    }

    return result;
}

} // anonymous namespace

// ============================================================
//  Public API
// ============================================================
namespace CSG
{

Mesh* makeUnion(const Mesh& A, const Mesh& B,
                const glm::mat4& matA, const glm::mat4& matB,
                const Options& opts)
{ return solveCSG(Operation::Union, A, B, matA, matB, opts); }

Mesh* makeDifference(const Mesh& A, const Mesh& B,
                     const glm::mat4& matA, const glm::mat4& matB,
                     const Options& opts)
{ return solveCSG(Operation::Difference, A, B, matA, matB, opts); }

Mesh* makeIntersection(const Mesh& A, const Mesh& B,
                       const glm::mat4& matA, const glm::mat4& matB,
                       const Options& opts)
{ return solveCSG(Operation::Intersection, A, B, matA, matB, opts); }

Mesh* compute(Operation op,
              const Mesh& A, const Mesh& B,
              const glm::mat4& matA, const glm::mat4& matB,
              const Options& opts)
{ return solveCSG(op, A, B, matA, matB, opts); }

Mesh* makeSymmetricDifference(const Mesh& A, const Mesh& B,
                               const glm::mat4& matA, const glm::mat4& matB,
                               const Options& opts)
{
    // XOR = Union(A,B) - Intersection(A,B)
    // Or equivalently: Union(A-B, B-A)
    auto polysA = meshToPolygons(A, matA);
    auto polysB = meshToPolygons(B, matB);
    if (polysA.empty() || polysB.empty()) return nullptr;

    // A - B
    auto a1 = std::make_unique<BSPNode>(polysA);
    auto b1 = std::make_unique<BSPNode>(polysB);
    a1->invert();
    a1->clipTo(*b1);
    b1->clipTo(*a1);
    b1->invert();
    b1->clipTo(*a1);
    b1->invert();
    a1->build(b1->allPolygons());
    a1->invert();
    auto diffAB = a1->allPolygons();

    // B - A
    auto a2 = std::make_unique<BSPNode>(polysA);
    auto b2 = std::make_unique<BSPNode>(polysB);
    b2->invert();
    b2->clipTo(*a2);
    a2->clipTo(*b2);
    a2->invert();
    a2->clipTo(*b2);
    a2->invert();
    b2->build(a2->allPolygons());
    b2->invert();
    auto diffBA = b2->allPolygons();

    // Merge both
    diffAB.insert(diffAB.end(), diffBA.begin(), diffBA.end());
    return buildResultMesh(diffAB, A, B, opts);
}

Mesh* makeInvert(const Mesh& A, const glm::mat4& matA, const Options& opts)
{
    auto polys = meshToPolygons(A, matA);
    if (polys.empty()) return nullptr;

    // Flip every polygon (reverse winding, negate normals)
    for (auto& p : polys)
        p.flip();

    return buildResultMesh(polys, A, A, opts);
}

SplitResult makeSplit(const Mesh& A,
                       const glm::vec3& planeNormal, float planeDist,
                       const glm::mat4& matA, const Options& opts)
{
    auto polys = meshToPolygons(A, matA);
    if (polys.empty()) return {};

    auto [frontPolys, backPolys] = splitPolygonsByPlane(polys, planeNormal, planeDist);

    SplitResult result;
    if (!frontPolys.empty())
        result.front = buildResultMesh(frontPolys, A, A, opts);
    if (!backPolys.empty())
        result.back = buildResultMesh(backPolys, A, A, opts);
    return result;
}

Mesh* makeHollow(const Mesh& A, float thickness,
                  const glm::mat4& matA, const Options& opts)
{
    auto outerPolys = meshToPolygons(A, matA);
    if (outerPolys.empty()) return nullptr;

    // Create inner (shrunk) version
    auto innerPolys = shrinkMeshPolygons(outerPolys, thickness);
    if (innerPolys.empty()) return nullptr;

    // Build BSP trees
    auto outer = std::make_unique<BSPNode>(outerPolys);
    auto inner = std::make_unique<BSPNode>(innerPolys);

    // Subtract inner from outer: Outer - Inner
    outer->invert();
    outer->clipTo(*inner);
    inner->clipTo(*outer);
    inner->invert();
    inner->clipTo(*outer);
    inner->invert();
    outer->build(inner->allPolygons());
    outer->invert();

    auto resultPolygons = outer->allPolygons();
    return buildResultMesh(resultPolygons, A, A, opts);
}

} // namespace CSG
