#include "CascadeShadowMap.hpp"
#include "RenderState.hpp"
#include "Camera.hpp"
#include "Material.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <SDL2/SDL.h>
#include <cmath>
#include <algorithm>
#include <array>

// ============================================================
//  Helpers
// ============================================================
namespace
{

    /// Return the 8 corners of the camera frustum sub-view between near and far.
    std::array<glm::vec4, 8> frustumCornersWorldSpace(const glm::mat4 &proj,
                                                      const glm::mat4 &view)
    {
        glm::mat4 inv = glm::inverse(proj * view);
        std::array<glm::vec4, 8> corners;
        int i = 0;
        for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
                for (int z = 0; z < 2; ++z)
                {
                    glm::vec4 pt = inv * glm::vec4(x * 2.f - 1.f, y * 2.f - 1.f, z * 2.f - 1.f, 1.f);
                    corners[i++] = pt / pt.w;
                }
        return corners;
    }

} // namespace

// ============================================================
//  CascadeShadowMap — initialise / release
// ============================================================
bool CascadeShadowMap::initialize(unsigned int w, unsigned int h)
{
    width_ = w;
    height_ = h;

    for (int i = 0; i < CSM_NUM_CASCADES; ++i)
    {
        // Depth texture
        glGenTextures(1, &textures_[i]);
        glBindTexture(GL_TEXTURE_2D, textures_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, (GLsizei)w, (GLsizei)h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // FBO — depth-only
        glGenFramebuffers(1, &fbos_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, fbos_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, textures_[i], 0);
        // Depth-only FBO — disable colour buffers (GLES3 glDrawBuffers)
        GLenum none = GL_NONE;
        glDrawBuffers(1, &none);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[CSM] FBO incomplete for cascade %d: 0x%x", i, status);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[CSM] Initialized %d cascades at %ux%u", CSM_NUM_CASCADES, w, h);
    return true;
}

void CascadeShadowMap::release()
{
    for (int i = 0; i < CSM_NUM_CASCADES; ++i)
    {
        if (fbos_[i])
        {
            glDeleteFramebuffers(1, &fbos_[i]);
            fbos_[i] = 0;
        }
        if (textures_[i])
        {
            glDeleteTextures(1, &textures_[i]);
            textures_[i] = 0;
        }
    }
}

// ============================================================
//  Build a tight ortho light matrix fitting the sub-frustum
// ============================================================
glm::mat4 CascadeShadowMap::computeLightSpaceMatrix(const Camera &cam,
                                                    float splitNear,
                                                    float splitFar,
                                                    float &texelSizeOut) const
{
    // Build a projection for just this slice
    float fovRad = glm::radians(cam.fov);
    glm::mat4 sliceProj = glm::perspective(fovRad, cam.aspect(), splitNear, splitFar);
    glm::mat4 view = cam.getView();

    auto corners = frustumCornersWorldSpace(sliceProj, view);

    // ── Sphere-fit the sub-frustum ────────────────────────────────────────
    // Use the average of the 8 corners as centre, then measure the max radius.
    // The ortho bounds are kept at ±radius so the shadow map footprint is
    // constant regardless of camera rotation (AABB changes; sphere does not).
    glm::vec3 centre(0.f);
    for (const auto &c : corners)
        centre += glm::vec3(c);
    centre /= (float)corners.size();

    float radius = 0.f;
    for (const auto &c : corners)
        radius = std::max(radius, glm::length(glm::vec3(c) - centre));

    // ── Texel-snap the centroid in light space ────────────────────────────
    // Snapping world (0,0,0) only stabilises light rotation; the centroid moves
    // continuously with the camera, which is what causes camera-move shimmer.
    // Fix: snap the centroid itself to a texel-sized grid in light space so
    // the entire shadow frustum jumps in discrete texel-sized steps.
    const float texelWorld = (2.f * radius) / (float)width_;   // world units per texel
    texelSizeOut = texelWorld;

    // Build a temporary light-space basis from the unsnapped centroid
    const glm::vec3 lightDir = glm::normalize(lightDirection);
    // Choose a stable up vector — avoid gimbal lock when light is near vertical
    glm::vec3 up = (std::abs(glm::dot(lightDir, glm::vec3(0,1,0))) < 0.99f)
                   ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
    glm::vec3 right   = glm::normalize(glm::cross(up, lightDir));
    glm::vec3 lightUp = glm::cross(lightDir, right);

    // Project centre onto the light-space XY axes, then round to texel grid
    float cx = glm::dot(centre, right);
    float cy = glm::dot(centre, lightUp);
    cx = std::round(cx / texelWorld) * texelWorld;
    cy = std::round(cy / texelWorld) * texelWorld;

    // Reconstruct snapped centroid in world space
    // (Z component stays free — depth range doesn't need snapping)
    float cz = glm::dot(centre, lightDir);
    glm::vec3 snappedCentre = cx * right + cy * lightUp + cz * lightDir;

    // Light-view matrix looking at the snapped centroid
    glm::mat4 lightView = glm::lookAt(
        snappedCentre,
        snappedCentre + lightDir,
        lightUp);

    // Symmetric ortho from sphere radius — stable size regardless of camera rotation
    float minX = -radius, maxX = radius;
    float minY = -radius, maxY = radius;

    // Z: use actual AABB of sub-frustum corners in light space (tight depth range)
    float minZ = 1e30f, maxZ = -1e30f;
    for (const auto &c : corners)
    {
        glm::vec4 ls = lightView * c;
        minZ = std::min(minZ, ls.z);
        maxZ = std::max(maxZ, ls.z);
    }
    // Pull Z planes out to catch casters outside the camera sub-frustum
    constexpr float zMult = 3.f;
    if (minZ < 0.f) minZ *= zMult; else minZ /= zMult;
    if (maxZ < 0.f) maxZ /= zMult; else maxZ *= zMult;

    glm::mat4 lightProj = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
    return lightProj * lightView;
}

// ============================================================
//  Per-frame update
// ============================================================
void CascadeShadowMap::update(const Camera &cam)
{
    // Use the CSM shadow far plane (set via setShadowFarPlane()), NOT the camera
    // far plane, so the logarithmic+uniform split scheme (GPU Gems 3) distributes
    // cascades only within the shadow range rather than the full view distance.
    const float near = cam.nearPlane;
    const float far  = farPlane;          // this->farPlane, controlled by setLambda/setShadowFarPlane
    const float ratio = far / near;
    for (int i = 0; i < CSM_NUM_CASCADES; ++i)
    {
        float p          = (float)(i + 1) / (float)CSM_NUM_CASCADES;
        float logSplit   = near * std::pow(ratio, p);
        float uniSplit   = near + (far - near) * p;
        cascadeSplits[i] = lambda * logSplit + (1.f - lambda) * uniSplit;
    }

    float prevSplit = cam.nearPlane;
    for (int i = 0; i < CSM_NUM_CASCADES; ++i)
    {
        lightSpaceMatrices[i] = computeLightSpaceMatrix(cam, prevSplit, cascadeSplits[i],
                                                        texelSizeWorld[i]);
        prevSplit = cascadeSplits[i];
    }
}

// ============================================================
//  FBO bind / unbind
// ============================================================
void CascadeShadowMap::beginCascade(int c)
{
    glDepthMask(GL_TRUE);   // ensure glClear writes depth even if last pass disabled it
    glBindFramebuffer(GL_FRAMEBUFFER, fbos_[c]);
    RenderState::instance().setViewport(0, 0, (GLsizei)width_, (GLsizei)height_);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void CascadeShadowMap::endCascade()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ============================================================
//  Upload to shader
// ============================================================

// Hardcoded uniform name tables — avoids std::to_string + string concatenation
// heap allocations every frame.  Must match CSM_NUM_CASCADES = 4.
static_assert(CSM_NUM_CASCADES == 4, "Update hardcoded uniform name tables below");
static constexpr const char* kShadowMap[4] = {
    "u_shadowMap[0]", "u_shadowMap[1]", "u_shadowMap[2]", "u_shadowMap[3]"
};
static constexpr const char* kLightSpace[4] = {
    "u_lightSpace[0]", "u_lightSpace[1]", "u_lightSpace[2]", "u_lightSpace[3]"
};
static constexpr const char* kCascadeSplits[4] = {
    "u_cascadeSplits[0]", "u_cascadeSplits[1]", "u_cascadeSplits[2]", "u_cascadeSplits[3]"
};
static constexpr const char* kTexelSize[4] = {
    "u_cascadeTexelSize[0]", "u_cascadeTexelSize[1]", "u_cascadeTexelSize[2]", "u_cascadeTexelSize[3]"
};

void CascadeShadowMap::bindToShader(Shader *shader, int baseTextureUnit) const
{
    if (!shader)
        return;

    auto& rs = RenderState::instance();
    for (int i = 0; i < CSM_NUM_CASCADES; ++i)
    {
        rs.bindTexture(baseTextureUnit + i, GL_TEXTURE_2D, textures_[i]);
        shader->setInt  (kShadowMap[i],    baseTextureUnit + i);
        shader->setMat4 (kLightSpace[i],   lightSpaceMatrices[i]);
        shader->setFloat(kCascadeSplits[i],cascadeSplits[i]);
        shader->setFloat(kTexelSize[i],    texelSizeWorld[i]);
    }
    shader->setVec2("u_shadowMapSize", glm::vec2((float)width_, (float)height_));
}

// ============================================================
//  CsmDepthPass
// ============================================================
void CsmDepthPass::execute(const FrameContext &ctx, RenderQueue &queue) const
{
    if (!csm || !shader)
        return;

    auto &rs = RenderState::instance();
    csm->beginCascade(cascade);

    rs.setDepthTest(true);
    rs.setDepthWrite(true);
    rs.setCull(true);
    rs.setBlend(false);

    // Polygon offset: nudge stored depth away from light so lit fragments
    // don't self-shadow. Works on flat planes too (unlike front-face culling).
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    rs.useProgram(shader->getId());
    shader->setMat4("u_lightSpace", csm->lightSpaceMatrices[cascade]);

    for (const auto &item : ctx.shadowQueue.getOpaque())
    {
        if (!item.drawable)
            continue;

        if (item.instanceCount > 1)
        {
            // Instanced draw — model matrix comes from the per-instance buffer,
            // not from u_model.  Requires instancedShader (csm_depth_instanced.vert).
            if (!instancedShader) continue;
            rs.useProgram(instancedShader->getId());
            instancedShader->setMat4("u_lightSpace", csm->lightSpaceMatrices[cascade]);
            if (item.indexCount > 0)
                item.drawable->drawRangeInstanced(item.indexStart, item.indexCount, item.instanceCount);
            else
                item.drawable->drawInstanced(item.instanceCount);
            // Switch back to regular shader for any subsequent non-instanced items
            rs.useProgram(shader->getId());
            shader->setMat4("u_lightSpace", csm->lightSpaceMatrices[cascade]);
        }
        else
        {
            shader->setMat4("u_model", item.model);
            if (item.indexCount > 0)
                item.drawable->drawRange(item.indexStart, item.indexCount);
            else
                item.drawable->draw();
        }
    }

    glDisable(GL_POLYGON_OFFSET_FILL);
    csm->endCascade();
}

// ============================================================
//  CsmTechnique
// ============================================================
CsmTechnique::CsmTechnique()
{
    csm_ = new CascadeShadowMap();
}

CsmTechnique::~CsmTechnique()
{
    release();
    delete csm_;
}

bool CsmTechnique::initialize(unsigned int shadowRes)
{
    if (!csm_->initialize(shadowRes, shadowRes))
        return false;
    return true;
}

void CsmTechnique::release()
{
    csm_->release();
}

void CsmTechnique::render(const FrameContext &ctx, RenderQueue &queue) const
{
    if (!ctx.camera)
        return;

    if (!ctx.secondary)
    {
        // Primary render: update cascade matrices + run depth passes
        csm_->update(*ctx.camera);
        for (RenderPass *pass : passes)
            if (auto *dp = dynamic_cast<CsmDepthPass *>(pass))
                dp->execute(ctx, queue);
    }

    // Upload CSM uniforms to litShader before the opaque lit pass
    // (uses whatever shadow maps exist — last frame's for secondary renders)
    if (litShader)
    {
        RenderState::instance().useProgram(litShader->getId());
        csm_->bindToShader(litShader, 1);
    }

    // Restore viewport + execute opaque + transparent passes
    auto &rs = RenderState::instance();
    rs.setViewport(ctx.viewport.x, ctx.viewport.y,
                   ctx.viewport.z, ctx.viewport.w);

    for (RenderPass *pass : passes)
        if (!dynamic_cast<CsmDepthPass *>(pass))
            pass->execute(ctx, queue);
}

OpaquePass *CsmTechnique::getOpaquePass() const
{
    for (auto *p : passes)
        if (auto *op = dynamic_cast<OpaquePass *>(p))
            return op;
    return nullptr;
}

TransparentPass *CsmTechnique::getTransparentPass() const
{
    for (auto *p : passes)
        if (auto *tp = dynamic_cast<TransparentPass *>(p))
            return tp;
    return nullptr;
}
