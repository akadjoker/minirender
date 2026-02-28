#include <cstring>
#include "RenderState.hpp"

RenderState &RenderState::instance()
{
    static RenderState state;
    return state;
}

RenderState::~RenderState()
{
    shutdown();
}

void RenderState::resetCache()
{
    depthTestKnown_ = false;
    depthWriteKnown_ = false;
    cullKnown_ = false;
    cullFaceKnown_ = false;
    blendKnown_ = false;
    blendFuncKnown_ = false;
    scissorTestKnown_ = false;
    scissorBoxKnown_ = false;
    programKnown_ = false;
    activeTextureKnown_ = false;
    viewportKnown_ = false;
    clearColorKnown_ = false;
    memset(boundTextures_, 0, sizeof(boundTextures_));
    memset(boundTargets_,  0, sizeof(boundTargets_));
}

void RenderState::shutdown()
{
    resetCache();
}

void RenderState::setDepthTest(bool enabled)
{
    if (depthTestKnown_ && depthTestEnabled_ == enabled)
        return;

    depthTestKnown_ = true;
    depthTestEnabled_ = enabled;
    if (enabled)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
}

void RenderState::setDepthWrite(bool enabled)
{
    if (depthWriteKnown_ && depthWriteEnabled_ == enabled)
        return;

    depthWriteKnown_ = true;
    depthWriteEnabled_ = enabled;
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
}

void RenderState::setCull(bool enabled)
{
    if (cullKnown_ && cullEnabled_ == enabled)
        return;

    cullKnown_ = true;
    cullEnabled_ = enabled;
    if (enabled)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
}

void RenderState::setCullFace(GLenum mode)
{
    if (cullFaceKnown_ && cullFaceMode_ == mode)
        return;

    cullFaceKnown_ = true;
    cullFaceMode_ = mode;
    glCullFace(mode);
}

void RenderState::setBlend(bool enabled)
{
    if (blendKnown_ && blendEnabled_ == enabled)
        return;

    blendKnown_ = true;
    blendEnabled_ = enabled;
    if (enabled)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
}

void RenderState::setBlendFunc(GLenum src, GLenum dst)
{
    if (blendFuncKnown_ && blendSrc_ == src && blendDst_ == dst)
        return;

    blendFuncKnown_ = true;
    blendSrc_ = src;
    blendDst_ = dst;
    glBlendFunc(src, dst);
}

void RenderState::setScissorTest(bool enabled)
{
    if (scissorTestKnown_ && scissorTestEnabled_ == enabled)
        return;

    scissorTestKnown_ = true;
    scissorTestEnabled_ = enabled;
    if (enabled)
        glEnable(GL_SCISSOR_TEST);
    else
        glDisable(GL_SCISSOR_TEST);
}

void RenderState::setScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (scissorBoxKnown_ && scissorX_ == x && scissorY_ == y && scissorWidth_ == width && scissorHeight_ == height)
        return;

    scissorBoxKnown_ = true;
    scissorX_ = x;
    scissorY_ = y;
    scissorWidth_ = width;
    scissorHeight_ = height;
    glScissor(x, y, width, height);
}

void RenderState::useProgram(GLuint program)
{
    if (programKnown_ && currentProgram_ == program)
        return;

    programKnown_ = true;
    currentProgram_ = program;
    glUseProgram(program);
}

void RenderState::bindTexture(GLuint unit, GLenum target, GLuint texture)
{
    if (activeTextureUnit_ != unit || !activeTextureKnown_)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        activeTextureKnown_ = true;
        activeTextureUnit_ = unit;
    }

    if (boundTextures_[unit] == texture && boundTargets_[unit] == target)
        return; // já está bound, não faz nada

    boundTextures_[unit] = texture;
    boundTargets_[unit] = target;
    glBindTexture(target, texture);
}

void RenderState::setViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (viewportKnown_ &&
        viewportX_ == x &&
        viewportY_ == y &&
        viewportWidth_ == width &&
        viewportHeight_ == height)
    {
        return;
    }

    viewportKnown_ = true;
    viewportX_ = x;
    viewportY_ = y;
    viewportWidth_ = width;
    viewportHeight_ = height;
    glViewport(x, y, width, height);
}

void RenderState::setClearColor(float r, float g, float b, float a)
{
    if (clearColorKnown_ &&
        clearR_ == r &&
        clearG_ == g &&
        clearB_ == b &&
        clearA_ == a)
    {
        return;
    }

    clearColorKnown_ = true;
    clearR_ = r;
    clearG_ = g;
    clearB_ = b;
    clearA_ = a;
    glClearColor(r, g, b, a);
}

void RenderState::clear(bool color, bool depth, bool stencil)
{
    GLbitfield flags = 0;
    if (color)
        flags |= GL_COLOR_BUFFER_BIT;
    if (depth)
        flags |= GL_DEPTH_BUFFER_BIT;
    if (stencil)
        flags |= GL_STENCIL_BUFFER_BIT;

    if (flags != 0)
        glClear(flags);
}
