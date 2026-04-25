#pragma once

#include <vector>

#include "genesis/GenesisTypes.hpp"

namespace mini_genesis
{
struct PortalDebugState
{
    int leafCount = 0;
    int portalCount = 0;
    int clusterCount = 0;
    int currentLeaf = -1;
    int currentCluster = -1;
    int reachableLeafs = 0;
};

class GenesisPortalSystem
{
public:
    void clear();
    void buildFromGbsp(const GbspData &data);

    // Position must be in engine coordinates.
    void update(const glm::vec3 &cameraPos);

    const PortalDebugState &debug() const { return debug_; }

private:
    int findLeaf(const glm::vec3 &point, int node) const;

    std::vector<BspNode> nodes_;
    std::vector<BspPlane> planes_;
    std::vector<BspLeaf> leafs_;
    std::vector<BspPortal> portals_;
    std::vector<std::vector<int>> links_;
    int rootNode_ = 0;
    PortalDebugState debug_;
};
} // namespace mini_genesis
