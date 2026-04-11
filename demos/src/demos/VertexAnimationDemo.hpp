#pragma once

#include <string>

#include "Animation.hpp"
#include "Animator.hpp"
#include "Demo.hpp"
#include "imgui.h"

class VertexAnimationDemo : public IDemo
{
public:
    VertexAnimationDemo();

    virtual const char *title() const override;
    virtual const char *description() const override;

    virtual bool setup(Device &device) override;
    virtual void update(float dt) override;
    virtual void drawGui() override;
    virtual void render() override;
    virtual void shutdown() override;

private:
    static int findAnimationIndex(const AnimatedMesh *mesh, const std::string &name);
    static void addSargeAnimations(VertexAnimatedMeshNode *lower, VertexAnimatedMeshNode *torso);
    static void drawTagAxes(RenderBatch &batch, VertexAnimatedMeshNode *node, float axisLength);
    static Material *ensureMaterial(Mesh *mesh, int slot, const char *name, const glm::vec4 &color);
    static Material *ensureMaterial(VertexAnimatedMesh *mesh, int slot, const char *name, const glm::vec4 &color);
    static Shader *createStaticShader();
    static Shader *createVertexAnimShader();
    static Shader *createSkinnedShader();
    static Shader *createLightmapShader();
    static void setupLighting(Shader *shader);
    std::string assetPath(const char *relativePath) const;
    std::string assetDir(const char *relativePath) const;

    Device *device_;
    std::string assetRoot_;
    Scene scene_;
    Camera *camera_;

    Shader *staticShader_;
    Shader *vertexAnimShader_;
    Shader *skinnedShader_;
    Shader *lightmapShader_;

    Mesh *groundMesh_;
    Mesh *cubeMesh_;
    Mesh *sphereMesh_;
    Mesh *glassMesh_;
    Mesh *gltfMesh_;
    Mesh *bspMesh_;

    VertexAnimatedMesh *md2Mesh_;
    VertexAnimatedMesh *md3Mesh_;
    VertexAnimatedMesh *md3MeshTorso_;
    VertexAnimatedMesh *md3MeshHead_;

    AnimatedMesh *b3dAnimatedMesh_;
    AnimatedMesh *iqmAnimatedMesh_;
    AnimatedMesh *gltfAnimatedMesh_;

    MeshNode *sphereNode_;
    MeshNode *cubeBNode_;
    MeshNode *gltfNode_;
    MeshNode *bspNode_;

    VertexAnimatedMeshNode *actor_;
    VertexAnimatedMeshNode *md3Actor_;
    VertexAnimatedMeshNode *md3ActorTorso_;
    VertexAnimatedMeshNode *md3ActorHead_;

    AnimatedMeshNode *b3dAnimatedNode_;
    AnimatedMeshNode *iqmAnimatedNode_;
    AnimatedMeshNode *gltfAnimatedNode_;

    Material overrideBlue_;
    RenderBatch tagBatch_;

    float frameSlider_;
    float modelScale_;
    bool play_;
    float fps_;
    float md3FrameSlider_;
    float md3TorsoFrameSlider_;
    bool md3Play_;
    float md3Fps_;
    bool showMd3Tags_;
    bool showSceneBounds_;
    float tagAxisSize_;
    float yaw_;
    float pitch_;
    float roll_;
};

inline VertexAnimationDemo::VertexAnimationDemo()
    : device_(nullptr),
      camera_(nullptr),
      staticShader_(nullptr),
      vertexAnimShader_(nullptr),
      skinnedShader_(nullptr),
      lightmapShader_(nullptr),
      groundMesh_(nullptr),
      cubeMesh_(nullptr),
      sphereMesh_(nullptr),
      glassMesh_(nullptr),
      gltfMesh_(nullptr),
      bspMesh_(nullptr),
      md2Mesh_(nullptr),
      md3Mesh_(nullptr),
      md3MeshTorso_(nullptr),
      md3MeshHead_(nullptr),
      b3dAnimatedMesh_(nullptr),
      iqmAnimatedMesh_(nullptr),
      gltfAnimatedMesh_(nullptr),
      sphereNode_(nullptr),
      cubeBNode_(nullptr),
      gltfNode_(nullptr),
      bspNode_(nullptr),
      actor_(nullptr),
      md3Actor_(nullptr),
      md3ActorTorso_(nullptr),
      md3ActorHead_(nullptr),
      b3dAnimatedNode_(nullptr),
      iqmAnimatedNode_(nullptr),
      gltfAnimatedNode_(nullptr),
      frameSlider_(0.0f),
      modelScale_(0.10f),
      play_(true),
      fps_(6.0f),
      md3FrameSlider_(0.0f),
      md3TorsoFrameSlider_(0.0f),
      md3Play_(true),
      md3Fps_(8.0f),
      showMd3Tags_(true),
      showSceneBounds_(false),
      tagAxisSize_(1.0f),
      yaw_(0.0f),
      pitch_(-90.0f),
      roll_(0.0f)
{
}

