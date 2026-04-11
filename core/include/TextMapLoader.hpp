#pragma once

#include "Math.hpp"

#include <string>
#include <vector>

class CollisionSystem;
class Mesh;
struct TextMapDocument;

struct TextMapLoadOptions
{
    std::string textureDirectory;
    bool remapZUpToYUp = true;
    float texturePixelsPerUnit = 128.0f;
    float planeEpsilon = 0.05f;
    float vertexEpsilon = 0.05f;
};

struct TextMapLoadResult
{
    Mesh *mesh = nullptr;
    BoundingBox bounds = {};
    std::vector<Triangle> collisionTriangles;

    void applyToCollision(CollisionSystem &system, bool clearFirst = true) const;
};

class TextMapLoader
{
public:
    static bool load(const std::string &meshName,
                     const std::string &mapPath,
                     const TextMapLoadOptions &options,
                     Mesh &outMesh,
                     TextMapLoadResult *outResult = nullptr,
                     std::string *error = nullptr);

    static bool build(const std::string &meshName,
                      const TextMapDocument &document,
                      const TextMapLoadOptions &options,
                      Mesh &outMesh,
                      TextMapLoadResult *outResult = nullptr,
                      std::string *error = nullptr);
};
