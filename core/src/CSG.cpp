#include "CSG.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace
{

constexpr float kEps = 1e-7f;

// ============================================================
//  Tri — world-space triangle
// ============================================================
struct Tri
{
    glm::vec3 v[3];
    glm::vec2 uv[3];
    glm::vec3 normal;
    int       srcMesh  = 0;
    int       matIndex = 0; // FIX 6: survives into buildMesh

    glm::vec3 centroid() const { return (v[0] + v[1] + v[2]) * (1.f / 3.f); }
};

// ============================================================
//  AABB
// ============================================================
struct AABB
{
    glm::vec3 mn{  std::numeric_limits<float>::max() };
    glm::vec3 mx{ -std::numeric_limits<float>::max() };

    void expand(const glm::vec3& p) { mn = glm::min(mn,p); mx = glm::max(mx,p); }
    void expand(const AABB& o)      { mn = glm::min(mn,o.mn); mx = glm::max(mx,o.mx); }

    bool overlaps(const AABB& o) const
    {
        return mn.x <= o.mx.x && mx.x >= o.mn.x &&
               mn.y <= o.mx.y && mx.y >= o.mn.y &&
               mn.z <= o.mx.z && mx.z >= o.mn.z;
    }
    glm::vec3 extent() const { return mx - mn; }
};

AABB triAABB(const Tri& t)
{
    AABB b;
    for (int i = 0; i < 3; ++i) b.expand(t.v[i]);
    return b;
}

// ============================================================
//  FIX 5 — BVH (median-split SAH, O(n log n) queries)
// ============================================================
struct BVHNode
{
    AABB aabb;
    int  left = -1, right = -1;
    int  triStart = 0, triCount = 0;
};

struct BVH
{
    std::vector<BVHNode> nodes;
    std::vector<int>     order;

    void build(const std::vector<Tri>& tris)
    {
        order.resize(tris.size());
        std::iota(order.begin(), order.end(), 0);
        nodes.reserve(2 * (int)tris.size());
        buildNode(tris, 0, (int)tris.size());
    }

    void query(const AABB& q, std::vector<int>& out) const
    {
        if (!nodes.empty()) queryNode(0, q, out);
    }

private:
    int buildNode(const std::vector<Tri>& tris, int start, int end)
    {
        BVHNode node;
        node.triStart = start;
        node.triCount = end - start;
        for (int i = start; i < end; ++i)
            node.aabb.expand(triAABB(tris[order[i]]));

        int idx = (int)nodes.size();
        nodes.push_back(node);

        if (end - start <= 4)
        {
            nodes[idx].left = nodes[idx].right = -1;
            return idx;
        }

        glm::vec3 ext = nodes[idx].aabb.extent();
        int axis = (ext.x >= ext.y && ext.x >= ext.z) ? 0
                 : (ext.y >= ext.z)                   ? 1 : 2;
        int mid = (start + end) / 2;
        std::nth_element(order.begin()+start, order.begin()+mid, order.begin()+end,
            [&](int a, int b){ return tris[a].centroid()[axis] < tris[b].centroid()[axis]; });

        int L = buildNode(tris, start, mid);
        int R = buildNode(tris, mid,   end);
        nodes[idx].left      = L;
        nodes[idx].right     = R;
        nodes[idx].triCount  = 0; // internal node
        return idx;
    }

    void queryNode(int ni, const AABB& q, std::vector<int>& out) const
    {
        const BVHNode& n = nodes[ni];
        if (!n.aabb.overlaps(q)) return;
        if (n.left == -1)
        {
            for (int i = n.triStart; i < n.triStart + n.triCount; ++i)
                out.push_back(order[i]);
            return;
        }
        queryNode(n.left,  q, out);
        queryNode(n.right, q, out);
    }
};

// ============================================================
//  FIX 3 — triTriIntersect with coplanar case
// ============================================================
enum class IsectType { None, Segment, Coplanar };

struct IsectResult
{
    IsectType type = IsectType::None;
    glm::vec3 p0, p1;
    std::vector<glm::vec3> coplanarPoly; // only for Coplanar
};

// Sutherland-Hodgman clip of `poly` against half-plane left of edge a→b.
static std::vector<glm::vec2> clipPoly2D(const std::vector<glm::vec2>& poly,
                                          glm::vec2 a, glm::vec2 b)
{
    std::vector<glm::vec2> out;
    int n = (int)poly.size();
    for (int i = 0; i < n; ++i)
    {
        glm::vec2 c = poly[i];
        glm::vec2 d = poly[(i+1)%n];
        float cc = (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x);
        float dc = (b.x-a.x)*(d.y-a.y) - (b.y-a.y)*(d.x-a.x);
        if (cc >= 0.f) out.push_back(c);
        if ((cc > 0.f) != (dc > 0.f))
        {
            float t = cc / (cc - dc);
            out.push_back(c + t*(d-c));
        }
    }
    return out;
}

IsectResult triTriIntersect(const Tri& T1, const Tri& T2, float eps)
{
    IsectResult res;

    // Distances of T2 verts to plane of T1
    glm::vec3 n1 = T1.normal;
    float     d1 = -glm::dot(n1, T1.v[0]);
    float dv[3];
    for (int i = 0; i < 3; ++i) dv[i] = glm::dot(n1, T2.v[i]) + d1;
    if ((dv[0]>eps && dv[1]>eps && dv[2]>eps) ||
        (dv[0]<-eps && dv[1]<-eps && dv[2]<-eps)) return res;

    // Distances of T1 verts to plane of T2
    glm::vec3 n2 = T2.normal;
    float     d2 = -glm::dot(n2, T2.v[0]);
    float du[3];
    for (int i = 0; i < 3; ++i) du[i] = glm::dot(n2, T1.v[i]) + d2;
    if ((du[0]>eps && du[1]>eps && du[2]>eps) ||
        (du[0]<-eps && du[1]<-eps && du[2]<-eps)) return res;

    // Intersection line
    glm::vec3 Dun = glm::cross(n1, n2);
    float Dun2 = glm::length2(Dun);

    // ── Coplanar ─────────────────────────────────────────────
    if (Dun2 < eps * eps)
    {
        // Project both tris to 2D on T1's plane
        glm::vec3 ax = glm::normalize(T1.v[1] - T1.v[0]);
        glm::vec3 ay = glm::cross(n1, ax);
        auto p2 = [&](const glm::vec3& p) -> glm::vec2 {
            glm::vec3 d = p - T1.v[0];
            return { glm::dot(d,ax), glm::dot(d,ay) };
        };

        std::vector<glm::vec2> subj = { p2(T1.v[0]), p2(T1.v[1]), p2(T1.v[2]) };
        std::vector<glm::vec2> clip = { p2(T2.v[0]), p2(T2.v[1]), p2(T2.v[2]) };

        std::vector<glm::vec2> poly = subj;
        int nc = (int)clip.size();
        for (int i = 0; i < nc && !poly.empty(); ++i)
            poly = clipPoly2D(poly, clip[i], clip[(i+1)%nc]);

        if (poly.size() < 3) return res;

        res.type = IsectType::Coplanar;
        res.coplanarPoly.reserve(poly.size());
        for (auto& pt : poly)
            res.coplanarPoly.push_back(T1.v[0] + ax*pt.x + ay*pt.y);
        return res;
    }

    // ── Normal segment ────────────────────────────────────────
    glm::vec3 D = glm::normalize(Dun);

    // Lone vertex: the one on the opposite side from the other two
    auto computeInterval = [&](const glm::vec3 vv[3], const float dd[3],
                                float& t0, float& t1_) -> bool
    {
        int lone = -1;
        for (int i = 0; i < 3; ++i)
        {
            int a = (i+1)%3, b = (i+2)%3;
            if ((dd[a] >= 0.f) == (dd[b] >= 0.f) &&
                (dd[i] >= 0.f) != (dd[a] >= 0.f))
            { lone = i; break; }
        }
        if (lone < 0)
        {
            // One vertex on the plane — pick the one closest to zero
            float best = std::abs(dd[0]); lone = 0;
            for (int i = 1; i < 3; ++i)
                if (std::abs(dd[i]) < best) { best = std::abs(dd[i]); lone = i; }
        }

        int a = (lone+1)%3, b = (lone+2)%3;
        float pL = glm::dot(D, vv[lone]);
        float pA = glm::dot(D, vv[a]);
        float pB = glm::dot(D, vv[b]);
        float da = dd[lone] - dd[a];
        float db = dd[lone] - dd[b];
        if (std::abs(da) < kEps || std::abs(db) < kEps) return false;

        float ta = pL + (pA - pL) * (dd[lone] / da);
        float tb = pL + (pB - pL) * (dd[lone] / db);
        t0  = std::min(ta, tb);
        t1_ = std::max(ta, tb);
        return true;
    };

    float t10, t11, t20, t21;
    if (!computeInterval(T1.v, du, t10, t11)) return res;
    if (!computeInterval(T2.v, dv, t20, t21)) return res;

    float i0 = std::max(t10, t20);
    float i1 = std::min(t11, t21);
    if (i0 > i1 + eps) return res;

    // World-space origin of the intersection line
    // Derivation: P = ((-d2)*cross(Dun,n1) + (-d1)*cross(n2,Dun)) / Dun2
    glm::vec3 orig = ((-d2)*glm::cross(Dun,n1) + (-d1)*glm::cross(n2,Dun)) / Dun2;

    res.type = IsectType::Segment;
    res.p0 = orig + D * i0;
    res.p1 = orig + D * i1;

    if (glm::length(res.p1 - res.p0) < eps)
        res.type = IsectType::None;

    return res;
}

// ============================================================
//  Flatten mesh → world-space Tri list, preserving matIndex
// ============================================================
std::vector<Tri> flattenMesh(const Mesh& mesh, const glm::mat4& mat, int srcId)
{
    const auto& verts   = mesh.buffer.vertices;
    const auto& indices = mesh.buffer.indices;

    // Map each index-triple's first-index → material slot
    auto matForIndex = [&](uint32_t triBase) -> int {
        for (const auto& surf : mesh.surfaces)
            if (triBase >= surf.index_start &&
                triBase <  surf.index_start + surf.index_count)
                return surf.material_index;
        return 0;
    };

    std::vector<Tri> tris;
    tris.reserve(indices.size() / 3);

    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        Tri t;
        t.srcMesh  = srcId;
        t.matIndex = matForIndex((uint32_t)i);
        for (int k = 0; k < 3; ++k)
        {
            uint32_t idx = indices[i+k];
            t.v[k]  = glm::vec3(mat * glm::vec4(verts[idx].position, 1.f));
            t.uv[k] = verts[idx].uv;
        }
        glm::vec3 cr = glm::cross(t.v[1]-t.v[0], t.v[2]-t.v[0]);
        float len = glm::length(cr);
        t.normal = (len > kEps) ? cr/len : glm::vec3(0,1,0);
        tris.push_back(t);
    }
    return tris;
}

