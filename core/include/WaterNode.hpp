#pragma once

#include "Node.hpp"
#include "RenderTarget.hpp"

class Camera;

class WaterNode3D : public RenderableNode
{
public:
    int rtWidth = 512;
    int rtHeight = 512;

    float reflectivity = 0.6f;
    float distortStrength = 0.025f;
    float clipBias = 1.5f;
    float uvTile = 24.0f;
    float shoreFadeRange = 3.0f;
    float depthDiscardCutoff = 0.02f;
    bool foamEnabled = true;

    std::string waterBumpPath = "assets/waterbump.png";
    std::string foamPath = "assets/foam.png";

    float waveHeight = 0.15f;
    float waveLength = 0.4f;
    float windForce = 1.0f;
    glm::vec2 windDirection = {1.f, 0.f};

    glm::vec4 wave1 = {1.0f, 0.0f, 0.12f, 30.0f};
    glm::vec4 wave2 = {0.7f, 0.7f, 0.08f, 20.0f};
    glm::vec4 wave3 = {0.0f, 1.0f, 0.06f, 14.0f};
    glm::vec4 wave4 = {-0.5f, 0.5f, 0.04f, 9.0f};

    float foamScale = 0.9f;
    float foamSpeed = 0.2f;
    float foamIntensity = 0.6f;
    float foamRange = 0.8f;
    float depthMult = 5.0f;

    float colorBlendFactor = 0.2f;
    glm::vec4 waterColor = {0.05f, 0.15f, 0.4f, 1.f};

    WaterNode3D(const std::string &name = "water");
    ~WaterNode3D() override;

    bool init(float width, float depth);
    void release();
    void update(float dt) override;

    void updateCameraUniforms(const Camera *camera);

    Material *getMaterial() const { return material_; }
    MeshBuffer *getRenderBuffer() { return &buffer_; }
    const MeshBuffer *getRenderBuffer() const { return &buffer_; }
    BoundingBox getAABB() const { return buffer_.aabb.transformed(worldMatrix()); }
    float waterHeight() const { return worldPosition().y; }

    RenderTarget *reflectionRT() const { return reflectionRT_; }
    RenderTarget *refractionRT() const { return refractionRT_; }

    Texture *debugReflTex() const;
    Texture *debugRefrTex() const;
    Texture *debugRefrDepthTex() const;

private:
    void buildGrid(float width, float depth, int subdivs = 128);

    MeshBuffer buffer_;
    Material *material_ = nullptr;
    RenderTarget *reflectionRT_ = nullptr;
    RenderTarget *refractionRT_ = nullptr;
    float time_ = 0.f;
};
