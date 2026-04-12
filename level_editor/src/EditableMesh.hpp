#pragma once

#include "Math.hpp"

#include <string>
#include <vector>

struct EditableVertex
{
    glm::vec3 position {0.0f, 0.0f, 0.0f};
};

struct EditableFace
{
    std::vector<int> indices;
    std::string materialName = "default";
};

class EditableMesh
{
public:
    static EditableMesh MakeBox(const glm::vec3& minBounds, const glm::vec3& maxBounds);
    static EditableMesh MakeHollowBox(const glm::vec3& minBounds, const glm::vec3& maxBounds, float wallThickness);
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
