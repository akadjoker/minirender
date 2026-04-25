#pragma once

#include <string>

#include "Mesh.hpp"
#include "genesis/GenesisTypes.hpp"

namespace mini_genesis
{
class GenesisBrushMeshBuilder
{
public:
    bool build(const Map3dtData &map,
               Mesh &mesh,
               std::string &error) const;
};
} // namespace mini_genesis
