#include "pch.h"
#include "Device.hpp"
#include "BuGUIRenderer.hpp"
#include <WidgetApp.hpp>
#include "Pixmap.hpp"
#include "Manager.hpp"
#include "RenderState.hpp"
#include "Input.hpp"
#define MSF_GIF_IMPL
#include "msf_gif.h"
// Platform OpenGL/GLES headers — resolved centrally
#include "Opengl.hpp"


#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <vector>

struct Device::GifRecordingState
{
    FILE* file = nullptr;
    MsfGifState gif = {};
    int width = 0;
    int height = 0;
    int fps = 12;
    int frameDelayCenti = 8;
    double captureInterval = 1.0 / 12.0;
    double accumulator = 0.0;
    int framesWritten = 0;
    std::string path;
    std::vector<uint8_t> pixels;
};

struct Device::FrameSequenceRecordingState
{
    int width = 0;
    int height = 0;
    int fps = 30;
    double captureInterval = 1.0 / 30.0;
    double accumulator = 0.0;
    int framesWritten = 0;
    std::string directory;
    std::string extension;
    std::vector<uint8_t> pixels;
};

double GetTime() { return static_cast<double>(SDL_GetTicks()) / 1000; }

//*************************************************************************************************
// Device
//*************************************************************************************************

Device &Device::Instance()
{
    static Device instance;
    return instance;
}
Device *Device::InstancePtr() { return &Instance(); }

Device::Device() : m_width(0), m_height(0)
{
    SDL_Log("[DEVICE] Initialized.");

    m_shouldclose = false;
    m_window = NULL;
    m_context = NULL;
    m_current = 0;
    m_previous = 0;
    m_update = 0;
    m_draw = 0;
    m_frame = 0;
    m_target = 0;
    m_vsyncEnabled = false;
    m_ready = false;
    m_is_resize = false;
}

Device::~Device()
{
    SDL_Log("[DEVICE] Destroyed.");
    Close();
}

bool Device::Create(int width, int height, const char *title, bool vzync, u16 monitorIndex)
{

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL could not initialize! SDL_Error: %s", SDL_GetError());
        return false;
    }
    m_current = 0;
    m_previous = 0;
    m_update = 0;
    m_draw = 0;
    m_frame = 0;

    // Do not combine software frame limiter with VSync.
    // Running both can cause pacing oscillation (e.g. bouncing 30/60 FPS).
    SetTargetFPS(vzync ? 0 : 1000);
    m_closekey = 256;
    m_width = width;
    m_height = height;

    m_current = GetTime();
    m_draw = m_current - m_previous;
    m_previous = m_current;
    m_frame = m_update + m_draw;

    // // Atributos de contexto antes de criar a janela
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);

    const int majorVersion = 3;
    const int minorVersion = 3; // pede 3.2 para ter debug core (altera para 3.1 para nao ter)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, majorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    // Formato de framebuffer
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

    // MSAA
    int sampleCount = 1; // mete >0 para ativar
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, sampleCount);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, sampleCount > 0 ? 1 : 0);

    int numDisplays = SDL_GetNumVideoDisplays();
    SDL_Log("[Device] Num Displays: %d", numDisplays);
    for (int i = 0; i < numDisplays; i++)
    {
        const char *displayName = SDL_GetDisplayName(i);
        SDL_Log("[Device] Display: %d - %s", i, displayName);
    }

    if (monitorIndex > numDisplays)
    {
        monitorIndex = 0;
    }

    // Criação
    m_window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED_DISPLAY(monitorIndex),
        SDL_WINDOWPOS_CENTERED_DISPLAY(monitorIndex),
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!m_window)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Device] Window! %s", SDL_GetError());
        return false;
    }

    m_context = SDL_GL_CreateContext(m_window);
    if (!m_context)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Device] Context! %s", SDL_GetError());
        return false;
    }

    // VSync
    SDL_GL_SetSwapInterval(vzync ? 1 : 0);
    m_vsyncEnabled = vzync;

    SDL_Log("Load opengl extensions.");

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    if (!gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Device] Failed to load GLES with glad");
        return false;
    }
    if (!(GLAD_GL_ES_VERSION_3_1))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "OpenGL ES 3.1 is required");
        return false;
    }
