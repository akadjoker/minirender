#pragma once

#include <string>
#include <vector>

#include <sys/stat.h>

#include "Core.hpp"
#include "Device.hpp"

class IDemo
{
public:
    virtual ~IDemo() {}

    virtual const char *title() const = 0;
    virtual const char *description() const
    {
        return "";
    }

    virtual bool setup(Device &device) = 0;
    virtual void update(float dt) = 0;
    virtual void drawGui() = 0;
    virtual void render() = 0;
    virtual void shutdown() = 0;
};

struct DemoEntry
{
    const char *name;
    IDemo *(*create)();
};

inline void unloadDemoAssets()
{
    AnimatedMeshManager::instance().unloadAll();
    VertexAnimatedMeshManager::instance().unloadAll();
    MeshManager::instance().unloadAll();
    TextureManager::instance().unloadAll();
    ShaderManager::instance().unloadAll();
}

inline bool demoPathExists(const std::string &path)
{
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

inline std::string demoJoinPath(const std::string &base, const std::string &path)
{
    if (base.empty() || base == ".")
        return path;
    if (base[base.size() - 1] == '/')
        return base + path;
    return base + "/" + path;
}

inline std::string findProjectAssetRoot()
{
    const std::vector<std::string> relativeCandidates = {
        "assets",
        "../assets",
        "../../assets",
    };

    for (size_t i = 0; i < relativeCandidates.size(); ++i)
    {
        if (demoPathExists(relativeCandidates[i]))
            return relativeCandidates[i];
    }

    char *basePath = SDL_GetBasePath();
    if (!basePath)
        return "assets";

    const std::string executableBase(basePath);
    SDL_free(basePath);

    const std::vector<std::string> executableCandidates = {
        demoJoinPath(executableBase, "../assets"),
        demoJoinPath(executableBase, "../../assets"),
        demoJoinPath(executableBase, "assets"),
    };

    for (size_t i = 0; i < executableCandidates.size(); ++i)
    {
        if (demoPathExists(executableCandidates[i]))
            return executableCandidates[i];
    }

    return "assets";
}
