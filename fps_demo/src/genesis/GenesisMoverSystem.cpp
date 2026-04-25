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

std::string getOrAny(const std::unordered_map<std::string, std::string> &kv,
                     const char *k0,
                     const char *k1 = nullptr,
                     const char *k2 = nullptr)
{
    if (const auto it = kv.find(k0); it != kv.end() && !it->second.empty())
        return it->second;
    if (k1)
    {
        if (const auto it = kv.find(k1); it != kv.end() && !it->second.empty())
            return it->second;
    }
    if (k2)
    {
        if (const auto it = kv.find(k2); it != kv.end() && !it->second.empty())
            return it->second;
    }
    return {};
}

glm::vec3 parseAnglesToDir(const std::string &angles)
{
    const glm::vec3 gdir = genesisAnglesToDir(GenesisEntities::parseVec3(angles, glm::vec3(0.0f)));
    return genesisDirToEngine(gdir);
}

bool isDoorLike(const std::string &classname)
{
    return classname == "func_door" ||
           classname == "func_door_rotating" ||
           classname.find("door") != std::string::npos;
}

bool isElevatorLike(const std::string &classname)
{
    return classname == "func_plat" ||
           classname.find("plat") != std::string::npos ||
           classname.find("lift") != std::string::npos ||
           classname.find("elevator") != std::string::npos;
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

    if (isDoorLike(classname) || isElevatorLike(classname))
    {
        GenesisMover m;
        m.type = isElevatorLike(classname) ? MoverType::Elevator : MoverType::Door;
        m.name = getOr(kv, "%name%");
        m.model = getOr(kv, "model");
        m.targetName = getOrAny(kv, "targetname", "target");
        m.origin = genesisPointToEngine(GenesisEntities::parseVec3(getOrAny(kv, "origin", "Origin"), glm::vec3(0.0f)));
        m.speed = parseFloatOr(kv, "speed", m.type == MoverType::Elevator ? 80.0f : 100.0f);
        m.wait = parseFloatOr(kv, "wait", 3.0f);
        m.travel = parseFloatOr(kv, "lip", 64.0f);
        if (m.travel <= 0.0f)
            m.travel = parseFloatOr(kv, "height", 64.0f);
        if (m.travel <= 0.0f)
            m.travel = parseFloatOr(kv, "distance", 64.0f);
        m.autoTriggerRadius = parseFloatOr(kv, "radius", m.type == MoverType::Elevator ? 120.0f : 96.0f);
        if (m.autoTriggerRadius <= 0.0f)
            m.autoTriggerRadius = (m.type == MoverType::Elevator ? 120.0f : 96.0f);

        const glm::vec3 movedir = GenesisEntities::parseVec3(getOrAny(kv, "movedir", "move_dir"), glm::vec3(0.0f));
        if (glm::length2(movedir) > 1e-8f)
            m.moveDir = genesisDirToEngine(glm::normalize(movedir));
        else if (m.type == MoverType::Elevator)
            m.moveDir = genesisDirToEngine(glm::vec3(0.0f, 1.0f, 0.0f));
        else
            m.moveDir = parseAnglesToDir(getOrAny(kv, "angles", "angle"));

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
        m.prevAmount = m.amount;

        if (forceAutoLoop_ && !m.moving)
        {
            m.moving = true;
            m.opening = true;
            m.waitTimer = 0.0f;
        }

        // Fallback behavior for maps without trigger entities wired to targets:
        // start mover when player approaches its origin.
        if (!forceAutoLoop_ && !m.moving)
        {
            const float r = std::max(1.0f, m.autoTriggerRadius);
            const float d2 = glm::length2(playerPos - m.origin);
            if (d2 <= r * r)
            {
                m.moving = true;
                m.opening = true;
                m.waitTimer = 0.0f;
            }
        }

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
