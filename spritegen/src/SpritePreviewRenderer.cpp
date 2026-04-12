#include "SpritePreviewRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <SDL2/SDL.h>
#include <glm/gtc/constants.hpp>

#include "Animation.hpp"
#include "Animator.hpp"
#include "Camera.hpp"
#include "Manager.hpp"
#include "Material.hpp"
#include "RenderState.hpp"

namespace
{

int viewIndex(SpritePreviewViewMode mode)
{
    switch (mode)
    {
    case SpritePreviewViewMode::Front: return 0;
    case SpritePreviewViewMode::Side: return 1;
    case SpritePreviewViewMode::Top: return 2;
    case SpritePreviewViewMode::Custom: return 3;
    }
    return 0;
}

std::filesystem::path executableBasePath()
{
    char* basePath = SDL_GetBasePath();
    if (!basePath)
        return std::filesystem::current_path();

    std::filesystem::path result(basePath);
    SDL_free(basePath);
    return result;
}

Shader* createSpritePreviewSkinnedShader()
{
    const char* vert = R"GLSL(
        #version 330 core
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
        }
    )GLSL";

    const char* frag = R"GLSL(
        #version 330 core
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
        }
    )GLSL";

    return ShaderManager::instance().loadFromSource("spritegen_preview_skinned_shader", vert, frag);
}

} // namespace

SpritePreviewRenderer::SpritePreviewRenderer()
{
    ensureSceneReady();
}

SpritePreviewRenderer::~SpritePreviewRenderer()
{
    scene_.clear();
}

bool SpritePreviewRenderer::ensureSceneReady()
{
    if (!camera_)
    {
        camera_ = scene_.createCamera("sprite_preview_camera");
        if (!camera_)
            return false;
        scene_.setCamera(camera_);
    }

    if (!skinnedShader_)
        skinnedShader_ = createSpritePreviewSkinnedShader();

    return camera_ != nullptr && skinnedShader_ != nullptr;
}

std::string SpritePreviewRenderer::resolveModelPath(const std::string& path) const
{
    if (path.empty())
        return std::string();

    std::filesystem::path requested(path);
    if (requested.is_absolute() && std::filesystem::exists(requested))
        return requested.string();

    const std::filesystem::path cwdCandidate = std::filesystem::current_path() / requested;
    if (std::filesystem::exists(cwdCandidate))
        return cwdCandidate.lexically_normal().string();

    const std::filesystem::path base = executableBasePath();
    const std::array<std::filesystem::path, 3> candidates = {
        base / requested,
        base / ".." / requested,
        base / ".." / ".." / requested,
    };

    for (const std::filesystem::path& candidate : candidates)
    {
        if (std::filesystem::exists(candidate))
            return candidate.lexically_normal().string();
    }

    return requested.string();
}

void SpritePreviewRenderer::clearSceneModel()
{
    if (node_)
    {
        scene_.remove(node_);
        delete node_;
        node_ = nullptr;
    }

    mesh_ = nullptr;
    animationNames_.clear();
}

bool SpritePreviewRenderer::loadModel(SpriteProject& project, std::string* errorMessage)
{
    if (!ensureSceneReady())
    {
        statusText_ = "Failed to initialize preview renderer";
        if (errorMessage)
            *errorMessage = statusText_;
        return false;
    }

    const std::string resolvedPath = resolveModelPath(project.modelPath);
    const std::filesystem::path fsPath(resolvedPath);
    if (!std::filesystem::exists(fsPath))
    {
        clearSceneModel();
        statusText_ = "Model path not found: " + resolvedPath;
        if (errorMessage)
            *errorMessage = statusText_;
        return false;
    }

    clearSceneModel();

    const std::string resourceName = "spritegen_preview_mesh";
    AnimatedMeshManager::instance().unload(resourceName);
    mesh_ = AnimatedMeshManager::instance().load(resourceName, resolvedPath, fsPath.parent_path().string());
    if (!mesh_)
    {
        statusText_ = "Failed to load animated mesh: " + resolvedPath;
        if (errorMessage)
            *errorMessage = statusText_;
        return false;
    }

    node_ = scene_.createAnimatedMeshNode("sprite_preview_node", mesh_);
    if (!node_)
    {
        mesh_ = nullptr;
        statusText_ = "Failed to create preview node";
        if (errorMessage)
            *errorMessage = statusText_;
        return false;
    }

    node_->renderType = RenderType::Skinning;
    node_->setPosition(0.0f, 0.0f, 0.0f);
    node_->setScale(glm::vec3(1.0f));
    node_->yaw(180.0f);

    loadedModelPath_ = resolvedPath;
    refreshAnimationList();
    syncProjectAnimation(project);

    statusText_ = "Loaded " + fsPath.filename().string();
    if (errorMessage)
        *errorMessage = statusText_;
    return true;
}

