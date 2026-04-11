#include "GridMapData.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>

namespace
{
struct RawGridFileHeader
{
    char notice[64];
    int32_t width;
    int32_t height;
    int32_t tileWidth;
    int32_t granularity;
    int32_t version;
};

struct RawBlendPoly
{
    int8_t tile1;
    int8_t tile2;
    int8_t rotate;
    int8_t fadeBits;
};

struct RawColorPoint
{
    uint32_t color;
    uint32_t specular;
};

glm::vec3 triangleNormal(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c)
{
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 n = glm::cross(ab, ac);
    const float len2 = glm::dot(n, n);
    if (len2 <= 1e-12f)
        return glm::vec3(0.0f, 1.0f, 0.0f);
    return n / std::sqrt(len2);
}

glm::vec3 normalizedOrUp(const glm::vec3 &value)
{
    const float len2 = glm::dot(value, value);
    if (len2 <= 1e-12f)
        return glm::vec3(0.0f, 1.0f, 0.0f);
    return value / std::sqrt(len2);
}

uint32_t hash2D(int x, int y, uint32_t seed)
{
    uint32_t h = seed;
    h ^= 0x9e3779b9u + static_cast<uint32_t>(x) + (h << 6) + (h >> 2);
    h ^= 0x85ebca6bu + static_cast<uint32_t>(y) + (h << 6) + (h >> 2);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

float hash01(int x, int y, uint32_t seed)
{
    const uint32_t h = hash2D(x, y, seed);
    return static_cast<float>(h & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

float smoothstep01(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

float valueNoise(float x, float y, uint32_t seed)
{
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);

    const float v00 = hash01(ix + 0, iy + 0, seed);
    const float v10 = hash01(ix + 1, iy + 0, seed);
    const float v01 = hash01(ix + 0, iy + 1, seed);
    const float v11 = hash01(ix + 1, iy + 1, seed);

    const float sx = smoothstep01(fx);
    const float sy = smoothstep01(fy);
    const float nx0 = v00 + (v10 - v00) * sx;
    const float nx1 = v01 + (v11 - v01) * sx;
    return nx0 + (nx1 - nx0) * sy;
}

float fractalNoise(float x, float y, uint32_t seed)
{
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float totalAmplitude = 0.0f;

    for (int octave = 0; octave < 5; ++octave)
    {
        sum += valueNoise(x * frequency, y * frequency, seed + static_cast<uint32_t>(octave) * 101u) * amplitude;
        totalAmplitude += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    if (totalAmplitude <= 1e-6f)
        return 0.0f;
    return sum / totalAmplitude;
}
}

bool GridMapData::create(int width, int height, int granularity, int tileWidth, std::string *error)
{
    sourcePath_.clear();
    notice_ = "MiniRender GridMapData";
    width_ = std::max(width, 0);
    height_ = std::max(height, 0);
    granularity_ = std::max(granularity, 1);
    version_ = 1;
    tileWidth_ = std::max(tileWidth, 1);
    sampleWidth_ = 0;
    sampleHeight_ = 0;
    heightSamples_.clear();
    tileIds_.clear();
    blends_.clear();
    shadePoints_.clear();
    colorPoints_.clear();
    vertexNormals_.clear();

    if (width_ <= 0 || height_ <= 0)
    {
        if (error)
            *error = "grid has invalid dimensions";
        return false;
    }

    sampleWidth_ = (width_ * granularity_) + 1;
    sampleHeight_ = (height_ * granularity_) + 1;

    const size_t sampleCount = static_cast<size_t>(sampleWidth_) * static_cast<size_t>(sampleHeight_);
    const size_t tileCount = static_cast<size_t>(width_) * static_cast<size_t>(height_);

    heightSamples_.assign(sampleCount, 0);
    tileIds_.assign(tileCount, 1);
    blends_.assign(tileCount, {});
    shadePoints_.assign(sampleCount, 255);
    colorPoints_.assign(sampleCount, GridMapColorPoint{});
    vertexNormals_.assign(sampleCount, glm::vec3(0.0f, 1.0f, 0.0f));

    return true;
}

bool GridMapData::load(const std::string &path, std::string *error)
{
    sourcePath_ = path;
    notice_.clear();
    width_ = 0;
    height_ = 0;
    granularity_ = 1;
    version_ = 0;
    tileWidth_ = 256;
    sampleWidth_ = 0;
    sampleHeight_ = 0;
    heightSamples_.clear();
    tileIds_.clear();
    blends_.clear();
    shadePoints_.clear();
    colorPoints_.clear();
    vertexNormals_.clear();

    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        if (error)
            *error = "failed to open grid file";
        return false;
    }

    RawGridFileHeader header = {};
    stream.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (!stream)
    {
        if (error)
            *error = "failed to read grid header";
        return false;
    }

    width_ = std::max<int>(header.width, 0);
    height_ = std::max<int>(header.height, 0);
    tileWidth_ = std::max<int>(header.tileWidth, 1);
    granularity_ = std::max<int>(header.granularity, 1);
    version_ = header.version;
    notice_ = std::string(header.notice, strnlen(header.notice, sizeof(header.notice)));

    if (width_ <= 0 || height_ <= 0)
    {
        if (error)
            *error = "grid has invalid dimensions";
        return false;
    }

    sampleWidth_ = (width_ * granularity_) + 1;
    sampleHeight_ = (height_ * granularity_) + 1;

    const size_t sampleCount = static_cast<size_t>(sampleWidth_) * static_cast<size_t>(sampleHeight_);
    const size_t tileCount = static_cast<size_t>(width_) * static_cast<size_t>(height_);

    heightSamples_.resize(sampleCount);
    tileIds_.resize(tileCount);
    blends_.resize(tileCount);
    shadePoints_.resize(sampleCount);
    colorPoints_.resize(sampleCount);

    stream.read(reinterpret_cast<char *>(heightSamples_.data()),
                static_cast<std::streamsize>(heightSamples_.size() * sizeof(int16_t)));
    if (!stream)
    {
        if (error)
            *error = "failed to read height samples";
        return false;
    }

    std::vector<int16_t> rawTiles(tileCount);
    stream.read(reinterpret_cast<char *>(rawTiles.data()),
                static_cast<std::streamsize>(rawTiles.size() * sizeof(int16_t)));
    if (!stream)
    {
        if (error)
            *error = "failed to read tile ids";
        return false;
    }
    tileIds_ = rawTiles;

    std::vector<RawBlendPoly> rawBlends(tileCount);
    stream.read(reinterpret_cast<char *>(rawBlends.data()),
                static_cast<std::streamsize>(rawBlends.size() * sizeof(RawBlendPoly)));
    if (!stream)
    {
        if (error)
            *error = "failed to read blend data";
        return false;
    }
    for (size_t i = 0; i < rawBlends.size(); ++i)
    {
        blends_[i].tile1 = rawBlends[i].tile1;
        blends_[i].tile2 = rawBlends[i].tile2;
        blends_[i].rotate = rawBlends[i].rotate;
        blends_[i].fadeBits = rawBlends[i].fadeBits;
    }

    stream.read(reinterpret_cast<char *>(shadePoints_.data()),
                static_cast<std::streamsize>(shadePoints_.size() * sizeof(uint8_t)));
    if (!stream)
    {
        if (error)
            *error = "failed to read shade points";
        return false;
    }

    std::vector<RawColorPoint> rawColors(sampleCount);
    stream.read(reinterpret_cast<char *>(rawColors.data()),
                static_cast<std::streamsize>(rawColors.size() * sizeof(RawColorPoint)));
    if (!stream)
    {
        if (error)
            *error = "failed to read color points";
        return false;
    }
    for (size_t i = 0; i < rawColors.size(); ++i)
    {
        colorPoints_[i].color = rawColors[i].color;
        colorPoints_[i].specular = rawColors[i].specular;
    }

    rebuildVertexNormals();
    return true;
}

bool GridMapData::generateProcedural(int width, int height, int granularity, int tileWidth,
                                     int seed, float maxHeight, float noiseScale,
                                     std::string *error)
{
    if (!create(width, height, granularity, tileWidth, error))
        return false;

    notice_ = "MiniRender Procedural Terrain";
    version_ = 2;
    const float clampedHeight = std::max(maxHeight, 1.0f);
    const float clampedScale = std::max(noiseScale, 0.0001f);
    const float centerX = static_cast<float>(sampleWidth_ - 1) * 0.5f;
    const float centerY = static_cast<float>(sampleHeight_ - 1) * 0.5f;
    const float invRadius = 1.0f / std::max(std::sqrt(centerX * centerX + centerY * centerY), 1.0f);

    for (int y = 0; y < sampleHeight_; ++y)
    {
        for (int x = 0; x < sampleWidth_; ++x)
        {
            const float nx = static_cast<float>(x) * clampedScale;
            const float ny = static_cast<float>(y) * clampedScale;
            const float low = fractalNoise(nx, ny, static_cast<uint32_t>(seed));
            const float detail = fractalNoise(nx * 3.5f, ny * 3.5f, static_cast<uint32_t>(seed) + 1337u);
            const float ridge = 1.0f - std::abs((detail * 2.0f) - 1.0f);
            const float dx = static_cast<float>(x) - centerX;
            const float dy = static_cast<float>(y) - centerY;
            const float falloff = 1.0f - std::clamp(std::sqrt(dx * dx + dy * dy) * invRadius, 0.0f, 1.0f);
            const float plateau = std::pow(falloff, 0.85f);
            const float height01 = std::clamp((low * 0.72f) + (ridge * 0.28f), 0.0f, 1.0f);
            const float shaped = std::pow(height01, 1.35f) * plateau;
            heightSamples_[sampleIndex(x, y)] = static_cast<int16_t>(std::lround(shaped * clampedHeight));

            GridMapColorPoint cp;
            const uint8_t shade = static_cast<uint8_t>(std::clamp(180.0f + shaped * 75.0f, 0.0f, 255.0f));
            shadePoints_[sampleIndex(x, y)] = shade;

            const uint8_t base = static_cast<uint8_t>(std::clamp(110.0f + shaped * 70.0f, 0.0f, 255.0f));
            const uint8_t green = static_cast<uint8_t>(std::clamp(96.0f + shaped * 58.0f, 0.0f, 255.0f));
            const uint8_t blue = static_cast<uint8_t>(std::clamp(70.0f + shaped * 24.0f, 0.0f, 255.0f));
            cp.color = 0xff000000u | (static_cast<uint32_t>(base) << 16) |
                       (static_cast<uint32_t>(green) << 8) |
                       static_cast<uint32_t>(blue);
            cp.specular = 0u;
            colorPoints_[sampleIndex(x, y)] = cp;
        }
    }

    for (int y = 0; y < height_; ++y)
    {
        for (int x = 0; x < width_; ++x)
            tileIds_[tileIndex(x, y)] = 1;
    }

    rebuildVertexNormals();
    return true;
}

float GridMapData::cellWorldSize() const
{
    return static_cast<float>(tileWidth_) / static_cast<float>(std::max(granularity_, 1));
}

float GridMapData::worldWidth() const
{
    return static_cast<float>(width_) * static_cast<float>(tileWidth_);
}

float GridMapData::worldDepth() const
{
    return static_cast<float>(height_) * static_cast<float>(tileWidth_);
}

int16_t GridMapData::heightSampleAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= sampleWidth_ || y >= sampleHeight_ || heightSamples_.empty())
        return 0;
    return heightSamples_[sampleIndex(x, y)];
}

int16_t GridMapData::heightSampleClamped(int x, int y) const
{
    if (heightSamples_.empty())
        return 0;
    x = std::clamp(x, 0, sampleWidth_ - 1);
    y = std::clamp(y, 0, sampleHeight_ - 1);
    return heightSamples_[sampleIndex(x, y)];
}

int16_t GridMapData::heightSampleWrapped(int x, int y) const
{
    if (heightSamples_.empty())
        return 0;

    const int wrapW = std::max(sampleWidth_ - 1, 1);
    const int wrapH = std::max(sampleHeight_ - 1, 1);
    x %= wrapW;
    y %= wrapH;
    if (x < 0) x += wrapW;
    if (y < 0) y += wrapH;
    return heightSamples_[sampleIndex(x, y)];
}

int16_t GridMapData::tileAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || tileIds_.empty())
        return -1;
    return tileIds_[tileIndex(x, y)];
}

