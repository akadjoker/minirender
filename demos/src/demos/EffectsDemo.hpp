#pragma once

#include <string>
#include <vector>

#include "Demo.hpp"
#include "Effects.hpp"
#include "ParticleNode.hpp"
#include <WidgetApp.hpp>
#include <ViewWidgets.hpp>
#include <BasicWidgets.hpp>

class EffectsDemo : public IDemo
{
public:
    EffectsDemo();

    virtual const char *title() const override;
    virtual const char *description() const override;

    virtual bool setup(Device &device) override;
    virtual void update(float dt) override;
    virtual void drawGui() override;
    virtual void render() override;
    virtual void shutdown() override;

private:
    static Shader *createLitShader();
    static Shader *createParticleShader();
    static Shader *createDecalShader();
    static Shader *createGrassShader();
    static Shader *createOverlayShader();
    static void setupLighting(Shader *shader);

    std::string assetPath(const char *relativePath) const;
    std::string pickFirstExistingAsset(const std::vector<const char *> &relativePaths) const;
    bool setupAssets();
    bool setupScene();
    void buildManualMesh();
    void syncVisibility();
    void resetCamera();
    void spawnTimedDecal();

    Device *device_;
    std::string assetRoot_;
    Scene scene_;
    Camera *camera_;

    Shader *litShader_;
    Shader *particleShader_;
    Shader *decalShader_;
    Shader *grassShader_;
    Shader *overlayShader_;

    Texture *groundTexture_;
    Texture *particleTexture_;
    Texture *decalTexture_;
    Texture *grassTexture_;
    Texture *flareTexture_;
    Texture *trailTexture_;

    Material groundMaterial_;
    Material wallMaterial_;
    Material manualMaterial_;
    Material particleMaterial_;
    Material sparkMaterial_;
    Material decalMaterial_;
    Material grassMaterial_;
    Material flareMaterial_;
    Material trailMaterial_;
    Material trailMarkerMaterialA_;
    Material trailMarkerMaterialB_;

    MeshNode *groundNode_;
    MeshNode *wallNode_;
    MeshNode *pedestalNode_;
    ManualMeshNode *manualMeshNode_;
    ParticleSystemNode *smokeEmitter_;
    ParticleSystemNode *sparkEmitter_;
    DecalNode *decalNode_;
    GrassNode *grassNode_;
    LensFlareNode *lensFlareNode_;
    RibbonTrailNode *ribbonTrailNode_;
    MeshNode *trailMarkerA_;
    MeshNode *trailMarkerB_;

    bool showParticles_;
    bool showDecals_;
    bool showGrass_;
    bool showLensFlare_;
    bool showManualMesh_;
    bool showRibbonTrail_;
    float decalTimer_;
    float time_;
    float flareYaw_;

    // Retained widgets
    BuGUI::FloatWindow* fw_  = nullptr;
    BuGUI::Label* smokeLabel_  = nullptr;
    BuGUI::Label* sparkLabel_  = nullptr;
    BuGUI::Label* decalLabel_  = nullptr;
    BuGUI::Label* grassLabel_  = nullptr;
    BuGUI::Label* manualLabel_ = nullptr;
    BuGUI::Label* ribbonLabel_ = nullptr;
};

inline EffectsDemo::EffectsDemo()
    : device_(nullptr),
      camera_(nullptr),
      litShader_(nullptr),
      particleShader_(nullptr),
      decalShader_(nullptr),
      grassShader_(nullptr),
      overlayShader_(nullptr),
      groundTexture_(nullptr),
      particleTexture_(nullptr),
      decalTexture_(nullptr),
      grassTexture_(nullptr),
      flareTexture_(nullptr),
      trailTexture_(nullptr),
      groundNode_(nullptr),
      wallNode_(nullptr),
      pedestalNode_(nullptr),
      manualMeshNode_(nullptr),
      smokeEmitter_(nullptr),
      sparkEmitter_(nullptr),
      decalNode_(nullptr),
      grassNode_(nullptr),
      lensFlareNode_(nullptr),
      ribbonTrailNode_(nullptr),
      trailMarkerA_(nullptr),
      trailMarkerB_(nullptr),
      showParticles_(true),
      showDecals_(true),
      showGrass_(true),
      showLensFlare_(true),
      showManualMesh_(true),
      showRibbonTrail_(true),
      decalTimer_(0.0f),
      time_(0.0f),
      flareYaw_(-28.0f)
{
}

