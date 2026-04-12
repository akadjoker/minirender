#include "EditorBrushGeometryOps.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>

#include <glm/gtx/norm.hpp>

const char *brushFaceName(int faceIndex)
{
    static const char *kNames[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    if (faceIndex < 0 || faceIndex >= 6)
        return "+X";
    return kNames[faceIndex];
}

int faceLeft() { return 1; }
int faceRight() { return 0; }
int faceTop() { return 2; }
int faceBottom() { return 3; }
int faceFront() { return 4; }
int faceBack() { return 5; }

int defaultFaceForView(EditorViewType viewType, int fallback)
{
    switch (viewType)
    {
    case EditorViewType::Top: return faceTop();
    case EditorViewType::Bottom: return faceBottom();
    case EditorViewType::Front: return faceFront();
    case EditorViewType::Back: return faceBack();
    case EditorViewType::Left: return faceLeft();
    case EditorViewType::Right: return faceRight();
    case EditorViewType::Perspective: break;
    }
    return fallback;
}

void swapBrushFaces(BrushVolume &brush, int a, int b)
{
    if (a < 0 || a >= 6 || b < 0 || b >= 6 || a == b)
        return;
    std::swap(brush.faceTextures[(size_t)a], brush.faceTextures[(size_t)b]);
    std::swap(brush.faceUV[(size_t)a], brush.faceUV[(size_t)b]);
    brush.dirty = true;
}

static int rotateFaceX90Once(int faceIndex)
{
    switch (faceIndex)
    {
    case 2: return 4;
    case 4: return 3;
    case 3: return 5;
    case 5: return 2;
    default: break;
    }
    return faceIndex;
}

static int rotateFaceY90Once(int faceIndex)
{
    switch (faceIndex)
    {
    case 0: return 5;
    case 5: return 1;
    case 1: return 4;
    case 4: return 0;
    default: break;
    }
    return faceIndex;
}

static int rotateFaceZ90Once(int faceIndex)
{
    switch (faceIndex)
    {
    case 0: return 2;
    case 2: return 1;
    case 1: return 3;
    case 3: return 0;
    default: break;
    }
    return faceIndex;
}

void rotateBrushY90(BrushVolume &brush, int turns)
{
    int steps = turns % 4;
    if (steps < 0)
        steps += 4;
    for (int i = 0; i < steps; ++i)
    {
        const glm::vec3 center = (brush.mins + brush.maxs) * 0.5f;
        glm::vec3 half = (brush.maxs - brush.mins) * 0.5f;
        std::swap(half.x, half.z);
        brush.mins = center - half;
        brush.maxs = center + half;

        const std::string oldPX = brush.faceTextures[(size_t)faceRight()];
        const std::string oldNX = brush.faceTextures[(size_t)faceLeft()];
        const std::string oldPZ = brush.faceTextures[(size_t)faceFront()];
        const std::string oldNZ = brush.faceTextures[(size_t)faceBack()];
        brush.faceTextures[(size_t)faceRight()] = oldPZ;
        brush.faceTextures[(size_t)faceBack()] = oldPX;
        brush.faceTextures[(size_t)faceLeft()] = oldNZ;
        brush.faceTextures[(size_t)faceFront()] = oldNX;

        const BrushVolume::FaceUV oldUvPX = brush.faceUV[(size_t)faceRight()];
        const BrushVolume::FaceUV oldUvNX = brush.faceUV[(size_t)faceLeft()];
        const BrushVolume::FaceUV oldUvPZ = brush.faceUV[(size_t)faceFront()];
        const BrushVolume::FaceUV oldUvNZ = brush.faceUV[(size_t)faceBack()];
        brush.faceUV[(size_t)faceRight()] = oldUvPZ;
        brush.faceUV[(size_t)faceBack()] = oldUvPX;
        brush.faceUV[(size_t)faceLeft()] = oldUvNZ;
        brush.faceUV[(size_t)faceFront()] = oldUvNX;
    }
    brush.dirty = true;
}

void rotateBrushX90(BrushVolume &brush, int turns)
{
    int steps = turns % 4;
    if (steps < 0)
        steps += 4;
    for (int i = 0; i < steps; ++i)
    {
        const glm::vec3 center = (brush.mins + brush.maxs) * 0.5f;
        glm::vec3 half = (brush.maxs - brush.mins) * 0.5f;
        std::swap(half.y, half.z);
        brush.mins = center - half;
        brush.maxs = center + half;

        const std::string oldPY = brush.faceTextures[(size_t)faceTop()];
        const std::string oldNY = brush.faceTextures[(size_t)faceBottom()];
        const std::string oldPZ = brush.faceTextures[(size_t)faceFront()];
        const std::string oldNZ = brush.faceTextures[(size_t)faceBack()];
        brush.faceTextures[(size_t)faceTop()] = oldNZ;
        brush.faceTextures[(size_t)faceFront()] = oldPY;
        brush.faceTextures[(size_t)faceBottom()] = oldPZ;
        brush.faceTextures[(size_t)faceBack()] = oldNY;

        const BrushVolume::FaceUV oldUvPY = brush.faceUV[(size_t)faceTop()];
        const BrushVolume::FaceUV oldUvNY = brush.faceUV[(size_t)faceBottom()];
        const BrushVolume::FaceUV oldUvPZ = brush.faceUV[(size_t)faceFront()];
        const BrushVolume::FaceUV oldUvNZ = brush.faceUV[(size_t)faceBack()];
        brush.faceUV[(size_t)faceTop()] = oldUvNZ;
        brush.faceUV[(size_t)faceFront()] = oldUvPY;
        brush.faceUV[(size_t)faceBottom()] = oldUvPZ;
        brush.faceUV[(size_t)faceBack()] = oldUvNY;
    }
    brush.dirty = true;
}

void rotateBrushZ90(BrushVolume &brush, int turns)
{
    int steps = turns % 4;
    if (steps < 0)
        steps += 4;
    for (int i = 0; i < steps; ++i)
    {
        const glm::vec3 center = (brush.mins + brush.maxs) * 0.5f;
        glm::vec3 half = (brush.maxs - brush.mins) * 0.5f;
        std::swap(half.x, half.y);
        brush.mins = center - half;
        brush.maxs = center + half;

        const std::string oldPX = brush.faceTextures[(size_t)faceRight()];
        const std::string oldNX = brush.faceTextures[(size_t)faceLeft()];
        const std::string oldPY = brush.faceTextures[(size_t)faceTop()];
        const std::string oldNY = brush.faceTextures[(size_t)faceBottom()];
        brush.faceTextures[(size_t)faceRight()] = oldNY;
        brush.faceTextures[(size_t)faceTop()] = oldPX;
        brush.faceTextures[(size_t)faceLeft()] = oldPY;
        brush.faceTextures[(size_t)faceBottom()] = oldNX;

        const BrushVolume::FaceUV oldUvPX = brush.faceUV[(size_t)faceRight()];
        const BrushVolume::FaceUV oldUvNX = brush.faceUV[(size_t)faceLeft()];
        const BrushVolume::FaceUV oldUvPY = brush.faceUV[(size_t)faceTop()];
        const BrushVolume::FaceUV oldUvNY = brush.faceUV[(size_t)faceBottom()];
        brush.faceUV[(size_t)faceRight()] = oldUvNY;
        brush.faceUV[(size_t)faceTop()] = oldUvPX;
        brush.faceUV[(size_t)faceLeft()] = oldUvPY;
        brush.faceUV[(size_t)faceBottom()] = oldUvNX;
    }
    brush.dirty = true;
}

void rotateBrushForView90(BrushVolume &brush, EditorViewType viewType, int turns)
{
    switch (viewType)
    {
    case EditorViewType::Top:
    case EditorViewType::Bottom:
        rotateBrushY90(brush, turns);
        break;
    case EditorViewType::Front:
    case EditorViewType::Back:
        rotateBrushZ90(brush, turns);
        break;
    case EditorViewType::Left:
    case EditorViewType::Right:
        rotateBrushX90(brush, turns);
        break;
    case EditorViewType::Perspective:
        break;
    }
}

int rotateFaceForView90(int faceIndex, EditorViewType viewType, int turns)
{
    int steps = turns % 4;
    if (steps < 0)
        steps += 4;
    for (int i = 0; i < steps; ++i)
    {
        switch (viewType)
        {
        case EditorViewType::Top:
        case EditorViewType::Bottom:
            faceIndex = rotateFaceY90Once(faceIndex);
            break;
        case EditorViewType::Front:
        case EditorViewType::Back:
            faceIndex = rotateFaceZ90Once(faceIndex);
            break;
        case EditorViewType::Left:
        case EditorViewType::Right:
            faceIndex = rotateFaceX90Once(faceIndex);
            break;
        case EditorViewType::Perspective:
            break;
        }
    }
    return faceIndex;
}

static int rotationDirectionForView(EditorViewType viewType)
{
    switch (viewType)
    {
    case EditorViewType::Bottom:
    case EditorViewType::Back:
    case EditorViewType::Left:
        return -1;
    case EditorViewType::Top:
    case EditorViewType::Front:
    case EditorViewType::Right:
    case EditorViewType::Perspective:
        break;
    }
    return 1;
}

int rotationTurnsFromMouseDrag(EditorViewType viewType, const glm::vec2 &mouseDelta)
{
    constexpr float pixelsPerStep = 42.0f;
    const float primaryDelta = (std::fabs(mouseDelta.x) >= std::fabs(mouseDelta.y))
        ? mouseDelta.x
        : -mouseDelta.y;
    const int steps = (int)std::floor(std::fabs(primaryDelta) / pixelsPerStep);
    if (steps <= 0)
        return 0;
    const int sign = primaryDelta < 0.0f ? -1 : 1;
    return steps * sign * rotationDirectionForView(viewType);
}

glm::vec3 randomBrushColor()
{
    static std::mt19937 rng(1337u);
    static std::uniform_real_distribution<float> hueDist(0.0f, 1.0f);
    const float h = hueDist(rng);
    const float s = 0.45f;
    const float v = 0.95f;

    const float hh = h * 6.0f;
    const int i = (int)std::floor(hh) % 6;
    const float f = hh - std::floor(hh);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);

    switch (i)
    {
    case 0: return glm::vec3(v, t, p);
    case 1: return glm::vec3(q, v, p);
    case 2: return glm::vec3(p, v, t);
    case 3: return glm::vec3(p, q, v);
    case 4: return glm::vec3(t, p, v);
    case 5: return glm::vec3(v, p, q);
    default: break;
    }
    return glm::vec3(0.47f, 0.82f, 1.0f);
}

