#pragma once

#include <cmath>
#include <string>
#include <vector>

#include <glm/gtx/norm.hpp>

#include "Demo.hpp"
#include "Effects.hpp"
#include "imgui.h"

class ToyPlaneDemo : public IDemo
{
public:
    ToyPlaneDemo();

    virtual const char *title() const override;
    virtual const char *description() const override;

    virtual bool setup(Device &device) override;
    virtual void update(float dt) override;
    virtual void drawGui() override;
    virtual void render() override;
    virtual void shutdown() override;

private:
    static Shader *createLitShader();
    static Shader *createVertexAnimShader();
    static Shader *createTrailShader();
    static void setupLighting(Shader *shader);

    static std::string findToyPlaneRoot();
    std::string assetPath(const char *relativePath) const;
    std::string toyPath(const char *relativePath) const;

    bool setupAssets();
    bool setupScene();
    void updateFlight(float dt);
    void rebuildRope();
    void syncWingAnchors();
    void resetCamera();

    Device *device_;
    std::string assetRoot_;
    std::string toyRoot_;
    Scene scene_;
    Camera *camera_;

    Shader *litShader_;
    Shader *vertexAnimShader_;
    Shader *trailShader_;

    Texture *groundTexture_;
    Texture *trailTexture_;
    VertexAnimatedMesh *planeMesh_;

    Material groundMaterial_;
    Material mastMaterial_;
    Material ropeMaterial_;
    Material planeMaterial_;
    Material trailMaterial_;

    MeshNode *groundNode_;
    MeshNode *mastNode_;
    Node3D *planeRig_;
    Node3D *planeVisualRoot_;
    VertexAnimatedMeshNode *planeNode_;
    ManualMeshNode *ropeNode_;
    RibbonSheetNode *trailNode_;
    Node3D *wingLeftAnchor_;
    Node3D *wingRightAnchor_;

    glm::vec3 orbitCenter_;
    float mastHeight_;
    float orbitAngle_;
    float orbitSpeed_;
    float speedRatio_;
    float bankAngle_;
    float ropeLength_;
    float planeScale_;
    float headingOffset_;
    float trailLifetime_;
    float wingHalfSpan_;
    float wingHeightBias_;
    float wingDepthBias_;
    float trailSampleDistance_;
    float propellerFrame_;
    glm::vec3 planeVisualOffset_;
    float meshYawOffset_;
    float meshPitchOffset_;
    float meshRollOffset_;
    bool play_;
    bool showPlane_;
    bool showRope_;
    bool showTrail_;
};

inline ToyPlaneDemo::ToyPlaneDemo()
    : device_(nullptr),
      camera_(nullptr),
      litShader_(nullptr),
      vertexAnimShader_(nullptr),
      trailShader_(nullptr),
      groundTexture_(nullptr),
      trailTexture_(nullptr),
      planeMesh_(nullptr),
      groundNode_(nullptr),
      mastNode_(nullptr),
      planeRig_(nullptr),
      planeVisualRoot_(nullptr),
      planeNode_(nullptr),
      ropeNode_(nullptr),
      trailNode_(nullptr),
      wingLeftAnchor_(nullptr),
      wingRightAnchor_(nullptr),
      orbitCenter_(0.0f, 0.0f, 0.0f),
      mastHeight_(8.75f),
      orbitAngle_(0.0f),
      orbitSpeed_(360.0f),
      speedRatio_(0.7f),
      bankAngle_(48.0f),
      ropeLength_(6.0f),
      planeScale_(0.12f),
      headingOffset_(0.0f),
      trailLifetime_(0.50f),
      wingHalfSpan_(15.0f),
      wingHeightBias_(0.0f),
      wingDepthBias_(0.0f),
      trailSampleDistance_(0.10f),
      propellerFrame_(0.0f),
      planeVisualOffset_(0.0f),
      meshYawOffset_(180.0f),
      meshPitchOffset_(0.0f),
      meshRollOffset_(0.0f),
      play_(true),
      showPlane_(true),
      showRope_(true),
      showTrail_(true)
{
}

