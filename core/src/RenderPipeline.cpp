#include "RenderPipeline.hpp"
#include "RenderState.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <string>

// ─── RenderPass::execute ──────────────────────────────────────────────────────────────────
void RenderPass::execute(const FrameContext &ctx, RenderQueue &queue) const
{
    if (!ctx.camera)
        return;

    auto &rs = RenderState::instance();
    rs.setViewport(ctx.viewport.x, ctx.viewport.y, ctx.viewport.z, ctx.viewport.w);

    // Scissor — only enabled when the pass opts in
    if (scissorTest)
    {
        const bool hasCustomRect = (scissorRect.z > 0 && scissorRect.w > 0);
        const glm::ivec4 &sr = hasCustomRect ? scissorRect : ctx.viewport;
        rs.setScissorTest(true);
        rs.setScissor(sr.x, sr.y, sr.z, sr.w);
    }
    else
    {
        rs.setScissorTest(false);
    }

    rs.setDepthTest(depthTest);
    rs.setDepthWrite(depthWrite);
    rs.setCull(cull);
    rs.setBlend(blend);
    if (blend)
        rs.setBlendFunc(blendSrc, blendDst);
    if (clearColor)
        rs.setClearColor(clearValue.r, clearValue.g, clearValue.b, clearValue.a);
    rs.clear(clearColor, clearDepth);

    std::vector<RenderItem> *items = nullptr;
    if (passMask & RenderPassMask::Opaque)
        items = &queue.getOpaque();
    else if (passMask & RenderPassMask::Transparent)
        items = &queue.getTransparent();
    if (!items || items->empty())
        return;

    if (sortMode != RenderSortMode::None)
    {
        const glm::vec3 camPos = ctx.camera->position;
        for (auto &item : *items)
            item.depth = glm::distance(glm::vec3(item.model[3]), camPos);
    }
    if (sortMode == RenderSortMode::FrontToBack)
        std::sort(items->begin(), items->end(), [](const RenderItem &a, const RenderItem &b)
                  {
            if (a.material != b.material) return a.material < b.material;
            if (a.drawable     != b.drawable)     return a.drawable     < b.drawable;
            return a.depth < b.depth; });
    else if (sortMode == RenderSortMode::BackToFront)
        std::sort(items->begin(), items->end(), [](const RenderItem &a, const RenderItem &b)
                  { return a.depth > b.depth; });

    const glm::mat4 &view     = ctx.camera->view;
    const glm::mat4 &proj     = ctx.camera->projection;
    const glm::mat4 &viewProj = ctx.camera->viewProjection;
    const glm::vec4  camPos   = glm::vec4(ctx.camera->position, 1.f);

    if (ctx.stats)
        ctx.stats->objects += static_cast<uint32_t>(items->size());

    if (shader)
    {
        // Pass has its own shader — bind once, draw all items (shadow / depth / outline)
        rs.useProgram(shader->getId());
        shader->setMat4("u_view",     view);
        shader->setMat4("u_proj",     proj);
        shader->setMat4("u_viewProj", viewProj);
        shader->setVec4("u_cameraPos", camPos);
        shader->setVec4("u_clipPlane", ctx.clipPlane);
        for (const auto &item : *items)
            drawItem(ctx, item, shader);
    }
    else
    {
        // Defer to each material's shader — batch by shader to minimise program switches
        Shader         *lastShader   = nullptr;
        const Material *lastMaterial = nullptr;
        for (const auto &item : *items)
        {
            if (!item.material)
                continue;
            Shader *sh = item.material->getShader();
            if (!sh)
                continue;
            if (sh != lastShader)
            {
                rs.useProgram(sh->getId());
                sh->setMat4("u_view",     view);
                sh->setMat4("u_proj",     proj);
                sh->setMat4("u_viewProj", viewProj);
                sh->setVec4("u_cameraPos", camPos);
                sh->setVec4("u_clipPlane", ctx.clipPlane);
                lastShader   = sh;
                lastMaterial = nullptr; // force re-bind on shader change
                if (ctx.stats) ctx.stats->shaderChanges++;
            }
            if (item.material != lastMaterial)
            {
                item.material->applyStates();
                int txCount = item.material->bindTexturesTo(sh);
                item.material->applyUniformsTo(sh);
                lastMaterial = item.material;
                if (ctx.stats)
                {
                    ctx.stats->materialChanges++;
                    ctx.stats->textureBinds += static_cast<uint32_t>(txCount);
                }
            }
            drawItemNoMaterial(ctx, item, sh);
        }
    }
}

