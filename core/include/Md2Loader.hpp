#pragma once

#include "Mesh.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

class Shader;

struct Md2RuntimeData
{
    int numFrames = 0;
    int numBaseVertices = 0;

    std::vector<glm::vec3> framePositions;       // numFrames * numBaseVertices
    std::vector<uint32_t> cornerToBaseVertex;    // mesh corner -> base MD2 vertex

    BoundingBox aabb = {};

    bool valid() const
    {
        return numFrames > 0 && numBaseVertices > 0 &&
               framePositions.size() == static_cast<size_t>(numFrames) * static_cast<size_t>(numBaseVertices) &&
               !cornerToBaseVertex.empty();
    }

    glm::vec3 framePos(int frame, uint32_t baseVertex) const;
};

class Md2Loader
{
public:
    struct Options
    {
        std::string meshName = "md2_mesh";
        std::string materialName = "md2_material";
        std::string texturePath;
        Shader *shader = nullptr;
        bool disableBackfaceCulling = true;
    };

    bool load(const std::string &modelPath,
              const Options &options,
              Mesh *outMesh,
              Md2RuntimeData *outRuntime) const;

    Mesh *load(const std::string &modelPath,
               const Options &options,
               Md2RuntimeData *outRuntime) const;
};
