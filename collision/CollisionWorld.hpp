#pragma once

#include "CollisionSystem.hpp"

#include <vector>

namespace collision_study
{

enum CollisionMethod
{
    COLLISION_METHOD_SPHERE = 1,
    COLLISION_METHOD_POLYGON = 2,
    COLLISION_METHOD_BOX = 3,
    COLLISION_METHOD_CAPSULE = 4
};

enum CollisionResponse
{
    COLLISION_RESPONSE_NONE = 0,
    COLLISION_RESPONSE_STOP = 1,
    COLLISION_RESPONSE_SLIDE = 2,
    COLLISION_RESPONSE_SLIDEXZ = 3
};

struct CollisionTarget
{
    int dstType = 1;
    int method = COLLISION_METHOD_POLYGON;
    int response = COLLISION_RESPONSE_SLIDE;

    // Sphere target
    glm::vec3 sphereCenter = glm::vec3(0.0f);
    float sphereRadius = 1.0f;

    // Box target
    BoundingBox box;

    // Capsule target
    glm::vec3 capsuleA = glm::vec3(0.0f, -0.5f, 0.0f);
    glm::vec3 capsuleB = glm::vec3(0.0f, 0.5f, 0.0f);
    float capsuleRadius = 0.5f;

    // Polygon target
    const CollisionSystem *mesh = nullptr;
};

struct CollisionMoveResult
{
    glm::vec3 finalPosition = glm::vec3(0.0f);
    int hitCount = 0;
    PickResult lastHit;
};

struct CollisionWorldConfig
{
    float epsilon = 0.001f;
    float zeroEpsilon = Epsilon;
    int defaultMaxHits = 10;
};

class CollisionWorld
{
public:
    CollisionWorldConfig &config() { return config_; }
    const CollisionWorldConfig &config() const { return config_; }

    void clearTargets();
    void addTarget(const CollisionTarget &target);

    CollisionMoveResult moveSphere(const glm::vec3 &prevPos,
                                   const glm::vec3 &desiredPos,
                                   float radius,
                                   int dstType,
                                   int maxHits = 10) const;

    CollisionMoveResult moveCapsule(const glm::vec3 &prevCenter,
                                    const glm::vec3 &desiredCenter,
                                    float radius,
                                    float height,
                                    int dstType,
                                    int maxHits = 10) const;

    CollisionMoveResult moveCameraSphere(const glm::vec3 &prevPos,
                                         const glm::vec3 &desiredPos,
                                         float radius,
                                         int dstType,
                                         int maxHits = 10) const;

    CollisionMoveResult moveCameraCapsule(const glm::vec3 &prevCenter,
                                          const glm::vec3 &desiredCenter,
                                          float radius,
                                          float height,
                                          int dstType,
                                          int maxHits = 10) const;

private:
    bool hitTest(const glm::vec3 &sv,
                 const glm::vec3 &dv,
                 float radius,
                 const CollisionTarget &target,
                 PickResult &hit) const;

    bool hitTestCapsule(const glm::vec3 &prevCenter,
                        const glm::vec3 &desiredCenter,
                        float radius,
                        float height,
                        const CollisionTarget &target,
                        PickResult &hit) const;

    std::vector<CollisionTarget> targets_;
    CollisionWorldConfig config_;
};

} // namespace collision_study
