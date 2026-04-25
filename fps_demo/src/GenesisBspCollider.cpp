#include "GenesisBspCollider.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

void GenesisBspCollider::clear()
{
    bnodes_.clear();
    planes_.clear();
    leafContents_.clear();
    leafFirstSides_.clear();
    leafNumSides_.clear();
    leafSides_.clear();
    rootNode_ = 0;
    lastTrace_ = {};
}

void GenesisBspCollider::setTree(std::vector<GenesisBspBNode> bnodes,
                                 std::vector<GenesisBspPlane> planes,
                                 int32_t rootNode)
{
    bnodes_ = std::move(bnodes);
    planes_ = std::move(planes);
    leafContents_.clear();
    leafFirstSides_.clear();
    leafNumSides_.clear();
    leafSides_.clear();
    // World collision in Genesis starts from model RootNode[0].
    if (rootNode >= 0 && rootNode < static_cast<int32_t>(bnodes_.size()))
        rootNode_ = rootNode;
    else
        rootNode_ = 0;
    lastTrace_ = {};
}

void GenesisBspCollider::setNodeTree(std::vector<GenesisBspBNode> nodes,
                                     std::vector<GenesisBspPlane> planes,
                                     std::vector<int32_t> leafContents,
                                     std::vector<int32_t> leafFirstSides,
                                     std::vector<int32_t> leafNumSides,
                                     std::vector<GenesisBspLeafSide> leafSides,
                                     int32_t rootNode)
{
    bnodes_ = std::move(nodes);
    planes_ = std::move(planes);
    leafContents_ = std::move(leafContents);
    leafFirstSides_ = std::move(leafFirstSides);
    leafNumSides_ = std::move(leafNumSides);
    leafSides_ = std::move(leafSides);
    if (rootNode >= 0 && rootNode < static_cast<int32_t>(bnodes_.size()))
        rootNode_ = rootNode;
    else
        rootNode_ = 0;
    lastTrace_ = {};
}

float GenesisBspCollider::adjustedPlaneDist(const GenesisBspPlane &plane,
                                            const glm::vec3 &mins,
                                            const glm::vec3 &maxs) const
{
    float dist = plane.dist;
    dist -= plane.normal.x * (plane.normal.x > 0.0f ? mins.x : maxs.x);
    dist -= plane.normal.y * (plane.normal.y > 0.0f ? mins.y : maxs.y);
    dist -= plane.normal.z * (plane.normal.z > 0.0f ? mins.z : maxs.z);
    return dist;
}

int GenesisBspCollider::boxOnPlaneSide(const glm::vec3 &mins,
                                       const glm::vec3 &maxs,
                                       const GenesisBspPlane &plane) const
{
    // Genesis Trace_BoxOnPlaneSide behavior for generic planes.
    glm::vec3 corners[2];
    for (int i = 0; i < 3; ++i)
    {
        const float n = plane.normal[i];
        if (n < 0.0f)
        {
            corners[0][i] = mins[i];
            corners[1][i] = maxs[i];
        }
        else
        {
            corners[1][i] = mins[i];
            corners[0][i] = maxs[i];
        }
    }

    const float d1 = glm::dot(plane.normal, corners[0]) - plane.dist;
    const float d2 = glm::dot(plane.normal, corners[1]) - plane.dist;

    int side = 0;
    if (d1 >= 0.0f)
        side |= PSIDE_FRONT;
    if (d2 < 0.0f)
        side |= PSIDE_BACK;
    return side;
}