glm::vec2 projectWorldToViewPlane(const glm::vec3 &p, EditorViewType viewType)
{
    switch (viewType)
    {
    case EditorViewType::Top:
        return glm::vec2(p.x, p.z);
    case EditorViewType::Bottom:
        return glm::vec2(p.x, -p.z);
    case EditorViewType::Front:
        return glm::vec2(p.x, p.y);
    case EditorViewType::Back:
        return glm::vec2(-p.x, p.y);
    case EditorViewType::Left:
        return glm::vec2(p.z, p.y);
    case EditorViewType::Right:
        return glm::vec2(-p.z, p.y);
    case EditorViewType::Perspective:
        break;
    }
    return glm::vec2(0.0f);
}

glm::vec3 applyViewDelta(const glm::vec3 &delta, EditorViewType viewType)
{
    switch (viewType)
    {
    case EditorViewType::Top:
    case EditorViewType::Bottom:
        return glm::vec3(delta.x, 0.0f, delta.z);
    case EditorViewType::Front:
    case EditorViewType::Back:
        return glm::vec3(delta.x, delta.y, 0.0f);
    case EditorViewType::Left:
    case EditorViewType::Right:
        return glm::vec3(0.0f, delta.y, delta.z);
    case EditorViewType::Perspective:
        break;
    }
    return glm::vec3(0.0f);
}

