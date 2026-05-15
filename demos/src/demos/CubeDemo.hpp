#pragma once

#include <cmath>
#include <cstdio>
#include <string>

#include <glm/gtc/type_ptr.hpp>

#include "Demo.hpp"
#include <WidgetApp.hpp>
#include <BasicWidgets.hpp>
#include <InputWidgets.hpp>
#include <ViewWidgets.hpp>
#include <GizmoWidgets.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// CubeDemo – cube com gizmo 3D de transform (Translate / Rotate / Scale)
// ─────────────────────────────────────────────────────────────────────────────
class CubeDemo : public IDemo
{
public:
    const char *title()       const override { return "Cube Demo"; }
    const char *description() const override { return "Cube + Gizmo3D transform widget"; }

    bool setup(Device &device) override;
    void update(float dt)      override;
    void drawGui()             override;
    void render()              override;
    void shutdown()            override;

private:
    static Shader *createShader();
    void           resetCamera();

    Device   *device_     = nullptr;
    Scene     scene_;
    Camera   *camera_     = nullptr;
    MeshNode *cubeNode_   = nullptr;
    MeshNode *groundNode_ = nullptr;
    Shader   *shader_     = nullptr;

    // Cube transform (world space)
    glm::vec3 cubePos_          = {0.0f, 0.0f, 0.0f};
    glm::vec3 cubeRot_          = {0.0f, 0.0f, 0.0f};  // pitch, yaw, roll (degrees)
    glm::vec3 cubeScale3_       = {1.0f, 1.0f, 1.0f};
    glm::vec3 gizmoDragStartPos_ = {0.0f, 0.0f, 0.0f};  // saved at drag start

    // Animation
    bool  autoRotate_ = false;
    float rotSpeed_   = 45.0f;

    // Cube colour
    glm::vec4 cubeColor_ = {0.35f, 0.60f, 1.00f, 1.0f};

    // Gizmo3D — fullscreen overlay, lives in a temporary root widget
    BuGUI::Gizmo3D *gizmo_     = nullptr;
    BuGUI::Widget  *gizmoRoot_ = nullptr;

    // Panel
    BuGUI::FloatWindow *fw_        = nullptr;
    BuGUI::Label       *infoLabel_ = nullptr;
};

