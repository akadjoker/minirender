
#include "Manager.hpp"


 

// ═════════════════════════════════════════════════════════════════════════════
//  MaterialManager
// ═════════════════════════════════════════════════════════════════════════════
MaterialManager &MaterialManager::instance()
{
    static MaterialManager instance;
    return instance;
}

Material *MaterialManager::create(const std::string &name, MaterialType type)
{
    if (has(name))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[MaterialManager] '%s' already exists, returning existing",
                    name.c_str());
        return get(name);
    }

    Material *m = new Material();
    m->name = name;
    m->setType(type);
    if (defaultShader_)
        m->setShader(defaultShader_);
    cache[name] = m;

    SDL_Log("[MaterialManager] Created '%s'", name.c_str());
    return m;
}

Material *MaterialManager::createSolid(const std::string &name, const glm::vec4 &color)
{
    Material *mat = create(name, MaterialType::Solid);
    return mat ? mat->setVec4("u_color", color) : nullptr;
}

Material *MaterialManager::createTextured(const std::string &name, Texture *albedo,
                                          const glm::vec4 &color)
{
    Material *mat = create(name, MaterialType::Textured);
    if (!mat) return nullptr;
    mat->setVec4("u_color", color);
    if (albedo)
        mat->setTexture("u_albedo", albedo);
    return mat;
}

Material *MaterialManager::createDetail(const std::string &name, Texture *base, Texture *detail,
                                        float detailScale, const glm::vec4 &color)
{
    Material *mat = create(name, MaterialType::Detail);
    if (!mat) return nullptr;
    mat->setVec4("u_color", color);
    mat->setFloat("u_detailScale", detailScale);
    if (base)
        mat->setTexture("u_albedo", base);
    if (detail)
        mat->setTexture("u_detail", detail);
    return mat;
}

Material *MaterialManager::createTerrain(const std::string &name, Texture *base, Texture *detail,
                                         float detailBlend, const glm::vec4 &color)
{
    Material *mat = create(name, MaterialType::Terrain);
    if (!mat) return nullptr;
    mat->setVec4("u_color", color);
    mat->setFloat("u_detailBlend", detailBlend);
    if (base)
        mat->setTexture("u_albedo", base);
    if (detail)
        mat->setTexture("u_detail", detail);
    return mat;
}

Material *MaterialManager::createWater(const std::string &name)
{
    Material *mat = create(name, MaterialType::Water);
    if (!mat) return nullptr;
    mat->setBlend(false);
    mat->setDepthTest(true);
    mat->setDepthWrite(true);
    mat->setCullFace(false);
    return mat;
}

Material *MaterialManager::createSkinned(const std::string &name, Texture *albedo,
                                         const glm::vec4 &color)
{
    Material *mat = create(name, MaterialType::Skinned);
    if (!mat) return nullptr;
    mat->setVec4("u_color", color);
    if (albedo)
        mat->setTexture("u_albedo", albedo);
    return mat;
}

void MaterialManager::applyDefaults()
{
    for (auto &[n, mat] : cache)
    {
        if (defaultShader_ && !mat->getShader())
            mat->setShader(defaultShader_);
        if (fallbackTex_ &&
            mat->getType() != MaterialType::Solid &&
            mat->getType() != MaterialType::Water &&
            !mat->hasTexture("u_albedo"))
            mat->setTexture("u_albedo", fallbackTex_);
    }
}

Material *MaterialManager::clone(const std::string &srcName,
                                 const std::string &newName)
{
    Material *src = get(srcName);
    if (!src)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[MaterialManager] clone: source '%s' not found", srcName.c_str());
        return nullptr;
    }

    if (has(newName))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[MaterialManager] clone: '%s' already exists", newName.c_str());
        return get(newName);
    }

    Material *m = new Material(*src);
    m->name = newName;
    cache[newName] = m;

    SDL_Log("[MaterialManager] Cloned '%s' -> '%s'", srcName.c_str(), newName.c_str());
    return m;
}

 