GridMapBlend GridMapData::blendAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || blends_.empty())
        return {};
    return blends_[tileIndex(x, y)];
}

uint8_t GridMapData::shadeAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= sampleWidth_ || y >= sampleHeight_ || shadePoints_.empty())
        return 255;
    return shadePoints_[sampleIndex(x, y)];
}

GridMapColorPoint GridMapData::colorAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= sampleWidth_ || y >= sampleHeight_ || colorPoints_.empty())
        return {};
    return colorPoints_[sampleIndex(x, y)];
}

float GridMapData::heightAtWorldPoint(float worldX, float worldZ) const
{
    if (empty())
        return std::numeric_limits<float>::lowest();

    if (worldX < 0.0f || worldZ < 0.0f || worldX > worldWidth() || worldZ > worldDepth())
        return std::numeric_limits<float>::lowest();

    const float cellSize = cellWorldSize();
    const int maxCellX = sampleWidth_ - 2;
    const int maxCellY = sampleHeight_ - 2;

    int cellX = static_cast<int>(std::floor(worldX / cellSize));
    int cellY = static_cast<int>(std::floor(worldZ / cellSize));
    cellX = std::clamp(cellX, 0, maxCellX);
    cellY = std::clamp(cellY, 0, maxCellY);

    const float originX = static_cast<float>(cellX) * cellSize;
    const float originZ = static_cast<float>(cellY) * cellSize;
    const float fx = std::clamp((worldX - originX) / cellSize, 0.0f, 1.0f);
    const float fz = std::clamp((worldZ - originZ) / cellSize, 0.0f, 1.0f);

    const float p3 = static_cast<float>(heightSampleClamped(cellX, cellY));
    const float p2 = static_cast<float>(heightSampleClamped(cellX + 1, cellY));
    const float p1 = static_cast<float>(heightSampleClamped(cellX + 1, cellY + 1));
    const float p0 = static_cast<float>(heightSampleClamped(cellX, cellY + 1));

    if ((fx + fz) >= 1.0f)
        return p1 + (1.0f - fz) * (p2 - p1) + (1.0f - fx) * (p0 - p1);

    return p3 + fx * (p2 - p3) + fz * (p0 - p3);
}

