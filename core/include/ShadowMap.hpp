#pragma once
#include "glad/glad.h"
#include "RenderPipeline.hpp"
#include "Node.hpp"
#include <glm/glm.hpp>

// ============================================================
//  ShadowMap — FBO + depth texture para 1 luz direccional
// ============================================================
class ShadowMap
{
public:
    unsigned int width  = 2048;
    unsigned int height = 2048;

    GLuint fbo     = 0;
    GLuint texture = 0;  // depth texture — bind ao shader principal

    bool initialize(unsigned int w = 2048, unsigned int h = 2048);
    void release();

    void bind();     
    void unbind();   

    ~ShadowMap() { release(); }
};

// ============================================================
//  ShadowPass — renderiza a cena do ponto de vista da luz
//  Adicionar antes do OpaquePass na Technique
// ============================================================
class ShadowPass : public RenderPass
{
public:
    ShadowPass();

    // Luz direccional — define a direcção e distância da cena
    glm::vec3 lightDir    = glm::normalize(glm::vec3(-1.f, -2.f, -1.f));
    float     lightDist   = 200.f;  // distância da origem ao "olho" da luz
    float     orthoSize   = 100.f;  // metade do tamanho da caixa ortográfica
    float     nearPlane   = 1.f;
    float     farPlane    = 500.f;

    // Calculado em execute() — usado pelo OpaquePass
    
    mutable glm::mat4 lightSpaceMatrix = glm::mat4(1.f);

    ShadowMap *shadowMap = nullptr;   

    void execute(const FrameContext &ctx, RenderQueue &queue) const override;

private:
    // mutable para poder actualizar lightSpaceMatrix em execute() const
    mutable glm::mat4 lightSpaceMatrix_ = glm::mat4(1.f);
};

// ============================================================
//  ShadowTechnique — Forward + shadow map
//  Ordem: ShadowPass → OpaquePass → TransparentPass
// ============================================================
class ShadowTechnique : public Technique
{
public:
    ShadowTechnique();
    ~ShadowTechnique();

    void render(const FrameContext &ctx, RenderQueue &queue) const override;

    ShadowPass  *shadowPass()      const;
    OpaquePass  *opaquePass()      const;
    TransparentPass *transparentPass() const;

    // Acesso à shadow map para bind manual ao shader
    ShadowMap   *getShadowMap()    const { return shadowMap_; }

    // Shader de iluminação — render() define u_lightSpace após o shadow pass
    Shader *litShader = nullptr;

private:
    ShadowMap *shadowMap_ = nullptr;  // owned
};
