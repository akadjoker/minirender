#include "CollisionSystem.hpp"

#include <algorithm>
#include <numeric>
#include <glm/gtx/norm.hpp>

namespace collision_study
{

namespace
{
static constexpr float kParallelEps = 1e-8f;
static constexpr float kZeroLenEps = 1e-12f;

struct SweepHit
{
    float t = 1.0f;
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 contact = glm::vec3(0.0f);
    int triIndex = -1;
};

static bool solveQuadratic(float a, float b, float c, float maxT, float &t)
{
    if (glm::abs(a) < kZeroLenEps)
        return false;

    const float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f)
        return false;

    const float sqrtD = glm::sqrt(discriminant);
    const float inv2a = 1.0f / (2.0f * a);

    float t0 = (-b - sqrtD) * inv2a;
    float t1 = (-b + sqrtD) * inv2a;
    if (t0 > t1)
        std::swap(t0, t1);

    if (t0 >= 0.0f && t0 <= maxT)
    {
        t = t0;
        return true;
    }
    if (t1 >= 0.0f && t1 <= maxT)
    {
        t = t1;
        return true;
    }
    return false;
}

static bool sweepSphereVsPlane(const glm::vec3 &from,
                               const glm::vec3 &delta,
                               float radius,
                               const glm::vec3 &faceNormal,
                               float planeD,
                               float maxT,
                               float &t,
                               glm::vec3 &sweepNormal)
{
    sweepNormal = faceNormal;
    float activePlaneD = planeD;
    float denom = glm::dot(sweepNormal, delta);

    if (denom > 0.0f)
    {
        sweepNormal = -sweepNormal;
        activePlaneD = -activePlaneD;
        denom = -denom;
    }

    if (denom >= -kParallelEps)
        return false;

    const float distFromPlane = glm::dot(sweepNormal, from) - activePlaneD;
    if (distFromPlane < -radius)
        return false;

    t = (radius - distFromPlane) / denom;
    return (t >= 0.0f && t <= maxT);
}

static bool sweepSphereVsPoint(const glm::vec3 &from,
                               const glm::vec3 &delta,
                               float radius,
                               const glm::vec3 &p,
                               float maxT,
                               float &t)
{
    const glm::vec3 oc = from - p;
    const float a = glm::dot(delta, delta);
    const float b = 2.0f * glm::dot(oc, delta);
    const float c = glm::dot(oc, oc) - radius * radius;
    return solveQuadratic(a, b, c, maxT, t);
}

static bool sweepSphereVsSegment(const glm::vec3 &from,
                                 const glm::vec3 &delta,
                                 float radius,
                                 const glm::vec3 &segA,
                                 const glm::vec3 &segB,
                                 float maxT,
                                 float &t,
                                 glm::vec3 &normal)
{
    const glm::vec3 edgeVec = segB - segA;
    const float edgeLen2 = glm::length2(edgeVec);

    if (edgeLen2 < kZeroLenEps)
    {
        if (!sweepSphereVsPoint(from, delta, radius, segA, maxT, t))
            return false;
        normal = glm::normalize(from + delta * t - segA);
        return true;
    }

    const float edgeLen = glm::sqrt(edgeLen2);
    const glm::vec3 edgeDir = edgeVec / edgeLen;
    const glm::vec3 fromRel = from - segA;
    const float fromProj = glm::dot(fromRel, edgeDir);
    const float deltaProj = glm::dot(delta, edgeDir);

    const glm::vec3 fromPerp = fromRel - edgeDir * fromProj;
    const glm::vec3 deltaPerp = delta - edgeDir * deltaProj;

    const float a = glm::dot(deltaPerp, deltaPerp);
    const float b = 2.0f * glm::dot(fromPerp, deltaPerp);
    const float c = glm::dot(fromPerp, fromPerp) - radius * radius;

    float tCyl = maxT;
    if (solveQuadratic(a, b, c, maxT, tCyl))
    {
        const float hitProj = fromProj + deltaProj * tCyl;
        if (hitProj >= 0.0f && hitProj <= edgeLen)
        {
            t = tCyl;
            const glm::vec3 sphereCenter = from + delta * t;
            const glm::vec3 closest = segA + edgeDir * hitProj;
            const glm::vec3 n = sphereCenter - closest;
            normal = glm::length2(n) > kZeroLenEps ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
            return true;
        }
    }

    float tA = maxT;
    float tB = maxT;
    const bool hitA = sweepSphereVsPoint(from, delta, radius, segA, maxT, tA);
    const bool hitB = sweepSphereVsPoint(from, delta, radius, segB, maxT, tB);
    if (!hitA && !hitB)
        return false;

    if (hitA && (!hitB || tA <= tB))
    {
        t = tA;
        normal = glm::normalize(from + delta * t - segA);
    }
    else
    {
        t = tB;
        normal = glm::normalize(from + delta * t - segB);
    }
    return true;
}

static bool sweepSphereVsTriangle(const glm::vec3 &from,
                                  const glm::vec3 &delta,
                                  float radius,
                                  const Triangle &tri,
                                  int triIndex,
                                  SweepHit &hit)
{
    const glm::vec3 pn = tri.normal();
    const float planeD = glm::dot(pn, tri.v0);

    bool updated = false;
    float bestT = hit.t;

    float tFace = bestT;
    glm::vec3 faceHitNormal = pn;
    if (sweepSphereVsPlane(from, delta, radius, pn, planeD, bestT, tFace, faceHitNormal))
    {
        const glm::vec3 contactOnPlane = (from + delta * tFace) - faceHitNormal * radius;
        if (tri.contains(contactOnPlane, 1e-4f))
        {
            bestT = tFace;
            hit.t = tFace;
            hit.normal = faceHitNormal;
            hit.contact = contactOnPlane;
            hit.triIndex = triIndex;
            updated = true;
        }
    }

    for (int edge = 0; edge < 3; ++edge)
    {
        const glm::vec3 &a = edge == 0 ? tri.v0 : (edge == 1 ? tri.v1 : tri.v2);
        const glm::vec3 &b = edge == 0 ? tri.v1 : (edge == 1 ? tri.v2 : tri.v0);

        float t;
        glm::vec3 n;
        if (sweepSphereVsSegment(from, delta, radius, a, b, bestT, t, n))
        {
            bestT = t;
            hit.t = t;
            hit.normal = n;
            hit.contact = from + delta * t - n * radius;
            hit.triIndex = triIndex;
            updated = true;
        }
    }

    return updated;
}

static glm::vec3 closestPointPointTriangle(const glm::vec3 &p, const Triangle &tri)
{
    const glm::vec3 ab = tri.v1 - tri.v0;
    const glm::vec3 ac = tri.v2 - tri.v0;
    const glm::vec3 ap = p - tri.v0;
    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f)
        return tri.v0;