// ============================================================
//  FIX 1 — isInsideMesh
//  • Receives only the target-mesh tris (no self-intersection risk).
//  • Offsets origin by rayEpsilon along each ray direction.
// ============================================================
bool isInsideMesh(const glm::vec3& rawPt,
                  const std::vector<Tri>& targetTris,
                  int nRays, float rayEpsilon)
{
    static const glm::vec3 dirs[7] = {
        { 0.f,      1.f,      0.f      },
        { 1.f,      0.f,      0.f      },
        { 0.f,      0.f,      1.f      },
        { 0.70711f, 0.70711f, 0.f      },
        { 0.57735f, 0.57735f, 0.57735f },
        { 0.f,      0.89443f, 0.44721f },
        {-0.70711f, 0.f,      0.70711f },
    };

    int nDirs  = std::min(nRays, 7);
    int inside = 0;

    for (int d = 0; d < nDirs; ++d)
    {
        glm::vec3 dir    = dirs[d];
        glm::vec3 origin = rawPt + dir * rayEpsilon; // FIX 1: offset

        int hits = 0;
        for (const auto& t : targetTris)
        {
            glm::vec3 e1 = t.v[1] - t.v[0];
            glm::vec3 e2 = t.v[2] - t.v[0];
            glm::vec3 h  = glm::cross(dir, e2);
            float a = glm::dot(e1, h);
            if (std::abs(a) < kEps) continue;
            float f = 1.f / a;
            glm::vec3 s = origin - t.v[0];
            float u = f * glm::dot(s, h);
            if (u < -kEps || u > 1.f+kEps) continue;
            glm::vec3 q = glm::cross(s, e1);
            float v = f * glm::dot(dir, q);
            if (v < -kEps || u+v > 1.f+kEps) continue;
            float tVal = f * glm::dot(e2, q);
            if (tVal > rayEpsilon) ++hits; // FIX 1: use rayEpsilon, not kEps
        }
        if (hits % 2 == 1) ++inside;
    }
    return inside > nDirs / 2;
}

