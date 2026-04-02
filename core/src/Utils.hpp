#pragma once

 

void LogError(const char *msg, ...);
void LogInfo(const char *msg, ...);
void LogWarning(const char *msg, ...);
 

#define ENABLE_GL_CHECK 1
#if ENABLE_GL_CHECK
#define CHECK_GL_ERROR(glFunc)                                        \
    do                                                                \
    {                                                                 \
        glFunc;                                                       \
        GLenum errorCode;                                             \
        while ((errorCode = glGetError()) != GL_NO_ERROR)             \
        {                                                             \
            const char *errorString = "UNKNOWN_ERROR";                \
            switch (errorCode)                                        \
            {                                                         \
            case GL_INVALID_ENUM:                                     \
                errorString = "GL_INVALID_ENUM";                      \
                break;                                                \
            case GL_INVALID_VALUE:                                    \
                errorString = "GL_INVALID_VALUE";                     \
                break;                                                \
            case GL_INVALID_OPERATION:                                \
                errorString = "GL_INVALID_OPERATION";                 \
                break;                                                \
            case GL_INVALID_FRAMEBUFFER_OPERATION:                    \
                errorString = "GL_INVALID_FRAMEBUFFER_OPERATION";     \
                break;                                                \
            case GL_OUT_OF_MEMORY:                                    \
                errorString = "GL_OUT_OF_MEMORY";                     \
                break;                                                \
            case GL_STACK_OVERFLOW:                                   \
                errorString = "GL_STACK_OVERFLOW";                    \
                break;                                                \
            case GL_STACK_UNDERFLOW:                                  \
                errorString = "GL_STACK_UNDERFLOW";                   \
                break;                                                \
            }                                                         \
            LogError("OpenGL Error: 0x%04X (%s) in %s at line %d\n",  \
                     errorCode, errorString, __FUNCTION__, __LINE__); \
        }                                                             \
    } while (0)
#else
#define CHECK_GL_ERROR(glFunc) \
    do                         \
    {                          \
        glFunc;                \
    } while (0)
#endif
