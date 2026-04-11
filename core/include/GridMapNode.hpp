#pragma once

#include "GridMapData.hpp"
#include "GridMapMeshBuilder.hpp"
#include "Node.hpp"

#include <string>
#include <vector>

class GridMapNode : public RenderableNode
{
public:
    explicit GridMapNode(const std::string &name = "GridMap");
    ~GridMapNode() override;

    bool load(const GridMapData &data,
              const GridMapMeshBuildOptions &options,
              std::string *error = nullptr);

    void render(Shader *shader, Camera *camera) override;

    BoundingBox getAABB() const { return bounds_; }
    int chunkCountX() const { return chunkCountX_; }
    int chunkCountY() const { return chunkCountY_; }
    int chunkCount() const { return static_cast<int>(chunks_.size()); }
    int visibleChunkCount() const { return visibleChunkCount_; }
    int selectedChunkCount() const { return selectedChunkCount_; }

private:
    struct Chunk
    {
        int x = 0;
        int y = 0;
        BoundingBox aabb = {};
        MeshBuffer buffer;
        Material *material = nullptr;
    };

    struct ChunkSelection
    {
        int x = 0;
        int y = 0;
    };

    GridMapData data_;
    GridMapMeshBuildOptions options_;
    std::vector<Chunk *> chunks_;
    std::vector<Material *> materials_;
    BoundingBox bounds_;
    int chunkCountX_ = 0;
    int chunkCountY_ = 0;
    int visibleChunkCount_ = 0;
    int selectedChunkCount_ = 0;

    void clearChunks();
    bool buildChunks(std::string *error);
    void addChunkSelection(int x, int y, std::vector<ChunkSelection> &out) const;
    int chunkIndex(int x, int y) const;
};
