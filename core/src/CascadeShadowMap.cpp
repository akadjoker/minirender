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
//  Compute split planes (practical split scheme)
// ============================================================
void CascadeShadowMap::computeSplits(float camNear, float camFar)
{
    float near = camNear;
    float far = glm::min(camFar, farPlane);

    for (int i = 0; i < CSM_NUM_CASCADES; ++i)
    {
        float p = (float)(i + 1) / (float)CSM_NUM_CASCADES;
        float logSplit = near * std::pow(far / near, p);
        float uniSplit = near + (far - near) * p;
        cascadeSplits[i] = lambda * logSplit + (1.f - lambda) * uniSplit;
    }
}

// ============================================================
//  Build a tight ortho light matrix fitting the sub-frustum
// ============================================================
glm::mat4 CascadeShadowMap::computeLightSpaceMatrix(const Camera &cam,
                                                    float splitNear,
                                                    float splitFar) const
{
    // Build a projection for just this slice
    float fovRad = glm::radians(cam.fov);
    glm::mat4 sliceProj = glm::perspective(fovRad, cam.aspect(), splitNear, splitFar);
    glm::mat4 view = cam.getView();

    auto corners = frustumCornersWorldSpace(sliceProj, view);

    // ── Sphere-fit the sub-frustum for X/Y ───────────────────────────────
    // Fitting X/Y to a sphere makes the shadow area constant regardless of
    // camera rotation (AABB changes with view angle; sphere does not).
    glm::vec3 centre(0.f);
    for (const auto &c : corners)
        centre += glm::vec3(c);
    centre /= (float)corners.size();

    float radius = 0.f;
    for (const auto &c : corners)
        radius = std::max(radius, glm::length(glm::vec3(c) - centre));

    // Light-view matrix looking at the centroid
    glm::mat4 lightView = glm::lookAt(
        centre,
        centre  + glm::normalize(lightDirection),
        
        glm::vec3(0.f, 1.f, 0.f));

    // Symmetric ortho from sphere radius — stable size no matter how camera rotates
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

    // Texel snapping on X/Y to eliminate sub-pixel shimmer
    float worldUnitsPerTexel = (2.f * radius) / (float)width_;
    minX = std::floor(minX / worldUnitsPerTexel) * worldUnitsPerTexel;
    maxX = std::floor(maxX / worldUnitsPerTexel) * worldUnitsPerTexel;
    minY = std::floor(minY / worldUnitsPerTexel) * worldUnitsPerTexel;
    maxY = std::floor(maxY / worldUnitsPerTexel) * worldUnitsPerTexel;

    glm::mat4 lightProj = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
    return lightProj * lightView;
}

// ============================================================
//  Per-frame update
// ============================================================
void CascadeShadowMap::update(const Camera &cam)
{
    computeSplits(cam.nearPlane, cam.farPlane);

    float prevSplit = cam.nearPlane;
    for (int i = 0; i < CSM_NUM_CASCADES; ++i)
    {
        lightSpaceMatrices[i] = computeLightSpaceMatrix(cam, prevSplit, cascadeSplits[i]);
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
void CascadeShadowMap::bindToShader(Shader *shader, int baseTextureUnit) const
{
    if (!shader)
        return;

    auto& rs = RenderState::instance();
    for (int i = 0; i < CSM_NUM_CASCADES; ++i)
    {
        rs.bindTexture(baseTextureUnit + i, GL_TEXTURE_2D, textures_[i]);
        shader->setInt("u_shadowMap[" + std::to_string(i) + "]", baseTextureUnit + i);
        shader->setMat4("u_lightSpace[" + std::to_string(i) + "]",
                        lightSpaceMatrices[i]);
        shader->setFloat("u_cascadeSplits[" + std::to_string(i) + "]",
                         cascadeSplits[i]);
    }
    shader->setVec2("u_shadowMapSize",
                    glm::vec2((float)width_, (float)height_));
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

    // Use shadow queue (gathered without camera frustum culling)
    // so casters outside the camera view still cast shadows.
    for (const auto &item : ctx.shadowQueue.getOpaque())
    {
        if (!item.drawable)
            continue;
        shader->setMat4("u_model", item.model);
        if (item.indexCount > 0)
            item.drawable->drawRange(item.indexStart, item.indexCount);
        else
            item.drawable->draw();
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

    // Update cascade matrices each frame from the current camera
    csm_->update(*ctx.camera);

    // Execute each cascade depth pass
    for (RenderPass *pass : passes)
    {
        if (auto *dp = dynamic_cast<CsmDepthPass *>(pass))
            dp->execute(ctx, queue);
    }

    // Upload CSM uniforms to litShader before the opaque lit pass
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
    {
        if (!dynamic_cast<CsmDepthPass *>(pass))
            pass->execute(ctx, queue);
    }
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