inline const char *VertexAnimationDemo::title() const
{
    return "Vertex Animation";
}

inline const char *VertexAnimationDemo::description() const
{
    return "Cena de teste para MD2, MD3, GLTF, IQM, B3D e render types.";
}

inline bool VertexAnimationDemo::setup(Device &device)
{
    device_ = &device;
    assetRoot_ = findProjectAssetRoot();

    staticShader_ = createStaticShader();
    vertexAnimShader_ = createVertexAnimShader();
    skinnedShader_ = createSkinnedShader();
    lightmapShader_ = createLightmapShader();
    if (!staticShader_ || !vertexAnimShader_ || !skinnedShader_ || !lightmapShader_)
    {
        shutdown();
        return false;
    }

    groundMesh_ = MeshManager::instance().create_plane("ground_demo", 14.0f, 14.0f, 8);
    cubeMesh_ = MeshManager::instance().create_cube("cube_demo", 1.5f);
    sphereMesh_ = MeshManager::instance().create_sphere("sphere_demo", 1.0f, 24);
    glassMesh_ = MeshManager::instance().create_quad("glass_demo", 2.0f, 2.0f);

    ensureMaterial(groundMesh_, 0, "ground", glm::vec4(0.58f, 0.60f, 0.63f, 1.0f));
    ensureMaterial(cubeMesh_, 0, "cube_red", glm::vec4(0.85f, 0.28f, 0.22f, 1.0f));
    ensureMaterial(sphereMesh_, 0, "sphere_green", glm::vec4(0.24f, 0.78f, 0.42f, 1.0f));
    Material *glassMat = ensureMaterial(glassMesh_, 0, "glass", glm::vec4(0.22f, 0.62f, 0.95f, 0.35f));
    if (glassMat)
    {
        glassMat->setBlend(true);
        glassMat->setCullFace(false);
    }

    overrideBlue_.name = "override_blue";
    overrideBlue_.type = MaterialType::Custom;
    overrideBlue_.setVec4("u_color", glm::vec4(0.20f, 0.46f, 0.95f, 1.0f));
    overrideBlue_.setTexture("u_albedo", TextureManager::instance().getWhite());

    md2Mesh_ = VertexAnimatedMeshManager::instance().load(
        "pknight_demo",
        assetPath("md2/pknight.md2").c_str(),
        assetDir("md2").c_str());
    if (md2Mesh_)
    {
        Material *md2Mat = ensureMaterial(md2Mesh_, 0, "pknight_tint", glm::vec4(1.0f));
        if (md2Mat)
        {
            Texture *md2Tex = TextureManager::instance().load("pknight_manual_tex", assetPath("md2/pknight.jpg").c_str());
            if (md2Tex)
                md2Mat->setTexture("u_albedo", md2Tex);
        }
    }

    md3Mesh_ = VertexAnimatedMeshManager::instance().load(
        "sarge_lower",
        assetPath("md3/sarge/lower.md3").c_str(),
        assetPath("md3/sarge/lower_default.skin").c_str());

    md3MeshTorso_ = VertexAnimatedMeshManager::instance().load(
        "sarge_upper",
        assetPath("md3/sarge/upper.md3").c_str(),
        assetPath("md3/sarge/upper_default.skin").c_str());

    md3MeshHead_ = VertexAnimatedMeshManager::instance().load(
        "sarge_head",
        assetPath("md3/sarge/head.md3").c_str(),
        assetPath("md3/sarge/head_default.skin").c_str());

    b3dAnimatedMesh_ = AnimatedMeshManager::instance().load(
        "ninja_b3d_anim",
        assetPath("b3d/ninja.b3d").c_str(),
        assetDir("b3d").c_str());

    iqmAnimatedMesh_ = AnimatedMeshManager::instance().load(
        "erebus_iqm_anim",
        assetPath("iqm/erebus/erebus.iqm").c_str(),
        assetDir("iqm/erebus").c_str());

    if (iqmAnimatedMesh_ && iqmAnimatedMesh_->materials.size() >= 2)
    {
        Texture *erebusTex = TextureManager::instance().load(
            "erebus_iqm_manual_base",
            assetPath("iqm/erebus/erebus.png").c_str());
        Texture *shadowTex = TextureManager::instance().load(
            "erebus_iqm_manual_shadow",
            assetPath("iqm/erebus/shadowhead.png").c_str());

        if (iqmAnimatedMesh_->materials[0])
            iqmAnimatedMesh_->materials[0]->setTexture("u_albedo", shadowTex);
        if (iqmAnimatedMesh_->materials[1])
            iqmAnimatedMesh_->materials[1]->setTexture("u_albedo", erebusTex);
    }

    gltfMesh_ = MeshManager::instance().load(
        "idle_glb_static",
        assetPath("gltf/idle.glb").c_str(),
        assetDir("gltf").c_str());

    gltfAnimatedMesh_ = AnimatedMeshManager::instance().load(
        "idle_glb_anim",
        assetPath("gltf/idle.glb").c_str(),
        assetDir("gltf").c_str());

    bspMesh_ = MeshManager::instance().load(
        "oa_rpg3dm2_bsp",
        assetPath("maps/oa_rpg3dm/maps/oa_rpg3dm2.bsp").c_str(),
        assetDir("maps/oa_rpg3dm").c_str());

    camera_ = scene_.createFreeCamera("main",
                                      device.GetWidth(), device.GetHeight(),
                                      glm::vec3(0.0f, 3.0f, 9.0f),
                                      glm::vec3(0.0f, 1.0f, 0.0f),
                                      8.0f, 0.18f, 3.0f);
    if (!camera_)
    {
        shutdown();
        return false;
    }
    camera_->clearColorVal = glm::vec4(0.73f, 0.82f, 0.93f, 1.0f);
    scene_.setCamera(camera_);

    MeshNode *ground = scene_.createMeshNode("ground", groundMesh_);
    if (ground)
    {
        ground->renderType = RenderType::Solid;
        ground->setPosition(0.0f, -1.0f, 0.0f);
    }

    MeshNode *cubeA = scene_.createMeshNode("cubeA", cubeMesh_);
    if (cubeA)
    {
        cubeA->renderType = RenderType::Solid;
        cubeA->setPosition(-2.2f, 0.0f, 0.0f);
    }

    cubeBNode_ = scene_.createMeshNode("cubeB", cubeMesh_);
    if (cubeBNode_)
    {
        cubeBNode_->renderType = RenderType::Solid;
        cubeBNode_->setMaterial(0, &overrideBlue_);
    }

    sphereNode_ = scene_.createMeshNode("sphere", sphereMesh_);
    if (sphereNode_)
    {
        sphereNode_->renderType = RenderType::Solid;
        sphereNode_->setPosition(0.0f, 0.0f, -3.5f);
    }

    MeshNode *glass = scene_.createMeshNode("glass", glassMesh_);
    if (glass)
    {
        glass->renderType = RenderType::Transparent;
        glass->setPosition(0.0f, 0.2f, 1.8f);
        glass->yaw(20.0f);
    }

    if (gltfMesh_)
    {
        gltfNode_ = scene_.createMeshNode("idle_glb_static", gltfMesh_);
        if (gltfNode_)
        {
            gltfNode_->renderType = RenderType::Solid;
            gltfNode_->setPosition(5.5f, 0.0f, 1.5f);
            gltfNode_->setScale(glm::vec3(1.0f));
            gltfNode_->yaw(180.0f);
        }
    }

    if (gltfAnimatedMesh_)
    {
        gltfAnimatedNode_ = scene_.createAnimatedMeshNode("idle_glb_anim", gltfAnimatedMesh_);
        if (gltfAnimatedNode_)
        {
            gltfAnimatedNode_->renderType = RenderType::Skinning;
            gltfAnimatedNode_->setPosition(8.0f, 0.0f, 1.5f);
            gltfAnimatedNode_->setScale(glm::vec3(0.01f));
        }
    }

    if (md2Mesh_)
    {
        actor_ = scene_.createVertexAnimatedMeshNode("pknight", md2Mesh_);
        if (actor_)
        {
            actor_->renderType = RenderType::Special;
            actor_->setPosition(0.0f, 2.0f, 3.8f);
            actor_->setScale(glm::vec3(0.10f));
            actor_->yaw(180.0f);
            actor_->setFrame(0.0f);
            actor_->visible = true;
        }
    }

    if (md3Mesh_)
    {
        md3Actor_ = scene_.createVertexAnimatedMeshNode("sarge_lower", md3Mesh_);
        if (md3Actor_)
        {
            md3Actor_->renderType = RenderType::Special;
            md3Actor_->setPosition(-2.5f, 2.0f, 3.8f);
            md3Actor_->setScale(glm::vec3(0.12f));
            md3Actor_->yaw(yaw_);
            md3Actor_->pitch(pitch_);
            md3Actor_->roll(roll_);
            md3Actor_->setFrame(100.0f);

            if (cubeBNode_)
                cubeBNode_->setParent(md3Actor_->getTag(0));
        }
    }

    if (md3MeshTorso_)
    {
        md3ActorTorso_ = scene_.createVertexAnimatedMeshNode("sarge_torso", md3MeshTorso_);
        if (md3ActorTorso_)
        {
            md3ActorTorso_->renderType = RenderType::Special;
            md3ActorTorso_->setFrame(0.0f);
            if (md3Actor_)
                md3ActorTorso_->setParent(md3Actor_->getTag(0));
        }
    }

    addSargeAnimations(md3Actor_, md3ActorTorso_);

    if (b3dAnimatedMesh_)
    {
        b3dAnimatedNode_ = scene_.createAnimatedMeshNode("ninja_b3d_anim", b3dAnimatedMesh_);
        if (b3dAnimatedNode_)
        {
            b3dAnimatedNode_->renderType = RenderType::Skinning;
            b3dAnimatedNode_->setPosition(2.8f, 0.0f, -1.0f);
            b3dAnimatedNode_->setScale(glm::vec3(0.05f));
            b3dAnimatedNode_->yaw(180.0f);
        }
    }

    if (iqmAnimatedMesh_)
    {
        iqmAnimatedNode_ = scene_.createAnimatedMeshNode("erebus_iqm_anim", iqmAnimatedMesh_);
        if (iqmAnimatedNode_)
        {
            iqmAnimatedNode_->renderType = RenderType::Skinning;
            iqmAnimatedNode_->setPosition(-5.0f, 0.0f, -1.0f);
            iqmAnimatedNode_->setScale(glm::vec3(0.04f));
            iqmAnimatedNode_->yaw(180.0f);
        }
    }

    if (md3MeshHead_)
    {
        md3ActorHead_ = scene_.createVertexAnimatedMeshNode("sarge_head", md3MeshHead_);
        if (md3ActorHead_)
        {
            md3ActorHead_->renderType = RenderType::Special;
            md3ActorHead_->setFrame(0.0f);
            if (md3ActorTorso_)
                md3ActorHead_->setParent(md3ActorTorso_->getTag(0));
        }
    }

    tagBatch_.Init();
    return true;
}

