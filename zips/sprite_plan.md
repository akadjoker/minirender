# Sprite Generator Plan

## Goal

Create a sprite generation tool that uses the engine's animated model formats to render sprite frames and build atlases.

The tool should support:
- animated models
- frame range export
- render-to-texture capture
- sprite sheet / atlas generation
- metadata export
- view presets
- per-surface texture overrides
- attachments such as weapons

This is a better short-term target than continuing the brush editor work, because it is more concrete and directly reuses the runtime and animation systems already in the engine.

## Core Idea

Load a model, choose an animation and frame range, render each frame into an offscreen target, save the images, and optionally pack them into an atlas with JSON metadata.

The user should be able to:
- choose a model
- choose an animation clip or frame range
- define output size
- choose one or more camera views
- tweak orientation / scale / pivot
- override textures per surface
- attach secondary models
- export frames and atlas

## Target Formats

### First-class formats for MVP

- `H3D`
- `B3D`
- `IQM`

These should all be first-class in the MVP because, in the engine, they already converge into the same mesh / skeleton / joints representation.

That means the sprite generator should not be designed around file-format-specific logic for these formats. It should be designed around the shared animated mesh abstraction.

### Second wave

- `MD3`
- `MD2`

These need more special handling because of tags, split models, and legacy animation conventions.

## Important Format Notes

### H3D / B3D / IQM

For the purposes of the sprite generator MVP, these should be treated as one family:
- same runtime mesh representation
- same joints / skeleton workflow
- same preview and render path
- same export pipeline

The tool should load them through a common animated-model layer instead of branching the whole app by file format.

### MD3

Important because of tag-based attachments:
- body/head/legs style setups
- weapon model on a tag
- needs attachment UI

Planned features:
- attach model to tag
- optional local rotation / offset / scale
- separate preview of attachment hierarchy

### MD2

Needs a custom workflow:
- frame-based animation
- often uses main model + weapon model
- may need two models rendered together

Planned features:
- primary model
- optional secondary model
- separate transform for secondary model
- optional synchronized playback

## MVP Scope

The MVP should already be useful in production.

### MVP Features

1. Load one animated model
2. Choose animation clip or frame range
3. Choose output sprite size
4. Render transparent sprites to offscreen target
5. Export individual PNG frames
6. Export atlas PNG
7. Export JSON metadata
8. Support 1 to 3 views
9. Preview animation before export

### MVP Model Support

- H3D
- B3D
- IQM

### MVP Views

- Front
- Side
- Top
- Custom

## Main User Workflow

1. Load model
2. Choose animation
3. Set start and end frame
4. Set FPS / sampling
5. Choose sprite size
6. Choose one or more views
7. Adjust orientation / zoom / pivot
8. Preview result
9. Export frames
10. Export atlas and JSON

## Tool Structure

Recommended architecture:

### 1. SpriteGeneratorData

Holds editable state:
- model path
- animation selection
- frame range
- output resolution
- atlas settings
- view presets
- texture overrides
- attachment settings

### 2. SpriteGeneratorRenderer

Responsible for:
- offscreen render target
- preview scene
- camera setup
- transparent background capture
- per-frame rendering

### 3. SpriteGeneratorExporter

Responsible for:
- writing PNG frame files
- packing frames into atlas
- writing metadata JSON

### 4. SpriteGeneratorUI

Responsible for:
- model / animation panel
- preview panels
- output settings
- surface override UI
- attachment UI
- export actions

## UI Proposal

### Left Panel

`Asset / Model`
- model file
- animation list
- playback controls
- start / end frame
- fps

`Views`
- enable front
- enable side
- enable top
- custom camera
- yaw / pitch / roll
- zoom
- pivot offset

### Center

Live preview:
- main preview
- optional multiple previews for active views

### Right Panel

`Surfaces`
- list surfaces / meshes
- hide / show
- change texture
- reset texture override

`Attachments`
- attach secondary model
- choose bone or tag
- primary transform
- secondary transform for weapon alignment
- gizmo editing in preview

`Export`
- frame size
- padding
- trim / crop
- atlas toggle
- output folder
- export button

## View System

The tool should support multiple output directions.

### Initial view presets

- Front
- Back
- Left
- Right
- Top
- Custom

### Useful settings per view

- yaw
- pitch
- roll
- distance or ortho size
- pivot offset
- enabled / disabled

### Recommended initial implementation

Start with:
- Front
- Side
- Top
- Custom

## Surface Overrides

This is important for character variation.

The tool should allow:
- selecting a surface
- changing its texture
- hiding a surface
- resetting to default

Examples:
- different head texture
- different chest texture
- hide helmet / shoulder piece

This is especially useful for:
- H3D
- B3D
- IQM
- MD3

## Attachments

### MD3 attachment flow

