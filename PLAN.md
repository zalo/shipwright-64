# Implementation Plan: Mario in Ocarina of Time (Web Build)

## Overview

Integrate Super Mario 64's character (physics, animations, geometry) into Ship of Harkinian's web build using libsm64. Mario replaces Link visually and physically while Link remains as the game logic proxy for OoT interactions.

**Stack**: C/C++ → Emscripten/WASM → WebGL2/OpenGL ES 2.0 → browser

---

## Phase 0: Repository Setup

### 0.1 Fork and Initialize

```bash
# In ~/Desktop/
cp -r Shipwright shipwright-64
cd shipwright-64
git init
git add -A && git commit -m "Initial fork of Shipwright web build"

# Add libsm64 as submodule
git submodule add https://github.com/libsm64/libsm64.git libsm64
git submodule update --init --recursive
```

### 0.2 Verify Upstream Build Still Works

```bash
./build_web.sh
python3 serve_web.py
# Confirm OoT loads and plays normally before any changes
```

### 0.3 Create Project Directory Structure

```bash
mkdir -p soh/soh/Mario
touch soh/soh/Mario/MarioManager.{cpp,h}
touch soh/soh/Mario/MarioActor.{cpp,h}
touch soh/soh/Mario/MarioCollision.{cpp,h}
touch soh/soh/Mario/MarioController.{cpp,h}
touch soh/soh/Mario/MarioRenderer.{cpp,h}
```

---

## Phase 1: libsm64 Emscripten Build

### 1.1 Understand libsm64 Build System

libsm64 uses a simple Makefile. Key files:
- `libsm64/src/` — core SM64 physics/animation C code
- `libsm64/include/libsm64.h` — public API header
- `libsm64/Makefile` — builds `dist/libsm64.so` or `dist/libsm64.a`

### 1.2 Create libsm64 CMakeLists.txt Wrapper

Create `libsm64/CMakeLists.txt` (or add directly to `soh/CMakeLists.txt`):

```cmake
# In soh/CMakeLists.txt, within the BUILD_FOR_WEB block:

# --- libsm64 ---
file(GLOB_RECURSE LIBSM64_SOURCES
    "${CMAKE_SOURCE_DIR}/libsm64/src/*.c"
)

add_library(sm64 STATIC ${LIBSM64_SOURCES})

target_include_directories(sm64 PUBLIC
    "${CMAKE_SOURCE_DIR}/libsm64/include"
    "${CMAKE_SOURCE_DIR}/libsm64/src"
)

target_compile_definitions(sm64 PRIVATE
    VERSION_US=1         # US ROM support
    NO_SEGMENTED_MEMORY=1  # Required for non-N64 platforms
)

target_compile_options(sm64 PRIVATE
    -w                   # suppress all warnings (decompiled N64 code)
    -O2
)

# Link sm64 into the main soh target
target_link_libraries(${PROJECT_NAME} PRIVATE sm64)
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_SOURCE_DIR}/libsm64/include"
)
```

### 1.3 Verify libsm64 Compiles

```bash
./build_web.sh 2>&1 | grep -i "sm64\|error" | head -40
```

Common issues and fixes:
- **`__builtin_expect` errors**: Add `-w` to suppress
- **`uint64_t` pointer size issues**: May need `#define AVOID_UB` or skip certain sm64 files that use 64-bit pointers
- **Threading**: libsm64 is single-threaded by default, no action needed

### 1.4 Exported Functions for SM64 ROM

Add to the linker `EXPORTED_FUNCTIONS` list in `soh/CMakeLists.txt`:

```cmake
_web_load_sm64_rom,_web_get_sm64_status,_web_sm64_init_done
```

---

## Phase 2: SM64 ROM Loading in Web Shell

### 2.1 Add SM64 ROM File Picker to shell.html

The existing shell.html has an OoT ROM picker. Add a parallel SM64 ROM picker.

**Location**: `soh/soh/web/shell.html`

```html
<!-- Add after the OoT ROM picker section -->
<div id="sm64-rom-section" style="display:none;">
  <h3>Super Mario 64 ROM (US version required)</h3>
  <p>Required for Mario's appearance, animations, and physics.</p>
  <input type="file" id="sm64-rom-picker" accept=".z64,.n64,.v64">
  <button onclick="loadSM64Rom()">Load SM64 ROM</button>
  <div id="sm64-status">Not loaded</div>
</div>
```

**JavaScript in shell.html**:

