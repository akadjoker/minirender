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

    shader->setMat4("u_model", model);
    shader->setMat3("u_normalMatrix", glm::mat3(glm::transpose(glm::inverse(model))));

    for (const Surface& surface : levelMesh->surfaces)
    {
        shader->setVec3("u_color", glm::vec3(0.8f));
        shader->setInt("u_hasAlbedo", 0);
        const bool hasSurfaceLightmap = surface.lightmap_index >= 0 &&
                                        surface.lightmap_index < (int)lightmapTextures.size() &&
                                        lightmapTextures[(size_t)surface.lightmap_index] != 0;
        shader->setInt("u_hasLightmap", hasSurfaceLightmap ? 1 : 0);
        if (hasSurfaceLightmap)
            bindLightmap(shader, surface.lightmap_index);

        Material* mat = (surface.material_index >= 0 &&
                         surface.material_index < (int)levelMesh->materials.size())
                        ? levelMesh->materials[surface.material_index]
                        : nullptr;

        if (mat)
        {
            mat->applyStates();
            mat->applyUniformsTo(shader);
            shader->setInt("u_hasAlbedo", mat->hasTexture("u_albedo") ? 1 : 0);
            mat->bindTexturesTo(shader);
        }
        else
        {
            Material::applyDefaultStates();
            shader->setVec3("u_color", glm::vec3(0.8f));
            shader->setInt("u_hasAlbedo", 0);
        }

        levelMesh->buffer.drawRange(surface.index_start, surface.index_count);
    }
}

void LevelNode::uploadLightmap()
{
    if (!levelMesh || levelMesh->lightmaps.empty())
        return;

    for (GLuint texture : lightmapTextures)
    {
        if (texture)
            glDeleteTextures(1, &texture);
    }
    lightmapTextures.clear();
    lightmapTextures.reserve(levelMesh->lightmaps.size());

    for (const auto& lm : levelMesh->lightmaps)
    {
        GLuint texture = 0;
        GLenum format = (lm.channels == 4) ? GL_RGBA : GL_RGB;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, format, lm.width, lm.height, 0,
                     format, GL_UNSIGNED_BYTE, lm.pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        lightmapTextures.push_back(texture);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void LevelNode::bindLightmap(Shader* shader, int lightmapIndex, int unit) const
{
    if (lightmapIndex < 0 || lightmapIndex >= (int)lightmapTextures.size()) return;
    GLuint texture = lightmapTextures[(size_t)lightmapIndex];
    if (!texture) return;
    RenderState::instance().bindTexture(unit, GL_TEXTURE_2D, texture);
    shader->setInt("u_lightmap", unit);
}
