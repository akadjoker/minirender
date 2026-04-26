#pragma once

#include "Math.hpp"

#include <vector>

namespace collision_study
{

class CollisionSystem
{
public:
    void clear();
    void setTriangles(const std::vector<Triangle> &triangles);
    void addTriangle(const Triangle &triangle);
    void addTriangles(const std::vector<Triangle> &triangles);

    int triangleCount() const { return (int)triangles_.size(); }
    const std::vector<Triangle> &triangles() const { return triangles_; }

    bool rayCast(const Ray &ray, float maxDist, PickResult &out) const;

    // Swept sphere against this triangle set (legacy-style).
    // Returns hit with distance in [0..1] along (origin + delta * t).
    bool sweepSphere(const glm::vec3 &origin,
                     const glm::vec3 &delta,
                     float radius,
                     PickResult &out) const;

    // Swept capsule against this triangle set.
    // originA/originB are the capsule segment endpoints before movement.
    bool sweepCapsule(const glm::vec3 &originA,
                      const glm::vec3 &originB,
                      const glm::vec3 &delta,
                      float radius,
                      PickResult &out) const;

private:
    struct BvhNode
    {
        BoundingBox bounds;
        int left = -1;
        int right = -1;
        int start = 0;
        int count = 0;

        bool isLeaf() const { return left < 0 && right < 0; }
    };

    void markDirty();
    void buildBvh() const;
    int buildBvhNode(int start, int count) const;
    void queryBvh(const BoundingBox &bounds, int nodeIndex, std::vector<int> &out) const;

    std::vector<Triangle> triangles_;
    mutable std::vector<int> triangleIndices_;
    mutable std::vector<BvhNode> bvhNodes_;
    mutable bool bvhDirty_ = true;
};

} // namespace collision_study