bool GenesisBspCollider::isSolidContents(int32_t node) const
{
    if (node >= 0)
        return false;

    // Genesis world tree: negative children index leafs via -(node+1)
    // and collision checks leaf contents against GE_CONTENTS_SOLID_CLIP.
    const int32_t leaf = -(node + 1);
    if (leaf >= 0 && leaf < static_cast<int32_t>(leafContents_.size()))
    {
        const int32_t contents = leafContents_[static_cast<size_t>(leaf)];
        constexpr int32_t GE_CONTENTS_SOLID = (1 << 0);
        constexpr int32_t GE_CONTENTS_WINDOW = (1 << 1);
        constexpr int32_t GE_CONTENTS_CLIP = (1 << 6);
        constexpr int32_t GE_CONTENTS_SOLID_CLIP = GE_CONTENTS_SOLID | GE_CONTENTS_WINDOW | GE_CONTENTS_CLIP;
        return (contents & GE_CONTENTS_SOLID_CLIP) != 0;
    }

    // Fallback for misc bnode traces using direct contents constants.
    if (node == -2)
        return false;
    return node == BSP_CONTENTS_SOLID;
}

bool GenesisBspCollider::intersectLeafSidesRecursive(const glm::vec3 &front,
                                                     const glm::vec3 &back,
                                                     int32_t leaf,
                                                     int32_t side,
                                                     int pside,
                                                     const glm::vec3 &mins,
                                                     const glm::vec3 &maxs,
                                                     bool invertOnPlaneSide,
                                                     LeafTraceState &out) const
{
    if (!pside)
        return false;
    if (leaf < 0 || leaf >= static_cast<int32_t>(leafFirstSides_.size()) ||
        leaf >= static_cast<int32_t>(leafNumSides_.size()))
        return false;
    if (side >= leafNumSides_[static_cast<size_t>(leaf)])
        return true;

    const int32_t sideIndex = leafFirstSides_[static_cast<size_t>(leaf)] + side;
    if (sideIndex < 0 || sideIndex >= static_cast<int32_t>(leafSides_.size()))
        return false;

    const GenesisBspLeafSide &leafSide = leafSides_[static_cast<size_t>(sideIndex)];
    if (leafSide.planeNum < 0 || leafSide.planeNum >= static_cast<int32_t>(planes_.size()))
        return false;

    GenesisBspPlane plane = planes_[static_cast<size_t>(leafSide.planeNum)];
    const bool flipPlane = invertOnPlaneSide ? (leafSide.planeSide != 0) : (leafSide.planeSide == 0);
    if (flipPlane)
    {
        plane.normal = -plane.normal;
        plane.dist = -plane.dist;
    }
    plane.dist = adjustedPlaneDist(plane, mins, maxs);

    const float fd = glm::dot(plane.normal, front) - plane.dist;
    const float bd = glm::dot(plane.normal, back) - plane.dist;

    if (fd >= 0.0f && bd >= 0.0f)
        return intersectLeafSidesRecursive(front, back, leaf, side + 1, 0, mins, maxs, invertOnPlaneSide, out);
    if (fd < 0.0f && bd < 0.0f)
        return intersectLeafSidesRecursive(front, back, leaf, side + 1, 1, mins, maxs, invertOnPlaneSide, out);

    const int side2 = (fd < 0.0f) ? 1 : 0;
    float t = (fd < 0.0f) ? ((fd + ON_EPSILON) / (fd - bd))
                          : ((fd - ON_EPSILON) / (fd - bd));
    t = glm::clamp(t, 0.0f, 1.0f);
    const glm::vec3 i = front + t * (back - front);

    if (intersectLeafSidesRecursive(front, i, leaf, side + 1, side2, mins, maxs, invertOnPlaneSide, out))
        return true;

    if (intersectLeafSidesRecursive(i, back, leaf, side + 1, !side2, mins, maxs, invertOnPlaneSide, out))
    {
        if (!out.hitSet)
        {
            out.hitSet = true;
            out.hitPos = i;
            out.planeNormal = plane.normal;
            out.planeIndex = leafSide.planeNum;
            out.side = side2;
        }
        return true;
    }

    return false;
}

