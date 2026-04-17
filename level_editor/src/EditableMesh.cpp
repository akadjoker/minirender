#include "EditableMesh.hpp"
#include <cmath>
#include <fstream>
#include <algorithm>
#include <numeric>

#include "stb_truetype.h"
#include "poly2tri.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
        {{0, 1, 2, 3}, "left"},
        {{4, 5, 6, 7}, "right"},
        {{0, 4, 5, 1}, "bottom"},
        {{1, 5, 6, 2}, "front"},
        {{2, 6, 7, 3}, "top"},
        {{3, 7, 4, 0}, "back"},
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

EditableMesh EditableMesh::MakeCylinder(const glm::vec3& center, float radius, float height, int segments)
{
    if (segments < 3) segments = 3;
    EditableMesh mesh;
    const float halfH = height * 0.5f;

    // Bottom center = 0, top center = 1
    mesh.vertices_.push_back({{center.x, center.y - halfH, center.z}});
    mesh.vertices_.push_back({{center.x, center.y + halfH, center.z}});

    // Bottom ring starts at index 2, top ring at 2 + segments
    for (int i = 0; i < segments; ++i)
    {
        const float angle = static_cast<float>(2.0 * M_PI * i / segments);
        const float x = center.x + radius * std::cos(angle);
        const float z = center.z + radius * std::sin(angle);
        mesh.vertices_.push_back({{x, center.y - halfH, z}});
    }
    for (int i = 0; i < segments; ++i)
    {
        const float angle = static_cast<float>(2.0 * M_PI * i / segments);
        const float x = center.x + radius * std::cos(angle);
        const float z = center.z + radius * std::sin(angle);
        mesh.vertices_.push_back({{x, center.y + halfH, z}});
    }

    const int botBase = 2;
    const int topBase = 2 + segments;

    // Bottom cap (fan, winding reversed for outward normal)
    EditableFace bottomCap;
    bottomCap.materialName = "bottom";
    for (int i = segments - 1; i >= 0; --i)
        bottomCap.indices.push_back(botBase + i);
    mesh.faces_.push_back(bottomCap);

    // Top cap
    EditableFace topCap;
    topCap.materialName = "top";
    for (int i = 0; i < segments; ++i)
        topCap.indices.push_back(topBase + i);
    mesh.faces_.push_back(topCap);

    // Side quads
    for (int i = 0; i < segments; ++i)
    {
        const int next = (i + 1) % segments;
        EditableFace side;
        side.materialName = "side";
        side.indices = {botBase + i, botBase + next, topBase + next, topBase + i};
        mesh.faces_.push_back(side);
    }

    return mesh;
}

EditableMesh EditableMesh::MakeSphere(const glm::vec3& center, float radius, int rings, int segments)
{
    if (rings < 2) rings = 2;
    if (segments < 3) segments = 3;
    EditableMesh mesh;

    // Top pole = 0
    mesh.vertices_.push_back({{center.x, center.y + radius, center.z}});

    // Ring vertices: ring 1..rings-1, each with 'segments' vertices
    for (int r = 1; r < rings; ++r)
    {
        const float phi = static_cast<float>(M_PI * r / rings);
        const float y = center.y + radius * std::cos(phi);
        const float ringR = radius * std::sin(phi);
        for (int s = 0; s < segments; ++s)
        {
            const float theta = static_cast<float>(2.0 * M_PI * s / segments);
            mesh.vertices_.push_back({{
                center.x + ringR * std::cos(theta),
                y,
                center.z + ringR * std::sin(theta)
            }});
        }
    }

    // Bottom pole
    const int bottomPole = static_cast<int>(mesh.vertices_.size());
    mesh.vertices_.push_back({{center.x, center.y - radius, center.z}});

    // Top cap triangles
    for (int s = 0; s < segments; ++s)
    {
        const int next = (s + 1) % segments;
        EditableFace f;
        f.materialName = "default";
        f.indices = {0, 1 + s, 1 + next};
        mesh.faces_.push_back(f);
    }

    // Middle quads
    for (int r = 0; r < rings - 2; ++r)
    {
        const int ringStart = 1 + r * segments;
        const int nextRingStart = 1 + (r + 1) * segments;
        for (int s = 0; s < segments; ++s)
        {
            const int next = (s + 1) % segments;
            EditableFace f;
            f.materialName = "default";
            f.indices = {ringStart + s, nextRingStart + s, nextRingStart + next, ringStart + next};
            mesh.faces_.push_back(f);
        }
    }

    // Bottom cap triangles
    const int lastRingStart = 1 + (rings - 2) * segments;
    for (int s = 0; s < segments; ++s)
    {
        const int next = (s + 1) % segments;
        EditableFace f;
        f.materialName = "default";
        f.indices = {lastRingStart + s, bottomPole, lastRingStart + next};
        mesh.faces_.push_back(f);
    }

    return mesh;
}