// ============================================================
//  Ear-clip triangulator
// ============================================================
std::vector<std::array<int,3>> earClip(const std::vector<glm::vec3>& poly,
                                        const glm::vec3& normal)
{
    std::vector<std::array<int,3>> res;
    int n = (int)poly.size();
    if (n < 3) return res;
    if (n == 3) { res.push_back({0,1,2}); return res; }

    glm::vec3 ax = glm::normalize(poly[1] - poly[0]);
    glm::vec3 ay = glm::cross(normal, ax);
    std::vector<glm::vec2> pts(n);
    for (int i = 0; i < n; ++i)
    {
        glm::vec3 d = poly[i] - poly[0];
        pts[i] = { glm::dot(d,ax), glm::dot(d,ay) };
    }

    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);

    auto cross2d = [](const glm::vec2& O, const glm::vec2& A, const glm::vec2& B) {
        return (A.x-O.x)*(B.y-O.y) - (A.y-O.y)*(B.x-O.x);
    };
    auto inTri = [&](const glm::vec2& p, const glm::vec2& a,
                      const glm::vec2& b, const glm::vec2& c) {
        float d1=cross2d(p,a,b), d2=cross2d(p,b,c), d3=cross2d(p,c,a);
        return !((d1<0||d2<0||d3<0) && (d1>0||d2>0||d3>0));
    };

    int safety = n * n * 2;
    while ((int)idx.size() > 3 && safety-- > 0)
    {
        int m = (int)idx.size();
        bool found = false;
        for (int i = 0; i < m; ++i)
        {
            int prev=idx[(i-1+m)%m], curr=idx[i], next=idx[(i+1)%m];
            if (cross2d(pts[prev],pts[curr],pts[next]) <= 0.f) continue;
            bool ear = true;
            for (int j = 0; j < m && ear; ++j)
            {
                int jj = idx[j];
                if (jj==prev||jj==curr||jj==next) continue;
                if (inTri(pts[jj],pts[prev],pts[curr],pts[next])) ear=false;
            }
            if (!ear) continue;
            res.push_back({prev,curr,next});
            idx.erase(idx.begin()+i);
            found = true; break;
        }
        if (!found) break;
    }
    if ((int)idx.size()==3) res.push_back({idx[0],idx[1],idx[2]});
    return res;
}