BrushVolume makeBrushFromDrag(EditorViewType viewType,
                              const glm::vec3 &start,
                              const glm::vec3 &end,
                              float defaultThickness,
                              float defaultHeight,
                              const glm::vec3 &focus,
                              const std::string &texturePath)
{
    BrushVolume brush;
    brush.color = randomBrushColor();
    brush.texturePath = texturePath;

    switch (viewType)
    {
    case EditorViewType::Top:
    case EditorViewType::Bottom:
        brush.mins.x = glm::min(start.x, end.x);
        brush.maxs.x = glm::max(start.x, end.x);
        brush.mins.z = glm::min(start.z, end.z);
        brush.maxs.z = glm::max(start.z, end.z);
        brush.mins.y = focus.y;
        brush.maxs.y = focus.y + glm::max(defaultHeight, 1.0f);
        break;
    case EditorViewType::Front:
    case EditorViewType::Back:
        brush.mins.x = glm::min(start.x, end.x);
        brush.maxs.x = glm::max(start.x, end.x);
        brush.mins.y = glm::min(start.y, end.y);
        brush.maxs.y = glm::max(start.y, end.y);
        brush.mins.z = focus.z - defaultThickness * 0.5f;
        brush.maxs.z = focus.z + defaultThickness * 0.5f;
        break;
    case EditorViewType::Left:
    case EditorViewType::Right:
        brush.mins.z = glm::min(start.z, end.z);
        brush.maxs.z = glm::max(start.z, end.z);
        brush.mins.y = glm::min(start.y, end.y);
        brush.maxs.y = glm::max(start.y, end.y);
        brush.mins.x = focus.x - defaultThickness * 0.5f;
        brush.maxs.x = focus.x + defaultThickness * 0.5f;
        break;
    case EditorViewType::Perspective:
        break;
    }

    if (brush.maxs.x - brush.mins.x < 1.0f)
        brush.maxs.x = brush.mins.x + 1.0f;
    if (brush.maxs.y - brush.mins.y < 1.0f)
        brush.maxs.y = brush.mins.y + 1.0f;
    if (brush.maxs.z - brush.mins.z < 1.0f)
        brush.maxs.z = brush.mins.z + 1.0f;

    return brush;
}

