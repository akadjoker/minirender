#pragma once

#include "glad/glad.h"

class RenderState
{
public:
    static RenderState &instance();

    RenderState(const RenderState &) = delete;
    RenderState &operator=(const RenderState &) = delete;

    void resetCache();
    void shutdown();

    void setDepthTest(bool enabled);
    void setDepthWrite(bool enabled);
    void setCull(bool enabled);
    void setCullFace(GLenum mode);
    void setBlend(bool enabled);
    void setBlendFunc(GLenum src, GLenum dst);
    void setScissorTest(bool enabled);
    void setScissor(GLint x, GLint y, GLsizei width, GLsizei height);

    void useProgram(GLuint program);
    void bindTexture(GLuint unit, GLenum target, GLuint texture);

    void setViewport(GLint x, GLint y, GLsizei width, GLsizei height);
    void setClearColor(float r, float g, float b, float a);
    void clear(bool color, bool depth, bool stencil = false);

private:
    RenderState() = default;
    ~RenderState();

    bool depthTestKnown_ = false;
    bool depthTestEnabled_ = false;

    bool depthWriteKnown_ = false;
    bool depthWriteEnabled_ = true;

    bool cullKnown_ = false;
    bool cullEnabled_ = false;

    bool cullFaceKnown_ = false;
    GLenum cullFaceMode_ = GL_BACK;

    bool blendKnown_ = false;
    bool blendEnabled_ = false;

    bool blendFuncKnown_ = false;
    GLenum blendSrc_ = GL_ONE;
    GLenum blendDst_ = GL_ZERO;

    bool scissorTestKnown_ = false;
    bool scissorTestEnabled_ = false;
    bool scissorBoxKnown_ = false;
    GLint scissorX_ = 0;
    GLint scissorY_ = 0;
    GLsizei scissorWidth_ = 0;
    GLsizei scissorHeight_ = 0;

    bool programKnown_ = false;
    GLuint currentProgram_ = 0;

    bool activeTextureKnown_ = false;
    GLuint activeTextureUnit_ = 0;

    bool viewportKnown_ = false;
    GLint viewportX_ = 0;
    GLint viewportY_ = 0;
    GLsizei viewportWidth_ = 0;
    GLsizei viewportHeight_ = 0;

    GLuint boundTextures_[16] = {};  // textura bound em cada unit 0..15
    GLenum boundTargets_[16]  = {};  // target (GL_TEXTURE_2D, etc)

    bool clearColorKnown_ = false;
    GLfloat clearR_ = 0.0f;
    GLfloat clearG_ = 0.0f;
    GLfloat clearB_ = 0.0f;
    GLfloat clearA_ = 1.0f;
};