EditableMesh EditableMesh::MakePlane(const glm::vec3& center, float width, float depth, int subdivX, int subdivZ)
{
    if (subdivX < 1) subdivX = 1;
    if (subdivZ < 1) subdivZ = 1;
    EditableMesh mesh;

    const float halfW = width * 0.5f;
    const float halfD = depth * 0.5f;
    const float stepX = width / subdivX;
    const float stepZ = depth / subdivZ;

    for (int z = 0; z <= subdivZ; ++z)
    {
        for (int x = 0; x <= subdivX; ++x)
        {
            EditableVertex v;
            v.position = center + glm::vec3(-halfW + x * stepX, 0.0f, -halfD + z * stepZ);
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            v.uv = glm::vec2(static_cast<float>(x) / subdivX, static_cast<float>(z) / subdivZ);
            mesh.vertices_.push_back(v);
        }
    }

    const int cols = subdivX + 1;
    for (int z = 0; z < subdivZ; ++z)
    {
        for (int x = 0; x < subdivX; ++x)
        {
            const int bl = z * cols + x;
            const int br = bl + 1;
            const int tl = bl + cols;
            const int tr = tl + 1;
            EditableFace f;
            f.materialName = "default";
            f.indices = {bl, br, tr, tl};
            mesh.faces_.push_back(f);
        }
    }

    return mesh;
}

EditableMesh EditableMesh::MakeWedge(const glm::vec3& minBounds, const glm::vec3& maxBounds)
{
    EditableMesh mesh;
    // 6 vertices: bottom quad + top edge (ramp shape)
    mesh.vertices_ = {
        {{minBounds.x, minBounds.y, minBounds.z}}, // 0 bottom-back-left
        {{maxBounds.x, minBounds.y, minBounds.z}}, // 1 bottom-back-right
        {{maxBounds.x, minBounds.y, maxBounds.z}}, // 2 bottom-front-right
        {{minBounds.x, minBounds.y, maxBounds.z}}, // 3 bottom-front-left
        {{maxBounds.x, maxBounds.y, minBounds.z}}, // 4 top-back-right
        {{minBounds.x, maxBounds.y, minBounds.z}}, // 5 top-back-left
    };

    mesh.faces_ = {
        {{3, 2, 1, 0}, "bottom"},       // bottom
        {{0, 1, 4, 5}, "back"},          // back wall
        {{5, 4, 2, 3}, "slope"},         // slope (ramp surface)
        {{0, 5, 3}, "left"},             // left triangle
        {{1, 2, 4}, "right"},            // right triangle
    };

    return mesh;
}