inline void VertexAnimationDemo::update(float dt)
{
    if (!device_ || !camera_)
        return;

    if (device_->IsResize())
        camera_->setViewport(0, 0, device_->GetWidth(), device_->GetHeight());

    if (sphereNode_)
        sphereNode_->yaw(-15.0f * dt);

    if (actor_)
        actor_->setScale(glm::vec3(modelScale_));

    scene_.update(dt);

    if (actor_)
        frameSlider_ = actor_->currentFrame();
    if (md3Actor_)
        md3FrameSlider_ = md3Actor_->currentFrame();
    if (md3ActorTorso_)
        md3TorsoFrameSlider_ = md3ActorTorso_->currentFrame();
}

inline void VertexAnimationDemo::drawGui()
{
    ImGui::SetNextWindowPos(ImVec2(16, 72), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_Once);
    if (!ImGui::Begin("Vertex Animation"))
    {
        ImGui::End();
        return;
    }

    if (actor_ && md2Mesh_)
    {
        ImGui::SeparatorText("MD2");
        ImGui::Text("Mesh: %s", md2Mesh_->name.c_str());
        ImGui::Text("Frames: %d", md2Mesh_->frameCount());
        ImGui::Checkbox("Play", &play_);
        ImGui::SliderFloat("FPS", &fps_, 1.0f, 20.0f);
        ImGui::SliderFloat("Scale", &modelScale_, 0.02f, 0.25f);

        const float maxFrame = (float)glm::max(0, md2Mesh_->frameCount() - 1);
        if (ImGui::SliderFloat("Frame", &frameSlider_, 0.0f, maxFrame))
        {
            play_ = false;
            actor_->setFrame(frameSlider_);
        }

        const int frameIndex = glm::clamp((int)frameSlider_, 0, glm::max(0, md2Mesh_->frameCount() - 1));
        if (frameIndex < (int)md2Mesh_->frameNames.size())
            ImGui::Text("Frame name: %s", md2Mesh_->frameNames[frameIndex].c_str());
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load assets/md2/pknight.md2");
    }

    if (md3Actor_ && md3Mesh_)
    {
        ImGui::SeparatorText("MD3");
        ImGui::Text("Mesh: %s", md3Mesh_->name.c_str());
        ImGui::Text("Frames: %d", md3Mesh_->frameCount());
        ImGui::Text("Tags/frame: %d", md3Mesh_->tagsPerFrame);
        ImGui::Checkbox("Play MD3", &md3Play_);
        ImGui::Checkbox("Show Tags", &showMd3Tags_);
        ImGui::Checkbox("Show Bounds", &showSceneBounds_);
        ImGui::SliderFloat("MD3 FPS", &md3Fps_, 1.0f, 20.0f);
        ImGui::SliderFloat("Tag Axis", &tagAxisSize_, 1.0f, 20.0f);

        const float maxFrame = (float)glm::max(0, md3Mesh_->frameCount() - 1);
        if (ImGui::SliderFloat("MD3 Frame", &md3FrameSlider_, 0.0f, maxFrame))
        {
            md3Play_ = false;
            md3Actor_->setFrame(md3FrameSlider_);
        }

        const int frameIndex = glm::clamp((int)md3FrameSlider_, 0, glm::max(0, md3Mesh_->frameCount() - 1));
        if (frameIndex < (int)md3Mesh_->frameNames.size())
            ImGui::Text("Frame name: %s", md3Mesh_->frameNames[frameIndex].c_str());

        for (int i = 0; i < md3Mesh_->tagsPerFrame; ++i)
            ImGui::Text("Tag %d: %s", i, md3Mesh_->tags[i].tag);
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load assets/md3/sarge/lower.md3");
    }

    if (md3ActorTorso_ && md3MeshTorso_)
    {
        ImGui::SeparatorText("MD3 Torso");
        ImGui::Text("Mesh: %s", md3MeshTorso_->name.c_str());
        ImGui::Text("Frames: %d", md3MeshTorso_->frameCount());
        ImGui::Text("Tags/frame: %d", md3MeshTorso_->tagsPerFrame);

        const float maxFrame = (float)glm::max(0, md3MeshTorso_->frameCount() - 1);
        if (ImGui::SliderFloat("MD3 Torso Frame", &md3TorsoFrameSlider_, 0.0f, maxFrame))
            md3ActorTorso_->setFrame(md3TorsoFrameSlider_);

        const int frameIndex = glm::clamp((int)md3TorsoFrameSlider_, 0, glm::max(0, md3MeshTorso_->frameCount() - 1));
        if (frameIndex < (int)md3MeshTorso_->frameNames.size())
            ImGui::Text("Frame name: %s", md3MeshTorso_->frameNames[frameIndex].c_str());
    }

    if (b3dAnimatedNode_ && b3dAnimatedMesh_)
    {
        ImGui::SeparatorText("B3D Animated");
        ImGui::Text("Mesh: %s", b3dAnimatedMesh_->name.c_str());
        ImGui::Text("Bones: %d", (int)b3dAnimatedMesh_->bones.size());
        ImGui::Text("Animations: %d", (int)b3dAnimatedMesh_->animations.size());
    }
    else
    {
        ImGui::SeparatorText("B3D Animated");
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load assets/b3d/ninja.b3d");
    }

    if (gltfNode_ && gltfMesh_)
    {
        ImGui::SeparatorText("GLTF / GLB");
        ImGui::Text("Mesh: %s", gltfMesh_->name.c_str());
        ImGui::Text("Vertices: %d", gltfMesh_->vertexCount());
        ImGui::Text("Indices: %d", gltfMesh_->indexCount());
        ImGui::Text("Surfaces: %d", (int)gltfMesh_->surfaces.size());
        ImGui::Text("Materials: %d", (int)gltfMesh_->materials.size());
    }
    else
    {
        ImGui::SeparatorText("GLTF / GLB");
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load assets/gltf/idle.glb");
    }

    if (gltfAnimatedNode_ && gltfAnimatedMesh_)
    {
        ImGui::SeparatorText("GLTF Animated");
        ImGui::Text("Mesh: %s", gltfAnimatedMesh_->name.c_str());
        ImGui::Text("Bones: %d", (int)gltfAnimatedMesh_->bones.size());
        ImGui::Text("Animations: %d", (int)gltfAnimatedMesh_->animations.size());
        ImGui::Text("Surfaces: %d", (int)gltfAnimatedMesh_->surfaces.size());
        if (gltfAnimatedNode_->animator && gltfAnimatedNode_->animator->layerCount() > 0)
        {
            AnimationLayer *layer = gltfAnimatedNode_->animator->getLayer(0);
            const std::string currentAnim = layer ? layer->currentName() : std::string();
            ImGui::Text("Current: %s", currentAnim.empty() ? "<none>" : currentAnim.c_str());
        }
    }
    else
    {
        ImGui::SeparatorText("GLTF Animated");
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load animated assets/gltf/idle.glb");
    }

    if (iqmAnimatedNode_ && iqmAnimatedMesh_)
    {
        ImGui::SeparatorText("IQM Animated");
        ImGui::Text("Mesh: %s", iqmAnimatedMesh_->name.c_str());
        ImGui::Text("Bones: %d", (int)iqmAnimatedMesh_->bones.size());
        ImGui::Text("Animations: %d", (int)iqmAnimatedMesh_->animations.size());
        ImGui::Text("Surfaces: %d", (int)iqmAnimatedMesh_->surfaces.size());
        if (iqmAnimatedNode_->animator && iqmAnimatedNode_->animator->layerCount() > 0)
        {
            AnimationLayer *layer = iqmAnimatedNode_->animator->getLayer(0);
            const std::string currentAnim = layer ? layer->currentName() : std::string();
            ImGui::Text("Current: %s", currentAnim.empty() ? "<none>" : currentAnim.c_str());
            if (ImGui::Button("Next IQM Animation") && layer && !iqmAnimatedMesh_->animations.empty())
            {
                int nextIndex = findAnimationIndex(iqmAnimatedMesh_, currentAnim);
                nextIndex = (nextIndex + 1) % (int)iqmAnimatedMesh_->animations.size();
                Animation *nextAnim = iqmAnimatedMesh_->animations[nextIndex];
                if (nextAnim)
                    layer->play(nextAnim->name);
            }
        }

        ImGui::SliderFloat("Yaw", &yaw_, -180.0f, 180.0f);
        ImGui::SliderFloat("Pitch", &pitch_, -180.0f, 180.0f);
        ImGui::SliderFloat("Roll", &roll_, -180.0f, 180.0f);

        iqmAnimatedNode_->setRotationEuler(glm::vec3(pitch_, yaw_, roll_));
    }
    else
    {
        ImGui::SeparatorText("IQM Animated");
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load assets/iqm/erebus/erebus.iqm");
    }

    if (bspNode_ && bspMesh_)
    {
        ImGui::SeparatorText("BSP");
        ImGui::Text("Mesh: %s", bspMesh_->name.c_str());
        ImGui::Text("Vertices: %d", bspMesh_->vertexCount());
        ImGui::Text("Indices: %d", bspMesh_->indexCount());
        ImGui::Text("Surfaces: %d", (int)bspMesh_->surfaces.size());
        ImGui::Text("Materials: %d", (int)bspMesh_->materials.size());
    }
    else
    {
        ImGui::SeparatorText("BSP");
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Failed to load assets/maps/oa_rpg3dm/maps/oa_rpg3dm2.bsp");
    }

    ImGui::End();
}

