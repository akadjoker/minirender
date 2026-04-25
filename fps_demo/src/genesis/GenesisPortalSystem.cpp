#include "genesis/GenesisPortalSystem.hpp"

#include <queue>

namespace mini_genesis
{
void GenesisPortalSystem::clear()
{
    nodes_.clear();
    planes_.clear();
    leafs_.clear();
    portals_.clear();
    links_.clear();
    rootNode_ = 0;
    debug_ = {};
}

void GenesisPortalSystem::buildFromGbsp(const GbspData &data)
{
    clear();

    nodes_ = data.nodes;
    planes_ = data.planes;
    leafs_ = data.leafs;
    portals_ = data.portals;
    rootNode_ = data.rootNode;

    links_.resize(leafs_.size());
    for (size_t leafIndex = 0; leafIndex < leafs_.size(); ++leafIndex)
    {
        const BspLeaf &leaf = leafs_[leafIndex];
        if (leaf.firstPortal < 0 || leaf.numPortals <= 0)
            continue;

        for (int i = 0; i < leaf.numPortals; ++i)
        {
            const int portalIndex = leaf.firstPortal + i;
            if (portalIndex < 0 || portalIndex >= static_cast<int>(portals_.size()))
                continue;

            const int leafTo = portals_[static_cast<size_t>(portalIndex)].leafTo;
            if (leafTo < 0 || leafTo >= static_cast<int>(leafs_.size()))
                continue;

            links_[leafIndex].push_back(leafTo);
        }
    }

    debug_.leafCount = static_cast<int>(leafs_.size());
    debug_.portalCount = static_cast<int>(portals_.size());
    debug_.clusterCount = static_cast<int>(data.clusters.size());
}

int GenesisPortalSystem::findLeaf(const glm::vec3 &point, int node) const
{
    if (node < 0)
        return -(node + 1);
    if (node >= static_cast<int>(nodes_.size()))
        return -1;

    const BspNode &n = nodes_[static_cast<size_t>(node)];
    if (n.planeNum < 0 || n.planeNum >= static_cast<int>(planes_.size()))
        return -1;

    const BspPlane &plane = planes_[static_cast<size_t>(n.planeNum)];
    const float d = glm::dot(plane.normal, point) - plane.dist;
    const int child = d < 0.0f ? n.children[1] : n.children[0];
    return findLeaf(point, child);
}

void GenesisPortalSystem::update(const glm::vec3 &cameraPos)
{
    debug_.currentLeaf = findLeaf(cameraPos, rootNode_);
    debug_.currentCluster = -1;
    debug_.reachableLeafs = 0;

    if (debug_.currentLeaf < 0 || debug_.currentLeaf >= static_cast<int>(leafs_.size()))
        return;

    debug_.currentCluster = leafs_[static_cast<size_t>(debug_.currentLeaf)].cluster;

    std::vector<uint8_t> visited(leafs_.size(), 0u);
    std::queue<int> open;
    open.push(debug_.currentLeaf);
    visited[static_cast<size_t>(debug_.currentLeaf)] = 1u;

    while (!open.empty())
    {
        const int leaf = open.front();
        open.pop();
        debug_.reachableLeafs++;

        for (int next : links_[static_cast<size_t>(leaf)])
        {
            if (next < 0 || next >= static_cast<int>(leafs_.size()))
                continue;
            if (visited[static_cast<size_t>(next)] != 0u)
                continue;

            visited[static_cast<size_t>(next)] = 1u;
            open.push(next);
        }
    }
}
} // namespace mini_genesis