inline const char *EffectsDemo::title() const
{
    return "Effects Lab";
}

inline const char *EffectsDemo::description() const
{
    return "Cena de validacao para particulas, decals, grass, lens flare e manual mesh.";
}

inline bool EffectsDemo::setup(Device &device)
{
    device_ = &device;
    assetRoot_ = findProjectAssetRoot();

    litShader_ = createLitShader();
    particleShader_ = createParticleShader();
    decalShader_ = createDecalShader();
    grassShader_ = createGrassShader();
    overlayShader_ = createOverlayShader();
    if (!litShader_ || !particleShader_ || !decalShader_ || !grassShader_ || !overlayShader_)
    {
        shutdown();
        return false;
    }

    if (!setupAssets() || !setupScene())
    {
        shutdown();
        return false;
    }

    syncVisibility();
    resetCamera();

    // ── Retained UI ────────────────────────────────────────────────
    fw_ = BuGUI::WidgetApp::instance().addFloat<BuGUI::FloatWindow>("Effects");
    fw_->setFloatPos(16.0f, 72.0f);
    fw_->setFloatSize(280.0f, 320.0f);
    fw_->setClosable(false);
    fw_->setResizable(false);

    auto* vbox = fw_->setContent<BuGUI::BoxLayout>(BuGUI::LayoutDir::Vertical);
    vbox->setSpacing(4.0f);
    vbox->setPadding(4.0f);

    vbox->createChild<BuGUI::Label>(description());

    auto* btnReset = vbox->createChild<BuGUI::Button>("Reset Camera");
    btnReset->clicked.connect([this]() { resetCamera(); });

    auto mkCheck = [&](const char* text, bool& flag) {
        auto* cb = vbox->createChild<BuGUI::CheckBox>(text);
        cb->setChecked(flag);
        cb->toggled.connect([this, &flag](bool v) { flag = v; syncVisibility(); });
    };
    mkCheck("Particles",    showParticles_);
    mkCheck("Decals",       showDecals_);
    mkCheck("Grass",        showGrass_);
    mkCheck("Lens Flare",   showLensFlare_);
    mkCheck("Manual Mesh",  showManualMesh_);
    mkCheck("Ribbon Trail", showRibbonTrail_);

    smokeLabel_  = vbox->createChild<BuGUI::Label>("Smoke: 0");
    sparkLabel_  = vbox->createChild<BuGUI::Label>("Spark: 0");
    decalLabel_  = vbox->createChild<BuGUI::Label>("Decals: 0");
    grassLabel_  = vbox->createChild<BuGUI::Label>("Grass: 0");
    manualLabel_ = vbox->createChild<BuGUI::Label>("Manual verts: 0");
    ribbonLabel_ = vbox->createChild<BuGUI::Label>("Ribbon chains: 0");

    return true;
}

inline void EffectsDemo::update(float dt)
{
    if (!device_ || !camera_)
        return;

    if (device_->IsResize())
        camera_->setViewport(0, 0, device_->GetWidth(), device_->GetHeight());

    time_ += dt;
    decalTimer_ += dt;

    if (manualMeshNode_)
    {
        manualMeshNode_->yaw(18.0f * dt);
        manualMeshNode_->setPosition(0.0f, 1.7f + std::sin(time_ * 1.7f) * 0.15f, 0.0f);
    }

    const float orbitA = time_ * 1.8f;
    const float orbitB = -time_ * 1.35f + 1.4f;
    const glm::vec3 markerPosA(std::cos(orbitA) * 2.4f, 1.7f + std::sin(time_ * 2.1f) * 0.35f, std::sin(orbitA) * 2.4f);
    const glm::vec3 markerPosB(std::cos(orbitB) * 1.7f, 2.6f + std::cos(time_ * 1.4f) * 0.45f, std::sin(orbitB) * 1.7f);
    if (trailMarkerA_)
        trailMarkerA_->setPosition(markerPosA);
    if (trailMarkerB_)
        trailMarkerB_->setPosition(markerPosB);

    if (lensFlareNode_)
    {
        flareYaw_ += dt * 4.0f;
        const glm::vec3 dir = glm::normalize(glm::vec3(std::sin(glm::radians(flareYaw_)), -0.85f, std::cos(glm::radians(flareYaw_))));
        lensFlareNode_->setSunDirection(dir);
    }

    if (showDecals_ && decalTimer_ >= 0.45f)
    {
        spawnTimedDecal();
        decalTimer_ = 0.0f;
    }

    scene_.update(dt);
}

