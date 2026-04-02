#include "Collision.hpp"
#include "Tree.hpp"
#include <cmath>
#include <algorithm>

// ============================================================
//  CollisionSystem
// ============================================================
CollisionSystem::CollisionSystem() {}

void CollisionSystem::addTriangle(const Triangle &tri)      { triangles_.push_back(tri); }
void CollisionSystem::removeTriangle(int i)
{
    if (i >= 0 && i < (int)triangles_.size())
        triangles_.erase(triangles_.begin() + i);
}
void CollisionSystem::addTriangles(const std::vector<Triangle> &t)
    { triangles_.insert(triangles_.end(), t.begin(), t.end()); }
void CollisionSystem::clear()                               { triangles_.clear(); }
int  CollisionSystem::triangleCount()   const { return (int)triangles_.size(); }
const Triangle &CollisionSystem::getTriangle(int i) const  { return triangles_[i]; }
const std::vector<Triangle> &CollisionSystem::getTriangles() const { return triangles_; }

// ============================================================
//  Candidate collection — world space
// ============================================================
void CollisionSystem::getCandidates(const glm::vec3 &ePos,
                                     const glm::vec3 &eRadius,
                                     std::vector<const Triangle *> &out) const
{
    // Convert eSpace position back to world space for the tree query
    glm::vec3 wPos = ePos * eRadius;

    if (octree_)
    {
        // Query a conservative sphere centred on world position
        float r = glm::max(glm::max(eRadius.x, eRadius.y), eRadius.z) * 2.f;
        octree_->querySphere(wPos, r, out);
        return;
    }
    if (quadtree_)
    {
        float r = glm::max(glm::max(eRadius.x, eRadius.y), eRadius.z) * 2.f;
        BoundingBox box;
        box.min = wPos - glm::vec3(r);
        box.max = wPos + glm::vec3(r);
        quadtree_->query(box, out);
        return;
    }
    // Fallback: iterate all
    for (const auto &t : triangles_)
        out.push_back(&t);
}

// ============================================================
//  lowestRoot — quadratic solver
// ============================================================
bool CollisionSystem::lowestRoot(float a, float b, float c, float maxR, float &root)
{
    float disc = b * b - 4.f * a * c;
    if (disc < 0.f) return false;

    float sqrtD = std::sqrt(disc);
    float r1 = (-b - sqrtD) / (2.f * a);
    float r2 = (-b + sqrtD) / (2.f * a);
    if (r1 > r2) std::swap(r1, r2);

    if (r1 > 0.f && r1 < maxR) { root = r1; return true; }
    if (r2 > 0.f && r2 < maxR) { root = r2; return true; }
    return false;
}