#endif

    SDL_Log("[DEVICE] Vendor  : %s", (const char *)glGetString(GL_VENDOR));
    SDL_Log("[DEVICE] Renderer: %s", (const char *)glGetString(GL_RENDERER));
    SDL_Log("[DEVICE] Version : %s", (const char *)glGetString(GL_VERSION));
    SDL_Log("[DEVICE] GLSL ES : %s",
            (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION));

    GLfloat maxAniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);

    SDL_Log("[DEVICE] Anisotropy: %f", maxAniso);

    TextureManager::instance();
    ShaderManager::instance();
    MeshManager::instance();
    AnimatedMeshManager::instance();
    VertexAnimatedMeshManager::instance();
    RenderState::instance();
    ShaderManager::instance();


     const char *vShader = GLSL(

            layout(location = 0) in vec3 position;
            layout(location = 1) in vec2 texCoord;
            layout(location = 2) in vec4 color;

            uniform mat4 mvp;

            out vec2 TexCoord;
            out vec4 vertexColor;
            void main() {
                gl_Position = mvp * vec4(position, 1.0);
                TexCoord = texCoord;
                vertexColor = color;
            });

        const char *fShader =
            GLSL(
                
                in vec2 TexCoord;
                out vec4 color;
                in vec4 vertexColor;
                uniform sampler2D texture0;
                void main() 
                {
                    color = texture(texture0, TexCoord) * vertexColor;
                });

            if (ShaderManager::instance().loadFromSource("Batch", vShader, fShader) == nullptr)
            {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Device] Failed to load 'Batch' shader");
                return false;
            }

    m_ready = true;

    return true;
}

void Device::Wait(float ms) { SDL_Delay(ms); }

int Device::GetFPS(void)
{
#define FPS_CAPTURE_FRAMES_COUNT 30   // 30 captures
#define FPS_AVERAGE_TIME_SECONDS 0.5f // 500 millisecondes
#define FPS_STEP (FPS_AVERAGE_TIME_SECONDS / FPS_CAPTURE_FRAMES_COUNT)

    static int index = 0;
    static float history[FPS_CAPTURE_FRAMES_COUNT] = {0};
    static float average = 0, last = 0;
    float fpsFrame = GetFrameTime();

    if (fpsFrame == 0)
        return 0;

    if ((GetTime() - last) > FPS_STEP)
    {
        last = (float)GetTime();
        index = (index + 1) % FPS_CAPTURE_FRAMES_COUNT;
        average -= history[index];
        history[index] = fpsFrame / FPS_CAPTURE_FRAMES_COUNT;
        average += history[index];
    }

    return (int)roundf(1.0f / average);
}

void Device::SetTargetFPS(int fps)
{
    if (fps < 1)
        m_target = 0.0;
    else
        m_target = 1.0 / (double)fps;
}

float Device::GetFrameTime(void) { return (float)m_frame; }

double Device::GetTime(void) { return (double)SDL_GetTicks() / 1000.0; }

u32 Device::GetTicks(void) { return SDL_GetTicks(); }

