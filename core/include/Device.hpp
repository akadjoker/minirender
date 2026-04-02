#pragma once

#include "Config.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

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
    bool m_ready;
    Sint32 m_closekey;
    bool m_imguiReady = false;

    Device();
    ~Device();
     
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

};



