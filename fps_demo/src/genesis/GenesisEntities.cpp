#include "genesis/GenesisEntities.hpp"

#include <cstdlib>
#include <cstdio>

#include "genesis/GenesisUtils.hpp"

namespace mini_genesis
{
glm::vec3 GenesisEntities::parseVec3(const std::string &value, const glm::vec3 &fallback)
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (std::sscanf(value.c_str(), "%f %f %f", &x, &y, &z) == 3)
        return glm::vec3(x, y, z);
    return fallback;
}

static bool isPlayerClass(const std::string &classname)
{
    const std::string c = toLower(classname);
    return c == "info_player_start" ||
           c == "deathmatchstart" ||
           c == "playerstart" ||
           c == "player_start";
}

static std::string valueOr(const std::unordered_map<std::string, std::string> &kv,
                           const char *key,
                           const char *alt = nullptr)
{
    auto it = kv.find(key);
    if (it != kv.end())
        return it->second;
    if (alt)
    {
        it = kv.find(alt);
        if (it != kv.end())
            return it->second;
    }
    return {};
}

static glm::vec3 parseForwardFromEntityKV(const std::unordered_map<std::string, std::string> &kv)
{
    // Prefer full "angles" (pitch yaw roll). Fallback to single yaw "angle".
    std::string angles = valueOr(kv, "angles", "Angles");
    if (!angles.empty())
    {
        const glm::vec3 gdir = genesisAnglesToDir(GenesisEntities::parseVec3(angles, glm::vec3(0.0f)));
        return genesisDirToEngine(gdir);
    }

    std::string angle = valueOr(kv, "angle", "Angle");
    if (!angle.empty())
    {
        char *end = nullptr;
        const float yaw = std::strtof(angle.c_str(), &end);
        if (end != angle.c_str())
        {
            const glm::vec3 gdir = genesisAnglesToDir(glm::vec3(0.0f, yaw, 0.0f));
            return genesisDirToEngine(gdir);
        }
    }

    return glm::vec3(0.0f, 0.0f, -1.0f);
}

void GenesisEntities::extractPlayerStarts(const std::vector<BspEntity> &entities,
                                          std::vector<PlayerStart> &outStarts)
{
    outStarts.clear();
    for (const BspEntity &entity : entities)
    {
        const std::string classname = valueOr(entity.kv, "classname");
        if (!isPlayerClass(classname))
            continue;

        PlayerStart s;
        s.name = valueOr(entity.kv, "%name%");
        s.position = genesisPointToEngine(parseVec3(valueOr(entity.kv, "origin", "origin")));
        s.forward = parseForwardFromEntityKV(entity.kv);
        outStarts.push_back(s);
    }
}

void GenesisEntities::extractPlayerStarts(const std::vector<Entity> &entities,
                                          std::vector<PlayerStart> &outStarts)
{
    outStarts.clear();
    for (const Entity &entity : entities)
    {
        const std::string classname = valueOr(entity.kv, "classname");
        if (!isPlayerClass(classname))
            continue;

        PlayerStart s;
        s.name = valueOr(entity.kv, "%name%");
        s.position = genesisPointToEngine(parseVec3(valueOr(entity.kv, "origin", "Origin")));
        s.forward = parseForwardFromEntityKV(entity.kv);
        outStarts.push_back(s);
    }
}

void GenesisEntities::extractLights(const std::vector<BspEntity> &entities,
                                    std::vector<LightEntity> &outLights)
{
    outLights.clear();
    for (const BspEntity &entity : entities)
    {
        if (toLower(valueOr(entity.kv, "classname")) != "light")
            continue;

        LightEntity l;
        l.name = valueOr(entity.kv, "%name%");
        l.position = genesisPointToEngine(parseVec3(valueOr(entity.kv, "origin")));
        l.intensity = std::max(1.0f, std::stof(valueOr(entity.kv, "light", "light").empty() ? "300" : valueOr(entity.kv, "light", "light")));
        l.color = parseVec3(valueOr(entity.kv, "color"), glm::vec3(255.0f));
        l.color /= 255.0f;
        outLights.push_back(l);
    }
}

void GenesisEntities::extractLights(const std::vector<Entity> &entities,
                                    std::vector<LightEntity> &outLights)
{
    outLights.clear();
    for (const Entity &entity : entities)
    {
        if (toLower(valueOr(entity.kv, "classname")) != "light")
            continue;

        LightEntity l;
        l.name = valueOr(entity.kv, "%name%");
        l.position = genesisPointToEngine(parseVec3(valueOr(entity.kv, "origin", "Origin")));
        l.intensity = std::max(1.0f, std::stof(valueOr(entity.kv, "light").empty() ? "300" : valueOr(entity.kv, "light")));
        l.color = parseVec3(valueOr(entity.kv, "color"), glm::vec3(255.0f));
        l.color /= 255.0f;
        outLights.push_back(l);
    }
}
} // namespace mini_genesis