bool Device::Run()
{
    if (!m_ready)
        return false;

    m_current = GetTime(); // Number of elapsed seconds since InitTimer()
    m_update = m_current - m_previous;
    m_previous = m_current;
    m_is_resize = false;

    if (m_buguiReady)
    {
        const double t = GetTime();
        auto& bio = BuGUI::GetIO();
        bio.deltaTime = static_cast<float>(t - m_buguiPrevTime);
        m_buguiPrevTime = t;
        int ww, wh, dw, dh;
        SDL_GetWindowSize(m_window, &ww, &wh);
        SDL_GL_GetDrawableSize(m_window, &dw, &dh);
        bio.displayWidth      = static_cast<float>(ww);
        bio.displayHeight     = static_cast<float>(wh);
        bio.framebufferScaleX = ww > 0 ? static_cast<float>(dw) / static_cast<float>(ww) : 1.0f;
        bio.framebufferScaleY = wh > 0 ? static_cast<float>(dh) / static_cast<float>(wh) : 1.0f;
        BuGUI::NewFrame();
    }

    SDL_Event event;
    Input::Update();

    while (SDL_PollEvent(&event) != 0)
    {
        if (m_buguiReady)
        {
            auto& bio = BuGUI::GetIO();
            switch (event.type)
            {
            case SDL_MOUSEMOTION:
                bio.mouseX = static_cast<float>(event.motion.x);
                bio.mouseY = static_cast<float>(event.motion.y);
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            {
                int btn = 0;
                if (event.button.button == SDL_BUTTON_RIGHT)  btn = 1;
                else if (event.button.button == SDL_BUTTON_MIDDLE) btn = 2;
                bio.mouseDown[btn] = (event.type == SDL_MOUSEBUTTONDOWN);
                break;
            }
            case SDL_MOUSEWHEEL:
            {
                float scale = (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -1.0f : 1.0f;
#if SDL_VERSION_ATLEAST(2, 0, 18)
                bio.mouseWheelX += event.wheel.preciseX * scale;
                bio.mouseWheelY += event.wheel.preciseY * scale;
#else
                bio.mouseWheelX += static_cast<float>(event.wheel.x) * scale;
                bio.mouseWheelY += static_cast<float>(event.wheel.y) * scale;
#endif
                break;
            }
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            {
                SDL_Keymod mod = (SDL_Keymod)event.key.keysym.mod;
                bio.addKeyEvent(event.key.keysym.sym, event.key.keysym.scancode,
                                (mod & KMOD_SHIFT) != 0, (mod & KMOD_CTRL) != 0,
                                (mod & KMOD_ALT)   != 0, event.type == SDL_KEYDOWN);
                break;
            }
            case SDL_TEXTINPUT:
                for (const char* p = event.text.text; *p; ++p)
                    bio.addInputCharacter(static_cast<unsigned char>(*p));
                break;
            default: break;
            }
        }

        switch (event.type)
        {
        case SDL_QUIT:
        {
            m_shouldclose = true;
            break;
        }
        case SDL_WINDOWEVENT:
        {
            switch (event.window.event)
            {
            case SDL_WINDOWEVENT_RESIZED:
            {
                m_width = event.window.data1;
                m_height = event.window.data2;
                m_is_resize = true;
                break;
            }
            }
            break;
        }
        case SDL_KEYDOWN:
        {
            if (event.key.keysym.sym == SDLK_ESCAPE)
            {
                SetShouldClose(true);
                break;
            }
            Input::OnKeyDown(event.key);
            break;
        }
        case SDL_KEYUP:
        {
            Input::OnKeyUp(event.key);
            break;
        }
        case SDL_TEXTINPUT:
        {
            Input::OnTextInput(event.text);
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
        {
            Input::OnMouseDown(event.button);
            break;
        }
        case SDL_MOUSEBUTTONUP:
        {
            Input::OnMouseUp(event.button);
            break;
        }
        case SDL_MOUSEMOTION:
        {
            Input::OnMouseMove(event.motion);
            break;
        }
        case SDL_MOUSEWHEEL:
        {
            Input::OnMouseWheel(event.wheel);
            break;
        }
        }
    }

    return !m_shouldclose;
}

int Device::PollEvents(SDL_Event *event)
{
    if (!m_ready)
        return false;
    m_current = GetTime();
    m_update = m_current - m_previous;
    m_previous = m_current;
    int ret = SDL_PollEvent(event);
    return ret;
}

void Device::Close()
{
    if (!m_ready)
        return;

    EndGifRecording();
    EndFrameSequenceRecording();
    m_ready = false;

    BuGUIShutdown();

    TextureManager::instance().unloadAll();
    ShaderManager::instance().unloadAll();
    MeshManager::instance().unloadAll();
    AnimatedMeshManager::instance().unloadAll();
    VertexAnimatedMeshManager::instance().unloadAll();
    RenderState::instance().shutdown();

    SDL_GL_DeleteContext(m_context);
    SDL_DestroyWindow(m_window);

    m_window = NULL;
    SDL_Log("[DEVICE] closed!");
    SDL_Quit();
}

void Device::Flip()
{
    if (m_buguiReady)
    {
        const BuGUI::DrawData* dd = BuGUI::GetDrawData();
        if (dd) m_buguiRenderer->render(*dd);
    }

    CaptureGifFrame();
    CaptureFrameSequenceFrame();

    SDL_GL_SwapWindow(m_window);

    m_current = GetTime();
    m_draw = m_current - m_previous;
    m_previous = m_current;
    m_frame = m_update + m_draw;

    // Wait for some milliseconds...
    if (!m_vsyncEnabled && m_target > 0.0 && m_frame < m_target)
    {
        Wait((float)(m_target - m_frame) * 1000.0f);

        m_current = GetTime();
        double waitTime = m_current - m_previous;
        m_previous = m_current;

        m_frame += waitTime; // Total frame time: update + draw + wait
    }

    m_is_resize = false;
}

bool Device::IsRunning() const
{
    return m_ready && !m_shouldclose;
}

void Device::BuGUIInit()
{
    if (m_buguiReady) return;  // already initialised
    m_buguiRenderer = std::make_unique<BuGUIRenderer>();
    if (!m_buguiRenderer->init())
    {
        SDL_Log("[DEVICE] BuGUIRenderer init failed");
        m_buguiRenderer.reset();
        return;
    }

    BuGUI::SetCurrentContext(BuGUI::CreateContext());

    m_fontAtlas = std::make_unique<BuGUI::FontAtlas>();
    if (!m_fontAtlas->buildDefault())
    {
        SDL_Log("[DEVICE] BuGUI FontAtlas build failed");
        BuGUI::DestroyContext(BuGUI::GetCurrentContext());
        m_buguiRenderer.reset();
        m_fontAtlas.reset();
        return;
    }

    const BuGUI::TextureHandle atlasHandle = m_buguiRenderer->createTexture(
        m_fontAtlas->width(), m_fontAtlas->height(), m_fontAtlas->pixels());
    m_fontAtlas->setTexture(atlasHandle);
    BuGUI::SetWhitePixel(atlasHandle, m_fontAtlas->whitePixelUV());

    auto& io = BuGUI::GetIO();
    io.setClipboardText = [](const char* text) { SDL_SetClipboardText(text); };
    io.getClipboardText = []() -> std::string {
        char* clip = SDL_GetClipboardText();
        std::string result = clip ? clip : "";
        SDL_free(clip);
        return result;
    };
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

    m_buguiPrevTime = GetTime();

    // ── WidgetApp ────────────────────────────────────────────────────────
    BuGUI::WidgetApp::instance().setTextureUpload([this](const unsigned char* rgba, int w, int h) {
        return m_buguiRenderer->createTexture(w, h, rgba);
    });
    BuGUI::WidgetApp::instance().setTextureDestroy([this](BuGUI::TextureHandle tex) {
        m_buguiRenderer->destroyTexture(tex);
    });
    BuGUI::WidgetApp::instance().init();
    // Transparent background — the 3-D scene shows through the widget overlay
    BuGUI::WidgetApp::instance().setDrawBackground(false);

    m_buguiReady = true;
    SDL_Log("[DEVICE] BuGUI initialised");
}

void Device::BuGUIBegin()
{
    if (!m_buguiReady) return;
    BuGUI::WidgetApp::instance().update(BuGUI::GetIO());
    // Block 3D-scene mouse input while the cursor is over a GUI widget.
    Input::SetGuiBlocked(BuGUI::WidgetApp::instance().wantsMouse());
}

void Device::BuGUIEnd()
{
    if (!m_buguiReady) return;
    BuGUI::WidgetApp::instance().paint(*BuGUI::GetDrawData(), &m_fontAtlas->defaultFont());
    BuGUI::Render();
}

void Device::BuGUIShutdown()
{
    if (!m_buguiReady) return;
    m_buguiReady = false;
    BuGUI::WidgetApp::instance().shutdown();
    if (m_fontAtlas)
    {
        m_buguiRenderer->destroyTexture(m_fontAtlas->texture());
        m_fontAtlas.reset();
    }
    m_buguiRenderer.reset();
    BuGUI::DestroyContext(BuGUI::GetCurrentContext());
    SDL_Log("[DEVICE] BuGUI shut down");
}

BuGUI::TextureHandle Device::BuGUICreateTexture(int w, int h, const unsigned char* rgba)
{
    if (!m_buguiReady) return {};
    return m_buguiRenderer->createTexture(w, h, rgba);
}

void Device::BuGUIDestroyTexture(BuGUI::TextureHandle handle)
{
    if (!m_buguiReady) return;
    m_buguiRenderer->destroyTexture(handle);
}

Pixmap *Device::CaptureFramebuffer()
{
    // Obter tamanho da janela
    int w = GetWidth();
    int h = GetHeight();

    // Criar Pixmap RGBA
    Pixmap *screenshot = new Pixmap(w, h, 4);

    if (!screenshot || !screenshot->IsValid())
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Device] Failed to create screenshot pixmap");
        return nullptr;
    }

    // Ler pixels do framebuffer
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, screenshot->pixels);

    // OpenGL lê de baixo para cima, então flip vertical
    screenshot->FlipVertical();

    SDL_Log("[Device] Captured framebuffer: %dx%d", w, h);

    return screenshot;
}

