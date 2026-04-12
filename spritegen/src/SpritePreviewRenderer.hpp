#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "RenderTarget.hpp"
#include "Scene.hpp"
#include "SpriteProject.hpp"

class AnimatedMesh;
class AnimatedMeshNode;
class Camera;
class Shader;
struct Texture;

class SpritePreviewRenderer
{
public:
    SpritePreviewRenderer();
    ~SpritePreviewRenderer();

    bool loadModel(SpriteProject& project, std::string* errorMessage = nullptr);
    bool reloadModel(SpriteProject& project, std::string* errorMessage = nullptr);

    void update(SpriteProject& project, float dt);
    Texture* renderView(const SpriteProject& project, SpritePreviewViewMode mode, int width, int height);

    bool hasLoadedModel() const { return mesh_ != nullptr; }
    const std::vector<std::string>& animationNames() const { return animationNames_; }
    const std::string& statusText() const { return statusText_; }
    const std::string& loadedPath() const { return loadedModelPath_; }
    int animationFrameMax(const std::string& animationName) const;

private:
    struct ViewTarget
    {
        std::unique_ptr<RenderTarget> target;
        int width = 0;
        int height = 0;
    };

    bool ensureSceneReady();
    bool ensureRenderTarget(SpritePreviewViewMode mode, int width, int height);
    void clearSceneModel();
    void refreshAnimationList();
    void syncProjectAnimation(SpriteProject& project);
    void applyTransformChannels(const SpriteProject& project);
    void configureCamera(const SpriteProject& project, SpritePreviewViewMode mode, int width, int height);
    void renderSceneToTarget(const SpriteProject& project, SpritePreviewViewMode mode, ViewTarget& target);
    std::string resolveModelPath(const std::string& path) const;

    Scene scene_;
    Camera* camera_ = nullptr;
    Shader* skinnedShader_ = nullptr;
    AnimatedMesh* mesh_ = nullptr;
    AnimatedMeshNode* node_ = nullptr;
    std::array<ViewTarget, 4> viewTargets_;
    std::vector<std::string> animationNames_;
    std::string loadedModelPath_;
    std::string statusText_;
};
