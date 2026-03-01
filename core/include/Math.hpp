#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <limits>

#pragma once

#define SMALL_FLOAT 0.000001f

const unsigned int MaxUInt32 = 0xFFFFFFFF;
const int MinInt32 = 0x80000000;
const int MaxInt32 = 0x7FFFFFFF;
const float MaxFloat = 3.402823466e+38F;
const float MinPosFloat = 1.175494351e-38F;
const float DEG2RAD = M_PI / 180.0f;
const float RAD2DEG = 180.0f / M_PI;
const float Pi = 3.141592654f;
const float TwoPi = 6.283185307f;
const float PiHalf = 1.570796327f;

const double PI64 = 3.1415926535897932384626433832795028841971693993751;
const double RECIPROCAL_PI64 = 1.0 / PI64;
 
const double DEGTORAD64 = PI64 / 180.0;
const double RADTODEG64 = 180.0 / PI64;
const float ROUNDING_ERROR_f32 = 0.000001f;
const double ROUNDING_ERROR_f64 = 0.00000001;
const float M_INFINITY = 1.0e30f;
const float Epsilon = 0.000001f;
const float ZeroEpsilon = 32.0f * MinPosFloat; // Very small epsilon for checking against 0.0f

 

#define powi(base, exp) (int)powf((float)(base), (float)(exp))

#define ToRadians(x) (float)(((x) * Pi / 180.0f))
#define ToDegrees(x) (float)(((x) * 180.0f / Pi))

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; } 
inline float Sin(float a) { return sin(a * Pi / 180); }
inline float Cos(float a) { return cos(a * Pi / 180); }
inline float Tan(float a) { return tan(a * Pi / 180); }
inline float SinRad(float a) { return sin(a); }
inline float CosRad(float a) { return cos(a); }
inline float TanRad(float a) { return tan(a); }
inline float ASin(float a) { return asin(a) * 180 / Pi; }
inline float ACos(float a) { return acos(a) * 180 / Pi; }
inline float ATan(float a) { return atan(a) * 180 / Pi; }
inline float ATan2(float y, float x) { return atan2(y, x) * 180 / Pi; }
inline float ASinRad(float a) { return asin(a); }
inline float ACosRad(float a) { return acos(a); }
inline float ATanRad(float a) { return atan(a); }
inline float ATan2Rad(float y, float x) { return atan2(y, x); }
inline int Floor(float a) { return (int)(floor(a)); }
inline int Ceil(float a) { return (int)(ceil(a)); }
inline int Trunc(float a)
{
    if (a > 0)
        return Floor(a);
    else
        return Ceil(a);
}
inline int Round(float a)
{
    if (a < 0)
        return (int)(ceil(a - 0.5f));
    else
        return (int)(floor(a + 0.5f));
}
inline float Sqrt(float a)
{
    if (a > 0)
        return sqrt(a);
    else
        return 0;
}
inline float Abs(float a)
{
    if (a < 0)
        a = -a;
    return a;
}
inline int Mod(int a, int b)
{
    if (b == 0)
        return 0;
    return a % b;
}
inline float FMod(float a, float b)
{
    if (b == 0)
        return 0;
    return fmod(a, b);
}
inline float Pow(float a, float b) { return pow(a, b); }
inline int Sign(float a)
{
    if (a < 0)
        return -1;
    else if (a > 0)
        return 1;
    else
        return 0;
}
inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }
inline int Min(int a, int b) { return a < b ? a : b; }
inline int Max(int a, int b) { return a > b ? a : b; }
inline float Clamp(float a, float min, float max)
{
    if (a < min)
        a = min;
    else if (a > max)
        a = max;
    return a;
}
inline int Clamp(int a, int min, int max)
{
    if (a < min)
        a = min;
    else if (a > max)
        a = max;
    return a;
}
inline float isZero(float v, float eps = 1e-6)
{
    return std::fabs(v) <= eps;
};

inline float Reciprocal(float v, float eps = 1e-12f)
{
    return (std::fabs(v) <= eps) ? 0.0f : 1.0f / v;
}
inline bool Equals(const float a, const float b, const float tolerance = ROUNDING_ERROR_f32)
{
    return (a + tolerance >= b) && (a - tolerance <= b);
}

template <typename T>
struct Rectangle
{

    T x;
    T y;
    T width;
    T height;

    Rectangle() : x(0), y(0), width(0), height(0) {}
    Rectangle(T x, T y, T width, T height)
        : x(x), y(y), width(width), height(height)
    {
    }
    Rectangle(const Rectangle &rect)
        : x(rect.x), y(rect.y), width(rect.width), height(rect.height)
    {
    }

    void Set(T x, T y, T width, T height)
    {
        this->x = x;
        this->y = y;
        this->width = width;
        this->height = height;
    }

    void Merge(const Rectangle &rect)
    {
        T right = x + width;
        T bottom = y + height;
        T rectRight = rect.x + rect.width;
        T rectBottom = rect.y + rect.height;
        x = Min(x, rect.x);
        y = Min(y, rect.y);
        right = Max(right, rectRight);
        bottom = Max(bottom, rectBottom);
        width = right - x;
        height = bottom - y;
    }

    void Clear()
    {
        x = 0;
        y = 0;
        width = 0;
        height = 0;
    }

    bool operator==(const Rectangle &other)
    {
        return x == other.x && y == other.y && width == other.width && height == other.height;
    }
    bool operator!=(const Rectangle &other)
    {
        return !(*this == other);
    }

    Rectangle &operator=(const Rectangle &rect)
    {
        if (this == &rect)
            return *this;
        x = rect.x;
        y = rect.y;
        width = rect.width;
        height = rect.height;
        return *this;
    }
};

