#pragma once
#include "Math.hpp"
#include <vector>
#include <limits>

// ============================================================
//  Quadtree — 2D spatial partition on the XZ plane
//
//  Designed for terrain / navmesh collision where Y is up.
//  Each node covers an AABB slice; children subdivide X and Z.
//
//  Usage:
//      Quadtree qt(worldAABB);
//      qt.build(triangles);     // insert all at once (preferred)
//      qt.query(ray, maxDist, candidates);
// ============================================================

struct QuadtreeNode
{
    BoundingBox               bounds;
    std::vector<Triangle>     triangles;   // triangles fully or partially inside
    QuadtreeNode             *children[4]; // NW, NE, SW, SE (null = leaf)
    int                       depth;
    bool                      isLeaf;

    QuadtreeNode(const BoundingBox &b, int d);
    ~QuadtreeNode();

    void split();

    bool overlapsTriangle(const Triangle &tri) const;
};

class Quadtree
{
public:
    Quadtree(const BoundingBox &bounds, int maxDepth = 8, int maxTriPerNode = 16);
    Quadtree(const glm::vec3 &min, const glm::vec3 &max,
             int maxDepth = 8, int maxTriPerNode = 16);
    ~Quadtree();

    // ── Build ──────────────────────────────────────────────────
    void insert(const Triangle &tri);
    void insert(const std::vector<Triangle> &tris);
    void build(const std::vector<Triangle> &tris);  // clear + insert all
    void clear();

    // ── Query ──────────────────────────────────────────────────
    // AABB overlap
    void query(const BoundingBox &region,
               std::vector<const Triangle *> &out) const;

    // All triangles within radius of a point (XZ only)
    void query(const glm::vec3 &point, float radius,
               std::vector<const Triangle *> &out) const;

    // Ray (returns candidate triangles along the ray)
    void queryRay(const Ray &ray, float maxDist,
                  std::vector<const Triangle *> &out) const;

    // ── Stats ──────────────────────────────────────────────────
    int totalTriangles()  const { return totalTris_; }
    void getStats(int &nodes, int &leaves, int &maxDepthReached) const;

private:
    QuadtreeNode *root_           = nullptr;
    int           maxDepth_       = 8;
    int           maxTriPerNode_  = 16;
    int           totalTris_      = 0;

    void insertRecursive(QuadtreeNode *node, const Triangle &tri);
    void queryAABB(const QuadtreeNode *node, const BoundingBox &region,
                   std::vector<const Triangle *> &out) const;
    void queryRayRec(const QuadtreeNode *node, const Ray &ray, float maxDist,
                     std::vector<const Triangle *> &out) const;
    void statsRec(const QuadtreeNode *node, int &nodes, int &leaves, int &depth) const;
};

// ============================================================
//  Octree — full 3D spatial partition
//
//  Splits each node into 8 octants (X, Y, Z halves).
//  Works for any 3D geometry, characters, projectiles, etc.
//
//  Usage:
//      Octree ot(worldAABB);
//      ot.build(triangles);
//      ot.queryRay(ray, maxDist, candidates);
// ============================================================

struct OctreeNode
{
    BoundingBox               bounds;
    std::vector<Triangle>     triangles;  // triangles whose AABB overlaps this node
    OctreeNode               *children[8]; // null = leaf
    int                       depth;
    bool                      isLeaf;

    OctreeNode(const BoundingBox &b, int d);
    ~OctreeNode();

    void split();

    bool overlapsTriangle(const Triangle &tri) const;
};

class Octree
{
public:
    Octree(const BoundingBox &bounds, int maxDepth = 8, int maxTriPerNode = 16);
    Octree(const glm::vec3 &min, const glm::vec3 &max,
           int maxDepth = 8, int maxTriPerNode = 16);
    ~Octree();

    // ── Build ──────────────────────────────────────────────────
    void insert(const Triangle &tri);
    void insert(const std::vector<Triangle> &tris);
    void build(const std::vector<Triangle> &tris);
    void clear();

    // ── Query ──────────────────────────────────────────────────
    // AABB overlap
    void query(const BoundingBox &region,
               std::vector<const Triangle *> &out) const;

    // Sphere overlap
    void querySphere(const glm::vec3 &center, float radius,
                     std::vector<const Triangle *> &out) const;

    // Ray
    void queryRay(const Ray &ray, float maxDist,
                  std::vector<const Triangle *> &out) const;

    // Frustum (visibility culling / shadow maps)
    void queryFrustum(const Frustum &frustum,
                      std::vector<const Triangle *> &out) const;

    // ── Stats ──────────────────────────────────────────────────
    int  totalTriangles()  const { return totalTris_; }
    void getStats(int &nodes, int &leaves, int &maxDepthReached) const;
    int  memoryKB() const;   // rough estimate in KB

private:
    OctreeNode *root_           = nullptr;
    int         maxDepth_       = 8;
    int         maxTriPerNode_  = 16;
    int         totalTris_      = 0;

    void insertRecursive(OctreeNode *node, const Triangle &tri);
    void queryAABB(const OctreeNode *node, const BoundingBox &region,
                   std::vector<const Triangle *> &out) const;
    void querySphereRec(const OctreeNode *node, const glm::vec3 &center, float radius,
                        std::vector<const Triangle *> &out) const;
    void queryRayRec(const OctreeNode *node, const Ray &ray, float maxDist,
                     std::vector<const Triangle *> &out) const;
    void queryFrustumRec(const OctreeNode *node, const Frustum &frustum,
                         std::vector<const Triangle *> &out) const;
    void statsRec(const OctreeNode *node, int &nodes, int &leaves, int &depth) const;
    int  countNodesRec(const OctreeNode *node) const;
};