// ─── setup ───────────────────────────────────────────────────────────────────
inline bool CubeDemo::setup(Device &device)
{
    device_ = &device;

    shader_ = createShader();
    if (!shader_)
        return false;

    camera_ = scene_.createFreeCamera("cube_cam",
                                      device.GetWidth(), device.GetHeight(),
                                      glm::vec3(4.0f, 3.0f, 6.0f),
                                      glm::vec3(0.0f, 0.0f, 0.0f),
                                      6.0f, 0.18f, 3.0f);
    if (!camera_)
        return false;

    camera_->clearColorVal = glm::vec4(0.10f, 0.10f, 0.15f, 1.0f);

    Mesh *cubeMesh   = MeshManager::instance().create_cube("cube_demo_mesh", 1.0f);
    Mesh *groundMesh = MeshManager::instance().create_plane("cube_ground_mesh", 14.0f, 14.0f, 1);
    if (!cubeMesh || !groundMesh)
        return false;

    cubeNode_  = scene_.createMeshNode("cube_demo_node",   cubeMesh);
    groundNode_ = scene_.createMeshNode("cube_ground_node", groundMesh);
    if (!cubeNode_ || !groundNode_)
        return false;

    groundNode_->setPosition(0.0f, -0.5f, 0.0f);
    groundNode_->renderType = RenderType::Special;

    // ── Gizmo3D — fullscreen overlay via a temporary root widget ──────────
    // root() is null until a stage/root is set — create one just for the gizmo.
    gizmoRoot_ = new BuGUI::Widget();
    gizmo_     = gizmoRoot_->createChild<BuGUI::Gizmo3D>();
    BuGUI::WidgetApp::instance().setRoot(gizmoRoot_);  // WidgetApp takes ownership
    gizmo_->setMode(BuGUI::GizmoMode3D::Translate);

    // onTranslate3D emits TOTAL delta from drag start (not per-frame delta),
    // so use absolute assignment relative to the saved start position.
    gizmo_->onDragStart.connect([this]() {
        gizmoDragStartPos_ = cubePos_;
    });
    gizmo_->onTranslate3D.connect([this](BuGUI::Vec3f d) {
        cubePos_ = gizmoDragStartPos_ + glm::vec3(d.x, d.y, d.z);
    });
    gizmo_->onRotate3D.connect([this](BuGUI::Vec3f r) {
        cubeRot_ = glm::vec3(r.x, r.y, r.z);
    });
    gizmo_->onScale3D.connect([this](BuGUI::Vec3f s) {
        cubeScale3_ = glm::vec3(s.x, s.y, s.z);
    });

    // ── FloatWindow panel ─────────────────────────────────────────────────
    fw_ = BuGUI::WidgetApp::instance().addFloat<BuGUI::FloatWindow>("Cube Controls");
    fw_->setFloatPos(16.0f, 72.0f);
    fw_->setFloatSize(220.0f, 340.0f);
    fw_->setClosable(false);
    fw_->setResizable(false);

    auto *vbox = fw_->setContent<BuGUI::BoxLayout>(BuGUI::LayoutDir::Vertical);
    vbox->setSpacing(4.0f);
    vbox->setPadding(4.0f);

    // Gizmo mode buttons
    vbox->createChild<BuGUI::Label>("Gizmo Mode");
    auto *hrow = vbox->createChild<BuGUI::BoxLayout>(BuGUI::LayoutDir::Horizontal);
    hrow->setSpacing(4.0f);
    auto *btnT = hrow->createChild<BuGUI::Button>("Translate");
    auto *btnR = hrow->createChild<BuGUI::Button>("Rotate");
    auto *btnS = hrow->createChild<BuGUI::Button>("Scale");
    btnT->clicked.connect([this]() { gizmo_->setMode(BuGUI::GizmoMode3D::Translate); });
    btnR->clicked.connect([this]() { gizmo_->setMode(BuGUI::GizmoMode3D::Rotate); });
    btnS->clicked.connect([this]() { gizmo_->setMode(BuGUI::GizmoMode3D::Scale); });

    // Colour sliders
    vbox->createChild<BuGUI::Label>("Color R");
    auto *sR = vbox->createChild<BuGUI::Slider>(0.0f, 1.0f, cubeColor_.r);
    sR->onValueChanged.connect([this](float v) { cubeColor_.r = v; });

    vbox->createChild<BuGUI::Label>("Color G");
    auto *sG = vbox->createChild<BuGUI::Slider>(0.0f, 1.0f, cubeColor_.g);
    sG->onValueChanged.connect([this](float v) { cubeColor_.g = v; });

    vbox->createChild<BuGUI::Label>("Color B");
    auto *sB = vbox->createChild<BuGUI::Slider>(0.0f, 1.0f, cubeColor_.b);
    sB->onValueChanged.connect([this](float v) { cubeColor_.b = v; });

    // Rotation speed
    vbox->createChild<BuGUI::Label>("Rot speed (deg/s)");
    auto *sSpd = vbox->createChild<BuGUI::Slider>(-180.0f, 180.0f, rotSpeed_);
    sSpd->onValueChanged.connect([this](float v) { rotSpeed_ = v; });

    // Auto-rotate
    auto *cbAuto = vbox->createChild<BuGUI::CheckBox>("Auto-rotate Y");
    cbAuto->setChecked(autoRotate_);
    cbAuto->toggled.connect([this](bool v) { autoRotate_ = v; });

    // Reset
    auto *btnReset = vbox->createChild<BuGUI::Button>("Reset");
    btnReset->clicked.connect([this]() {
        cubePos_    = {0, 0, 0};
        cubeRot_    = {0, 0, 0};
        cubeScale3_ = {1, 1, 1};
        resetCamera();
    });

    infoLabel_ = vbox->createChild<BuGUI::Label>("pos: 0 0 0");

    return true;
}