inline void EffectsDemo::drawGui()
{
    if (smokeLabel_)  smokeLabel_ ->setText("Smoke: "  + std::to_string(smokeEmitter_    ? smokeEmitter_->activeCount()        : 0));
    if (sparkLabel_)  sparkLabel_ ->setText("Spark: "  + std::to_string(sparkEmitter_    ? sparkEmitter_->activeCount()        : 0));
    if (decalLabel_)  decalLabel_ ->setText("Decals: " + std::to_string(decalNode_        ? decalNode_->activeCount()           : 0));
    if (grassLabel_)  grassLabel_ ->setText("Grass: "  + std::to_string(grassNode_        ? grassNode_->clumpCount()            : 0));
    if (manualLabel_) manualLabel_->setText("Manual verts: " + std::to_string(manualMeshNode_ ? manualMeshNode_->vertexCount() : 0));
    if (ribbonLabel_) ribbonLabel_->setText("Ribbon chains: " + std::to_string(ribbonTrailNode_? ribbonTrailNode_->activeChainCount() : 0));
}

inline void EffectsDemo::render()
{
    if (!camera_)
        return;

    scene_.setCamera(camera_);
    scene_.beginPass();

    scene_.setShader(litShader_);
    setupLighting(litShader_);
    scene_.render(RenderType::Solid);

    scene_.setShader(grassShader_);
    setupLighting(grassShader_);
    scene_.render(RenderType::Terrain);

    scene_.setShader(decalShader_);
    scene_.render(RenderType::Special);

    scene_.setShader(particleShader_);
    scene_.render(RenderType::Transparent);

    scene_.setShader(overlayShader_);
    scene_.render(RenderType::Overlay);

    scene_.endPass();
}

inline void EffectsDemo::shutdown()
{
    scene_.clear();
    unloadDemoAssets();

    if (fw_) { BuGUI::WidgetApp::instance().removeFloat(fw_); fw_ = nullptr; }
    smokeLabel_ = sparkLabel_ = decalLabel_ = grassLabel_ = manualLabel_ = ribbonLabel_ = nullptr;

    camera_ = nullptr;
    litShader_ = nullptr;
    particleShader_ = nullptr;
    decalShader_ = nullptr;
    grassShader_ = nullptr;
    overlayShader_ = nullptr;
    groundTexture_ = nullptr;
    particleTexture_ = nullptr;
    decalTexture_ = nullptr;
    grassTexture_ = nullptr;
    flareTexture_ = nullptr;
    trailTexture_ = nullptr;
    groundNode_ = nullptr;
    wallNode_ = nullptr;
    pedestalNode_ = nullptr;
    manualMeshNode_ = nullptr;
    smokeEmitter_ = nullptr;
    sparkEmitter_ = nullptr;
    decalNode_ = nullptr;
    grassNode_ = nullptr;
    lensFlareNode_ = nullptr;
    ribbonTrailNode_ = nullptr;
    trailMarkerA_ = nullptr;
    trailMarkerB_ = nullptr;
    assetRoot_.clear();
}

inline Shader *EffectsDemo::createLitShader()
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

    return ShaderManager::instance().loadFromSource("effects_demo_lit_shader", vert, frag);
}

inline Shader *EffectsDemo::createParticleShader()
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

        uniform sampler2D u_albedo;
        uniform vec4 u_color;

        void main()
        {
            vec4 tex = texture(u_albedo, v_uv);
            FragColor = tex * v_color * u_color;
        });

    return ShaderManager::instance().loadFromSource("effects_demo_particle_shader", vert, frag);
}

inline Shader *EffectsDemo::createDecalShader()
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

        uniform sampler2D u_albedo;

        void main()
        {
            FragColor = texture(u_albedo, v_uv) * v_color;
        });

    return ShaderManager::instance().loadFromSource("effects_demo_decal_shader", vert, frag);
}

