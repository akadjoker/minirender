#include "EditableMesh.hpp"

EditableMesh EditableMesh::MakeBox(const glm::vec3& minBounds, const glm::vec3& maxBounds)
{
    EditableMesh mesh;
    mesh.vertices_ = {
        {{minBounds.x, minBounds.y, minBounds.z}},
        {{maxBounds.x, minBounds.y, minBounds.z}},
        {{maxBounds.x, maxBounds.y, minBounds.z}},
        {{minBounds.x, maxBounds.y, minBounds.z}},
        {{minBounds.x, minBounds.y, maxBounds.z}},
        {{maxBounds.x, minBounds.y, maxBounds.z}},
        {{maxBounds.x, maxBounds.y, maxBounds.z}},
        {{minBounds.x, maxBounds.y, maxBounds.z}},
    };

    mesh.faces_ = {
        {{0, 1, 2, 3}, "wall"},
        {{4, 5, 6, 7}, "wall"},
        {{0, 4, 5, 1}, "wall"},
        {{1, 5, 6, 2}, "wall"},
        {{2, 6, 7, 3}, "wall"},
        {{3, 7, 4, 0}, "wall"},
    };

    return mesh;
}

EditableMesh EditableMesh::FromData(const std::vector<EditableVertex>& vertices, const std::vector<EditableFace>& faces)
{
    EditableMesh mesh;
    mesh.vertices_ = vertices;
    mesh.faces_ = faces;
    return mesh;
}

void EditableMesh::setData(const std::vector<EditableVertex>& vertices, const std::vector<EditableFace>& faces)
{
    vertices_ = vertices;
    faces_ = faces;
}
