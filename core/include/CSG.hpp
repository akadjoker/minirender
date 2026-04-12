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
//    • Both meshes should be watertight (closed, no holes).
//    • Faces wound counter-clockwise (standard OpenGL front-face).
//
//  Pipeline:
//    1. Flatten to world-space triangle soup (preserving matIndex).
//    2. BVH broad-phase — O(n log n), SAH split.
//    3. Triangle–triangle intersection (Möller 1997) with full
//       coplanar handling via Sutherland-Hodgman clipping.
//    4. cutTriangle() — per-edge bucket sort, dedup, ear-clip.
//    5. isInsideMesh() — rays offset by rayEpsilon; only tests
//       against the *other* mesh's soup (no auto-intersections).
//    6. Face selection by operation.
//    7. buildMesh() — weld, remove degenerate tris, rebuild
//       Surface/material table, compute_normals/tangents, upload.
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

} // namespace CSG