Use tags directly:
- attach weapon to tag
- apply local transform offsets
- preview final combined setup

### MD2 and generic attachment flow

Allow a secondary model:
- second model loaded at same time
- own transform
- optional sync to current frame or animation

This also helps with:
- held weapons
- backpacks
- test props

### Weapon attachment requirements

This is important enough to be treated as a first-class design constraint.

The sprite generator should support:
- selecting the target bone or tag for the weapon
- editing the weapon placement visually
- having two transforms when needed

Recommended attachment model:
- attachment anchor transform
- weapon local transform

This gives enough control for:
- matching hand position
- compensating format differences
- fixing weapon angle without changing the source asset
- handling cases where the same weapon needs slightly different alignment per character

Recommended UI for weapon attachment:
- attachment target dropdown: bone or tag
- numeric transform fields
- gizmo in preview
- reset transform
- optional save as preset

## Rendering Requirements

The renderer should support:
- transparent background
- fixed-size output
- render target capture
- optional lighting presets
- stable framing across animation frames

Important requirement:
- avoid sprite jitter between frames

This means we should think carefully about:
- model centering
- pivot/origin
- optional fixed bounding box across the animation

## UI / Editing Widgets

The sprite generator should not rely only on basic ImGui controls.

Useful widget integrations:
- `ImGuizmo`
- `ImSequencer`
- `ImCurveEditor`
- `ImGradient`
- `ImZoomSlider`

### Planned uses

`ImGuizmo`
- edit weapon transforms in the preview
- edit pivot / origin / custom attachment transforms

`ImSequencer`
- define animation export ranges
- show start / end / current frame
- support multiple export segments later if needed

`ImCurveEditor`
- optional per-export curves
- camera tweaks
- opacity or effect curves later if ever needed

`ImGradient`
- background gradient presets
- optional stylized export backgrounds if transparent output is disabled

`ImZoomSlider`
- timeline zoom
- frame range navigation
- atlas/frame preview zoom controls

## Export Requirements

### Individual frames

Output example:

- `walk_000.png`
- `walk_001.png`
- `walk_002.png`

### Atlas

Output example:

- `walk_atlas.png`

### Metadata

Output example:

- `walk_atlas.json`

Suggested JSON fields:

- animation name
- fps
- frame count
- frame width / height
- pivot
- atlas rect per frame
- view name
- optional bounding box

Example structure:

```json
{
  "animation": "walk",
  "fps": 12,
  "frameSize": [128, 128],
  "pivot": [64, 112],
  "frames": [
    { "atlas": [0, 0, 128, 128] },
    { "atlas": [128, 0, 128, 128] }
  ]
}
```

## Recommended Development Phases

### Phase 1: MVP

- new sprite generator tool/app
- H3D / B3D / IQM support through the same animated mesh path
- single model
- animation selection
- frame range
- offscreen render
- PNG frame export
- atlas export
- JSON metadata export

### Phase 2: Materials

- per-surface texture override
- surface visibility toggles

### Phase 3: Attachments

- secondary model support
- attachment transforms
- bone / tag selection
- weapon gizmo editing
- MD3 tag attachments

### Phase 3.5: Advanced UI widgets

- integrate `ImGuizmo`
- integrate `ImSequencer`
- integrate `ImCurveEditor`
- integrate `ImGradient`
- integrate `ImZoomSlider`

### Phase 4: Legacy format handling

- MD2 dual-model workflow
- MD3 full setup support

### Phase 5: Advanced features

- multi-direction export
- better crop / trim
- outline / stylized render pass
- shadow options
- batch export presets

## Technical Risks

### Animation frame stability

Potential issue:
- model moves too much during animation
- sprite alignment jitters between frames

Need:
- fixed pivot or stable root handling

### Multiple format differences

Potential issue:
- each format has different animation / attachment semantics

Need:
- common abstraction layer for playback
- special-case adapters only where necessary

Note:
- `H3D`, `B3D`, and `IQM` should share the same animated mesh abstraction in the MVP
- `MD3` and `MD2` are the real special cases

### Atlas packing

Potential issue:
- inconsistent sprite bounds
- wasted atlas space

Need:
- simple packer first
- smarter packing later

## Recommendation

Do not start with every format at once.

Best order:

1. build the tool around the shared animated mesh abstraction
2. get the full render/export loop working
3. add surface override UI
4. add attachments
5. then support `MD3` and `MD2`

This gives the fastest useful result with the least architectural risk.

## Immediate Next Step

Create the technical implementation plan for the MVP:

1. choose whether this is a new app or an editor mode
2. define the main classes and files
3. define the export pipeline
4. define the preview UI layout
5. implement `H3D/B3D/IQM -> shared animated mesh -> render target -> PNG/atlas/json`