void GenesisBspCollider::findClosestLeafIntersectionRecursive(int32_t node,
                                                              const glm::vec3 &moveMins,
                                                              const glm::vec3 &moveMaxs,
                                                              TraceState &state) const
{
    state.result.nodeVisits++;

    if (node < 0)
    {
        if (!isSolidContents(node))
            return;

        const int32_t leaf = -(node + 1);
        if (leaf < 0 || leaf >= static_cast<int32_t>(leafNumSides_.size()) ||
            leafNumSides_[static_cast<size_t>(leaf)] <= 0)
            return;

        LeafTraceState leafHitPrimary;
        intersectLeafSidesRecursive(state.result.start,
                                    state.result.end,
                                    leaf,
                                    0,
                                    1,
                                    state.mins,
                                    state.maxs,
                                    true,
                                    leafHitPrimary);

        LeafTraceState leafHitAlt;
        intersectLeafSidesRecursive(state.result.start,
                                    state.result.end,
                                    leaf,
                                    0,
                                    1,
                                    state.mins,
                                    state.maxs,
                                    false,
                                    leafHitAlt);

        LeafTraceState leafHit = leafHitPrimary;
        if (!leafHit.hitSet && leafHitAlt.hitSet)
            leafHit = leafHitAlt;
        else if (leafHit.hitSet && leafHitAlt.hitSet)
        {
            const float primaryDist = glm::length(leafHit.hitPos - state.result.start);
            const float altDist = glm::length(leafHitAlt.hitPos - state.result.start);
            if (altDist < primaryDist)
                leafHit = leafHitAlt;
        }

        if (!leafHit.hitSet)
            return;

        const float hitDist = glm::length(leafHit.hitPos - state.result.start);
        if (!state.hitSet || hitDist < state.bestDist)
        {
            state.hitSet = true;
            state.bestDist = hitDist;
            state.result.hit = true;
            state.result.endPos = leafHit.hitPos;
            state.result.planeNormal = leafHit.planeNormal;
            state.result.planeIndex = leafHit.planeIndex;
            state.result.side = leafHit.side;
        }
        return;
    }

    if (node >= static_cast<int32_t>(bnodes_.size()))
        return;

    const GenesisBspBNode &bnode = bnodes_[static_cast<size_t>(node)];
    if (bnode.planeNum < 0 || bnode.planeNum >= static_cast<int32_t>(planes_.size()))
        return;

    const GenesisBspPlane &plane = planes_[static_cast<size_t>(bnode.planeNum)];
    const int side = boxOnPlaneSide(moveMins, moveMaxs, plane);

    if (side & PSIDE_FRONT)
        findClosestLeafIntersectionRecursive(bnode.children[0], moveMins, moveMaxs, state);
    if (side & PSIDE_BACK)
        findClosestLeafIntersectionRecursive(bnode.children[1], moveMins, moveMaxs, state);
}

bool GenesisBspCollider::intersectRecursive(const glm::vec3 &front,
                                            const glm::vec3 &back,
                                            int32_t node,
                                            TraceState &state) const
{
    state.result.nodeVisits++;

    if (node < 0)
    {
        if (!isSolidContents(node))
            return false;

        const int32_t leaf = -(node + 1);
        if (leaf >= 0 && leaf < static_cast<int32_t>(leafNumSides_.size()) &&
            leafNumSides_[static_cast<size_t>(leaf)] > 0)
        {
            LeafTraceState leafHit;
            if (!intersectLeafSidesRecursive(front, back, leaf, 0, 1, state.mins, state.maxs, true, leafHit))
                return false;

            if (!state.hitSet && leafHit.hitSet)
            {
                state.hitSet = true;
                state.result.hit = true;
                state.result.endPos = leafHit.hitPos;
                state.result.planeNormal = leafHit.planeNormal;
                state.result.planeIndex = leafHit.planeIndex;
                state.result.side = leafHit.side;
            }
            return true;
        }
        return true;
    }
    if (node >= static_cast<int32_t>(bnodes_.size()))
        return false;

    const GenesisBspBNode &bnode = bnodes_[static_cast<size_t>(node)];
    if (bnode.planeNum < 0 || bnode.planeNum >= static_cast<int32_t>(planes_.size()))
        return false;

    GenesisBspPlane plane = planes_[static_cast<size_t>(bnode.planeNum)];
    plane.dist = adjustedPlaneDist(plane, state.mins, state.maxs);

    const float fd = glm::dot(plane.normal, front) - plane.dist;
    const float bd = glm::dot(plane.normal, back) - plane.dist;

    if (fd >= ON_EPSILON && bd >= ON_EPSILON)
        return intersectRecursive(front, back, bnode.children[0], state);
    if (fd <= -ON_EPSILON && bd <= -ON_EPSILON)
        return intersectRecursive(front, back, bnode.children[1], state);

    const int32_t side = (fd < 0.0f) ? 1 : 0;
    float t;
    if (fd < 0.0f)
        t = (fd + ON_EPSILON) / (fd - bd);
    else
        t = (fd - ON_EPSILON) / (fd - bd);
    t = glm::clamp(t, 0.0f, 1.0f);
    const glm::vec3 i = front + t * (back - front);

    if (intersectRecursive(front, i, bnode.children[side], state))
    {
        return true;
    }
    if (intersectRecursive(i, back, bnode.children[!side], state))
    {
        if (!state.hitSet)
        {
            state.hitSet = true;
            state.result.hit = true;
            state.result.endPos = i;
            state.result.planeNormal = plane.normal;
            state.result.planeIndex = bnode.planeNum;
            state.result.side = side;
        }
        return true;
    }

    return false;
}