// drawItemNoMaterial: material state already applied — only bones + model matrix + draw
void RenderPass::drawItemNoMaterial(const FrameContext &ctx, const RenderItem &item, Shader *sh) const
{
    if (!item.drawable || !sh)
        return;
    item.drawable->applyBoneMatrices(sh);
    sh->setMat4("u_model", item.model);

    const uint32_t idxCount = item.indexCount > 0
                                  ? item.indexCount
                                  : static_cast<uint32_t>(item.drawable->indexCount());
    if (ctx.stats)
    {
        ctx.stats->drawCalls++;
        ctx.stats->triangles += idxCount / 3;
        ctx.stats->vertices += static_cast<uint32_t>(item.drawable->vertexCount());
    }
    if (item.indexCount > 0)
        item.drawable->drawRange(item.indexStart, item.indexCount);
    else
        item.drawable->draw();
}

void RenderPass::drawItem(const FrameContext &ctx, const RenderItem &item, Shader *sh) const
{
    if (!item.drawable || !item.material || !sh)
        return;

    item.material->applyStates();
    item.material->bindTexturesTo(sh);
    item.material->applyUniformsTo(sh);
    item.drawable->applyBoneMatrices(sh);
    sh->setMat4("u_model", item.model);

    const uint32_t idxCount = item.indexCount > 0
                                  ? item.indexCount
                                  : static_cast<uint32_t>(item.drawable->indexCount());

    if (ctx.stats)
    {
        ctx.stats->drawCalls++;
        ctx.stats->triangles += idxCount / 3;
        ctx.stats->vertices += static_cast<uint32_t>(item.drawable->vertexCount());
    }

    if (item.indexCount > 0)
        item.drawable->drawRange(item.indexStart, item.indexCount);
    else
        item.drawable->draw();
}

// ─── Concrete passes ──────────────────────────────────────────────────────────────────
OpaquePass::OpaquePass()
{
    clearColor = true;
    clearDepth = true;
    clearValue = {0.15f, 0.15f, 0.2f, 1.0f};
    depthTest = true;
    depthWrite = true;
    cull = true;
    blend = false;
    passMask = RenderPassMask::Opaque;
    sortMode = RenderSortMode::FrontToBack;
}

TransparentPass::TransparentPass()
{
    clearColor = false;
    clearDepth = false;
    depthTest = true;
    depthWrite = false;
    cull = false;
    blend = true;
    passMask = RenderPassMask::Transparent;
    sortMode = RenderSortMode::BackToFront;
}

// ─── SkyPass ───────────────────────────────────────────────────────────────────────
void SkyPass::execute(const FrameContext &ctx, RenderQueue &) const
{
    if (!shader) return;

    auto &rs = RenderState::instance();
    rs.setDepthTest(true);
    rs.setDepthWrite(false);
    rs.setCull(false);
    rs.setBlend(false);
    // LEQUAL: sky fragment at depth=1.0 passes for pixels with no geometry
    glDepthFunc(GL_LEQUAL);

    if (dummyVao_ == 0)
        glGenVertexArrays(1, &dummyVao_);
    glBindVertexArray(dummyVao_);

    rs.useProgram(shader->getId());
    shader->setVec3("u_skyTop",      skyTop);
    shader->setVec3("u_skyHorizon",  skyHorizon);
    shader->setVec3("u_groundColor", groundColor);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glDepthFunc(GL_LESS);   // restore default
    rs.setCull(true);
    rs.setDepthWrite(true);
}

// ─── Technique ──────────────────────────────────────────────────────────────────
void Technique::render(const FrameContext &ctx, RenderQueue &queue) const
{
    for (auto *p : passes)
        p->execute(ctx, queue);
}

// ─── ForwardTechnique ──────────────────────────────────────────────────────────────────
ForwardTechnique::ForwardTechnique()
{
    name = "Forward";
    opaque_ = addPass<OpaquePass>();
    transparent_ = addPass<TransparentPass>();
}

// ─── GBufferPass ──────────────────────────────────────────────────────────────────────
GBufferPass::GBufferPass()
{
    clearColor = true;
    clearDepth = true;
    clearValue = {0.f, 0.f, 0.f, 1.f};
    depthTest  = true;
    depthWrite = true;
    cull       = true;
    blend      = false;
    passMask   = RenderPassMask::Opaque;
    sortMode   = RenderSortMode::FrontToBack;
}

void GBufferPass::execute(const FrameContext &ctx, RenderQueue &queue) const
{
    if (!gbuffer || !gbuffer->valid() || !shader || !ctx.camera)
        return;

    gbuffer->bind();

    auto &rs = RenderState::instance();
    rs.setDepthTest(true);
    rs.setDepthWrite(true);
    rs.setCull(true);
    rs.setBlend(false);
    rs.setClearColor(clearValue.r, clearValue.g, clearValue.b, clearValue.a);
    rs.clear(true, true);

    rs.useProgram(shader->getId());
    shader->setMat4("u_view", ctx.camera->view);
    shader->setMat4("u_proj", ctx.camera->projection);

    auto &items = queue.getOpaque();
    for (const auto &item : items)
        drawItem(ctx, item, shader);

    gbuffer->unbind();
}