```javascript
async function loadSM64Rom() {
    const file = document.getElementById('sm64-rom-picker').files[0];
    if (!file) return;

    const buffer = await file.arrayBuffer();
    const data = new Uint8Array(buffer);

    // Validate: SM64 US ROM header
    // Byte-swap detection: check first 4 bytes
    // z64 (big-endian): 80 37 12 40
    // v64 (byte-swapped): 37 80 40 12
    // n64 (little-endian): 40 12 37 80

    const magic = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    if (magic !== 0x80371240) {
        // Try to byte-swap if needed
        // ... byte swap logic ...
    }

    // Write to Emscripten virtual filesystem
    FS.writeFile('/sm64.z64', data);

    // Check if already cached in IndexedDB
    localStorage.setItem('sm64_rom_loaded', '1');

    // Call C function to initialize libsm64
    const result = Module.ccall('web_load_sm64_rom', 'number',
        ['string', 'number'],
        ['/sm64.z64', data.byteLength]
    );

    if (result === 0) {
        document.getElementById('sm64-status').textContent = 'Mario loaded!';
        document.getElementById('sm64-rom-section').style.borderColor = 'green';
    } else {
        document.getElementById('sm64-status').textContent = 'Error: Invalid ROM';
    }
}

// Cache SM64 ROM in IndexedDB (alongside OoT assets)
async function cacheSM64Rom() {
    // Use same IDBFS pattern as OoT save data
    Module.ccall('web_save_to_idb', null, [], []);
}
```

### 2.2 C-side ROM Loading Function

In `soh/soh/web/web_main.cpp`:

```cpp
#include "Mario/MarioManager.h"

extern "C" {

// Called from JS after SM64 ROM is written to /sm64.z64
int web_load_sm64_rom(const char* romPath, int romSize) {
    FILE* f = fopen(romPath, "rb");
    if (!f) return -1;

    std::vector<uint8_t> romData(romSize);
    size_t read = fread(romData.data(), 1, romSize, f);
    fclose(f);

    if (read != (size_t)romSize) return -2;

    return MarioManager::Get()->InitFromRom(romData);
    // Returns 0 on success, negative on error
}

int web_get_sm64_status() {
    return MarioManager::Get()->IsReady() ? 1 : 0;
}

} // extern "C"
```

Export these from CMakeLists.txt `EXPORTED_FUNCTIONS`.

### 2.3 IndexedDB Caching for SM64 ROM

The SM64 ROM is large (~8MB). Cache it in IndexedDB using the existing IDBFS infrastructure:

```javascript
// In shell.html JS — persist SM64 ROM to IDBFS /Save directory
// This piggybacks on the existing web_save_to_idb() mechanism
// The SM64 ROM can be saved as /Save/sm64.z64 and reloaded on next visit

async function checkCachedSM64Rom() {
    // Try to load from IDBFS /Save/sm64.z64
    try {
        const data = FS.readFile('/Save/sm64.z64');
        if (data && data.length > 1024*1024) { // > 1MB sanity check
            FS.writeFile('/sm64.z64', data);
            Module.ccall('web_load_sm64_rom', 'number', ['string', 'number'],
                ['/sm64.z64', data.length]);
            return true;
        }
    } catch(e) {}
    return false;
}
```

---

## Phase 3: MarioManager — Core Coordinator

`MarioManager` is the singleton that owns the libsm64 state and coordinates all Mario subsystems.

### 3.1 MarioManager.h

```cpp
#pragma once
#include <vector>
#include <cstdint>
#include <libsm64.h>

// Forward declarations
struct PlayState;  // OoT game state

class MarioManager {
public:
    static MarioManager* Get();

    // Lifecycle
    int  InitFromRom(const std::vector<uint8_t>& romData);
    bool IsReady() const { return mInitialized; }
    void Shutdown();

    // Called each OoT game frame
    void OnGameFrame(PlayState* play);

    // Called when scene changes
    void OnSceneLoad(PlayState* play);
    void OnSceneUnload();

    // CVar toggle
    bool IsMarioModeEnabled() const;

    // Mario state (read by renderer)
    const SM64MarioState&            GetMarioState() const { return mState; }
    const SM64MarioGeometryBuffers&  GetGeometry()   const { return mGeometry; }
    int32_t                          GetMarioId()    const { return mMarioId; }

private:
    MarioManager() = default;

    bool     mInitialized = false;
    int32_t  mMarioId = -1;
    bool     mSceneLoaded = false;

    SM64MarioState           mState{};
    SM64MarioGeometryBuffers mGeometry{};

    // Geometry buffer storage (allocated once)
    std::vector<float>    mPosBuf;
    std::vector<float>    mNormBuf;
    std::vector<float>    mColorBuf;
    std::vector<float>    mUVBuf;

    // Texture atlas (uploaded to GL once)
    uint8_t mTextureAtlas[SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT * 4];

    // Tick accumulator for 30Hz SM64 physics
    float mTickAccum = 0.0f;
    static constexpr float SM64_TICK_DT = 1.0f / 30.0f;
};
```

