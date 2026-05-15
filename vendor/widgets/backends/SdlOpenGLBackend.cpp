#include "SdlOpenGLBackend.hpp"

#include <Opengl.hpp>
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace {

double nowSeconds()
{
    return static_cast<double>(SDL_GetTicks()) / 1000.0;
}

GLuint compileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "BuGUI demo shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

} // namespace

bool SdlOpenGLBackend::init(const char* title, int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
        return false;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow(title,
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          width,
                                          height,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window)
        return false;

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context)
    {
        SDL_DestroyWindow(window);
        return false;
    }

    SDL_GL_SetSwapInterval(1);

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    if (!gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        return false;
    }
#endif

    window_ = window;
    glContext_ = context;
    previousTime_ = nowSeconds();

    if (!createDeviceObjects())
    {
        shutdown();
        return false;
    }

    BuGUI::SetCurrentContext(BuGUI::CreateContext());
    updateDisplaySize();

    // Wire up clipboard callbacks
    auto& io = BuGUI::GetIO();
    io.setClipboardText = [](const char* text) {
        SDL_SetClipboardText(text);
    };
    io.getClipboardText = []() -> std::string {
        char* clip = SDL_GetClipboardText();
        std::string result = clip ? clip : "";
        SDL_free(clip);
        return result;
    };

    // Wire up file I/O callbacks
    io.readFile = [](const std::string& path) -> std::string {
        SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
        if (!rw) return "";
        Sint64 size = SDL_RWsize(rw);
        if (size <= 0) { SDL_RWclose(rw); return ""; }
        std::string buf(static_cast<size_t>(size), '\0');
        SDL_RWread(rw, &buf[0], 1, buf.size());
        SDL_RWclose(rw);
        return buf;
    };
    io.writeFile = [](const std::string& path, const std::string& data) -> bool {
        SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "wb");
        if (!rw) return false;
        size_t written = SDL_RWwrite(rw, data.c_str(), 1, data.size());
        SDL_RWclose(rw);
        return written == data.size();
    };

    return true;
}

bool SdlOpenGLBackend::beginFrame()
{
    if (shouldClose_)
        return false;

    double t = nowSeconds();
    auto& io = BuGUI::GetIO();
    io.deltaTime = static_cast<float>(t - previousTime_);
    previousTime_ = t;

    // Apply cursor requested last frame
    applyCursor(io.wantedCursor);

    // Clear previous frame's transient IO data BEFORE filling new events
    BuGUI::NewFrame();

    processEvents();
    updateDisplaySize();
    return !shouldClose_;
}

void SdlOpenGLBackend::render(const BuGUI::DrawData& drawData)
{
    SDL_Window* window = static_cast<SDL_Window*>(window_);
    int drawableW = 0;
    int drawableH = 0;
    SDL_GL_GetDrawableSize(window, &drawableW, &drawableH);

    glViewport(0, 0, drawableW, drawableH);
    glClearColor(clearColor_.r / 255.0f,
                 clearColor_.g / 255.0f,
                 clearColor_.b / 255.0f,
                 clearColor_.a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    renderDrawData(drawData);
}

void SdlOpenGLBackend::present()
{
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(window_));
}

BuGUI::TextureHandle SdlOpenGLBackend::createTextureRGBA(int width, int height, const unsigned char* pixels)
{
    if (width <= 0 || height <= 0 || !pixels)
        return {};

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 width,
                 height,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    return BuGUI::TextureHandle{static_cast<uintptr_t>(texture)};
}

void SdlOpenGLBackend::destroyTexture(BuGUI::TextureHandle texture)
{
    if (!texture)
        return;

    GLuint id = static_cast<GLuint>(texture.value);
    glDeleteTextures(1, &id);
}

void SdlOpenGLBackend::shutdown()
{
    BuGUI::DestroyContext(BuGUI::GetCurrentContext());
    destroyDeviceObjects();

    if (glContext_)
    {
        SDL_GL_DeleteContext(static_cast<SDL_GLContext>(glContext_));
        glContext_ = nullptr;
    }

    if (window_)
    {
        SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
        window_ = nullptr;
    }

    for (auto& c : sdlCursors_)
        if (c) { SDL_FreeCursor(static_cast<SDL_Cursor*>(c)); c = nullptr; }

    SDL_Quit();
}

