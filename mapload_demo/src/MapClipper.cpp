#include "MapClipper.hpp"

#include <algorithm>
#include <cmath>

namespace
{
glm::vec3 mapPointToEngine(const glm::vec3 &point, bool remapZUpToYUp)
{
    return remapZUpToYUp ? glm::vec3(point.x, point.z, point.y) : point;
}

bool buildPlane(const TextMapFace &face,
                bool remapZUpToYUp,
                const glm::vec3 &offset,
                DemoClipPlane &outPlane,
                BoundingBox &bounds)
{
    const glm::vec3 p0 = mapPointToEngine(face.points[0], remapZUpToYUp) + offset;
    const glm::vec3 p1 = mapPointToEngine(face.points[1], remapZUpToYUp) + offset;
    const glm::vec3 p2 = mapPointToEngine(face.points[2], remapZUpToYUp) + offset;

    bounds.expand(p0);
    bounds.expand(p1);
    bounds.expand(p2);

    const glm::vec3 edge1 = p1 - p0;
    const glm::vec3 edge2 = p2 - p1;
    glm::vec3 normal = glm::cross(edge1, edge2);
    const float len2 = glm::length2(normal);
    if (len2 <= 1e-8f)
        return false;

    normal /= std::sqrt(len2);
    outPlane.normal = normal;
    outPlane.dist = glm::dot(normal, p0);
    return true;
}

DemoClipBrush transformedBrush(const DemoClipBrush &brush, const glm::vec3 &offset)
{
    DemoClipBrush transformed;
    transformed.bounds = brush.bounds;
    transformed.bounds.min += offset;
    transformed.bounds.max += offset;
    transformed.planes.reserve(brush.planes.size());
    for (const DemoClipPlane &plane : brush.planes)
    {
        DemoClipPlane moved = plane;
        moved.dist += glm::dot(plane.normal, offset);
        transformed.planes.push_back(moved);
    }
    return transformed;
}
} // namespace

void MapClipper::clear()
{
    brushes_.clear();
}

void MapClipper::addBrush(const TextMapBrush &brush, bool remapZUpToYUp, const glm::vec3 &offset)
{
    DemoClipBrush clipBrush;
    clipBrush.planes.reserve(brush.faces.size());
    for (const TextMapFace &face : brush.faces)
    {
        DemoClipPlane plane;
        if (buildPlane(face, remapZUpToYUp, offset, plane, clipBrush.bounds))
            clipBrush.planes.push_back(plane);
    }

    if (clipBrush.planes.size() >= 4 && clipBrush.bounds.is_valid())
        brushes_.push_back(std::move(clipBrush));
}

void MapClipper::addEntity(const TextMapEntity &entity, bool remapZUpToYUp, const glm::vec3 &offset)
{
    for (const TextMapBrush &brush : entity.brushes)
        addBrush(brush, remapZUpToYUp, offset);
}

void MapClipper::appendTransformed(const MapClipper &other, const glm::vec3 &offset)
{
    brushes_.reserve(brushes_.size() + other.brushes_.size());
    for (const DemoClipBrush &brush : other.brushes_)
        brushes_.push_back(transformedBrush(brush, offset));
}

std::vector<const DemoClipBrush *> MapClipper::gatherBrushes(const glm::vec3 &start,
                                                             const glm::vec3 &end,
                                                             float radius) const
{
    const float reach = glm::length(end - start) + radius + 1.0f;
    BoundingBox query;
    query.expand(start - glm::vec3(reach));
    query.expand(start + glm::vec3(reach));
    query.expand(end - glm::vec3(radius));
    query.expand(end + glm::vec3(radius));

    std::vector<const DemoClipBrush *> result;
    result.reserve(brushes_.size());
    for (const DemoClipBrush &brush : brushes_)
    {
        if (brush.bounds.intersects(query))
            result.push_back(&brush);
    }
    return result;
}

void MapClipper::clipCheckBrushes(const glm::vec3 &start,
                                  const glm::vec3 &end,
                                  float radius,
                                  const std::vector<const DemoClipBrush *> &brushes,
                                  ClipState &outState) const
{
    outState = {};
    outState.dist = 1.0f;
    glm::dvec3 clipEnd = glm::dvec3(end);

    for (const DemoClipBrush *brush : brushes)
    {
        double d1 = 0.0;
        double d2 = outState.dist;
        glm::dvec3 p1 = glm::dvec3(start);
        glm::dvec3 p2 = clipEnd;
        bool startSolid = true;
        const DemoClipPlane savedPlane = outState.plane;
        const bool savedHasPlane = outState.hasPlane;

        size_t i = 0;
        for (; i < brush->planes.size(); ++i)
        {
            const DemoClipPlane &plane = brush->planes[i];
            const double dot1 = glm::dot(glm::dvec3(plane.normal), p1) - plane.dist - radius;
            const double dot2 = glm::dot(glm::dvec3(plane.normal), p2) - plane.dist - radius;

            if (dot1 >= 0.0 && dot2 >= 0.0)
                break;
            if (dot1 < 0.0 && dot2 < 0.0)
                continue;

            const double fraction = dot1 / (dot1 - dot2);
            const glm::dvec3 p = p1 + (p2 - p1) * fraction;
            const double d = d1 + (d2 - d1) * fraction;

            if (dot1 >= 0.0)
            {
                p1 = p;
                d1 = d;
                outState.plane = plane;
                outState.hasPlane = true;
                startSolid = false;
            }
            else
            {
                p2 = p;
                d2 = d;
            }
        }

        if (i == brush->planes.size())
        {
            if (startSolid)
            {
                outState.end = start;
                outState.dist = 0.0f;
                outState.hasPlane = false;
                outState.startSolid = true;
                return;
            }

            clipEnd = p1;
            outState.dist = static_cast<float>(d1);
        }
        else
        {
            outState.plane = savedPlane;
            outState.hasPlane = savedHasPlane;
        }
    }

    outState.end = glm::vec3(clipEnd);
}

