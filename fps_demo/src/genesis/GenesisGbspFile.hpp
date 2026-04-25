#pragma once

#include <string>

#include "genesis/GenesisTypes.hpp"

namespace mini_genesis
{
class GenesisGbspFile
{
public:
    bool load(const std::string &path, GbspData &out, std::string &error) const;

private:
    bool parseEntData(const std::vector<uint8_t> &entData,
                      std::vector<BspEntity> &entities,
                      std::string &error) const;
};
} // namespace mini_genesis