    const glm::vec3 bp = p - tri.v1;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3)
        return tri.v1;

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
    {
        const float v = d1 / (d1 - d3);
        return tri.v0 + ab * v;
    }

    const glm::vec3 cp = p - tri.v2;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6)
        return tri.v2;

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
    {
        const float w = d2 / (d2 - d6);
        return tri.v0 + ac * w;
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
    {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return tri.v1 + (tri.v2 - tri.v1) * w;
    }

    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    return tri.v0 + ab * v + ac * w;
}

static float closestPointSegmentSegment(const glm::vec3 &p1,
                                        const glm::vec3 &q1,
                                        const glm::vec3 &p2,
                                        const glm::vec3 &q2,
                                        glm::vec3 &c1,
                                        glm::vec3 &c2)
{
    const glm::vec3 d1 = q1 - p1;
    const glm::vec3 d2 = q2 - p2;
    const glm::vec3 r = p1 - p2;
    const float a = glm::dot(d1, d1);
    const float e = glm::dot(d2, d2);
    const float f = glm::dot(d2, r);

    float s = 0.0f;
    float t = 0.0f;

    if (a <= kZeroLenEps && e <= kZeroLenEps)
    {
        c1 = p1;
        c2 = p2;
        return glm::length2(c1 - c2);
    }

    if (a <= kZeroLenEps)
    {
        t = Clamp(f / e, 0.0f, 1.0f);
    }
    else
    {
        const float c = glm::dot(d1, r);
        if (e <= kZeroLenEps)
        {
            s = Clamp(-c / a, 0.0f, 1.0f);
        }
        else
        {
            const float b = glm::dot(d1, d2);
            const float denom = a * e - b * b;
            s = denom != 0.0f ? Clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;

            if (t < 0.0f)
            {
                t = 0.0f;
                s = Clamp(-c / a, 0.0f, 1.0f);
            }
            else if (t > 1.0f)
            {
                t = 1.0f;
                s = Clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }

    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
    return glm::length2(c1 - c2);
}

static bool segmentIntersectsTriangle(const glm::vec3 &a,
                                      const glm::vec3 &b,
                                      const Triangle &tri,
                                      glm::vec3 &point)
{
    const glm::vec3 ab = b - a;
    const float len = glm::length(ab);
    if (len <= kZeroLenEps)
        return false;

    const float t = tri.intersect_ray(a, ab / len);
    if (t < 0.0f || t > len)
        return false;

    point = a + (ab / len) * t;
    return true;
}

static float closestPointSegmentTriangle(const glm::vec3 &a,
                                         const glm::vec3 &b,
                                         const Triangle &tri,
                                         glm::vec3 &segPoint,
                                         glm::vec3 &triPoint)
{
    glm::vec3 intersection(0.0f);
    if (segmentIntersectsTriangle(a, b, tri, intersection))
    {
        segPoint = intersection;
        triPoint = intersection;
        return 0.0f;
    }

    segPoint = a;
    triPoint = closestPointPointTriangle(a, tri);
    float best = glm::length2(segPoint - triPoint);

    glm::vec3 pointOnSeg = b;
    glm::vec3 pointOnTri = closestPointPointTriangle(b, tri);
    float dist2 = glm::length2(pointOnSeg - pointOnTri);
    if (dist2 < best)
    {
        best = dist2;
        segPoint = pointOnSeg;
        triPoint = pointOnTri;
    }

    const glm::vec3 edgesA[3] = {tri.v0, tri.v1, tri.v2};
    const glm::vec3 edgesB[3] = {tri.v1, tri.v2, tri.v0};
    for (int i = 0; i < 3; ++i)
    {
        glm::vec3 c1(0.0f);
        glm::vec3 c2(0.0f);
        dist2 = closestPointSegmentSegment(a, b, edgesA[i], edgesB[i], c1, c2);
        if (dist2 < best)
        {
            best = dist2;
            segPoint = c1;
            triPoint = c2;
        }
    }

    return best;
}

static bool capsuleVsTriangle(const glm::vec3 &a,
                              const glm::vec3 &b,
                              float radius,
                              const Triangle &tri,
                              int triIndex,
                              SweepHit &hit)
{
    glm::vec3 capsulePoint(0.0f);
    glm::vec3 triPoint(0.0f);
    const float dist2 = closestPointSegmentTriangle(a, b, tri, capsulePoint, triPoint);
    if (dist2 > radius * radius)
        return false;

    glm::vec3 n = capsulePoint - triPoint;
    if (glm::length2(n) <= kZeroLenEps)
        n = tri.normal();
    else
        n = glm::normalize(n);

    hit.t = 1.0f;
    hit.normal = n;
    hit.contact = capsulePoint - n * radius;
    hit.triIndex = triIndex;
    return true;
}

static float capsuleTriangleDistanceAt(const glm::vec3 &a,
                                       const glm::vec3 &b,
                                       const glm::vec3 &delta,
                                       float t,
                                       const Triangle &tri,
                                       glm::vec3 &capsulePoint,
                                       glm::vec3 &triPoint)
{
    return closestPointSegmentTriangle(a + delta * t, b + delta * t, tri, capsulePoint, triPoint);
}

static void fillCapsuleTriangleHit(const glm::vec3 &a,
                                   const glm::vec3 &b,
                                   const glm::vec3 &delta,
                                   float radius,
                                   const Triangle &tri,
                                   int triIndex,
                                   float t,
                                   SweepHit &hit)
{
    glm::vec3 capsulePoint(0.0f);
    glm::vec3 triPoint(0.0f);
    capsuleTriangleDistanceAt(a, b, delta, t, tri, capsulePoint, triPoint);

    glm::vec3 n = capsulePoint - triPoint;
    if (glm::length2(n) <= kZeroLenEps)
    {
        n = tri.normal();
        if (glm::dot(n, delta) > 0.0f)
            n = -n;
    }
    else
    {
        n = glm::normalize(n);
    }

    hit.t = t;
    hit.normal = n;
    hit.contact = capsulePoint - n * radius;
    hit.triIndex = triIndex;
}

static bool sweepCapsuleVsTriangle(const glm::vec3 &a,
                                   const glm::vec3 &b,
                                   const glm::vec3 &delta,
                                   float radius,
                                   const Triangle &tri,
                                   int triIndex,
                                   float maxT,
                                   SweepHit &hit)
{
    const float speed = glm::length(delta);
    if (speed <= kZeroLenEps)
        return capsuleVsTriangle(a, b, radius, tri, triIndex, hit);

    const float radius2 = radius * radius;
    glm::vec3 capsulePoint(0.0f);
    glm::vec3 triPoint(0.0f);

    float t = 0.0f;
    float prevT = 0.0f;
    float dist2 = capsuleTriangleDistanceAt(a, b, delta, t, tri, capsulePoint, triPoint);
    if (dist2 <= radius2)
    {
        fillCapsuleTriangleHit(a, b, delta, radius, tri, triIndex, 0.0f, hit);
        return true;
    }

    bool bracketed = false;
    float lo = 0.0f;
    float hi = maxT;

    // Conservative advancement: the closest distance can shrink no faster than
    // the capsule translation speed, so this cannot step past first contact.
    for (int iter = 0; iter < 48 && t < maxT; ++iter)
    {
        const float dist = glm::sqrt(glm::max(dist2, 0.0f));
        float step = (dist - radius) / speed;
        step = glm::clamp(step * 0.75f, 0.0001f, 0.25f);

        prevT = t;
        t = glm::min(t + step, maxT);
        dist2 = capsuleTriangleDistanceAt(a, b, delta, t, tri, capsulePoint, triPoint);

        if (dist2 <= radius2)
        {
            lo = prevT;
            hi = t;
            bracketed = true;
            break;
        }
    }

    if (!bracketed)
        return false;

    for (int iter = 0; iter < 16; ++iter)
    {
        const float mid = (lo + hi) * 0.5f;
        dist2 = capsuleTriangleDistanceAt(a, b, delta, mid, tri, capsulePoint, triPoint);
        if (dist2 <= radius2)
            hi = mid;
        else
            lo = mid;
    }

    fillCapsuleTriangleHit(a, b, delta, radius, tri, triIndex, hi, hit);
    return hit.t <= maxT;
}

static BoundingBox sweptSphereBounds(const glm::vec3 &from, const glm::vec3 &delta, float radius)
{
    BoundingBox bounds;
    bounds.expand(from);
    bounds.expand(from + delta);
    bounds.min -= glm::vec3(radius);
    bounds.max += glm::vec3(radius);
    return bounds;
}

static BoundingBox sweptCapsuleBounds(const glm::vec3 &a,
                                      const glm::vec3 &b,
                                      const glm::vec3 &delta,
                                      float radius)
{
    BoundingBox bounds;
    bounds.expand(a);
    bounds.expand(b);
    bounds.expand(a + delta);
    bounds.expand(b + delta);
    bounds.min -= glm::vec3(radius);
    bounds.max += glm::vec3(radius);
    return bounds;
}

static bool sweepSphereVsMesh(const glm::vec3 &from,
                              const glm::vec3 &delta,
                              float radius,
                              const std::vector<Triangle> &tris,
                              const std::vector<int> &candidates,
                              SweepHit &hit)
{
    if (glm::length2(delta) < kZeroLenEps)
        return false;

    hit = {};
    hit.t = 1.0f;

    bool anyHit = false;
    for (int triIndex : candidates)
    {
        if (sweepSphereVsTriangle(from, delta, radius, tris[triIndex], triIndex, hit))
            anyHit = true;
    }

    return anyHit;
}

} // namespace

void CollisionSystem::clear()
{
    triangles_.clear();
    markDirty();
}

void CollisionSystem::setTriangles(const std::vector<Triangle> &triangles)
{
    triangles_ = triangles;
    markDirty();
}

void CollisionSystem::addTriangle(const Triangle &triangle)
{
    triangles_.push_back(triangle);
    markDirty();
}

void CollisionSystem::addTriangles(const std::vector<Triangle> &triangles)
{
    triangles_.insert(triangles_.end(), triangles.begin(), triangles.end());
    markDirty();
}

void CollisionSystem::markDirty()
{
    bvhDirty_ = true;
}

void CollisionSystem::buildBvh() const
{
    if (!bvhDirty_)
        return;

    triangleIndices_.resize(triangles_.size());
    std::iota(triangleIndices_.begin(), triangleIndices_.end(), 0);
    bvhNodes_.clear();

    if (!triangles_.empty())
        buildBvhNode(0, (int)triangles_.size());

    bvhDirty_ = false;
}

int CollisionSystem::buildBvhNode(int start, int count) const
{
    if (count <= 0)
        return -1;

    BvhNode node;
    for (int i = start; i < start + count; ++i)
        node.bounds.expand(triangles_[triangleIndices_[i]].getBounds());

    const int nodeIndex = (int)bvhNodes_.size();
    bvhNodes_.push_back(node);

    static constexpr int kLeafTriangleCount = 12;
    if (count <= kLeafTriangleCount)
    {
        bvhNodes_[nodeIndex].start = start;
        bvhNodes_[nodeIndex].count = count;
        return nodeIndex;
    }

    BoundingBox centroidBounds;
    for (int i = start; i < start + count; ++i)
        centroidBounds.expand(triangles_[triangleIndices_[i]].center());

    const glm::vec3 size = centroidBounds.size();
    int axis = 0;
    if (size.y > size.x && size.y >= size.z)
        axis = 1;
    else if (size.z > size.x && size.z >= size.y)
        axis = 2;

    const int mid = start + count / 2;
    std::nth_element(triangleIndices_.begin() + start,
                     triangleIndices_.begin() + mid,
                     triangleIndices_.begin() + start + count,
                     [&](int lhs, int rhs) {
                         return triangles_[lhs].center()[axis] < triangles_[rhs].center()[axis];
                     });

    const int leftCount = mid - start;
    const int rightCount = start + count - mid;
    bvhNodes_[nodeIndex].left = leftCount > 0 ? buildBvhNode(start, leftCount) : -1;
    bvhNodes_[nodeIndex].right = rightCount > 0 ? buildBvhNode(mid, rightCount) : -1;
    return nodeIndex;
}

void CollisionSystem::queryBvh(const BoundingBox &bounds, int nodeIndex, std::vector<int> &out) const
{
    if (nodeIndex < 0 || nodeIndex >= (int)bvhNodes_.size())
        return;

    const BvhNode &node = bvhNodes_[nodeIndex];
    if (!node.bounds.intersects(bounds))
        return;

    if (node.isLeaf())
    {
        for (int i = 0; i < node.count; ++i)
            out.push_back(triangleIndices_[node.start + i]);
        return;
    }

    queryBvh(bounds, node.left, out);
    queryBvh(bounds, node.right, out);
}

bool CollisionSystem::rayCast(const Ray &ray, float maxDist, PickResult &out) const
{
    out = {};
    out.distance = maxDist;

    buildBvh();

    std::vector<int> candidates;
    if (!bvhNodes_.empty())
    {
        BoundingBox rayBounds;
        rayBounds.expand(ray.origin);
        rayBounds.expand(ray.at(maxDist));
        queryBvh(rayBounds, 0, candidates);
    }
    else
    {
        candidates.reserve(triangles_.size());
        for (int i = 0; i < (int)triangles_.size(); ++i)
            candidates.push_back(i);
    }

    for (int i : candidates)
    {
        const float t = triangles_[i].intersect_ray(ray.origin, ray.direction);
        if (t <= 0.0f || t > out.distance)
            continue;

        out.hit = true;
        out.distance = t;
        out.point = ray.at(t);
        out.normal = triangles_[i].normal();
        out.triangleIndex = i;
        out.surfaceIndex = -1;
    }

    return out.hit;
}

bool CollisionSystem::sweepSphere(const glm::vec3 &origin,
                                  const glm::vec3 &delta,
                                  float radius,
                                  PickResult &out) const
{
    buildBvh();

    std::vector<int> candidates;
    if (!bvhNodes_.empty())
        queryBvh(sweptSphereBounds(origin, delta, radius), 0, candidates);
    else
        candidates.clear();

    if (candidates.empty())
    {
        out = {};
        return false;
    }

    SweepHit hit;
    if (!sweepSphereVsMesh(origin, delta, radius, triangles_, candidates, hit))
    {
        out = {};
        return false;
    }

    out.hit = true;
    out.distance = hit.t;
    out.point = hit.contact;
    out.normal = hit.normal;
    out.triangleIndex = hit.triIndex;
    out.surfaceIndex = -1;
    return true;
}

bool CollisionSystem::sweepCapsule(const glm::vec3 &originA,
                                   const glm::vec3 &originB,
                                   const glm::vec3 &delta,
                                   float radius,
                                   PickResult &out) const
{
    out = {};

    const glm::vec3 axis = originB - originA;
    const float axisLen = glm::length(axis);
    if (axisLen <= kZeroLenEps)
        return sweepSphere(originA, delta, radius, out);

    buildBvh();

    std::vector<int> candidates;
    if (!bvhNodes_.empty())
        queryBvh(sweptCapsuleBounds(originA, originB, delta, radius), 0, candidates);

    if (candidates.empty())
        return false;

    bool hasHit = false;
    SweepHit best;
    best.t = 1.0f;

    for (int triIndex : candidates)
    {
        SweepHit hit;
        if (sweepCapsuleVsTriangle(originA, originB, delta, radius, triangles_[triIndex], triIndex, best.t, hit) &&
            (!hasHit || hit.t < best.t))
        {
            hasHit = true;
            best = hit;
        }
    }

    if (!hasHit)
        return false;

    out.hit = true;
    out.distance = best.t;
    out.point = best.contact;
    out.normal = best.normal;
    out.triangleIndex = best.triIndex;
    out.surfaceIndex = -1;
    return true;
}

} // namespace collision_study