inline void VertexAnimationDemo::render()
{
    if (!camera_)
        return;

    scene_.setCamera(camera_);
    scene_.beginPass();

    scene_.setShader(staticShader_);
    setupLighting(staticShader_);
    scene_.render(RenderType::Solid);

    scene_.setShader(vertexAnimShader_);
    setupLighting(vertexAnimShader_);
    scene_.render(RenderType::Special);

    scene_.setShader(skinnedShader_);
    setupLighting(skinnedShader_);
    scene_.render(RenderType::Skinning);

    scene_.setShader(lightmapShader_);
    scene_.render(RenderType::Lightmap);

    scene_.setShader(staticShader_);
    setupLighting(staticShader_);
    scene_.render(RenderType::Transparent);

    scene_.endPass();

    if (md3Actor_ && md3Mesh_ && showMd3Tags_)
    {
        tagBatch_.SetMatrix(camera_->viewProjection);
        Material::applyDefaultStates();
        drawTagAxes(tagBatch_, md3Actor_, tagAxisSize_);
        drawTagAxes(tagBatch_, md3ActorTorso_, tagAxisSize_);
        tagBatch_.Render();
    }

    if (showSceneBounds_)
    {
        tagBatch_.SetMatrix(camera_->viewProjection);
        Material::applyDefaultStates();
        scene_.debug(&tagBatch_);
        tagBatch_.Render();
    }
}