// ─── DeferredLightingPass ─────────────────────────────────────────────────────────────
DeferredLightingPass::DeferredLightingPass()
{
    clearColor = true;
    clearDepth = false;
    clearValue = {0.f, 0.f, 0.f, 1.f};
    depthTest  = false;
    depthWrite = false;
    cull       = false;
    blend      = false;

    // Fullscreen triangle needs a bound VAO even though it uses no attributes
    glGenVertexArrays(1, &dummyVAO_);
}

DeferredLightingPass::~DeferredLightingPass()
{
    if (dummyVAO_) { glDeleteVertexArrays(1, &dummyVAO_); dummyVAO_ = 0; }
}

void DeferredLightingPass::execute(const FrameContext &ctx, RenderQueue &/*queue*/) const
{
    if (!gbuffer || !shader || !ctx.camera)
        return;

    auto &rs = RenderState::instance();
    rs.setViewport(ctx.viewport.x, ctx.viewport.y, ctx.viewport.z, ctx.viewport.w);
    rs.setDepthTest(false);
    rs.setDepthWrite(false);
    rs.setCull(false);
    rs.setBlend(false);
    rs.setClearColor(clearValue.r, clearValue.g, clearValue.b, clearValue.a);
    rs.clear(true, false);

    // Bind GBuffer textures to units 0-2
    rs.bindTexture(0, GL_TEXTURE_2D, gbuffer->gPosition);
    rs.bindTexture(1, GL_TEXTURE_2D, gbuffer->gNormal);
    rs.bindTexture(2, GL_TEXTURE_2D, gbuffer->gAlbedoSpec);

    rs.useProgram(shader->getId());
    shader->setInt("gPosition",   0);
    shader->setInt("gNormal",     1);
    shader->setInt("gAlbedoSpec", 2);

    shader->setVec3("u_viewPos", ctx.camera->position);

    // Directional light
    shader->setVec3("u_dirLightDir",   glm::normalize(dirLightDir));
    shader->setVec3("u_dirLightColor", dirLightColor);

    // Point lights from scene
    int numPt = 0;
    for (const auto *light : ctx.lights)
    {
        if (light->lightType != LightType::Point) continue;
        const auto *pt  = static_cast<const PointLight *>(light);
        std::string idx = "[" + std::to_string(numPt) + "]";
        shader->setVec3 ("u_pointPos"    + idx, pt->worldPosition());
        shader->setVec3 ("u_pointColor"  + idx, pt->color * pt->intensity);
        shader->setFloat("u_pointRadius" + idx, pt->range);
        if (++numPt >= 16) break;
    }
    shader->setInt("u_numPointLights", numPt);

    // Draw fullscreen triangle — no vertex data, positions from gl_VertexID
    glBindVertexArray(dummyVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

// ─── DeferredTechnique ────────────────────────────────────────────────────────────────
DeferredTechnique::DeferredTechnique()
{
    name = "Deferred";

    gbuffer_ = new GBuffer();
    if (!gbuffer_->initialize(1024, 768))
    {
        delete gbuffer_;
        gbuffer_ = nullptr;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[DeferredTechnique] GBuffer init failed");
        return;
    }

    auto *gp = addPass<GBufferPass>();
    gp->gbuffer = gbuffer_;

    auto *lp = addPass<DeferredLightingPass>();
    lp->gbuffer = gbuffer_;

    addPass<TransparentPass>();
}

DeferredTechnique::~DeferredTechnique()
{
    delete gbuffer_;
}

void DeferredTechnique::onResize(unsigned int w, unsigned int h)
{
    if (gbuffer_) gbuffer_->resize(w, h);
}

void DeferredTechnique::render(const FrameContext &ctx, RenderQueue &queue) const
{
    if (passes.size() < 3) return;

    geometryPass() ->execute(ctx, queue);
    lightingPass() ->execute(ctx, queue);
    transparentPass()->execute(ctx, queue);
}

GBufferPass          *DeferredTechnique::geometryPass()    const { return passes.size() > 0 ? static_cast<GBufferPass*>          (passes[0]) : nullptr; }
DeferredLightingPass *DeferredTechnique::lightingPass()    const { return passes.size() > 1 ? static_cast<DeferredLightingPass*> (passes[1]) : nullptr; }
TransparentPass      *DeferredTechnique::transparentPass() const { return passes.size() > 2 ? static_cast<TransparentPass*>      (passes[2]) : nullptr; }
