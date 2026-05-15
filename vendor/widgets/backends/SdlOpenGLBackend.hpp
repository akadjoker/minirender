#pragma once

#include "BuGUI.hpp"
#include <cstdint>

using namespace BuGUI;

class SdlOpenGLBackend
{
public:
    bool init(const char* title, int width, int height);
    void shutdown();

    bool beginFrame();
    void render(const BuGUI::DrawData& drawData);
    void present();

    BuGUI::TextureHandle createTextureRGBA(int width, int height, const unsigned char* pixels);
    void destroyTexture(BuGUI::TextureHandle texture);

    void setBackgroundColor(const Color& color);
    bool shouldClose() const { return shouldClose_; }

private:
    bool createDeviceObjects();
    void destroyDeviceObjects();
    void processEvents();
    void updateDisplaySize();
    void renderDrawData(const BuGUI::DrawData& drawData);
    void applyCursor(int cursorType);

    void* window_ = nullptr;
    void* glContext_ = nullptr;
    void* sdlCursors_[8] = {};  // SDL_Cursor* indexed by CursorType
    int   activeCursor_ = 0;
    uint32_t shader_ = 0;
    uint32_t vao_ = 0;
    uint32_t vbo_ = 0;
    uint32_t ebo_ = 0;
    Color clearColor_ = Color(24, 26, 30, 255);
    double previousTime_ = 0.0;
    bool shouldClose_ = false;
};