inline void VertexAnimationDemo::shutdown()
{
    scene_.clear();
    unloadDemoAssets();

    camera_ = nullptr;
    staticShader_ = nullptr;
    vertexAnimShader_ = nullptr;
    skinnedShader_ = nullptr;
    lightmapShader_ = nullptr;
    groundMesh_ = nullptr;
    cubeMesh_ = nullptr;
    sphereMesh_ = nullptr;
    glassMesh_ = nullptr;
    gltfMesh_ = nullptr;
    bspMesh_ = nullptr;
    md2Mesh_ = nullptr;
    md3Mesh_ = nullptr;
    md3MeshTorso_ = nullptr;
    md3MeshHead_ = nullptr;
    b3dAnimatedMesh_ = nullptr;
    iqmAnimatedMesh_ = nullptr;
    gltfAnimatedMesh_ = nullptr;
    sphereNode_ = nullptr;
    cubeBNode_ = nullptr;
    gltfNode_ = nullptr;
    bspNode_ = nullptr;
    actor_ = nullptr;
    md3Actor_ = nullptr;
    md3ActorTorso_ = nullptr;
    md3ActorHead_ = nullptr;
    b3dAnimatedNode_ = nullptr;
    iqmAnimatedNode_ = nullptr;
    gltfAnimatedNode_ = nullptr;
    assetRoot_.clear();
}

