#pragma once
#include "Node.hpp"
#include "Mesh.hpp"
#include "Material.hpp"
#include "RenderPipeline.hpp"
#include "RenderTarget.hpp"

// ============================================================
//  WaterNode3D — real-time reflection + refraction water plane.
//
//  Usage:
//    auto *water = scene.createWaterNode("water");
//    water->init(200.f, 200.f, waterShader);
//    water->setPosition({0, 0, 0});  // Y = water surface height
//
//  preRender (called automatically by Scene each frame):
//    1) renders scene with reflected camera     → reflectionRT
//    2) renders scene with clip below surface   → refractionRT
//  Those textures are then bound to the water material as
//  u_reflection and u_refraction.
//
//  The water shader reads u_time (auto-set each update(dt))
//  for animated ripples.
// ============================================================
class WaterNode3D : public Node3D
{
public:
    // RT resolution: 512×512 by default (change before init())
    int rtWidth  = 512;
    int rtHeight = 512;

    // Blend between refraction and reflection at normal incidence
    float reflectivity    = 0.6f;
    // Bump-map perturbation strength (screen-space UV offset)
    float distortStrength = 0.05f;

    // Texture assets (set before init())
    std::string waterBumpPath = "assets/waterbump.png";
    std::string foamPath      = "assets/foam_shore.png";

    // Gerstner wave params
    float waveHeight    = 0.15f;           // Gerstner vertex amplitude (keep small)
    float waveLength    = 0.1f;            // bump UV scroll scale
    float windForce     = 1.0f;            // bump UV scroll speed
    glm::vec2 windDirection = {1.f, 0.f};  // scroll direction

    glm::vec4 wave1 = { 1.0f,  0.0f, 0.5f, 10.0f};
    glm::vec4 wave2 = { 0.7f,  0.7f, 0.3f,  6.0f};
    glm::vec4 wave3 = { 0.0f,  1.0f, 0.2f,  3.0f};
    glm::vec4 wave4 = {-0.5f,  0.5f, 0.15f, 2.0f};

    // Foam params
    float foamScale     = 0.9f;
    float foamSpeed     = 0.2f;
    float foamIntensity = 0.6f;
    float foamRange     = 0.8f;  // shore foam distance (world units)
    float depthMult     = 5.0f;  // depth normalisation divisor (was 'mult')

    // Clip planes for reflection/refraction renders are pushed outward by this
    // many world units to avoid a blue gap at the shoreline.  0.5 is usually fine.
    float clipBias = 0.5f;

    // Water color tint
    glm::vec4 waterColor        = {0.05f, 0.15f, 0.4f, 1.f};
    float     colorBlendFactor  = 0.2f;

    // Create water plane geometry and FBOs.
    // width/depth: world-space extents of the quad.
    // waterShader: the water.vert / water.frag shader.
    bool init(float width, float depth, Shader *waterShader);
    ~WaterNode3D() override { release(); }
    void release();

    // Water material — set extra uniforms here (e.g. tint colour)
    Material *getMaterial() const { return material_; }

    // Called automatically by Scene::preRenderNodes each frame.
    void preRender(class Scene *scene, const Camera *mainCam) override;

    // Submit water quad to render queue.
    void gatherRenderItems(RenderQueue &q, const FrameContext &ctx) override;

    // Advance ripple animation — called by Scene::update().
    void update(float dt) override;

    // Debug: expose render-target textures for overlay visualisation
    Texture *debugReflTex()      const { return reflRT_  ? reflRT_->colorTex()  : nullptr; }
    Texture *debugRefrTex()      const { return refrRT_  ? refrRT_->colorTex()  : nullptr; }
    Texture *debugRefrDepthTex() const { return refrRT_  ? refrRT_->depthTex()  : nullptr; }

private:
    void buildQuad(float w, float d);

    MeshBuffer buffer_;
    Material  *material_      = nullptr;
    float      time_          = 0.f;
    bool       rendering_     = false;   // recursion guard

    // Render targets (owned by WaterNode3D)
    RenderTarget *reflRT_  = nullptr;
    RenderTarget *refrRT_  = nullptr;
};