### 3.2 MarioManager.cpp

```cpp
#include "MarioManager.h"
#include "MarioCollision.h"
#include "MarioController.h"
#include "MarioRenderer.h"
#include <libsm64.h>

// OoT headers
extern "C" {
#include "z64.h"
#include "z64actor.h"
}

static MarioManager sInstance;
MarioManager* MarioManager::Get() { return &sInstance; }

int MarioManager::InitFromRom(const std::vector<uint8_t>& romData) {
    if (romData.size() < 8 * 1024 * 1024) return -1; // Too small

    // Initialize libsm64 — extracts Mario assets from ROM
    sm64_global_init(romData.data(), mTextureAtlas);

    // Allocate geometry buffers (max triangles)
    mPosBuf.resize(SM64_GEO_MAX_TRIANGLES * 3 * 3);   // 3 verts * xyz
    mNormBuf.resize(SM64_GEO_MAX_TRIANGLES * 3 * 3);
    mColorBuf.resize(SM64_GEO_MAX_TRIANGLES * 3 * 3);
    mUVBuf.resize(SM64_GEO_MAX_TRIANGLES * 3 * 2);

    mGeometry.position   = mPosBuf.data();
    mGeometry.normal     = mNormBuf.data();
    mGeometry.color      = mColorBuf.data();
    mGeometry.uv         = mUVBuf.data();

    // Upload texture atlas to GPU
    MarioRenderer::Get()->UploadTextureAtlas(mTextureAtlas);

    mInitialized = true;
    return 0;
}

void MarioManager::OnSceneLoad(PlayState* play) {
    if (!mInitialized || !IsMarioModeEnabled()) return;

    // Convert OoT collision geometry → SM64 surfaces
    auto surfaces = MarioCollision::BuildSurfacesFromScene(play);
    sm64_static_surfaces_load(surfaces.data(), (uint32_t)surfaces.size());

    // Spawn Mario at Link's starting position
    Player* link = GET_PLAYER(play);
    mMarioId = sm64_mario_create(
        (int16_t)link->actor.world.pos.x,
        (int16_t)link->actor.world.pos.y,
        (int16_t)link->actor.world.pos.z
    );

    mSceneLoaded = true;
    mTickAccum = 0.0f;
}

void MarioManager::OnSceneUnload() {
    if (mMarioId >= 0) {
        sm64_mario_delete(mMarioId);
        mMarioId = -1;
    }
    mSceneLoaded = false;
}

void MarioManager::OnGameFrame(PlayState* play) {
    if (!mInitialized || !IsMarioModeEnabled() || !mSceneLoaded) return;
    if (mMarioId < 0) return;

    Player* link = GET_PLAYER(play);

    // During cutscenes: sync Mario to Link, skip SM64 physics
    bool inCutscene = (play->csCtx.state != CS_STATE_IDLE);
    if (inCutscene) {
        // Teleport SM64 Mario to Link's cutscene position
        sm64_set_mario_position(mMarioId,
            (int16_t)link->actor.world.pos.x,
            (int16_t)link->actor.world.pos.y,
            (int16_t)link->actor.world.pos.z);
        return;
    }

    // Build SM64 inputs from OoT controller
    SM64MarioInputs inputs = MarioController::BuildInputs(play);

    // Tick SM64 physics at 30Hz using accumulator
    // OoT's deltaTime from gfx system or fixed 1/60
    float dt = 1.0f / 60.0f;  // OoT web runs ~60fps
    mTickAccum += dt;

    while (mTickAccum >= SM64_TICK_DT) {
        sm64_mario_tick(mMarioId, &inputs, &mState, &mGeometry);
        mTickAccum -= SM64_TICK_DT;
    }

    // Sync Mario's SM64 position back to Link actor
    // This keeps OoT game logic (triggers, colliders, etc.) working
    link->actor.world.pos.x = mState.position[0];
    link->actor.world.pos.y = mState.position[1];
    link->actor.world.pos.z = mState.position[2];

    // Update Link's facing angle from Mario's facing direction
    link->actor.shape.rot.y = (int16_t)(mState.faceAngle * (32768.0f / M_PI));

    // Suppress Link's rendering (make him invisible)
    link->actor.flags |= ACTOR_FLAG_DRAW_CULLED;  // or set actor.draw = NULL
}

bool MarioManager::IsMarioModeEnabled() const {
    return CVarGetInteger("gMarioModeEnabled", 0) != 0;
}

void MarioManager::Shutdown() {
    OnSceneUnload();
    if (mInitialized) {
        sm64_global_terminate();
        mInitialized = false;
    }
}
```