inline std::string VertexAnimationDemo::assetPath(const char *relativePath) const
{
    return demoJoinPath(assetRoot_, relativePath);
}

inline std::string VertexAnimationDemo::assetDir(const char *relativePath) const
{
    return demoJoinPath(assetRoot_, relativePath);
}

inline int VertexAnimationDemo::findAnimationIndex(const AnimatedMesh *mesh, const std::string &name)
{
    if (!mesh)
        return -1;

    for (int i = 0; i < (int)mesh->animations.size(); ++i)
    {
        if (mesh->animations[i] && mesh->animations[i]->name == name)
            return i;
    }

    return -1;
}

inline void VertexAnimationDemo::addSargeAnimations(VertexAnimatedMeshNode *lower, VertexAnimatedMeshNode *torso)
{
    if (lower)
    {
        lower->frameAnimator.addAnimation("idle", 165, 1, 15.0f, false);
        lower->frameAnimator.addAnimation("jump", 131, 140, 15.0f, false);
        lower->frameAnimator.addAnimation("walk", 98, 108, 6, true);
        lower->frameAnimator.addAnimation("run", 98, 108, 18, true);
        lower->frameAnimator.play("run");
    }

    if (torso)
    {
        torso->frameAnimator.addAnimation("stand", 152, 152, 15.0f, false);
        torso->frameAnimator.addAnimation("stand2", 151, 151, 15.0f, false);
        torso->frameAnimator.addAnimation("wepon", 90, 90, 18.0f, false);
        torso->frameAnimator.addAnimation("gesture", 91, 127, 18.0f, true);
        torso->frameAnimator.addAnimation("attack", 130, 135, 15.0f, false);
        torso->frameAnimator.addAnimation("attack2", 136, 141, 15.0f, false);
        torso->frameAnimator.addAnimation("drop", 142, 146, 20.0f, false);
        torso->frameAnimator.addAnimation("raise", 147, 150, 20.0f, false);
        torso->frameAnimator.play("stand2");
    }
}

