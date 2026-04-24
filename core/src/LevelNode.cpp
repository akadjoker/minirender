#include "LevelNode.hpp"
#include "RenderState.hpp"
#include "Camera.hpp"

LevelNode::LevelNode() : RenderableNode()
{
    type = NodeType::MeshNode;
}

void LevelNode::render(Shader* shader, Camera* camera)
{
    if (!shader || !camera || !levelMesh)
        return;

    const glm::mat4 model = worldMatrix();
    const BoundingBox worldBounds = levelMesh->aabb.transformed(model);
    if (worldBounds.is_valid() && !camera->frustum.contains(worldBounds))
        return;

    shader->setMat4("u_model", model);
    shader->setMat3("u_normalMatrix", glm::mat3(glm::transpose(glm::inverse(model))));

    if (lightmapTexture)
        bindLightmap(shader);

    for (const Surface& surface : levelMesh->surfaces)
    {
        Material* mat = (surface.material_index >= 0 &&
                         surface.material_index < (int)levelMesh->materials.size())
                        ? levelMesh->materials[surface.material_index]
                        : nullptr;

        if (mat)
        {
            mat->applyStates();
            mat->applyUniformsTo(shader);
            mat->bindTexturesTo(shader);
        }
        else
        {
            Material::applyDefaultStates();
        }

        levelMesh->buffer.drawRange(surface.index_start, surface.index_count);
    }
}

void LevelNode::uploadLightmap()
{
    if (!levelMesh || levelMesh->lightmap.empty())
        return;

    if (lightmapTexture)
    {
        glDeleteTextures(1, &lightmapTexture);
        lightmapTexture = 0;
    }

    const auto& lm = levelMesh->lightmap;
    GLenum format = (lm.channels == 4) ? GL_RGBA : GL_RGB;

    glGenTextures(1, &lightmapTexture);
    glBindTexture(GL_TEXTURE_2D, lightmapTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, format, lm.width, lm.height, 0,
                 format, GL_UNSIGNED_BYTE, lm.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void LevelNode::bindLightmap(Shader* shader, int unit) const
{
    if (!lightmapTexture) return;
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, lightmapTexture);
    shader->setInt("u_lightmap", unit);
}
