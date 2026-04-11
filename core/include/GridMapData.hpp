#pragma once

#include "Math.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct GridMapBlend
{
    int8_t tile1    = -1;
    int8_t tile2    = -1;
    int8_t rotate   = 0;
    int8_t fadeBits = 0;
};

struct GridMapColorPoint
{
    uint32_t color    = 0xffffffffu;
    uint32_t specular = 0u;
};

class GridMapData
{
public:
    bool create(int width, int height, int granularity, int tileWidth, std::string *error = nullptr);
    bool load(const std::string &path, std::string *error = nullptr);
    bool generateProcedural(int width, int height, int granularity, int tileWidth,
                            int seed, float maxHeight, float noiseScale,
                            std::string *error = nullptr);

    bool empty() const { return width_ <= 0 || height_ <= 0 || heightSamples_.empty(); }

    const std::string &sourcePath() const { return sourcePath_; }
    const std::string &notice() const { return notice_; }

    int width() const { return width_; }
    int height() const { return height_; }
    int granularity() const { return granularity_; }
    int version() const { return version_; }
    int tileWidth() const { return tileWidth_; }
    int sampleWidth() const { return sampleWidth_; }
    int sampleHeight() const { return sampleHeight_; }

    float cellWorldSize() const;
    float worldWidth() const;
    float worldDepth() const;

    int16_t heightSampleAt(int x, int y) const;
    int16_t heightSampleClamped(int x, int y) const;
    int16_t heightSampleWrapped(int x, int y) const;
    int16_t tileAt(int x, int y) const;
    GridMapBlend blendAt(int x, int y) const;
    uint8_t shadeAt(int x, int y) const;
    GridMapColorPoint colorAt(int x, int y) const;

    float heightAtWorldPoint(float worldX, float worldZ) const;
    glm::vec3 normalAtWorldPoint(float worldX, float worldZ) const;
    BoundingBox computeBounds() const;

    const std::vector<int16_t> &heightSamples() const { return heightSamples_; }
    const std::vector<int16_t> &tileIds() const { return tileIds_; }
    const std::vector<GridMapBlend> &blends() const { return blends_; }
    const std::vector<uint8_t> &shadePoints() const { return shadePoints_; }
    const std::vector<GridMapColorPoint> &colorPoints() const { return colorPoints_; }
    const std::vector<glm::vec3> &vertexNormals() const { return vertexNormals_; }

private:
    std::string sourcePath_;
    std::string notice_;
    int width_ = 0;
    int height_ = 0;
    int granularity_ = 1;
    int version_ = 0;
    int tileWidth_ = 256;
    int sampleWidth_ = 0;
    int sampleHeight_ = 0;

    std::vector<int16_t> heightSamples_;
    std::vector<int16_t> tileIds_;
    std::vector<GridMapBlend> blends_;
    std::vector<uint8_t> shadePoints_;
    std::vector<GridMapColorPoint> colorPoints_;
    std::vector<glm::vec3> vertexNormals_;

    int sampleIndex(int x, int y) const;
    int tileIndex(int x, int y) const;
    glm::vec3 samplePosition(int x, int y) const;
    void rebuildVertexNormals();
};
