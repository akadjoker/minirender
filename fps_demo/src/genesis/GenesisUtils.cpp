#include "genesis/GenesisUtils.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace mini_genesis
{
std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
    {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(const std::string &value)
{
    size_t a = 0;
    while (a < value.size() && std::isspace(static_cast<unsigned char>(value[a])))
        ++a;
    size_t b = value.size();
    while (b > a && std::isspace(static_cast<unsigned char>(value[b - 1])))
        --b;
    return value.substr(a, b - a);
}

bool startsWith(const std::string &value, const std::string &prefix)
{
    return value.rfind(prefix, 0) == 0;
}

bool readFileBytes(const std::string &path, std::vector<uint8_t> &bytes, std::string &error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "nao consegui abrir ficheiro";
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0)
    {
        error = "ficheiro vazio";
        return false;
    }
    file.seekg(0, std::ios::beg);
    bytes.resize(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char *>(bytes.data()), size))
    {
        error = "falha a ler bytes";
        return false;
    }
    return true;
}

bool readFileText(const std::string &path, std::string &text, std::string &error)
{
    std::ifstream file(path);
    if (!file)
    {
        error = "nao consegui abrir ficheiro texto";
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    text = ss.str();
    if (text.empty())
    {
        error = "ficheiro texto vazio";
        return false;
    }
    return true;
}

int32_t readI32Raw(const std::vector<uint8_t> &bytes, size_t offset)
{
    uint32_t v = 0;
    std::memcpy(&v, bytes.data() + offset, sizeof(v));
    return static_cast<int32_t>(v);
}

float readF32Raw(const std::vector<uint8_t> &bytes, size_t offset)
{
    uint32_t raw = 0;
    std::memcpy(&raw, bytes.data() + offset, sizeof(raw));
    float v = 0.0f;
    std::memcpy(&v, &raw, sizeof(v));
    return v;
}

std::string readFixedStringRaw(const std::vector<uint8_t> &bytes, size_t offset, size_t len)
{
    std::string out;
    for (size_t i = 0; i < len; ++i)
    {
        const char c = static_cast<char>(bytes[offset + i]);
        if (c == '\0')
            break;
        out.push_back(c);
    }
    return out;
}

std::string parseQuoted(const std::string &line)
{
    const size_t a = line.find('"');
    if (a == std::string::npos)
        return {};
    const size_t b = line.find('"', a + 1);
    if (b == std::string::npos)
        return {};
    return line.substr(a + 1, b - a - 1);
}

glm::vec3 genesisPointToEngine(const glm::vec3 &point)
{
    return glm::vec3(point.x, point.z, point.y);
}

glm::vec3 enginePointToGenesis(const glm::vec3 &point)
{
    return glm::vec3(point.x, point.z, point.y);
}

glm::vec3 genesisDirToEngine(const glm::vec3 &dir)
{
    const glm::vec3 out = genesisPointToEngine(dir);
    if (glm::length2(out) <= 1e-8f)
        return glm::vec3(0.0f, 0.0f, -1.0f);
    return glm::normalize(out);
}

glm::vec3 engineDirToGenesis(const glm::vec3 &dir)
{
    const glm::vec3 out = enginePointToGenesis(dir);
    if (glm::length2(out) <= 1e-8f)
        return glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(out);
}

glm::vec3 genesisAnglesToDir(const glm::vec3 &anglesDegrees)
{
    const float yaw = glm::radians(anglesDegrees.y);
    const float pitch = glm::radians(anglesDegrees.x);
    glm::vec3 dir;
    dir.x = std::cos(pitch) * std::cos(yaw);
    dir.y = std::sin(pitch);
    dir.z = std::cos(pitch) * std::sin(yaw);
    if (glm::length2(dir) <= 1e-8f)
        return glm::vec3(0.0f, 0.0f, 1.0f);
    return glm::normalize(dir);
}
} // namespace mini_genesis
