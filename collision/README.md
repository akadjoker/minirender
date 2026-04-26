# collision

Pasta root com o demo e o sistema de colisao adaptado.

## Estrutura atual
- `CollisionSystem.hpp/.cpp`
  - Narrow-phase inspirado em `study/blitz3d/collision.cpp`
  - sphere/triangle sweep, raycast, slide base

- `CollisionWorld.hpp/.cpp`
  - Orquestracao inspirada em `study/blitz3d/world.cpp`
  - `hitTest` por metodo (`SPHERE`, `POLYGON`, `BOX`)
  - respostas (`STOP`, `SLIDE`, `SLIDEXZ`)

- `main.cpp`
  - Demo root do pipeline completo (world + mesh collision)

## Build/Run
- Target: `collision`
- Binario: `bin/collision`