void SdlOpenGLBackend::setBackgroundColor(const Color& color)
{
    clearColor_ = color;
}

bool SdlOpenGLBackend::createDeviceObjects()
{
    const char* vs = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec4 aColor;
        layout(location = 2) in vec2 aUV;
        uniform vec2 uDisplaySize;
        uniform mat3 uCamera;
        out vec4 vColor;
        out vec2 vUV;
        void main()
        {
            vec2 pos = (uCamera * vec3(aPos, 1.0)).xy;
            vec2 p   = (pos / uDisplaySize) * 2.0 - 1.0;
            gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
            vColor = aColor;
            vUV = aUV;
        }
    )";

    const char* fs = R"(
        #version 330 core
        in vec4 vColor;
        in vec2 vUV;
        uniform sampler2D uTexture;
        uniform int uUseTexture;
        out vec4 FragColor;
        void main()
        {
            vec4 texel = uUseTexture != 0 ? texture(uTexture, vUV) : vec4(1.0);
            FragColor = vColor * texel;
        }
    )";

    GLuint vert = compileShader(GL_VERTEX_SHADER, vs);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!vert || !frag)
        return false;

    shader_ = glCreateProgram();
    glAttachShader(shader_, vert);
    glAttachShader(shader_, frag);
    glLinkProgram(shader_);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok = 0;
    glGetProgramiv(shader_, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = {};
        glGetProgramInfoLog(shader_, sizeof(log), nullptr, log);
        std::fprintf(stderr, "BuGUI demo shader link failed: %s\n", log);
        destroyDeviceObjects();
        return false;
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(BuGUI::DrawVertex),
                          reinterpret_cast<void*>(offsetof(BuGUI::DrawVertex, x)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(BuGUI::DrawVertex),
                          reinterpret_cast<void*>(offsetof(BuGUI::DrawVertex, color)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                          sizeof(BuGUI::DrawVertex),
                          reinterpret_cast<void*>(offsetof(BuGUI::DrawVertex, u)));

    glBindVertexArray(0);
    return true;
}

void SdlOpenGLBackend::destroyDeviceObjects()
{
    if (ebo_)
    {
        GLuint id = ebo_;
        glDeleteBuffers(1, &id);
        ebo_ = 0;
    }

    if (vbo_)
    {
        GLuint id = vbo_;
        glDeleteBuffers(1, &id);
        vbo_ = 0;
    }

    if (vao_)
    {
        GLuint id = vao_;
        glDeleteVertexArrays(1, &id);
        vao_ = 0;
    }

    if (shader_)
    {
        glDeleteProgram(shader_);
        shader_ = 0;
    }
}

void SdlOpenGLBackend::processEvents()
{
    auto& io = BuGUI::GetIO();

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            shouldClose_ = true;
            break;

        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                updateDisplaySize();
            break;

        case SDL_MOUSEMOTION:
            io.mouseX = static_cast<float>(event.motion.x);
            io.mouseY = static_cast<float>(event.motion.y);
            break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        {
            int button = 0;
            if (event.button.button == SDL_BUTTON_RIGHT)
                button = 1;
            else if (event.button.button == SDL_BUTTON_MIDDLE)
                button = 2;
            io.mouseDown[button] = event.type == SDL_MOUSEBUTTONDOWN;
            break;
        }

        case SDL_MOUSEWHEEL:
        {
            float scale = (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -1.0f : 1.0f;
#if SDL_VERSION_ATLEAST(2, 0, 18)
            io.mouseWheelX += event.wheel.preciseX * scale;
            io.mouseWheelY += event.wheel.preciseY * scale;
#else
            io.mouseWheelX += static_cast<float>(event.wheel.x) * scale;
            io.mouseWheelY += static_cast<float>(event.wheel.y) * scale;
#endif
            break;
        }

        case SDL_KEYDOWN:
        case SDL_KEYUP:
        {
            // Use event-embedded mod state — more reliable than SDL_GetModState()
            SDL_Keymod mod = (SDL_Keymod)event.key.keysym.mod;
            io.addKeyEvent(
                event.key.keysym.sym,
                event.key.keysym.scancode,
                (mod & KMOD_SHIFT) != 0,
                (mod & KMOD_CTRL)  != 0,
                (mod & KMOD_ALT)   != 0,
                event.type == SDL_KEYDOWN);
            break;
        }

        case SDL_TEXTINPUT:
            for (const char* p = event.text.text; *p; ++p)
                io.addInputCharacter(static_cast<unsigned char>(*p));
            break;

        case SDL_DROPFILE:
        case SDL_DROPTEXT:
        {
            if (event.drop.file) {
                int imx, imy;
                SDL_GetMouseState(&imx, &imy);
                float fmx = static_cast<float>(imx);
                float fmy = static_cast<float>(imy);
                if (event.type == SDL_DROPFILE) {
                    io.addDropEvent(fmx, fmy, {std::string(event.drop.file)});
                } else {
                    io.addDropEvent(fmx, fmy, {}, std::string(event.drop.file));
                }
                SDL_free(event.drop.file);
            }
            break;
        }

        default:
            break;
        }
    }

    SDL_Keymod mod = SDL_GetModState();
    io.keyCtrl = (mod & KMOD_CTRL) != 0;
    io.keyShift = (mod & KMOD_SHIFT) != 0;
    io.keyAlt = (mod & KMOD_ALT) != 0;
}