static float distancePointToRect2D(const glm::vec2 &p, const glm::vec2 &mins, const glm::vec2 &maxs)
{
    const float dx = glm::max(glm::max(mins.x - p.x, 0.0f), p.x - maxs.x);
    const float dy = glm::max(glm::max(mins.y - p.y, 0.0f), p.y - maxs.y);
    return std::sqrt(dx * dx + dy * dy);
}

int findBrushAtPoint(const std::vector<BrushVolume> &brushes,
                     EditorViewType viewType,
                     const glm::vec3 &worldPoint,
                     float maxDistance)
{
    int bestIndex = -1;
    float bestDistance = maxDistance;
    const glm::vec2 point2 = projectWorldToViewPlane(worldPoint, viewType);

    for (int i = 0; i < (int)brushes.size(); ++i)
    {
        if (brushes[i].hidden)
            continue;
        const glm::vec2 a = projectWorldToViewPlane(brushes[i].mins, viewType);
        const glm::vec2 b = projectWorldToViewPlane(brushes[i].maxs, viewType);
        const glm::vec2 mins(glm::min(a.x, b.x), glm::min(a.y, b.y));
        const glm::vec2 maxs(glm::max(a.x, b.x), glm::max(a.y, b.y));
        const float distance = distancePointToRect2D(point2, mins, maxs);
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    return bestIndex;
}

bool brushIntersectsSelectionRect(const BrushVolume &brush,
                                  EditorViewType viewType,
                                  const glm::vec3 &rectStartWorld,
                                  const glm::vec3 &rectEndWorld)
{
    const glm::vec2 r0 = projectWorldToViewPlane(rectStartWorld, viewType);
    const glm::vec2 r1 = projectWorldToViewPlane(rectEndWorld, viewType);
    const glm::vec2 rectMins(glm::min(r0.x, r1.x), glm::min(r0.y, r1.y));
    const glm::vec2 rectMaxs(glm::max(r0.x, r1.x), glm::max(r0.y, r1.y));

    const glm::vec2 a = projectWorldToViewPlane(brush.mins, viewType);
    const glm::vec2 b = projectWorldToViewPlane(brush.maxs, viewType);
    const glm::vec2 brushMins(glm::min(a.x, b.x), glm::min(a.y, b.y));
    const glm::vec2 brushMaxs(glm::max(a.x, b.x), glm::max(a.y, b.y));

    const bool overlapX = !(brushMaxs.x < rectMins.x || brushMins.x > rectMaxs.x);
    const bool overlapY = !(brushMaxs.y < rectMins.y || brushMins.y > rectMaxs.y);
    return overlapX && overlapY;
}

static bool raycastBrushFace(const BrushVolume &brush, const Ray &ray, float &outT, int &outFace)
{
    constexpr float eps = 1e-7f;
    float tMin = 0.0f;
    float tMax = 1e30f;
    int enterFace = -1;
    int exitFace = -1;

    const int minFaces[3] = {1, 3, 5};
    const int maxFaces[3] = {0, 2, 4};

    for (int axis = 0; axis < 3; ++axis)
    {
        const float origin = ray.origin[axis];
        const float dir = ray.direction[axis];
        const float bMin = brush.mins[axis];
        const float bMax = brush.maxs[axis];

        if (std::fabs(dir) <= eps)
        {
            if (origin < bMin || origin > bMax)
                return false;
            continue;
        }

        float invDir = 1.0f / dir;
        float t1 = (bMin - origin) * invDir;
        float t2 = (bMax - origin) * invDir;
        int face1 = minFaces[axis];
        int face2 = maxFaces[axis];
        if (t1 > t2)
        {
            std::swap(t1, t2);
            std::swap(face1, face2);
        }

        if (t1 > tMin)
        {
            tMin = t1;
            enterFace = face1;
        }
        if (t2 < tMax)
        {
            tMax = t2;
            exitFace = face2;
        }
        if (tMin > tMax)
            return false;
    }

    if (tMax < 0.0f)
        return false;

    if (tMin >= 0.0f)
    {
        outT = tMin;
        outFace = enterFace;
    }
    else
    {
        outT = tMax;
        outFace = exitFace;
    }
    return outFace >= 0;
}

bool pickBrushWithRay(const std::vector<BrushVolume> &brushes,
                      const Ray &ray,
                      int &outBrush,
                      int &outFace,
                      float maxDistance)
{
    outBrush = -1;
    outFace = -1;
    float bestT = maxDistance;

    for (int i = 0; i < (int)brushes.size(); ++i)
    {
        if (brushes[i].hidden)
            continue;
        float t = 0.0f;
        int face = -1;
        if (!raycastBrushFace(brushes[i], ray, t, face))
            continue;
        if (t < 0.0f || t > bestT)
            continue;
        bestT = t;
        outBrush = i;
        outFace = face;
    }

    return outBrush >= 0;
}

BrushScaleAxis chooseBrushScaleAxis(EditorViewType viewType, const glm::vec2 &viewDelta)
{
    const bool useFirst = std::fabs(viewDelta.x) >= std::fabs(viewDelta.y);
    switch (viewType)
    {
    case EditorViewType::Top:
    case EditorViewType::Bottom:
        return useFirst ? BrushScaleAxis::X : BrushScaleAxis::Z;
    case EditorViewType::Front:
    case EditorViewType::Back:
        return useFirst ? BrushScaleAxis::X : BrushScaleAxis::Y;
    case EditorViewType::Left:
    case EditorViewType::Right:
        return useFirst ? BrushScaleAxis::Z : BrushScaleAxis::Y;
    case EditorViewType::Perspective:
        break;
    }
    return BrushScaleAxis::None;
}

float brushAxisValue(const glm::vec3 &v, BrushScaleAxis axis)
{
    switch (axis)
    {
    case BrushScaleAxis::X: return v.x;
    case BrushScaleAxis::Y: return v.y;
    case BrushScaleAxis::Z: return v.z;
    case BrushScaleAxis::None: break;
    }
    return 0.0f;
}

int brushFaceForScaleAxis(BrushScaleAxis axis, bool positiveFace)
{
    switch (axis)
    {
    case BrushScaleAxis::X: return positiveFace ? 0 : 1;
    case BrushScaleAxis::Y: return positiveFace ? 2 : 3;
    case BrushScaleAxis::Z: return positiveFace ? 4 : 5;
    case BrushScaleAxis::None: break;
    }
    return 2;
}

BrushVolume scaleBrushFaceAlongAxis(const BrushVolume &original,
                                    BrushScaleAxis axis,
                                    bool positiveFace,
                                    float delta)
{
    BrushVolume result = original;
    constexpr float minSize = 1.0f;

    switch (axis)
    {
    case BrushScaleAxis::X:
        if (positiveFace)
            result.maxs.x = glm::max(original.maxs.x + delta, original.mins.x + minSize);
        else
            result.mins.x = glm::min(original.mins.x + delta, original.maxs.x - minSize);
        break;
    case BrushScaleAxis::Y:
        if (positiveFace)
            result.maxs.y = glm::max(original.maxs.y + delta, original.mins.y + minSize);
        else
            result.mins.y = glm::min(original.mins.y + delta, original.maxs.y - minSize);
        break;
    case BrushScaleAxis::Z:
        if (positiveFace)
            result.maxs.z = glm::max(original.maxs.z + delta, original.mins.z + minSize);
        else
            result.mins.z = glm::min(original.mins.z + delta, original.maxs.z - minSize);
        break;
    case BrushScaleAxis::None:
        break;
    }

    return result;
}
