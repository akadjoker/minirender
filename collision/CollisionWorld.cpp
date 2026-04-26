#include "CollisionWorld.hpp"

#include <algorithm>
#include <limits>
#include <vector>
#include <glm/gtx/norm.hpp>

namespace collision_study
{

namespace
{
bool solveQuadratic(float a, float b, float c, float maxT, float &t)
{
    if (glm::abs(a) <= 1e-12f)
        return false;

    const float d = b * b - 4.0f * a * c;
    if (d < 0.0f)
        return false;

    const float sd = glm::sqrt(d);
    float t0 = (-b - sd) / (2.0f * a);
    float t1 = (-b + sd) / (2.0f * a);
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

glm::vec3 closestPointPointAABB(const glm::vec3 &p, const BoundingBox &box)
{
    return glm::vec3(Clamp(p.x, box.min.x, box.max.x),
                     Clamp(p.y, box.min.y, box.max.y),
                     Clamp(p.z, box.min.z, box.max.z));
}

glm::vec3 normalFromAABBPoint(const glm::vec3 &p, const BoundingBox &box)
{
    const glm::vec3 q = closestPointPointAABB(p, box);
    glm::vec3 n = p - q;
    if (glm::length2(n) > 1e-12f)
        return glm::normalize(n);

    const float dMinX = glm::abs(p.x - box.min.x);
    const float dMaxX = glm::abs(box.max.x - p.x);
    const float dMinY = glm::abs(p.y - box.min.y);
    const float dMaxY = glm::abs(box.max.y - p.y);
    const float dMinZ = glm::abs(p.z - box.min.z);
    const float dMaxZ = glm::abs(box.max.z - p.z);

    float best = dMinX;
    n = glm::vec3(-1.0f, 0.0f, 0.0f);
    if (dMaxX < best) { best = dMaxX; n = glm::vec3(1.0f, 0.0f, 0.0f); }
    if (dMinY < best) { best = dMinY; n = glm::vec3(0.0f, -1.0f, 0.0f); }
    if (dMaxY < best) { best = dMaxY; n = glm::vec3(0.0f, 1.0f, 0.0f); }
    if (dMinZ < best) { best = dMinZ; n = glm::vec3(0.0f, 0.0f, -1.0f); }
    if (dMaxZ < best) { n = glm::vec3(0.0f, 0.0f, 1.0f); }
    return n;
}

float closestPointSegmentPoint(const glm::vec3 &a,
                               const glm::vec3 &b,
                               const glm::vec3 &p,
                               float &t,
                               glm::vec3 &c)
{
    const glm::vec3 ab = b - a;
    const float abLen2 = glm::dot(ab, ab);
    if (abLen2 <= 1e-12f)
    {
        t = 0.0f;
        c = a;
        return glm::length2(p - c);
    }

    t = Clamp(glm::dot(p - a, ab) / abLen2, 0.0f, 1.0f);
    c = a + ab * t;
    return glm::length2(p - c);
}

float sqDistPointAABB(const glm::vec3 &p, const BoundingBox &box)
{
    const glm::vec3 q = closestPointPointAABB(p, box);
    return glm::length2(p - q);
}

float closestPointSegmentAABB(const glm::vec3 &a,
                              const glm::vec3 &b,
                              const BoundingBox &box,
                              float &segmentT,
                              glm::vec3 &segmentPoint,
                              glm::vec3 &boxPoint)
{
    const glm::vec3 d = b - a;
    std::vector<float> breaks;
    breaks.reserve(8);
    breaks.push_back(0.0f);
    breaks.push_back(1.0f);

    for (int axis = 0; axis < 3; ++axis)
    {
        const float da = d[axis];
        if (glm::abs(da) <= 1e-12f)
            continue;

        const float t0 = (box.min[axis] - a[axis]) / da;
        const float t1 = (box.max[axis] - a[axis]) / da;
        if (t0 > 0.0f && t0 < 1.0f) breaks.push_back(t0);
        if (t1 > 0.0f && t1 < 1.0f) breaks.push_back(t1);
    }

    std::sort(breaks.begin(), breaks.end());
    breaks.erase(std::unique(breaks.begin(), breaks.end(), [](float lhs, float rhs) {
        return glm::abs(lhs - rhs) <= 1e-6f;
    }), breaks.end());

    float bestDist2 = std::numeric_limits<float>::max();
    float bestT = 0.0f;

    auto evaluate = [&](float t) {
        const glm::vec3 p = a + d * t;
        const float dist2 = sqDistPointAABB(p, box);
        if (dist2 < bestDist2)
        {
            bestDist2 = dist2;
            bestT = t;
        }
    };

    for (float t : breaks)
        evaluate(t);

    for (size_t i = 0; i + 1 < breaks.size(); ++i)
    {
        const float lo = breaks[i];
        const float hi = breaks[i + 1];
        if (hi - lo <= 1e-6f)
            continue;

        const float mid = (lo + hi) * 0.5f;
        float numerator = 0.0f;
        float denominator = 0.0f;

        for (int axis = 0; axis < 3; ++axis)
        {
            const float p = a[axis] + d[axis] * mid;
            float bound = 0.0f;
            bool active = false;
            if (p < box.min[axis])
            {
                bound = box.min[axis];
                active = true;
            }
            else if (p > box.max[axis])
            {
                bound = box.max[axis];
                active = true;
            }

            if (active)
            {
                numerator += d[axis] * (bound - a[axis]);
                denominator += d[axis] * d[axis];
            }
        }

        if (denominator > 1e-12f)
            evaluate(Clamp(numerator / denominator, lo, hi));
    }

    segmentT = bestT;
    segmentPoint = a + d * bestT;
    boxPoint = closestPointPointAABB(segmentPoint, box);
    return bestDist2;
}

float closestPointSegmentSegment(const glm::vec3 &p1,
                                 const glm::vec3 &q1,
                                 const glm::vec3 &p2,
                                 const glm::vec3 &q2,
                                 float &s,
                                 float &t,
                                 glm::vec3 &c1,
                                 glm::vec3 &c2)
{
    const glm::vec3 d1 = q1 - p1;
    const glm::vec3 d2 = q2 - p2;
    const glm::vec3 r = p1 - p2;
    const float a = glm::dot(d1, d1);
    const float e = glm::dot(d2, d2);
    const float f = glm::dot(d2, r);

    if (a <= 1e-12f && e <= 1e-12f)
    {
        s = 0.0f;
        t = 0.0f;
        c1 = p1;
        c2 = p2;
        return glm::length2(c1 - c2);
    }

    if (a <= 1e-12f)
    {
        s = 0.0f;
        t = Clamp(f / e, 0.0f, 1.0f);
    }
    else
    {
        const float c = glm::dot(d1, r);
        if (e <= 1e-12f)
        {
            t = 0.0f;
            s = Clamp(-c / a, 0.0f, 1.0f);
        }
        else
        {
            const float b = glm::dot(d1, d2);
            const float denom = a * e - b * b;

            if (denom != 0.0f)
                s = Clamp((b * f - c * e) / denom, 0.0f, 1.0f);
            else
                s = 0.0f;

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

template <typename DistanceFn, typename FillHitFn>
bool conservativeSweepCapsule(float radius,
                              float speed,
                              DistanceFn distanceFn,
                              FillHitFn fillHit,
                              PickResult &out)
{
    const float radius2 = radius * radius;
    float t = 0.0f;
    float prevT = 0.0f;
    float dist2 = distanceFn(t);

    if (dist2 <= radius2)
    {
        fillHit(0.0f, out);
        return true;
    }

    bool bracketed = false;
    float lo = 0.0f;
    float hi = 1.0f;

    for (int iter = 0; iter < 48 && t < 1.0f; ++iter)
    {
        const float dist = glm::sqrt(glm::max(dist2, 0.0f));
        float step = speed > 1e-12f ? (dist - radius) / speed : 1.0f;
        step = glm::clamp(step * 0.75f, 0.0001f, 0.25f);

        prevT = t;
        t = glm::min(t + step, 1.0f);
        dist2 = distanceFn(t);

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
        if (distanceFn(mid) <= radius2)
            hi = mid;
        else
            lo = mid;
    }

    fillHit(hi, out);
    return true;
}

bool sweepCapsuleSphere(const glm::vec3 &capA,
                        const glm::vec3 &capB,
                        const glm::vec3 &delta,
                        float capRadius,
                        const glm::vec3 &sphereCenter,
                        float sphereRadius,
                        PickResult &out)
{
    const float combinedRadius = capRadius + sphereRadius;
    const float speed = glm::length(delta);

    auto distanceFn = [&](float t) {
        float segT = 0.0f;
        glm::vec3 closest(0.0f);
        return closestPointSegmentPoint(capA + delta * t, capB + delta * t, sphereCenter, segT, closest);
    };

    auto fillHit = [&](float t, PickResult &hit) {
        float segT = 0.0f;
        glm::vec3 closest(0.0f);
        closestPointSegmentPoint(capA + delta * t, capB + delta * t, sphereCenter, segT, closest);

        glm::vec3 n = closest - sphereCenter;
        if (glm::length2(n) <= 1e-12f)
            n = glm::vec3(1.0f, 0.0f, 0.0f);
        else
            n = glm::normalize(n);

        hit.hit = true;
        hit.distance = t;
        hit.normal = n;
        hit.point = closest - n * capRadius;
        hit.triangleIndex = -1;
        hit.surfaceIndex = -1;
    };

    return conservativeSweepCapsule(combinedRadius, speed, distanceFn, fillHit, out);
}

bool sweepCapsuleCapsule(const glm::vec3 &capA,
                         const glm::vec3 &capB,
                         const glm::vec3 &delta,
                         float radiusA,
                         const glm::vec3 &targetA,
                         const glm::vec3 &targetB,
                         float radiusB,
                         PickResult &out)
{
    const float combinedRadius = radiusA + radiusB;
    const float speed = glm::length(delta);

    auto distanceFn = [&](float t) {
        float s = 0.0f;
        float u = 0.0f;
        glm::vec3 ca(0.0f);
        glm::vec3 cb(0.0f);
        return closestPointSegmentSegment(capA + delta * t, capB + delta * t, targetA, targetB, s, u, ca, cb);
    };

    auto fillHit = [&](float t, PickResult &hit) {
        float s = 0.0f;
        float u = 0.0f;
        glm::vec3 ca(0.0f);
        glm::vec3 cb(0.0f);
        closestPointSegmentSegment(capA + delta * t, capB + delta * t, targetA, targetB, s, u, ca, cb);

        glm::vec3 n = ca - cb;
        if (glm::length2(n) <= 1e-12f)
            n = glm::vec3(1.0f, 0.0f, 0.0f);
        else
            n = glm::normalize(n);

        hit.hit = true;
        hit.distance = t;
        hit.normal = n;
        hit.point = ca - n * radiusA;
        hit.triangleIndex = -1;
        hit.surfaceIndex = -1;
    };

    return conservativeSweepCapsule(combinedRadius, speed, distanceFn, fillHit, out);
}

bool sweepCapsuleAABB(const glm::vec3 &capA,
                      const glm::vec3 &capB,
                      const glm::vec3 &delta,
                      float radius,
                      const BoundingBox &box,
                      PickResult &out)
{
    if (!box.is_valid())
        return false;

    const float speed = glm::length(delta);

    auto distanceFn = [&](float t) {
        float segT = 0.0f;
        glm::vec3 segPoint(0.0f);
        glm::vec3 boxPoint(0.0f);
        return closestPointSegmentAABB(capA + delta * t, capB + delta * t, box, segT, segPoint, boxPoint);
    };

    auto fillHit = [&](float t, PickResult &hit) {
        float segT = 0.0f;
        glm::vec3 segPoint(0.0f);
        glm::vec3 boxPoint(0.0f);
        closestPointSegmentAABB(capA + delta * t, capB + delta * t, box, segT, segPoint, boxPoint);

        glm::vec3 n = segPoint - boxPoint;
        if (glm::length2(n) <= 1e-12f)
            n = normalFromAABBPoint(segPoint, box);
        else
            n = glm::normalize(n);

        hit.hit = true;
        hit.distance = t;
        hit.normal = n;
        hit.point = segPoint - n * radius;
        hit.triangleIndex = -1;
        hit.surfaceIndex = -1;
    };

    return conservativeSweepCapsule(radius, speed, distanceFn, fillHit, out);
}

bool sweepSphereSphere(const glm::vec3 &sv,
                       const glm::vec3 &dv,
                       float srcRadius,
                       const glm::vec3 &dstCenter,
                       float dstRadius,
                       PickResult &out)
{
    const glm::vec3 delta = dv - sv;
    const float r = srcRadius + dstRadius;

    const glm::vec3 o = sv - dstCenter;
    const float a = glm::dot(delta, delta);
    if (a <= 1e-12f)
        return false;

    const float b = 2.0f * glm::dot(o, delta);
    const float c = glm::dot(o, o) - r * r;
    const float d = b * b - 4.0f * a * c;
    if (d < 0.0f)
        return false;

    const float sd = glm::sqrt(d);
    const float t1 = (-b - sd) / (2.0f * a);
    const float t2 = (-b + sd) / (2.0f * a);

    float t = t1;
    if (t < 0.0f || t > 1.0f)
        t = t2;
    if (t < 0.0f || t > 1.0f)
        return false;

    const glm::vec3 centerAtHit = sv + delta * t;
    glm::vec3 n = centerAtHit - dstCenter;
    if (glm::length2(n) <= 1e-12f)
        n = glm::vec3(0.0f, 1.0f, 0.0f);
    else
        n = glm::normalize(n);

    out.hit = true;
    out.distance = t;
    out.normal = n;
    out.point = centerAtHit - n * srcRadius;
    out.triangleIndex = -1;
    out.surfaceIndex = -1;
    return true;
}

bool sweepSphereAABB(const glm::vec3 &sv,
                     const glm::vec3 &dv,
                     float radius,
                     const BoundingBox &box,
                     PickResult &out)
{
    if (!box.is_valid())
        return false;

    BoundingBox expanded = box;
    expanded.min -= glm::vec3(radius);
    expanded.max += glm::vec3(radius);

    const glm::vec3 delta = dv - sv;
    const float len = glm::length(delta);
    if (len <= 1e-12f)
        return false;

    const glm::vec3 dir = delta / len;
    const float tDist = expanded.intersects_ray(sv, dir);
    if (tDist < 0.0f)
        return false;

    const float t = tDist / len;
    if (t < 0.0f || t > 1.0f)
        return false;

    const glm::vec3 hitPos = sv + delta * t;
    const glm::vec3 c = expanded.center();
    glm::vec3 local = hitPos - c;

    glm::vec3 n(0.0f);
    const glm::vec3 ext = expanded.extents();
    const float ax = glm::abs(local.x / Max(ext.x, 1e-6f));
    const float ay = glm::abs(local.y / Max(ext.y, 1e-6f));
    const float az = glm::abs(local.z / Max(ext.z, 1e-6f));
    if (ax >= ay && ax >= az) n.x = local.x >= 0.0f ? 1.0f : -1.0f;
    else if (ay >= ax && ay >= az) n.y = local.y >= 0.0f ? 1.0f : -1.0f;
    else n.z = local.z >= 0.0f ? 1.0f : -1.0f;

    out.hit = true;
    out.distance = t;
    out.normal = n;
    out.point = hitPos - n * radius;
    out.triangleIndex = -1;
    out.surfaceIndex = -1;
    return true;
}

bool sweepSphereCapsule(const glm::vec3 &sv,
                        const glm::vec3 &dv,
                        float srcRadius,
                        const glm::vec3 &capA,
                        const glm::vec3 &capB,
                        float capRadius,
                        PickResult &out)
{
    const glm::vec3 delta = dv - sv;
    if (glm::length2(delta) <= 1e-12f)
        return false;

    const glm::vec3 seg = capB - capA;
    const float segLen2 = glm::length2(seg);
    const float radius = srcRadius + capRadius;

    if (segLen2 <= 1e-12f)
        return sweepSphereSphere(sv, dv, srcRadius, capA, capRadius, out);

    const glm::vec3 axis = seg * glm::inversesqrt(segLen2);
    const glm::vec3 rel = sv - capA;
    const float relProj = glm::dot(rel, axis);
    const float deltaProj = glm::dot(delta, axis);
    const glm::vec3 relPerp = rel - axis * relProj;
    const glm::vec3 deltaPerp = delta - axis * deltaProj;

    float bestT = 1.0f;
    glm::vec3 bestNormal(0.0f, 1.0f, 0.0f);
    bool hit = false;

    float tCyl = bestT;
    if (solveQuadratic(glm::dot(deltaPerp, deltaPerp),
                       2.0f * glm::dot(relPerp, deltaPerp),
                       glm::dot(relPerp, relPerp) - radius * radius,
                       bestT,
                       tCyl))
    {
        const float proj = relProj + deltaProj * tCyl;
        const float segLen = glm::sqrt(segLen2);
        if (proj >= 0.0f && proj <= segLen)
        {
            bestT = tCyl;
            const glm::vec3 center = sv + delta * bestT;
            const glm::vec3 closest = capA + axis * proj;
            const glm::vec3 n = center - closest;
            bestNormal = glm::length2(n) > 1e-12f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
            hit = true;
        }
    }

    PickResult capHit;
    if (sweepSphereSphere(sv, dv, srcRadius, capA, capRadius, capHit) && capHit.distance <= bestT)
    {
        bestT = capHit.distance;
        bestNormal = capHit.normal;
        hit = true;
    }
    if (sweepSphereSphere(sv, dv, srcRadius, capB, capRadius, capHit) && capHit.distance <= bestT)
    {
        bestT = capHit.distance;
        bestNormal = capHit.normal;
        hit = true;
    }

    if (!hit)
        return false;

    const glm::vec3 centerAtHit = sv + delta * bestT;
    out.hit = true;
    out.distance = bestT;
    out.normal = bestNormal;
    out.point = centerAtHit - bestNormal * srcRadius;
    out.triangleIndex = -1;
    out.surfaceIndex = -1;
    return true;
}
} // namespace

void CollisionWorld::clearTargets()
{
    targets_.clear();
}

void CollisionWorld::addTarget(const CollisionTarget &target)
{
    targets_.push_back(target);
}

bool CollisionWorld::hitTest(const glm::vec3 &sv,
                             const glm::vec3 &dv,
                             float radius,
                             const CollisionTarget &target,
                             PickResult &hit) const
{
    switch (target.method)
    {
    case COLLISION_METHOD_SPHERE:
        return sweepSphereSphere(sv, dv, radius, target.sphereCenter, target.sphereRadius, hit);
    case COLLISION_METHOD_BOX:
        return sweepSphereAABB(sv, dv, radius, target.box, hit);
    case COLLISION_METHOD_CAPSULE:
        return sweepSphereCapsule(sv, dv, radius, target.capsuleA, target.capsuleB, target.capsuleRadius, hit);
    case COLLISION_METHOD_POLYGON:
        if (target.mesh)
            return target.mesh->sweepSphere(sv, dv - sv, radius, hit);
        return false;
    default:
        return false;
    }
}

bool CollisionWorld::hitTestCapsule(const glm::vec3 &prevCenter,
                                    const glm::vec3 &desiredCenter,
                                    float radius,
                                    float height,
                                    const CollisionTarget &target,
                                    PickResult &hit) const
{
    const float halfSegment = glm::max(height * 0.5f - radius, 0.0f);
    const glm::vec3 delta = desiredCenter - prevCenter;

    if (target.method == COLLISION_METHOD_POLYGON)
    {
        if (!target.mesh)
            return false;

        const glm::vec3 a = prevCenter + glm::vec3(0.0f, -halfSegment, 0.0f);
        const glm::vec3 b = prevCenter + glm::vec3(0.0f, halfSegment, 0.0f);
        return target.mesh->sweepCapsule(a, b, delta, radius, hit);
    }

    const glm::vec3 prevA = prevCenter + glm::vec3(0.0f, -halfSegment, 0.0f);
    const glm::vec3 prevB = prevCenter + glm::vec3(0.0f, halfSegment, 0.0f);

    if (target.method == COLLISION_METHOD_SPHERE)
        return sweepCapsuleSphere(prevA, prevB, delta, radius, target.sphereCenter, target.sphereRadius, hit);
    if (target.method == COLLISION_METHOD_BOX)
        return sweepCapsuleAABB(prevA, prevB, delta, radius, target.box, hit);
    if (target.method == COLLISION_METHOD_CAPSULE)
        return sweepCapsuleCapsule(prevA, prevB, delta, radius, target.capsuleA, target.capsuleB, target.capsuleRadius, hit);

    return false;
}

CollisionMoveResult CollisionWorld::moveSphere(const glm::vec3 &prevPos,
                                               const glm::vec3 &desiredPos,
                                               float radius,
                                               int dstType,
                                               int maxHits) const
{
    CollisionMoveResult out;
    const float epsilon = config_.epsilon;
    const float zeroEps = config_.zeroEpsilon;
    const int maxCollisionHits = maxHits > 0 ? maxHits : config_.defaultMaxHits;

    glm::vec3 sv = prevPos;
    glm::vec3 dv = desiredPos;
    glm::vec3 panic = sv;

    float td = glm::length(dv - sv);
    float td_xz = glm::length(glm::vec2(dv.x - sv.x, dv.z - sv.z));

    int n_hit = 0;
    Plane planes[2];

    for (;;)
    {
        if (out.hitCount >= maxCollisionHits)
            break;

        bool hasHit = false;
        PickResult best;
        best.distance = 1.0f;
        const CollisionTarget *bestTarget = nullptr;

        for (size_t i = 0; i < targets_.size(); ++i)
        {
            const CollisionTarget &t = targets_[i];
            if (t.dstType != dstType)
                continue;

            PickResult h;
            if (!hitTest(sv, dv, radius, t, h))
                continue;

            if (!hasHit || h.distance < best.distance)
            {
                hasHit = true;
                best = h;
                bestTarget = &t;
            }
        }

        if (!hasHit || !bestTarget)
            break;

        out.hitCount++;
        out.lastHit = best;

        const glm::vec3 delta = dv - sv;
        const float t = Clamp(best.distance, 0.0f, 1.0f);
        const glm::vec3 hitPos = sv + delta * t;

        Plane collPlane(best.normal, hitPos);
        collPlane.d -= epsilon;

        if (bestTarget->response == COLLISION_RESPONSE_STOP || bestTarget->response == COLLISION_RESPONSE_NONE)
        {
            dv = sv;
            break;
        }

        glm::vec3 nv = dv - best.normal * glm::dot(dv - hitPos, best.normal);

        if (n_hit == 0)
        {
            dv = nv;
        }
        else if (n_hit == 1)
        {
            if (planes[0].distance(nv) >= 0.0f)
            {
                dv = nv;
                n_hit = 0;
            }
            else
            {
                const float ndot = glm::dot(planes[0].normal, collPlane.normal);
                if (glm::abs(ndot) < 1.0f - zeroEps)
                {
                    const glm::vec3 dir = glm::cross(planes[0].normal, collPlane.normal);
                    const float dirLen2 = glm::length2(dir);
                    if (dirLen2 > 1e-12f)
                    {
                        // nearest point on crease line to dv (simple projection)
                        const glm::vec3 dnorm = glm::normalize(dir);
                        const float tline = glm::dot(dv - hitPos, dnorm);
                        dv = hitPos + dnorm * tline;
                    }
                    else
                    {
                        dv = sv;
                        break;
                    }
                }
                else
                {
                    dv = sv;
                    break;
                }
            }
        }
        else
        {
            if (planes[0].distance(nv) >= 0.0f && planes[1].distance(nv) >= 0.0f)
            {
                dv = nv;
                n_hit = 0;
            }
            else
            {
                dv = sv;
                break;
            }
        }

        glm::vec3 dd = dv - sv;
        const glm::vec3 originalDir = delta;

        if (glm::dot(dd, originalDir) <= 0.0f)
        {
            dv = sv;
            break;
        }

        if (bestTarget->response == COLLISION_RESPONSE_SLIDE)
        {
            const float d = glm::length(dd);
            if (d <= zeroEps)
            {
                dv = sv;
                break;
            }
            if (d > td)
                dd *= td / d;
        }
        else if (bestTarget->response == COLLISION_RESPONSE_SLIDEXZ)
        {
            const float d = glm::length(glm::vec2(dd.x, dd.z));
            if (d <= zeroEps)
            {
                dv = sv;
                break;
            }
            if (d > td_xz)
                dd *= td_xz / d;
        }

        sv += best.normal * epsilon;
        dv = sv + dd;

        if (n_hit < 2)
            planes[n_hit++] = collPlane;

        td = glm::length(dv - sv);
        td_xz = glm::length(glm::vec2(dv.x - sv.x, dv.z - sv.z));
    }

    out.finalPosition = out.hitCount >= maxCollisionHits ? panic : dv;
    return out;
}

CollisionMoveResult CollisionWorld::moveCapsule(const glm::vec3 &prevCenter,
                                                const glm::vec3 &desiredCenter,
                                                float radius,
                                                float height,
                                                int dstType,
                                                int maxHits) const
{
    CollisionMoveResult out;
    const float epsilon = config_.epsilon;
    const float zeroEps = config_.zeroEpsilon;
    const int maxCollisionHits = maxHits > 0 ? maxHits : config_.defaultMaxHits;

    glm::vec3 sv = prevCenter;
    glm::vec3 dv = desiredCenter;
    glm::vec3 panic = sv;

    float td = glm::length(dv - sv);
    float td_xz = glm::length(glm::vec2(dv.x - sv.x, dv.z - sv.z));

    int n_hit = 0;
    Plane planes[2];

    for (;;)
    {
        if (out.hitCount >= maxCollisionHits)
            break;

        bool hasHit = false;
        PickResult best;
        best.distance = 1.0f;
        const CollisionTarget *bestTarget = nullptr;

        for (size_t i = 0; i < targets_.size(); ++i)
        {
            const CollisionTarget &t = targets_[i];
            if (t.dstType != dstType)
                continue;

            PickResult h;
            if (!hitTestCapsule(sv, dv, radius, height, t, h))
                continue;

            if (!hasHit || h.distance < best.distance)
            {
                hasHit = true;
                best = h;
                bestTarget = &t;
            }
        }

        if (!hasHit || !bestTarget)
            break;

        out.hitCount++;
        out.lastHit = best;

        const glm::vec3 delta = dv - sv;
        const float t = Clamp(best.distance, 0.0f, 1.0f);
        const glm::vec3 hitPos = sv + delta * t;

        Plane collPlane(best.normal, hitPos);
        collPlane.d -= epsilon;

        if (bestTarget->response == COLLISION_RESPONSE_STOP || bestTarget->response == COLLISION_RESPONSE_NONE)
        {
            dv = sv;
            break;
        }

        glm::vec3 nv = dv - best.normal * glm::dot(dv - hitPos, best.normal);

        if (n_hit == 0)
        {
            dv = nv;
        }
        else if (n_hit == 1)
        {
            if (planes[0].distance(nv) >= 0.0f)
            {
                dv = nv;
                n_hit = 0;
            }
            else
            {
                const float ndot = glm::dot(planes[0].normal, collPlane.normal);
                if (glm::abs(ndot) < 1.0f - zeroEps)
                {
                    const glm::vec3 dir = glm::cross(planes[0].normal, collPlane.normal);
                    const float dirLen2 = glm::length2(dir);
                    if (dirLen2 > 1e-12f)
                    {
                        const glm::vec3 dnorm = glm::normalize(dir);
                        const float tline = glm::dot(dv - hitPos, dnorm);
                        dv = hitPos + dnorm * tline;
                    }
                    else
                    {
                        dv = sv;
                        break;
                    }
                }
                else
                {
                    dv = sv;
                    break;
                }
            }
        }
        else
        {
            if (planes[0].distance(nv) >= 0.0f && planes[1].distance(nv) >= 0.0f)
            {
                dv = nv;
                n_hit = 0;
            }
            else
            {
                dv = sv;
                break;
            }
        }

        glm::vec3 dd = dv - sv;
        const glm::vec3 originalDir = delta;

        if (glm::dot(dd, originalDir) <= 0.0f)
        {
            dv = sv;
            break;
        }

        if (bestTarget->response == COLLISION_RESPONSE_SLIDE)
        {
            const float d = glm::length(dd);
            if (d <= zeroEps)
            {
                dv = sv;
                break;
            }
            if (d > td)
                dd *= td / d;
        }
        else if (bestTarget->response == COLLISION_RESPONSE_SLIDEXZ)
        {
            const float d = glm::length(glm::vec2(dd.x, dd.z));
            if (d <= zeroEps)
            {
                dv = sv;
                break;
            }
            if (d > td_xz)
                dd *= td_xz / d;
        }

        sv += best.normal * epsilon;
        dv = sv + dd;

        if (n_hit < 2)
            planes[n_hit++] = collPlane;

        td = glm::length(dv - sv);
        td_xz = glm::length(glm::vec2(dv.x - sv.x, dv.z - sv.z));
    }

    out.finalPosition = out.hitCount >= maxCollisionHits ? panic : dv;
    return out;
}

CollisionMoveResult CollisionWorld::moveCameraSphere(const glm::vec3 &prevPos,
                                                     const glm::vec3 &desiredPos,
                                                     float radius,
                                                     int dstType,
                                                     int maxHits) const
{
    return moveSphere(prevPos, desiredPos, radius, dstType, maxHits);
}

CollisionMoveResult CollisionWorld::moveCameraCapsule(const glm::vec3 &prevCenter,
                                                      const glm::vec3 &desiredCenter,
                                                      float radius,
                                                      float height,
                                                      int dstType,
                                                      int maxHits) const
{
    return moveCapsule(prevCenter, desiredCenter, radius, height, dstType, maxHits);
}

} // namespace collision_study