inline void VertexAnimationDemo::drawTagAxes(RenderBatch &batch, VertexAnimatedMeshNode *node, float axisLength)
{
    if (!node || !node->mesh || node->mesh->tagsPerFrame <= 0)
        return;

    for (int i = 0; i < node->mesh->tagsPerFrame; ++i)
    {
        Node3D *tag = node->getTag(i);
        if (!tag)
            continue;

        const glm::mat4 tagWorld = tag->worldMatrix();
        const glm::vec3 origin = glm::vec3(tagWorld[3]);
        const glm::vec3 axisX = glm::normalize(glm::vec3(tagWorld[0])) * axisLength;
        const glm::vec3 axisY = glm::normalize(glm::vec3(tagWorld[1])) * axisLength;
        const glm::vec3 axisZ = glm::normalize(glm::vec3(tagWorld[2])) * axisLength;

        batch.SetColor(Color::RED);
        batch.Line3D(origin, origin + axisX);
        batch.SetColor(Color::GREEN);
        batch.Line3D(origin, origin + axisY);
        batch.SetColor(Color::BLUE);
        batch.Line3D(origin, origin + axisZ);
    }
}

inline Material *VertexAnimationDemo::ensureMaterial(Mesh *mesh, int slot, const char *name, const glm::vec4 &color)
{
    if (!mesh)
        return nullptr;

    if (slot >= (int)mesh->materials.size())
        mesh->materials.resize(slot + 1, nullptr);
    if (!mesh->materials[slot])
        mesh->materials[slot] = new Material();

    Material *mat = mesh->materials[slot];
    mat->name = name;
    mat->type = MaterialType::Custom;
    mat->setVec4("u_color", color);
    mat->setTexture("u_albedo", TextureManager::instance().getWhite());
    return mat;
}

