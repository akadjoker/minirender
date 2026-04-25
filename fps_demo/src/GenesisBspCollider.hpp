#pragma once

#include <cstdint>
#include <vector>

#include "Math.hpp"

struct GenesisBspPlane
{
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    float dist = 0.0f;
};

struct GenesisBspBNode
{
    int32_t children[2] = {0, 0};
    int32_t planeNum = -1;
};

struct GenesisBspLeafSide
{
    int32_t planeNum = -1;
    int32_t planeSide = 0;
};

struct GenesisTraceHit
{
    bool hit = false;
    glm::vec3 point = glm::vec3(0.0f);
    glm::vec3 planeNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    int32_t planeIndex = -1;
    int32_t side = 0;
};

struct GenesisTraceResult
{
    bool hit = false;
    bool startSolid = false;
    bool allSolid = false;
    float fraction = 1.0f;
    glm::vec3 start = glm::vec3(0.0f);
    glm::vec3 end = glm::vec3(0.0f);
    glm::vec3 endPos = glm::vec3(0.0f);
    glm::vec3 planeNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    int32_t planeIndex = -1;
    int32_t side = 0;
    int nodeVisits = 0;
};

class GenesisBspCollider
{
public:
    void clear();

    void setTree(std::vector<GenesisBspBNode> bnodes,
                 std::vector<GenesisBspPlane> planes,
                 int32_t rootNode = 0);
    void setNodeTree(std::vector<GenesisBspBNode> nodes,
                     std::vector<GenesisBspPlane> planes,
                     std::vector<int32_t> leafContents,
                     std::vector<int32_t> leafFirstSides,
                     std::vector<int32_t> leafNumSides,
                     std::vector<GenesisBspLeafSide> leafSides,
                     int32_t rootNode = 0);

    bool hasTree() const { return !bnodes_.empty() && !planes_.empty(); }
    int planeCount() const { return static_cast<int>(planes_.size()); }

    bool traceBox(const glm::vec3 &front,
                  const glm::vec3 &back,
                  const glm::vec3 &mins,
                  const glm::vec3 &maxs,
                  GenesisTraceHit &outHit) const;

    bool traceBoxDetailed(const glm::vec3 &front,
                          const glm::vec3 &back,
                          const glm::vec3 &mins,
                          const glm::vec3 &maxs,
                          GenesisTraceResult &outResult) const;

    glm::vec3 moveAndSlide(const glm::vec3 &position,
                           const glm::vec3 &delta,
                           const glm::vec3 &mins,
                           const glm::vec3 &maxs,
                           int maxBumps = 4) const;

    const GenesisTraceResult &lastTrace() const { return lastTrace_; }

private:
    static constexpr int PSIDE_FRONT = 1;
    static constexpr int PSIDE_BACK = 2;
    static constexpr int32_t BSP_CONTENTS_SOLID = -1;
    static constexpr float ON_EPSILON = 0.1f;

    struct TraceState
    {
        glm::vec3 mins = glm::vec3(0.0f);
        glm::vec3 maxs = glm::vec3(0.0f);
        float bestDist = 0.0f;
        bool hitSet = false;
        GenesisTraceResult result;
    };
    struct LeafTraceState
    {
        bool hitSet = false;
        glm::vec3 hitPos = glm::vec3(0.0f);
        glm::vec3 planeNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        int32_t planeIndex = -1;
        int32_t side = 0;
    };

    bool intersectRecursive(const glm::vec3 &front,
                            const glm::vec3 &back,
                            int32_t node,
                            TraceState &state) const;

    bool isSolidPosition(const glm::vec3 &point,
                         int32_t node,
                         const glm::vec3 &mins,
                         const glm::vec3 &maxs,
                         int &nodeVisits) const;

    float adjustedPlaneDist(const GenesisBspPlane &plane,
                            const glm::vec3 &mins,
                            const glm::vec3 &maxs) const;

    int boxOnPlaneSide(const glm::vec3 &mins,
                       const glm::vec3 &maxs,
                       const GenesisBspPlane &plane) const;

    bool isSolidContents(int32_t node) const;
    bool intersectLeafSidesRecursive(const glm::vec3 &front,
                                     const glm::vec3 &back,
                                     int32_t leaf,
                                     int32_t side,
                                     int pside,
                                     const glm::vec3 &mins,
                                     const glm::vec3 &maxs,
                                     bool invertOnPlaneSide,
                                     LeafTraceState &out) const;
    void findClosestLeafIntersectionRecursive(int32_t node,
                                              const glm::vec3 &moveMins,
                                              const glm::vec3 &moveMaxs,
                                              TraceState &state) const;

    std::vector<GenesisBspBNode> bnodes_;
    std::vector<GenesisBspPlane> planes_;
    std::vector<int32_t> leafContents_;
    std::vector<int32_t> leafFirstSides_;
    std::vector<int32_t> leafNumSides_;
    std::vector<GenesisBspLeafSide> leafSides_;
    int32_t rootNode_ = 0;
    mutable GenesisTraceResult lastTrace_;
};
