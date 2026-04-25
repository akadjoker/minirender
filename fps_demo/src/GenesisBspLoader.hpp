#pragma once

#include <string>
#include <vector>

#include "GenesisBspCollider.hpp"
#include "Mesh.hpp"

struct GenesisLoadResult
{
    std::vector<glm::vec3> playerStarts;
    std::vector<glm::vec3> playerStartForwards;
    BoundingBox bounds = {};
    std::string status;
    std::string error;
};

class GenesisBspLoader
{
public:
    bool load(const std::string &path,
              Mesh &mesh,
              GenesisBspCollider &collider,
              GenesisLoadResult &out) const;

    static glm::vec3 pointToEngine(const glm::vec3 &point);
};