inline Shader *EffectsDemo::createGrassShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 3) in vec2 uv;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;
        uniform float u_time;
        uniform float u_windStrength;
        uniform float u_windSpeed;
        uniform vec2 u_windDir;

        out vec3 v_normal;
        out vec2 v_uv;

        void main()
        {
            vec4 worldPos = u_model * vec4(position, 1.0);
            float blade = 1.0 - uv.y;
            float phase = dot(worldPos.xz, u_windDir) * 0.45 + u_time * u_windSpeed;
            float sway = sin(phase) * u_windStrength * blade;
            worldPos.x += u_windDir.x * sway;
            worldPos.z += u_windDir.y * sway;

            v_normal = normalize(mat3(transpose(inverse(u_model))) * normal);
            v_uv = uv;
            gl_Position = u_projection * u_view * worldPos;
        });

    const char *frag = GLSL(
        in vec3 v_normal;
        in vec2 v_uv;
        out vec4 FragColor;

        uniform sampler2D u_albedo;
        uniform vec3 u_lightDir;
        uniform vec3 u_lightColor;
        uniform vec3 u_ambientColor;

        void main()
        {
            vec4 albedo = texture(u_albedo, v_uv);
            if (albedo.a < 0.5)
                discard;

            vec3 norm = normalize(v_normal);
            float ndotl = abs(dot(norm, normalize(-u_lightDir)));
            float diff = max(ndotl, 0.12);
            float rootFactor = 0.5 + 0.5 * (1.0 - v_uv.y);
            vec3 color = albedo.rgb * (u_ambientColor + diff * u_lightColor) * rootFactor;
            FragColor = vec4(color, 1.0);
        });

    return ShaderManager::instance().loadFromSource("effects_demo_grass_shader", vert, frag);
}

inline Shader *EffectsDemo::createOverlayShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec2 uv;
        layout(location = 2) in vec4 color;

        out vec2 v_uv;
        out vec4 v_color;

        void main()
        {
            v_uv = uv;
            v_color = color;
            gl_Position = vec4(position, 1.0);
        });

    const char *frag = GLSL(
        in vec2 v_uv;
        in vec4 v_color;
        out vec4 FragColor;

        uniform sampler2D u_albedo;

        void main()
        {
            FragColor = texture(u_albedo, v_uv) * v_color;
        });

    return ShaderManager::instance().loadFromSource("effects_demo_overlay_shader", vert, frag);
}

inline void EffectsDemo::setupLighting(Shader *shader)
{
    if (!shader)
        return;

    shader->setVec3("u_lightDir", glm::normalize(glm::vec3(-0.45f, -1.0f, -0.25f)));
    shader->setVec3("u_ambient", glm::vec3(0.18f, 0.20f, 0.22f));
    shader->setVec3("u_lightColor", glm::vec3(0.95f, 0.93f, 0.88f));
    shader->setVec3("u_ambientColor", glm::vec3(0.20f, 0.22f, 0.18f));
}

inline std::string EffectsDemo::assetPath(const char *relativePath) const
{
    return demoJoinPath(assetRoot_, relativePath);
}

inline std::string EffectsDemo::pickFirstExistingAsset(const std::vector<const char *> &relativePaths) const
{
    for (size_t i = 0; i < relativePaths.size(); ++i)
    {
        const std::string candidate = assetPath(relativePaths[i]);
        if (demoPathExists(candidate))
            return candidate;
    }

    return relativePaths.empty() ? std::string() : assetPath(relativePaths[0]);
}

