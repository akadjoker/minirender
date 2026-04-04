# Pipeline E Scripting

## Problema Real

O sistema actual de passes em C++ funciona para demos, mas e demasiado baixo nivel para scripting.

Se expores directamente `Technique`, `RenderPass`, `Shader`, `GLenum`, FBOs e estados de blend ao Bulang, vais criar:

- scripts dificeis de manter
- dependencias directas de OpenGL na linguagem de jogo
- demos que deixam de ser portaveis entre pipelines
- muita fragilidade quando mudares o renderer

O erro seria tentar fazer o script montar o renderer inteiro ao nivel do motor.

## Regra Principal

O script nao deve construir passes low-level.

O script deve montar cenas e escolher configuracoes de pipeline.

Em termos praticos:

- C++ define como o renderer funciona
- dados definem quais os blocos activos
- Bulang controla comportamento, spawn, cameras, luzes, materiais e troca de perfil visual

## Arquitectura Recomendada

Usa 3 camadas.

### 1. Renderer Core

Fica em C++.

Responsabilidade:

- gerir render targets
- executar passes
- ordenar dependencias
- fazer resize
- gerir shaders e recursos GPU
- decidir forward, deferred, shadow, postprocess

Tipos recomendados:

```cpp
struct PassDesc {
    std::string name;
    std::string type;
    bool enabled = true;
    std::string inputColor;
    std::string inputDepth;
    std::string outputTarget;
    std::unordered_map<std::string, Variant> params;
};

struct PipelineDesc {
    std::string name;
    std::vector<PassDesc> passes;
};

class SceneRenderer {
public:
    void setPipeline(const PipelineDesc& desc);
    void render(Scene& scene, Camera& camera);
};
```

Aqui podes continuar a usar internamente `RenderPass`, `Technique` e `Pipeline`, mas isso deixa de ser a API publica para scripts.

### 2. Pipeline Presets

Tambem podem ser definidos em C++, ou carregados de ficheiro, mas num formato declarativo simples.

Exemplos de presets:

- `forward_basic`
- `forward_pbr`
- `forward_pbr_csm`
- `deferred_pbr`
- `water_reflection`
- `editor_view`

O script apenas pede um preset e altera parametros.

Exemplo mental:

```text
demo usa pipeline "forward_pbr_csm"
editor usa pipeline "editor_view"
mobile usa pipeline "forward_basic"
```

### 3. Gameplay / Demo Script

Bulang fica nesta camada.

Responsabilidade:

- carregar cenas
- criar entidades
- ligar animacoes
- trocar materiais
- escolher camara activa
- activar um preset de renderer
- alterar parametros globais como exposure, fog, bloom

Bulang nao sabe o que e um FBO. So sabe o que e um pipeline preset.

## Modelo Mental Certo

Nao penses em:

- "como o script cria todos os passes?"

Pensa em:

- "como o script escolhe um perfil de renderer e muda opcoes?"

Isto e a diferenca entre uma API de engine e uma API de GPU.

## O Que Expor Ao Bulang

Expoe uma API pequena e estavel.

### Renderer

```bulang
var renderer = Engine.renderer();
renderer.setPipeline("forward_pbr_csm");
renderer.setExposure(1.1);
renderer.setBloom(1);
renderer.setBloomStrength(0.25);
renderer.setFog(1);
renderer.setFogColor(0.52, 0.61, 0.70);
renderer.setFogRange(30.0, 180.0);
```

### Scene

```bulang
var scene = Scene();
var cam = scene.createCamera("main");
var sun = scene.createDirectionalLight("sun");
var node = scene.createMeshNode("ogre", "assets/models/ogre.glb");
```

### Material Overrides

```bulang
var mat = node.getMaterial(0);
mat.setFloat("roughness", 0.35);
mat.setFloat("metallic", 0.0);
mat.setTexture("albedo", "assets/textures/ogre.png");
```

### Optional Pipeline Params

```bulang
renderer.setPipelineParam("csm.splitLambda", 0.72);
renderer.setPipelineParam("shadow.size", 2048);
renderer.setPipelineParam("ssao.radius", 0.8);
```

O script ve nomes estaveis. O motor traduz isso para uniforms, buffers, FBOs e passes.

## O Que Nao Expor Ao Bulang

Nao exponhas isto numa primeira fase:

- `RenderPass*`
- `Technique*`
- `Shader*`
- `GLuint`
- enums GL de blend, cull e depth
- alocacao manual de render targets
- dependencia explicita entre passes

Isso e API interna do renderer.

