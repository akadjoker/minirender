#include "genesis/GenesisBrushMeshBuilder.hpp"

#include <unordered_map>

#include "Manager.hpp"

namespace mini_genesis
{
bool GenesisBrushMeshBuilder::build(const Map3dtData &map,
                                    Mesh &mesh,
                                    std::string &error) const
{
    mesh.release_materials();
    mesh.materials.clear();
    mesh.surfaces.clear();
    mesh.buffer.vertices.clear();
    mesh.buffer.indices.clear();

    std::unordered_map<std::string, int> materialByTex;
    auto getMaterial = [&](const std::string &texture) -> int
    {
        auto it = materialByTex.find(texture);
        if (it != materialByTex.end())
            return it->second;

        Material *mat = new Material();
        mat->name = texture.empty() ? "brush_default" : texture;
        mat->setCullFace(false);
        mat->setVec4("u_color", glm::vec4(0.9f, 0.9f, 0.9f, 1.0f));
        mat->setTexture("u_albedo", TextureManager::instance().getWhite());
        mat->setInt("u_hasAlbedo", 0);
        mat->setInt("u_hasLightmap", 0);

        const int idx = mesh.add_material(mat);
        materialByTex[texture] = idx;
        return idx;
    };

    std::unordered_map<int, uint32_t> surfaceStart;
    for (const Brush &brush : map.brushes)
    {
        for (const BrushFace &face : brush.faces)
        {
            if (face.points.size() < 3)
                continue;

            const int mat = getMaterial(face.texture);
            if (surfaceStart.find(mat) == surfaceStart.end())
                surfaceStart[mat] = static_cast<uint32_t>(mesh.buffer.indices.size());

            const uint32_t base = static_cast<uint32_t>(mesh.buffer.vertices.size());
            glm::vec3 n = glm::normalize(glm::cross(face.points[1] - face.points[0], face.points[2] - face.points[0]));

            for (const glm::vec3 &p : face.points)
            {
                Vertex v{};
                v.position = glm::vec3(p.x, p.z, p.y);
                v.normal = n;
                v.uv = glm::vec2(p.x * 0.01f, p.y * 0.01f);
                v.tangent = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                mesh.buffer.vertices.push_back(v);
            }

            for (uint32_t i = 1; i + 1 < face.points.size(); ++i)
            {
                mesh.buffer.indices.push_back(base);
                mesh.buffer.indices.push_back(base + i);
                mesh.buffer.indices.push_back(base + i + 1);
            }
        }
    }

    if (mesh.buffer.indices.empty())
    {
        error = "3dt sem triangulos de brush";
        return false;
    }

    for (const auto &kv : materialByTex)
    {
        const int mat = kv.second;
        const uint32_t start = surfaceStart[mat];
        uint32_t end = static_cast<uint32_t>(mesh.buffer.indices.size());

        for (const auto &next : surfaceStart)
        {
            if (next.second > start)
                end = std::min(end, next.second);
        }

        if (end > start)
            mesh.add_surface(start, end - start, mat);
    }

    mesh.upload();
    return true;
}
} // namespace mini_genesis
