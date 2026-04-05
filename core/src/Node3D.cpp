#include "Node.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

Node3D::Node3D() : Node()
{
    type = NodeType::Node3D;
}

Node3D::~Node3D()
{
   
}

Node3D *Node3D::effectiveTransformParent() const
{
    if (transformParent_)
        return transformParent_;
    return parent ? parent->asNode3D() : nullptr;
}

// ============================================================
//  Dirty propagation
// ============================================================
void Node3D::markDirty()
{
    dirty_ = true;

    for (auto *child : children)
        if (auto *c = child->asNode3D())
            c->markDirty();
}

// ============================================================
//  Matrices
// ============================================================
glm::mat4 Node3D::localMatrix() const
{
    glm::mat4 m = glm::translate(glm::mat4(1.f), position);
    m = m * glm::mat4_cast(rotation);
    m = glm::scale(m, scale);
    return m;
}

glm::mat4 Node3D::worldMatrix() const
{
    Node3D *p3d = effectiveTransformParent();
    worldCache_ = p3d ? p3d->worldMatrix() * localMatrix() : localMatrix();
    dirty_ = false;
    return worldCache_;
}

// ============================================================
//  World space queries
// ============================================================
glm::vec3 Node3D::worldPosition() const
{
    return glm::vec3(worldMatrix()[3]);
}

glm::quat Node3D::worldRotation() const
{
    if (Node3D *p3d = effectiveTransformParent())
        return p3d->worldRotation() * rotation;
    return rotation;
}

glm::vec3 Node3D::worldScale() const
{
    glm::mat4 w = worldMatrix();
    return {
        glm::length(glm::vec3(w[0])),
        glm::length(glm::vec3(w[1])),
        glm::length(glm::vec3(w[2]))};
}

// ============================================================
//  Local axes
// ============================================================
glm::vec3 Node3D::forward() const { return worldRotation() * glm::vec3(0, 0, -1); }
glm::vec3 Node3D::back() const { return worldRotation() * glm::vec3(0, 0, 1); }
glm::vec3 Node3D::right() const { return worldRotation() * glm::vec3(1, 0, 0); }
glm::vec3 Node3D::left() const { return worldRotation() * glm::vec3(-1, 0, 0); }
glm::vec3 Node3D::up() const { return worldRotation() * glm::vec3(0, 1, 0); }
glm::vec3 Node3D::down() const { return worldRotation() * glm::vec3(0, -1, 0); }

void Node3D::setLocal(glm::mat4 mat)
{
    position = glm::vec3(mat[3]);
    rotation = glm::normalize(glm::quat_cast(glm::mat3(mat)));
    scale = glm::vec3(
        glm::length(glm::vec3(mat[0])),
        glm::length(glm::vec3(mat[1])),
        glm::length(glm::vec3(mat[2])));
    markDirty();
}

void Node3D::setWorld(glm::mat4 mat)
{
    if (Node3D *p3d = effectiveTransformParent())
        setLocal(glm::inverse(p3d->worldMatrix()) * mat);
    else
        setLocal(mat);
}

// ============================================================
//  Setters
// ============================================================
void Node3D::setPosition(const glm::vec3 &p)
{
    position = p;
    markDirty();
}
void Node3D::setPosition(float x, float y, float z)
{
    position = {x, y, z};
    markDirty();
}
void Node3D::setRotation(const glm::quat &q)
{
    rotation = glm::normalize(q);
    markDirty();
}

void Node3D::setRotationEuler(const glm::vec3 &degreesPitchYawRoll)
{
    glm::vec3 radians = glm::radians(degreesPitchYawRoll);
    float pitch = radians.x;
    float yaw   = radians.y;
    float roll  = radians.z;

    float cy = std::cos(yaw   * 0.5f);
    float sy = std::sin(yaw   * 0.5f);
    float cp = std::cos(pitch * 0.5f);
    float sp = std::sin(pitch * 0.5f);
    float cr = std::cos(roll  * 0.5f);
    float sr = std::sin(roll  * 0.5f);

    rotation.w = cr * cp * cy + sr * sp * sy;
    rotation.x = cr * sp * cy + sr * cp * sy;
    rotation.y = cr * cp * sy - sr * sp * cy;
    rotation.z = sr * cp * cy - cr * sp * sy;
    rotation = glm::normalize(rotation);
    markDirty();
}

