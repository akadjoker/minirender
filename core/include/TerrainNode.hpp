#pragma once

#include "Node.hpp"
#include "Pixmap.hpp"
#include <cstdint>
#include <string>
#include <vector>

class Camera;

struct TerrainRaycastResult
{
    bool hit = false;
    glm::vec3 position = {};
    glm::vec3 normal = {0.f, 1.f, 0.f};
    float distance = 0.f;
};

enum class TerrainPatchSize : int
{
    Patch9 = 9,
    Patch17 = 17,
    Patch33 = 33,
    Patch65 = 65,
    Patch129 = 129,
};

class TerrainLodNode : public RenderableNode
{
public:
    TerrainLodNode(const std::string &name = "TerrainLod",
                   int maxLOD = 4,
                   TerrainPatchSize patchSz = TerrainPatchSize::Patch17,
                   float detailScale = 8.f);
    ~TerrainLodNode() override;

    bool loadFromHeightmap(const std::string &path,
                           float heightScale = 1.f,
                           int smoothFactor = 0);

    void setScale(const glm::vec3 &s) { Node3D::setScale(s); applyTransformation(); }
    void setScale(float s) { setScale(glm::vec3(s)); }
    void setPosition(const glm::vec3 &p) { Node3D::setPosition(p); applyTransformation(); }

    void setLODDistance(int lod, float dist);
    void setTextureScale(float s);
    void setCameraMovementDelta(float d) { m_camMoveDelta = d; }
    void setCameraRotationDelta(float dot) { m_camRotDelta = dot; }

    float getHeightAt(float worldX, float worldZ) const;
    glm::vec3 getNormalAt(float worldX, float worldZ) const;
    TerrainRaycastResult raycast(const Ray &ray, float maxDist = 2000.f) const;

    BoundingBox getAABB() const { return m_aabb; }
    Material *getMaterial() const { return m_material; }
    void setMaterial(Material *m) { m_material = m; }

    bool prepareForRender(const Camera *camera, const Frustum &frustum);
    TerrainBuffer *getRenderBuffer() const { return m_renderBuffer; }
    uint32_t getVisibleIndexCount() const { return m_indicesToRender; }

private:
    struct Patch
    {
        BoundingBox aabb = {};
        glm::vec3 center = {};
        int currentLOD = 0;
        Patch *top = nullptr;
        Patch *bottom = nullptr;
        Patch *left = nullptr;
        Patch *right = nullptr;
    };

    float *m_heightData = nullptr;
    int m_size = 0;
    int m_patchSize = 17;
    int m_calcPatchSize = 16;
    int m_patchCount = 0;
    int m_maxLOD = 4;
    float m_heightScale = 1.f;
    float m_texScale = 1.f;
    float m_detailScale = 8.f;

    TerrainBuffer *m_renderBuffer = nullptr;
    uint32_t m_indicesToRender = 0;
    std::vector<TerrainVertex> m_sourceVerts;
    std::vector<Patch> m_patches;
    std::vector<float> m_lodDist;
    BoundingBox m_aabb;
    Material *m_material = nullptr;

    glm::vec3 m_oldCamPos = {};
    glm::vec3 m_oldCamForward = {0.f, 0.f, -1.f};
    float m_camMoveDelta = 10.f;
    float m_camRotDelta = 0.9998f;
    bool m_forceRecalc = true;

    void calculateNormals();
    void applyTransformation();
    void calculateDistanceThresholds();
    void createPatches();
    void calculatePatchData();

    bool preRenderLODCalculations(const glm::vec3 &camPos,
                                  const glm::vec3 &camForward,
                                  const Frustum &frustum);
    void preRenderIndicesCalculations();
    uint32_t getIndex(int patchX, int patchZ, int patchIdx, uint32_t vX, uint32_t vZ) const;
    float sampleHeight(int x, int z) const;
    glm::vec3 calcNormal(int x, int z) const;
    void smooth(int iterations);
};
