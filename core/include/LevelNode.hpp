#pragma once
#include "Node.hpp"
#include "LevelFormat.hpp"
#include <vector>

// ============================================================
//  LevelNode — renders a LevelMesh in the scene graph
//
//  Uses LevelMeshBuffer (LevelVertex with 2 UV channels).
//  Supports per-surface material + lightmap texture binding.
// ============================================================
class LevelNode : public RenderableNode
{
public:
    LevelMesh* levelMesh = nullptr;
    std::vector<GLuint> lightmapTextures;

    LevelNode();

    void render(Shader* shader, Camera* camera) override;

    // Upload the embedded lightmap from levelMesh to a GL texture.
    // Call once after loading.
    void uploadLightmap();

    void bindLightmap(Shader* shader, int lightmapIndex, int unit = 2) const;
};
