#include "Renderer.hpp"
#include "Camera.hpp"
#include "Manager.hpp"
#include "Material.hpp"
#include "Node.hpp"
#include "RenderState.hpp"
#include "Scene.hpp"
#include "WaterNode.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

static const DirectionalLight *findPrimaryDirectionalLight(const RenderScene &scene)
{
    for (const Light *light : scene.lights)
    {
        if (!light || light->lightType != LightType::Directional)
            continue;
        return static_cast<const DirectionalLight *>(light);
    }
    return nullptr;
}

static void sendPrimaryLight(Shader *shader, const RenderScene &scene)
{
    glm::vec4 lightDir = glm::vec4(0.f, 1.f, 0.f, 0.f);
    glm::vec4 lightColor = glm::vec4(1.f, 1.f, 1.f, 1.f);
    glm::vec4 ambient = glm::vec4(0.08f, 0.08f, 0.08f, 1.f);

    if (const DirectionalLight *directional = findPrimaryDirectionalLight(scene))
    {
        lightDir = glm::vec4(glm::normalize(-directional->forward()), 0.f);
        lightColor = glm::vec4(directional->color * directional->intensity, 1.f);
        ambient = glm::vec4(directional->ambient, 1.f);
    }

    shader->setVec4("u_lightDir", lightDir);
    shader->setVec4("u_lightColor", lightColor);
    shader->setVec4("u_ambient", ambient);
}

static BoundingBox computeSceneBounds(const RenderScene &scene)
{
    BoundingBox bounds;
    bool hasBounds = false;

    auto gather = [&](const std::vector<RenderObject> &items)
    {
        for (const RenderObject &item : items)
        {
            if (!item.worldBounds.is_valid())
                continue;
            if (!hasBounds)
            {
                bounds = item.worldBounds;
                hasBounds = true;
            }
            else
            {
                bounds.expand(item.worldBounds);
            }
        }
    };

    gather(scene.opaque);
    gather(scene.water);
    gather(scene.transparent);

    if (!hasBounds)
    {
        bounds.expand(glm::vec3(-10.f, -10.f, -10.f));
        bounds.expand(glm::vec3(10.f, 10.f, 10.f));
    }

    return bounds;
}

static glm::mat4 buildLightSpaceMatrix(const RenderScene &scene, const DirectionalLight *light)
{
    BoundingBox bounds = computeSceneBounds(scene);
    glm::vec3 center = bounds.center();
    glm::vec3 extents = bounds.extents();
    float radius = glm::length(extents);
    if (radius < 10.0f)
        radius = 10.0f;

    glm::vec3 lightDir = glm::normalize(-light->forward());
    glm::vec3 up = glm::vec3(0.f, 1.f, 0.f);
    if (std::abs(glm::dot(lightDir, up)) > 0.98f)
        up = glm::vec3(0.f, 0.f, 1.f);

    glm::vec3 eye = center - lightDir * (radius * 2.0f);
    glm::mat4 lightView = glm::lookAt(eye, center, up);

    glm::vec3 corners[8] = {
        {bounds.min.x, bounds.min.y, bounds.min.z},
        {bounds.max.x, bounds.min.y, bounds.min.z},
        {bounds.min.x, bounds.max.y, bounds.min.z},
        {bounds.max.x, bounds.max.y, bounds.min.z},
        {bounds.min.x, bounds.min.y, bounds.max.z},
        {bounds.max.x, bounds.min.y, bounds.max.z},
        {bounds.min.x, bounds.max.y, bounds.max.z},
        {bounds.max.x, bounds.max.y, bounds.max.z},
    };

    glm::vec3 minLS(std::numeric_limits<float>::max());
    glm::vec3 maxLS(-std::numeric_limits<float>::max());
    for (const glm::vec3 &corner : corners)
    {
        glm::vec3 p = glm::vec3(lightView * glm::vec4(corner, 1.0f));
        minLS = glm::min(minLS, p);
        maxLS = glm::max(maxLS, p);
    }

    const float padding = 8.0f;
    minLS -= glm::vec3(padding);
    maxLS += glm::vec3(padding);

    glm::mat4 lightProj = glm::ortho(minLS.x, maxLS.x,
                                     minLS.y, maxLS.y,
                                     -maxLS.z - padding, -minLS.z + padding);
    return lightProj * lightView;
}