EditableMesh EditableMesh::MakeStairs(const glm::vec3& minBounds, const glm::vec3& maxBounds, int steps)
{
    if (steps < 1) steps = 1;
    EditableMesh mesh;

    const glm::vec3 size = maxBounds - minBounds;
    const float stepW = size.z / steps;
    const float stepH = size.y / steps;

    for (int s = 0; s < steps; ++s)
    {
        const float z0 = minBounds.z + s * stepW;
        const float z1 = z0 + stepW;
        const float y0 = minBounds.y + s * stepH;
        const float y1 = y0 + stepH;

        const int base = static_cast<int>(mesh.vertices_.size());
        mesh.vertices_.push_back({{minBounds.x, y0, z0}}); // 0 bottom-back-left
        mesh.vertices_.push_back({{maxBounds.x, y0, z0}}); // 1 bottom-back-right
        mesh.vertices_.push_back({{maxBounds.x, y0, z1}}); // 2 bottom-front-right
        mesh.vertices_.push_back({{minBounds.x, y0, z1}}); // 3 bottom-front-left
        mesh.vertices_.push_back({{minBounds.x, y1, z0}}); // 4 top-back-left
        mesh.vertices_.push_back({{maxBounds.x, y1, z0}}); // 5 top-back-right
        mesh.vertices_.push_back({{maxBounds.x, y1, z1}}); // 6 top-front-right
        mesh.vertices_.push_back({{minBounds.x, y1, z1}}); // 7 top-front-left

        // Front face (riser)
        mesh.faces_.push_back({{base + 4, base + 5, base + 1, base + 0}, "riser"});
        // Top face (tread)
        mesh.faces_.push_back({{base + 7, base + 6, base + 5, base + 4}, "tread"});
        // Left side
        mesh.faces_.push_back({{base + 0, base + 3, base + 7, base + 4}, "side"});
        // Right side
        mesh.faces_.push_back({{base + 1, base + 5, base + 6, base + 2}, "side"});

        if (s == 0)
        {
            // Bottom of first step
            mesh.faces_.push_back({{base + 3, base + 2, base + 1, base + 0}, "bottom"});
        }
        if (s == steps - 1)
        {
            // Back of last step at the very back
            mesh.faces_.push_back({{base + 2, base + 3, base + 7, base + 6}, "back"});
        }
    }

    return mesh;
}

