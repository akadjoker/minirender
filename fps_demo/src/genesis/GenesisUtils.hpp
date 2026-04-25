#pragma once

#include <string>
#include <vector>

#include "Math.hpp"

namespace mini_genesis
{
std::string toLower(std::string value);
std::string trim(const std::string &value);
bool startsWith(const std::string &value, const std::string &prefix);

bool readFileBytes(const std::string &path, std::vector<uint8_t> &bytes, std::string &error);
bool readFileText(const std::string &path, std::string &text, std::string &error);

int32_t readI32Raw(const std::vector<uint8_t> &bytes, size_t offset);
float readF32Raw(const std::vector<uint8_t> &bytes, size_t offset);
std::string readFixedStringRaw(const std::vector<uint8_t> &bytes, size_t offset, size_t len);

std::string parseQuoted(const std::string &line);

// Genesis3D (x,y,z) -> engine OpenGL space (x,z,y)
glm::vec3 genesisPointToEngine(const glm::vec3 &point);
glm::vec3 enginePointToGenesis(const glm::vec3 &point);
glm::vec3 genesisDirToEngine(const glm::vec3 &dir);
glm::vec3 engineDirToGenesis(const glm::vec3 &dir);

// Genesis "angles" convention: x=pitch, y=yaw, z=roll (degrees).
// Returns direction in Genesis space.
glm::vec3 genesisAnglesToDir(const glm::vec3 &anglesDegrees);
} // namespace mini_genesis
