#pragma once

#include "GenesisBspCollider.hpp"

class Camera;

class FpsPlayerController
{
public:
    glm::vec3 position = glm::vec3(0.0f, 64.0f, 0.0f);
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    float eyeOffset = 34.0f;
    float radius = 18.0f;
    float forwardSpeed = 190.0f;
    float backwardSpeed = 120.0f;
    float strafeSpeed = 170.0f;
    float flyVerticalSpeed = 170.0f;
    float sprintMultiplier = 1.8f;
    float turnSpeed = 180.0f;
    float mouseSensitivity = 0.12f;
    float gravity = 820.0f;
    float jumpSpeed = 440.0f;
    float verticalSpeed = 0.0f;
    bool grounded = false;
    bool useGravity = false;
    bool useJump = false;
    GenesisTraceResult lastMoveTrace = {};
    GenesisTraceResult lastSlideTrace = {};
    GenesisTraceResult lastGroundTrace = {};

    glm::vec3 eyePosition() const;

    void setSpawn(Camera *camera,
                  const glm::vec3 &spawn,
                  const glm::vec3 &forward = glm::vec3(0.0f, 0.0f, -1.0f));

    void update(Camera *camera,
                const GenesisBspCollider &collider,
                float dt,
                bool allowMouseLook);
};