bool GenesisBspCollider::isSolidPosition(const glm::vec3 &point,
                                         int32_t node,
                                         const glm::vec3 &mins,
                                         const glm::vec3 &maxs,
                                         int &nodeVisits) const
{
    nodeVisits++;
    if (isSolidContents(node))
        return true;
    if (node < 0 || node >= static_cast<int32_t>(bnodes_.size()))
        return false;

    const GenesisBspBNode &bnode = bnodes_[static_cast<size_t>(node)];
    if (bnode.planeNum < 0 || bnode.planeNum >= static_cast<int32_t>(planes_.size()))
        return false;

    GenesisBspPlane plane = planes_[static_cast<size_t>(bnode.planeNum)];
    plane.dist = adjustedPlaneDist(plane, mins, maxs);

    const float d = glm::dot(plane.normal, point) - plane.dist;
    if (d >= 0.0f)
        return isSolidPosition(point, bnode.children[0], mins, maxs, nodeVisits);
    return isSolidPosition(point, bnode.children[1], mins, maxs, nodeVisits);
}

bool GenesisBspCollider::traceBoxDetailed(const glm::vec3 &front,
                                          const glm::vec3 &back,
                                          const glm::vec3 &mins,
                                          const glm::vec3 &maxs,
                                          GenesisTraceResult &outResult) const
{
    outResult = {};
    outResult.start = front;
    outResult.end = back;
    outResult.endPos = back;
    if (!hasTree())
    {
        lastTrace_ = outResult;
        return false;
    }

    int classifyVisits = 0;
    outResult.startSolid = isSolidPosition(front, rootNode_, mins, maxs, classifyVisits);
    outResult.allSolid = outResult.startSolid && isSolidPosition(back, rootNode_, mins, maxs, classifyVisits);

    TraceState state;
    state.mins = mins;
    state.maxs = maxs;
    state.bestDist = std::numeric_limits<float>::max();
    state.result = outResult;
    state.result.nodeVisits = classifyVisits;

    bool segmentHit = false;
    if (!leafContents_.empty() && !leafSides_.empty())
    {
        glm::vec3 moveMins(0.0f);
        glm::vec3 moveMaxs(0.0f);
        for (int i = 0; i < 3; ++i)
        {
            if (back[i] > front[i])
            {
                moveMins[i] = front[i] + mins[i] - 1.0f;
                moveMaxs[i] = back[i] + maxs[i] + 1.0f;
            }
            else
            {
                moveMins[i] = back[i] + mins[i] - 1.0f;
                moveMaxs[i] = front[i] + maxs[i] + 1.0f;
            }
        }
        findClosestLeafIntersectionRecursive(rootNode_, moveMins, moveMaxs, state);
        segmentHit = state.hitSet;
    }
    else
    {
        segmentHit = intersectRecursive(front, back, rootNode_, state);
    }

    outResult = state.result;

    const glm::vec3 move = back - front;
    const float moveLen2 = glm::length2(move);
    if (state.hitSet && moveLen2 > 1e-8f)
    {
        // Keep the hit point/fraction mathematically consistent with the swept segment.
        float t = glm::dot(outResult.endPos - front, move) / moveLen2;
        t = glm::clamp(t, 0.0f, 1.0f);
        outResult.fraction = t;
        outResult.endPos = front + move * t;
        outResult.hit = true;
    }
    else if (outResult.startSolid)
    {
        outResult.hit = true;
        outResult.fraction = 0.0f;
        outResult.endPos = front;
    }
    else
    {
        outResult.hit = false;
        outResult.fraction = 1.0f;
        outResult.endPos = back;
    }

    lastTrace_ = outResult;
    return segmentHit || outResult.startSolid;
}