void SdlOpenGLBackend::updateDisplaySize()
{
    if (!window_)
        return;

    SDL_Window* window = static_cast<SDL_Window*>(window_);
    int windowW = 0;
    int windowH = 0;
    int drawableW = 0;
    int drawableH = 0;
    SDL_GetWindowSize(window, &windowW, &windowH);
    SDL_GL_GetDrawableSize(window, &drawableW, &drawableH);

    auto& io = BuGUI::GetIO();
    io.displayWidth = static_cast<float>(windowW);
    io.displayHeight = static_cast<float>(windowH);
    io.framebufferScaleX = windowW > 0 ? static_cast<float>(drawableW) / static_cast<float>(windowW) : 1.0f;
    io.framebufferScaleY = windowH > 0 ? static_cast<float>(drawableH) / static_cast<float>(windowH) : 1.0f;
}

static std::array<float, 9> cameraToMat3(const BuGUI::Camera2D& cam,
                                           float displayW, float displayH)
{
    float pivX = cam.pivotX * displayW;
    float pivY = cam.pivotY * displayH;
    float c    = std::cos(cam.angle) * cam.scale;
    float s    = std::sin(cam.angle) * cam.scale;
    float ox   = cam.x - pivX;
    float oy   = cam.y - pivY;
    float tx   = c * ox - s * oy + pivX;
    float ty   = s * ox + c * oy + pivY;
    return { c, s, 0.0f,
            -s, c, 0.0f,
            tx, ty, 1.0f };
}