---

## Phase 4: Collision Bridge (OoT → SM64)

This is the most complex phase. OoT's static collision geometry must be converted to SM64 surface format so Mario has proper physics interactions.

### 4.1 OoT Collision Data Structure

```c
// From OoT source (z64bgcheck.h)
typedef struct {
    /* 0x00 */ Vec3s minBounds;
    /* 0x06 */ Vec3s maxBounds;
    /* 0x0C */ s16 numVertices;
    /* 0x10 */ Vec3s* vertices;
    /* 0x14 */ s16 numPolygons;
    /* 0x18 */ CollisionPoly* polygons;   // each has 3 vertex indices + surface type
    /* 0x1C */ SurfaceType* surfaceTypeList;
    /* 0x20 */ BgCamInfo* bgCamList;
    /* 0x24 */ s16 numWaterBoxes;
    /* 0x28 */ WaterBox* waterBoxes;
} CollisionHeader;

typedef struct {
    /* 0x00 */ u16 type;
    /* 0x02 */ u16 vtxData[3];  // vertex indices (12-bit) + flags (4-bit)
    /* 0x08 */ Vec3s normal;
    /* 0x0E */ s16 dist;
} CollisionPoly;
```

### 4.2 MarioCollision.cpp

```cpp
#include "MarioCollision.h"
#include <libsm64.h>

extern "C" {
#include "z64.h"
#include "z64bgcheck.h"
}

// Map OoT surface types to SM64 terrain types
static uint16_t MapSurfaceType(uint16_t ootType) {
    // OoT surface type flags (lower byte = material type)
    uint8_t mat = ootType & 0xFF;
    switch (mat) {
        case 0x00: return SURFACE_DEFAULT;        // normal ground
        case 0x09: return SURFACE_SHALLOW_WATER;  // water surface
        case 0x0B: return SURFACE_ICE;            // ice
        case 0x0C: return SURFACE_NOISE_DEFAULT;  // sand/dirt
        case 0x05: return SURFACE_WALL_MISC;      // steep wall
        default:   return SURFACE_DEFAULT;
    }
}

std::vector<SM64Surface> MarioCollision::BuildSurfacesFromScene(PlayState* play) {
    std::vector<SM64Surface> surfaces;
    surfaces.reserve(4096);

    BgContext* bgCtx = &play->colCtx;

    // Static collision from scene and all loaded rooms
    StaticLookup* statLookup = bgCtx->stat.bgActors;
    for (int i = 0; i < BG_ACTOR_MAX; i++) {
        BgActor* bgActor = &bgCtx->stat.bgActors[i];
        if (bgActor->actor.update == NULL) continue;  // inactive

        CollisionHeader* colHeader = bgActor->colHeader;
        if (!colHeader || colHeader->numPolygons == 0) continue;

        AppendCollisionHeader(surfaces, colHeader, bgActor->curTransform);
    }

    // Also include mesh collision from all rooms directly
    for (int roomIdx = 0; roomIdx < play->numRooms; roomIdx++) {
        // Room collision is loaded via scene's collision header
        // Access via bgCtx->stat
    }

    // Direct scene static collision
    if (bgCtx->stat.colHeader) {
        AppendCollisionHeader(surfaces, bgCtx->stat.colHeader, nullptr);
    }

    return surfaces;
}

void MarioCollision::AppendCollisionHeader(
    std::vector<SM64Surface>& out,
    CollisionHeader* header,
    MtxF* transform  // optional world transform
) {
    if (!header || !header->vertices || !header->polygons) return;

    for (int i = 0; i < header->numPolygons; i++) {
        CollisionPoly* poly = &header->polygons[i];

        // Extract vertex indices (lower 12 bits of vtxData)
        int vi0 = poly->vtxData[0] & 0x1FFF;
        int vi1 = poly->vtxData[1] & 0x1FFF;
        int vi2 = poly->vtxData[2] & 0x1FFF;

        Vec3s* v0 = &header->vertices[vi0];
        Vec3s* v1 = &header->vertices[vi1];
        Vec3s* v2 = &header->vertices[vi2];

        SM64Surface surf{};
        surf.type    = SURFACE_DEFAULT;
        surf.force   = 0;
        surf.terrain = TERRAIN_GRASS;

        // Map surface type
        if (header->surfaceTypeList) {
            SurfaceType st = header->surfaceTypeList[poly->type & 0x1FF];
            surf.type = MapSurfaceType(st.data[0] & 0xFF);
        }

        // Copy vertices (SM64Surface uses int16_t[3][3])
        surf.vertices[0][0] = v0->x;
        surf.vertices[0][1] = v0->y;
        surf.vertices[0][2] = v0->z;
        surf.vertices[1][0] = v1->x;
        surf.vertices[1][1] = v1->y;
        surf.vertices[1][2] = v1->z;
        surf.vertices[2][0] = v2->x;
        surf.vertices[2][1] = v2->y;
        surf.vertices[2][2] = v2->z;

        out.push_back(surf);
    }
}

// Dynamic collision objects (moving platforms, etc.)
// Called each frame for objects that move
uint32_t MarioCollision::CreateDynamicObject(CollisionHeader* header) {
    // Build surface list for this object
    std::vector<SM64Surface> surfaces;
    AppendCollisionHeader(surfaces, header, nullptr);

    SM64SurfaceObject obj{};
    obj.surfaceCount = surfaces.size();
    obj.surfaces = surfaces.data();
    obj.transform.position[0] = 0;
    obj.transform.position[1] = 0;
    obj.transform.position[2] = 0;
    obj.transform.eulerRotation[0] = 0;
    obj.transform.eulerRotation[1] = 0;
    obj.transform.eulerRotation[2] = 0;

    return sm64_surface_object_create(&obj);
}

void MarioCollision::UpdateDynamicObject(uint32_t objId, float x, float y, float z, float ry) {
    SM64ObjectTransform t{};
    t.position[0] = x;
    t.position[1] = y;
    t.position[2] = z;
    t.eulerRotation[1] = ry;
    sm64_surface_object_move(objId, &t);
}
```

