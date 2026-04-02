#pragma once
#include "Math.hpp"
#include <vector>
#include <limits>

// Forward declarations for optional tree acceleration
class Octree;
class Quadtree;

// ============================================================
//  CollisionInfo  — result of a simple cast query
// ============================================================
struct CollisionInfo
{
    bool            hit              = false;
    glm::vec3       point            = {};
    glm::vec3       normal           = {};
    float           distance         = std::numeric_limits<float>::max();
    const Triangle *triangle         = nullptr;
};

// ============================================================
//  CollisionPacket  — state for the Fauerby ellipsoid-sliding algorithm
// ============================================================
struct CollisionPacket
{
    // Ellipsoid radii (use glm::vec3(r,r,r) for a uniform sphere)
    glm::vec3 eRadius = {1, 1, 1};

    // World-space (R3) inputs
    glm::vec3 R3Position;
    glm::vec3 R3Velocity;

    // Ellipsoid-space working values (computed each pass)
    glm::vec3 ePosition;
    glm::vec3 eVelocity;
    glm::vec3 eNormalizedVelocity;

    // Output of one pass
    bool            foundCollision     = false;
    float           nearestDistance    = std::numeric_limits<float>::max();
    glm::vec3       intersectionPoint  = {};
    const Triangle *intersectionTri    = nullptr;

    // Tuning
    float slidingSpeed      = 0.005f; // very-close distance (metres)
    int   maxRecursionDepth = 5;
};

// ============================================================
//  CollisionSystem  — manages triangle soup + slide collision
//
//  Optional acceleration:
//      system.setOctree(octree)       // uses Octree for candidate queries
//      system.setQuadtree(quadtree)   // uses Quadtree for candidate queries
//  (only one is active at a time; set the other to nullptr)
// ============================================================
class CollisionSystem
{
public:
    CollisionSystem();

    // ── Triangle management ───────────────────────────────────
    void addTriangle(const Triangle &tri);
    void addTriangles(const std::vector<Triangle> &tris);
    void removeTriangle(int index);
    void clear();

    int                           triangleCount() const;
    const Triangle               &getTriangle(int i) const;
    const std::vector<Triangle>  &getTriangles() const;

    // ── Spacial-acceleration attachment (non-owning) ──────────
    // If set, the collision system queries the tree instead of
    // iterating the full triangle list.
    void setOctree  (Octree   *tree) { octree_   = tree; quadtree_ = nullptr; }
    void setQuadtree(Quadtree *tree) { quadtree_ = tree; octree_   = nullptr; }

    // ── Sliding collision (Fauerby) ───────────────────────────
    // collideAndSlide: two-pass (movement + gravity), returns final world position.
    // radius: ellipsoid radii (e.g. {0.4f, 0.9f, 0.4f} for a character)
    // gravity: world-space gravity per frame (e.g. {0, -9.8f * dt, 0})
    // outGrounded: set true when the gravity pass cannot move down
    glm::vec3 collideAndSlide(const glm::vec3 &position,
                               const glm::vec3 &velocity,
                               const glm::vec3 &radius,
                               const glm::vec3 &gravity,
                               bool            &outGrounded);

    // Overloads
    glm::vec3 collideAndSlide(const glm::vec3 &position,
                               const glm::vec3 &velocity,
                               const glm::vec3 &radius);

    glm::vec3 sphereSlide(const glm::vec3 &position,
                           const glm::vec3 &velocity,
                           float            radius,
                           const glm::vec3 &gravity,
                           bool            &outGrounded);

    glm::vec3 sphereSlide(const glm::vec3 &position,
                           const glm::vec3 &velocity,
                           float            radius);

    // ── Simple casts ─────────────────────────────────────────
    bool rayCast     (const Ray &ray, float maxDist, CollisionInfo &out) const;
    bool sphereCast  (const glm::vec3 &origin, const glm::vec3 &dir,
                      float radius, float maxDist, CollisionInfo &out) const;
    bool pointInside (const glm::vec3 &point) const;

private:
    std::vector<Triangle> triangles_;
    Octree               *octree_    = nullptr;
    Quadtree             *quadtree_  = nullptr;

    // Collect candidate triangles for a position + radius query (eSpace)
    void getCandidates(const glm::vec3 &ePos, const glm::vec3 &eRadius,
                       std::vector<const Triangle *> &out) const;

    // Fauerby per-triangle test (in ellipsoid space)
    bool checkTriangle(CollisionPacket &packet, const Triangle &tri);

    // Solve quadratic: returns true and sets root to the smallest positive root < maxR
    static bool lowestRoot(float a, float b, float c, float maxR, float &root);

    // Recursive slide step
    glm::vec3 collideWithWorld(int depth, CollisionPacket &packet,
                                const glm::vec3 &pos, const glm::vec3 &vel);
};
