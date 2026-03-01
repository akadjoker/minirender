#pragma once
#include "Demo.hpp"
#include <vector>
#include <string>
#include <SDL2/SDL.h>

 
class DemoManager
{
public:
    ~DemoManager() { releaseAll(); }

    void add(Demo *demo)    { demos_.push_back(demo); }

    bool init()
    {
        if (demos_.empty()) return false;
        return demos_[current_]->init();
    }

    void update(float dt)
    {
        if (valid()) demos_[current_]->update(dt);

  
    }

    Scene& getScene() 
    { 
        return demos_[current_]->getScene();
    }
    void render()
    {
        if (valid()) demos_[current_]->render();
    }

    void onResize(int w, int h)
    {
        if (valid()) demos_[current_]->onResize(w, h);
    }

    // Devolve o nome do demo actual — útil para o título da janela
    const char *currentName() const
    {
        return valid() ? demos_[current_]->name() : "—";
    }

    // Processa eventos SDL — troca demo com LEFT/RIGHT
    // Devolve false se SDL_QUIT foi recebido
    // bool processEvents(SDL_Window *window)
    // {
    //     SDL_Event e;
    //     while (SDL_PollEvent(&e))
    //     {
    //         if (e.type == SDL_QUIT)
    //             return false;

    //         if (e.type == SDL_KEYDOWN)
    //         {
    //             SDL_Scancode key = e.key.keysym.scancode;

    //             // Deixa o demo consumir primeiro
    //             if (valid() && demos_[current_]->onKey(key))
    //                 continue;

    //             if (key == SDL_SCANCODE_RIGHT)      { next(window);     continue; }
    //             if (key == SDL_SCANCODE_LEFT)       { prev(window);     continue; }
    //             if (key == SDL_SCANCODE_ESCAPE)     return false;
    //         }
    //     }
    //     return true;
    // }

    void releaseAll()
    {
        for (auto *d : demos_) { d->release(); delete d; }
        demos_.clear();
        current_ = 0;
    }

    int  count()   const { return (int)demos_.size(); }
    int  current() const { return current_; }

private:
    bool valid() const { return !demos_.empty() && current_ < (int)demos_.size(); }

    void switchTo(int idx )
    {
        if (!valid()) return;
        demos_[current_]->release();
        current_ = (idx + (int)demos_.size()) % (int)demos_.size();
        demos_[current_]->init();

        
    }

    void next( ) { switchTo(current_ + 1); }
    void prev( ) { switchTo(current_ - 1); }

    std::vector<Demo *> demos_;
    int                 current_ = 0;
};
