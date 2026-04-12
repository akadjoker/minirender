#pragma once

#include <vector>
#include <string>
#include <functional>
#include <glm/glm.hpp>

#include "EditorData.hpp"

// ─────────────────────────────────────────────
//  Operações CSG
// ─────────────────────────────────────────────
enum class CSGOperation
{
    Add,        // adiciona brush à cena
    Subtract,   // cria buraco nos brushes existentes
    Intersect,  // mantém só a parte em comum
    Hollow      // torna brush oco (paredes + subtract interior)
};

// ─────────────────────────────────────────────
//  Resultado de uma operação CSG
// ─────────────────────────────────────────────
struct CSGResult
{
    std::vector<BrushVolume> brushes;  // brushes resultantes
    int                      removed = 0; // quantos brushes foram removidos/substituídos
    int                      added   = 0; // quantos brushes foram adicionados
};

// ─────────────────────────────────────────────
//  CSG — funções principais
// ─────────────────────────────────────────────
namespace CSG
{
    // Verifica se dois AABBs se intersectam
    inline bool intersects(const BrushVolume& A, const BrushVolume& B)
    {
        return !(A.maxs.x <= B.mins.x || A.mins.x >= B.maxs.x ||
                 A.maxs.y <= B.mins.y || A.mins.y >= B.maxs.y ||
                 A.maxs.z <= B.mins.z || A.mins.z >= B.maxs.z);
    }

    // ─────────────────────────────────────────
    //  SUBTRACT — corta A removendo a parte de B
    //  Devolve até 6 brushes que formam A\B
    // ─────────────────────────────────────────
    inline std::vector<BrushVolume> subtract(const BrushVolume& A,
                                              const BrushVolume& B)
    {
        std::vector<BrushVolume> result;

        if (!intersects(A, B))
        {
            result.push_back(A);
            return result;
        }

        // Overlap real entre A e B
        const float ox0 = glm::max(A.mins.x, B.mins.x);
        const float ox1 = glm::min(A.maxs.x, B.maxs.x);
        const float oy0 = glm::max(A.mins.y, B.mins.y);
        const float oy1 = glm::min(A.maxs.y, B.maxs.y);

        // Lambda para adicionar um pedaço se válido
        auto addPiece = [&](glm::vec3 mn, glm::vec3 mx)
        {
            if ((mx.x - mn.x) > 1e-4f &&
                (mx.y - mn.y) > 1e-4f &&
                (mx.z - mn.z) > 1e-4f)
            {
                BrushVolume piece = A;
                piece.mins  = mn;
                piece.maxs  = mx;
                piece.dirty = true;
                result.push_back(piece);
            }
        };

        // Fatia ESQUERDA  (-X de B)
        addPiece(A.mins,
                 glm::vec3(glm::min(B.mins.x, A.maxs.x), A.maxs.y, A.maxs.z));

        // Fatia DIREITA   (+X de B)
        addPiece(glm::vec3(glm::max(B.maxs.x, A.mins.x), A.mins.y, A.mins.z),
                 A.maxs);

        // Fatia BAIXO     (-Y de B)  — só na coluna X do overlap
        addPiece(glm::vec3(ox0, A.mins.y, A.mins.z),
                 glm::vec3(ox1, glm::min(B.mins.y, A.maxs.y), A.maxs.z));

        // Fatia CIMA      (+Y de B)
        addPiece(glm::vec3(ox0, glm::max(B.maxs.y, A.mins.y), A.mins.z),
                 glm::vec3(ox1, A.maxs.y, A.maxs.z));

        // Fatia FRENTE    (-Z de B)  — só no overlap XY
        addPiece(glm::vec3(ox0, oy0, A.mins.z),
                 glm::vec3(ox1, oy1, glm::min(B.mins.z, A.maxs.z)));

        // Fatia TRÁS      (+Z de B)
        addPiece(glm::vec3(ox0, oy0, glm::max(B.maxs.z, A.mins.z)),
                 glm::vec3(ox1, oy1, A.maxs.z));

        return result;
    }

    // ─────────────────────────────────────────
    //  INTERSECT — mantém só a parte em comum
    // ─────────────────────────────────────────
    inline std::vector<BrushVolume> intersect(const BrushVolume& A,
                                               const BrushVolume& B)
    {
        std::vector<BrushVolume> result;
        if (!intersects(A, B))
            return result;

        BrushVolume piece = A;
        piece.mins  = glm::max(A.mins, B.mins);
        piece.maxs  = glm::min(A.maxs, B.maxs);
        piece.dirty = true;

        if (piece.isValid())
            result.push_back(piece);

        return result;
    }

