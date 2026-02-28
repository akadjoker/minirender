# MiniPipeline

A lightweight real-time 3D rendering engine built from scratch in C++17 on top of OpenGL 3.3 Core / OpenGL ES 3.0 and SDL2.

The goal was to build every layer manually — no game-engine middleware — to deeply understand how modern render pipelines work under the hood.

---

## Features

### Rendering
- **Forward Renderer** — opaque + transparent passes with front-to-back / back-to-front sorting
- **Deferred Shading** — GBuffer geometry pass → fullscreen lighting pass
  - RGB16F world-space position + normal buffers
  - RGBA8 albedo + specular buffer
  - Blinn-Phong lighting with 1 directional light + up to 16 point lights
- **Shadow Mapping** — directional light orthographic depth pass
  - PCF 3×3 soft shadows
  - Adaptive depth bias (reduces acne and peter panning)
  - Configurable ortho frustum size and light distance

### Scene Graph
- Node hierarchy (`Node → Node3D → MeshNode / Light / Camera`)
- Per-node world matrix caching with dirty propagation
- Typed light nodes: `PointLight`, `DirectionalLight`, `SpotLight`

### Render Pipeline Architecture
- `RenderQueue` — submitted once, consumed by multiple passes
- `RenderPass` — configurable depth, cull, blend, scissor, sort state
- `Technique` — owns and sequences passes; scene calls `render()` once per frame
  - `ForwardTechnique` — OpaquePass + TransparentPass
  - `ShadowTechnique` — ShadowPass + OpaquePass + TransparentPass
  - `DeferredTechnique` — GBufferPass + DeferredLightingPass + TransparentPass (forward)
- `RenderState` — OpenGL state cache (avoids redundant API calls)

### Frustum Culling
- Per-node and per-surface AABB culling against the camera frustum
- World-space AABBs precomputed once per gather, never rebuilt in passes

### Asset Pipeline
- **GLTF 2.0** loader via [cgltf](https://github.com/jkuhlmann/cgltf)
- **OBJ / MTL** loader
- Procedural mesh generation: cube, plane, sphere, cylinder, wireframe variants
- Tangent computation (Mikktspace-style)
- Texture loading via SDL2_image (PNG, JPG, …)

### Material System
- Per-material shader, textures (multi-slot), and uniform overrides
- `MaterialManager` with default shader / fallback texture propagation
- Material cloning for per-instance overrides

### Camera
- Perspective camera with cached view/projection/viewProjection matrices
- `FreeCameraController` — WASD + mouse look, configurable speed + sprint

### Demo Framework
- `DemoManager` — switch demos at runtime with `←` / `→`
- `DemoShadow` — shadow map showcase (5 cubes, directional light)
- `DemoDeferred` — deferred shading with 8 orbiting coloured point lights

---

## Architecture Overview

```
Scene
 ├── Node tree (MeshNode, Light, Camera, …)
 ├── RenderQueue  ← gatherNode() fills this every frame (frustum culled)
 └── Technique[]
      └── RenderPass[]  ← each pass reads the queue and issues GL draw calls
```

```
DeferredTechnique
 ├── GBufferPass        → renders geometry into GBuffer FBO (MRT)
 ├── DeferredLightingPass → fullscreen triangle, samples GBuffer, outputs lit color
 └── TransparentPass    → forward pass for alpha-blended geometry
```

---

## Dependencies

| Library | Purpose |
|---|---|
| [SDL2](https://www.libsdl.org/) | Window, context, input, file I/O |
| [SDL2_image](https://github.com/libsdl-org/SDL_image) | Texture loading |
| [glad](https://glad.dav1d.de/) | OpenGL / GLES function loader |
| [glm](https://github.com/g-truc/glm) | Math (vectors, matrices, quaternions) |
| [cgltf](https://github.com/jkuhlmann/cgltf) | GLTF 2.0 mesh loading (header-only) |
| [par_shapes](https://github.com/prideout/par) | Procedural mesh generation (header-only) |

---

## Build

```bash
# Configure (Debug with AddressSanitizer + UBSan)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build

# Run (assets are relative to the bin/ directory)
cd bin && ./mini_pipeline
```

Requires **CMake 3.16+**, **Clang** or **GCC** with C++17, and the dependencies above installed system-wide.

---

## Shader Layout

| File | Purpose |
|---|---|
| `unlit.vert/frag` | Unlit colour/texture pass |
| `depth.vert/frag` | Shadow map depth-only pass |
| `lit_shadow.vert/frag` | Forward lit pass with PCF shadow mapping |
| `gbuffer.vert/frag` | Deferred geometry pass (writes MRT) |
| `deferred_lighting.vert/frag` | Deferred fullscreen lighting pass |

Vertex attributes follow a fixed layout:

| Location | Attribute |
|---|---|
| 0 | `a_position` (vec3) |
| 1 | `a_normal` (vec3) |
| 2 | `a_tangent` (vec4, w = handedness) |
| 3 | `a_uv` (vec2) |

---

## Controls

| Key | Action |
|---|---|
| `W A S D` | Move camera |
| `Mouse` | Look around |
| `Shift` | Sprint |
| `→` / `←` | Next / previous demo |
| `Escape` | Quit |

---

## Project Structure

```
minirender/
├── src/          C++ source & headers
├── bin/
│   └── assets/
│       ├── shaders/    GLSL shaders
│       ├── models/     Mesh files (GLTF/OBJ)
│       └── textures/   Texture files
├── include/
│   └── glad/     GL loader
└── CMakeLists.txt
```
