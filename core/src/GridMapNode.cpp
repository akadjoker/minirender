#include "GridMapNode.hpp"

#include "Camera.hpp"
#include "Manager.hpp"

#include <algorithm>
#include <cmath>
#include <sys/stat.h>
#include <unordered_map>

namespace
{
struct ChunkTextureRef
{
    Texture *texture = nullptr;
    std::string key;
};

uint32_t gridVertexIndex(int x, int y, int rowStride)
{
    return static_cast<uint32_t>(x + (y * rowStride));
}

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

ChunkTextureRef loadChunkTexture(const GridMapMeshBuildOptions &options,
                                 int chunkX,
                                 int chunkY)
{
    const std::string textureDirectory = options.textureDirectory;
    if (textureDirectory.empty())
        return {TextureManager::instance().getWhite(), "__white__"};

    const std::string chunkPath = joinPath(textureDirectory,
                                           options.textureBaseName + "_" +
                                           std::to_string(chunkX) + "_x_" +
                                           std::to_string(chunkY) + ".bmp");
    if (pathExists(chunkPath))
    {
        Texture *texture = TextureManager::instance().load("gridmap_texture::" + chunkPath, chunkPath);
        if (texture)
            return {texture, chunkPath};
    }

    const std::string fallbackPath = joinPath(textureDirectory, options.fallbackTextureName);
    if (pathExists(fallbackPath))
    {
        Texture *texture = TextureManager::instance().load("gridmap_texture::" + fallbackPath, fallbackPath);
        if (texture)
            return {texture, fallbackPath};
    }

    return {TextureManager::instance().getWhite(), "__white__"};
}
}

GridMapNode::GridMapNode(const std::string &name)
{
    this->name = name;
    renderType = RenderType::Terrain;
}

GridMapNode::~GridMapNode()
{
    clearChunks();
}

bool GridMapNode::load(const GridMapData &data,
                       const GridMapMeshBuildOptions &options,
                       std::string *error)
{
    data_ = data;
    options_ = options;
    return buildChunks(error);
}

void GridMapNode::render(Shader *shader, Camera *camera)
{
    visibleChunkCount_ = 0;
    selectedChunkCount_ = 0;

    if (!shader || !camera || !visible || chunks_.empty())
        return;

    const glm::mat4 model = worldMatrix();
    shader->setMat4("u_model", model);
    shader->setMat3("u_normalMatrix", glm::mat3(glm::transpose(glm::inverse(model))));

    const glm::vec3 localCamera = worldToLocalPoint(camera->worldPosition());
    const float chunkWorldSize = static_cast<float>(std::max(options_.chunkSizeTiles, 1) * data_.tileWidth());
    const int cx = static_cast<int>(std::floor(localCamera.x / chunkWorldSize));
    const int cy = static_cast<int>(std::floor(localCamera.z / chunkWorldSize));

    const int renderRadius = std::max(options_.renderRadiusChunks, 0);
    std::vector<ChunkSelection> selected;
    selected.reserve(static_cast<size_t>((renderRadius * 2 + 1) * (renderRadius * 2 + 1)));

    for (int radius = 0; radius <= renderRadius; ++radius)
    {
        for (int y = cy - radius; y <= cy + radius; ++y)
        {
            for (int x = cx - radius; x <= cx + radius; ++x)
            {
                const bool isBorder = (radius == 0) ||
                                      (x == cx - radius) || (x == cx + radius) ||
                                      (y == cy - radius) || (y == cy + radius);
                if (!isBorder)
                    continue;
                addChunkSelection(x, y, selected);
            }
        }
    }

    selectedChunkCount_ = static_cast<int>(selected.size());

    for (const ChunkSelection &entry : selected)
    {
        const int index = chunkIndex(entry.x, entry.y);
        if (index < 0 || index >= static_cast<int>(chunks_.size()))
            continue;

        Chunk *chunk = chunks_[index];
        if (!chunk)
            continue;

        const BoundingBox worldBounds = chunk->aabb.transformed(model);
        if (worldBounds.is_valid() && !camera->frustum.contains(worldBounds))
            continue;

        if (chunk->material)
        {
            chunk->material->applyStates();
            chunk->material->applyUniformsTo(shader);
            chunk->material->bindTexturesTo(shader);
        }

        chunk->buffer.draw();
        ++visibleChunkCount_;
    }
}

void GridMapNode::clearChunks()
{
    for (Chunk *chunk : chunks_)
    {
        if (!chunk)
            continue;
        chunk->buffer.free();
        chunk->material = nullptr;
        delete chunk;
    }
    chunks_.clear();

    for (Material *material : materials_)
        delete material;
    materials_.clear();

    bounds_ = {};
    chunkCountX_ = 0;
    chunkCountY_ = 0;
    visibleChunkCount_ = 0;
    selectedChunkCount_ = 0;
}