inline bool EffectsDemo::setupAssets()
{
    groundTexture_ = TextureManager::instance().load(
        "effects_demo_ground_tex",
        pickFirstExistingAsset({"terr_dirt-grass.jpg", "terrain-texture.jpg", "terrain_texture.jpg"}));
    particleTexture_ = TextureManager::instance().load(
        "effects_demo_particle_tex",
        pickFirstExistingAsset({"trail.png", "foam.png", "light.jpg"}));
    decalTexture_ = TextureManager::instance().load(
        "effects_demo_decal_tex",
        pickFirstExistingAsset({"decal.png", "foam.png"}));
    grassTexture_ = TextureManager::instance().load(
        "effects_demo_grass_tex",
        pickFirstExistingAsset({"grass2.png", "grass1.png"}));
    flareTexture_ = TextureManager::instance().load(
        "effects_demo_flare_tex",
        pickFirstExistingAsset({"flares.png", "trail.png"}));
    trailTexture_ = TextureManager::instance().load(
        "effects_demo_trail_tex",
        pickFirstExistingAsset({"trail.png", "foam.png", "light.jpg"}));

    if (!groundTexture_) groundTexture_ = TextureManager::instance().getWhite();
    if (!particleTexture_) particleTexture_ = TextureManager::instance().getWhite();
    if (!decalTexture_) decalTexture_ = TextureManager::instance().getWhite();
    if (!grassTexture_) grassTexture_ = TextureManager::instance().getWhite();
    if (!flareTexture_) flareTexture_ = TextureManager::instance().getWhite();
    if (!trailTexture_) trailTexture_ = TextureManager::instance().getWhite();

    groundMaterial_.name = "effects_demo_ground_material";
    groundMaterial_.type = MaterialType::Custom;
    groundMaterial_.setVec4("u_color", glm::vec4(0.90f, 0.92f, 0.95f, 1.0f));
    groundMaterial_.setTexture("u_albedo", groundTexture_);

    wallMaterial_.name = "effects_demo_wall_material";
    wallMaterial_.type = MaterialType::Custom;
    wallMaterial_.setVec4("u_color", glm::vec4(0.78f, 0.82f, 0.88f, 1.0f));
    wallMaterial_.setTexture("u_albedo", TextureManager::instance().getWhite());

    manualMaterial_.name = "effects_demo_manual_material";
    manualMaterial_.type = MaterialType::Custom;
    manualMaterial_.setVec4("u_color", glm::vec4(0.28f, 0.82f, 0.92f, 1.0f));
    manualMaterial_.setTexture("u_albedo", TextureManager::instance().getWhite());

    particleMaterial_.name = "effects_demo_particle_material";
    particleMaterial_.type = MaterialType::Custom;
    particleMaterial_.setVec4("u_color", glm::vec4(1.0f));
    particleMaterial_.setTexture("u_albedo", particleTexture_);
    particleMaterial_.setBlend(true);
    particleMaterial_.setDepthWrite(false);
    particleMaterial_.setCullFace(false);

    sparkMaterial_.name = "effects_demo_spark_material";
    sparkMaterial_.type = MaterialType::Custom;
    sparkMaterial_.setVec4("u_color", glm::vec4(1.0f, 0.85f, 0.45f, 1.0f));
    sparkMaterial_.setTexture("u_albedo", flareTexture_);
    sparkMaterial_.setBlend(true);
    sparkMaterial_.setBlendFunc(GL_SRC_ALPHA, GL_ONE);
    sparkMaterial_.setDepthWrite(false);
    sparkMaterial_.setCullFace(false);

    decalMaterial_.name = "effects_demo_decal_material";
    decalMaterial_.type = MaterialType::Custom;
    decalMaterial_.setTexture("u_albedo", decalTexture_);
    decalMaterial_.setBlend(true);
    decalMaterial_.setDepthWrite(false);
    decalMaterial_.setCullFace(false);

    grassMaterial_.name = "effects_demo_grass_material";
    grassMaterial_.type = MaterialType::Custom;
    grassMaterial_.setTexture("u_albedo", grassTexture_);
    grassMaterial_.setFloat("u_windStrength", 0.26f);
    grassMaterial_.setFloat("u_windSpeed", 1.4f);
    grassMaterial_.setVec2("u_windDir", glm::normalize(glm::vec2(1.0f, 0.25f)));
    grassMaterial_.setCullFace(false);

    flareMaterial_.name = "effects_demo_flare_material";
    flareMaterial_.type = MaterialType::Custom;
    flareMaterial_.setTexture("u_albedo", flareTexture_);
    flareMaterial_.setBlend(true);
    flareMaterial_.setBlendFunc(GL_SRC_ALPHA, GL_ONE);
    flareMaterial_.setDepthTest(false);
    flareMaterial_.setDepthWrite(false);
    flareMaterial_.setCullFace(false);

    trailMaterial_.name = "effects_demo_trail_material";
    trailMaterial_.type = MaterialType::Custom;
    trailMaterial_.setVec4("u_color", glm::vec4(0.85f, 0.95f, 1.0f, 1.0f));
    trailMaterial_.setTexture("u_albedo", trailTexture_);
    trailMaterial_.setBlend(true);
    trailMaterial_.setBlendFunc(GL_SRC_ALPHA, GL_ONE);
    trailMaterial_.setDepthWrite(false);
    trailMaterial_.setCullFace(false);

    trailMarkerMaterialA_.name = "effects_demo_trail_marker_a";
    trailMarkerMaterialA_.type = MaterialType::Custom;
    trailMarkerMaterialA_.setVec4("u_color", glm::vec4(1.0f, 0.35f, 0.35f, 1.0f));
    trailMarkerMaterialA_.setTexture("u_albedo", TextureManager::instance().getWhite());

    trailMarkerMaterialB_.name = "effects_demo_trail_marker_b";
    trailMarkerMaterialB_.type = MaterialType::Custom;
    trailMarkerMaterialB_.setVec4("u_color", glm::vec4(0.35f, 0.85f, 1.0f, 1.0f));
    trailMarkerMaterialB_.setTexture("u_albedo", TextureManager::instance().getWhite());

    return true;
}

