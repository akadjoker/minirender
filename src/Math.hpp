#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <limits>

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
//  Frustum
// ============================================================
struct Frustum
{
    Plane planes[6]; // left, right, bottom, top, near, far

    static Frustum from_matrix(const glm::mat4 &vp);

    bool contains(const BoundingBox &aabb) const;
    bool contains(const Sphere &sphere) const;
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
