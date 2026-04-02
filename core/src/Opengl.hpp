#pragma once

// Detectar plataforma WEB (Emscripten)
#if defined(__EMSCRIPTEN__)
    #include <GLES3/gl3.h>
    #include <GLES3/gl2ext.h>

#elif defined(__ANDROID__)
    #include <GLES3/gl3.h>
    #include <GLES3/gl2ext.h>

// Desktop (Windows, Linux, macOS)
#else
    // No desktop usamos o GLAD para carregar as funções OpenGL modernas
    #include <glad/glad.h>
#endif
