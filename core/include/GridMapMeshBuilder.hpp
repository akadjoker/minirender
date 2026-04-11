#pragma once

#include "GridMapData.hpp"
#include "Mesh.hpp"

#include <string>

struct GridMapMeshBuildOptions
{
    int chunkSizeTiles = 32;
    float heightScale = 1.0f;
    int renderRadiusChunks = 2;
    std::string textureDirectory;
    std::string textureBaseName = "desert";
    std::string fallbackTextureName = "teximage.bmp";
};

class GridMapMeshBuilder
{
public:
    static bool build(const std::string &meshName,
                      const GridMapData &data,
                      const GridMapMeshBuildOptions &options,
                      Mesh &outMesh,
                      std::string *error = nullptr);
};