bool Renderer::initialize()
{
    if (initialized_)
        return true;

    auto &shaders = ShaderManager::instance();
    solidShader_ = shaders.load("renderer_v2_solid",
                                "assets/shaders/solid.vert",
                                "assets/shaders/solid.frag");
    texturedShader_ = shaders.load("renderer_v2_textured",
                                   "assets/shaders/textured.vert",
                                   "assets/shaders/textured.frag");
    detailShader_ = shaders.load("renderer_v2_detail",
                                 "assets/shaders/detail.vert",
                                 "assets/shaders/detail.frag");
    terrainShader_ = shaders.load("renderer_v2_terrain",
                                  "assets/shaders/terrain.vert",
                                  "assets/shaders/terrain.frag");
    waterShader_ = shaders.load("renderer_v2_water",
                                "assets/shaders/water.vert",
                                "assets/shaders/water.frag");
    skyShader_ = shaders.load("renderer_v2_sky",
                              "assets/shaders/sky.vert",
                              "assets/shaders/sky.frag");
    skinnedShader_ = shaders.load("renderer_v2_skinned",
                                  "assets/shaders/skinned_simple.vert",
                                  "assets/shaders/skinned_simple.frag");
    shadowStaticShader_ = shaders.load("renderer_v2_shadow_static",
                                       "assets/shaders/shadow_depth.vert",
                                       "assets/shaders/shadow_depth.frag");
    shadowSkinnedShader_ = shaders.load("renderer_v2_shadow_skinned",
                                        "assets/shaders/shadow_depth_skinned.vert",
                                        "assets/shaders/shadow_depth.frag");

    if (!shadowTarget_.valid())
    {
        shadowPassReady_ = shadowTarget_.create(2048, 2048) &&
                           shadowTarget_.addDepthTexture() &&
                           shadowTarget_.finalize();
    }
    else
    {
        shadowPassReady_ = true;
    }

    if (skyVao_ == 0)
        glGenVertexArrays(1, &skyVao_);

    initialized_ = solidShader_ && texturedShader_ && detailShader_ && terrainShader_ &&
                   waterShader_ && skyShader_ && skinnedShader_ &&
                   shadowStaticShader_ && shadowSkinnedShader_ &&
                   shadowPassReady_;
    return initialized_;
}

Shader *Renderer::resolveShader(const Material *material) const
{
    if (!material)
        return nullptr;

    if (material->getType() == MaterialType::Custom && material->getShader())
        return material->getShader();

    switch (material->getType())
    {
    case MaterialType::Solid:
        return solidShader_;
    case MaterialType::Textured:
        return texturedShader_;
    case MaterialType::Detail:
        return detailShader_;
    case MaterialType::Terrain:
        return terrainShader_;
    case MaterialType::Water:
        return waterShader_;
    case MaterialType::Skinned:
        return skinnedShader_;
    case MaterialType::Custom:
        return material->getShader();
    }

    return nullptr;
}

const DirectionalLight *Renderer::primaryShadowLight(const RenderScene &scene) const
{
    const DirectionalLight *light = findPrimaryDirectionalLight(scene);
    if (!light || !light->castShadow)
        return nullptr;
    return light;
}

