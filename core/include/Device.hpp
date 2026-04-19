#pragma once

#include "Config.hpp"
#include <SDL2/SDL.h>
#include <memory>
#include <string>

// Forward-declare so Device.hpp doesn't pull all of imgui.h into every TU
struct ImGuiContext;
class Pixmap;

class     Device final
{
public:
  
    bool Create(int width, int height,const char* title,bool vzync=false,u16 monitorIndex=0);
 
    bool Run();
    int  PollEvents( SDL_Event *event);

    void Close();

    void Flip();

   
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }


    void Wait(float ms);
    void SetTargetFPS(int fps);
    int  GetFPS(void);
    float GetFrameTime(void);
    double GetTime(void);
    u32  GetTicks(void) ;

    void SetShouldClose(bool close) { m_shouldclose = close; }

    bool ShouldClose() const { return m_shouldclose; }

    void SetCloseKey(Sint32 key) { m_closekey = key; }
    bool IsReady() const { return m_ready && !m_shouldclose; }
    bool IsResize() const { return m_is_resize; }
    bool IsRunning() const;

    bool TakeScreenshot(const char* filename);
    Pixmap* CaptureFramebuffer();
    bool BeginGifRecording(const char* filename = nullptr, int fps = 12);
    bool EndGifRecording();
    bool IsGifRecording() const;
    std::string GetGifRecordingPath() const;
    int GetGifRecordingFPS() const;
    int GetGifRecordingFrameCount() const;
    bool BeginFrameSequenceRecording(const char* directory = nullptr, const char* extension = "png", int fps = 30);
    bool EndFrameSequenceRecording();
    bool IsFrameSequenceRecording() const;
    std::string GetFrameSequenceDirectory() const;
    std::string GetFrameSequenceExtension() const;
    int GetFrameSequenceFPS() const;
    int GetFrameSequenceFrameCount() const;
    std::string GetLastFrameSequenceDirectory() const;
    std::string GetLastFrameSequenceExtension() const;
    int GetLastFrameSequenceFPS() const;
    bool ExportLastFrameSequenceToVideo(const char* outputFilename = nullptr) const;

    SDL_Window*   GetWindow() const { return m_window; }
    SDL_GLContext  GetGLContext() const { return m_context; }

    // ── ImGui integration ────────────────────────────────────────
    // Call once after Create().  glsl_version e.g. "#version 300 es"
    void ImGuiInit(const char *glsl_version = "#version 300 es");
    // Call at the start of render — opens a new ImGui frame
    void ImGuiBegin();
    // Call at the end of render — renders ImGui draw data
    void ImGuiEnd();
    // Called automatically by Close(), but safe to call manually
    void ImGuiShutdown();

    static Device& Instance();
    static Device* InstancePtr();


private:
    struct GifRecordingState;
    struct FrameSequenceRecordingState;
 
    int m_width;
    int m_height;    
    bool m_shouldclose ;
    bool m_is_resize;
    SDL_Window   *m_window;
    SDL_GLContext m_context;

    double m_current;                 
    double m_previous;                  
    double m_update;                    
    double m_draw;                       
    double m_frame;   
    double m_target;
    bool m_vsyncEnabled;
    bool m_ready;
    Sint32 m_closekey;
    bool m_imguiReady = false;
    std::unique_ptr<GifRecordingState> m_gifRecording;
    std::unique_ptr<FrameSequenceRecordingState> m_frameSequenceRecording;
    std::string m_lastFrameSequenceDirectory;
    std::string m_lastFrameSequenceExtension;
    int m_lastFrameSequenceFPS = 0;

    Device();
    ~Device();
     
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    void CaptureGifFrame();
    void CaptureFrameSequenceFrame();
    std::string BuildDefaultGifPath() const;
    std::string BuildDefaultFrameSequenceDirectory(const char* extension) const;
    std::string BuildDefaultVideoPath(const std::string& frameDirectory) const;
    bool ExportFrameSequenceToVideo(const std::string& directory,
                                    const std::string& extension,
                                    int fps,
                                    const char* outputFilename) const;

};