EditableMesh EditableMesh::MakeSpiralStairs(const glm::vec3& center, float innerRadius, float outerRadius,
                                            float height, int steps, float angleDegrees)
{
    if (steps < 1) steps = 1;
    if (innerRadius < 0.1f) innerRadius = 0.1f;
    if (outerRadius <= innerRadius) outerRadius = innerRadius + 1.0f;

    EditableMesh mesh;
    const float totalAngle = static_cast<float>(angleDegrees * M_PI / 180.0);
    const float stepAngle = totalAngle / steps;
    const float stepH = height / steps;

    for (int s = 0; s < steps; ++s)
    {
        const float a0 = s * stepAngle;
        const float a1 = (s + 1) * stepAngle;
        const float y0 = center.y + s * stepH;
        const float y1 = y0 + stepH;

        const float ci0 = std::cos(a0), si0 = std::sin(a0);
        const float ci1 = std::cos(a1), si1 = std::sin(a1);

        const int base = static_cast<int>(mesh.vertices_.size());
        // Bottom 4 vertices (y0): inner0, outer0, outer1, inner1
        mesh.vertices_.push_back({{center.x + innerRadius * ci0, y0, center.z + innerRadius * si0}});
        mesh.vertices_.push_back({{center.x + outerRadius * ci0, y0, center.z + outerRadius * si0}});
        mesh.vertices_.push_back({{center.x + outerRadius * ci1, y0, center.z + outerRadius * si1}});
        mesh.vertices_.push_back({{center.x + innerRadius * ci1, y0, center.z + innerRadius * si1}});
        // Top 4 vertices (y1): inner0, outer0, outer1, inner1
        mesh.vertices_.push_back({{center.x + innerRadius * ci0, y1, center.z + innerRadius * si0}});
        mesh.vertices_.push_back({{center.x + outerRadius * ci0, y1, center.z + outerRadius * si0}});
        mesh.vertices_.push_back({{center.x + outerRadius * ci1, y1, center.z + outerRadius * si1}});
        mesh.vertices_.push_back({{center.x + innerRadius * ci1, y1, center.z + innerRadius * si1}});

        // Top face (tread)
        mesh.faces_.push_back({{base + 4, base + 5, base + 6, base + 7}, "tread"});
        // Bottom face
        mesh.faces_.push_back({{base + 3, base + 2, base + 1, base + 0}, "bottom"});
        // Front riser (leading edge at a0)
        mesh.faces_.push_back({{base + 0, base + 1, base + 5, base + 4}, "riser"});
        // Back (trailing edge at a1)
        mesh.faces_.push_back({{base + 2, base + 3, base + 7, base + 6}, "riser"});
        // Outer wall
        mesh.faces_.push_back({{base + 1, base + 2, base + 6, base + 5}, "outer"});
        // Inner wall
        mesh.faces_.push_back({{base + 3, base + 0, base + 4, base + 7}, "inner"});
    }

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

// ── Text mesh generation helpers ──────────────────────────────────────────────

namespace {

using Vec2 = glm::vec2;

struct Contour {
    std::vector<Vec2> points;
};

// Signed area of a polygon (positive = CCW)
static float polyArea(const std::vector<Vec2>& pts)
{
    float a = 0.0f;
    const int n = static_cast<int>(pts.size());
    for (int i = 0, j = n - 1; i < n; j = i++)
        a += (pts[j].x - pts[i].x) * (pts[j].y + pts[i].y);
    return a * 0.5f;
}

// Check if contour contains a point (ray casting)
static bool contourContains(const std::vector<Vec2>& outer, Vec2 pt)
{
    int crossings = 0;
    const int n = static_cast<int>(outer.size());
    for (int i = 0, j = n - 1; i < n; j = i++)
    {
        const Vec2& a = outer[j];
        const Vec2& b = outer[i];
        if ((a.y <= pt.y && b.y > pt.y) || (b.y <= pt.y && a.y > pt.y))
        {
            const float t = (pt.y - a.y) / (b.y - a.y);
            if (pt.x < a.x + t * (b.x - a.x))
                crossings++;
        }
    }
    return (crossings & 1) != 0;
}

// Remove consecutive duplicate points (poly2tri throws on repeats)
static void removeDuplicates(std::vector<Vec2>& pts, float eps = 1e-4f)
{
    if (pts.size() < 2) return;
    std::vector<Vec2> clean;
    clean.push_back(pts[0]);
    for (size_t i = 1; i < pts.size(); ++i)
    {
        if (glm::length(pts[i] - clean.back()) > eps)
            clean.push_back(pts[i]);
    }
    // Also check last vs first
    if (clean.size() > 1 && glm::length(clean.back() - clean.front()) < eps)
        clean.pop_back();
    pts = clean;
}

} // anonymous namespace

EditableMesh EditableMesh::MakeText(const std::string& text, const std::string& fontPath, float size, float extrude, int curveQuality)
{
    EditableMesh mesh;
    if (text.empty()) return mesh;

    // Load font file
    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return mesh;
    const auto fileSize = file.tellg();
    file.seekg(0);
    std::vector<unsigned char> fontBuffer(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(fontBuffer.data()), fileSize);
    file.close();

    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, fontBuffer.data(), stbtt_GetFontOffsetForIndex(fontBuffer.data(), 0)))
        return mesh;

    const float scale = stbtt_ScaleForPixelHeight(&font, size);
    if (curveQuality < 1) curveQuality = 1;
    if (curveQuality > 10) curveQuality = 10;

    float cursorX = 0.0f;

    for (char ch : text)
    {
        if (ch == ' ')
        {
            int advW = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&font, ch, &advW, &lsb);
            cursorX += advW * scale;
            continue;
        }

        stbtt_vertex* verts = nullptr;
        const int numVerts = stbtt_GetCodepointShape(&font, static_cast<int>(ch), &verts);
        if (numVerts == 0) continue;

        // Extract contours from glyph shape
        std::vector<Contour> contours;
        Contour current;

        for (int i = 0; i < numVerts; ++i)
        {
            const float px = verts[i].x * scale + cursorX;
            const float py = verts[i].y * scale;

            if (verts[i].type == STBTT_vmove)
            {
                if (current.points.size() >= 3)
                    contours.push_back(current);
                current.points.clear();
                current.points.push_back({px, py});
            }
            else if (verts[i].type == STBTT_vline)
            {
                current.points.push_back({px, py});
            }
            else if (verts[i].type == STBTT_vcurve)
            {
                const Vec2 p0 = current.points.back();
                const Vec2 cp = {verts[i].cx * scale + cursorX, verts[i].cy * scale};
                const Vec2 p1 = {px, py};
                for (int s = 1; s <= curveQuality; ++s)
                {
                    const float t = static_cast<float>(s) / curveQuality;
                    const float u = 1.0f - t;
                    const Vec2 pt = u * u * p0 + 2.0f * u * t * cp + t * t * p1;
                    current.points.push_back(pt);
                }
            }
            else if (verts[i].type == STBTT_vcubic)
            {
                const Vec2 p0 = current.points.back();
                const Vec2 c1 = {verts[i].cx * scale + cursorX, verts[i].cy * scale};
                const Vec2 c2 = {verts[i].cx1 * scale + cursorX, verts[i].cy1 * scale};
                const Vec2 p1 = {px, py};
                for (int s = 1; s <= curveQuality; ++s)
                {
                    const float t = static_cast<float>(s) / curveQuality;
                    const float u = 1.0f - t;
                    const Vec2 pt = u*u*u*p0 + 3.0f*u*u*t*c1 + 3.0f*u*t*t*c2 + t*t*t*p1;
                    current.points.push_back(pt);
                }
            }
        }
        if (current.points.size() >= 3)
            contours.push_back(current);

        stbtt_FreeShape(&font, verts);

        // Advance cursor
        int advW = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&font, static_cast<int>(ch), &advW, &lsb);
        cursorX += advW * scale;

        if (contours.empty()) continue;

        // Clean up contours (remove duplicate points)
        for (auto& c : contours)
            removeDuplicates(c.points);

        // Classify contours: outer (negative area in stb Y-up) vs holes
        std::vector<int> outerIdx, holeIdx;
        for (int c = 0; c < static_cast<int>(contours.size()); ++c)
        {
            if (contours[c].points.size() < 3) continue;
            if (polyArea(contours[c].points) < 0.0f)
                outerIdx.push_back(c);
            else
                holeIdx.push_back(c);
        }

        // For each outer contour, triangulate with poly2tri (supports holes natively)
        for (int oi : outerIdx)
        {
            auto& outerPts = contours[oi].points;
            if (outerPts.size() < 3) continue;

            // Ensure outer contour is CCW (negative polyArea = CW in our convention, need to reverse)
            if (polyArea(outerPts) < 0.0f)
                std::reverse(outerPts.begin(), outerPts.end());

            // Allocate p2t::Point storage (poly2tri takes raw pointers, we own the memory)
            std::vector<std::vector<p2t::Point>> p2tStorage;
            p2tStorage.emplace_back();
            auto& outerP2t = p2tStorage.back();
            outerP2t.reserve(outerPts.size());
            for (const auto& p : outerPts)
                outerP2t.emplace_back(static_cast<double>(p.x), static_cast<double>(p.y));

            std::vector<p2t::Point*> outerPolyline;
            for (auto& p : outerP2t)
                outerPolyline.push_back(&p);

            // Collect holes for this outer contour
            std::vector<std::vector<p2t::Point*>> holePolylines;
            for (int hi : holeIdx)
            {
                auto& holePts = contours[hi].points;
                if (holePts.size() < 3) continue;
                if (!contourContains(outerPts, holePts[0])) continue;

                // Holes must be CW
                if (polyArea(holePts) > 0.0f)
                    std::reverse(holePts.begin(), holePts.end());

                p2tStorage.emplace_back();
                auto& holeP2t = p2tStorage.back();
                holeP2t.reserve(holePts.size());
                for (const auto& p : holePts)
                    holeP2t.emplace_back(static_cast<double>(p.x), static_cast<double>(p.y));

                holePolylines.emplace_back();
                for (auto& p : holeP2t)
                    holePolylines.back().push_back(&p);
            }

            // Triangulate with poly2tri
            try
            {
                p2t::CDT cdt(outerPolyline);
                for (auto& hole : holePolylines)
                    cdt.AddHole(hole);
                cdt.Triangulate();

                auto triangles = cdt.GetTriangles();

                // Build a point -> vertex index map
                // Collect all unique points first
                std::vector<Vec2> allPoly;
                for (const auto& p : outerPts)
                    allPoly.push_back(p);
                for (int hi : holeIdx)
                {
                    auto& holePts = contours[hi].points;
                    if (holePts.size() < 3 || !contourContains(outerPts, holePts[0])) continue;
                    for (const auto& p : holePts)
                        allPoly.push_back(p);
                }

                const int baseVtx = static_cast<int>(mesh.vertices_.size());

                // Front face vertices (Z = 0)
                for (const auto& p : allPoly)
                {
                    EditableVertex v;
                    v.position = glm::vec3(p.x, p.y, 0.0f);
                    v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                    v.uv = glm::vec2(p.x / size, p.y / size);
                    mesh.vertices_.push_back(v);
                }

                // Map p2t::Point* to vertex index by position lookup
                auto findVtxIndex = [&](const p2t::Point* pt) -> int {
                    const float px = static_cast<float>(pt->x);
                    const float py = static_cast<float>(pt->y);
                    for (int i = 0; i < static_cast<int>(allPoly.size()); ++i)
                    {
                        if (std::fabs(allPoly[i].x - px) < 1e-4f && std::fabs(allPoly[i].y - py) < 1e-4f)
                            return baseVtx + i;
                    }
                    // Steiner point — add new vertex
                    EditableVertex v;
                    v.position = glm::vec3(px, py, 0.0f);
                    v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                    v.uv = glm::vec2(px / size, py / size);
                    mesh.vertices_.push_back(v);
                    allPoly.push_back({px, py});
                    return static_cast<int>(mesh.vertices_.size()) - 1;
                };

                // Front face triangles
                for (auto* tri : triangles)
                {
                    const int i0 = findVtxIndex(tri->GetPoint(0));
                    const int i1 = findVtxIndex(tri->GetPoint(1));
                    const int i2 = findVtxIndex(tri->GetPoint(2));
                    EditableFace f;
                    f.materialName = "default";
                    f.indices = {i0, i1, i2};
                    mesh.faces_.push_back(f);
                }

                if (extrude > 0.001f)
                {
                    const int backBase = static_cast<int>(mesh.vertices_.size());
                    const int polyCount = static_cast<int>(allPoly.size());

                    // Back face vertices (Z = -extrude)
                    for (const auto& p : allPoly)
                    {
                        EditableVertex v;
                        v.position = glm::vec3(p.x, p.y, -extrude);
                        v.normal = glm::vec3(0.0f, 0.0f, -1.0f);
                        v.uv = glm::vec2(p.x / size, p.y / size);
                        mesh.vertices_.push_back(v);
                    }

                    // Back face triangles (reversed winding)
                    for (auto* tri : triangles)
                    {
                        const int i0 = findVtxIndex(tri->GetPoint(0));
                        const int i1 = findVtxIndex(tri->GetPoint(1));
                        const int i2 = findVtxIndex(tri->GetPoint(2));
                        // Offset to back vertices
                        const int b0 = i0 - baseVtx + backBase;
                        const int b1 = i1 - baseVtx + backBase;
                        const int b2 = i2 - baseVtx + backBase;
                        EditableFace f;
                        f.materialName = "default";
                        f.indices = {b2, b1, b0};
                        mesh.faces_.push_back(f);
                    }

                    // Side faces — only for the outer contour and hole contour edges
                    auto addSideFaces = [&](const std::vector<Vec2>& contourPts) {
                        const int cn = static_cast<int>(contourPts.size());
                        for (int i = 0; i < cn; ++i)
                        {
                            const int j = (i + 1) % cn;
                            const Vec2& a = contourPts[i];
                            const Vec2& b = contourPts[j];
                            const glm::vec2 edge = glm::normalize(glm::vec2(b.x - a.x, b.y - a.y));
                            const glm::vec3 sideN = glm::normalize(glm::vec3(edge.y, -edge.x, 0.0f));

                            const int si = static_cast<int>(mesh.vertices_.size());
                            mesh.vertices_.push_back({glm::vec3(a.x, a.y, 0.0f),       sideN, {0,0}});
                            mesh.vertices_.push_back({glm::vec3(b.x, b.y, 0.0f),       sideN, {1,0}});
                            mesh.vertices_.push_back({glm::vec3(b.x, b.y, -extrude),   sideN, {1,1}});
                            mesh.vertices_.push_back({glm::vec3(a.x, a.y, -extrude),   sideN, {0,1}});

                            EditableFace f;
                            f.materialName = "default";
                            f.indices = {si, si+1, si+2, si+3};
                            mesh.faces_.push_back(f);
                        }
                    };

                    addSideFaces(outerPts);
                    for (int hi : holeIdx)
                    {
                        auto& holePts = contours[hi].points;
                        if (holePts.size() < 3 || !contourContains(outerPts, holePts[0])) continue;
                        addSideFaces(holePts);
                    }
                }
            }
            catch (...)
            {
                // poly2tri can throw on degenerate input — skip this contour
                continue;
            }
        }
    }

    return mesh;
}