void Renderer::renderSky(const RenderScene &scene)
{
    if (!scene.camera || !scene.sky.enabled || !skyShader_)
        return;

    auto &state = RenderState::instance();
    state.setDepthTest(false);
    state.setDepthWrite(false);
    state.setCull(false);
    state.setBlend(false);
    state.useProgram(skyShader_->getId());

    skyShader_->setMat4("u_invViewProj", glm::inverse(scene.camera->viewProjection));
    skyShader_->setVec4("u_cameraPos", glm::vec4(scene.camera->worldPosition(), 1.f));
    sendPrimaryLight(skyShader_, scene);
    skyShader_->setVec3("u_skyTop", scene.sky.top);
    skyShader_->setVec3("u_skyHorizon", scene.sky.horizon);
    skyShader_->setVec3("u_groundColor", scene.sky.ground);

    glBindVertexArray(skyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void Renderer::renderShadowPass(const RenderScene &scene)
{
    const DirectionalLight *light = primaryShadowLight(scene);
    if (!light || !shadowPassReady_)
        return;

    lightSpaceMatrix_ = buildLightSpaceMatrix(scene, light);

    shadowTarget_.bind();

    auto &state = RenderState::instance();
    state.resetCache();
    state.setViewport(0, 0, shadowTarget_.width(), shadowTarget_.height());
    state.setDepthTest(true);
    state.setDepthWrite(true);
    state.setBlend(false);
    state.setCull(true);
    state.setCullFace(GL_BACK);
    state.clear(false, true);

    for (const RenderObject &item : scene.opaque)
    {
        if (!item.drawable || !item.castShadow)
            continue;

        Shader *shader = item.drawable->isSkinned() ? shadowSkinnedShader_ : shadowStaticShader_;
        if (!shader)
            continue;

        state.useProgram(shader->getId());
        shader->setMat4("u_lightViewProj", lightSpaceMatrix_);
        shader->setMat4("u_model", item.model);
        item.drawable->applyBoneMatrices(shader);

        if (item.indexCount > 0)
            item.drawable->drawRange(item.indexStart, item.indexCount);
        else
            item.drawable->draw();
    }

    shadowTarget_.unbind();
    state.resetCache();
}

void Renderer::renderSceneToTarget(Scene &scene,
                                   Camera *camera,
                                   RenderTarget *target,
                                   const Node *ignoredNode,
                                   const glm::vec4 *clipPlanes,
                                   int clipPlaneCount)
{
    if (!camera || !target)
        return;

    RenderScene secondaryScene;
    scene.buildRenderScene(camera, secondaryScene, ignoredNode);
    if (!secondaryScene.camera)
        return;

    target->bind();

    auto &state = RenderState::instance();
    state.resetCache();
    state.setViewport(0, 0, target->width(), target->height());
    state.setClearColor(secondaryScene.clearColorValue.r,
                        secondaryScene.clearColorValue.g,
                        secondaryScene.clearColorValue.b,
                        secondaryScene.clearColorValue.a);
    state.clear(secondaryScene.clearColor, secondaryScene.clearDepth);

    renderList(secondaryScene, secondaryScene.opaque, false, clipPlanes, clipPlaneCount, false);
    renderList(secondaryScene, secondaryScene.transparent, true, clipPlanes, clipPlaneCount, false);

    target->unbind();
    state.resetCache();
}

void Renderer::updateWaterTargets(Scene &scene, const RenderScene &sceneView)
{
    if (!sceneView.camera || sceneView.water.empty())
        return;

    for (const RenderObject &item : sceneView.water)
    {
        auto *water = dynamic_cast<WaterNode3D *>(item.owner);
        if (!water || !water->reflectionRT() || !water->refractionRT())
            continue;

        water->updateCameraUniforms(sceneView.camera);

        const glm::vec3 camPos = sceneView.camera->position;
        const float waterY = water->waterHeight();
        const float clipBias = water->clipBias;

        Camera reflectedCam;
        reflectedCam.fov = sceneView.camera->fov;
        reflectedCam.nearPlane = sceneView.camera->nearPlane;
        reflectedCam.farPlane = sceneView.camera->farPlane;
        reflectedCam.clearColor = sceneView.camera->clearColor;
        reflectedCam.clearDepth = sceneView.camera->clearDepth;
        reflectedCam.clearColorVal = sceneView.camera->clearColorVal;
        reflectedCam.viewport = sceneView.camera->viewport;
        reflectedCam.setAspect(sceneView.camera->viewport.z, sceneView.camera->viewport.w);
        reflectedCam.setPosition({camPos.x, 2.0f * waterY - camPos.y, camPos.z});
        glm::vec3 reflectedEuler = sceneView.camera->getEulerAngles();
        reflectedEuler.x = -reflectedEuler.x;
        reflectedCam.setEulerAngles(reflectedEuler);

        Camera refractedCam;
        refractedCam.fov = sceneView.camera->fov;
        refractedCam.nearPlane = sceneView.camera->nearPlane;
        refractedCam.farPlane = sceneView.camera->farPlane;
        refractedCam.clearColor = sceneView.camera->clearColor;
        refractedCam.clearDepth = sceneView.camera->clearDepth;
        refractedCam.clearColorVal = sceneView.camera->clearColorVal;
        refractedCam.viewport = sceneView.camera->viewport;
        refractedCam.setAspect(sceneView.camera->viewport.z, sceneView.camera->viewport.w);
        refractedCam.setPosition(sceneView.camera->position);
        refractedCam.setRotation(sceneView.camera->rotation);

        const glm::vec4 reflectionPlane(0.f, 1.f, 0.f, -(waterY - clipBias));
        const glm::vec4 refractionPlane(0.f, -1.f, 0.f, waterY + clipBias);

        renderSceneToTarget(scene, &reflectedCam, water->reflectionRT(), water, &reflectionPlane, 1);
        renderSceneToTarget(scene, &refractedCam, water->refractionRT(), water, &refractionPlane, 1);
    }
}

void Renderer::renderList(const RenderScene &scene,
                          std::vector<RenderObject> &items,
                          bool backToFront,
                          const glm::vec4 *clipPlanes,
                          int clipPlaneCount,
                          bool allowShadows)
{
    if (!scene.camera || items.empty())
        return;

    const glm::vec3 camPos = scene.camera->worldPosition();
    for (auto &item : items)
    {
        const glm::vec3 center = item.worldBounds.is_valid()
            ? item.worldBounds.center()
            : glm::vec3(item.model[3]);
        item.depth = glm::distance(center, camPos);
    }

    std::sort(items.begin(), items.end(), [backToFront](const RenderObject &a, const RenderObject &b)
    {
        if (backToFront)
            return a.depth > b.depth;
        if (a.material != b.material)
            return a.material < b.material;
        return a.depth < b.depth;
    });

    auto &state = RenderState::instance();
    Shader *lastShader = nullptr;
    const Material *lastMaterial = nullptr;
    const DirectionalLight *shadowLight = primaryShadowLight(scene);
    const bool shadowsEnabled = allowShadows &&
        shadowLight && shadowTarget_.depthTex() && shadowTarget_.depthTex()->id != 0;

    const glm::mat4 &view = scene.camera->view;
    const glm::mat4 &proj = scene.camera->projection;
    const glm::mat4 &viewProj = scene.camera->viewProjection;
    const glm::vec4 cameraPos = glm::vec4(scene.camera->worldPosition(), 1.f);

    for (const auto &item : items)
    {
        if (!item.drawable || !item.material)
            continue;

        Shader *shader = resolveShader(item.material);
        if (!shader)
            continue;

        if (shader != lastShader)
        {
            state.useProgram(shader->getId());
            shader->setMat4("u_view", view);
            shader->setMat4("u_proj", proj);
            shader->setMat4("u_viewProj", viewProj);
            shader->setInt("u_clipPlaneCount", glm::clamp(clipPlaneCount, 0, MaxClipPlanes));
            for (int i = 0; i < MaxClipPlanes; ++i)
            {
                const glm::vec4 plane = (clipPlanes && i < clipPlaneCount)
                    ? clipPlanes[i]
                    : glm::vec4(0.f);
                shader->setVec4("u_clipPlanes[" + std::to_string(i) + "]", plane);
            }
            if (shader != solidShader_)
            {
                shader->setVec4("u_cameraPos", cameraPos);
                sendPrimaryLight(shader, scene);
                if (shader != waterShader_)
                {
                    shader->setMat4("u_lightSpace", lightSpaceMatrix_);
                    shader->setFloat("u_shadowBias", shadowBias_);
                    shader->setInt("u_shadowMap", 7);
                }
            }
            lastShader = shader;
            lastMaterial = nullptr;
        }

        if (item.material != lastMaterial)
        {
            item.material->applyStates();
            item.material->bindTexturesTo(shader);
            item.material->applyUniformsTo(shader);
            if (shader != solidShader_ && shader != waterShader_ && shadowsEnabled)
                state.bindTexture(7, GL_TEXTURE_2D, shadowTarget_.depthTex()->id);
            lastMaterial = item.material;
        }

        item.drawable->applyBoneMatrices(shader);
        if (shader != solidShader_ && shader != waterShader_)
            shader->setInt("u_shadowEnabled", (shadowsEnabled && item.receiveShadow) ? 1 : 0);
        shader->setMat4("u_model", item.model);

        if (item.indexCount > 0)
            item.drawable->drawRange(item.indexStart, item.indexCount);
        else
            item.drawable->draw();
    }
}

void Renderer::render(Scene &scene, Camera *camera)
{
    if (!camera || !initialize())
        return;

    RenderScene renderScene;
    scene.buildRenderScene(camera, renderScene);
    if (!renderScene.camera)
        return;

    auto &state = RenderState::instance();
    state.setViewport(renderScene.viewport.x,
                      renderScene.viewport.y,
                      renderScene.viewport.z,
                      renderScene.viewport.w);
    renderShadowPass(renderScene);
    updateWaterTargets(scene, renderScene);

    state.resetCache();
    state.setViewport(renderScene.viewport.x,
                      renderScene.viewport.y,
                      renderScene.viewport.z,
                      renderScene.viewport.w);
    state.setClearColor(renderScene.clearColorValue.r,
                        renderScene.clearColorValue.g,
                        renderScene.clearColorValue.b,
                        renderScene.clearColorValue.a);
    state.clear(renderScene.clearColor, renderScene.clearDepth);

    renderSky(renderScene);
    renderList(renderScene, renderScene.opaque, false);
    renderList(renderScene, renderScene.water, false);
    renderList(renderScene, renderScene.transparent, true);
}