glm::vec3 GridMapData::normalAtWorldPoint(float worldX, float worldZ) const
{
    if (empty() || vertexNormals_.empty())
        return glm::vec3(0.0f, 1.0f, 0.0f);

    if (worldX < 0.0f || worldZ < 0.0f || worldX > worldWidth() || worldZ > worldDepth())
        return glm::vec3(0.0f, 1.0f, 0.0f);

    const float cellSize = cellWorldSize();
    const int maxCellX = sampleWidth_ - 2;
    const int maxCellY = sampleHeight_ - 2;

    int cellX = static_cast<int>(std::floor(worldX / cellSize));
    int cellY = static_cast<int>(std::floor(worldZ / cellSize));
    cellX = std::clamp(cellX, 0, maxCellX);
    cellY = std::clamp(cellY, 0, maxCellY);

    const float originX = static_cast<float>(cellX) * cellSize;
    const float originZ = static_cast<float>(cellY) * cellSize;
    const float fx = std::clamp((worldX - originX) / cellSize, 0.0f, 1.0f);
    const float fz = std::clamp((worldZ - originZ) / cellSize, 0.0f, 1.0f);

    const glm::vec3 p3 = vertexNormals_[sampleIndex(cellX, cellY)];
    const glm::vec3 p2 = vertexNormals_[sampleIndex(cellX + 1, cellY)];
    const glm::vec3 p1 = vertexNormals_[sampleIndex(cellX + 1, cellY + 1)];
    const glm::vec3 p0 = vertexNormals_[sampleIndex(cellX, cellY + 1)];

    if ((fx + fz) >= 1.0f)
    {
        const float w1 = fx + fz - 1.0f;
        const float w2 = 1.0f - fz;
        const float w0 = 1.0f - fx;
        return normalizedOrUp((p1 * w1) + (p2 * w2) + (p0 * w0));
    }

    const float w3 = 1.0f - fx - fz;
    return normalizedOrUp((p3 * w3) + (p2 * fx) + (p0 * fz));
}

