#include "SpritePreviewRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <SDL2/SDL.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "Animation.hpp"
#include "Animator.hpp"
#include "Camera.hpp"
#include "Manager.hpp"
#include "Material.hpp"
#include "Node.hpp"
#include "Opengl.hpp"
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
    weaponMesh_ = nullptr;
    weaponNode_ = nullptr;
    animationNames_.clear();
    surfaceLabels_.clear();
    boneLabels_.clear();
    attachmentBoneName_.clear();
    activeSocketBone_.clear();
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
    applyDefaultSurfaceTextures();
    rebuildSurfaceLabels();
    rebuildBoneLabels();
    syncProjectAnimation(project);
    if (!animationNames_.empty())
        project.animationPlaying = true;

    updateAttachmentSocket();

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

void SpritePreviewRenderer::rebuildBoneLabels()
{
    boneLabels_.clear();
    if (!mesh_)
        return;

    boneLabels_.reserve(mesh_->bones.size());
    for (size_t i = 0; i < mesh_->bones.size(); ++i)
    {
        const Bone& bone = mesh_->bones[i];
        std::string label = bone.name.empty() ? ("Bone " + std::to_string(i)) : bone.name;
        label += " (#" + std::to_string(i) + ")";
        boneLabels_.push_back(label);
    }
}
void SpritePreviewRenderer::rebuildSurfaceLabels()
{
    surfaceLabels_.clear();
    if (!mesh_)
        return;

    surfaceLabels_.reserve(mesh_->surfaces.size());
    for (size_t i = 0; i < mesh_->surfaces.size(); ++i)
    {
        const Surface& surface = mesh_->surfaces[i];
        std::string label = "Surface " + std::to_string(i);
        Material* mat = node_ ? node_->getMaterial(surface.material_index) : nullptr;
        if (mat && !mat->name.empty())
            label += " (" + mat->name + ")";
        else
            label += " (mat " + std::to_string(surface.material_index) + ")";
        surfaceLabels_.push_back(label);
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

Material* SpritePreviewRenderer::getOrCreateMaterial(int slot)
{
    if (!node_)
        return nullptr;

    Material* mat = node_->getMaterial(slot);
    if (!mat)
    {
        mat = new Material();
        mat->name = "spritegen_material_" + std::to_string(slot);
        node_->setMaterial(slot, mat);
    }
    return mat;
}

void SpritePreviewRenderer::applyDefaultSurfaceTextures()
{
    if (!mesh_ || !node_)
        return;

    Texture* pattern = TextureManager::instance().getPattern();
    if (!pattern)
        pattern = TextureManager::instance().getWhite();

    for (const Surface& surface : mesh_->surfaces)
    {
        const int slot = surface.material_index;
        Material* mat = getOrCreateMaterial(slot);
        if (!mat)
            continue;
        if (!mat->hasTexture("u_albedo"))
            mat->setTexture("u_albedo", pattern);
    }
}

bool SpritePreviewRenderer::setSurfaceTexture(int surfaceIndex, const std::string& path, std::string* errorMessage)
{
    if (!mesh_ || !node_)
    {
        if (errorMessage)
            *errorMessage = "No model loaded";
        return false;
    }

    if (surfaceIndex < 0 || surfaceIndex >= static_cast<int>(mesh_->surfaces.size()))
    {
        if (errorMessage)
            *errorMessage = "Invalid surface index";
        return false;
    }

    const Surface& surface = mesh_->surfaces[surfaceIndex];
    Material* mat = getOrCreateMaterial(surface.material_index);
    if (!mat)
    {
        if (errorMessage)
            *errorMessage = "Material not available";
        return false;
    }

    Texture* texture = nullptr;
    if (!path.empty())
    {
        const std::string name = "spritegen_surface_tex::" + path;
        texture = TextureManager::instance().load(name, path);
    }
    else
    {
        texture = TextureManager::instance().getPattern();
    }

    if (!texture)
    {
        if (errorMessage)
            *errorMessage = "Failed to load texture";
        return false;
    }

    mat->setTexture("u_albedo", texture);
    rebuildSurfaceLabels();
    return true;
}

bool SpritePreviewRenderer::resetSurfaceTexture(int surfaceIndex, std::string* errorMessage)
{
    return setSurfaceTexture(surfaceIndex, "", errorMessage);
}

void SpritePreviewRenderer::syncProjectAnimation(SpriteProject& project)
{
    if (!node_ || !mesh_ || !node_->animator || node_->animator->layerCount() <= 0)
        return;

    if (!animationNames_.empty())
    {
        const bool found = std::find(animationNames_.begin(), animationNames_.end(), project.animationName) != animationNames_.end();
        if (project.animationName.empty() || !found)
        {
            auto idle = std::find(animationNames_.begin(), animationNames_.end(), "idle");
            project.animationName = (idle != animationNames_.end()) ? *idle : animationNames_.front();
            project.frameStart = 0;
            project.frameEnd = animationFrameMax(project.animationName);
            project.currentFrame = 0.0f;
        }
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
    updateAttachmentSocket();

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

glm::mat4 SpritePreviewRenderer::buildAttachmentOffset() const
{
    glm::mat4 offset(1.0f);
    offset = glm::translate(offset, attachmentPosition_);
    offset *= glm::toMat4(glm::quat(glm::radians(attachmentRotation_)));
    offset = glm::scale(offset, attachmentScale_);
    return offset;
}

void SpritePreviewRenderer::updateAttachmentSocket()
{
    if (!node_)
        return;

    if (!attachmentEnabled_ || !weaponNode_)
    {
        if (!activeSocketBone_.empty())
        {
            node_->removeSocket(activeSocketBone_);
            activeSocketBone_.clear();
        }
        return;
    }

    if (attachmentBoneName_.empty())
        return;

    if (activeSocketBone_ != attachmentBoneName_)
    {
        if (!activeSocketBone_.empty())
            node_->removeSocket(activeSocketBone_);
        node_->addSocket(attachmentBoneName_, weaponNode_, buildAttachmentOffset());
        activeSocketBone_ = attachmentBoneName_;
    }
    else if (auto* socket = node_->getSocket(activeSocketBone_))
    {
        socket->localOffset = buildAttachmentOffset();
    }
}

bool SpritePreviewRenderer::loadWeapon(const std::string& path, std::string* errorMessage)
{
    if (!node_)
    {
        if (errorMessage)
            *errorMessage = "Load a model first";
        return false;
    }

    if (path.empty())
    {
        if (weaponNode_)
            scene_.remove(weaponNode_);
        weaponNode_ = nullptr;
        weaponMesh_ = nullptr;
        updateAttachmentSocket();
        return true;
    }

    const std::filesystem::path fsPath(path);
    if (!std::filesystem::exists(fsPath))
    {
        if (errorMessage)
            *errorMessage = "Weapon path not found";
        return false;
    }

    const std::string resourceName = "spritegen_weapon_mesh";
    AnimatedMeshManager::instance().unload(resourceName);
    weaponMesh_ = AnimatedMeshManager::instance().load(resourceName, path, fsPath.parent_path().string());
    if (!weaponMesh_)
    {
        if (errorMessage)
            *errorMessage = "Failed to load weapon mesh";
        return false;
    }

    if (weaponNode_)
        scene_.remove(weaponNode_);
    weaponNode_ = scene_.createAnimatedMeshNode("spritegen_weapon_node", weaponMesh_);
    weaponNode_->renderType = RenderType::Skinning;
    weaponNode_->setScale(glm::vec3(1.0f));

    updateAttachmentSocket();
    return true;
}

void SpritePreviewRenderer::setAttachmentBoneName(const std::string& boneName)
{
    attachmentBoneName_ = boneName;
    updateAttachmentSocket();
}

void SpritePreviewRenderer::setAttachmentTransform(const glm::vec3& position,
                                                   const glm::vec3& rotation,
                                                   const glm::vec3& scale)
{
    attachmentPosition_ = position;
    attachmentRotation_ = rotation;
    attachmentScale_ = scale;
    updateAttachmentSocket();
}

void SpritePreviewRenderer::setAttachmentEnabled(bool enabled)
{
    attachmentEnabled_ = enabled;
    updateAttachmentSocket();
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

    // Use the mesh rest bounds for stable framing so the camera and gizmo
    // don't drift with per-frame animation changes.
    BoundingBox bounds = mesh_ ? mesh_->aabb : BoundingBox{};
    glm::vec3 center(0.0f);
    glm::vec3 extents(1.0f);
    if (bounds.is_valid())
    {
        center = bounds.center();
        extents = glm::max(bounds.max - bounds.min, glm::vec3(0.001f));
    }

    const float halfMax = std::max(std::max(extents.x, extents.y), extents.z) * 0.5f;
    const float customZoom = std::max(project.customPreviewZoom, 0.1f);
    const float orthoZoom = std::max(project.orthoPreviewZoom, 0.1f);
    const float framedRadius = std::max(halfMax, 0.5f) / (mode == SpritePreviewViewMode::Custom ? customZoom : orthoZoom);

    if (mode == SpritePreviewViewMode::Custom)
    {
        camera_->projectionType = ProjectionType::Perspective;
        camera_->setFov(40.0f);
        camera_->setViewPlanes(0.05f, 1000.0f);

        const float yawRad = glm::radians(project.customPreviewYaw);
        const float pitchRad = glm::radians(project.customPreviewPitch);
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
        camera_->orthoSize = std::max(extents.y, std::max(extents.x, extents.z)) * 0.75f / orthoZoom;
        const float distance = framedRadius * 6.0f;

        if (mode == SpritePreviewViewMode::Front)
        {
            const float dir = project.frontDirection == SpriteFrontDirection::Front ? 1.0f : -1.0f;
            camera_->setPosition(center + glm::vec3(0.0f, 0.0f, distance * dir));
            camera_->lookAt(center);
        }
        else if (mode == SpritePreviewViewMode::Side)
        {
            const float dir = project.sideDirection == SpriteSideDirection::Right ? 1.0f : -1.0f;
            camera_->setPosition(center + glm::vec3(distance * dir, 0.0f, 0.0f));
            camera_->lookAt(center);
        }
        else
        {
            const float dir = project.topDirection == SpriteTopDirection::Top ? 1.0f : -1.0f;
            camera_->setPosition(center + glm::vec3(0.0f, distance * dir, 0.0f));
            camera_->lookAt(center,
                            project.topDirection == SpriteTopDirection::Top
                                ? glm::vec3(0.0f, 0.0f, -1.0f)
                                : glm::vec3(0.0f, 0.0f, 1.0f));
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
    scene_.update(0.0f);
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

bool SpritePreviewRenderer::prepareCamera(const SpriteProject& project,
                                          SpritePreviewViewMode mode,
                                          int width,
                                          int height)
{
    if (!camera_)
        return false;
    configureCamera(project, mode, width, height);
    return true;
}

const glm::mat4& SpritePreviewRenderer::cameraView() const
{
    static glm::mat4 identity(1.0f);
    return camera_ ? camera_->view : identity;
}

const glm::mat4& SpritePreviewRenderer::cameraProj() const
{
    static glm::mat4 identity(1.0f);
    return camera_ ? camera_->projection : identity;
}

std::unique_ptr<Pixmap> SpritePreviewRenderer::renderToPixmap(const SpriteProject& project,
                                                              SpritePreviewViewMode mode,
                                                              int width,
                                                              int height,
                                                              std::string* errorMessage)
{
    if (!ensureRenderTarget(mode, width, height))
    {
        if (errorMessage)
            *errorMessage = "Render target not ready";
        return nullptr;
    }

    ViewTarget& slot = viewTargets_[viewIndex(mode)];
    if (!mesh_ || !node_)
    {
        if (errorMessage)
            *errorMessage = "No model loaded";
        return nullptr;
    }

    renderSceneToTarget(project, mode, slot);

    auto out = std::make_unique<Pixmap>(width, height, 4);
    glBindFramebuffer(GL_FRAMEBUFFER, slot.target->fbo());
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out->pixels);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    out->FlipVertical();

    if (!project.transparentBackground && out->pixels)
    {
        const int pixelCount = out->width * out->height;
        for (int i = 0; i < pixelCount; ++i)
            out->pixels[i * out->components + 3] = 255;
    }

    return out;
}
