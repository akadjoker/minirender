#include "Manager.hpp"
#include "MeshLoader.hpp"

AnimatedMeshManager &AnimatedMeshManager::instance()
{
    static AnimatedMeshManager inst;
    return inst;
}

AnimatedMesh *AnimatedMeshManager::create(const std::string &name)
{
    if (has(name))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[AnimatedMeshManager] '%s' already exists, returning existing",
                    name.c_str());
        return get(name);
    }

    auto *mesh = new AnimatedMesh();
    mesh->name = name;
    cache[name] = mesh;
    return mesh;
}

AnimatedMesh *AnimatedMeshManager::load(const std::string &name,
                                        const std::string &path,
                                        const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    auto dot = path.rfind('.');
    if (dot == std::string::npos)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[AnimatedMeshManager] No extension in path: %s", path.c_str());
        return nullptr;
    }

    std::string ext = path.substr(dot + 1);
    if (ext == "h3d" || ext == "mesh")
        return load_h3d(name, path, texture_dir);

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[AnimatedMeshManager] Unknown animated mesh format '%s': %s",
                ext.c_str(), path.c_str());
    return nullptr;
}

AnimatedMesh *AnimatedMeshManager::load_h3d(const std::string &name,
                                            const std::string &path,
                                            const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    auto *mesh = new AnimatedMesh();
    mesh->name = name;

    MeshReader reader;
    reader.textureDir = texture_dir;

    if (!reader.load(path, mesh))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[AnimatedMeshManager] Failed to load h3d '%s': %s",
                     name.c_str(), path.c_str());
        delete mesh;
        return nullptr;
    }
    cache[name] = mesh;
    return mesh;
}
