#include "GridMapMeshBuilder.hpp"

#include "Manager.hpp"

#include <algorithm>
#include <cmath>
#include <sys/stat.h>

namespace
{
bool pathExists(const std::string &path)
{
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

std::string joinPath(const std::string &base, const std::string &path)
{
    if (base.empty())
        return path;
    if (!base.empty() && base.back() == '/')
        return base + path;
    return base + "/" + path;
}

uint32_t gridVertexIndex(int x, int y, int rowStride)
{
    return static_cast<uint32_t>(x + (y * rowStride));
}

void clearMesh(Mesh &mesh)
{
    mesh.buffer.free();
    mesh.release_materials();
    mesh.buffer.vertices.clear();
    mesh.buffer.indices.clear();
    mesh.surfaces.clear();
    mesh.aabb = {};
}

Texture *loadChunkTexture(const std::string &meshName,
                          const std::string &textureDirectory,
                          const std::string &textureBaseName,
                          const std::string &fallbackTextureName,
                          int chunkX,
                          int chunkY)
{
    std::string resolvedPath;
    if (!textureDirectory.empty())
    {
        const std::string candidate = joinPath(textureDirectory,
                                               textureBaseName + "_" +
                                               std::to_string(chunkX) + "_x_" +
                                               std::to_string(chunkY) + ".bmp");
        if (pathExists(candidate))
            resolvedPath = candidate;
        else
        {
            const std::string fallback = joinPath(textureDirectory, fallbackTextureName);
            if (pathExists(fallback))
                resolvedPath = fallback;
        }
    }

    if (resolvedPath.empty())
        return TextureManager::instance().getWhite();

    return TextureManager::instance().load(meshName + "::chunktex_" +
                                           std::to_string(chunkX) + "_" +
                                           std::to_string(chunkY),
                                           resolvedPath);
}
}

bool GridMapMeshBuilder::build(const std::string &meshName,
                               const GridMapData &data,
                               const GridMapMeshBuildOptions &options,
                               Mesh &outMesh,
                               std::string *error)
{
    clearMesh(outMesh);
    outMesh.name = meshName;

    if (data.empty())
    {
        if (error)
            *error = "grid map is empty";
        return false;
    }

    const int chunkSizeTiles = std::max(options.chunkSizeTiles, 1);
    const int chunkWidth = (data.width() + chunkSizeTiles - 1) / chunkSizeTiles;
    const int chunkHeight = (data.height() + chunkSizeTiles - 1) / chunkSizeTiles;
    const int granularity = std::max(data.granularity(), 1);
    const float cellSize = data.cellWorldSize();
    const float heightScale = std::max(options.heightScale, 0.001f);

    if (chunkWidth <= 0 || chunkHeight <= 0)
    {
        if (error)
            *error = "grid map produced no chunks";
        return false;
    }

    Texture *white = TextureManager::instance().getWhite();

    for (int cy = 0; cy < chunkHeight; ++cy)
    {
        for (int cx = 0; cx < chunkWidth; ++cx)
        {
            const int tileX0 = cx * chunkSizeTiles;
            const int tileY0 = cy * chunkSizeTiles;
            const int tileX1 = std::min(tileX0 + chunkSizeTiles, data.width());
            const int tileY1 = std::min(tileY0 + chunkSizeTiles, data.height());
            if (tileX0 >= tileX1 || tileY0 >= tileY1)
                continue;

            const int sampleX0 = tileX0 * granularity;
            const int sampleY0 = tileY0 * granularity;
            const int sampleX1 = tileX1 * granularity;
            const int sampleY1 = tileY1 * granularity;
            const int sampleCols = (sampleX1 - sampleX0) + 1;
            const int sampleRows = (sampleY1 - sampleY0) + 1;
            if (sampleCols < 2 || sampleRows < 2)
                continue;

            const uint32_t vertexBase = static_cast<uint32_t>(outMesh.buffer.vertices.size());
            const uint32_t indexStart = static_cast<uint32_t>(outMesh.buffer.indices.size());

            outMesh.buffer.vertices.reserve(outMesh.buffer.vertices.size() +
                                            static_cast<size_t>(sampleCols) * static_cast<size_t>(sampleRows));

            for (int sy = sampleY0; sy <= sampleY1; ++sy)
            {
                for (int sx = sampleX0; sx <= sampleX1; ++sx)
                {
                    Vertex vertex;
                    vertex.position = glm::vec3(static_cast<float>(sx) * cellSize,
                                                static_cast<float>(data.heightSampleClamped(sx, sy)) * heightScale,
                                                static_cast<float>(sy) * cellSize);
                    vertex.normal = data.normalAtWorldPoint(vertex.position.x, vertex.position.z);
                    vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                    vertex.uv = glm::vec2(static_cast<float>(sx - sampleX0) / static_cast<float>(sampleCols - 1),
                                          static_cast<float>(sy - sampleY0) / static_cast<float>(sampleRows - 1));
                    outMesh.buffer.vertices.push_back(vertex);
                }
            }

            for (int gy = 0; gy < sampleRows - 1; ++gy)
            {
                for (int gx = 0; gx < sampleCols - 1; ++gx)
                {
                    const uint32_t p3 = vertexBase + gridVertexIndex(gx + 0, gy + 0, sampleCols);
                    const uint32_t p2 = vertexBase + gridVertexIndex(gx + 1, gy + 0, sampleCols);
                    const uint32_t p1 = vertexBase + gridVertexIndex(gx + 1, gy + 1, sampleCols);
                    const uint32_t p0 = vertexBase + gridVertexIndex(gx + 0, gy + 1, sampleCols);

                    outMesh.buffer.indices.push_back(p2);
                    outMesh.buffer.indices.push_back(p0);
                    outMesh.buffer.indices.push_back(p1);

                    outMesh.buffer.indices.push_back(p3);
                    outMesh.buffer.indices.push_back(p0);
                    outMesh.buffer.indices.push_back(p2);
                }
            }

            Material *material = new Material();
            material->name = meshName + "::chunk_" + std::to_string(cx) + "_" + std::to_string(cy);
            material->setTexture("u_albedo",
                                 loadChunkTexture(meshName,
                                                  options.textureDirectory,
                                                  options.textureBaseName,
                                                  options.fallbackTextureName,
                                                  cx, cy));
            material->setVec4("u_color", glm::vec4(1.0f));
            if (!material->getTexture("u_albedo"))
                material->setTexture("u_albedo", white);

            const int materialIndex = outMesh.add_material(material);
            outMesh.add_surface(indexStart,
                                static_cast<uint32_t>(outMesh.buffer.indices.size()) - indexStart,
                                materialIndex);
        }
    }

    if (outMesh.buffer.vertices.empty() || outMesh.buffer.indices.empty())
    {
        if (error)
            *error = "grid map mesh builder produced no geometry";
        return false;
    }

    outMesh.upload();
    outMesh.compute_surface_aabbs();
    return true;
}