bool Device::TakeScreenshot(const char *filename)
{
    Pixmap *screenshot = CaptureFramebuffer();

    if (!screenshot)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Device] Failed to capture framebuffer");
        return false;
    }

    // Salvar como PNG
    bool success = screenshot->Save(filename);

    if (success)
    {
        SDL_Log("[Device] Screenshot saved: %s", filename);
    }
    else
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Device] Failed to save screenshot: %s", filename);
    }

    delete screenshot;
    return success;
}

bool Device::BeginGifRecording(const char* filename, int fps)
{
    if (!m_ready || !m_window)
        return false;
    if (IsGifRecording())
        return false;

    std::string outputPath = (filename && filename[0] != '\0') ? filename : BuildDefaultGifPath();
    std::filesystem::path path(outputPath);
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());

    auto recording = std::make_unique<GifRecordingState>();
    recording->width = GetWidth();
    recording->height = GetHeight();
    recording->fps = std::clamp(fps, 1, 50);
    recording->frameDelayCenti = std::max(1, static_cast<int>(std::round(100.0 / static_cast<double>(recording->fps))));
    recording->captureInterval = recording->frameDelayCenti / 100.0;
    recording->accumulator = recording->captureInterval;
    recording->path = path.generic_string();
    recording->pixels.resize(static_cast<std::size_t>(recording->width) * static_cast<std::size_t>(recording->height) * 4u);

    recording->file = std::fopen(recording->path.c_str(), "wb");
    if (!recording->file)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Device] Failed to open GIF file: %s", recording->path.c_str());
        return false;
    }

    msf_gif_alpha_threshold = 0;
    msf_gif_bgra_flag = 0;
    if (!msf_gif_begin_to_file(&recording->gif, recording->width, recording->height,
                               reinterpret_cast<MsfGifFileWriteFunc>(std::fwrite), recording->file))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Device] Failed to begin GIF recording");
        std::fclose(recording->file);
        return false;
    }

    SDL_Log("[Device] GIF recording started: %s (%dx%d @ %d fps)",
            recording->path.c_str(), recording->width, recording->height, recording->fps);
    m_gifRecording = std::move(recording);
    return true;
}

