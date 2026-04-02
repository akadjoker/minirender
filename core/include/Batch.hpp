#pragma once
#include <vector>
#include "Config.hpp"
#include <glm/glm.hpp>
#include "Math.hpp"
#include "glad/glad.h"

class Color;
struct Texture;
class Shader;

struct TexVertex
{
    glm::vec2 position;
    glm::vec2 texCoord;
};

struct HermitePoint
{
    glm::vec2 position;
    glm::vec2 tangent;
};

struct BatchVertexBuffer
{
    int elementCount;
    std::vector<float> vertices;
    std::vector<float> texcoords;
    std::vector<unsigned char> colors;
    std::vector<unsigned int> indices;
    unsigned int vaoId;
    unsigned int vboId[4];
};

struct DrawCall
{
    int mode;
    int vertexCount;
    int vertexAlignment;
    unsigned int textureId;
};

class RenderBatch
{
public:
    RenderBatch();
    virtual ~RenderBatch();

    void Release();

    void Init(int numBuffers = 1, int bufferElements = 10000);

    void SetColor(const Color &color);
    void SetColor(float r, float g, float b);

    void SetColor(u8 r, u8 g, u8 b, u8 a);

    void SetAlpha(float a);

    void Line2D(int startPosX, int startPosY, int endPosX, int endPosY);
    void Line2D(const glm::vec2 &start, const glm::vec2 &end);

    void Line3D(const glm::vec3 &start, const glm::vec3 &end);
    void Line3D(float startX, float startY, float startZ, float endX, float endY, float endZ);

    void Circle(int centerX, int centerY, float radius, bool fill = false);
    void Rectangle(int posX, int posY, int width, int height, bool fill = false);

    void Triangle(float x1, float y1, float x2, float y2, float x3, float y3, bool fill = true);
    void Ellipse(int centerX, int centerY, float radiusX, float radiusY, bool fill = true);

    void Polygon(int centerX, int centerY, int sides, float radius, float rotation = 0.0f, bool fill = true);
    void Polyline(const glm::vec2 *points, int pointCount);

    void RoundedRectangle(int posX, int posY, int width, int height, float roundness, int segments = 8, bool fill = true);
    void CircleSector(int centerX, int centerY, float radius, float startAngle, float endAngle, int segments = 16, bool fill = true);
    void Grid(int posX, int posY, int width, int height, int cellWidth, int cellHeight);

    void TexturedPolygon(const glm::vec2 *points, int pointCount, unsigned int textureId);
    void TexturedPolygonCustomUV(const TexVertex *vertices, int vertexCount, unsigned int textureId);
    void TexturedQuad(const glm::vec2 &p1, const glm::vec2 &p2, const glm::vec2 &p3, const glm::vec2 &p4, unsigned int textureId);
    void TexturedQuad(const glm::vec2 &p1, const glm::vec2 &p2, const glm::vec2 &p3, const glm::vec2 &p4, const glm::vec2 &uv1, const glm::vec2 &uv2, const glm::vec2 &uv3, const glm::vec2 &uv4, unsigned int textureId);
    void TexturedTriangle(const glm::vec2 &p1, const glm::vec2 &p2, const glm::vec2 &p3, unsigned int textureId);
    void TexturedTriangle(const glm::vec2 &p1, const glm::vec2 &p2, const glm::vec2 &p3, const glm::vec2 &uv1, const glm::vec2 &uv2, const glm::vec2 &uv3, unsigned int textureId);

    void BezierQuadratic(const glm::vec2 &p0, const glm::vec2 &p1, const glm::vec2 &p2, int segments = 20);
    void BezierCubic(const glm::vec2 &p0, const glm::vec2 &p1, const glm::vec2 &p2, const glm::vec2 &p3, int segments = 30);
    void CatmullRomSpline(const glm::vec2 *points, int pointCount, int segments = 20);
    void BSpline(const glm::vec2 *points, int pointCount, int segments = 20, int degree = 3);
    void HermiteSpline(const HermitePoint *points, int pointCount, int segments = 20);

    // Spline com espessura
    void ThickSpline(const glm::vec2 *points, int pointCount, float thickness, int segments = 20);

    void Box(const BoundingBox &box);
    void Box(const BoundingBox &box, const glm::mat4 &transform);

    void Circle3D(const glm::vec3& center, float radius, const glm::vec3& normal, int segments);
    void CircleXZ(const glm::vec3& center, float radius, int segments);

    void Cube(const glm::vec3 &position, float width, float height, float depth, bool wire = true);
    void Sphere(const glm::vec3 &position, float radius, int rings, int slices, bool wire = true);
    void Cone(const glm::vec3 &position, float radius, float height, int segments, bool wire);
    void Cylinder(const glm::vec3 &position, float radius, float height, int segments, bool wire);
    void Capsule(const glm::vec3 &position, float radius, float height, int segments, bool wire);

    void TriangleLines(const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3);

    void Triangle(const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3);
    void Triangle(const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3, const glm::vec2 &t1, const glm::vec2 &t2, const glm::vec2 &t3);

    void Grid(int slices, float spacing, bool axes = true);

    void Quad(const glm::vec2 *coords, const glm::vec2 *texcoords);
    void Quad(Texture *texture, const glm::vec2 *coords, const glm::vec2 *texcoords);
    void Quad(Texture *texture, float x, float y, float width, float height);
    void Quad(Texture *texture, const FloatRect &src, float x, float y, float width, float height);
    void Quad(u32 texture, float x, float y, float width, float height);
    void Quad(Texture *texture, float x1, float y1, float x2, float y2, const FloatRect &src);
    void QuadCentered(Texture *texture, float x, float y, float size, const FloatRect &clip);

    void DrawQuad(float x1, float y1, float x2, float y2,
                  float u0, float v0, float u1, float v1);
    
    void DrawQuad(Texture* texture,
                  float x1, float y1, float x2, float y2,
                  float u0, float v0, float u1, float v1);
    
    void DrawQuad(float x1, float y1, float x2, float y2,
                  float u0, float v0, float u1, float v1,
                  const Color& color);

    void BeginTransform(const glm::mat4 &transform);
    void EndTransform();

    void Render();

    void SetMode(int mode);

    void Vertex2f(float x, float y);
    void Vertex3f(float x, float y, float z);
    void TexCoord2f(float x, float y);

    void SetTexture(unsigned int id);

    void SetTexture(Texture *texture);

    void SetMatrix(const glm::mat4 &matrix);

private:
    bool CheckRenderBatchLimit(int vCount);

    int bufferCount;
    int currentBuffer;
    int drawCounter;
    float currentDepth;
    int vertexCounter;
    s32 defaultTextureId;
    bool use_matrix;
    glm::mat4 modelMatrix;
    glm::mat4 viewMatrix;

    Texture *m_defaultTexture;

    std::vector<DrawCall *> draws;
    std::vector<BatchVertexBuffer *> vertexBuffer;

    float texcoordx, texcoordy;
    u8 colorr, colorg, colorb, colora;
    Shader *shader;

private:
};
