#include "CollisionSystem.hpp"
#include "CollisionWorld.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace cs = collision_study;

namespace
{
int failures = 0;

void check(bool condition, const std::string &name)
{
    if (condition)
    {
        std::cout << "[PASS] " << name << "\n";
        return;
    }

    std::cout << "[FAIL] " << name << "\n";
    ++failures;
}

cs::CollisionSystem makeWallMesh()
{
    cs::CollisionSystem mesh;
    mesh.addTriangle({glm::vec3(0.0f, -2.0f, -2.0f),
                      glm::vec3(0.0f, 2.0f, -2.0f),
                      glm::vec3(0.0f, 2.0f, 2.0f)});
    mesh.addTriangle({glm::vec3(0.0f, -2.0f, -2.0f),
                      glm::vec3(0.0f, 2.0f, 2.0f),
                      glm::vec3(0.0f, -2.0f, 2.0f)});
    return mesh;
}

BoundingBox makeBox(const glm::vec3 &minPoint, const glm::vec3 &maxPoint)
{
    BoundingBox box;
    box.expand(minPoint);
    box.expand(maxPoint);
    return box;
}

cs::CollisionTarget makePolygonTarget(const cs::CollisionSystem &mesh)
{
    cs::CollisionTarget target;
    target.dstType = 1;
    target.method = cs::COLLISION_METHOD_POLYGON;
    target.response = cs::COLLISION_RESPONSE_SLIDE;
    target.mesh = &mesh;
    return target;
}
} // namespace

int main()
{
    cs::CollisionSystem wall = makeWallMesh();

    PickResult hit;
    check(wall.sweepSphere(glm::vec3(-2.0f, 0.0f, 0.0f),
                           glm::vec3(4.0f, 0.0f, 0.0f),
                           0.5f,
                           hit),
          "CollisionSystem sphere sweep hits polygon wall");
    check(hit.hit && hit.distance >= 0.0f && hit.distance <= 1.0f,
          "Sphere sweep returns normalized hit distance");

    PickResult capsuleHit;
    check(wall.sweepCapsule(glm::vec3(-2.0f, -0.5f, 0.0f),
                            glm::vec3(-2.0f, 0.5f, 0.0f),
                            glm::vec3(4.0f, 0.0f, 0.0f),
                            0.5f,
                            capsuleHit),
          "CollisionSystem capsule sweep hits polygon wall");

    cs::CollisionWorld world;
    world.addTarget(makePolygonTarget(wall));

    cs::CollisionMoveResult movedSphere = world.moveSphere(glm::vec3(-2.0f, 0.0f, 0.0f),
                                                           glm::vec3(2.0f, 0.0f, 0.0f),
                                                           0.5f,
                                                           1);
    check(movedSphere.hitCount > 0, "CollisionWorld moveSphere hits polygon target");

    cs::CollisionMoveResult movedCapsuleMesh = world.moveCapsule(glm::vec3(-2.0f, 0.0f, 0.0f),
                                                                 glm::vec3(2.0f, 0.0f, 0.0f),
                                                                 0.5f,
                                                                 2.0f,
                                                                 1);
    check(movedCapsuleMesh.hitCount > 0, "CollisionWorld moveCapsule hits polygon target");

    cs::CollisionWorld primitiveWorld;

    cs::CollisionTarget sphereTarget;
    sphereTarget.dstType = 1;
    sphereTarget.method = cs::COLLISION_METHOD_SPHERE;
    sphereTarget.response = cs::COLLISION_RESPONSE_SLIDE;
    sphereTarget.sphereCenter = glm::vec3(0.0f);
    sphereTarget.sphereRadius = 0.5f;
    primitiveWorld.addTarget(sphereTarget);

    cs::CollisionTarget boxTarget;
    boxTarget.dstType = 1;
    boxTarget.method = cs::COLLISION_METHOD_BOX;
    boxTarget.response = cs::COLLISION_RESPONSE_SLIDE;
    boxTarget.box = makeBox(glm::vec3(-0.5f), glm::vec3(0.5f));
    primitiveWorld.addTarget(boxTarget);

    cs::CollisionTarget capsuleTarget;
    capsuleTarget.dstType = 1;
    capsuleTarget.method = cs::COLLISION_METHOD_CAPSULE;
    capsuleTarget.response = cs::COLLISION_RESPONSE_SLIDE;
    capsuleTarget.capsuleA = glm::vec3(0.0f, -0.5f, 1.5f);
    capsuleTarget.capsuleB = glm::vec3(0.0f, 0.5f, 1.5f);
    capsuleTarget.capsuleRadius = 0.5f;
    primitiveWorld.addTarget(capsuleTarget);

    cs::CollisionMoveResult movedCapsuleSphere = primitiveWorld.moveCapsule(glm::vec3(-2.0f, 0.0f, 0.0f),
                                                                            glm::vec3(2.0f, 0.0f, 0.0f),
                                                                            0.5f,
                                                                            2.0f,
                                                                            1);
    check(movedCapsuleSphere.hitCount > 0, "CollisionWorld moveCapsule hits sphere/box primitive");

    cs::CollisionWorld capsuleOnlyWorld;
    capsuleOnlyWorld.addTarget(capsuleTarget);
    cs::CollisionMoveResult movedCapsuleCapsule = capsuleOnlyWorld.moveCapsule(glm::vec3(-2.0f, 0.0f, 1.5f),
                                                                               glm::vec3(2.0f, 0.0f, 1.5f),
                                                                               0.5f,
                                                                               2.0f,
                                                                               1);
    check(movedCapsuleCapsule.hitCount > 0, "CollisionWorld moveCapsule hits capsule primitive");

    if (failures != 0)
    {
        std::cout << failures << " collision test(s) failed\n";
        return 1;
    }

    std::cout << "All collision tests passed\n";
    return 0;
}