bool GridMapNode::buildChunks(std::string *error)
{
    clearChunks();

    if (data_.empty())
    {
        if (error)
            *error = "grid map is empty";
        return false;
    }

    const int chunkSizeTiles = std::max(options_.chunkSizeTiles, 1);
    const int granularity = std::max(data_.granularity(), 1);
    const float cellSize = data_.cellWorldSize();
    const float heightScale = std::max(options_.heightScale, 0.001f);
    std::unordered_map<std::string, Material *> materialPool;

    chunkCountX_ = (data_.width() + chunkSizeTiles - 1) / chunkSizeTiles;
    chunkCountY_ = (data_.height() + chunkSizeTiles - 1) / chunkSizeTiles;
    chunks_.reserve(static_cast<size_t>(chunkCountX_ * chunkCountY_));

    for (int cy = 0; cy < chunkCountY_; ++cy)
    {
        for (int cx = 0; cx < chunkCountX_; ++cx)
        {
            const int tileX0 = cx * chunkSizeTiles;
            const int tileY0 = cy * chunkSizeTiles;
            const int tileX1 = std::min(tileX0 + chunkSizeTiles, data_.width());
            const int tileY1 = std::min(tileY0 + chunkSizeTiles, data_.height());
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

            Chunk *chunk = new Chunk();
            chunk->x = cx;
            chunk->y = cy;
            chunk->buffer.vertices.reserve(static_cast<size_t>(sampleCols) * static_cast<size_t>(sampleRows));
            chunk->buffer.indices.reserve(static_cast<size_t>(sampleCols - 1) *
                                          static_cast<size_t>(sampleRows - 1) * 6u);

            for (int sy = sampleY0; sy <= sampleY1; ++sy)
            {
                for (int sx = sampleX0; sx <= sampleX1; ++sx)
                {
                    Vertex vertex;
                    vertex.position = glm::vec3(static_cast<float>(sx) * cellSize,
                                                static_cast<float>(data_.heightSampleClamped(sx, sy)) * heightScale,
                                                static_cast<float>(sy) * cellSize);
                    vertex.normal = data_.normalAtWorldPoint(vertex.position.x, vertex.position.z);
                    vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
                    vertex.uv = glm::vec2(static_cast<float>(sx - sampleX0) / static_cast<float>(sampleCols - 1),
                                          static_cast<float>(sy - sampleY0) / static_cast<float>(sampleRows - 1));
                    chunk->buffer.vertices.push_back(vertex);
                    chunk->aabb.expand(vertex.position);
                    bounds_.expand(vertex.position);
                }
            }

            for (int gy = 0; gy < sampleRows - 1; ++gy)
            {
                for (int gx = 0; gx < sampleCols - 1; ++gx)
                {
                    const uint32_t p3 = gridVertexIndex(gx + 0, gy + 0, sampleCols);
                    const uint32_t p2 = gridVertexIndex(gx + 1, gy + 0, sampleCols);
                    const uint32_t p1 = gridVertexIndex(gx + 1, gy + 1, sampleCols);
                    const uint32_t p0 = gridVertexIndex(gx + 0, gy + 1, sampleCols);

                    chunk->buffer.indices.push_back(p2);
                    chunk->buffer.indices.push_back(p0);
                    chunk->buffer.indices.push_back(p1);

                    chunk->buffer.indices.push_back(p3);
                    chunk->buffer.indices.push_back(p0);
                    chunk->buffer.indices.push_back(p2);
                }
            }

            const ChunkTextureRef textureRef = loadChunkTexture(options_, cx, cy);
            auto foundMaterial = materialPool.find(textureRef.key);
            if (foundMaterial != materialPool.end())
            {
                chunk->material = foundMaterial->second;
            }
            else
            {
                Material *material = new Material();
                material->name = name + "::mat_" + std::to_string(static_cast<int>(materials_.size()));
                material->setTexture("u_albedo", textureRef.texture);
                material->setVec4("u_color", glm::vec4(1.0f));
                materials_.push_back(material);
                materialPool.emplace(textureRef.key, material);
                chunk->material = material;
            }

            chunk->buffer.upload();
            chunks_.push_back(chunk);
        }
    }

    if (chunks_.empty())
    {
        if (error)
            *error = "grid map node produced no chunks";
        return false;
    }

    return true;
}

void GridMapNode::addChunkSelection(int x, int y, std::vector<ChunkSelection> &out) const
{
    if (x < 0 || y < 0 || x >= chunkCountX_ || y >= chunkCountY_)
        return;

    for (const ChunkSelection &entry : out)
    {
        if (entry.x == x && entry.y == y)
            return;
    }

    out.push_back({x, y});
}

int GridMapNode::chunkIndex(int x, int y) const
{
    if (x < 0 || y < 0 || x >= chunkCountX_ || y >= chunkCountY_)
        return -1;
    return x + (y * chunkCountX_);
}
