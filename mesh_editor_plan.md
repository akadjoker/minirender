# Mesh Editor Plan

## Goal

Build a new level editor around an editable mesh model instead of the current legacy brush system.

The editor should support:

- direct vertex / edge / face editing
- object transforms
- grid and snapping
- face extrusion and clipping
- entity placement with properties
- optional CSG on top of editable geometry

This is a better fit for the kind of maps we want to build:

- ramps
- stairs
- beveled corners
- sloped walls
- custom openings
- reusable gameplay geometry

## Core Direction

Do not keep expanding the current mixed brush system.

Instead, define the editor around:

1. `EditableMesh`
2. `MeshObject`
3. `EntityObject`
4. mesh editing tools
5. export / bake pipeline

This means geometry is always editable mesh data first.

Brush-like behavior can still exist, but as tools or primitives that generate editable mesh, not as the main architecture.

## Main Architecture

### 1. EditableMesh

Owns the geometry data in editor form.

Suggested data:

- vertices
- edges
- faces
- face material assignment
- face UV transform
- selection flags

Suggested operations:

- add/remove vertex
- add/remove face
- rebuild edges
- triangulate for rendering
- compute normals
- compute bounds

Important:

- keep editor topology separate from runtime render mesh
- generate runtime mesh from editable mesh when needed

### 2. MeshObject

Scene object that owns one `EditableMesh` plus transform and editor state.

Suggested fields:

- name
- transform
- editable mesh
- visibility
- lock/freeze flags
- material defaults

### 3. EntityObject

Scene object for gameplay and logic elements.

Examples:

- `player_start`
- `light`
- `door`
- `elevator`
- `trigger`
- `spawn_point`

Suggested fields:

- entity class name
- transform
- key/value properties
- optional preview mesh / icon

### 4. EditorScene

Owns all editable scene objects.

Suggested content:

- mesh objects
- entity objects
- selection state
- undo/redo stack
- file path / dirty state

## Editing Modes

The editor should support explicit selection/edit modes:

- object mode
- vertex mode
- edge mode
- face mode
- entity mode

This is much stronger than the current tool mix because the whole editor behavior becomes predictable.

## First Tool Set

These should be the first real tools:

1. select
2. move
3. rotate
4. scale
5. extrude face
6. clip by plane
7. split edge
8. create primitive

### Primitive Creation

Primitives should only be starting points:

- box
- plane
- cylinder
- wedge
- stairs generator

After creation they become normal editable mesh.

This is the key difference from the failed brush direction.

## Viewports

Keep the multi-view editor style because it helps level design:

- top
- front/back
- left/right
- 3D perspective

### View Behavior

- each viewport keeps its own camera state
- 2D views are orthographic
- 3D view uses orbit / fly controls
- tools act consistently in all views

### Gizmos

Useful for:

- object transform
- entity transform

But do not depend on gizmos for mesh editing.

Vertex/edge/face manipulation must work even without fancy gizmos.

## Mesh Editing Operations

### MVP Operations

- move selected vertices
- move selected faces
- extrude selected face
- delete face
- split edge
- bridge faces later
- clip mesh with plane

### After MVP

- inset face
- bevel edge
- weld vertices
- dissolve edge
- merge faces
- duplicate faces
- flip normals

## CSG Strategy

CSG should not be the foundation.

It should be an optional higher-level operation over clean mesh data.

Recommendation:

- start without full live CSG
- build robust editable mesh first
- later add:
  - union
  - subtract
  - intersect
  - hollow

If we do CSG too early, we risk repeating the same trap as the old brush system.

## Entity System

Entities should be simple and powerful.

Recommended first classes:

- `player_start`
- `light_point`
- `light_spot`
- `door_sliding`
- `elevator_platform`
- `trigger_box`

### Property Editing

Each entity should expose:

- name
- class
- transform
- editable properties

Example properties:

- speed
- target
- wait_time
- color
- intensity
- radius

## Rendering Model

Editor rendering should be split into layers:

1. mesh solid view
2. wireframe overlay
3. selection overlay
4. entity icons / helpers
5. gizmos / guides / grid

### Important

We need visual helpers for non-mesh objects:

- light radius / cone
- trigger boxes
- player start marker
- path / target links

## File Format

We need a scene format that stores editor-native data, not just baked mesh.

Suggested content:

- mesh objects with editable topology
- entity objects with properties
- materials
- per-face UV settings
- editor metadata

This allows reopening and continuing work safely.

## Undo / Redo

Must exist from the start for all editing actions.

Recommended command categories:

- transform object
- create/delete object
- edit vertices
- edit faces
- assign material
- create/delete entity
- modify entity property

## New Project Structure

Create a new project instead of extending the old editor further.

Suggested modules:

- `meshedit/src/MeshEditorApp.*`
- `meshedit/src/MeshEditorScene.*`
- `meshedit/src/EditableMesh.*`
- `meshedit/src/MeshEditorSelection.*`
- `meshedit/src/MeshEditorCommands.*`
- `meshedit/src/MeshEditorRender.*`
- `meshedit/src/MeshEditorIO.*`
- `meshedit/src/MeshEditorTheme.*`

Optional later:

- `meshedit/src/MeshEditorCSG.*`
- `meshedit/src/MeshEditorEntities.*`

## Recommended Development Phases

### Phase 1: Foundation

- new app shell
- theme
- layout
- 4 views
- editor scene
- editable mesh data model
- object selection

### Phase 2: Basic Editing

- create box
- vertex selection
- face selection
- move / rotate / scale object
- move vertices / faces

### Phase 3: Real Mesh Tools

- extrude face
- clip plane
- split edge
- delete face
- recalc normals / triangulation

### Phase 4: Entities

- entity objects
- entity list
- property panel
- debug drawing in views

### Phase 5: Serialization

- save/load editor-native scene
- undo/redo integration

### Phase 6: Advanced Ops

- CSG boolean
- stairs generator
- bevel/inset
- material tools
- UV tools

## MVP Definition

The MVP is successful when we can:

1. create a mesh object
2. select vertices and faces
3. extrude a face into a ramp-like shape
4. place a `player_start`
5. place a light
6. save and reopen the scene

If we can do that, the editor is already more useful than the current brush path.

## Recommendation

The best next move is:

1. create the new project shell
2. define `EditableMesh`
3. implement multi-view layout
4. get object / vertex / face selection working
5. make one primitive editable immediately

Do not start with CSG.
Do not start with many primitive types.
Do not start by porting the whole old editor.

Start with one clean vertical slice that proves the architecture.
