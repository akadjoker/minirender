#include "Tree.hpp"
#include <algorithm>
#include <cmath>
#include <cassert>

// ============================================================
//  Helper: AABB ↔ AABB overlap in 2D (XZ)
// ============================================================
static bool aabbXZOverlap(const BoundingBox &a, const BoundingBox &b)
{
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

// ============================================================
//  QuadtreeNode
// ============================================================
QuadtreeNode::QuadtreeNode(const BoundingBox &b, int d)
    : bounds(b), depth(d), isLeaf(true)
{
    for (int i = 0; i < 4; ++i) children[i] = nullptr;
}

QuadtreeNode::~QuadtreeNode()
{
    for (int i = 0; i < 4; ++i)
        delete children[i];
}

void QuadtreeNode::split()
{
    if (!isLeaf) return;
    isLeaf = false;

    glm::vec3 c = bounds.center();
    float yMin = bounds.min.y;
    float yMax = bounds.max.y;

    // NW, NE, SW, SE — split on X and Z
    children[0] = new QuadtreeNode({{bounds.min.x, yMin, bounds.min.z}, {c.x, yMax, c.z}}, depth + 1);
    children[1] = new QuadtreeNode({{c.x,          yMin, bounds.min.z}, {bounds.max.x, yMax, c.z}}, depth + 1);
    children[2] = new QuadtreeNode({{bounds.min.x, yMin, c.z},          {c.x, yMax, bounds.max.z}}, depth + 1);
    children[3] = new QuadtreeNode({{c.x,          yMin, c.z},          {bounds.max.x, yMax, bounds.max.z}}, depth + 1);
}

bool QuadtreeNode::overlapsTriangle(const Triangle &tri) const
{
    BoundingBox tb = tri.getBounds();
    return bounds.intersects(tb);
}

// ============================================================
//  Quadtree
// ============================================================
Quadtree::Quadtree(const BoundingBox &b, int maxD, int maxTri)
    : maxDepth_(maxD), maxTriPerNode_(maxTri)
{
    root_ = new QuadtreeNode(b, 0);
}

Quadtree::Quadtree(const glm::vec3 &mn, const glm::vec3 &mx, int maxD, int maxTri)
    : maxDepth_(maxD), maxTriPerNode_(maxTri)
{
    root_ = new QuadtreeNode({mn, mx}, 0);
}

Quadtree::~Quadtree() { delete root_; }

void Quadtree::clear()
{
    BoundingBox b = root_->bounds;
    delete root_;
    root_  = new QuadtreeNode(b, 0);
    totalTris_ = 0;
}

void Quadtree::insert(const Triangle &tri)
{
    insertRecursive(root_, tri);
    ++totalTris_;
}

void Quadtree::insert(const std::vector<Triangle> &tris)
{
    for (const auto &t : tris) insert(t);
}

void Quadtree::build(const std::vector<Triangle> &tris)
{
    clear();
    for (const auto &t : tris) { insertRecursive(root_, t); ++totalTris_; }
}

void Quadtree::insertRecursive(QuadtreeNode *node, const Triangle &tri)
{
    if (!node->overlapsTriangle(tri)) return;

    if (node->isLeaf)
    {
        node->triangles.push_back(tri);

        // Split if overfull and not at max depth
        if ((int)node->triangles.size() > maxTriPerNode_ && node->depth < maxDepth_)
        {
            node->split();
            // Push existing triangles into children
            std::vector<Triangle> existing = std::move(node->triangles);
            for (const auto &t : existing)
                for (int i = 0; i < 4; ++i)
                    if (node->children[i]->overlapsTriangle(t))
                        node->children[i]->triangles.push_back(t);
        }
    }
    else
    {
        for (int i = 0; i < 4; ++i)
            insertRecursive(node->children[i], tri);
    }
}

void Quadtree::query(const BoundingBox &region,
                     std::vector<const Triangle *> &out) const
{
    queryAABB(root_, region, out);
}

void Quadtree::queryAABB(const QuadtreeNode *node, const BoundingBox &region,
                          std::vector<const Triangle *> &out) const
{
    if (!node) return;
    if (!node->bounds.intersects(region)) return;

    for (const auto &t : node->triangles)
    {
        if (region.intersects(t.getBounds()))
            out.push_back(&t);
    }

    if (!node->isLeaf)
        for (int i = 0; i < 4; ++i) queryAABB(node->children[i], region, out);
}

void Quadtree::query(const glm::vec3 &point, float radius,
                     std::vector<const Triangle *> &out) const
{
    BoundingBox region;
    region.min = point - glm::vec3(radius);
    region.max = point + glm::vec3(radius);
    query(region, out);
}

void Quadtree::queryRay(const Ray &ray, float maxDist,
                         std::vector<const Triangle *> &out) const
{
    queryRayRec(root_, ray, maxDist, out);
}

void Quadtree::queryRayRec(const QuadtreeNode *node, const Ray &ray, float maxDist,
                             std::vector<const Triangle *> &out) const
{
    if (!node) return;
    float t = node->bounds.intersects_ray(ray.origin, ray.direction);
    if (t < 0.f || t > maxDist) return;

    for (const auto &tri : node->triangles)
        out.push_back(&tri);

    if (!node->isLeaf)
        for (int i = 0; i < 4; ++i) queryRayRec(node->children[i], ray, maxDist, out);
}

void Quadtree::getStats(int &nodes, int &leaves, int &maxDepthReached) const
{
    nodes = leaves = maxDepthReached = 0;
    statsRec(root_, nodes, leaves, maxDepthReached);
}

void Quadtree::statsRec(const QuadtreeNode *node, int &nodes, int &leaves, int &depth) const
{
    if (!node) return;
    ++nodes;
    if (node->depth > depth) depth = node->depth;
    if (node->isLeaf) { ++leaves; return; }
    for (int i = 0; i < 4; ++i) statsRec(node->children[i], nodes, leaves, depth);
}

// ============================================================
//  OctreeNode
// ============================================================
OctreeNode::OctreeNode(const BoundingBox &b, int d)
    : bounds(b), depth(d), isLeaf(true)
{
    for (int i = 0; i < 8; ++i) children[i] = nullptr;
}

OctreeNode::~OctreeNode()
{
    for (int i = 0; i < 8; ++i) delete children[i];
}

void OctreeNode::split()
{
    if (!isLeaf) return;
    isLeaf = false;

    glm::vec3 mn = bounds.min;
    glm::vec3 mx = bounds.max;
    glm::vec3 c  = bounds.center();

    // 8 children ordered as bitmask (X bit=0, Y bit=1, Z bit=2)
    children[0] = new OctreeNode({{mn.x, mn.y, mn.z}, {c.x, c.y, c.z}}, depth + 1);
    children[1] = new OctreeNode({{c.x,  mn.y, mn.z}, {mx.x, c.y, c.z}}, depth + 1);
    children[2] = new OctreeNode({{mn.x, c.y,  mn.z}, {c.x, mx.y, c.z}}, depth + 1);
    children[3] = new OctreeNode({{c.x,  c.y,  mn.z}, {mx.x, mx.y, c.z}}, depth + 1);
    children[4] = new OctreeNode({{mn.x, mn.y, c.z},  {c.x, c.y, mx.z}}, depth + 1);
    children[5] = new OctreeNode({{c.x,  mn.y, c.z},  {mx.x, c.y, mx.z}}, depth + 1);
    children[6] = new OctreeNode({{mn.x, c.y,  c.z},  {c.x, mx.y, mx.z}}, depth + 1);
    children[7] = new OctreeNode({{c.x,  c.y,  c.z},  {mx.x, mx.y, mx.z}}, depth + 1);
}

bool OctreeNode::overlapsTriangle(const Triangle &tri) const
{
    return bounds.intersects(tri.getBounds());
}

// ============================================================
//  Octree
// ============================================================
Octree::Octree(const BoundingBox &b, int maxD, int maxTri)
    : maxDepth_(maxD), maxTriPerNode_(maxTri)
{
    root_ = new OctreeNode(b, 0);
}

Octree::Octree(const glm::vec3 &mn, const glm::vec3 &mx, int maxD, int maxTri)
    : maxDepth_(maxD), maxTriPerNode_(maxTri)
{
    root_ = new OctreeNode({mn, mx}, 0);
}

Octree::~Octree() { delete root_; }

void Octree::clear()
{
    BoundingBox b = root_->bounds;
    delete root_;
    root_  = new OctreeNode(b, 0);
    totalTris_ = 0;
}

void Octree::insert(const Triangle &tri)
{
    insertRecursive(root_, tri);
    ++totalTris_;
}

void Octree::insert(const std::vector<Triangle> &tris)
{
    for (const auto &t : tris) insert(t);
}

void Octree::build(const std::vector<Triangle> &tris)
{
    clear();
    for (const auto &t : tris) { insertRecursive(root_, t); ++totalTris_; }
}

void Octree::insertRecursive(OctreeNode *node, const Triangle &tri)
{
    if (!node->overlapsTriangle(tri)) return;

    if (node->isLeaf)
    {
        node->triangles.push_back(tri);

        if ((int)node->triangles.size() > maxTriPerNode_ && node->depth < maxDepth_)
        {
            node->split();
            std::vector<Triangle> existing = std::move(node->triangles);
            for (const auto &t : existing)
                for (int i = 0; i < 8; ++i)
                    if (node->children[i]->overlapsTriangle(t))
                        node->children[i]->triangles.push_back(t);
        }
    }
    else
    {
        for (int i = 0; i < 8; ++i)
            insertRecursive(node->children[i], tri);
    }
}

void Octree::query(const BoundingBox &region,
                   std::vector<const Triangle *> &out) const
{
    queryAABB(root_, region, out);
}

void Octree::queryAABB(const OctreeNode *node, const BoundingBox &region,
                        std::vector<const Triangle *> &out) const
{
    if (!node) return;
    if (!node->bounds.intersects(region)) return;

    for (const auto &t : node->triangles)
        if (region.intersects(t.getBounds())) out.push_back(&t);

    if (!node->isLeaf)
        for (int i = 0; i < 8; ++i) queryAABB(node->children[i], region, out);
}

void Octree::querySphere(const glm::vec3 &center, float radius,
                          std::vector<const Triangle *> &out) const
{
    querySphereRec(root_, center, radius, out);
}

void Octree::querySphereRec(const OctreeNode *node, const glm::vec3 &center, float radius,
                              std::vector<const Triangle *> &out) const
{
    if (!node) return;
    Sphere s{center, radius};
    if (!s.intersects(node->bounds)) return;

    for (const auto &t : node->triangles)
    {
        BoundingBox tb = t.getBounds();
        if (s.intersects(tb)) out.push_back(&t);
    }

    if (!node->isLeaf)
        for (int i = 0; i < 8; ++i) querySphereRec(node->children[i], center, radius, out);
}

void Octree::queryRay(const Ray &ray, float maxDist,
                       std::vector<const Triangle *> &out) const
{
    queryRayRec(root_, ray, maxDist, out);
}

void Octree::queryRayRec(const OctreeNode *node, const Ray &ray, float maxDist,
                          std::vector<const Triangle *> &out) const
{
    if (!node) return;
    float t = node->bounds.intersects_ray(ray.origin, ray.direction);
    if (t < 0.f || t > maxDist) return;

    for (const auto &tri : node->triangles)
        out.push_back(&tri);

    if (!node->isLeaf)
        for (int i = 0; i < 8; ++i) queryRayRec(node->children[i], ray, maxDist, out);
}

void Octree::queryFrustum(const Frustum &frustum,
                           std::vector<const Triangle *> &out) const
{
    queryFrustumRec(root_, frustum, out);
}

void Octree::queryFrustumRec(const OctreeNode *node, const Frustum &frustum,
                               std::vector<const Triangle *> &out) const
{
    if (!node) return;
    if (!frustum.contains(node->bounds)) return;

    for (const auto &tri : node->triangles)
        out.push_back(&tri);

    if (!node->isLeaf)
        for (int i = 0; i < 8; ++i) queryFrustumRec(node->children[i], frustum, out);
}

void Octree::getStats(int &nodes, int &leaves, int &maxDepthReached) const
{
    nodes = leaves = maxDepthReached = 0;
    statsRec(root_, nodes, leaves, maxDepthReached);
}

void Octree::statsRec(const OctreeNode *node, int &nodes, int &leaves, int &depth) const
{
    if (!node) return;
    ++nodes;
    if (node->depth > depth) depth = node->depth;
    if (node->isLeaf) { ++leaves; return; }
    for (int i = 0; i < 8; ++i) statsRec(node->children[i], nodes, leaves, depth);
}

int Octree::countNodesRec(const OctreeNode *node) const
{
    if (!node) return 0;
    int n = 1;
    if (!node->isLeaf)
        for (int i = 0; i < 8; ++i) n += countNodesRec(node->children[i]);
    return n;
}

int Octree::memoryKB() const
{
    int n = countNodesRec(root_);
    // rough: each node ≈ sizeof(OctreeNode) + triangles stored (potentially duplicated)
    // use totalTris_ * sizeof(Triangle) as stored data estimate
    size_t bytes = (size_t)n * sizeof(OctreeNode)
                 + (size_t)totalTris_ * sizeof(Triangle) * 2; // ~2x duplication on average
    return (int)(bytes / 1024);
}
