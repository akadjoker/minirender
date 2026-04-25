#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "genesis/GenesisTypes.hpp"

namespace mini_genesis
{
enum class MoverType
{
    Door,
    Elevator,
};

struct GenesisMover
{
    MoverType type = MoverType::Door;
    std::string name;
    std::string model;
    std::string targetName;
    glm::vec3 origin = glm::vec3(0.0f);
    glm::vec3 moveDir = glm::vec3(0.0f, 0.0f, 1.0f);
    float speed = 100.0f;
    float wait = 3.0f;
    float travel = 64.0f;

    bool moving = false;
    bool opening = false;
    float amount = 0.0f;
    float waitTimer = 0.0f;
};

struct GenesisTrigger
{
    std::string target;
    glm::vec3 origin = glm::vec3(0.0f);
    float radius = 56.0f;
};

class GenesisMoverSystem
{
public:
    void clear();
    void buildFromBspEntities(const std::vector<BspEntity> &entities);
    void buildFrom3dtEntities(const std::vector<Entity> &entities);

    // Position must be in engine coordinates.
    void update(float dt, const glm::vec3 &playerPos);

    const std::vector<GenesisMover> &movers() const { return movers_; }
    const std::vector<GenesisTrigger> &triggers() const { return triggers_; }

private:
    void activateTarget(const std::string &target);
    void ingestKV(const std::unordered_map<std::string, std::string> &kv);

    std::vector<GenesisMover> movers_;
    std::vector<GenesisTrigger> triggers_;
};
} // namespace mini_genesis
