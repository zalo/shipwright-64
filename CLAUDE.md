# Shipwright-64: Mario in Ocarina of Time

## What This Project Is

A fork of Ship of Harkinian (Zelda: Ocarina of Time web build) that integrates libsm64 to replace Link with a fully playable Super Mario 64 character. Mario uses his authentic SM64 physics, animations, and geometry while navigating the OoT world.

**Web-only (Emscripten/WASM).** No native build support is planned or needed.

**Two ROMs required at runtime:**
- `Legend of Zelda, The - Ocarina of Time (U) (v1.2).z64` — provides all OoT game assets
- `Super Mario 64 (U) [!].z64` — provides Mario's geometry, textures, and animation data via libsm64

## Repository Structure

```
shipwright-64/
├── CLAUDE.md                      ← you are here
├── PLAN.md                        ← detailed implementation plan
├── CMakeLists.txt                 ← root CMake (delegates to soh/)
├── build_web.sh                   ← Emscripten build script
├── libsm64/                       ← git submodule (libsm64 C library)
├── soh/                           ← forked Ship of Harkinian
│   ├── CMakeLists.txt             ← modified to include libsm64
│   └── soh/
│       ├── Mario/                 ← NEW: all Mario integration code
│       │   ├── MarioManager.cpp/h     ← top-level coordinator
│       │   ├── MarioActor.cpp/h       ← OoT Actor wrapper
│       │   ├── MarioCollision.cpp/h   ← OoT→SM64 collision bridge
│       │   ├── MarioController.cpp/h  ← input mapping
│       │   └── MarioRenderer.cpp/h    ← OpenGL ES 2.0 mesh renderer
│       └── web/
│           └── shell.html             ← modified: SM64 ROM picker added
└── libultraship/                  ← unchanged upstream library
```

## Key Reference Directories

- `~/Desktop/Shipwright/` — upstream Ship of Harkinian (Emscripten fork), do not modify
- `~/Desktop/MarioWorkspace/` — SM64 research and reference implementations

## Architecture Overview

### How It Works

1. **Dual ROM loading**: The web shell loads both ROMs. The OoT ROM goes through the normal OTR extraction pipeline. The SM64 ROM is passed raw to `sm64_global_init()`.

2. **libsm64 as physics/animation engine**: Every game frame, `sm64_mario_tick()` is called with the current controller input. It returns Mario's new position/velocity and a triangle mesh for rendering.

3. **Link as game logic proxy**: Link (the OoT player actor) remains alive and handles all game logic — triggers, doors, item collection, cutscenes, dialogue. Link is made invisible. Mario's world position is synced to/from Link's position each frame.

4. **OoT collision fed to libsm64**: When a scene/room loads, its `CollisionHeader` polygon data is converted to `SM64Surface` arrays and loaded into libsm64 via `sm64_static_surfaces_load()`. This gives Mario authentic SM64 physics (slopes, ledge grabs, etc.).

5. **Mario rendered via direct OpenGL ES 2**: libsm64 outputs a dynamic triangle mesh each frame. A custom GLSL shader renders this after OoT's F3D render pass.

### Coordinate System

Both OoT and SM64 use Y-up, Z-forward coordinate systems. Units are compatible (both N64 games use similar world scales). A scale constant `MARIO_WORLD_SCALE` (default 1.0f) is available for tuning.

### Input Mapping

| OoT Input | SM64 Action |
|-----------|-------------|
| Left stick | `stickX` / `stickY` (camera-relative) |
| A button | Jump |
| B button | Attack (punch/kick) |
| Z trigger | Crouch / ground-pound prep |
| R button | Slide / dive |

Camera forward direction is extracted from OoT's camera system to compute stick-relative movement, exactly as SM64 does.

## Build Instructions

```bash
# Prerequisites: Emscripten SDK activated, cmake, python3

# Initialize libsm64 submodule
git submodule update --init --recursive

# Build
./build_web.sh

# Serve with required COOP/COEP headers
python3 serve_web.py
```

The build output is `build-web/soh.html` (plus `.js`, `.wasm`, `.data`).

## libsm64 Key API

```c
// Initialize: call once after SM64 ROM is loaded into memory
void sm64_global_init(const uint8_t *rom, uint8_t *outTexture);
// rom: full SM64 ROM bytes (8MB)
// outTexture: caller-allocated SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT * 4 bytes (RGBA)

// Load static collision geometry for current scene
void sm64_static_surfaces_load(const struct SM64Surface *surfaces, uint32_t numSurfaces);

// Create Mario instance, returns ID
int32_t sm64_mario_create(int16_t x, int16_t y, int16_t z);

// Tick Mario physics (call every frame at ~20Hz SM64 rate or 60Hz with interpolation)
void sm64_mario_tick(
    int32_t marioId,
    const struct SM64MarioInputs *inputs,    // joystick + buttons
    struct SM64MarioState *outState,         // position, velocity, health, action
    struct SM64MarioGeometryBuffers *outGeometry  // triangle mesh for this frame
);

// Cleanup
void sm64_mario_delete(int32_t marioId);
void sm64_global_terminate();
```

## Critical Implementation Notes

### libsm64 Emscripten Compilation
- libsm64 is pure C (C99), compiles cleanly with Emscripten
- Disable threading: `-DUSE_PTHREAD=0` or equivalent
- Must compile with `-DVERSION_US=1` for US ROM support
- Link as a static library: `libsm64.a` then link into the main WASM binary

### SM64 Tick Rate vs OoT Frame Rate
- SM64 runs at 30Hz game logic (20Hz physics internally)
- OoT runs at 20Hz (N64) or uncapped on web
- Solution: call `sm64_mario_tick()` at a fixed 30Hz via an accumulator, interpolate render position between ticks

### OoT Collision Conversion
OoT `CollisionPoly` stores vertex indices into a separate vertex array. The converter must:
1. Dereference vertex indices from `colHeader->vertices[poly.vi[0/1/2]]`
2. Convert `Vec3s` (s16 x3) to `SM64Surface vertices[3][3]` (s16 x3 x3)
3. Map OoT surface types to SM64 terrain types (see `MarioCollision.cpp`)

Dynamic collision (moving platforms, pushable blocks) must be updated each frame via `sm64_surface_object_*` API.

### Rendering in OoT's Pipeline
OoT uses F3D display lists processed by libultraship's renderer. Mario's mesh bypasses this entirely:
- Hook into `gfx_run()` post-call via a render hook in libultraship
- Issue raw OpenGL ES 2.0 calls to draw Mario's VBO
- Mario's texture atlas is uploaded once as a GL texture at init time

### CVar: Mario Mode Toggle
- `gMarioModeEnabled` (bool CVar) — when false, Link renders and controls normally
- Toggle via the enhancement menu, keyboard shortcut (default: M key), or URL parameter

## Common Gotchas

- **libsm64 needs the full SM64 ROM**, not a truncated or modified version. Reject invalid ROMs with a clear error message in the UI.
- **SM64 surface load clears all previous surfaces** — must reload every scene change.
- **Mario health in SM64 is measured in wedges** (8 wedges = full health). Map to OoT hearts display separately; Mario's HP is cosmetic — OoT's Link health governs actual death.
- **Cutscene mode**: During OoT cutscenes (`gSaveContext.cutsceneIndex > 0`), disable SM64 input and sync Mario's position to Link directly without ticking physics.
- **Scene transitions**: Delete and recreate the SM64 Mario instance on scene change to avoid stale physics state.
