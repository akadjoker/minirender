#pragma once

#include "DemoBase.hpp"

class DemoMD3 : public DemoBase
{
public:
    const char *name() override { return "MD3 (WIP)"; }

    bool init() override
    {
        DemoBase::init();
        return true;
    }

    void update(float dt) override
    {
        DemoBase::update(dt);
    }

    void render() override
    {
        DemoBase::render();
    }

    void release() override
    {
        DemoBase::release();
    }
};