bool Device::EndGifRecording()
{
    if (!m_gifRecording)
        return false;

    bool success = true;
    if (m_gifRecording->file)
    {
        success = msf_gif_end_to_file(&m_gifRecording->gif) != 0;
        std::fclose(m_gifRecording->file);
        m_gifRecording->file = nullptr;
    }

    SDL_Log(success
            ? "[Device] GIF recording saved: %s (%d frames)"
            : "[Device] GIF recording failed while finalizing: %s",
            m_gifRecording->path.c_str(),
            m_gifRecording->framesWritten);

    m_gifRecording.reset();
    return success;
}

bool Device::IsGifRecording() const
{
    return static_cast<bool>(m_gifRecording);
}

std::string Device::GetGifRecordingPath() const
{
    return m_gifRecording ? m_gifRecording->path : std::string();
}

int Device::GetGifRecordingFPS() const
{
    return m_gifRecording ? m_gifRecording->fps : 0;
}

int Device::GetGifRecordingFrameCount() const
{
    return m_gifRecording ? m_gifRecording->framesWritten : 0;
}

bool Device::BeginFrameSequenceRecording(const char* directory, const char* extension, int fps)
{
    if (!m_ready || !m_window)
        return false;
    if (IsFrameSequenceRecording())
        return false;

    std::string normalizedExtension = (extension && extension[0] != '\0') ? extension : "png";
    if (!normalizedExtension.empty() && normalizedExtension.front() == '.')
        normalizedExtension.erase(normalizedExtension.begin());
    std::transform(normalizedExtension.begin(), normalizedExtension.end(), normalizedExtension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (normalizedExtension != "png" && normalizedExtension != "jpg" && normalizedExtension != "jpeg")
        return false;

    auto recording = std::make_unique<FrameSequenceRecordingState>();
    recording->width = GetWidth();
    recording->height = GetHeight();
    recording->fps = std::clamp(fps, 1, 120);
    recording->captureInterval = 1.0 / static_cast<double>(recording->fps);
    recording->accumulator = recording->captureInterval;
    recording->extension = normalizedExtension == "jpeg" ? "jpg" : normalizedExtension;
    recording->directory = (directory && directory[0] != '\0')
        ? std::filesystem::path(directory).generic_string()
        : BuildDefaultFrameSequenceDirectory(recording->extension.c_str());
    recording->pixels.resize(static_cast<std::size_t>(recording->width) * static_cast<std::size_t>(recording->height) * 4u);

    std::filesystem::create_directories(recording->directory);
    SDL_Log("[Device] Frame sequence recording started: %s (%dx%d @ %d fps, %s)",
            recording->directory.c_str(), recording->width, recording->height, recording->fps, recording->extension.c_str());
    m_frameSequenceRecording = std::move(recording);
    return true;
}

bool Device::EndFrameSequenceRecording()
{
    if (!m_frameSequenceRecording)
        return false;

    m_lastFrameSequenceDirectory = m_frameSequenceRecording->directory;
    m_lastFrameSequenceExtension = m_frameSequenceRecording->extension;
    m_lastFrameSequenceFPS = m_frameSequenceRecording->fps;

    SDL_Log("[Device] Frame sequence recording saved: %s (%d frames, %s)",
            m_frameSequenceRecording->directory.c_str(),
            m_frameSequenceRecording->framesWritten,
            m_frameSequenceRecording->extension.c_str());
    m_frameSequenceRecording.reset();
    return true;
}

bool Device::IsFrameSequenceRecording() const
{
    return static_cast<bool>(m_frameSequenceRecording);
}

std::string Device::GetFrameSequenceDirectory() const
{
    return m_frameSequenceRecording ? m_frameSequenceRecording->directory : std::string();
}

std::string Device::GetFrameSequenceExtension() const
{
    return m_frameSequenceRecording ? m_frameSequenceRecording->extension : std::string();
}

int Device::GetFrameSequenceFPS() const
{
    return m_frameSequenceRecording ? m_frameSequenceRecording->fps : 0;
}

int Device::GetFrameSequenceFrameCount() const
{
    return m_frameSequenceRecording ? m_frameSequenceRecording->framesWritten : 0;
}

std::string Device::GetLastFrameSequenceDirectory() const
{
    return m_lastFrameSequenceDirectory;
}

std::string Device::GetLastFrameSequenceExtension() const
{
    return m_lastFrameSequenceExtension;
}

int Device::GetLastFrameSequenceFPS() const
{
    return m_lastFrameSequenceFPS;
}

bool Device::ExportLastFrameSequenceToVideo(const char* outputFilename) const
{
    if (m_lastFrameSequenceDirectory.empty() || m_lastFrameSequenceExtension.empty() || m_lastFrameSequenceFPS <= 0)
        return false;

    return ExportFrameSequenceToVideo(m_lastFrameSequenceDirectory,
                                      m_lastFrameSequenceExtension,
                                      m_lastFrameSequenceFPS,
                                      outputFilename);
}

void Device::CaptureGifFrame()
{
    if (!m_gifRecording)
        return;

    GifRecordingState& recording = *m_gifRecording;
    if (GetWidth() != recording.width || GetHeight() != recording.height)
    {
        SDL_Log("[Device] Window resized during GIF recording, stopping capture");
        EndGifRecording();
        return;
    }

    const double frameSeconds = (m_update > 0.0) ? m_update : ((m_target > 0.0) ? m_target : (1.0 / 60.0));
    recording.accumulator += frameSeconds;
    if (recording.framesWritten > 0 && recording.accumulator + 1e-9 < recording.captureInterval)
        return;

    recording.accumulator = std::max(0.0, recording.accumulator - recording.captureInterval);
    glReadPixels(0, 0, recording.width, recording.height, GL_RGBA, GL_UNSIGNED_BYTE, recording.pixels.data());

    const int rowBytes = recording.width * 4;
    for (int y = 0; y < recording.height / 2; ++y)
    {
        uint8_t* top = recording.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(rowBytes);
        uint8_t* bottom = recording.pixels.data() +
            static_cast<std::size_t>(recording.height - 1 - y) * static_cast<std::size_t>(rowBytes);
        for (int x = 0; x < rowBytes; ++x)
            std::swap(top[x], bottom[x]);
    }

    if (!msf_gif_frame_to_file(&recording.gif, recording.pixels.data(),
                               recording.frameDelayCenti, 16, rowBytes))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Device] Failed to append GIF frame");
        EndGifRecording();
        return;
    }

    recording.framesWritten++;
}

