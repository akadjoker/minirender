#include "genesis/GenesisMoverSystem.hpp"

#include <algorithm>
#include <cmath>

#include "genesis/GenesisEntities.hpp"
#include "genesis/GenesisUtils.hpp"

namespace mini_genesis
{
namespace
{
float parseFloatOr(const std::unordered_map<std::string, std::string> &kv,
                  const char *key,
                  float fallback)
{
    const auto it = kv.find(key);
    if (it == kv.end() || it->second.empty())
        return fallback;
    try
    {
        return std::stof(it->second);
    }
    catch (...)
    {
        return fallback;
    }
}

std::string getOr(const std::unordered_map<std::string, std::string> &kv, const char *key)
{
    const auto it = kv.find(key);
    if (it != kv.end())
        return it->second;
    return {};
}

glm::vec3 parseAnglesToDir(const std::string &angles)
{
    const glm::vec3 gdir = genesisAnglesToDir(GenesisEntities::parseVec3(angles, glm::vec3(0.0f)));
    return genesisDirToEngine(gdir);
}
} // namespace

void GenesisMoverSystem::clear()
{
    movers_.clear();
    triggers_.clear();
}

void GenesisMoverSystem::ingestKV(const std::unordered_map<std::string, std::string> &kv)
{
    const std::string classname = toLower(getOr(kv, "classname"));
    if (classname.empty())
        return;

    if (classname == "func_door" || classname == "func_door_rotating" || classname == "func_plat")
    {
        GenesisMover m;
        m.type = (classname == "func_plat") ? MoverType::Elevator : MoverType::Door;
        m.name = getOr(kv, "%name%");
        m.model = getOr(kv, "model");
        m.targetName = getOr(kv, "targetname");
        m.origin = genesisPointToEngine(GenesisEntities::parseVec3(getOr(kv, "origin"), glm::vec3(0.0f)));
        m.speed = parseFloatOr(kv, "speed", classname == "func_plat" ? 80.0f : 100.0f);
        m.wait = parseFloatOr(kv, "wait", 3.0f);
        m.travel = parseFloatOr(kv, "lip", 64.0f);

        if (classname == "func_plat")
            m.moveDir = genesisDirToEngine(glm::vec3(0.0f, 1.0f, 0.0f));
        else
            m.moveDir = parseAnglesToDir(getOr(kv, "angles"));

        movers_.push_back(std::move(m));
        return;
    }

    if (classname == "trigger_once" || classname == "trigger_multiple")
    {
        GenesisTrigger t;
        t.target = getOr(kv, "target");
        t.origin = genesisPointToEngine(GenesisEntities::parseVec3(getOr(kv, "origin"), glm::vec3(0.0f)));
        t.radius = parseFloatOr(kv, "radius", 56.0f);
        triggers_.push_back(std::move(t));
    }
}

void GenesisMoverSystem::buildFromBspEntities(const std::vector<BspEntity> &entities)
{
    clear();
    for (const BspEntity &e : entities)
        ingestKV(e.kv);
}

void GenesisMoverSystem::buildFrom3dtEntities(const std::vector<Entity> &entities)
{
    clear();
    for (const Entity &e : entities)
        ingestKV(e.kv);
}

void GenesisMoverSystem::activateTarget(const std::string &target)
{
    if (target.empty())
        return;

    for (GenesisMover &m : movers_)
    {
        if (m.targetName != target)
            continue;
        m.moving = true;
        m.opening = true;
        m.waitTimer = 0.0f;
    }
}

void GenesisMoverSystem::update(float dt, const glm::vec3 &playerPos)
{
    for (const GenesisTrigger &t : triggers_)
    {
        const float d2 = glm::length2(playerPos - t.origin);
        if (d2 <= t.radius * t.radius)
            activateTarget(t.target);
    }

    for (GenesisMover &m : movers_)
    {
        if (!m.moving)
            continue;

        if (m.opening)
        {
            m.amount += (m.speed * dt) / std::max(1.0f, m.travel);
            if (m.amount >= 1.0f)
            {
                m.amount = 1.0f;
                m.opening = false;
                m.waitTimer = m.wait;
            }
            continue;
        }

        if (m.waitTimer > 0.0f)
        {
            m.waitTimer -= dt;
            continue;
        }

        m.amount -= (m.speed * dt) / std::max(1.0f, m.travel);
        if (m.amount <= 0.0f)
        {
            m.amount = 0.0f;
            m.moving = false;
        }
    }
}
} // namespace mini_genesis