---

## Phase 5: Input Controller

### 5.1 MarioController.cpp

```cpp
#include "MarioController.h"
#include <libsm64.h>
#include <cmath>

extern "C" {
#include "z64.h"
#include "z64actor.h"
#include "z_camera.h"
}

SM64MarioInputs MarioController::BuildInputs(PlayState* play) {
    SM64MarioInputs inputs{};

    Input* controller = &play->state.input[0];
    s8 stickX = controller->cur.stick_x;
    s8 stickY = controller->cur.stick_y;

    // Get OoT camera forward angle (yaw only, in radians)
    Camera* cam = GET_ACTIVE_CAM(play);
    float camYaw = cam->unk_14C * (M_PI / 32768.0f);  // s16 angle → radians

    // Rotate stick input to be camera-relative (as SM64 does)
    // SM64 expects stick X/Y relative to camera facing
    float sx = (float)stickX / 64.0f;  // normalize to [-1, 1]
    float sy = (float)stickY / 64.0f;

    // Apply camera rotation to stick (SM64's camera_process_input equivalent)
    float cosYaw = cosf(camYaw);
    float sinYaw = sinf(camYaw);
    inputs.stickX =  sx * cosYaw + sy * sinYaw;
    inputs.stickY = -sx * sinYaw + sy * cosYaw;

    // Clamp to SM64 range [-1, 1]
    inputs.stickX = fmaxf(-1.0f, fminf(1.0f, inputs.stickX));
    inputs.stickY = fmaxf(-1.0f, fminf(1.0f, inputs.stickY));

    // Button mapping
    u16 held = controller->cur.button;
    inputs.buttonA = (held & BTN_A)      ? 1 : 0;  // Jump
    inputs.buttonB = (held & BTN_B)      ? 1 : 0;  // Punch/kick
    inputs.buttonZ = (held & BTN_Z)      ? 1 : 0;  // Crouch/ground pound
    // R-stick or R button → camera C-buttons for now no-op

    // Camera angle (in degrees, SM64 convention)
    // SM64 wants the camera angle so it can adjust Mario's stick direction
    // We pass 0 since we already rotated the stick above
    inputs.camLookX = 0.0f;
    inputs.camLookZ = 0.0f;

    return inputs;
}
```