// Euler angles in DEGREES, order YXZ (same semantic order used in the public API)
// returns vec3(pitch, yaw, roll)
glm::vec3 Node3D::getEulerAngles() const
{
    glm::quat q = glm::normalize(rotation);

    float sinPitch = 2.f * (q.w * q.x - q.y * q.z);
    float pitch;
    if (std::fabs(sinPitch) >= 1.f)
        pitch = std::copysign(glm::half_pi<float>(), sinPitch);
    else
        pitch = std::asin(sinPitch);

    float sinYaw = 2.f * (q.w * q.y + q.x * q.z);
    float cosYaw = 1.f - 2.f * (q.y * q.y + q.x * q.x);
    float yaw    = std::atan2(sinYaw, cosYaw);

    float sinRoll = 2.f * (q.w * q.z + q.x * q.y);
    float cosRoll = 1.f - 2.f * (q.x * q.x + q.z * q.z);
    float roll    = std::atan2(sinRoll, cosRoll);

    return glm::degrees(glm::vec3(pitch, yaw, roll));
}

// Euler angles in DEGREES, order YXZ — matches getEulerAngles round-trip
void Node3D::setEulerAngles(const glm::vec3 &pitchYawRollDegrees)
{
    glm::vec3 radians = glm::radians(pitchYawRollDegrees);
    float pitch = radians.x;
    float yaw   = radians.y;
    float roll  = radians.z;

    float cy = std::cos(yaw   * 0.5f);
    float sy = std::sin(yaw   * 0.5f);
    float cp = std::cos(pitch * 0.5f);
    float sp = std::sin(pitch * 0.5f);
    float cr = std::cos(roll  * 0.5f);
    float sr = std::sin(roll  * 0.5f);

    rotation.w = cr * cp * cy + sr * sp * sy;
    rotation.x = cr * sp * cy + sr * cp * sy;
    rotation.y = cr * cp * sy - sr * sp * cy;
    rotation.z = sr * cp * cy - cr * sp * sy;
    rotation = glm::normalize(rotation);
    markDirty();
}
void Node3D::setScale(const glm::vec3 &s)
{
    scale = s;
    markDirty();
}
void Node3D::setScale(float s)
{
    scale = {s, s, s};
    markDirty();
}

void Node3D::resetTransform()
{
    position = {0.f, 0.f, 0.f};
    rotation = glm::quat(1.f, 0.f, 0.f, 0.f);
    scale = {1.f, 1.f, 1.f};
    markDirty();
}

// ============================================================
//  Translation
// ============================================================
void Node3D::translate(const glm::vec3 &delta, TransformSpace space)
{
    if (space == TransformSpace::Local)
        position += rotation * delta;
    else if (space == TransformSpace::World)
    {
        if (Node3D *p3d = effectiveTransformParent())
            position += glm::inverse(p3d->worldRotation()) * delta;
        else
            position += delta;
    }
    else // Parent
        position += delta;

    markDirty();
}

void Node3D::move(const glm::vec3 &worldDelta)
{
    position += worldDelta;
    markDirty();
}

// ============================================================
//  Rotation
// ============================================================
void Node3D::rotate(const glm::quat &rot, TransformSpace space)
{
    if (space == TransformSpace::Local)
        rotation = rotation * rot;
    else if (space == TransformSpace::Parent)
        rotation = rot * rotation;
    else // World
    {
        glm::quat parentRot = glm::quat(1.f, 0.f, 0.f, 0.f);
        if (Node3D *p3d = effectiveTransformParent())
            parentRot = p3d->worldRotation();
        rotation = (glm::inverse(parentRot) * rot * parentRot) * rotation;
    }
    rotation = glm::normalize(rotation);
    markDirty();
}

void Node3D::rotate(float angle, const glm::vec3 &axis, TransformSpace space)
{
    rotate(glm::angleAxis(angle, axis), space);
}

