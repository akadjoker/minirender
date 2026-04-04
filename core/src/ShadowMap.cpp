#include "Opengl.hpp"
#include "ShadowMap.hpp"
#include "RenderState.hpp"
#include "Material.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <cmath>
#include <SDL2/SDL.h>

// ============================================================
//  ShadowMap
// ============================================================
bool ShadowMap::initialize(unsigned int w, unsigned int h)
{
    width  = w;
    height = h;

    // Depth texture
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 (GLsizei)width, (GLsizei)height, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Clamp to border — fora do shadow map não há sombra
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float border[] = {1.f, 1.f, 1.f, 1.f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    // FBO
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[ShadowMap] FBO incomplete: 0x%x", status);
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "[ShadowMap] Created %ux%u depth FBO", width, height);
    return true;
}

void ShadowMap::bind()
{
    auto &rs = RenderState::instance();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    rs.setViewport(0, 0, (GLsizei)width, (GLsizei)height);
    rs.setScissorTest(false);
    rs.setDepthWrite(true);
    rs.clear(false, true);
}

void ShadowMap::unbind()
{
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ShadowMap::release()
{
    if (texture) { glDeleteTextures(1, &texture);      texture = 0; }
    if (fbo)     { glDeleteFramebuffers(1, &fbo);      fbo     = 0; }
}

// ============================================================
//  ShadowPass
// ============================================================
ShadowPass::ShadowPass()
{
    clearColor = false;
    clearDepth = true;
    depthTest  = true;
    depthWrite = true;
    cull       = true;
    blend      = false;
    passMask   = RenderPassMask::Opaque; // sombras só de opacos
    sortMode   = RenderSortMode::None;   // ordem não interessa para depth
}

void ShadowPass::execute(const FrameContext &ctx, RenderQueue &queue) const
{
    if (!shadowMap || !ctx.camera)
        return;

    auto &rs = RenderState::instance();

    // ── Calcular lightSpaceMatrix ajustado ao frustum da câmara ──
    glm::vec3 dir = glm::normalize(lightDir);
    // Up vector seguro quando a luz aponta quase verticalmente
    glm::vec3 up  = (std::fabs(dir.y) > 0.99f) ? glm::vec3(1.f, 0.f, 0.f)
                                                 : glm::vec3(0.f, 1.f, 0.f);

    // 1. Desprojectar os 8 cantos NDC do frustum da câmara para world space
    glm::mat4 invVP = glm::inverse(ctx.camera->getProjection() * ctx.camera->getView());
    static const glm::vec4 ndc[8] = {
        {-1,-1,-1,1},{1,-1,-1,1},{-1,1,-1,1},{1,1,-1,1},
        {-1,-1, 1,1},{1,-1, 1,1},{-1,1, 1,1},{1,1, 1,1}
    };
    glm::vec3 corners[8];
    glm::vec3 centroid(0.f);
    for (int i = 0; i < 8; i++) {
        glm::vec4 p = invVP * ndc[i];
        corners[i]  = glm::vec3(p) / p.w;
        centroid   += corners[i];
    }
    centroid /= 8.f;

    // 2. Light view: recua lightDist do centróide do frustum
    glm::mat4 lightV = glm::lookAt(centroid - dir * lightDist, centroid, up);

    // 3. AABB dos cantos do frustum em light space
    glm::vec3 lsMin( 1e9f), lsMax(-1e9f);
    for (auto &c : corners) {
        glm::vec3 ls = glm::vec3(lightV * glm::vec4(c, 1.f));
        lsMin        = glm::min(lsMin, ls);
        lsMax        = glm::max(lsMax, ls);
    }

    // 4. Ortho ajustado + pequeno padding XY para evitar artefactos de borda
    float xPad = (lsMax.x - lsMin.x) * 0.02f;
    float yPad = (lsMax.y - lsMin.y) * 0.02f;
    // Em right-handed eye space, objectos à frente têm Z negativo.
    // nearP = -lsMax.z (plano mais próximo), farP = -lsMin.z (plano mais afastado).
    // Extender farP por lightDist para apanhar shadow casters fora do view frustum.
    float nearP = std::max(nearPlane, -lsMax.z);
    float farP  = -lsMin.z + lightDist;
    glm::mat4 lightP = glm::ortho(lsMin.x - xPad, lsMax.x + xPad,
                                   lsMin.y - yPad, lsMax.y + yPad,
                                   nearP, farP);

    lightSpaceMatrix_  = lightP * lightV;
    lightSpaceMatrix   = lightSpaceMatrix_; // expõe para o OpaquePass

    // ── Renderizar depth ─────────────────────────────────────
    shadowMap->bind();

    rs.setDepthTest(true);
    rs.setDepthWrite(true);
    rs.setCull(true);
 
    rs.setBlend(false);

    if (!shader)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[ShadowPass] No depth shader set");
        shadowMap->unbind();
        return;
    }

    rs.useProgram(shader->getId());
    shader->setMat4("u_lightSpace", lightSpaceMatrix_);

    auto &items = queue.getOpaque();
    for (const auto &item : items)
    {
        if (!item.drawable) continue;
        shader->setMat4("u_model", item.model);

        if (item.indexCount > 0)
            item.drawable->drawRange(item.indexStart, item.indexCount);
        else
            item.drawable->draw();
    }

 
    shadowMap->unbind();
}

// ============================================================
//  ShadowTechnique
// ============================================================
ShadowTechnique::ShadowTechnique()
{
    name = "Forward+Shadow";

    shadowMap_ = new ShadowMap();
    if (!shadowMap_->initialize(2048, 2048))
    {
        delete shadowMap_;
        shadowMap_ = nullptr;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[ShadowTechnique] Failed to create shadow map");
        return;
    }

    auto *sp = addPass<ShadowPass>();
    sp->shadowMap = shadowMap_;

    addPass<OpaquePass>();
    addPass<TransparentPass>();
}

ShadowTechnique::~ShadowTechnique()
{
    delete shadowMap_;
}

void ShadowTechnique::render(const FrameContext &ctx, RenderQueue &queue) const
{
    if (passes.size() < 3) return;

    // 1. Shadow pass — preenche depth texture
    auto *sp = shadowPass();
    sp->execute(ctx, queue);

    // 2. Opaque pass — bind shadow map + lightSpaceMatrix ao shader
    auto *op = opaquePass();
    if (op && shadowMap_)
    {
        // Bind shadow map na unit 1 (unit 0 = diffuse)
        RenderState::instance().bindTexture(1, GL_TEXTURE_2D, shadowMap_->texture);
    }

    // Actualiza u_lightSpace no lit shader com a matriz recém-calculada
    if (litShader)
    {
        RenderState::instance().useProgram(litShader->getId());
        litShader->setMat4("u_lightSpace", sp->lightSpaceMatrix);
    }

    op->execute(ctx, queue);

    // 3. Transparent — sem sombras
    transparentPass()->execute(ctx, queue);
}

ShadowPass      *ShadowTechnique::shadowPass()      const { return passes.size() > 0 ? static_cast<ShadowPass*>(passes[0])      : nullptr; }
OpaquePass      *ShadowTechnique::opaquePass()      const { return passes.size() > 1 ? static_cast<OpaquePass*>(passes[1])      : nullptr; }
TransparentPass *ShadowTechnique::transparentPass() const { return passes.size() > 2 ? static_cast<TransparentPass*>(passes[2]) : nullptr; }