---

## Phase 6: Mario Renderer (OpenGL ES 2.0)

Mario's geometry from libsm64 is a dynamic triangle mesh. We render it via direct OpenGL ES 2.0 calls, bypassing OoT's F3D pipeline.

### 6.1 MarioRenderer.h

```cpp
#pragma once
#include <GLES2/gl2.h>
#include <libsm64.h>

class MarioRenderer {
public:
    static MarioRenderer* Get();

    void Init();
    void UploadTextureAtlas(const uint8_t* atlasRGBA);  // SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT * 4
    void DrawMario(const SM64MarioGeometryBuffers& geo, float scale);
    void Shutdown();

private:
    MarioRenderer() = default;

    bool   mReady = false;

    // GL resources
    GLuint mVAO = 0;
    GLuint mVBOPos = 0, mVBONorm = 0, mVBOColor = 0, mVBOUV = 0;
    GLuint mTexture = 0;
    GLuint mShader = 0;

    // Shader uniform locations
    GLint  mUniMVP = -1;
    GLint  mUniTexture = -1;
    GLint  mUniScale = -1;

    static const char* kVertShader;
    static const char* kFragShader;
    GLuint CompileShader(GLenum type, const char* src);
    GLuint LinkProgram(GLuint vert, GLuint frag);
};
```

### 6.2 Mario GLSL Shaders

```glsl
// Vertex shader
attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec3 aColor;
attribute vec2 aUV;

uniform mat4 uMVP;

varying vec3 vColor;
varying vec2 vUV;
varying float vLight;

void main() {
    gl_Position = uMVP * vec4(aPosition, 1.0);
    vColor = aColor;
    vUV = aUV;
    // Simple directional light (top-down, SM64-like)
    vec3 lightDir = normalize(vec3(0.3, 1.0, 0.5));
    vLight = max(dot(aNormal, lightDir), 0.2);
}

// Fragment shader
precision mediump float;
uniform sampler2D uTexture;
varying vec3 vColor;
varying vec2 vUV;
varying float vLight;

void main() {
    vec4 tex = texture2D(uTexture, vUV);
    // SM64 uses vertex color to modulate texture
    gl_FragColor = vec4(tex.rgb * vColor * vLight, tex.a);
    // Discard fully transparent pixels
    if (gl_FragColor.a < 0.1) discard;
}
```

### 6.3 MarioRenderer.cpp Key Functions

```cpp
void MarioRenderer::UploadTextureAtlas(const uint8_t* atlasRGBA) {
    glGenTextures(1, &mTexture);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
        SM64_TEXTURE_WIDTH, SM64_TEXTURE_HEIGHT,
        0, GL_RGBA, GL_UNSIGNED_BYTE, atlasRGBA);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    mReady = true;
}

void MarioRenderer::DrawMario(const SM64MarioGeometryBuffers& geo, float scale) {
    if (!mReady || geo.numTrianglesUsed == 0) return;

    int numVerts = geo.numTrianglesUsed * 3;

    // Upload dynamic geometry to GPU
    glBindBuffer(GL_ARRAY_BUFFER, mVBOPos);
    glBufferData(GL_ARRAY_BUFFER, numVerts * 3 * sizeof(float), geo.position, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, mVBONorm);
    glBufferData(GL_ARRAY_BUFFER, numVerts * 3 * sizeof(float), geo.normal, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, mVBOColor);
    glBufferData(GL_ARRAY_BUFFER, numVerts * 3 * sizeof(float), geo.color, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, mVBOUV);
    glBufferData(GL_ARRAY_BUFFER, numVerts * 2 * sizeof(float), geo.uv, GL_DYNAMIC_DRAW);

    // Build MVP matrix from OoT camera
    // (see below for camera matrix extraction)
    float mvp[16];
    BuildMVP(mvp, scale);

    // Draw
    glUseProgram(mShader);
    glUniformMatrix4fv(mUniMVP, 1, GL_FALSE, mvp);
    glUniform1i(mUniTexture, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mTexture);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Set up attributes
    // ... (bind each VBO to its attribute location)

    glDrawArrays(GL_TRIANGLES, 0, numVerts);

    glUseProgram(0);
}
```

### 6.4 Hook into OoT's Render Pipeline