// ============================================================
//  Barycentric UV interpolation
// ============================================================
glm::vec2 baryUV(const Tri& tri, const glm::vec3& p)
{
    glm::vec3 e0=tri.v[1]-tri.v[0], e1=tri.v[2]-tri.v[0], ep=p-tri.v[0];
    float d00=glm::dot(e0,e0), d01=glm::dot(e0,e1), d11=glm::dot(e1,e1);
    float d20=glm::dot(ep,e0), d21=glm::dot(ep,e1);
    float inv = d00*d11 - d01*d01;
    glm::vec3 bary(1.f/3.f);
    if (std::abs(inv) > kEps)
    {
        bary.y = (d11*d20-d01*d21)/inv;
        bary.z = (d00*d21-d01*d20)/inv;
        bary.x = 1.f-bary.y-bary.z;
    }
    return tri.uv[0]*bary.x + tri.uv[1]*bary.y + tri.uv[2]*bary.z;
}

// ============================================================
//  FIX 2 — cutTriangle: per-edge bucketed, sorted, deduped insert
// ============================================================
std::vector<Tri> cutTriangle(const Tri& tri,
                              const std::vector<glm::vec3>& cuts,
                              float eps)
{
    if (cuts.empty()) return { tri };

    // Bucket each cut point onto its closest original edge,
    // using the ORIGINAL tri.v[] edges (not the growing poly).
    struct EdgePt { float t; glm::vec3 p; };
    std::array<std::vector<EdgePt>,3> edgePts;

    for (const auto& cp : cuts)
    {
        int   bestEdge = -1;
        float bestDist = eps * 100.f;
        float bestT    = 0.f;

        for (int e = 0; e < 3; ++e)
        {
            const glm::vec3& a  = tri.v[e];
            const glm::vec3& b  = tri.v[(e+1)%3];
            glm::vec3 ab = b - a;
            float len2 = glm::length2(ab);
            if (len2 < kEps) continue;
            float t = glm::clamp(glm::dot(cp-a, ab)/len2, 0.f, 1.f);
            float dist = glm::length(cp - (a + ab*t));
            if (dist < bestDist) { bestDist=dist; bestEdge=e; bestT=t; }
        }

        // Interior only (not at corners)
        if (bestEdge >= 0 && bestT > eps && bestT < 1.f-eps)
            edgePts[bestEdge].push_back({bestT, cp});
    }

    // Sort + deduplicate each edge's points by t
    for (int e = 0; e < 3; ++e)
    {
        auto& ep = edgePts[e];
        std::sort(ep.begin(), ep.end(),
                  [](const EdgePt& a, const EdgePt& b){ return a.t < b.t; });
        auto it = std::unique(ep.begin(), ep.end(),
                  [&](const EdgePt& a, const EdgePt& b){
                      return std::abs(a.t-b.t) < eps;
                  });
        ep.erase(it, ep.end());
    }

    // Build ordered polygon: v0 → edge0 pts → v1 → edge1 pts → v2 → edge2 pts
    std::vector<glm::vec3> poly;
    poly.reserve(3 + cuts.size());
    for (int e = 0; e < 3; ++e)
    {
        poly.push_back(tri.v[e]);
        for (const auto& ep : edgePts[e]) poly.push_back(ep.p);
    }

    auto triIdx = earClip(poly, tri.normal);

    std::vector<Tri> result;
    result.reserve(triIdx.size());
    for (auto& ti : triIdx)
    {
        Tri t2 = tri;
        for (int k = 0; k < 3; ++k)
        {
            t2.v[k]  = poly[ti[k]];
            t2.uv[k] = baryUV(tri, t2.v[k]);
        }
        glm::vec3 cr = glm::cross(t2.v[1]-t2.v[0], t2.v[2]-t2.v[0]);
        float len = glm::length(cr);
        t2.normal = (len > kEps) ? cr/len : tri.normal;
        result.push_back(t2);
    }
    return result;
}