void SdlOpenGLBackend::renderDrawData(const BuGUI::DrawData& drawData)
{
    if (drawData.passes.empty())
        return;
    if (drawData.displayWidth <= 0.0f || drawData.displayHeight <= 0.0f)
        return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_SCISSOR_TEST);

    glUseProgram(shader_);
    GLint displayLoc    = glGetUniformLocation(shader_, "uDisplaySize");
    GLint cameraLoc     = glGetUniformLocation(shader_, "uCamera");
    GLint useTextureLoc = glGetUniformLocation(shader_, "uUseTexture");
    GLint textureLoc    = glGetUniformLocation(shader_, "uTexture");
    glUniform2f(displayLoc, drawData.displayWidth, drawData.displayHeight);
    glUniform1i(textureLoc, 0);

    glBindVertexArray(vao_);

    for (const BuGUI::DrawPass& pass : drawData.passes)
    {
        if (!pass.list) continue;
        const auto& vertices = pass.list->vertices();
        const auto& indices  = pass.list->indices();
        const auto& commands = pass.list->commands();
        if (vertices.empty() || indices.empty() || commands.empty()) continue;

        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices.size() * sizeof(BuGUI::DrawVertex)),
                     vertices.data(), GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                     indices.data(), GL_STREAM_DRAW);

        auto mat = cameraToMat3(pass.camera, drawData.displayWidth, drawData.displayHeight);
        glUniformMatrix3fv(cameraLoc, 1, GL_FALSE, mat.data());

        const float camAngle  = pass.camera.angle;
        const float camScale  = pass.camera.scale;
        const float pivX      = pass.camera.pivotX * drawData.displayWidth;
        const float pivY      = pass.camera.pivotY * drawData.displayHeight;
        const float c         = std::cos(camAngle) * camScale;
        const float ss        = std::sin(camAngle) * camScale;
        const float tx        = c * (pass.camera.x - pivX) - ss * (pass.camera.y - pivY) + pivX;
        const float ty        = ss * (pass.camera.x - pivX) +  c * (pass.camera.y - pivY) + pivY;
        const bool  noRotation = (camAngle == 0.0f);

        for (const BuGUI::DrawCmd& cmd : commands)
        {
            float scaleX = drawData.framebufferScaleX;
            float scaleY = drawData.framebufferScaleY;
            float fbW = drawData.displayWidth  * scaleX;
            float fbH = drawData.displayHeight * scaleY;

            float cx0, cy0, cx1, cy1;
            if (noRotation)
            {
                cx0 = c * cmd.clip.x                + tx;
                cy0 = c * cmd.clip.y                + ty;
                cx1 = c * (cmd.clip.x + cmd.clip.w) + tx;
                cy1 = c * (cmd.clip.y + cmd.clip.h) + ty;
                if (cx0 > cx1) std::swap(cx0, cx1);
                if (cy0 > cy1) std::swap(cy0, cy1);
            }
            else
            {
                cx0 = 0.0f; cy0 = 0.0f;
                cx1 = drawData.displayWidth;
                cy1 = drawData.displayHeight;
            }

            float x0 = std::max(0.0f, cx0 * scaleX);
            float y0 = std::max(0.0f, (drawData.displayHeight - cy1) * scaleY);
            float x1 = std::min(fbW,  cx1 * scaleX);
            float y1 = std::min(fbH,  (drawData.displayHeight - cy0) * scaleY);

            int clipX = static_cast<int>(x0);
            int clipY = static_cast<int>(y0);
            int clipW = static_cast<int>(x1 - x0);
            int clipH = static_cast<int>(y1 - y0);

            if (clipW <= 0 || clipH <= 0)
                continue;

            glScissor(clipX, clipY, clipW, clipH);
            if (cmd.texture)
            {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(cmd.texture.value));
                glUniform1i(useTextureLoc, 1);
            }
            else
            {
                glBindTexture(GL_TEXTURE_2D, 0);
                glUniform1i(useTextureLoc, 0);
            }

            glDrawElements(GL_TRIANGLES,
                           static_cast<GLsizei>(cmd.indexCount),
                           GL_UNSIGNED_INT,
                           reinterpret_cast<void*>(static_cast<uintptr_t>(cmd.indexOffset * sizeof(uint32_t))));
        } // commands
    } // passes

    glDisable(GL_SCISSOR_TEST);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);
}

void SdlOpenGLBackend::applyCursor(int cursorType)
{
    if (cursorType == activeCursor_)
        return;
    activeCursor_ = cursorType;

    // Lazy-create system cursors (indexed by CursorType enum order)
    static const SDL_SystemCursor sdlMap[] = {
        SDL_SYSTEM_CURSOR_ARROW,     // 0 Arrow
        SDL_SYSTEM_CURSOR_IBEAM,     // 1 IBeam
        SDL_SYSTEM_CURSOR_CROSSHAIR, // 2 Crosshair
        SDL_SYSTEM_CURSOR_HAND,      // 3 Hand
        SDL_SYSTEM_CURSOR_SIZEWE,    // 4 ResizeEW
        SDL_SYSTEM_CURSOR_SIZENS,    // 5 ResizeNS
        SDL_SYSTEM_CURSOR_SIZENESW,  // 6 ResizeNESW
        SDL_SYSTEM_CURSOR_SIZENWSE,  // 7 ResizeNWSE
    };
    constexpr int kN = static_cast<int>(sizeof(sdlMap) / sizeof(sdlMap[0]));
    int idx = (cursorType >= 0 && cursorType < kN) ? cursorType : 0;

    if (!sdlCursors_[idx])
        sdlCursors_[idx] = SDL_CreateSystemCursor(sdlMap[idx]);

    if (sdlCursors_[idx])
        SDL_SetCursor(static_cast<SDL_Cursor*>(sdlCursors_[idx]));
}
