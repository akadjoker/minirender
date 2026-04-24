#pragma once
#include "Node.hpp"
#include "LevelFormat.hpp"

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
    GLuint     lightmapTexture = 0;     // uploaded lightmap GL texture

    LevelNode();

    void render(Shader* shader, Camera* camera) override;

    // Upload the embedded lightmap from levelMesh to a GL texture.
    // Call once after loading.
    void uploadLightmap();

    // Bind lightmap to a texture unit (default: unit 2)
    void bindLightmap(Shader* shader, int unit = 2) const;
};