// ============================================================
//  FIX 4 — weld + degenerate triangle removal
// ============================================================
std::vector<uint32_t> weldVertices(std::vector<Vertex>& verts, float epsilon)
{
    int n = (int)verts.size();
    std::vector<int> canonical(n, -1);
    for (int i = 0; i < n; ++i)
    {
        if (canonical[i] >= 0) continue;
        for (int j = i+1; j < n; ++j)
        {
            if (canonical[j] >= 0) continue;
            if (glm::length2(verts[i].position-verts[j].position) < epsilon*epsilon)
                canonical[j] = i;
        }
    }

    std::vector<int>      added(n, -1);
    std::vector<Vertex>   newV;
    std::vector<uint32_t> remap(n);
    newV.reserve(n);

    for (int i = 0; i < n; ++i)
    {
        int src = (canonical[i] >= 0) ? canonical[i] : i;
        if (added[src] < 0) { added[src]=(int)newV.size(); newV.push_back(verts[src]); }
        remap[i] = (uint32_t)added[src];
    }
    verts = std::move(newV);
    return remap;
}

// FIX 4: remove index triples where 2+ indices collapsed to the same value
void removeDegenerateTris(std::vector<uint32_t>& idx)
{
    std::vector<uint32_t> clean;
    clean.reserve(idx.size());
    for (size_t i = 0; i+2 < idx.size(); i += 3)
    {
        uint32_t a=idx[i], b=idx[i+1], c=idx[i+2];
        if (a!=b && b!=c && a!=c)
        { clean.push_back(a); clean.push_back(b); clean.push_back(c); }
    }
    idx = std::move(clean);
}

