#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>
#include <memory>

#include "RenderTarget.hpp"
#include "Pixmap.hpp"
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
    std::unique_ptr<Pixmap> renderToPixmap(const SpriteProject& project,
                                           SpritePreviewViewMode mode,
                                           int width,
                                           int height,
                                           std::string* errorMessage = nullptr);
    bool prepareCamera(const SpriteProject& project,
                       SpritePreviewViewMode mode,
                       int width,
                       int height);
    const glm::mat4& cameraView() const;
    const glm::mat4& cameraProj() const;

    bool hasLoadedModel() const { return mesh_ != nullptr; }
    const std::vector<std::string>& animationNames() const { return animationNames_; }
    const std::vector<std::string>& surfaceLabels() const { return surfaceLabels_; }
    const std::vector<std::string>& boneLabels() const { return boneLabels_; }
    const std::string& statusText() const { return statusText_; }
    const std::string& loadedPath() const { return loadedModelPath_; }
    int animationFrameMax(const std::string& animationName) const;
    bool setSurfaceTexture(int surfaceIndex, const std::string& path, std::string* errorMessage = nullptr);
    bool resetSurfaceTexture(int surfaceIndex, std::string* errorMessage = nullptr);
    bool loadWeapon(const std::string& path, std::string* errorMessage = nullptr);
    void setAttachmentBoneName(const std::string& boneName);
    void setAttachmentTransform(const glm::vec3& position,
                                const glm::vec3& rotation,
                                const glm::vec3& scale);
    void setAttachmentEnabled(bool enabled);

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
    void rebuildSurfaceLabels();
    void rebuildBoneLabels();
    void syncProjectAnimation(SpriteProject& project);
    void applyTransformChannels(const SpriteProject& project);
    void configureCamera(const SpriteProject& project, SpritePreviewViewMode mode, int width, int height);
    void renderSceneToTarget(const SpriteProject& project, SpritePreviewViewMode mode, ViewTarget& target);
    std::string resolveModelPath(const std::string& path) const;
    Material* getOrCreateMaterial(int slot);
    void applyDefaultSurfaceTextures();
    void updateAttachmentSocket();
    glm::mat4 buildAttachmentOffset() const;

    Scene scene_;
    Camera* camera_ = nullptr;
    Shader* skinnedShader_ = nullptr;
    AnimatedMesh* mesh_ = nullptr;
    AnimatedMeshNode* node_ = nullptr;
    AnimatedMesh* weaponMesh_ = nullptr;
    AnimatedMeshNode* weaponNode_ = nullptr;
    std::array<ViewTarget, 4> viewTargets_;
    std::vector<std::string> animationNames_;
    std::vector<std::string> surfaceLabels_;
    std::vector<std::string> boneLabels_;
    std::string attachmentBoneName_;
    std::string activeSocketBone_;
    glm::vec3 attachmentPosition_ = glm::vec3(0.0f);
    glm::vec3 attachmentRotation_ = glm::vec3(0.0f);
    glm::vec3 attachmentScale_ = glm::vec3(1.0f);
    bool attachmentEnabled_ = false;
    std::string loadedModelPath_;
    std::string statusText_;
};