bool SpritePreviewRenderer::reloadModel(SpriteProject& project, std::string* errorMessage)
{
    return loadModel(project, errorMessage);
}

void SpritePreviewRenderer::refreshAnimationList()
{
    animationNames_.clear();
    if (!mesh_)
        return;

    for (Animation* animation : mesh_->animations)
    {
        if (animation)
            animationNames_.push_back(animation->name);
    }
}

int SpritePreviewRenderer::animationFrameMax(const std::string& animationName) const
{
    if (!mesh_ || animationName.empty())
        return 0;

    for (Animation* animation : mesh_->animations)
    {
        if (animation && animation->name == animationName)
            return std::max(0, static_cast<int>(std::round(animation->duration)));
    }
    return 0;
}

void SpritePreviewRenderer::syncProjectAnimation(SpriteProject& project)
{
    if (!node_ || !mesh_ || !node_->animator || node_->animator->layerCount() <= 0)
        return;

    if (project.animationName.empty() && !animationNames_.empty())
    {
        auto idle = std::find(animationNames_.begin(), animationNames_.end(), "idle");
        project.animationName = (idle != animationNames_.end()) ? *idle : animationNames_.front();
        project.frameStart = 0;
        project.frameEnd = animationFrameMax(project.animationName);
        project.currentFrame = 0.0f;
    }

    AnimationLayer* layer = node_->animator->getLayer(0);
    if (!layer || project.animationName.empty())
        return;

    if (layer->currentName() != project.animationName)
        layer->play(project.animationName, PlayMode::Loop, 0.0f);

    const int maxFrame = animationFrameMax(project.animationName);
    project.frameStart = std::clamp(project.frameStart, 0, maxFrame);
    project.frameEnd = std::clamp(project.frameEnd, project.frameStart, maxFrame);
    project.currentFrame = std::clamp(project.currentFrame, static_cast<float>(project.frameStart), static_cast<float>(project.frameEnd));
}

void SpritePreviewRenderer::applyTransformChannels(const SpriteProject& project)
{
    if (!node_ || !node_->animator) return;

    AnimationLayer* layer = node_->animator->getLayer(0);
    if (!layer) return;

    unsigned char mask = 0;
    if (project.usePositionChannel)
        mask |= (unsigned char)TransformChannel::Position;
    if (project.useRotationChannel)
        mask |= (unsigned char)TransformChannel::Rotation;
    if (project.useScaleChannel)
        mask |= (unsigned char)TransformChannel::Scale;

    layer->setTransformMask(mask);
}

void SpritePreviewRenderer::update(SpriteProject& project, float dt)
{
    if (!node_ || !mesh_ || !node_->animator || node_->animator->layerCount() <= 0)
        return;

    // Apply model transform from project
    node_->setPosition(project.modelPosition.x, project.modelPosition.y, project.modelPosition.z);
    node_->setScale(glm::vec3(project.modelScale));
    node_->setRotationEuler(project.modelRotation);

    syncProjectAnimation(project);
    applyTransformChannels(project);

    AnimationLayer* layer = node_->animator->getLayer(0);
    if (!layer || project.animationName.empty())
        return;

    const float start = static_cast<float>(project.frameStart);
    const float end = static_cast<float>(std::max(project.frameStart, project.frameEnd));
    const int clipMaxFrame = std::max(1, animationFrameMax(project.animationName));

    if (project.animationPlaying && end > start)
    {
        project.currentFrame += dt * project.animationFps;
        if (project.currentFrame > end)
        {
            if (project.animationLoop)
                project.currentFrame = start + std::fmod(project.currentFrame - start, end - start + 1.0f);
            else
                project.currentFrame = end;
        }
    }

    project.currentFrame = std::clamp(project.currentFrame, start, end);
    layer->setNormalizedTime(std::clamp(project.currentFrame / static_cast<float>(clipMaxFrame), 0.0f, 1.0f));
    scene_.update(0.0f);
}

bool SpritePreviewRenderer::ensureRenderTarget(SpritePreviewViewMode mode, int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    ViewTarget& slot = viewTargets_[viewIndex(mode)];
    if (slot.target && slot.target->valid() && slot.width == width && slot.height == height)
        return true;

    slot.target = std::make_unique<RenderTarget>();
    slot.width = width;
    slot.height = height;
    if (!slot.target->create(width, height))
        return false;
    if (!slot.target->addColorAttachment())
        return false;
    if (!slot.target->addDepthAttachment())
        return false;
    return slot.target->finalize();
}