std::string Device::BuildDefaultGifPath() const
{
    std::time_t now = std::time(nullptr);
    std::tm tmNow = {};
#if defined(_WIN32)
    localtime_s(&tmNow, &now);
#else
    localtime_r(&now, &tmNow);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmNow);
    std::filesystem::path output = std::filesystem::current_path() / "captures" /
        ("minirender_" + std::string(stamp) + ".gif");
    return output.generic_string();
}

void Device::CaptureFrameSequenceFrame()
{
    if (!m_frameSequenceRecording)
        return;

    FrameSequenceRecordingState& recording = *m_frameSequenceRecording;
    if (GetWidth() != recording.width || GetHeight() != recording.height)
    {
        SDL_Log("[Device] Window resized during frame sequence recording, stopping capture");
        EndFrameSequenceRecording();
        return;
    }

    const double frameSeconds = (m_update > 0.0) ? m_update : ((m_target > 0.0) ? m_target : (1.0 / 60.0));
    recording.accumulator += frameSeconds;
    if (recording.framesWritten > 0 && recording.accumulator + 1e-9 < recording.captureInterval)
        return;

    recording.accumulator = std::max(0.0, recording.accumulator - recording.captureInterval);
    glReadPixels(0, 0, recording.width, recording.height, GL_RGBA, GL_UNSIGNED_BYTE, recording.pixels.data());

    Pixmap frame(recording.width, recording.height, 4, recording.pixels.data());
    frame.FlipVertical();

    char fileName[64];
    std::snprintf(fileName, sizeof(fileName), "frame_%06d.%s",
                  recording.framesWritten + 1, recording.extension.c_str());
    const std::filesystem::path outputPath = std::filesystem::path(recording.directory) / fileName;

    if (!frame.Save(outputPath.string().c_str()))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Device] Failed to save frame: %s", outputPath.string().c_str());
        EndFrameSequenceRecording();
        return;
    }

    recording.framesWritten++;
}