int MapClipper::clipMoveSlide(glm::vec3 &ioPosition,
                              const glm::vec3 &desiredEnd,
                              float radius,
                              const std::vector<const DemoClipBrush *> &brushes,
                              bool &outHitGround) const
{
    glm::vec3 speed = desiredEnd - ioPosition;
    glm::vec3 start = ioPosition;
    glm::vec3 end = desiredEnd;
    glm::vec3 safe = start;
    glm::vec3 delta = speed;
    int clipFlag = 0;
    outHitGround = false;

    for (int i = 0; i < 4; ++i)
    {
        ClipState state;
        clipCheckBrushes(start, end, radius, brushes, state);
        if (state.startSolid)
        {
            if (i == 0)
                return 4;

            start = safe;
            end = start + delta;
            continue;
        }

        glm::vec3 v = end - start;
        const float len = glm::length(v);
        if (len > 1.0f)
            v = glm::normalize(v) * 0.1f;
        else
            v *= 0.5f;

        safe = state.end - v;

        if (state.dist >= 1.0f)
            break;
        if (!state.hasPlane)
            continue;

        const DemoClipPlane &plane = state.plane;
        if (plane.normal.y > 0.6f)
            outHitGround = true;
        if (plane.normal.y < 0.1f)
            clipFlag |= 2;
        if (plane.normal.y > 0.8f)
            clipFlag |= 1;

        delta *= (1.0f - state.dist);
        const float dot = glm::dot(plane.normal, delta);
        delta -= plane.normal * dot;

        if (std::fabs(delta.x) < 0.1f) delta.x = 0.0f;
        if (std::fabs(delta.y) < 0.1f) delta.y = 0.0f;
        if (std::fabs(delta.z) < 0.1f) delta.z = 0.0f;
        if (glm::dot(speed, delta) < 0.0f)
            delta = glm::vec3(0.0f);

        start = state.end + plane.normal * 0.1f;
        end = start + delta;
    }

    ioPosition = safe;
    return clipFlag;
}

int MapClipper::traceSphere(glm::vec3 &ioPosition,
                            const glm::vec3 &desiredEnd,
                            float radius,
                            bool &outHitGround) const
{
    const glm::vec3 savedStart = ioPosition;
    const glm::vec3 moveVec = desiredEnd - ioPosition;
    const bool gravityOnly = std::fabs(moveVec.x) <= 1e-6f && std::fabs(moveVec.z) <= 1e-6f;

    std::vector<const DemoClipBrush *> brushes = gatherBrushes(ioPosition, desiredEnd, radius);
    if (brushes.empty())
    {
        ioPosition = desiredEnd;
        outHitGround = false;
        return 0;
    }

    glm::vec3 start = ioPosition;
    glm::vec3 dest = desiredEnd;
    bool hitGround = false;
    int clipFlag = clipMoveSlide(start, dest, radius, brushes, hitGround);

    if (hitGround && gravityOnly)
    {
        start.x = savedStart.x;
        start.z = savedStart.z;
        clipFlag = 0;
    }

    if ((clipFlag & 2) == 2)
    {
        glm::vec3 stepStart = start;
        glm::vec3 stepDest = dest;
        stepStart.y += 32.0f;
        stepDest.y += 12.0f;
        bool tempGround = false;
        const int temp = clipMoveSlide(stepStart, stepDest, radius, brushes, tempGround);
        if (temp != 4 && (temp & 2) == 0)
        {
            start = stepStart;
            clipFlag = 0;
            hitGround = hitGround || tempGround;
        }
    }

    ioPosition = start;
    outHitGround = hitGround;
    return clipFlag;
}

bool MapClipper::dropToFloor(glm::vec3 &ioPosition, float radius, float maxDrop, bool &outHitGround) const
{
    const glm::vec3 desiredEnd = ioPosition - glm::vec3(0.0f, maxDrop, 0.0f);
    const glm::vec3 original = ioPosition;
    traceSphere(ioPosition, desiredEnd, radius, outHitGround);
    return outHitGround || glm::length2(ioPosition - original) > 1e-6f;
}
