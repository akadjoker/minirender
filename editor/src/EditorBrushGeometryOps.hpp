#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "EditorData.hpp"
#include "Math.hpp"

const char *brushFaceName(int faceIndex);
int faceLeft();
int faceRight();
int faceTop();
int faceBottom();
int faceFront();
int faceBack();
int defaultFaceForView(EditorViewType viewType, int fallback);
void swapBrushFaces(BrushVolume &brush, int a, int b);
void rotateBrushX90(BrushVolume &brush, int turns);
void rotateBrushY90(BrushVolume &brush, int turns);
void rotateBrushZ90(BrushVolume &brush, int turns);
void rotateBrushForView90(BrushVolume &brush, EditorViewType viewType, int turns);
int rotateFaceForView90(int faceIndex, EditorViewType viewType, int turns);
int rotationTurnsFromMouseDrag(EditorViewType viewType, const glm::vec2 &mouseDelta);
glm::vec3 randomBrushColor();
glm::vec2 projectWorldToViewPlane(const glm::vec3 &p, EditorViewType viewType);
glm::vec3 applyViewDelta(const glm::vec3 &delta, EditorViewType viewType);
BrushVolume makeBrushFromDrag(EditorViewType viewType,
                              const glm::vec3 &start,
                              const glm::vec3 &end,
                              float defaultThickness,
                              float defaultHeight,
                              const glm::vec3 &focus,
                              const std::string &texturePath);
int findBrushAtPoint(const std::vector<BrushVolume> &brushes,
                     EditorViewType viewType,
                     const glm::vec3 &worldPoint,
                     float maxDistance);
bool brushIntersectsSelectionRect(const BrushVolume &brush,
                                  EditorViewType viewType,
                                  const glm::vec3 &rectStartWorld,
                                  const glm::vec3 &rectEndWorld);
bool pickBrushWithRay(const std::vector<BrushVolume> &brushes,
                      const Ray &ray,
                      int &outBrush,
                      int &outFace,
                      float maxDistance = 1e30f);

enum class BrushScaleAxis
{
    None,
    X,
    Y,
    Z
};

BrushScaleAxis chooseBrushScaleAxis(EditorViewType viewType, const glm::vec2 &viewDelta);
float brushAxisValue(const glm::vec3 &v, BrushScaleAxis axis);
int brushFaceForScaleAxis(BrushScaleAxis axis, bool positiveFace);
BrushVolume scaleBrushFaceAlongAxis(const BrushVolume &original,
                                    BrushScaleAxis axis,
                                    bool positiveFace,
                                    float delta);
