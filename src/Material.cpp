
#include "Material.hpp"
#include "RenderState.hpp"

Material* Material::setTexture(const std::string& u, Texture* t)
{
    for (auto& slot : textures)
    {
        if (slot.uniform == u)
        {
            slot.texture = t;
            return this;
        }
    }
    textures.push_back({ t, u });
    return this;
}


Material *Material::setInt(const std::string &u, int v)
{
    uniforms[u] = {.type = UniformType::Int, .i = v};
    return this;
}
Material *Material::setFloat(const std::string &u, float v)
{
    uniforms[u] = {.type = UniformType::Float, .f = v};
    return this;
}
Material *Material::setVec2(const std::string &u, glm::vec2 v)
{
    uniforms[u] = {.type = UniformType::Vec2, .v2 = v};
    return this;
}
Material *Material::setVec3(const std::string &u, glm::vec3 v)
{
    uniforms[u] = {.type = UniformType::Vec3, .v3 = v};
    return this;
}
Material *Material::setVec4(const std::string &u, glm::vec4 v)
{
    uniforms[u] = {.type = UniformType::Vec4, .v4 = v};
    return this;
}
Material *Material::setMat3(const std::string &u, glm::mat3 v)
{
    uniforms[u] = {.type = UniformType::Mat3, .m3 = v};
    return this;
}
Material *Material::setMat4(const std::string &u, glm::mat4 v)
{
    uniforms[u] = {.type = UniformType::Mat4, .m4 = v};
    return this;
}

void Material::applyStates() const
{
    auto &rs = RenderState::instance();
    rs.setBlend(blend);
    rs.setCull(cullFace);
    rs.setDepthTest(depthTest);
    rs.setDepthWrite(depthWrite);
}

void Material::bindTextures() const
{
    auto &rs = RenderState::instance();
    int unit = 0;
    for (const auto &slot : textures)
    {
        if (slot.texture)
        {
            rs.bindTexture(unit, slot.texture->target, slot.texture->id);
            shader->setInt(slot.uniform, unit);
            unit++;
        }
    }
}

void Material::applyUniforms() const
{
    for (const auto &[name, u] : uniforms)
    {
        switch (u.type)
        {
        case UniformType::Int: shader->setInt(name, u.i); break;
        case UniformType::Float: shader->setFloat(name, u.f); break;
        case UniformType::Vec2: shader->setVec2(name, u.v2); break;
        case UniformType::Vec3: shader->setVec3(name, u.v3); break;
        case UniformType::Vec4: shader->setVec4(name, u.v4); break;
        case UniformType::Mat3: shader->setMat3(name, u.m3); break;
        case UniformType::Mat4: shader->setMat4(name, u.m4); break;
        }
    }
}

void Material::bindTexturesTo(Shader *sh) const
{
    auto &rs = RenderState::instance();
    int unit = 0;
    for (const auto &slot : textures)
    {
        if (slot.texture)
        {
            rs.bindTexture(unit, slot.texture->target, slot.texture->id);
            sh->setInt(slot.uniform, unit);
            unit++;
        }
    }
}

void Material::applyUniformsTo(Shader *sh) const
{
    for (const auto &[name, u] : uniforms)
    {
        switch (u.type)
        {
        case UniformType::Int:   sh->setInt  (name, u.i);  break;
        case UniformType::Float: sh->setFloat(name, u.f);  break;
        case UniformType::Vec2:  sh->setVec2 (name, u.v2); break;
        case UniformType::Vec3:  sh->setVec3 (name, u.v3); break;
        case UniformType::Vec4:  sh->setVec4 (name, u.v4); break;
        case UniformType::Mat3:  sh->setMat3 (name, u.m3); break;
        case UniformType::Mat4:  sh->setMat4 (name, u.m4); break;
        }
    }
}

void Material::bind() const
{
    if (!shader) return;
    RenderState::instance().useProgram(shader->getId());
    applyStates();
    bindTextures();
    applyUniforms();
}
