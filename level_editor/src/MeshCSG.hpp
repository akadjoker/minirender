#pragma once

#include "EditableMesh.hpp"

struct MeshPlane
{
    glm::vec3 normal {0.0f, 1.0f, 0.0f};
    float distance = 0.0f;

    static MeshPlane FromPointNormal(const glm::vec3& point, const glm::vec3& normal);
    float signedDistanceTo(const glm::vec3& point) const;
};

EditableMesh clipEditableMeshAgainstPlane(const EditableMesh& mesh,
                                          const MeshPlane& plane,
                                          bool keepFront,
                                          bool addCap = true,
                                          float epsilon = 1e-4f);
