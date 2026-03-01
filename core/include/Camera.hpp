#pragma once
#include <glm/glm.hpp>
#include "Node.hpp"

class CameraController;

enum class ProjectionType
{
    Perspective,
    Orthographic
};
 
// ============================================================
//  Camera — perspective camera node
// ============================================================
class Camera : public Node3D
{
public:
    Camera();

    ~Camera();
    // ── Projection params ───────────────────────────────────
    ProjectionType projectionType = ProjectionType::Perspective;
    float fov      = 45.0f;   // vertical FOV (degrees) — Perspective only
    float nearPlane = 0.1f;
    float farPlane  = 1000.0f;
    float orthoSize = 10.0f;  // half-height in world units — Orthographic only
    // ortho width = orthoSize * aspect

    // ── Viewport / clear ────────────────────────────────────
    glm::ivec4 viewport = glm::ivec4(0); // x, y, w, h (pixels)
    bool clearColor = true;
    glm::vec4 clearColorVal = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    bool clearDepth = true;

    // ── Cached matrices — valid after updateMatrices() ──────
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::mat4 viewProjection = glm::mat4(1.0f);

    void update(float dt);

    // ── Setters ─────────────────────────────────────────────
    // Sets projection type (Perspective / Orthographic).
    void setProjectionType(ProjectionType type) { projectionType = type; }

    // Sets vertical FOV (degrees).
    void setFov(float degrees);

    // Sets near and far clip planes.
    void setViewPlanes(float nearP, float farP);

    // Sets viewport rect AND updates the aspect ratio.
    void setViewport(int x, int y, int w, int h);

    // Updates only the aspect ratio from window dimensions.
    void setAspect(int w, int h);

    float aspect() const { return aspect_; }

    CameraController *setController(CameraController *ctrl);
    CameraController *getController() const { return controller_; }

    // ── Per-frame ───────────────────────────────────────────
    // Recomputes view / projection / viewProjection.
    // Called once per frame by Scene::renderCamera().
    void updateMatrices();

    glm::mat4 getView() const;
    glm::mat4 getProjection() const;

    // Retorna vec3(x, y, depth). Depth vai de 0.0 a 1.0.
    glm::vec3 project(const glm::vec3& worldCoords) const;

    // Recebe vec3(x, y, depth). Para picking (raio) use depth 0.0 (near) e 1.0 (far).
    glm::vec3 unproject(const glm::vec3& windowCoords) const;

    // Cria um raio a partir das coordenadas do rato (SDL: 0,0 top-left)
    Ray getRay(float mouseX, float mouseY) const;

private:
    float aspect_ = 1024.0f / 768.0f;
    CameraController *controller_ = nullptr;
};

// ============================================================
//  CameraController — base para qualquer controller de câmara
// ============================================================
class CameraController
{
public:
    virtual ~CameraController() = default;
    virtual void update(Camera &camera, float dt) = 0;
    virtual void onAttach(Camera &camera) {} // chamado quando é anexado
    virtual void onDetach(Camera &camera) {} // chamado quando é removido
};

// ============================================================
//  FreeCameraController — FPS style, WASD + rato
// ============================================================
class FreeCameraController : public CameraController
{
public:
    float moveSpeed = 8.0f;
    float mouseSensitivity = 0.15f;
    float sprintMultiplier = 2.5f;

    void update(Camera &camera, float dt) override;
};

// ============================================================
//  OrbitCameraController — orbita à volta de um target
// ============================================================
class OrbitCameraController : public CameraController
{
public:
    glm::vec3 target = {0.f, 0.f, 0.f};
    float distance = 10.f;
    float minDist = 1.f;
    float maxDist = 500.f;
    float sensitivity = 0.3f;
    float zoomSpeed = 2.0f;

    void onAttach(Camera &camera) override;
    void update(Camera &camera, float dt) override;

private:
    float yaw_ = 0.f;
    float pitch_ = 20.f;
};

// ============================================================
//  MayaCameraController — Alt+LMB orbit, Alt+MMB pan, scroll zoom
// ============================================================
class MayaCameraController : public CameraController
{
public:
    glm::vec3 target = {0.f, 0.f, 0.f};
    float distance = 10.f;
    float minDist = 0.5f;
    float maxDist = 1000.f;
    float orbitSpeed = 0.4f;
    float panSpeed = 0.05f;
    float zoomSpeed = 1.0f;

    void onAttach(Camera &camera) override;
    void update(Camera &camera, float dt) override;

private:
    float yaw_ = 0.f;
    float pitch_ = 20.f;
};