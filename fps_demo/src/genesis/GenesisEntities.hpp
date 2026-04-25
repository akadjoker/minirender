#pragma once

#include <string>
#include <vector>

#include "genesis/GenesisTypes.hpp"

namespace mini_genesis
{
struct PlayerStart
{
    std::string name;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f);
};

struct LightEntity
{
    std::string name;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 300.0f;
};

class GenesisEntities
{
public:
    static glm::vec3 parseVec3(const std::string &value, const glm::vec3 &fallback = glm::vec3(0.0f));

    static void extractPlayerStarts(const std::vector<BspEntity> &entities,
                                    std::vector<PlayerStart> &outStarts);

    static void extractPlayerStarts(const std::vector<Entity> &entities,
                                    std::vector<PlayerStart> &outStarts);

    static void extractLights(const std::vector<BspEntity> &entities,
                              std::vector<LightEntity> &outLights);

    static void extractLights(const std::vector<Entity> &entities,
                              std::vector<LightEntity> &outLights);
};
} // namespace mini_genesis
