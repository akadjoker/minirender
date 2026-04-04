#pragma once

#include "Mesh.hpp"
#include "VertexAnimation.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

class Shader;

struct Md3AnimCfgClip
{
    std::string name;
    int first = 0;
    int last = 0;
    float fps = 10.0f;
    bool loop = true;
};

struct Md3SurfaceRuntime
{
    std::string name;
    std::string shaderName;

    int numFrames = 0;
    int numVerts = 0;
    int numTris = 0;

    std::vector<uint32_t> indices;
    std::vector<glm::vec2> texcoords;
    std::vector<glm::vec3> framePositions;
    std::vector<glm::vec3> frameNormals;

    uint32_t vertexStart = 0;
    uint32_t indexStart = 0;
    uint32_t indexCount = 0;

    BoundingBox aabb = {};

    bool ready() const
    {
        return numFrames > 0 && numVerts > 0 && numTris > 0 &&
               framePositions.size() == static_cast<size_t>(numFrames) * static_cast<size_t>(numVerts) &&
               frameNormals.size() == static_cast<size_t>(numFrames) * static_cast<size_t>(numVerts) &&
               texcoords.size() == static_cast<size_t>(numVerts) &&
               indices.size() == static_cast<size_t>(numTris) * 3u;
    }
};

struct Md3PartRuntime
{
    std::string id;
    int frameCount = 0;

    std::vector<Md3SurfaceRuntime> surfaces;
    std::vector<VertexTagTrack> tagTracks;

    BoundingBox aabb = {};

    bool valid() const { return frameCount > 0 && !surfaces.empty(); }
};

class Md3Loader
{
public:
    struct Options
    {
        std::string partId = "md3_part";
        std::string meshName = "md3_mesh";
        std::string materialPrefix = "md3_mat";
        std::string skinPath;
        Shader *shader = nullptr;
        bool disableBackfaceCulling = true;
    };

    bool load(const std::string &modelPath,
              const Options &options,
              Mesh *outMesh,
              Md3PartRuntime *outRuntime,
              VertexAnimController *outController = nullptr) const;

    Mesh *load(const std::string &modelPath,
               const Options &options,
               Md3PartRuntime *outRuntime,
               VertexAnimController *outController = nullptr) const;

    static bool buildAsset(Mesh *templateMesh,
                           const Md3PartRuntime &runtime,
                           VertexAnimAsset *outAsset);

    static void applySample(Mesh *mesh,
                            const Md3PartRuntime &runtime,
                            const VertexAnimSample &sample);

    static bool parseAnimationCfg(const std::string &cfgPath,
                                  std::vector<Md3AnimCfgClip> &outClips);

    static void applyClipsFromCfg(const std::vector<Md3AnimCfgClip> &clips,
                                  const std::string &requiredPrefix,
                                  int frameCount,
                                  VertexAnimController *outController,
                                  std::vector<std::string> *outClipNames = nullptr,
                                  bool forceLoop = true);
};
