#pragma once

#include <string>

#include "genesis/GenesisTypes.hpp"

namespace mini_genesis
{
class Genesis3dtFile
{
public:
    bool load(const std::string &path, Map3dtData &out, std::string &error) const;
};
} // namespace mini_genesis