    // ─────────────────────────────────────────
    //  HOLLOW — torna um brush oco
    //  Cria 6 paredes à volta de um interior vazio
    // ─────────────────────────────────────────
    inline std::vector<BrushVolume> hollow(const BrushVolume& A,
                                            float wallThickness)
    {
        std::vector<BrushVolume> result;

        const float t = glm::max(wallThickness, 1.0f);

        // Verifica que o brush é suficientemente grande
        if ((A.maxs.x - A.mins.x) < t * 2.0f + 1.0f ||
            (A.maxs.y - A.mins.y) < t * 2.0f + 1.0f ||
            (A.maxs.z - A.mins.z) < t * 2.0f + 1.0f)
        {
            result.push_back(A); // demasiado pequeno para hollow
            return result;
        }

        // Interior a subtrair
        BrushVolume inner = A;
        inner.mins = A.mins + glm::vec3(t);
        inner.maxs = A.maxs - glm::vec3(t);

        // Usa subtract para criar os 6 lados
        return subtract(A, inner);
    }

    // ─────────────────────────────────────────
    //  SPLIT — divide um brush num plano
    //  axis: 0=X, 1=Y, 2=Z   position: coordenada do corte
    // ─────────────────────────────────────────
    inline std::vector<BrushVolume> split(const BrushVolume& A,
                                           int axis,
                                           float position)
    {
        std::vector<BrushVolume> result;

        // Clamp para dentro do brush
        const float mn = A.mins[axis];
        const float mx = A.maxs[axis];

        if (position <= mn + 1e-4f || position >= mx - 1e-4f)
        {
            result.push_back(A);
            return result;
        }

        // Parte A (antes do corte)
        BrushVolume partA = A;
        partA.maxs[axis] = position;
        partA.dirty = true;
        if (partA.isValid())
            result.push_back(partA);

        // Parte B (depois do corte)
        BrushVolume partB = A;
        partB.mins[axis] = position;
        partB.dirty = true;
        if (partB.isValid())
            result.push_back(partB);

        return result;
    }

    // ─────────────────────────────────────────
    //  SPLIT NO MEIO — divide brush ao meio
    //  axis: 0=X, 1=Y, 2=Z
    // ─────────────────────────────────────────
    inline std::vector<BrushVolume> splitMiddle(const BrushVolume& A, int axis)
    {
        const float mid = (A.mins[axis] + A.maxs[axis]) * 0.5f;
        return split(A, axis, mid);
    }

    // ─────────────────────────────────────────
    //  Aplica CSG a toda a cena
    // ─────────────────────────────────────────
    inline CSGResult applyToScene(std::vector<BrushVolume>& scene,
                                   const BrushVolume&        tool,
                                   CSGOperation              op,
                                   float                     hollowThickness = 16.0f)
    {
        CSGResult res;

        switch (op)
        {
        // ── ADD ────────────────────────────────
        case CSGOperation::Add:
        {
            BrushVolume b  = tool;
            b.dirty        = true;
            scene.push_back(b);
            res.added      = 1;
            break;
        }

        // ── SUBTRACT ───────────────────────────
        case CSGOperation::Subtract:
        {
            std::vector<BrushVolume> newScene;
            for (auto& brush : scene)
            {
                if (!intersects(brush, tool))
                {
                    newScene.push_back(brush);
                    continue;
                }
                res.removed++;
                auto pieces = subtract(brush, tool);
                res.added += (int)pieces.size();
                for (auto& p : pieces)
                    newScene.push_back(p);
            }
            scene = std::move(newScene);
            break;
        }

        // ── INTERSECT ──────────────────────────
        case CSGOperation::Intersect:
        {
            std::vector<BrushVolume> newScene;
            for (auto& brush : scene)
            {
                auto pieces = intersect(brush, tool);
                res.removed++;
                res.added += (int)pieces.size();
                for (auto& p : pieces)
                    newScene.push_back(p);
            }
            scene = std::move(newScene);
            break;
        }

        // ── HOLLOW ─────────────────────────────
        case CSGOperation::Hollow:
        {
            auto walls = hollow(tool, hollowThickness);
            for (auto& w : walls)
                scene.push_back(w);
            res.added = (int)walls.size();
            break;
        }
        }

        return res;
    }

} // namespace CSG