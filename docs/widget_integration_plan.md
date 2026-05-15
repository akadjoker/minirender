# Plano: Integração de BuGUI Widgets no Core como Biblioteca de UI

## Situação Atual

| Componente | Estado |
|---|---|
| `core` | Tem `Device` (SDL window + GL context). ImGui integrado via `ImGuiInit/Begin/End`. |
| `vendor/widgets` (`bugui_widgets`) | BuGUI widget library com **backend próprio** (`SdlOpenGLBackend`) que cria a sua própria janela SDL — não é reutilizável quando o `Device` já existe. |
| Editor, demos | Usam ImGui directamente através do `Device`. |

**Problema central:** `SdlOpenGLBackend` abre a sua própria janela SDL e contexto GL. Precisa de um *backend* que use o contexto já existente no `Device`.

---

## Arquitectura Alvo

```
┌─────────────────────────────────────────────────────┐
│  App (editor / demo)                                │
│   BuGUI::Begin() … widgets … BuGUI::End()           │
└─────────────────┬───────────────────────────────────┘
                  │ calls
┌─────────────────▼───────────────────────────────────┐
│  Device  (core)                                     │
│   BuGUIInit()   BuGUIBegin()   BuGUIEnd()           │
│         │              │              │             │
│   cria contexto   feed eventos   render DrawData    │
└─────────────────┬───────────────────────────────────┘
                  │ usa
┌─────────────────▼───────────────────────────────────┐
│  CoreBuGUIRenderer  (novo, em core/src)             │
│   GL shader + VAO/VBO/EBO (igual ao SdlOpenGL)      │
│   renderDrawData(const BuGUI::DrawData&)            │
│   createTextureRGBA(...)  / destroyTexture(...)     │
└─────────────────────────────────────────────────────┘
```

---

## Fases

### Fase 1 — CoreBuGUIRenderer (sem tocar em nada existente)

**Ficheiros novos:**
- `core/src/BuGUIRenderer.hpp`
- `core/src/BuGUIRenderer.cpp`

**Responsabilidades:**
- Compilar e ligar o shader GL (idêntico ao de `SdlOpenGLBackend`, mas sem SDL init nem criação de janela).
- `renderDrawData(const BuGUI::DrawData&)` — faz o draw call exacto da `SdlOpenGLBackend::renderDrawData`.
- `createTextureRGBA` / `destroyTexture` — wraps GL directo (sem `TextureManager` — BuGUI gere os seus handles).
- Upload do atlas de fontes BuGUI → `BuGUI::TextureHandle`.

**Não depende de:** `SdlOpenGLBackend`, SDL_Init, criação de janela.

---

### Fase 2 — Integração em Device

Adicionar ao `Device`:

```cpp
// Device.hpp
void BuGUIInit();                  // cria contexto BuGUI, upload do font atlas
void BuGUIBegin();                 // SDL events → BuGUI IO, chama BuGUI::NewFrame()
void BuGUIEnd();                   // BuGUI::EndFrame(), render DrawData
void BuGUIShutdown();              // destrói contexto e GL objects

BuGUI::TextureHandle BuGUICreateTexture(int w, int h, const unsigned char* rgba);
void                 BuGUIDestroyTexture(BuGUI::TextureHandle handle);
```

**Ciclo de render (Device):**

```
Device::Run() →
  BuGUIBegin()           ← antes de qualquer widget call
  [app faz widgets]
  BuGUIEnd()             ← depois de ImGuiEnd() para não colidir com depth/blend state
  Flip()
```

**Routing de eventos SDL → BuGUI IO:**

Dentro de `BuGUIBegin()`, processar `SDL_PollEvent` em paralelo com o input existente e preencher:
```cpp
BuGUI::IO& io = BuGUI::GetIO();
io.mousePos    = {mx, my};
io.mouseButtons[0..2] = ...;
io.mouseWheel  = ...;
io.displayW / io.displayH = ...;
io.deltaTime   = ...;
io.keyChar     = ...;   // SDL_TEXTINPUT
io.keys[...]   = ...;   // SDL_KEYDOWN/UP
```

> **Nota:** O `Device::PollEvents` já consume os eventos SDL. A solução é processar BuGUI IO dentro do mesmo loop de eventos, antes de devolver ao caller.

---

### Fase 3 — CMakeLists: ligar bugui_widgets ao core

Em `core/CMakeLists.txt`:

```cmake
target_link_libraries(core
    imgui
    bugui_widgets          # ← adicionar
    OpenGL::GL
    ${SDL2_LIBRARIES}
    glm::glm
)

target_include_directories(core PUBLIC
    include src
    ${CMAKE_SOURCE_DIR}/vendor/widgets/include  # ← adicionar
    ${CMAKE_SOURCE_DIR}/vendor/imgui
)
```

Em `CMakeLists.txt` raiz, garantir que `bugui_widgets` é construído antes de `core`:
```cmake
add_subdirectory(vendor/widgets)
add_subdirectory(core)
```

---

### Fase 4 — Coexistência ImGui / BuGUI (opcional)

Durante a migração, as duas libs coexistem no mesmo frame:

```
BuGUIBegin()
ImGuiBegin()          ← Device::ImGuiBegin()
  [ImGui widgets]
ImGuiEnd()            ← Device::ImGuiEnd() — render ImGui
  [BuGUI widgets]
BuGUIEnd()            ← render BuGUI por cima
Flip()
```

ImGui e BuGUI têm os seus próprios VAO/shader — não interferem. Ambos fazem blend sobre o framebuffer.

---

### Fase 5 — Migração de ferramentas (futuro)

Substituir widgets ImGui no editor e demos pelos equivalentes BuGUI:

| ImGui widget | BuGUI equivalente |
|---|---|
| `ImGui::Begin/End` | `DockPanel` / `FloatWindow` |
| `ImGui::InputText` | `TextInputWidgets` |
| `ImGui::TreeNode` | `TreePropertyColorWidgets` |
| File dialog | `FileDialog` |
| Console | `ConsoleWidget` |
| Property grid | `DataWidgets` |
| Node editor | `NodeEditor` |
| Timeline | `Timeline` |

---

## Dependências e Riscos

| Risco | Mitigação |
|---|---|
| SDL event loop consumido duas vezes | Processar BuGUI IO dentro do loop de eventos do `Device`, não em separado |
| GL state (blend, scissor, VAO) corrompido entre ImGui e BuGUI | Cada renderer salva/restaura o seu state (`glPushAttrib` ou save/restore manual) |
| BuGUI usa `TextureHandle` (uint32) — colide com `Texture*` do core | São sistemas isolados; BuGUI não usa `TextureManager`. Gerir texturas BuGUI via `BuGUICreateTexture` no `Device` |
| PCH conflicts (`core` usa `src/pch.h`, `bugui_widgets` usa `src/pch.hpp`) | São PCHs de targets distintos — sem conflito. `core` inclui headers BuGUI normalmente |

---

## Ordem de implementação recomendada

1. `BuGUIRenderer.hpp/.cpp` — testar o renderer isolado com um mini-demo
2. Adicionar `BuGUIInit/Begin/End/Shutdown` ao `Device` 
3. Atualizar `CMakeLists.txt` (core + raiz)
4. Testar num demo simples: `demos/src/` com um botão BuGUI no ecrã
5. Migrar progressivamente o editor
