#pragma once

#include <vector>

#include "Demo.hpp"
#include "demos/EffectsDemo.hpp"
#include "demos/TerrainDemo.hpp"
#include "demos/ToyPlaneDemo.hpp"
#include "demos/VertexAnimationDemo.hpp"

template <typename T>
IDemo *createDemoInstance()
{
    return new T();
}

inline const std::vector<DemoEntry> &getDemoRegistry()
{
    static const std::vector<DemoEntry> demos = {
        {"Effects Lab", &createDemoInstance<EffectsDemo>},
        {"Terrain Tests", &createDemoInstance<TerrainDemo>},
        {"Toy Plane", &createDemoInstance<ToyPlaneDemo>},
        {"Vertex Animation", &createDemoInstance<VertexAnimationDemo>},
    };

    return demos;
}
