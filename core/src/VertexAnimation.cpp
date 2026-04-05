#include "Manager.hpp"

VertexAnimatedMeshManager &VertexAnimatedMeshManager::instance()
{
    static VertexAnimatedMeshManager inst;
    return inst;
}

VertexAnimatedMesh *VertexAnimatedMeshManager::create(const std::string &name)
{
    if (has(name))
        return get(name);

    auto *mesh = new VertexAnimatedMesh();
    mesh->name = name;
    cache[name] = mesh;
    return mesh;
}

VertexAnimatedMesh *VertexAnimatedMeshManager::load(const std::string &name,
                                                    const std::string &path,
                                                    const std::string &texture_dir)
{
    if (auto *existing = get(name))
        return existing;

    const auto dot = path.rfind('.');
    if (dot == std::string::npos)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[VertexAnimatedMeshManager] No extension in path: %s", path.c_str());
        return nullptr;
    }

    const std::string ext = path.substr(dot + 1);
    if (ext == "md2")
        return load_md2(name, path, texture_dir);
    if (ext == "md3")
        return load_md3(name, path, texture_dir);

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[VertexAnimatedMeshManager] Unknown vertex anim mesh format '%s': %s",
                ext.c_str(), path.c_str());
    return nullptr;
}