// ============================================================
//  checkTriangle — Fauerby swept-sphere test in ellipsoid space
//
//  The triangle vertices are first scaled into eSpace
//  (dividing each component by eRadius) before running the test.
// ============================================================
bool CollisionSystem::checkTriangle(CollisionPacket &packet, const Triangle &worldTri)
{
    // Scale triangle vertices to ellipsoid space
    Triangle tri;
    tri.v0 = worldTri.v0 / packet.eRadius;
    tri.v1 = worldTri.v1 / packet.eRadius;
    tri.v2 = worldTri.v2 / packet.eRadius;

    const glm::vec3 &p1 = tri.v0;
    const glm::vec3 &p2 = tri.v1;
    const glm::vec3 &p3 = tri.v2;

    // Plane of the triangle in eSpace
    Plane trianglePlane = tri.getPlane();
    const glm::vec3 &planeNormal = trianglePlane.normal;

    float normalDotVel = glm::dot(planeNormal, packet.eVelocity);
    // Skip back-face and perfectly perpendicular
    if (std::fabs(normalDotVel) < 1e-6f) return false;

    float signedDist = trianglePlane.distance(packet.ePosition);
    float normalDotNormVel = glm::dot(planeNormal, packet.eNormalizedVelocity);

    float t0, t1;
    bool embedded = false;

    if (std::fabs(normalDotNormVel) < 1e-6f)
    {
        if (std::fabs(signedDist) >= 1.f) return false;
        embedded = true;
        t0 = 0.f; t1 = 1.f;
    }
    else
    {
        float nvi = 1.f / normalDotNormVel;
        t0 = (-1.f - signedDist) * nvi;
        t1 = ( 1.f - signedDist) * nvi;
        if (t0 > t1) std::swap(t0, t1);
        if (t0 > 1.f || t1 < 0.f) return false;
        t0 = glm::clamp(t0, 0.f, 1.f);
        t1 = glm::clamp(t1, 0.f, 1.f);
    }

    glm::vec3 collisionPoint = {};
    bool      found          = false;
    float     t              = 1.f;

    // ── Interior check ─────────────────────────────────────────────
    if (!embedded)
    {
        glm::vec3 planePoint = packet.ePosition - planeNormal + packet.eVelocity * t0;
        if (tri.contains(planePoint))
        {
            found          = true;
            t              = t0;
            collisionPoint = planePoint;
        }
    }

    // ── Vertices and edges ─────────────────────────────────────────
    if (!found)
    {
        const glm::vec3 &vel  = packet.eVelocity;
        const glm::vec3 &base = packet.ePosition;
        float velLen2 = glm::length2(vel);
        float a, b, c, newT;

        // Vertex P1
        a = velLen2;
        b = 2.f * glm::dot(vel, base - p1);
        c = glm::length2(p1 - base) - 1.f;
        if (lowestRoot(a, b, c, t, newT)) { t = newT; found = true; collisionPoint = p1; }

        // Vertex P2
        b = 2.f * glm::dot(vel, base - p2);
        c = glm::length2(p2 - base) - 1.f;
        if (lowestRoot(a, b, c, t, newT)) { t = newT; found = true; collisionPoint = p2; }

        // Vertex P3
        b = 2.f * glm::dot(vel, base - p3);
        c = glm::length2(p3 - base) - 1.f;
        if (lowestRoot(a, b, c, t, newT)) { t = newT; found = true; collisionPoint = p3; }

        // Edge helper lambda
        auto testEdge = [&](const glm::vec3 &ea, const glm::vec3 &eb)
        {
            glm::vec3 edge = eb - ea;
            glm::vec3 btv  = ea - base;
            float eLen2   = glm::length2(edge);
            float eDotVel = glm::dot(edge, vel);
            float eDotBtv = glm::dot(edge, btv);

            a = eLen2 * -velLen2 + eDotVel * eDotVel;
            b = eLen2 * (2.f * glm::dot(vel, btv)) - 2.f * eDotVel * eDotBtv;
            c = eLen2 * (1.f - glm::length2(btv)) + eDotBtv * eDotBtv;

            if (lowestRoot(a, b, c, t, newT))
            {
                float f = (eDotVel * newT - eDotBtv) / eLen2;
                if (f >= 0.f && f <= 1.f)
                {
                    t              = newT;
                    found          = true;
                    collisionPoint = ea + edge * f;
                }
            }
        };

        testEdge(p1, p2);
        testEdge(p2, p3);
        testEdge(p3, p1);
    }

    if (found && t >= 0.f && t <= packet.nearestDistance)
    {
        packet.nearestDistance    = t;
        packet.intersectionPoint  = collisionPoint;
        packet.foundCollision     = true;
        packet.intersectionTri    = &worldTri; // keep world-space pointer
        return true;
    }
    return false;
}

// ============================================================
//  collideWithWorld — recursive Fauerby sliding
// ============================================================
glm::vec3 CollisionSystem::collideWithWorld(int depth, CollisionPacket &packet,
                                             const glm::vec3 &pos, const glm::vec3 &vel)
{
    if (depth > packet.maxRecursionDepth) return pos;

    packet.eVelocity          = vel;
    packet.eNormalizedVelocity = glm::length2(vel) > 1e-10f
                                    ? glm::normalize(vel)
                                    : vel;
    packet.ePosition          = pos;
    packet.foundCollision     = false;
    packet.nearestDistance    = std::numeric_limits<float>::max();

    // Gather candidate triangles (world space)
    std::vector<const Triangle *> candidates;
    getCandidates(pos, packet.eRadius, candidates);

    for (const auto *tri : candidates)
        checkTriangle(packet, *tri);

    if (!packet.foundCollision)
        return pos + vel;

    // ── Settle very-close-distance threshold ──────────────────
    float vcd = packet.slidingSpeed;
    glm::vec3 newPos = pos;
    if (packet.nearestDistance >= vcd)
    {
        glm::vec3 vn = glm::length2(vel) > 1e-10f ? glm::normalize(vel) : vel;
        newPos = pos + vn * (packet.nearestDistance - vcd);
        packet.intersectionPoint -= vn * vcd;
    }

    // ── Slide plane ───────────────────────────────────────────
    glm::vec3 slideNormal = glm::length2(newPos - packet.intersectionPoint) > 1e-10f
                                ? glm::normalize(newPos - packet.intersectionPoint)
                                : packet.eNormalizedVelocity; // degenerate fallback

    glm::vec3 dest    = pos + vel;
    float     distP   = glm::dot(dest - packet.intersectionPoint, slideNormal);
    glm::vec3 newDest = dest - slideNormal * distP;
    glm::vec3 newVel  = newDest - packet.intersectionPoint;

    if (glm::length(newVel) < vcd) return newPos;

    return collideWithWorld(depth + 1, packet, newPos, newVel);
}