bool GenesisBspCollider::traceBox(const glm::vec3 &front,
                                  const glm::vec3 &back,
                                  const glm::vec3 &mins,
                                  const glm::vec3 &maxs,
                                  GenesisTraceHit &outHit) const
{
    GenesisTraceResult result;
    const bool hit = traceBoxDetailed(front, back, mins, maxs, result);

    outHit = {};
    if (!hit || !result.hit)
        return false;

    outHit.hit = true;
    outHit.point = result.endPos;
    outHit.planeNormal = result.planeNormal;
    outHit.planeIndex = result.planeIndex;
    outHit.side = result.side;
    return true;
}

glm::vec3 GenesisBspCollider::moveAndSlide(const glm::vec3 &position,
                                           const glm::vec3 &delta,
                                           const glm::vec3 &mins,
                                           const glm::vec3 &maxs,
                                           int maxBumps) const
{
    if (!hasTree())
        return position + delta;

    glm::vec3 pos = position;
    glm::vec3 remain = delta;

    for (int bump = 0; bump < maxBumps; ++bump)
    {
        if (glm::length2(remain) <= 1e-8f)
            break;

        const glm::vec3 end = pos + remain;
        GenesisTraceResult hit;
        if (!traceBoxDetailed(pos, end, mins, maxs, hit) || !hit.hit)
        {
            pos = end;
            break;
        }
        if (hit.startSolid && hit.planeIndex < 0)
        {
            // Try to depenetrate instead of hard-locking the player in place.
            const float step = std::max(2.0f, glm::max(glm::max(std::abs(mins.x), std::abs(mins.y)), std::abs(mins.z)) * 0.25f);
            const std::array<glm::vec3, 10> offsets = {
                glm::vec3(0.0f, step, 0.0f),
                glm::vec3(0.0f, step * 2.0f, 0.0f),
                glm::vec3(step, 0.0f, 0.0f),
                glm::vec3(-step, 0.0f, 0.0f),
                glm::vec3(0.0f, 0.0f, step),
                glm::vec3(0.0f, 0.0f, -step),
                glm::vec3(step, step, 0.0f),
                glm::vec3(-step, step, 0.0f),
                glm::vec3(0.0f, step, step),
                glm::vec3(0.0f, step, -step),
            };

            bool recovered = false;
            GenesisTraceResult probe;
            for (const glm::vec3 &off : offsets)
            {
                const glm::vec3 candidate = pos + off;
                traceBoxDetailed(candidate, candidate, mins, maxs, probe);
                if (!probe.startSolid)
                {
                    pos = candidate;
                    recovered = true;
                    break;
                }
            }

            if (recovered)
                continue;
            break;
        }

        glm::vec3 n = hit.planeNormal;
        if (glm::dot(n, remain) > 0.0f)
            n = -n;

        pos = hit.endPos + n * ON_EPSILON;

        const float leftFrac = glm::clamp(1.0f - hit.fraction, 0.0f, 1.0f);
        glm::vec3 left = remain * leftFrac;
        glm::vec3 clipped = left - n * glm::dot(left, n);
        if (glm::length2(clipped) <= 1e-8f)
            break;

        if (hit.fraction <= 1e-5f)
            break;

        remain = clipped;
    }

    return pos;
}