inline Material *VertexAnimationDemo::ensureMaterial(VertexAnimatedMesh *mesh, int slot, const char *name, const glm::vec4 &color)
{
    if (!mesh)
        return nullptr;

    if (slot >= (int)mesh->materials.size())
        mesh->materials.resize(slot + 1, nullptr);
    if (!mesh->materials[slot])
        mesh->materials[slot] = new Material();

    Material *mat = mesh->materials[slot];
    mat->name = name;
    mat->type = MaterialType::Custom;
    mat->setVec4("u_color", color);
    mat->setTexture("u_albedo", TextureManager::instance().getWhite());
    return mat;
}

inline Shader *VertexAnimationDemo::createStaticShader()
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
            vec3 lit = albedo.rgb * (u_ambient + vec3(0.85 * diff));
            FragColor = vec4(lit, albedo.a);
        });

    return ShaderManager::instance().loadFromSource("demo_static_shader", vert, frag);
}

inline Shader *VertexAnimationDemo::createVertexAnimShader()
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
            FragColor = albedo;
        });

    return ShaderManager::instance().loadFromSource("demo_vertex_anim_shader", vert, frag);
}

inline Shader *VertexAnimationDemo::createSkinnedShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 3) in vec2 uv;
        layout(location = 4) in ivec4 boneIds;
        layout(location = 5) in vec4 boneWeights;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;
        uniform mat3 u_normalMatrix;
        uniform mat4 u_boneMatrices[100];

        out vec3 v_normal;
        out vec2 v_uv;

        void main()
        {
            mat4 skin =
                boneWeights.x * u_boneMatrices[boneIds.x] +
                boneWeights.y * u_boneMatrices[boneIds.y] +
                boneWeights.z * u_boneMatrices[boneIds.z] +
                boneWeights.w * u_boneMatrices[boneIds.w];

            vec3 skinnedPos = vec3(skin * vec4(position, 1.0));
            vec3 skinnedNormal = mat3(skin) * normal;

            v_normal = normalize(u_normalMatrix * skinnedNormal);
            v_uv = uv;
            gl_Position = u_projection * u_view * u_model * vec4(skinnedPos, 1.0);
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
            vec4 albedo = texture(u_albedo, v_uv) * u_color;
            vec3 N = normalize(v_normal);
            vec3 L = normalize(-u_lightDir);
            float diff = max(dot(N, L), 0.0);
            vec3 lit = albedo.rgb * (u_ambient + vec3(0.85 * diff));
            FragColor = vec4(lit, albedo.a);
        });

    return ShaderManager::instance().loadFromSource("demo_skinned_shader", vert, frag);
}

inline Shader *VertexAnimationDemo::createLightmapShader()
{
    const char *vert = GLSL(
        layout(location = 0) in vec3 position;
        layout(location = 1) in vec3 normal;
        layout(location = 2) in vec4 tangent;
        layout(location = 3) in vec2 uv;

        uniform mat4 u_model;
        uniform mat4 u_view;
        uniform mat4 u_projection;

        out vec2 v_uv;
        out vec2 v_lmUv;

        void main()
        {
            v_uv = uv;
            v_lmUv = tangent.xy;
            gl_Position = u_projection * u_view * u_model * vec4(position, 1.0);
        });

    const char *frag = GLSL(
        in vec2 v_uv;
        in vec2 v_lmUv;
        out vec4 FragColor;

        uniform vec4 u_color;
        uniform sampler2D u_albedo;
        uniform sampler2D u_lightmap;
        uniform int u_useLightmap;

        void main()
        {
            vec4 albedo = texture(u_albedo, v_uv) * u_color;
            vec3 lm = (u_useLightmap != 0) ? texture(u_lightmap, v_lmUv).rgb : vec3(1.0);
            FragColor = vec4(albedo.rgb * lm, albedo.a);
        });

    return ShaderManager::instance().loadFromSource("demo_lightmap_shader", vert, frag);
}

inline void VertexAnimationDemo::setupLighting(Shader *shader)
{
    if (!shader)
        return;

    shader->setVec3("u_lightDir", glm::normalize(glm::vec3(-0.45f, -1.0f, -0.25f)));
    shader->setVec3("u_ambient", glm::vec3(0.20f, 0.22f, 0.24f));
}