// ============================================================
//  Public API — collideAndSlide
// ============================================================
glm::vec3 CollisionSystem::collideAndSlide(const glm::vec3 &position,
                                             const glm::vec3 &velocity,
                                             const glm::vec3 &radius,
                                             const glm::vec3 &gravity,
                                             bool            &outGrounded)
{
    CollisionPacket packet;
    packet.eRadius     = radius;
    packet.R3Position  = position;
    packet.R3Velocity  = velocity;

    glm::vec3 ePos = position / radius;
    glm::vec3 eVel = velocity / radius;

    // Pass 1: movement
    glm::vec3 finalEPos = collideWithWorld(0, packet, ePos, eVel);

    // Pass 2: gravity
    outGrounded = false;
    if (glm::length2(gravity) > 1e-10f)
    {
        packet.R3Position = finalEPos * radius;
        packet.R3Velocity = gravity;
        glm::vec3 eGrav   = gravity / radius;
        glm::vec3 gravPos = collideWithWorld(0, packet, finalEPos, eGrav);

        float gravDist = glm::length(gravPos - finalEPos) * glm::length(radius);
        outGrounded    = gravDist < packet.slidingSpeed * 2.f;
        finalEPos      = gravPos;
    }

    return finalEPos * radius; // back to world space
}

glm::vec3 CollisionSystem::collideAndSlide(const glm::vec3 &position,
                                             const glm::vec3 &velocity,
                                             const glm::vec3 &radius)
{
    bool g; return collideAndSlide(position, velocity, radius, {0,0,0}, g);
}

glm::vec3 CollisionSystem::sphereSlide(const glm::vec3 &position,
                                         const glm::vec3 &velocity,
                                         float radius,
                                         const glm::vec3 &gravity,
                                         bool &outGrounded)
{
    return collideAndSlide(position, velocity, {radius, radius, radius}, gravity, outGrounded);
}

glm::vec3 CollisionSystem::sphereSlide(const glm::vec3 &position,
                                         const glm::vec3 &velocity,
                                         float radius)
{
    bool g; return sphereSlide(position, velocity, radius, {0,0,0}, g);
}

// ============================================================
//  Simple casts
// ============================================================
bool CollisionSystem::rayCast(const Ray &ray, float maxDist, CollisionInfo &out) const
{
    out = {};
    out.distance = maxDist;

    for (const auto &tri : triangles_)
    {
        float t = tri.intersect_ray(ray.origin, ray.direction);
        if (t > 0.f && t < out.distance)
        {
            out.hit      = true;
            out.distance = t;
            out.point    = ray.at(t);
            out.normal   = tri.normal();
            out.triangle = &tri;
        }
    }
    return out.hit;
}

bool CollisionSystem::sphereCast(const glm::vec3 &origin, const glm::vec3 &dir,
                                   float radius, float maxDist, CollisionInfo &out) const
{
    // Fallback: treat as ray cast (TODO: proper swept-sphere)
    return rayCast({origin, dir}, maxDist, out);
}

bool CollisionSystem::pointInside(const glm::vec3 &point) const
{
    // Jordan curve theorem via ray casting (arbitrary direction)
    Ray ray{point, glm::normalize(glm::vec3(1.f, 0.707f, 0.707f))};
    int hits = 0;
    for (const auto &tri : triangles_)
    {
        float t = tri.intersect_ray(ray.origin, ray.direction);
        if (t > 0.f) ++hits;
    }
    return (hits & 1) == 1;
}
