#include "FpsPlayerController.hpp"

#include <cmath>

#include "Camera.hpp"
#include "Input.hpp"

glm::vec3 FpsPlayerController::eyePosition() const
{
    return position + glm::vec3(0.0f, eyeOffset, 0.0f);
}

void FpsPlayerController::setSpawn(Camera *camera,
                                   const glm::vec3 &spawn,
                                   const glm::vec3 &forward)
{
    if (!camera)
        return;

    position = spawn;

    glm::vec3 forwardFlat = forward;
    forwardFlat.y = 0.0f;
    if (glm::length2(forwardFlat) <= 1e-8f)
        forwardFlat = glm::vec3(0.0f, 0.0f, -1.0f);
    else
        forwardFlat = glm::normalize(forwardFlat);

    // Node3D uses right-handed yaw around +Y. Convert a forward vector to that yaw convention.
    yawDegrees = glm::degrees(std::atan2(-forwardFlat.x, -forwardFlat.z));
    pitchDegrees = 0.0f;
    verticalSpeed = 0.0f;
    grounded = false;

    camera->setPosition(eyePosition());
    camera->setEulerAngles(glm::vec3(pitchDegrees, yawDegrees, 0.0f));
}

void FpsPlayerController::update(Camera *camera,
                                 const GenesisBspCollider &collider,
                                 float dt,
                                 bool allowMouseLook)
{
    if (!camera)
        return;
    dt = glm::clamp(dt, 0.0f, 0.05f);

    float move = 0.0f;
    float strafe = 0.0f;
    float flyY = 0.0f;
    float turn = 0.0f;

    if (Input::IsKeyDown(KEY_UP) || Input::IsKeyDown(KEY_W))
        move = forwardSpeed;
    if (Input::IsKeyDown(KEY_DOWN) || Input::IsKeyDown(KEY_S))
        move = -backwardSpeed;
    if (Input::IsKeyDown(KEY_A))
        strafe = -strafeSpeed;
    if (Input::IsKeyDown(KEY_D))
        strafe = strafeSpeed;
    if (!useGravity)
    {
        if (Input::IsKeyDown(KEY_Q))
            flyY = flyVerticalSpeed;
        if (Input::IsKeyDown(KEY_Z) || Input::IsKeyDown(KEY_E))
            flyY = -flyVerticalSpeed;
    }
    if (Input::IsKeyDown(KEY_LEFT))
        turn = turnSpeed;
    if (Input::IsKeyDown(KEY_RIGHT))
        turn = -turnSpeed;
    if (Input::IsKeyDown(KEY_LEFT_SHIFT))
    {
        if (move > 0.0f)
            move *= sprintMultiplier;
        strafe *= sprintMultiplier;
        flyY *= sprintMultiplier;
    }

    if (useJump && Input::IsKeyPressed(KEY_SPACE) && grounded)
        verticalSpeed = jumpSpeed;

    yawDegrees += turn * dt;
    if (allowMouseLook)
    {
        const glm::vec2 mouseDelta = Input::GetMouseDelta();
        yawDegrees += -mouseDelta.x * mouseSensitivity;
        pitchDegrees += -mouseDelta.y * mouseSensitivity;
        pitchDegrees = glm::clamp(pitchDegrees, -89.0f, 89.0f);
    }

    const float yawRadians = glm::radians(yawDegrees);
    glm::vec3 forward(-std::sin(yawRadians), 0.0f, -std::cos(yawRadians));
    if (glm::length2(forward) > 1e-8f)
        forward = glm::normalize(forward);
    else
        forward = glm::vec3(0.0f, 0.0f, -1.0f);

    glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::length2(right) > 1e-8f)
        right = glm::normalize(right);
    else
        right = glm::vec3(1.0f, 0.0f, 0.0f);

    if (useGravity)
        verticalSpeed -= gravity * dt;
    else
        verticalSpeed = 0.0f;

    const glm::vec3 desiredDelta = forward * (move * dt) +
                                   right * (strafe * dt) +
                                   glm::vec3(0.0f, (verticalSpeed + flyY) * dt, 0.0f);

    if (collider.hasTree())
    {
        const glm::vec3 mins(-radius, -radius, -radius);
        const glm::vec3 maxs(radius, radius, radius);

        const glm::vec3 moveEnd = position + desiredDelta;
        collider.traceBoxDetailed(position, moveEnd, mins, maxs, lastMoveTrace);
        position = collider.moveAndSlide(position, desiredDelta, mins, maxs, 5);
        lastSlideTrace = collider.lastTrace();

        // Ground check: only treat as grounded when the hit plane points up enough.
        const bool groundHit = collider.traceBoxDetailed(position,
                                                         position + glm::vec3(0.0f, -4.0f, 0.0f),
                                                         mins,
                                                         maxs,
                                                         lastGroundTrace);
        grounded = groundHit && lastGroundTrace.hit && lastGroundTrace.planeNormal.y > 0.35f;

        // Ceiling hit: stop upward velocity immediately when colliding with a downward-facing plane.
        const bool hitCeiling = (lastMoveTrace.hit && lastMoveTrace.planeNormal.y < -0.35f) ||
                                (lastSlideTrace.hit && lastSlideTrace.planeNormal.y < -0.35f);
        if (hitCeiling && verticalSpeed > 0.0f)
            verticalSpeed = 0.0f;

        if (grounded && verticalSpeed < 0.0f)
            verticalSpeed = 0.0f;
    }
    else
    {
        position += desiredDelta;
        grounded = false;
        lastMoveTrace = {};
        lastSlideTrace = {};
        lastGroundTrace = {};
    }

    camera->setPosition(eyePosition());
    camera->setEulerAngles(glm::vec3(pitchDegrees, yawDegrees, 0.0f));
}