void Node3D::yaw(float degrees)
{
    rotate(glm::radians(degrees), {0, 1, 0}, TransformSpace::World);
}

void Node3D::pitch(float degrees)
{
    rotate(glm::radians(degrees), {1, 0, 0}, TransformSpace::Local);
}

void Node3D::roll(float degrees)
{
    rotate(glm::radians(degrees), {0, 0, 1}, TransformSpace::Local);
}

// ============================================================
//  Look
// ============================================================
void Node3D::lookAt(const glm::vec3 &target, const glm::vec3 &up)
{
    glm::vec3 dir = target - worldPosition();
    if (glm::length(dir) < 1e-6f)
        return;
    lookDirection(glm::normalize(dir), up);
}

void Node3D::lookDirection(const glm::vec3 &dir, const glm::vec3 &up)
{
    if (glm::length(dir) < 1e-6f)
        return;
    
    rotation = glm::quatLookAt(glm::normalize(dir), up);
    markDirty();
}

void Node3D::lookAtSmooth(const glm::vec3 &target, float t, const glm::vec3 &up)
{
    glm::vec3 dir = target - worldPosition();
    if (glm::length(dir) < 1e-6f)
        return;
    
    glm::quat targetRot = glm::quatLookAt(glm::normalize(dir), up);
    rotation = glm::slerp(rotation, targetRot, glm::clamp(t, 0.f, 1.f));
    rotation = glm::normalize(rotation);
    markDirty();
}

// ============================================================
//  Utility
// ============================================================
float Node3D::distanceTo(const Node3D *other) const
{
    return other ? glm::distance(worldPosition(), other->worldPosition()) : 0.f;
}

float Node3D::distanceTo(const glm::vec3 &worldPos) const
{
    return glm::distance(worldPosition(), worldPos);
}

bool Node3D::inRange(const Node3D *other, float range) const
{
    if (!other)
        return false;
    glm::vec3 d = worldPosition() - other->worldPosition();
    return glm::dot(d, d) <= range * range; // evita sqrt
}

glm::vec3 Node3D::directionTo(const Node3D *other) const
{
    return other ? directionTo(other->worldPosition()) : glm::vec3(0.f);
}

glm::vec3 Node3D::directionTo(const glm::vec3 &worldPos) const
{
    glm::vec3 d = worldPos - worldPosition();
    float len = glm::length(d);
    return len > 1e-6f ? d / len : glm::vec3(0.f);
}

// ============================================================
//  RotateTowards / MoveTowards
// ============================================================
bool Node3D::rotateTo(const glm::quat &targetRot, float maxDegrees)
{
    
    float angleDeg = glm::degrees(glm::angle(glm::inverse(rotation) * targetRot));
    if (angleDeg <= maxDegrees)
    {
        setRotation(targetRot);
        return true;
    }
    float t = maxDegrees / angleDeg;
    rotation = glm::normalize(glm::slerp(rotation, targetRot, t));
    markDirty();
    return false;
}

bool Node3D::moveTo(const glm::vec3 &worldTarget, float maxDelta)
{
    glm::vec3 wp = worldPosition();
    glm::vec3 d  = worldTarget - wp;
    float dist   = glm::length(d);
    if (dist <= maxDelta)
    {
        move(worldTarget - wp);
        return true;
    }
    move((d / dist) * maxDelta);
    return false;
}

// ============================================================
//  Space conversion
// ============================================================
glm::vec3 Node3D::worldToLocalPoint(const glm::vec3 &worldPoint) const
{
    return glm::vec3(glm::inverse(worldMatrix()) * glm::vec4(worldPoint, 1.f));
}

glm::vec3 Node3D::localToWorldPoint(const glm::vec3 &localPoint) const
{
    return glm::vec3(worldMatrix() * glm::vec4(localPoint, 1.f));
}

// ============================================================
//  Re-parent
// ============================================================
void Node3D::setParent(Node3D *newParent)
{
    if (transformParent_ == newParent)
        return;

    transformParent_ = newParent;
    markDirty();
}
