#pragma once
#include <SDL2/SDL.h>
#include <string>

 
class Demo
{
public:
    virtual ~Demo() = default;

    virtual bool        init()           = 0;
    virtual void        update(float dt) = 0;
    virtual void        render()         = 0;
    virtual void        release()        = 0;
    virtual const char *name()           = 0;

 
    virtual void onResize(int w, int h) {}
 
    virtual bool onKey(SDL_Scancode key) { return false; }
};
