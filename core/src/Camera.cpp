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
    if (projectionType == ProjectionType::Orthographic)
    {
        float halfH = orthoSize;
        float halfW = halfH * aspect_;
        return glm::ortho(-halfW, halfW, -halfH, halfH, nearPlane, farPlane);
    }
    return glm::perspective(glm::radians(fov), aspect_, nearPlane, farPlane);
}

void Camera::updateMatrices()
{
    view = getView();
    projection = getProjection();
    viewProjection = projection * view;
}

glm::vec3 Camera::project(const glm::vec3& worldCoords) const
{
    return glm::project(worldCoords, view, projection, glm::vec4(viewport));
}

glm::vec3 Camera::unproject(const glm::vec3& windowCoords) const
{
    return glm::unProject(windowCoords, view, projection, glm::vec4(viewport));
}

Ray Camera::getRay(float mouseX, float mouseY) const
{
    // Inverter Y (SDL 0=topo, GL 0=fundo)
    // Assumindo que viewport.w é a altura (4º componente do ivec4)
    float winY = (float)viewport.w - mouseY;

    glm::vec3 start = unproject(glm::vec3(mouseX, winY, 0.0f));
    glm::vec3 end   = unproject(glm::vec3(mouseX, winY, 1.0f));

    return { start, glm::normalize(end - start) };
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
 

    float speed = moveSpeed * dt;
    if (Input::IsKeyDown(KEY_LEFT_SHIFT))
        speed *= sprintMultiplier;

    if (Input::IsKeyDown(KEY_W))
        camera.translate(glm::vec3(0, 0, -1) * speed, TransformSpace::Local);
    if (Input::IsKeyDown(KEY_S))
        camera.translate(glm::vec3(0, 0,  1) * speed, TransformSpace::Local);
    if (Input::IsKeyDown(KEY_A))
        camera.translate(glm::vec3(-1, 0, 0) * speed, TransformSpace::Local);
    if (Input::IsKeyDown(KEY_D))
        camera.translate(glm::vec3( 1, 0, 0) * speed, TransformSpace::Local);
    if (Input::IsKeyDown(KEY_E))
        camera.translate(glm::vec3(0,  1, 0) * speed, TransformSpace::World);
    if (Input::IsKeyDown(KEY_Q))
        camera.translate(glm::vec3(0, -1, 0) * speed, TransformSpace::World);
 

    if (Input::IsMouseDown(MouseButton::LEFT))
    {
        camera.rotate(glm::radians(-Input::GetMouseDelta().x * mouseSensitivity),
                      {0, 1, 0}, TransformSpace::World);
        camera.rotate(glm::radians(-Input::GetMouseDelta().y * mouseSensitivity),
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
    

    if (Input::IsMouseDown(MouseButton::LEFT))
    {
        yaw_ += Input::GetMouseDelta().x * sensitivity;
        pitch_ -= Input::GetMouseDelta().y * sensitivity;
        pitch_ = std::clamp(pitch_, -89.f, 89.f);
    }

    // scroll zoom
    float scroll = Input::GetMouseWheelMoveV();
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
 
    bool altDown = Input::IsKeyDown(KEY_LEFT_ALT);

    // Alt + LMB — orbit
    if (altDown && Input::IsMouseDown(MouseButton::LEFT))
    {
        yaw_ += Input::GetMouseDelta().x * orbitSpeed;
        pitch_ -= Input::GetMouseDelta().y * orbitSpeed;
        pitch_ = std::clamp(pitch_, -89.f, 89.f);
    }

    // Alt + MMB — pan
    if (altDown && Input::IsMouseDown(MouseButton::MIDDLE))
    {
        glm::vec3 right = camera.right();
        glm::vec3 up = camera.up();
        float pan = panSpeed * distance;
        target -= right * (Input::GetMouseDelta().x * pan);
        target += up * (Input::GetMouseDelta().y * pan);
    }

    // scroll zoom (ou Alt + RMB)
    float scroll = Input::GetMouseWheelMoveV();
    if (altDown && Input::IsMouseDown(MouseButton::RIGHT))
        scroll -= Input::GetMouseDelta().x * 0.05f;

    distance -= scroll * zoomSpeed;
    distance = std::clamp(distance, minDist, maxDist);

    applyOrbit(camera, target, distance, yaw_, pitch_);
}