In `soh/soh/OTRGlobals.cpp` or the game's main render call, add a post-render hook:

```cpp
// After gfx_run() call in the render loop
// Find the location in libultraship where the final frame is presented

// In soh/src/port/Engine.cpp or equivalent:
void Engine_Update() {
    // ... existing OoT update ...
    gfxRun(displayList);

    // NEW: Draw Mario on top of OoT scene
    if (MarioManager::Get()->IsReady() && MarioManager::Get()->IsMarioModeEnabled()) {
        MarioRenderer::Get()->DrawMario(
            MarioManager::Get()->GetGeometry(),
            1.0f  // scale
        );
    }
}
```

The exact hook point needs to be found by searching for `gfx_run` or `GfxWindowManagerApi_Update` in the Shipwright codebase.

---

## Phase 7: OoT Integration Hooks

### 7.1 Scene Load/Unload Hooks

Find where scenes are loaded in OoT (`soh/src/code/z_scene.c`, `z_scene_table.c`):

```cpp
// Add in Shipwright's enhancement hooks system or directly in z_scene.c

// When scene loads (after collision is set up):
extern "C" void Scene_OnLoad(PlayState* play) {
    MarioManager::Get()->OnSceneLoad(play);
}

// When scene unloads:
extern "C" void Scene_OnUnload(PlayState* play) {
    MarioManager::Get()->OnSceneUnload();
}
```

Shipwright has a hook system via `GameInteractor`. Find the appropriate hook:

```cpp
// In Shipwright's hook system (search for GameInteractor::Instance->ExecuteHooks)
GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(
    [](int16_t sceneNum) {
        MarioManager::Get()->OnSceneLoad(gPlayState);
    }
);
```

### 7.2 Per-Frame Update Hook

```cpp
// Hook into the game's Update() call
GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(
    []() {
        if (gPlayState) {
            MarioManager::Get()->OnGameFrame(gPlayState);
        }
    }
);
```

### 7.3 Enhancement Menu Entry

Add Mario Mode to the Ship of Harkinian enhancement menu:

```cpp
// In soh/soh/SohGui/ or the enhancement menu file
// Search for UIWidgets::CVarCheckbox usage

UIWidgets::CVarCheckbox("Mario Mode (requires SM64 ROM)", "gMarioModeEnabled",
    { .tooltip = "Play as Mario! Requires Super Mario 64 US ROM to be loaded." });
```

---

## Phase 8: CMakeLists.txt Integration

### 8.1 Add libsm64 Sources to soh/CMakeLists.txt

```cmake
if(BUILD_FOR_WEB)
    # Collect libsm64 C sources
    file(GLOB_RECURSE LIBSM64_C_SOURCES
        "${CMAKE_SOURCE_DIR}/libsm64/src/*.c"
    )

    # Exclude any file that pulls in pthreads or platform-specific code
    # (inspect libsm64/src/ and exclude as needed)
    list(FILTER LIBSM64_C_SOURCES EXCLUDE REGEX ".*_thread.*\\.c$")

    # Build as static library
    add_library(sm64static STATIC ${LIBSM64_C_SOURCES})
    target_include_directories(sm64static PUBLIC
        "${CMAKE_SOURCE_DIR}/libsm64/include"
        "${CMAKE_SOURCE_DIR}/libsm64/src"
        "${CMAKE_SOURCE_DIR}/libsm64/src/decomp"
    )
    target_compile_definitions(sm64static PRIVATE
        VERSION_US=1
        NO_SEGMENTED_MEMORY=1
        TARGET_WEB=1
    )
    target_compile_options(sm64static PRIVATE -w -O2)

    # Mario integration sources
    target_sources(${PROJECT_NAME} PRIVATE
        soh/Mario/MarioManager.cpp
        soh/Mario/MarioActor.cpp
        soh/Mario/MarioCollision.cpp
        soh/Mario/MarioController.cpp
        soh/Mario/MarioRenderer.cpp
    )

    target_include_directories(${PROJECT_NAME} PRIVATE
        "${CMAKE_SOURCE_DIR}/libsm64/include"
        soh/Mario
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE sm64static)
endif()
```

### 8.2 Update EXPORTED_FUNCTIONS

In `soh/CMakeLists.txt`, add to the `-sEXPORTED_FUNCTIONS` linker option:

```
_web_load_sm64_rom,_web_get_sm64_status
```

---

## Phase 9: Testing & Iteration

### 9.1 Test Matrix

