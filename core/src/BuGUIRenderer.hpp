#pragma once

#include <BuGUI.hpp>
#include <cstdint>

// Internal to core — not part of the public API.
// Renders BuGUI::DrawData onto the current OpenGL context.
// Saves and restores all GL state it touches so it is safe to
// call between 3-D draw calls without disturbing the scene state.
class BuGUIRenderer
{
public:
    bool init();
    void shutdown();

    // Render the BuGUI draw data produced by BuGUI::Render().
    // Does NOT clear the framebuffer — draws on top of whatever is there.
    void render(const BuGUI::DrawData& drawData);

    BuGUI::TextureHandle createTexture(int w, int h, const unsigned char* rgba);
    void                 destroyTexture(BuGUI::TextureHandle handle);

private:
    bool createDeviceObjects();
    void destroyDeviceObjects();
    void renderDrawData(const BuGUI::DrawData& drawData);

    uint32_t shader_ = 0;
    uint32_t vao_    = 0;
    uint32_t vbo_    = 0;
    uint32_t ebo_    = 0;
};
