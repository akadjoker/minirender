#pragma once
#include <SDL2/SDL.h>

class Input
{
public:
    static Input &instance()
    {
        static Input inst;
        return inst;
    }

    void process()
    {
        mouseDX = mouseDY = 0;
        scrollY = 0;
        memcpy(prevKeys, keys, sizeof(keys));
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
                _quit = true;
            if (e.type == SDL_KEYDOWN)
                keys[e.key.keysym.scancode] = true;
            if (e.type == SDL_KEYUP)
                keys[e.key.keysym.scancode] = false;
            if (e.type == SDL_MOUSEMOTION)
            {
                mouseDX = (float)e.motion.xrel;
                mouseDY = (float)e.motion.yrel;
            }
            if (e.type == SDL_MOUSEBUTTONDOWN)
                mouseButtons[e.button.button] = true;
            if (e.type == SDL_MOUSEBUTTONUP)
                mouseButtons[e.button.button] = false;
            if (e.type == SDL_MOUSEWHEEL)
                scrollY = (float)e.wheel.y;
        }
    }

    bool isKey    (SDL_Scancode key) const { return keys[key]; }
    // true only on the frame the key was first pressed
    bool isKeyDown(SDL_Scancode key) const { return keys[key] && !prevKeys[key]; }
    bool isMouseDown(int btn) const { return mouseButtons[btn]; }
    float getDeltaX() const { return mouseDX; }
    float getDeltaY() const { return mouseDY; }
    bool shouldQuit() const { return _quit; }
    float getScrollY() const { return scrollY; }

private:
    bool keys[SDL_NUM_SCANCODES]     = {};
    bool prevKeys[SDL_NUM_SCANCODES] = {};
    bool mouseButtons[8] = {};
    float mouseDX = 0, mouseDY = 0;
    float scrollY = 0;
    bool _quit = false;
};
