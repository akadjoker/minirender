
#include "Manager.hpp"


 

// ═════════════════════════════════════════════════════════════════════════════
//  MaterialManager
// ═════════════════════════════════════════════════════════════════════════════
MaterialManager &MaterialManager::instance()
{
    static MaterialManager instance;
    return instance;
}

Material *MaterialManager::create(const std::string &name)
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
    if (defaultShader_)
        m->setShader(defaultShader_);
    cache[name] = m;

    SDL_Log("[MaterialManager] Created '%s'", name.c_str());
    return m;
}

void MaterialManager::applyDefaults()
{
    for (auto &[n, mat] : cache)
    {
        if (defaultShader_ && !mat->getShader())
            mat->setShader(defaultShader_);
        if (fallbackTex_ && !mat->hasTexture("u_albedo"))
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

 