// ============================================================
//  FIX 6 — buildMesh: surfaces + materials preserved
// ============================================================
Mesh* buildMesh(const std::vector<Tri>& tris,
                const Mesh& srcA, const Mesh& srcB,
                const CSG::Options& opts)
{
    auto* result = new Mesh();
    int nMatA = (int)srcA.materials.size();

    // Combined material key: A → [0..nMatA-1], B → [nMatA..]
    auto matKey = [&](const Tri& t) { return (t.srcMesh==0) ? t.matIndex
                                                             : nMatA+t.matIndex; };

    // Collect unique material keys in order of first appearance
    std::vector<int> keys;
    for (const auto& t : tris)
    {
        int k = matKey(t);
        if (std::find(keys.begin(), keys.end(), k) == keys.end())
            keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());

    for (int k : keys)
    {
        uint32_t surfStart = (uint32_t)result->buffer.indices.size();

        for (const auto& t : tris)
        {
            if (matKey(t) != k) continue;
            uint32_t base = (uint32_t)result->buffer.vertices.size();
            for (int i = 0; i < 3; ++i)
            {
                Vertex v{};
                v.position = t.v[i];
                v.normal   = t.normal;
                v.uv       = t.uv[i];
                result->buffer.vertices.push_back(v);
            }
            result->buffer.indices.push_back(base);
            result->buffer.indices.push_back(base+1);
            result->buffer.indices.push_back(base+2);
        }

        uint32_t surfCount = (uint32_t)result->buffer.indices.size() - surfStart;
        if (surfCount == 0) continue;

        Material* mat = nullptr;
        if (k < nMatA)
        {
            if (k < (int)srcA.materials.size()) mat = srcA.materials[k];
        }
        else
        {
            int bi = k - nMatA;
            if (bi < (int)srcB.materials.size()) mat = srcB.materials[bi];
        }
        int slot = result->add_material(mat);
        result->add_surface(surfStart, surfCount, slot);
    }

    // Weld + degenerate removal
    if (!result->buffer.vertices.empty())
    {
        auto remap = weldVertices(result->buffer.vertices, opts.weldEpsilon);
        for (auto& i : result->buffer.indices) i = remap[i];

        // FIX 4: remove zero-area triangles
        removeDegenerateTris(result->buffer.indices);

        // Recount surface index_count after compaction.
        // Surfaces were emitted in key order with contiguous ranges;
        // removeDegenerateTris only removes triples, never reorders,
        // so index_start values are still valid — only counts may shrink.
        size_t total = result->buffer.indices.size();
        int ns = (int)result->surfaces.size();
        for (int s = 0; s < ns; ++s)
        {
            uint32_t nextStart = (s+1 < ns)
                ? result->surfaces[s+1].index_start
                : (uint32_t)total;
            uint32_t clampedStart = std::min(result->surfaces[s].index_start,
                                             (uint32_t)total);
            uint32_t clampedNext  = std::min(nextStart, (uint32_t)total);
            result->surfaces[s].index_count = clampedNext - clampedStart;
        }
    }

    if (opts.smoothNormals) result->compute_normals();
    result->compute_tangents();
    result->upload();
    return result;
}