void SpritePreviewRenderer::configureCamera(const SpriteProject& project, SpritePreviewViewMode mode, int width, int height)
{
    if (!camera_)
        return;

    camera_->setViewport(0, 0, width, height);
    camera_->clearColorVal = project.clearColor;

    BoundingBox bounds = mesh_ ? mesh_->computeSkinnedAABB() : BoundingBox{};
    glm::vec3 center(0.0f);
    glm::vec3 extents(1.0f);
    if (bounds.is_valid())
    {
        center = bounds.center();
        extents = glm::max(bounds.max - bounds.min, glm::vec3(0.001f));
    }

    const float halfMax = std::max(std::max(extents.x, extents.y), extents.z) * 0.5f;
    const float framedRadius = std::max(halfMax, 0.5f) / std::max(project.previewZoom, 0.1f);

    if (mode == SpritePreviewViewMode::Custom)
    {
        camera_->projectionType = ProjectionType::Perspective;
        camera_->setFov(40.0f);
        camera_->setViewPlanes(0.05f, 1000.0f);

        const float yawRad = glm::radians(project.previewYaw);
        const float pitchRad = glm::radians(project.previewPitch);
        const float distance = framedRadius * 3.4f + 1.0f;

        // Clamp pitch to avoid gimbal lock
        float clampedPitch = glm::clamp(pitchRad, glm::radians(-89.0f), glm::radians(89.0f));

        const glm::vec3 offset(
            std::cos(clampedPitch) * std::sin(yawRad) * distance,
            std::sin(clampedPitch) * distance + extents.y * 0.25f,
            std::cos(clampedPitch) * std::cos(yawRad) * distance);
        camera_->setPosition(center + offset);
        camera_->lookAt(center + glm::vec3(0.0f, extents.y * 0.15f, 0.0f));
    }
    else
    {
        camera_->projectionType = ProjectionType::Orthographic;
        camera_->setViewPlanes(-1000.0f, 1000.0f);
        camera_->orthoSize = std::max(extents.y, std::max(extents.x, extents.z)) * 0.75f / std::max(project.previewZoom, 0.1f);
        const float distance = framedRadius * 6.0f;

        if (mode == SpritePreviewViewMode::Front)
        {
            camera_->setPosition(center + glm::vec3(0.0f, 0.0f, distance));
            camera_->lookAt(center);
        }
        else if (mode == SpritePreviewViewMode::Side)
        {
            camera_->setPosition(center + glm::vec3(distance, 0.0f, 0.0f));
            camera_->lookAt(center);
        }
        else
        {
            camera_->setPosition(center + glm::vec3(0.0f, distance, 0.0f));
            camera_->lookAt(center, glm::vec3(0.0f, 0.0f, -1.0f));
        }
    }

    camera_->updateMatrices();
}

void SpritePreviewRenderer::renderSceneToTarget(const SpriteProject& project, SpritePreviewViewMode mode, ViewTarget& target)
{
    if (!target.target || !target.target->valid() || !camera_ || !skinnedShader_)
        return;

    target.target->bind();
    configureCamera(project, mode, target.width, target.height);

    scene_.setCamera(camera_);
    scene_.beginPass();
    scene_.setShader(skinnedShader_);

    Texture* white = TextureManager::instance().getWhite();
    if (white)
    {
        RenderState::instance().bindTexture(0, white->target, white->id);
        skinnedShader_->setInt("u_albedo", 0);
    }

    skinnedShader_->setVec4("u_color", glm::vec4(1.0f));
    skinnedShader_->setVec3("u_lightDir", glm::normalize(glm::vec3(-0.6f, -1.0f, -0.45f)));
    skinnedShader_->setVec3("u_ambient", glm::vec3(0.40f));
    scene_.render(RenderType::Skinning);
    scene_.endPass();

    target.target->unbind();
}

Texture* SpritePreviewRenderer::renderView(const SpriteProject& project, SpritePreviewViewMode mode, int width, int height)
{
    if (!ensureRenderTarget(mode, width, height))
        return nullptr;

    ViewTarget& slot = viewTargets_[viewIndex(mode)];
    if (!mesh_ || !node_)
        return slot.target ? slot.target->colorTex() : nullptr;

    renderSceneToTarget(project, mode, slot);
    return slot.target->colorTex();
}