| Test | Expected Result |
|------|----------------|
| Build compiles cleanly | No linker errors, WASM outputs |
| OoT ROM loads alone | Game works normally without SM64 ROM |
| SM64 ROM rejected if invalid | Clear error message in UI |
| SM64 ROM accepted | Mario Mode toggle appears in menu |
| Deku Tree - Mario moves | Mario walks with SM64 physics on OoT floor geometry |
| Deku Tree - Mario jumps | SM64 jump physics work, Mario lands on platforms |
| Mario attacks | Punch animation plays |
| Scene transition | No crash, Mario respawns at new scene start position |
| Link triggers work | Pushing blocks, pulling levers, opening doors function |
| Cutscene | Mario stays frozen/synced during cutscene |
| Health system | OoT heart system still drives death/game over |
| Mario Mode toggle | Switching off shows Link normally |

### 9.2 Known Issues to Address During Testing

1. **Coordinate scale mismatch**: If Mario is tiny or huge, adjust `MARIO_WORLD_SCALE`
2. **Camera angle**: If Mario runs the wrong direction relative to stick, adjust `camYaw` calculation
3. **Floor detection failure**: If Mario falls through geometry, check CollisionHeader polygon extraction (vertex index bit masking)
4. **SM64 action stuck**: If Mario gets stuck in an action state, add `sm64_set_mario_action()` calls on scene load to reset
5. **Memory pressure**: SM64 texture atlas (4MB RGBA) + geometry buffers + OoT assets fit within the 256MB WASM heap

---

## Phase 10: Polish

### 10.1 Mario Sound Effects

libsm64 triggers SM64 sound effects through a callback. Implement a minimal audio bridge:

```c
// libsm64 calls this for sound effects
void sm64_play_sound(int soundBits, float *pos) {
    // Map SM64 sound IDs to equivalent sounds or silence
    // Option A: use SM64's audio engine if linked
    // Option B: play OoT sounds for approximated actions
    //   - Jump sound → OoT's link jump sound
    //   - Land sound → OoT's land sound
    //   - Coin → OoT's rupee sound
}
```

### 10.2 Mario's Position on Map

Hide Link's position marker on the minimap during Mario mode, or show Mario's hat icon.

### 10.3 Death / Game Over

When OoT's health reaches 0:
- Trigger SM64 Mario death animation before game over screen
- Use `sm64_set_mario_action(mMarioId, ACT_DEATH_EXIT)` or equivalent

### 10.4 URL Parameter

Support `?mario=1` URL parameter to auto-enable Mario Mode after both ROMs are loaded:

```javascript
// In shell.html init
const params = new URLSearchParams(window.location.search);
if (params.get('mario') === '1') {
    // Auto-enable Mario mode via CVar after init
    Module.ccall('web_set_mario_mode', null, ['number'], [1]);
}
```

---

## File Change Summary

| File | Change Type | Notes |
|------|-------------|-------|
| `libsm64/` | New (submodule) | libsm64 library |
| `soh/soh/Mario/*.cpp/h` | New | All Mario integration code |
| `soh/CMakeLists.txt` | Modified | Add libsm64, Mario sources, export funcs |
| `soh/soh/web/shell.html` | Modified | Add SM64 ROM picker UI |
| `soh/soh/web/web_main.cpp` | Modified | Add `web_load_sm64_rom()`, `web_get_sm64_status()` |
| `soh/soh/SohGui/` | Modified | Add Mario Mode toggle to enhancement menu |
| `soh/src/code/z_scene.c` | Modified | Add Mario scene load/unload hooks |
| `.gitmodules` | New | libsm64 submodule entry |
| `CLAUDE.md` | New | Project guide |
| `PLAN.md` | New | This document |

---

## Risks and Mitigations

| Risk | Mitigation |
|------|-----------|
| libsm64 Emscripten compile errors | Fix per-file; decompiled N64 code has known portability issues — suppress with `-w`, fix pointer size issues |
| OoT collision vertex format wrong | Validate with a debug overlay rendering the extracted surfaces as wireframe |
| SM64 physics diverge from OoT geometry | Add `MARIO_WORLD_SCALE` tuning constant, test in simple flat rooms first |
| Memory exceeds 256MB WASM limit | Profile with Chrome DevTools, increase `INITIAL_MEMORY` if needed |
| F3D render hook location not obvious | Search for `gfx_run` in Shipwright; add hook after the present/swap call |
| Cutscene position diverges | Force SM64 Mario teleport on every scene init, not just scene load |
