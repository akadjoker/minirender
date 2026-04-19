#pragma once

#include "LevelEditorScene.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

struct LightmapResult
{
    struct Atlas
    {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> pixels; // RGB
        std::string savedPath;
    };

    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // RGB
    std::string savedPath;
    std::vector<Atlas> atlases;

    // Per-mesh: for each GPU vertex, the lightmap UV (stored in tangent.xy)
    // Indexed by mesh object index, then by face index within that mesh
    struct MeshLmUVs
    {
        // Per EditableFace, per vertex in that face: lightmap UV
        std::vector<std::vector<glm::vec2>> faceVertexUVs;
        std::vector<int> faceAtlasIndices;
    };
    std::vector<MeshLmUVs> meshUVs;
};

struct LightmapSettings
{
    int resolution = 256;       // atlas size (NxN)
    int samplesPerTexel = 1;    // for soft shadows (1 = hard)
    float bias = 2.0f;          // shadow ray bias
    float ambient = 0.05f;      // minimum ambient light
    std::string outputPath = "lightmap.png";
};

// Bake lightmaps for the entire scene
// progress: optional atomic float [0..1] updated during bake
LightmapResult BakeLightmaps(const LevelEditorScene& scene, const LightmapSettings& settings,
                              std::atomic<float>* progress = nullptr);