// ─── update ──────────────────────────────────────────────────────────────────
inline void CubeDemo::update(float dt)
{
    if (autoRotate_)
        cubeRot_.y = std::fmod(cubeRot_.y + rotSpeed_ * dt, 360.0f);

    if (cubeNode_)
    {
        cubeNode_->setPosition(cubePos_);
        cubeNode_->setRotationEuler(cubeRot_);
        cubeNode_->setScale(cubeScale3_);
    }

    const int w = device_->GetWidth();
    const int h = device_->GetHeight();

    if (camera_)
        camera_->setViewport(0, 0, w, h);

    // Feed gizmo with current VP matrices and target transform
    if (gizmo_ && camera_)
    {
        gizmo_->setViewProjection(
            glm::value_ptr(camera_->view),
            glm::value_ptr(camera_->projection),
            w, h);
        gizmo_->setTarget(&cubePos_.x, &cubeRot_.x, &cubeScale3_.x);
    }

    scene_.update(dt);
}

// ─── drawGui ─────────────────────────────────────────────────────────────────
inline void CubeDemo::drawGui()
{
    if (infoLabel_)
    {
        char buf[80];
        std::snprintf(buf, sizeof(buf),
                      "pos: %.1f %.1f %.1f\nrot: %.0f %.0f %.0f",
                      cubePos_.x, cubePos_.y, cubePos_.z,
                      cubeRot_.x, cubeRot_.y, cubeRot_.z);
        infoLabel_->setText(buf);
    }
}

// ─── render ──────────────────────────────────────────────────────────────────
inline void CubeDemo::render()
{
    if (!camera_)
        return;

    const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.45f, -1.0f, -0.25f));
    const glm::vec3 ambient  = glm::vec3(0.18f, 0.18f, 0.22f);

    scene_.setCamera(camera_);
    scene_.beginPass();

    scene_.setShader(shader_);

    shader_->setVec4("u_color",    cubeColor_);
    shader_->setVec3("u_lightDir", lightDir);
    shader_->setVec3("u_ambient",  ambient);
    scene_.render(RenderType::Solid);

    shader_->setVec4("u_color",    glm::vec4(0.30f, 0.32f, 0.28f, 1.0f));
    shader_->setVec3("u_lightDir", lightDir);
    shader_->setVec3("u_ambient",  ambient);
    scene_.render(RenderType::Special);

    scene_.endPass();
}

// ─── shutdown ────────────────────────────────────────────────────────────────
inline void CubeDemo::shutdown()
{
    // Clear the temporary root (deletes gizmoRoot_ + gizmo_ child).
    // Must null gizmo_ first so nothing touches the freed pointer.
    gizmo_     = nullptr;
    gizmoRoot_ = nullptr;
    BuGUI::WidgetApp::instance().setRoot(nullptr);

    scene_.clear();
    unloadDemoAssets();

    if (fw_) { BuGUI::WidgetApp::instance().removeFloat(fw_); fw_ = nullptr; }
    infoLabel_ = nullptr;
    camera_    = nullptr;
    shader_    = nullptr;
    cubeNode_  = nullptr;
    groundNode_ = nullptr;
}

// ─── resetCamera ─────────────────────────────────────────────────────────────
inline void CubeDemo::resetCamera()
{
    if (!camera_)
        return;
    camera_->setPosition(glm::vec3(4.0f, 3.0f, 6.0f));
    camera_->lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
    camera_->updateMatrices();
}

// ─── createShader ────────────────────────────────────────────────────────────
inline Shader *CubeDemo::createShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;
        uniform mat3 u_normalMatrix;

        out vec3 v_normal;

        void main()
        {
            v_normal    = normalize(u_normalMatrix * normal);
            gl_Position = u_projection * u_view * u_model * vec4(position, 1.0);
        });

    const char *frag = GLSL(
        in  vec3 v_normal;
        out vec4 FragColor;

        uniform vec4 u_color;
        uniform vec3 u_lightDir;
        uniform vec3 u_ambient;

        void main()
        {
            vec3  N    = normalize(v_normal);
            vec3  L    = normalize(-u_lightDir);
            float diff = max(dot(N, L), 0.0);
            vec3  lit  = u_color.rgb * (u_ambient + vec3(0.9 * diff));
            FragColor  = vec4(lit, u_color.a);
        });

    return ShaderManager::instance().loadFromSource("cube_demo_shader", vert, frag);
}
