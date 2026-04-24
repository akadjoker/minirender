#pragma once
#include "Mesh.hpp"

// ============================================================
//  CSG — Constructive Solid Geometry for triangle meshes
//
//  Operates directly on MeshBuffer / Mesh.
//  All operations return a new Mesh with normals, tangents,
//  surfaces, materials and AABB already computed and uploaded.
//
//  Requirements:
//     Both meshes should be watertight (closed, no holes).
//     Faces wound counter-clockwise (standard OpenGL front-face).
//
//  Pipeline (BSP-tree, based on csg.js / Evan Wallace):
//    1. Convert meshes to world-space convex polygon soup.
//    2. Build BSP trees for both meshes.
//    3. Clip trees against each other (removes inside/outside).
//    4. Merge surviving polygons.
//    5. Fan-triangulate, rebuild Surface/material table,
//       compute normals/tangents, upload.
// ============================================================

namespace CSG
{
    enum class Operation { Union, Difference, Intersection };

    struct Options
    {
        // Vertices closer than this are merged.
        float weldEpsilon       = 1e-5f;
        // Intersection segments shorter than this are discarded.
        float intersectEpsilon  = 1e-6f;
        // Origin of each classification ray is shifted by this along
        // the ray direction before casting, to avoid sitting on a face.
        float rayEpsilon        = 1e-4f;
        // Number of independent rays for inside/outside voting (max 7).
        int   insideTestRays    = 3;
        // Recompute smooth normals on the result (false = flat/face normals).
        bool  smoothNormals     = true;
    };

    // Returns a heap-allocated Mesh. Caller owns the pointer.
    Mesh* makeUnion       (const Mesh& A, const Mesh& B,
                           const glm::mat4& matA = glm::mat4(1.f),
                           const glm::mat4& matB = glm::mat4(1.f),
                           const Options& opts   = {});

    Mesh* makeDifference  (const Mesh& A, const Mesh& B,
                           const glm::mat4& matA = glm::mat4(1.f),
                           const glm::mat4& matB = glm::mat4(1.f),
                           const Options& opts   = {});

    Mesh* makeIntersection(const Mesh& A, const Mesh& B,
                           const glm::mat4& matA = glm::mat4(1.f),
                           const glm::mat4& matB = glm::mat4(1.f),
                           const Options& opts   = {});

    Mesh* compute(Operation op,
                  const Mesh& A, const Mesh& B,
                  const glm::mat4& matA = glm::mat4(1.f),
                  const glm::mat4& matB = glm::mat4(1.f),
                  const Options& opts   = {});

    // Symmetric difference (XOR): parts in A or B but not both.
    Mesh* makeSymmetricDifference(const Mesh& A, const Mesh& B,
                                  const glm::mat4& matA = glm::mat4(1.f),
                                  const glm::mat4& matB = glm::mat4(1.f),
                                  const Options& opts   = {});

    // Invert a single mesh (flip inside/outside, reverse winding + normals).
    Mesh* makeInvert(const Mesh& A,
                     const glm::mat4& matA = glm::mat4(1.f),
                     const Options& opts   = {});

    // Split a mesh along a plane. Returns two halves (front, back).
    // Either pointer may be null if the mesh is entirely on one side.
    struct SplitResult { Mesh* front = nullptr; Mesh* back = nullptr; };
    SplitResult makeSplit(const Mesh& A,
                          const glm::vec3& planeNormal, float planeDist,
                          const glm::mat4& matA = glm::mat4(1.f),
                          const Options& opts   = {});

    // Hollow (shell): shrink a copy inward and subtract from the original.
    // thickness = wall thickness in world units.
    Mesh* makeHollow(const Mesh& A, float thickness,
                     const glm::mat4& matA = glm::mat4(1.f),
                     const Options& opts   = {});

} // namespace CSG