std::string Device::BuildDefaultFrameSequenceDirectory(const char* extension) const
{
    std::time_t now = std::time(nullptr);
    std::tm tmNow = {};
#if defined(_WIN32)
    localtime_s(&tmNow, &now);
#else
    localtime_r(&now, &tmNow);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tmNow);
    std::filesystem::path output = std::filesystem::current_path() / "captures" /
        ("frames_" + std::string(extension ? extension : "png") + "_" + std::string(stamp));
    return output.generic_string();
}

std::string Device::BuildDefaultVideoPath(const std::string& frameDirectory) const
{
    std::filesystem::path directory(frameDirectory);
    std::filesystem::path output = directory;
    output += ".mp4";
    return output.generic_string();
}

bool Device::ExportFrameSequenceToVideo(const std::string& directory,
                                        const std::string& extension,
                                        int fps,
                                        const char* outputFilename) const
{
    if (directory.empty() || extension.empty() || fps <= 0)
        return false;

    const std::filesystem::path inputDir(directory);
    if (!std::filesystem::exists(inputDir))
        return false;

    const std::string outputPath = (outputFilename && outputFilename[0] != '\0')
        ? std::filesystem::path(outputFilename).generic_string()
        : BuildDefaultVideoPath(directory);

    const std::string ffmpegCheck = "ffmpeg -version > /dev/null 2>&1";
    if (std::system(ffmpegCheck.c_str()) != 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Device] ffmpeg not found in PATH");
        return false;
    }

    const std::filesystem::path inputPattern = inputDir / ("frame_%06d." + extension);
    const std::string command =
        "ffmpeg -y -framerate " + std::to_string(fps) +
        " -i \"" + inputPattern.generic_string() +
        "\" -vf \"pad=ceil(iw/2)*2:ceil(ih/2)*2,format=yuv420p\""
        " -c:v libx264 -pix_fmt yuv420p \"" + outputPath + "\"";

    SDL_Log("[Device] Exporting video: %s", outputPath.c_str());
    const int result = std::system(command.c_str());
    if (result != 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[Device] ffmpeg video export failed");
        return false;
    }

    SDL_Log("[Device] Video exported: %s", outputPath.c_str());
    return true;
}
