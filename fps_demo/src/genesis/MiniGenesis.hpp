#pragma once

#include <string>

#include "genesis/Genesis3dtFile.hpp"
#include "genesis/GenesisBrushMeshBuilder.hpp"
#include "genesis/GenesisEntities.hpp"
#include "genesis/GenesisGbspFile.hpp"

namespace mini_genesis
{
class MiniGenesis
{
public:
    bool loadGbsp(const std::string &path, GbspData &out, std::string &error) const
    {
        return gbsp_.load(path, out, error);
    }

    bool load3dt(const std::string &path, Map3dtData &out, std::string &error) const
    {
        return map3dt_.load(path, out, error);
    }

    bool buildBrushMesh(const Map3dtData &map, Mesh &mesh, std::string &error) const
    {
        return brushBuilder_.build(map, mesh, error);
    }

private:
    GenesisGbspFile gbsp_;
    Genesis3dtFile map3dt_;
    GenesisBrushMeshBuilder brushBuilder_;
};
} // namespace mini_genesis