inline const char *ToyPlaneDemo::title() const
{
    return "Toy Plane";
}

inline const char *ToyPlaneDemo::description() const
{
    return "Recriacao da demo toyplane da 6DX com MD2, GUI e ribbon sheet baseada nas pontas das asas.";
}

inline std::string ToyPlaneDemo::findToyPlaneRoot()
{
    const std::vector<std::string> relativeCandidates = {
        "gdx/toyplane",
        "../gdx/toyplane",
        "../../gdx/toyplane",
    };

    for (size_t i = 0; i < relativeCandidates.size(); ++i)
    {
        if (demoPathExists(relativeCandidates[i]))
            return relativeCandidates[i];
    }

    char *basePath = SDL_GetBasePath();
    if (!basePath)
        return "gdx/toyplane";

    const std::string executableBase(basePath);
    SDL_free(basePath);

    const std::vector<std::string> executableCandidates = {
        demoJoinPath(executableBase, "../gdx/toyplane"),
        demoJoinPath(executableBase, "../../gdx/toyplane"),
        demoJoinPath(executableBase, "gdx/toyplane"),
    };

    for (size_t i = 0; i < executableCandidates.size(); ++i)
    {
        if (demoPathExists(executableCandidates[i]))
            return executableCandidates[i];
    }

    return "gdx/toyplane";
}

inline std::string ToyPlaneDemo::assetPath(const char *relativePath) const
{
    return demoJoinPath(assetRoot_, relativePath);
}

inline std::string ToyPlaneDemo::toyPath(const char *relativePath) const
{
    return demoJoinPath(toyRoot_, relativePath);
}

inline bool ToyPlaneDemo::setup(Device &device)
{
    device_ = &device;
    assetRoot_ = findProjectAssetRoot();
    toyRoot_ = findToyPlaneRoot();

    litShader_ = createLitShader();
    vertexAnimShader_ = createVertexAnimShader();
    trailShader_ = createTrailShader();
    if (!litShader_ || !vertexAnimShader_ || !trailShader_)
    {
        shutdown();
        return false;
    }

    if (!setupAssets() || !setupScene())
    {
        shutdown();
        return false;
    }

    resetCamera();
    return true;
}

inline bool ToyPlaneDemo::setupAssets()
{
    groundTexture_ = TextureManager::instance().load(
        "toyplane_ground_tex",
        assetPath("terr_dirt-grass.jpg"));
    if (!groundTexture_)
        groundTexture_ = TextureManager::instance().getWhite();

    trailTexture_ = TextureManager::instance().load(
        "toyplane_trail_tex",
        demoPathExists(toyPath("trail+alpha.tga")) ? toyPath("trail+alpha.tga") : assetPath("trail.png"));
    if (!trailTexture_)
        trailTexture_ = TextureManager::instance().getWhite();

    planeMesh_ = VertexAnimatedMeshManager::instance().load(
        "toyplane_airplane",
        toyPath("md2/airplane.md2"),
        toyPath("md2"));
    if (!planeMesh_)
        return false;

    groundMaterial_.name = "toyplane_ground_mat";
    groundMaterial_.type = MaterialType::Custom;
    groundMaterial_.setVec4("u_color", glm::vec4(0.90f, 0.92f, 0.86f, 1.0f));
    groundMaterial_.setTexture("u_albedo", groundTexture_);

    mastMaterial_.name = "toyplane_mast_mat";
    mastMaterial_.type = MaterialType::Custom;
    mastMaterial_.setVec4("u_color", glm::vec4(0.74f, 0.67f, 0.55f, 1.0f));
    mastMaterial_.setTexture("u_albedo", TextureManager::instance().getWhite());

    ropeMaterial_.name = "toyplane_rope_mat";
    ropeMaterial_.type = MaterialType::Custom;
    ropeMaterial_.setVec4("u_color", glm::vec4(0.97f, 0.96f, 0.93f, 1.0f));
    ropeMaterial_.setTexture("u_albedo", TextureManager::instance().getWhite());
    ropeMaterial_.setCullFace(false);

    planeMaterial_.name = "toyplane_plane_mat";
    planeMaterial_.type = MaterialType::Custom;
    planeMaterial_.setVec4("u_color", glm::vec4(1.0f));
    Texture *planeTexture = nullptr;
    if (!planeMesh_->materials.empty() && planeMesh_->materials[0])
        planeTexture = planeMesh_->materials[0]->getTexture("u_albedo");
    if (!planeTexture)
        planeTexture = TextureManager::instance().load("toyplane_manual_skin", toyPath("md2/airplane.pcx"));
    if (!planeTexture)
        planeTexture = TextureManager::instance().getWhite();
    planeMaterial_.setTexture("u_albedo", planeTexture);

    trailMaterial_.name = "toyplane_trail_mat";
    trailMaterial_.type = MaterialType::Custom;
    trailMaterial_.setVec4("u_color", glm::vec4(1.0f));
    trailMaterial_.setTexture("u_albedo", trailTexture_);
    trailMaterial_.setBlend(true);
    trailMaterial_.setDepthWrite(false);
    trailMaterial_.setCullFace(false);

    return true;
}