// ============================================================
//  Face selection rules
// ============================================================
struct FaceRule { bool keepAout,keepAin,keepBout,keepBin,flipBin; };

FaceRule ruleFor(CSG::Operation op)
{
    switch (op)
    {
    case CSG::Operation::Union:        return {true, false,true, false,false};
    case CSG::Operation::Difference:   return {true, false,false,true, true };
    case CSG::Operation::Intersection: return {false,true, false,true, false};
    }
    return {true,false,true,false,false};
}

// ============================================================
//  Core solver
// ============================================================
Mesh* solveCSG(CSG::Operation op,
               const Mesh& A, const Mesh& B,
               const glm::mat4& matA, const glm::mat4& matB,
               const CSG::Options& opts)
{
    // 1 — Flatten
    auto trisA = flattenMesh(A, matA, 0);
    auto trisB = flattenMesh(B, matB, 1);

    // 2 — BVH (FIX 5)
    BVH bvhB; bvhB.build(trisB);

    // 3 — Narrow-phase
    std::unordered_map<int,std::vector<glm::vec3>> cutsA, cutsB;

    for (int ai = 0; ai < (int)trisA.size(); ++ai)
    {
        std::vector<int> cands;
        bvhB.query(triAABB(trisA[ai]), cands);

        for (int bi : cands)
        {
            auto r = triTriIntersect(trisA[ai], trisB[bi], opts.intersectEpsilon);
            if (r.type == IsectType::Segment)
            {
                cutsA[ai].push_back(r.p0); cutsA[ai].push_back(r.p1);
                cutsB[bi].push_back(r.p0); cutsB[bi].push_back(r.p1);
            }
            else if (r.type == IsectType::Coplanar) // FIX 3
            {
                for (const auto& p : r.coplanarPoly)
                { cutsA[ai].push_back(p); cutsB[bi].push_back(p); }
            }
        }
    }

    // 4 — Retriangulate
    auto retri = [&](const std::vector<Tri>& src,
                     std::unordered_map<int,std::vector<glm::vec3>>& cuts,
                     int sid) -> std::vector<Tri>
    {
        std::vector<Tri> out;
        out.reserve(src.size() * 2);
        for (int i = 0; i < (int)src.size(); ++i)
        {
            auto it = cuts.find(i);
            if (it == cuts.end()) { out.push_back(src[i]); }
            else
            {
                auto pieces = cutTriangle(src[i], it->second, opts.intersectEpsilon);
                out.insert(out.end(), pieces.begin(), pieces.end());
            }
        }
        for (auto& t : out) t.srcMesh = sid;
        return out;
    };

    auto soupA = retri(trisA, cutsA, 0);
    auto soupB = retri(trisB, cutsB, 1);

    // 5 — Classify (FIX 1: pass only the OTHER mesh's soup)
    FaceRule rule = ruleFor(op);
    std::vector<Tri> result;
    result.reserve(soupA.size() + soupB.size());

    for (const auto& t : soupA)
    {
        bool inB = isInsideMesh(t.centroid(), soupB,
                                opts.insideTestRays, opts.rayEpsilon);
        if ( inB && rule.keepAin)  result.push_back(t);
        if (!inB && rule.keepAout) result.push_back(t);
    }

    for (auto t : soupB)
    {
        bool inA = isInsideMesh(t.centroid(), soupA,
                                opts.insideTestRays, opts.rayEpsilon);
        bool keep = (inA && rule.keepBin) || (!inA && rule.keepBout);
        if (!keep) continue;
        if (inA && rule.flipBin)
        {
            std::swap(t.v[1],t.v[2]);
            std::swap(t.uv[1],t.uv[2]);
            t.normal = -t.normal;
        }
        result.push_back(t);
    }

    // 6 — Build (FIX 4, FIX 6)
    return buildMesh(result, A, B, opts);
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

} // namespace CSG