#pragma once

#include <string>

#include <glm/glm.hpp>

#include "EditorData.hpp"

struct EditorConvexFacePolygon
{
    int faceIndex = -1;
    std::vector<glm::vec3> vertices;
};

enum class EditorRampDirection
{
    PosX,
    NegX,
    PosZ,
    NegZ
};

EditorBrush makeBoxConvexBrush(const glm::vec3 &mins,
                               const glm::vec3 &maxs,
                               const std::string &name = std::string(),
                               const std::string &texturePath = std::string());
EditorBrush makeWedgeConvexBrush(const glm::vec3 &mins,
                                 const glm::vec3 &maxs,
                                 EditorRampDirection direction,
                                 const std::string &name = std::string(),
                                 const std::string &texturePath = std::string());
EditorBrush makeCylinderConvexBrush(const glm::vec3 &center,
                                    float radius,
                                    float height,
                                    int sides,
                                    const std::string &name = std::string(),
                                    const std::string &texturePath = std::string());
EditorBrush makeConvexBrushFromVolume(const BrushVolume &volume);
glm::vec3 convexBrushInteriorPoint(const EditorBrush &brush);
void normalizeConvexBrush(EditorBrush &brush);
bool clipConvexBrush(const EditorBrush &source,
                     const glm::vec3 &planePoint,
                     const glm::vec3 &planeNormal,
                     const std::string &texturePath,
                     EditorBrush &outBrush);
bool moveConvexBrushFace(const EditorBrush &source,
                         int faceIndex,
                         float distance,
                         EditorBrush &outBrush);
std::vector<EditorConvexFacePolygon> buildConvexFacePolygons(const EditorBrush &brush);