BoundingBox GridMapData::computeBounds() const
{
    BoundingBox bounds;
    if (empty())
        return bounds;

    for (int y = 0; y < sampleHeight_; ++y)
    {
        for (int x = 0; x < sampleWidth_; ++x)
            bounds.expand(samplePosition(x, y));
    }
    return bounds;
}

int GridMapData::sampleIndex(int x, int y) const
{
    return x + (y * sampleWidth_);
}

int GridMapData::tileIndex(int x, int y) const
{
    return x + (y * width_);
}

glm::vec3 GridMapData::samplePosition(int x, int y) const
{
    const float cellSize = cellWorldSize();
    return glm::vec3(static_cast<float>(x) * cellSize,
                     static_cast<float>(heightSampleClamped(x, y)),
                     static_cast<float>(y) * cellSize);
}

void GridMapData::rebuildVertexNormals()
{
    vertexNormals_.assign(static_cast<size_t>(sampleWidth_) * static_cast<size_t>(sampleHeight_),
                          glm::vec3(0.0f));

    if (empty())
        return;

    for (int y = 0; y < sampleHeight_ - 1; ++y)
    {
        for (int x = 0; x < sampleWidth_ - 1; ++x)
        {
            const glm::vec3 p0 = samplePosition(x, y + 1);
            const glm::vec3 p1 = samplePosition(x + 1, y + 1);
            const glm::vec3 p2 = samplePosition(x + 1, y);
            const glm::vec3 p3 = samplePosition(x, y);

            const glm::vec3 triA = triangleNormal(p2, p0, p1);
            const glm::vec3 triB = triangleNormal(p3, p0, p2);

            vertexNormals_[sampleIndex(x + 1, y)] += triA;
            vertexNormals_[sampleIndex(x, y + 1)] += triA;
            vertexNormals_[sampleIndex(x + 1, y + 1)] += triA;

            vertexNormals_[sampleIndex(x, y)] += triB;
            vertexNormals_[sampleIndex(x, y + 1)] += triB;
            vertexNormals_[sampleIndex(x + 1, y)] += triB;
        }
    }

    for (glm::vec3 &normal : vertexNormals_)
        normal = normalizedOrUp(normal);
}
