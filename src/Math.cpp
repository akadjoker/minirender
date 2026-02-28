#include "Math.hpp"
#include <glm/gtc/matrix_transform.hpp>

// ============================================================
//  AABB
// ============================================================
void BoundingBox::expand(const glm::vec3 &p)
{
    min = glm::min(min, p);
    max = glm::max(max, p);
}

void BoundingBox::expand(const BoundingBox &other)
{
    min = glm::min(min, other.min);
    max = glm::max(max, other.max);
}

glm::vec3 BoundingBox::center() const { return (min + max) * 0.5f; }
glm::vec3 BoundingBox::extents() const { return (max - min) * 0.5f; }
glm::vec3 BoundingBox::size() const { return max - min; }

bool BoundingBox::is_valid() const
{
    return min.x <= max.x && min.y <= max.y && min.z <= max.z;
}

bool BoundingBox::contains(const glm::vec3 &p) const
{
    return p.x >= min.x && p.x <= max.x &&
           p.y >= min.y && p.y <= max.y &&
           p.z >= min.z && p.z <= max.z;
}

bool BoundingBox::intersects(const BoundingBox &other) const
{
    return min.x <= other.max.x && max.x >= other.min.x &&
           min.y <= other.max.y && max.y >= other.min.y &&
           min.z <= other.max.z && max.z >= other.min.z;
}

float BoundingBox::intersects_ray(const glm::vec3 &origin, const glm::vec3 &dir) const
{
    glm::vec3 inv = 1.0f / dir;
    glm::vec3 t0 = (min - origin) * inv;
    glm::vec3 t1 = (max - origin) * inv;
    glm::vec3 tmin = glm::min(t0, t1);
    glm::vec3 tmax = glm::max(t0, t1);
    float enter = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
    float exit = glm::min(glm::min(tmax.x, tmax.y), tmax.z);
    return (exit >= enter && exit >= 0.0f) ? enter : -1.0f;
}

BoundingBox BoundingBox::transformed(const glm::mat4 &m) const
{
    glm::vec3 corners[8] = {
        {min.x, min.y, min.z},
        {max.x, min.y, min.z},
        {min.x, max.y, min.z},
        {max.x, max.y, min.z},
        {min.x, min.y, max.z},
        {max.x, min.y, max.z},
        {min.x, max.y, max.z},
        {max.x, max.y, max.z},
    };
    BoundingBox result;
    for (auto &c : corners)
        result.expand(glm::vec3(m * glm::vec4(c, 1.0f)));
    return result;
}

// ============================================================
//  Sphere
// ============================================================
bool Sphere::contains(const glm::vec3 &p) const
{
    return glm::length2(p - center) <= radius * radius;
}

bool Sphere::intersects(const Sphere &other) const
{
    float r = radius + other.radius;
    return glm::length2(center - other.center) <= r * r;
}

bool Sphere::intersects(const BoundingBox &aabb) const
{
    glm::vec3 closest = glm::clamp(center, aabb.min, aabb.max);
    return glm::length2(center - closest) <= radius * radius;
}

Sphere Sphere::from_aabb(const BoundingBox &aabb)
{
    return {aabb.center(), glm::length(aabb.extents())};
}

// ============================================================
//  Plane
// ============================================================
Plane::Plane(const glm::vec3 &n, float d_val)
{
    float len = glm::length(n);
    normal = n / len;
    d = d_val / len;
}

Plane::Plane(const glm::vec3 &n, const glm::vec3 &point)
    : normal(glm::normalize(n)), d(-glm::dot(glm::normalize(n), point)) {}

float Plane::distance(const glm::vec3 &p) const
{
    return glm::dot(normal, p) + d;
}

// ============================================================
//  Frustum
// ============================================================
Frustum Frustum::from_matrix(const glm::mat4 &vp)
{
    Frustum f;
    f.planes[0] = Plane(glm::vec3(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0]), vp[3][3] + vp[3][0]);
    f.planes[1] = Plane(glm::vec3(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0]), vp[3][3] - vp[3][0]);
    f.planes[2] = Plane(glm::vec3(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1]), vp[3][3] + vp[3][1]);
    f.planes[3] = Plane(glm::vec3(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1]), vp[3][3] - vp[3][1]);
    f.planes[4] = Plane(glm::vec3(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2]), vp[3][3] + vp[3][2]);
    f.planes[5] = Plane(glm::vec3(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2]), vp[3][3] - vp[3][2]);
    return f;
}

bool Frustum::contains(const BoundingBox &aabb) const
{
    for (auto &p : planes)
    {
        glm::vec3 positive = {
            p.normal.x >= 0 ? aabb.max.x : aabb.min.x,
            p.normal.y >= 0 ? aabb.max.y : aabb.min.y,
            p.normal.z >= 0 ? aabb.max.z : aabb.min.z,
        };
        if (p.distance(positive) < 0)
            return false;
    }
    return true;
}

bool Frustum::contains(const Sphere &s) const
{
    for (auto &p : planes)
        if (p.distance(s.center) < -s.radius)
            return false;
    return true;
}

// ============================================================
//  Ray
// ============================================================
Ray::Ray(const glm::vec3 &o, const glm::vec3 &d)
    : origin(o), direction(glm::normalize(d)) {}

glm::vec3 Ray::at(float t) const
{
    return origin + direction * t;
}

Ray Ray::from_screen(const glm::vec2 &screen_pos,
                     const glm::vec2 &screen_size,
                     const glm::mat4 &view,
                     const glm::mat4 &proj)
{
    glm::vec4 ndc = {
        (screen_pos.x / screen_size.x) * 2.0f - 1.0f,
        1.0f - (screen_pos.y / screen_size.y) * 2.0f,
        -1.0f, 1.0f};
    glm::mat4 inv = glm::inverse(proj * view);
    glm::vec4 world = inv * ndc;
    world /= world.w;
    glm::vec3 eye = glm::vec3(glm::inverse(view)[3]);
    return Ray(eye, glm::vec3(world) - eye);
}
