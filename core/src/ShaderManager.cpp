// ─────────────────────────────────────────────────────────────────────────────
//  ShaderManager.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "Manager.hpp"

ShaderManager &ShaderManager::instance()
{
    static ShaderManager instance;
    return instance;
}

Shader *ShaderManager::load(const std::string &name,
                            const std::string &vertPath,
                            const std::string &fragPath)
{
    if (Shader *existing = get(name))
    {
        SDL_Log("[ShaderManager] '%s' already cached", name.c_str());
        return existing;
    }

    Shader *s = new Shader(name);

    if (!s->load(vertPath, fragPath))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[ShaderManager] Failed to load '%s' (%s | %s)",
                     name.c_str(), vertPath.c_str(), fragPath.c_str());
        delete s;
        return nullptr;
    }

    paths[name] = {vertPath, fragPath};
    cache[name] = s;

    SDL_Log("[ShaderManager] Loaded '%s'", name.c_str());
    return s;
}

Shader *ShaderManager::loadFromSource(const std::string &name,
                                      const std::string &vertSrc,
                                      const std::string &fragSrc)
{
    if (Shader *existing = get(name))
    {
        SDL_Log("[ShaderManager] '%s' already cached", name.c_str());
        return existing;
    }

    Shader *s = new Shader(name);

    if (!s->loadFromSource(vertSrc, fragSrc))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[ShaderManager] Failed to compile inline shader '%s'",
                     name.c_str());
        delete s;
        return nullptr;
    }

    cache[name] = s;

    SDL_Log("[ShaderManager] Compiled inline shader '%s'", name.c_str());
    return s;
}

bool ShaderManager::reload(const std::string &name)
{
    auto pit = paths.find(name);
    if (pit == paths.end())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[ShaderManager] reload('%s'): no file paths — loaded from source?",
                    name.c_str());
        return false;
    }

    Shader *s = get(name);
    if (!s)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[ShaderManager] reload('%s'): not in cache",
                     name.c_str());
        return false;
    }

    const bool ok = s->load(pit->second.vert, pit->second.frag);

    if (ok)
        SDL_Log("[ShaderManager] Hot-reloaded '%s'", name.c_str());
    else
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[ShaderManager] Hot-reload FAILED for '%s' — keeping old program",
                     name.c_str());

    return ok;
}

void ShaderManager::reloadAll()
{
    for (const auto &[name, _] : paths)
        reload(name);
}