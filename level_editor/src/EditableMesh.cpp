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

EditableMesh EditableMesh::MakeHollowBox(const glm::vec3& minBounds, const glm::vec3& maxBounds, float wallThickness)
{
    const glm::vec3 size = maxBounds - minBounds;
    const float maxThickness = std::max(0.0f, std::min(std::min(size.x, size.y), size.z) * 0.5f - 0.001f);
    const float thickness = glm::clamp(wallThickness, 0.001f, maxThickness);
    if (maxThickness <= 0.0f)
        return MakeBox(minBounds, maxBounds);

    EditableMesh mesh;
    const glm::vec3 innerMin = minBounds + glm::vec3(thickness);
    const glm::vec3 innerMax = maxBounds - glm::vec3(thickness);

    mesh.vertices_ = {
        {{minBounds.x, minBounds.y, minBounds.z}},
        {{maxBounds.x, minBounds.y, minBounds.z}},
        {{maxBounds.x, maxBounds.y, minBounds.z}},
        {{minBounds.x, maxBounds.y, minBounds.z}},
        {{minBounds.x, minBounds.y, maxBounds.z}},
        {{maxBounds.x, minBounds.y, maxBounds.z}},
        {{maxBounds.x, maxBounds.y, maxBounds.z}},
        {{minBounds.x, maxBounds.y, maxBounds.z}},

        {{innerMin.x, innerMin.y, innerMin.z}},
        {{innerMax.x, innerMin.y, innerMin.z}},
        {{innerMax.x, innerMax.y, innerMin.z}},
        {{innerMin.x, innerMax.y, innerMin.z}},
        {{innerMin.x, innerMin.y, innerMax.z}},
        {{innerMax.x, innerMin.y, innerMax.z}},
        {{innerMax.x, innerMax.y, innerMax.z}},
        {{innerMin.x, innerMax.y, innerMax.z}},
    };

    mesh.faces_ = {
        {{0, 1, 2, 3}, "wall"},
        {{4, 5, 6, 7}, "wall"},
        {{0, 4, 5, 1}, "wall"},
        {{1, 5, 6, 2}, "wall"},
        {{2, 6, 7, 3}, "wall"},
        {{3, 7, 4, 0}, "wall"},

        {{11, 10, 9, 8}, "inner"},
        {{12, 13, 14, 15}, "inner"},
        {{9, 13, 12, 8}, "inner"},
        {{10, 14, 13, 9}, "inner"},
        {{11, 15, 14, 10}, "inner"},
        {{8, 12, 15, 11}, "inner"},
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