inline bool ToyPlaneDemo::setupScene()
{
    camera_ = scene_.createFreeCamera("toyplane_camera",
                                      device_->GetWidth(), device_->GetHeight(),
                                      glm::vec3(10.0f, 7.5f, 12.5f),
                                      glm::vec3(0.0f, 3.0f, 0.0f),
                                      10.0f, 0.18f, 3.0f);
    if (!camera_)
        return false;
    camera_->clearColorVal = glm::vec4(0.68f, 0.80f, 0.93f, 1.0f);

    Mesh *groundMesh = MeshManager::instance().create_plane("toyplane_ground", 36.0f, 36.0f, 18);
    Mesh *mastMesh = MeshManager::instance().create_cube("toyplane_mast", 1.0f);
    if (!groundMesh || !mastMesh)
        return false;

    groundNode_ = scene_.createMeshNode("toyplane_ground_node", groundMesh);
    mastNode_ = scene_.createMeshNode("toyplane_mast_node", mastMesh);
    if (!groundNode_ || !mastNode_)
        return false;

    groundNode_->renderType = RenderType::Solid;
    groundNode_->setMaterial(0, &groundMaterial_);

    mastNode_->renderType = RenderType::Solid;
    mastNode_->setMaterial(0, &mastMaterial_);
    mastNode_->setPosition(orbitCenter_.x, mastHeight_ * 0.5f, orbitCenter_.z);
    mastNode_->setScale(glm::vec3(0.18f, mastHeight_ * 0.5f, 0.18f));

    planeRig_ = new Node3D();
    planeRig_->name = "toyplane_rig";
    scene_.add(planeRig_);

    planeVisualRoot_ = new Node3D();
    planeVisualRoot_->name = "toyplane_visual_root";
    planeRig_->addChild(planeVisualRoot_);

    planeNode_ = scene_.createVertexAnimatedMeshNode("toyplane_plane", planeMesh_);
    if (!planeRig_ || !planeVisualRoot_ || !planeNode_)
        return false;
    planeNode_->setParent(planeVisualRoot_);
    planeNode_->renderType = RenderType::Special;
    planeNode_->setMaterial(0, &planeMaterial_);
    planeNode_->frameAnimator.clearAnimations();
    planeNode_->setFrame(0.0f);
    planeVisualOffset_ = -planeMesh_->aabb.center();
    planeVisualRoot_->setScale(glm::vec3(planeScale_));
    planeVisualRoot_->setEulerAngles(glm::vec3(meshPitchOffset_, meshYawOffset_, meshRollOffset_));
    planeNode_->setPosition(planeVisualOffset_);
    planeNode_->setScale(glm::vec3(1.0f));
    planeNode_->setEulerAngles(glm::vec3(0.0f));

    wingLeftAnchor_ = new Node3D();
    wingLeftAnchor_->name = "toyplane_left_wing";
    planeVisualRoot_->addChild(wingLeftAnchor_);

    wingRightAnchor_ = new Node3D();
    wingRightAnchor_->name = "toyplane_right_wing";
    planeVisualRoot_->addChild(wingRightAnchor_);

    ropeNode_ = new ManualMeshNode();
    ropeNode_->name = "toyplane_rope";
    ropeNode_->renderType = RenderType::Solid;
    ropeNode_->material = &ropeMaterial_;
    scene_.add(ropeNode_);

    trailNode_ = new RibbonSheetNode(96);
    trailNode_->name = "toyplane_trail";
    trailNode_->renderType = RenderType::Transparent;
    trailNode_->material = &trailMaterial_;
    trailNode_->setLifetime(trailLifetime_);
    trailNode_->setMinSampleDistance(trailSampleDistance_);
    trailNode_->setColors(glm::vec4(1.0f, 1.0f, 1.0f, 0.95f), glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    scene_.add(trailNode_);

    syncWingAnchors();
    updateFlight(0.0f);
    rebuildRope();
    trailNode_->clearSamples();
    if (showTrail_)
        trailNode_->addSample(wingLeftAnchor_->worldPosition(), wingRightAnchor_->worldPosition());

    return true;
}

inline void ToyPlaneDemo::update(float dt)
{
    if (!device_ || !camera_)
        return;

    if (device_->IsResize())
        camera_->setViewport(0, 0, device_->GetWidth(), device_->GetHeight());

    syncWingAnchors();
    if (trailNode_)
    {
        trailNode_->setLifetime(trailLifetime_);
        trailNode_->setMinSampleDistance(trailSampleDistance_);
        trailNode_->visible = showTrail_;
    }
    if (ropeNode_)
        ropeNode_->visible = showRope_;
    if (planeRig_)
        planeRig_->visible = showPlane_;
    if (planeVisualRoot_)
        planeVisualRoot_->visible = showPlane_;
    if (planeNode_)
        planeNode_->visible = showPlane_;

    updateFlight(dt);
    rebuildRope();
    scene_.update(dt);

    if (showTrail_ && trailNode_ && wingLeftAnchor_ && wingRightAnchor_)
        trailNode_->addSample(wingLeftAnchor_->worldPosition(), wingRightAnchor_->worldPosition());
}

inline void ToyPlaneDemo::updateFlight(float dt)
{
    if (!planeRig_ || !planeVisualRoot_ || !planeNode_)
        return;

    mastHeight_ = glm::max(mastHeight_, ropeLength_ + 0.5f);
    const float speed = glm::max(0.0f, speedRatio_);
    if (play_)
        orbitAngle_ += orbitSpeed_ * speed * dt;

    while (orbitAngle_ >= 360.0f)
        orbitAngle_ -= 360.0f;

    if (mastNode_)
    {
        mastNode_->setPosition(orbitCenter_.x, mastHeight_ * 0.5f, orbitCenter_.z);
        mastNode_->setScale(glm::vec3(0.18f, mastHeight_ * 0.5f, 0.18f));
    }

    const float topAngle = bankAngle_ * speed;
    glm::mat4 tilt = glm::rotate(glm::mat4(1.0f), glm::radians(topAngle), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 orbit = glm::rotate(glm::mat4(1.0f), glm::radians(orbitAngle_), glm::vec3(0.0f, -1.0f, 0.0f));
    const glm::vec3 orbitTop(orbitCenter_.x, mastHeight_, orbitCenter_.z);
    const glm::vec3 localPlane(0.0f, -ropeLength_, 0.0f);
    const glm::vec3 planePos = orbitTop + glm::vec3(orbit * tilt * glm::vec4(localPlane, 1.0f));
    const glm::mat4 orbitNext = glm::rotate(glm::mat4(1.0f), glm::radians(orbitAngle_ + 1.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    const glm::vec3 nextPos = orbitTop + glm::vec3(orbitNext * tilt * glm::vec4(localPlane, 1.0f));
    glm::vec3 flightDir = nextPos - planePos;
    if (glm::length2(flightDir) < 1e-8f)
        flightDir = glm::vec3(0.0f, 0.0f, -1.0f);
    else
        flightDir = glm::normalize(flightDir);

    planeRig_->setPosition(planePos);
    planeRig_->lookDirection(flightDir);
    if (headingOffset_ != 0.0f)
        planeRig_->rotate(headingOffset_, glm::vec3(0.0f, 1.0f, 0.0f), TransformSpace::Local);
    planeRig_->roll(-topAngle);

    planeVisualRoot_->setScale(glm::vec3(planeScale_));
    planeVisualRoot_->setEulerAngles(glm::vec3(meshPitchOffset_, meshYawOffset_, meshRollOffset_));
    planeNode_->setPosition(planeVisualOffset_);
    planeNode_->setScale(glm::vec3(1.0f));
    planeNode_->setEulerAngles(glm::vec3(0.0f));

    if (play_ && planeMesh_ && planeMesh_->frameCount() > 1)
    {
        propellerFrame_ += dt * (8.0f + speed * 36.0f);
        while (propellerFrame_ >= (float)planeMesh_->frameCount())
            propellerFrame_ -= (float)planeMesh_->frameCount();
        planeNode_->setFrame(propellerFrame_);
    }
}

inline void ToyPlaneDemo::syncWingAnchors()
{
    if (!wingLeftAnchor_ || !wingRightAnchor_)
        return;

    const glm::vec3 anchorBase = planeVisualOffset_ + glm::vec3(0.0f, wingHeightBias_, wingDepthBias_);
    wingLeftAnchor_->setPosition(anchorBase + glm::vec3(-wingHalfSpan_, 0.0f, 0.0f));
    wingRightAnchor_->setPosition(anchorBase + glm::vec3(wingHalfSpan_, 0.0f, 0.0f));
}

inline void ToyPlaneDemo::rebuildRope()
{
    if (!ropeNode_ || !planeRig_)
        return;

    ropeNode_->clear();
    std::vector<Vertex> &verts = ropeNode_->vertices();
    std::vector<uint32_t> &indices = ropeNode_->indices();

    const glm::vec3 top(orbitCenter_.x, mastHeight_, orbitCenter_.z);
    const glm::vec3 bottom = planeRig_->worldPosition();
    const float ropeHalfWidth = 0.05f;
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);

    auto pushQuad = [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d) {
        const uint32_t base = (uint32_t)verts.size();
        verts.push_back({a, glm::vec3(0.0f), tangent, glm::vec2(0.0f, 0.0f)});
        verts.push_back({b, glm::vec3(0.0f), tangent, glm::vec2(1.0f, 0.0f)});
        verts.push_back({c, glm::vec3(0.0f), tangent, glm::vec2(1.0f, 1.0f)});
        verts.push_back({d, glm::vec3(0.0f), tangent, glm::vec2(0.0f, 1.0f)});
        indices.insert(indices.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
    };

    pushQuad(top + glm::vec3(-ropeHalfWidth, 0.0f, 0.0f),
             top + glm::vec3( ropeHalfWidth, 0.0f, 0.0f),
             bottom + glm::vec3( ropeHalfWidth, 0.0f, 0.0f),
             bottom + glm::vec3(-ropeHalfWidth, 0.0f, 0.0f));

    pushQuad(top + glm::vec3(0.0f, 0.0f, -ropeHalfWidth),
             top + glm::vec3(0.0f, 0.0f,  ropeHalfWidth),
             bottom + glm::vec3(0.0f, 0.0f,  ropeHalfWidth),
             bottom + glm::vec3(0.0f, 0.0f, -ropeHalfWidth));

    ropeNode_->computeNormals();
    ropeNode_->build();
}

inline void ToyPlaneDemo::drawGui()
{
    ImGui::SetNextWindowPos(ImVec2(16, 72), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Once);
    if (!ImGui::Begin("Toy Plane"))
    {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped("%s", description());
    ImGui::TextWrapped("Toyplane root: %s", toyRoot_.c_str());

    if (ImGui::Button("Reset Camera"))
        resetCamera();
    ImGui::SameLine();
    if (ImGui::Button("Reset Trail") && trailNode_)
        trailNode_->clearSamples();

    ImGui::Checkbox("Play", &play_);
    ImGui::Checkbox("Plane", &showPlane_);
    ImGui::Checkbox("Rope", &showRope_);
    if (ImGui::Checkbox("Trail", &showTrail_) && !showTrail_ && trailNode_)
        trailNode_->clearSamples();

    ImGui::SeparatorText("Flight");
    ImGui::SliderFloat("Speed Ratio", &speedRatio_, 0.0f, 1.2f, "%.2f");
    ImGui::SliderFloat("Orbit Speed", &orbitSpeed_, 45.0f, 540.0f, "%.1f deg/s");
    ImGui::SliderFloat("Bank Angle", &bankAngle_, 0.0f, 75.0f, "%.1f deg");
    ImGui::SliderFloat("Mast Height", &mastHeight_, 3.0f, 18.0f, "%.2f");
    ImGui::SliderFloat("Rope Length", &ropeLength_, 2.0f, 10.0f, "%.2f");
    ImGui::SliderFloat("Heading Offset", &headingOffset_, -180.0f, 180.0f, "%.1f deg");
    ImGui::SliderFloat("Plane Scale", &planeScale_, 0.04f, 0.22f, "%.3f");

    ImGui::SeparatorText("Trail");
    ImGui::SliderFloat("Trail Lifetime", &trailLifetime_, 0.15f, 1.5f, "%.2f s");
    ImGui::SliderFloat("Sample Distance", &trailSampleDistance_, 0.01f, 0.35f, "%.2f");
    ImGui::SliderFloat("Wing Half Span", &wingHalfSpan_, 5.0f, 80.0f, "%.1f");
    ImGui::SliderFloat("Wing Height", &wingHeightBias_, -20.0f, 20.0f, "%.2f");
    ImGui::SliderFloat("Wing Depth", &wingDepthBias_, -20.0f, 20.0f, "%.2f");

    ImGui::SeparatorText("Mesh Align");
    ImGui::SliderFloat("Mesh Offset X", &planeVisualOffset_.x, -80.0f, 80.0f, "%.2f");
    ImGui::SliderFloat("Mesh Offset Y", &planeVisualOffset_.y, -80.0f, 80.0f, "%.2f");
    ImGui::SliderFloat("Mesh Offset Z", &planeVisualOffset_.z, -80.0f, 80.0f, "%.2f");
    ImGui::SliderFloat("Mesh Yaw", &meshYawOffset_, -180.0f, 180.0f, "%.1f deg");
    ImGui::SliderFloat("Mesh Pitch", &meshPitchOffset_, -180.0f, 180.0f, "%.1f deg");
    ImGui::SliderFloat("Mesh Roll", &meshRollOffset_, -180.0f, 180.0f, "%.1f deg");

    ImGui::SeparatorText("Stats");
    ImGui::Text("MD2 frames: %d", planeMesh_ ? planeMesh_->frameCount() : 0);
    ImGui::Text("Trail samples: %d", trailNode_ ? trailNode_->sampleCount() : 0);
    ImGui::Text("Orbit angle: %.1f", orbitAngle_);

    ImGui::End();
}

inline void ToyPlaneDemo::render()
{
    if (!camera_)
        return;

    scene_.setCamera(camera_);
    scene_.beginPass();

    scene_.setShader(litShader_);
    setupLighting(litShader_);
    scene_.render(RenderType::Solid);

    scene_.setShader(vertexAnimShader_);
    setupLighting(vertexAnimShader_);
    scene_.render(RenderType::Special);

    scene_.setShader(trailShader_);
    scene_.render(RenderType::Transparent);

    scene_.endPass();
}

inline void ToyPlaneDemo::resetCamera()
{
    if (!camera_)
        return;

    camera_->setPosition(glm::vec3(10.0f, 7.5f, 12.5f));
    camera_->lookAt(glm::vec3(0.0f, 3.5f, 0.0f));
    camera_->updateMatrices();
}

inline void ToyPlaneDemo::shutdown()
{
    scene_.clear();
    unloadDemoAssets();

    camera_ = nullptr;
    litShader_ = nullptr;
    vertexAnimShader_ = nullptr;
    trailShader_ = nullptr;
    groundTexture_ = nullptr;
    trailTexture_ = nullptr;
    planeMesh_ = nullptr;
    groundNode_ = nullptr;
    mastNode_ = nullptr;
    planeRig_ = nullptr;
    planeVisualRoot_ = nullptr;
    planeNode_ = nullptr;
    ropeNode_ = nullptr;
    trailNode_ = nullptr;
    wingLeftAnchor_ = nullptr;
    wingRightAnchor_ = nullptr;
    assetRoot_.clear();
    toyRoot_.clear();
}

inline Shader *ToyPlaneDemo::createLitShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 3) in vec2 uv;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;
        uniform mat3 u_normalMatrix;

        out vec3 v_normal;
        out vec2 v_uv;

        void main()
        {
            v_normal = normalize(u_normalMatrix * normal);
            v_uv = uv;
            gl_Position = u_projection * u_view * u_model * vec4(position, 1.0);
        });

    const char *frag = GLSL(
        in vec3 v_normal;
        in vec2 v_uv;
        out vec4 FragColor;

        uniform vec4 u_color;
        uniform sampler2D u_albedo;
        uniform vec3 u_lightDir;
        uniform vec3 u_ambient;

        void main()
        {
            vec3 N = normalize(v_normal);
            vec3 L = normalize(-u_lightDir);
            float diff = max(dot(N, L), 0.0);
            vec4 albedo = texture(u_albedo, v_uv) * u_color;
            vec3 lit = albedo.rgb * (u_ambient + vec3(0.9 * diff));
            FragColor = vec4(lit, albedo.a);
        });

    return ShaderManager::instance().loadFromSource("toyplane_lit_shader", vert, frag);
}

inline Shader *ToyPlaneDemo::createVertexAnimShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        layout(location = 2) in vec3 normal;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;
        uniform mat3 u_normalMatrix;

        out vec3 v_normal;
        out vec2 v_uv;

        void main()
        {
            v_normal = normalize(u_normalMatrix * normal);
            v_uv = uv;
            gl_Position = u_projection * u_view * u_model * vec4(position, 1.0);
        });

    const char *frag = GLSL(
        in vec3 v_normal;
        in vec2 v_uv;
        out vec4 FragColor;

        uniform vec4 u_color;
        uniform sampler2D u_albedo;
        uniform vec3 u_lightDir;
        uniform vec3 u_ambient;

        void main()
        {
            vec3 N = normalize(v_normal);
            vec3 L = normalize(-u_lightDir);
            float diff = max(dot(N, L), 0.0);
            vec4 albedo = texture(u_albedo, v_uv) * u_color;
            vec3 lit = albedo.rgb * (u_ambient + vec3(0.85 * diff));
            FragColor = vec4(lit, albedo.a);
        });

    return ShaderManager::instance().loadFromSource("toyplane_vertex_anim_shader", vert, frag);
}

inline Shader *ToyPlaneDemo::createTrailShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        layout(location = 2) in vec4 color;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;

        out vec2 v_uv;
        out vec4 v_color;

        void main()
        {
            v_uv = uv;
            v_color = color;
            gl_Position = u_projection * u_view * u_model * vec4(position, 1.0);
        });

    const char *frag = GLSL(
        in vec2 v_uv;
        in vec4 v_color;
        out vec4 FragColor;

        uniform vec4 u_color;
        uniform sampler2D u_albedo;

        void main()
        {
            FragColor = texture(u_albedo, v_uv) * v_color * u_color;
        });

    return ShaderManager::instance().loadFromSource("toyplane_trail_shader", vert, frag);
}

inline void ToyPlaneDemo::setupLighting(Shader *shader)
{
    if (!shader)
        return;

    shader->setVec3("u_lightDir", glm::normalize(glm::vec3(-0.45f, -1.0f, -0.25f)));
    shader->setVec3("u_ambient", glm::vec3(0.20f, 0.22f, 0.24f));
}
