#pragma once

#include "Math.hpp"

#include <string>
#include <vector>

enum class UvProjection : int
{
    Box = 0,
    Planar,
    Cylindrical,
    Spherical,
    Mesh
};

struct EditableVertex
{
    glm::vec3 position {0.0f, 0.0f, 0.0f};
    glm::vec3 normal   {0.0f, 1.0f, 0.0f};
    glm::vec2 uv       {0.0f, 0.0f};
};

struct EditableFace
{
    std::vector<int> indices;
    std::string materialName = "default";
    glm::vec2 uvOffset {0.0f, 0.0f};
    glm::vec2 uvScale  {1.0f, 1.0f};
    float     uvRotation = 0.0f;
    UvProjection uvProjection = UvProjection::Box;
};

class EditableMesh
{
public:
    static EditableMesh MakeBox(const glm::vec3& minBounds, const glm::vec3& maxBounds);
    static EditableMesh MakeHollowBox(const glm::vec3& minBounds, const glm::vec3& maxBounds, float wallThickness);
    static EditableMesh MakeRoom(const glm::vec3& minBounds, const glm::vec3& maxBounds, float wallThickness);
    static EditableMesh MakeSector(const glm::vec3& minBounds, const glm::vec3& maxBounds, float wallThickness,
                                   bool left, bool right, bool top, bool bottom, bool front, bool back);
    static EditableMesh MakeCylinder(const glm::vec3& center, float radius, float height, int segments);
    static EditableMesh MakeSphere(const glm::vec3& center, float radius, int rings, int segments);
    static EditableMesh MakePlane(const glm::vec3& center, float width, float depth, int subdivX, int subdivZ);
    static EditableMesh MakeWedge(const glm::vec3& minBounds, const glm::vec3& maxBounds);
    static EditableMesh MakeStairs(const glm::vec3& minBounds, const glm::vec3& maxBounds, int steps);
    static EditableMesh MakeSpiralStairs(const glm::vec3& center, float innerRadius, float outerRadius, float height, int steps, float angleDegrees);
    static EditableMesh MakeText(const std::string& text, const std::string& fontPath, float size, float extrude, int curveQuality);
    static EditableMesh FromData(const std::vector<EditableVertex>& vertices, const std::vector<EditableFace>& faces);

    const std::vector<EditableVertex>& vertices() const { return vertices_; }
    const std::vector<EditableFace>& faces() const { return faces_; }
    std::vector<EditableVertex>& verticesMutable() { return vertices_; }
    std::vector<EditableFace>& facesMutable() { return faces_; }
    void setData(const std::vector<EditableVertex>& vertices, const std::vector<EditableFace>& faces);

    std::size_t vertexCount() const { return vertices_.size(); }
    std::size_t faceCount() const { return faces_.size(); }

private:
    std::vector<EditableVertex> vertices_;
    std::vector<EditableFace> faces_;
};