## Como Organizar Os Demos

Um demo nao deve ser um renderer diferente. Um demo deve ser uma combinacao de:

- cena
- script de comportamento
- preset de pipeline
- UI de debug opcional

Estrutura recomendada:

```text
Demo = SceneSetup + ControllerScript + PipelinePreset
```

Exemplos:

- `CarShowroom = showroom.scene + showroom_controller.bu + forward_pbr_csm`
- `Sponza = sponza.scene + freecam.bu + deferred_pbr`
- `Water = water.scene + water_test.bu + forward_pbr_csm`

Assim deixas de duplicar codigo de renderer entre demos.

## Pipeline Base Recomendado

Para nao te perderes, escolhe um pipeline principal e so depois adiciona variantes.

### Fase 1

`forward_pbr_csm`

Passes:

1. sky
2. shadow_csm
3. opaque_pbr
4. transparent_forward
5. overlay

Este deve ser o pipeline default do engine.

### Fase 2

`forward_pbr_csm_post`

Passes:

1. sky
2. shadow_csm
3. opaque_pbr_hdr
4. transparent_forward_hdr
5. bloom_extract
6. bloom_blur
7. tonemap
8. overlay

### Fase 3

`deferred_pbr`

Passes:

1. gbuffer_pbr
2. deferred_light
3. transparent_forward
4. tonemap
5. overlay

Deferred deve ser variante, nao o centro da arquitectura inteira.

## Adaptacao Do Codigo Actual

Hoje tens isto:

- `RenderPass`
- `Technique`
- `Pipeline`
- demos a montar tecnicas directamente

Isso pode continuar, mas so como backend.

O passo seguinte e adicionar uma camada acima.

### Nova Camada

```cpp
class RendererFacade {
public:
    bool loadPipelinePreset(const std::string& name);
    void setGlobalFloat(const std::string& name, float value);
    void setGlobalVec3(const std::string& name, const glm::vec3& value);
    void render(Scene& scene, Camera& camera);
};
```

Internamente:

- `forward_pbr_csm` cria `Technique` e `Pass` concretos
- `setGlobalFloat("exposure")` actualiza contexto global do renderer
- o script nunca toca nos detalhes internos

## Estado Global Do Renderer

Cria um bloco unico de configuracao global.

```cpp
struct RendererGlobals {
    float exposure = 1.0f;
    float bloomStrength = 0.0f;
    float fogEnabled = 0.0f;
    glm::vec3 fogColor = {0.5f, 0.6f, 0.7f};
    glm::vec2 fogRange = {30.0f, 150.0f};
    float shadowBias = 0.0025f;
    float csmLambda = 0.7f;
};
```

Este bloco e muito melhor para scripting do que dezenas de uniforms espalhados.

## Como O Script Deve Interagir

### Bom

```bulang
renderer.setPipeline("forward_pbr_csm_post");
renderer.setExposure(1.2);
renderer.setBloom(1);
renderer.setPipelineParam("shadow.size", 2048);
```

### Mau

```bulang
var pass = RenderPass();
pass.setBlendSrc(GL_SRC_ALPHA);
pass.setFramebuffer(myFbo);
pass.shader = Shader("...");
```

Isto ultimo vai transformar o scripting numa extensao insegura do backend OpenGL.

## Roadmap Minimo

### Passo 1

Define 1 pipeline canonico:

- `forward_pbr_csm`

### Passo 2

Cria `RendererGlobals` e passa tudo por la.

### Passo 3

Cria `RendererFacade` para esconder `Technique` e `RenderPass`.

### Passo 4

Expoe ao Bulang apenas:

- `renderer.setPipeline(name)`
- `renderer.setExposure(value)`
- `renderer.setBloom(enabled)`
- `renderer.setPipelineParam(name, value)`
- API de cena e materiais

### Passo 5

Passa os demos para este modelo:

- setup de cena
- pipeline preset
- controller script

## Decisao Pratica

Se queres rapidez e pouca dor, segue esta regra:

- pipeline low-level em C++
- presets em dados
- gameplay e orchestration em Bulang

Essa separacao vai deixar-te:

- fazer varios demos sem duplicar renderer
- trocar pipeline sem reescrever scripts
- manter o motor evolutivo
- usar Bulang para o que interessa mesmo

## Formula Simples

Usa esta formula como guia:

```text
Script escolhe o que quer
Preset descreve o renderer
C++ executa como isso acontece
```

Se o script estiver a decidir como criar FBOs e passes, a fronteira esta mal desenhada.