inline bool EffectsDemo::setupScene()
{
    camera_ = scene_.createFreeCamera("effects_camera",
                                      device_->GetWidth(), device_->GetHeight(),
                                      glm::vec3(8.0f, 5.5f, 9.5f),
                                      glm::vec3(0.0f, 1.2f, 0.0f),
                                      10.0f, 0.18f, 3.0f);
    if (!camera_)
        return false;
    camera_->clearColorVal = glm::vec4(0.68f, 0.79f, 0.92f, 1.0f);

    Mesh *groundMesh = MeshManager::instance().create_plane("effects_demo_ground", 18.0f, 18.0f, 12);
    Mesh *wallMesh = MeshManager::instance().create_quad("effects_demo_wall", 6.0f, 4.0f);
    Mesh *cubeMesh = MeshManager::instance().create_cube("effects_demo_cube", 1.2f);
    Mesh *trailMarkerMesh = MeshManager::instance().create_sphere("effects_demo_trail_marker", 0.18f, 12);

    groundNode_ = scene_.createMeshNode("effects_ground", groundMesh);
    wallNode_ = scene_.createMeshNode("effects_wall", wallMesh);
    pedestalNode_ = scene_.createMeshNode("effects_pedestal", cubeMesh);
    if (!groundNode_ || !wallNode_ || !pedestalNode_)
        return false;

    groundNode_->renderType = RenderType::Solid;
    groundNode_->setMaterial(0, &groundMaterial_);
    groundNode_->setPosition(0.0f, 0.0f, 0.0f);

    wallNode_->renderType = RenderType::Solid;
    wallNode_->setMaterial(0, &wallMaterial_);
    wallNode_->setPosition(-2.0f, 2.0f, -4.5f);
    wallNode_->yaw(180.0f);

    pedestalNode_->renderType = RenderType::Solid;
    pedestalNode_->setMaterial(0, &wallMaterial_);
    pedestalNode_->setPosition(0.0f, 0.6f, 0.0f);
    pedestalNode_->setScale(glm::vec3(1.2f, 0.35f, 1.2f));

    manualMeshNode_ = new ManualMeshNode();
    manualMeshNode_->name = "effects_manual_mesh";
    manualMeshNode_->renderType = RenderType::Solid;
    manualMeshNode_->material = &manualMaterial_;
    buildManualMesh();
    manualMeshNode_->setPosition(0.0f, 1.7f, 0.0f);
    scene_.add(manualMeshNode_);

    smokeEmitter_ = new ParticleSystemNode("effects_smoke", 700);
    smokeEmitter_->renderType = RenderType::Transparent;
    smokeEmitter_->setMaterial(&particleMaterial_);
    smokeEmitter_->setPosition(0.0f, 1.0f, 0.0f);
    smokeEmitter_->setContinuous(85.0f)
                 ->setShapeCone(18.0f, 0.15f)
                 ->setEmissionDirection(glm::vec3(0.0f, 1.0f, 0.0f))
                 ->setSpreadAngle(22.0f)
                 ->setLifetime(1.5f, 2.6f)
                 ->setSpeed(0.7f, 1.4f)
                 ->setSize(glm::vec2(0.25f, 0.25f), glm::vec2(1.25f, 1.25f))
                 ->setColor(glm::vec4(1.0f, 0.65f, 0.25f, 0.9f), glm::vec4(0.18f, 0.18f, 0.20f, 0.0f))
                 ->setDrag(0.35f)
                 ->addTurbulence(0.35f, 1.6f)
                 ->addColorOverLifetime(glm::vec4(1.0f, 0.72f, 0.35f, 0.9f), glm::vec4(0.15f, 0.15f, 0.18f, 0.0f))
                 ->setAutoPlay(true);
    scene_.add(smokeEmitter_);

    sparkEmitter_ = new ParticleSystemNode("effects_sparks", 240);
    sparkEmitter_->renderType = RenderType::Transparent;
    sparkEmitter_->setMaterial(&sparkMaterial_);
    sparkEmitter_->setPosition(0.0f, 1.8f, 0.0f);
    sparkEmitter_->setPulse(2.2f, 16)
                 ->setShapeSphere(0.15f)
                 ->setEmissionDirection(glm::vec3(0.0f, 1.0f, 0.0f))
                 ->setSpreadAngle(65.0f)
                 ->setLifetime(0.5f, 0.9f)
                 ->setSpeed(2.6f, 4.6f)
                 ->setSize(0.10f, 0.03f)
                 ->setColor(glm::vec4(1.0f, 0.9f, 0.45f, 1.0f), glm::vec4(1.0f, 0.25f, 0.05f, 0.0f))
                 ->setDrag(0.2f)
                 ->addGravity(glm::vec3(0.0f, -1.5f, 0.0f))
                 ->setAutoPlay(true);
    scene_.add(sparkEmitter_);

    decalNode_ = new DecalNode(256);
    decalNode_->name = "effects_decals";
    decalNode_->renderType = RenderType::Special;
    decalNode_->material = &decalMaterial_;
    decalNode_->setDefaultLifetime(5.0f);
    decalNode_->setDefaultFadeStart(0.65f);
    decalNode_->setDefaultSize(glm::vec2(0.9f, 0.9f));
    scene_.add(decalNode_);
    decalNode_->addDecal(glm::vec3(-1.6f, 0.02f, 1.2f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(1.2f, 1.2f), glm::vec4(1.0f, 0.95f, 0.85f, 0.95f), 8.0f);
    decalNode_->addDecal(glm::vec3(-1.8f, 2.0f, -4.48f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(1.0f, 1.0f), glm::vec4(0.55f, 0.82f, 1.0f, 0.9f), 8.0f);

    grassNode_ = new GrassNode(GrassNode::GrassType::TriCross);
    grassNode_->name = "effects_grass";
    grassNode_->renderType = RenderType::Terrain;
    grassNode_->material = &grassMaterial_;
    grassNode_->fillArea(glm::vec3(0.0f, 0.0f, 0.0f), 14.0f, 14.0f, 240, 0.55f, 1.15f, 1234u);
    grassNode_->build();
    scene_.add(grassNode_);

    lensFlareNode_ = new LensFlareNode();
    lensFlareNode_->name = "effects_lens_flare";
    lensFlareNode_->renderType = RenderType::Overlay;
    lensFlareNode_->material = &flareMaterial_;
    lensFlareNode_->setSunDirection(glm::normalize(glm::vec3(-0.45f, -1.0f, -0.25f)));
    lensFlareNode_->initDefaultFlares();
    scene_.add(lensFlareNode_);

    trailMarkerA_ = scene_.createMeshNode("effects_trail_marker_a", trailMarkerMesh);
    trailMarkerB_ = scene_.createMeshNode("effects_trail_marker_b", trailMarkerMesh);
    if (!trailMarkerA_ || !trailMarkerB_)
        return false;
    trailMarkerA_->renderType = RenderType::Solid;
    trailMarkerB_->renderType = RenderType::Solid;
    trailMarkerA_->setMaterial(0, &trailMarkerMaterialA_);
    trailMarkerB_->setMaterial(0, &trailMarkerMaterialB_);

    ribbonTrailNode_ = new RibbonTrailNode(2, 36);
    ribbonTrailNode_->name = "effects_ribbon_trail";
    ribbonTrailNode_->renderType = RenderType::Transparent;
    ribbonTrailNode_->material = &trailMaterial_;
    ribbonTrailNode_->setTrailLength(1.25f);
    ribbonTrailNode_->setMinSegmentLength(0.03f);
    scene_.add(ribbonTrailNode_);
    const int chainA = ribbonTrailNode_->addChain(trailMarkerA_, glm::vec4(1.0f, 0.42f, 0.22f, 0.95f), glm::vec4(1.0f, 0.85f, 0.35f, 0.0f), 0.48f, 0.02f);
    const int chainB = ribbonTrailNode_->addChain(trailMarkerB_, glm::vec4(0.35f, 0.85f, 1.0f, 0.95f), glm::vec4(0.85f, 0.95f, 1.0f, 0.0f), 0.34f, 0.02f);
    if (chainA < 0 || chainB < 0)
        return false;

    return true;
}

inline void EffectsDemo::buildManualMesh()
{
    if (!manualMeshNode_)
        return;

    manualMeshNode_->begin(GL_TRIANGLES, false);

    const glm::vec3 top(0.0f, 1.6f, 0.0f);
    const glm::vec3 b0(-0.45f, 0.0f, -0.45f);
    const glm::vec3 b1(0.45f, 0.0f, -0.45f);
    const glm::vec3 b2(0.45f, 0.0f, 0.45f);
    const glm::vec3 b3(-0.45f, 0.0f, 0.45f);
    const glm::vec3 bottom(0.0f, -0.3f, 0.0f);

    manualMeshNode_->position(top).texCoord(0.0f, 0.0f);
    manualMeshNode_->position(b0).texCoord(0.0f, 1.0f);
    manualMeshNode_->position(b1).texCoord(1.0f, 1.0f);
    manualMeshNode_->position(b2).texCoord(1.0f, 1.0f);
    manualMeshNode_->position(b3).texCoord(0.0f, 1.0f);
    manualMeshNode_->position(bottom).texCoord(0.5f, 0.5f);

    manualMeshNode_->triangle(0, 1, 2);
    manualMeshNode_->triangle(0, 2, 3);
    manualMeshNode_->triangle(0, 3, 4);
    manualMeshNode_->triangle(0, 4, 1);
    manualMeshNode_->triangle(5, 2, 1);
    manualMeshNode_->triangle(5, 3, 2);
    manualMeshNode_->triangle(5, 4, 3);
    manualMeshNode_->triangle(5, 1, 4);
    manualMeshNode_->computeNormals();
    manualMeshNode_->end();
}

inline void EffectsDemo::syncVisibility()
{
    if (smokeEmitter_)
        smokeEmitter_->visible = showParticles_;
    if (sparkEmitter_)
        sparkEmitter_->visible = showParticles_;
    if (decalNode_)
        decalNode_->visible = showDecals_;
    if (grassNode_)
        grassNode_->visible = showGrass_;
    if (lensFlareNode_)
        lensFlareNode_->visible = showLensFlare_;
    if (manualMeshNode_)
        manualMeshNode_->visible = showManualMesh_;
    if (ribbonTrailNode_)
        ribbonTrailNode_->visible = showRibbonTrail_;
    if (trailMarkerA_)
        trailMarkerA_->visible = showRibbonTrail_;
    if (trailMarkerB_)
        trailMarkerB_->visible = showRibbonTrail_;
}

inline void EffectsDemo::resetCamera()
{
    if (!camera_)
        return;

    camera_->setPosition(glm::vec3(8.0f, 5.5f, 9.5f));
    camera_->lookAt(glm::vec3(0.0f, 1.5f, 0.0f));
    camera_->updateMatrices();
}

inline void EffectsDemo::spawnTimedDecal()
{
    if (!decalNode_)
        return;

    const float angle = time_ * 1.7f;
    const float radius = 2.6f + std::sin(time_ * 0.7f) * 0.4f;
    const glm::vec3 groundPos(std::cos(angle) * radius, 0.02f, std::sin(angle) * radius);
    const glm::vec4 groundColor(1.0f, 0.75f + 0.2f * std::sin(time_ * 1.3f), 0.35f, 0.9f);
    decalNode_->addDecal(groundPos, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.8f, 0.8f), groundColor, 4.5f);

    if (((int)(time_ * 2.0f)) % 3 == 0)
    {
        const glm::vec3 wallPos(-2.0f + std::sin(angle) * 1.3f, 1.8f + std::cos(angle * 0.7f) * 0.8f, -4.48f);
        decalNode_->addDecal(wallPos, glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.75f, 0.75f), glm::vec4(0.55f, 0.85f, 1.0f, 0.85f), 4.0f);
    }
}
