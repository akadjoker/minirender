#include "Camera.hpp"
#include "Input.hpp"
#include <glm/gtc/matrix_transform.hpp>

// ============================================================
//  Camera
// ============================================================

void Camera::setFov(float degrees)
{
    fov = degrees;
}

void Camera::setViewPlanes(float nearP, float farP)
{
    nearPlane = nearP;
    farPlane = farP;
}

void Camera::setViewport(int x, int y, int w, int h)
{
    viewport = glm::ivec4(x, y, w, h);
    if (h > 0)
        aspect_ = static_cast<float>(w) / static_cast<float>(h);
}

void Camera::setAspect(int w, int h)
{
    if (h > 0)
        aspect_ = static_cast<float>(w) / static_cast<float>(h);
}

glm::mat4 Camera::getView() const
{
    return glm::inverse(worldMatrix());
}

glm::mat4 Camera::getProjection() const
{
    return glm::perspective(glm::radians(fov), aspect_, nearPlane, farPlane);
}

void Camera::updateMatrices()
{
    view = getView();
    projection = getProjection();
    viewProjection = projection * view;
}

Camera::Camera():Node3D()
{
    type = NodeType::Camera;
}

Camera::~Camera()
{
    if (controller_)
        delete controller_;
}

void Camera::update(float dt)
{
    if (controller_)
        controller_->update(*this, dt);
}


CameraController* Camera::setController(CameraController *ctrl)
{
    if (controller_)
    {
        controller_->onDetach(*this);
        delete controller_;
    }
    controller_ = ctrl;
    if (controller_)
        controller_->onAttach(*this);
    return controller_;
}
 

// ============================================================
//  FreeCameraController
// ============================================================
void FreeCameraController::update(Camera &camera, float dt)
{
    Input &input = Input::instance();

    float speed = moveSpeed * dt;
    if (input.isKey(SDL_SCANCODE_LSHIFT))
        speed *= sprintMultiplier;

    if (input.isKey(SDL_SCANCODE_W))
        camera.translate(glm::vec3(0, 0, -1) * speed, TransformSpace::Local);
    if (input.isKey(SDL_SCANCODE_S))
        camera.translate(glm::vec3(0, 0,  1) * speed, TransformSpace::Local);
    if (input.isKey(SDL_SCANCODE_A))
        camera.translate(glm::vec3(-1, 0, 0) * speed, TransformSpace::Local);
    if (input.isKey(SDL_SCANCODE_D))
        camera.translate(glm::vec3( 1, 0, 0) * speed, TransformSpace::Local);
    if (input.isKey(SDL_SCANCODE_E))
        camera.translate(glm::vec3(0,  1, 0) * speed, TransformSpace::World);
    if (input.isKey(SDL_SCANCODE_Q))
        camera.translate(glm::vec3(0, -1, 0) * speed, TransformSpace::World);
 

    if (input.isMouseDown(SDL_BUTTON_LEFT))
    {
        camera.rotate(glm::radians(-input.getDeltaX() * mouseSensitivity),
                      {0, 1, 0}, TransformSpace::World);
        camera.rotate(glm::radians(-input.getDeltaY() * mouseSensitivity),
                      {1, 0, 0}, TransformSpace::Local);
    }
}

// ============================================================
//  OrbitCameraController
// ============================================================
static void applyOrbit(Camera &camera, const glm::vec3 &target,
                       float distance, float yaw, float pitch)
{
    glm::vec3 offset;
    offset.x = distance * cosf(glm::radians(pitch)) * sinf(glm::radians(yaw));
    offset.y = distance * sinf(glm::radians(pitch));
    offset.z = distance * cosf(glm::radians(pitch)) * cosf(glm::radians(yaw));

    camera.position = target + offset;
    camera.lookAt(target);
}

void OrbitCameraController::onAttach(Camera &camera)
{
    // calcula yaw/pitch iniciais a partir da posição actual da câmara
    glm::vec3 dir = camera.position - target;
    distance = glm::length(dir);
    if (distance > 0.001f)
    {
        dir = glm::normalize(dir);
        pitch_ = glm::degrees(asinf(dir.y));
        yaw_ = glm::degrees(atan2f(dir.x, dir.z));
    }
}

void OrbitCameraController::update(Camera &camera, float dt)
{
    Input &input = Input::instance();

    if (input.isMouseDown(SDL_BUTTON_LEFT))
    {
        yaw_ += input.getDeltaX() * sensitivity;
        pitch_ -= input.getDeltaY() * sensitivity;
        pitch_ = std::clamp(pitch_, -89.f, 89.f);
    }

    // scroll zoom
    float scroll = input.getScrollY();
    distance -= scroll * zoomSpeed;
    distance = std::clamp(distance, minDist, maxDist);

    applyOrbit(camera, target, distance, yaw_, pitch_);
}

// ============================================================
//  MayaCameraController
// ============================================================
void MayaCameraController::onAttach(Camera &camera)
{
    glm::vec3 dir = camera.position - target;
    distance = glm::length(dir);
    if (distance > 0.001f)
    {
        dir = glm::normalize(dir);
        pitch_ = glm::degrees(asinf(dir.y));
        yaw_ = glm::degrees(atan2f(dir.x, dir.z));
    }
}

void MayaCameraController::update(Camera &camera, float dt)
{
    Input &input = Input::instance();
    bool altDown = input.isKey(SDL_SCANCODE_LALT);

    // Alt + LMB — orbit
    if (altDown && input.isMouseDown(SDL_BUTTON_LEFT))
    {
        yaw_ += input.getDeltaX() * orbitSpeed;
        pitch_ -= input.getDeltaY() * orbitSpeed;
        pitch_ = std::clamp(pitch_, -89.f, 89.f);
    }

    // Alt + MMB — pan
    if (altDown && input.isMouseDown(SDL_BUTTON_MIDDLE))
    {
        glm::vec3 right = camera.right();
        glm::vec3 up = camera.up();
        float pan = panSpeed * distance;
        target -= right * (input.getDeltaX() * pan);
        target += up * (input.getDeltaY() * pan);
    }

    // scroll zoom (ou Alt + RMB)
    float scroll = input.getScrollY();
    if (altDown && input.isMouseDown(SDL_BUTTON_RIGHT))
        scroll -= input.getDeltaX() * 0.05f;

    distance -= scroll * zoomSpeed;
    distance = std::clamp(distance, minDist, maxDist);

    applyOrbit(camera, target, distance, yaw_, pitch_);
}