template <typename T>
struct Size
{
    T width;
    T height;

    Size() : width(0), height(0) {}
    Size(T w, T h) : width(w), height(h) {}
    Size(const Size &size) : width(size.width), height(size.height) {}

    Size &operator=(const Size &size)
    {
        if (this == &size)
            return *this;
        width = size.width;
        height = size.height;
        return *this;
    }
    bool operator==(const Size &other)
    {
        return width == other.width && height == other.height;
    }
    bool operator!=(const Size &other)
    {
        return !(*this == other);
    }
};

typedef Rectangle<int> IntRect;
typedef Rectangle<float> FloatRect;
typedef Size<int> IntSize;
typedef Size<float> FloatSize;


// ============================================================
//  AABB
// ============================================================
struct BoundingBox
{
    glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
    glm::vec3 max = glm::vec3(-std::numeric_limits<float>::max());

    

    void expand(const glm::vec3 &p);
    void expand(const BoundingBox &other);

    glm::vec3 center() const;
    glm::vec3 extents() const;
    glm::vec3 size() const;
    bool is_valid() const;

    bool contains(const glm::vec3 &p) const;
    bool intersects(const BoundingBox &other) const;
    float intersects_ray(const glm::vec3 &origin,
                         const glm::vec3 &dir) const;
    BoundingBox transformed(const glm::mat4 &m) const;
};

// ============================================================
//  Sphere
// ============================================================
struct Sphere
{
    glm::vec3 center = glm::vec3(0);
    float radius = 0.0f;

    bool contains(const glm::vec3 &p) const;
    bool intersects(const Sphere &other) const;
    bool intersects(const BoundingBox &aabb) const;

    static Sphere from_aabb(const BoundingBox &aabb);
};

// ============================================================
//  Plane
// ============================================================
struct Plane
{
    glm::vec3 normal = glm::vec3(0, 1, 0);
    float d = 0.0f;

    Plane() = default;
    Plane(const glm::vec3 &n, float d);
    Plane(const glm::vec3 &n, const glm::vec3 &point);

    float distance(const glm::vec3 &p) const;
};

// ============================================================
//  Triangle
// ============================================================
struct Triangle
{
    glm::vec3 v0 = glm::vec3(0.0f);
    glm::vec3 v1 = glm::vec3(0.0f);
    glm::vec3 v2 = glm::vec3(0.0f);

    glm::vec3 center() const
    {
        return (v0 + v1 + v2) / 3.0f;
    }

    glm::vec3 normal() const
    {
        const glm::vec3 n = glm::cross(v1 - v0, v2 - v0);
        const float len2 = glm::length2(n);
        if (len2 <= Epsilon * Epsilon)
            return glm::vec3(0.0f, 1.0f, 0.0f);
        return n / glm::sqrt(len2);
    }

    Plane getPlane() const
    {
        return Plane(normal(), v0);
    }

    void getBounds(glm::vec3 &outMin, glm::vec3 &outMax) const
    {
        outMin = glm::min(glm::min(v0, v1), v2);
        outMax = glm::max(glm::max(v0, v1), v2);
    }

    BoundingBox getBounds() const
    {
        BoundingBox box;
        box.expand(v0);
        box.expand(v1);
        box.expand(v2);
        return box;
    }

    bool contains(const glm::vec3 &point, float epsilon = 1e-5f) const
    {
        const glm::vec3 n = glm::cross(v1 - v0, v2 - v0);
        const float len2 = glm::length2(n);
        if (len2 <= epsilon * epsilon)
            return false;

        const float planeError = glm::dot(n, point - v0);
        if (std::fabs(planeError) > epsilon * glm::sqrt(len2))
            return false;

        const glm::vec3 c0 = glm::cross(v1 - v0, point - v0);
        const glm::vec3 c1 = glm::cross(v2 - v1, point - v1);
        const glm::vec3 c2 = glm::cross(v0 - v2, point - v2);

        return glm::dot(n, c0) >= -epsilon &&
               glm::dot(n, c1) >= -epsilon &&
               glm::dot(n, c2) >= -epsilon;
    }

    // Möller–Trumbore ray-triangle intersection.
    // Returns t > 0 on hit (distance along the ray), -1 on miss.
    float intersect_ray(const glm::vec3 &orig, const glm::vec3 &dir) const;
};

// ============================================================
//  Frustum
// ============================================================
struct Frustum
{
    Plane planes[6]; // left, right, bottom, top, near, far

    static Frustum from_matrix(const glm::mat4 &vp);
    static Frustum infinite(); // always contains everything — use for shadow gather

    bool contains(const BoundingBox &aabb) const;
    bool contains(const Sphere &sphere) const;
    bool intersectsAABB(const BoundingBox &aabb) const { return contains(aabb); }
};

// ============================================================
//  Ray
// ============================================================
struct Ray
{
    glm::vec3 origin;
    glm::vec3 direction;

    Ray(const glm::vec3 &o, const glm::vec3 &d);

    glm::vec3 at(float t) const;

    static Ray from_screen(const glm::vec2 &screen_pos,
                           const glm::vec2 &screen_size,
                           const glm::mat4 &view,
                           const glm::mat4 &proj);
};

struct PickResult
{
    bool      hit       = false;
    float     distance  = 0.f;       // distância ao longo do raio
    glm::vec3 point     = {};        // ponto de impacto world space
    glm::vec3 normal    = {};        // normal do triângulo
    int       surfaceIndex = -1;     // qual surface foi atingida
    int       triangleIndex = -1;    // índice do triângulo dentro da surface
};
 
