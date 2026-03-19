# Ship of Harkinian Update/Render Loop Architecture

## Overview

SoH separates 20 FPS game logic from 60+ FPS rendering through a matrix interpolation system. The N64 game updates at 20 Hz (every 3rd frame at 60 Hz display), and the PC port fills the gaps with interpolated vertex transformations.

## Main Loop Entry

**File:** `soh/src/code/graph.c`

```
Graph_ThreadEntry → RunFrame() (state machine, yields after each frame)
  → Graph_StartFrame()           // input, hotkeys, per-frame setup
  → PadMgr_ThreadEntry()         // poll controllers
  → Graph_Update()               // game logic + display list generation
  → Graph_ProcessGfxCommands()   // interpolation + GPU submission
  → return (yields to window system)
```

`RunFrame()` uses a state machine pattern: state 0 initializes, state 1 processes one frame then returns. This allows the window system to process events between frames.

## VSync and Frame Timing

**File:** `libultraship/src/fast/backends/gfx_sdl2.cpp`

### SwapBuffersBegin (line ~701)
1. Checks CVar for VSync enable/disable
2. Calls `SDL_GL_SetSwapInterval(1 or 0)`
3. Calls `SyncFramerateWithTime()` for software frame pacing
4. Calls `SDL_GL_SwapWindow()` for buffer swap

### SyncFramerateWithTime (line ~655)
- Calculates target frame time: `10,000,000 / target_fps` (in 100ns units)
- Sleeps until ~10-15ms before deadline
- Busy-waits the remaining time for precision
- **On Emscripten: returns immediately** — browser controls timing via `requestAnimationFrame`

## R_UPDATE_RATE: Game Logic Frequency

**File:** `soh/include/regs.h` — `#define R_UPDATE_RATE SREG(30)`

| Value | Effective FPS | Used In |
|-------|--------------|---------|
| 3     | 20 FPS       | Normal gameplay (default) |
| 1     | 60 FPS       | Title screen, file select, some animations |

Formula: `game_update_fps = 60 / R_UPDATE_RATE`

The game logic (actor updates, physics, collision) runs at this rate. Rendering runs at the display refresh rate (60+ FPS), with interpolation filling the gaps.

## Frame Interpolation System

**Files:** `soh/soh/frame_interpolation.cpp`, `frame_interpolation.h`

### Recording Phase
During each game logic frame, the system records all matrix operations into a tree:

```
FrameInterpolation_StartRecord()  // Swap current→previous, start new recording
  ... game logic generates display lists ...
  // Each matrix op (translate, rotate, scale, mult) is recorded as a tree node
FrameInterpolation_StopRecord()   // Finalize recording
```

**Recorded operations (enum Op):**
- Tree structure: `OpenChild`, `CloseChild`
- Matrix ops: `MatrixPush`, `MatrixPop`, `MatrixPut`, `MatrixMult`
- Transforms: `MatrixTranslate`, `MatrixScale`, `MatrixRotate1Coord`, `MatrixRotateZYX`
- Actor special: `MatrixSetTranslateRotateYXZ`
- Output: `MatrixMtxFToMtx`, `MatrixToMtx`

### Interpolation Phase
Between game logic frames, `FrameInterpolation_Interpolate(step)` generates intermediate matrices:

```c
// step = 0.0 (previous frame) to 1.0 (current frame)
// w = 1.0 - step (weight of previous frame)
for each element:
    result[i][j] = w * previous[i][j] + step * current[i][j]
```

**Angle interpolation** handles 16-bit wrap-around by taking the shortest path, preventing 180° flip artifacts.

**Actor root matrix correction** stores the inverse of the root transform and applies it to children, preventing "paper effect" rotation artifacts.

### Output
Returns `unordered_map<Mtx*, MtxF>` — maps original matrix pointers to interpolated matrices. The Fast3D interpreter uses this map to substitute matrices during display list execution.

## Graph_ProcessGfxCommands Flow

**File:** `soh/soh/OTRGlobals.cpp` (line ~1747)

```
1. Get target FPS (user setting, e.g., 60)
2. Get original FPS (60 / R_UPDATE_RATE, e.g., 20)
3. Calculate interpolation frames needed
4. For each interpolated frame:
   a. FrameInterpolation_Interpolate(time / next_frame) → matrix map
   b. DrawAndRunGraphicsCommands(display_list, matrix_map)
      → Interpreter::Run(commands, mtx_replacements)
      → For each GBI command: load matrix (check replacement map), load vertices, draw triangles
      → Interpreter::EndFrame() → VSync + SwapBuffers
```

## Fast3D Display List Execution

**File:** `libultraship/src/fast/interpreter.cpp`

### Interpreter::Run (line ~4413)
1. Store interpolated matrix map
2. `StartFrame()` — resize framebuffers, setup render targets
3. Execute display list command-by-command via `gfx_step()`
4. `Flush()` — submit remaining triangles to GPU

### GBI Command Format
Each command is 64 bits: upper 32 (opcode + params) + lower 32 (data/address). Commands include:
- **G_MTX**: Load/multiply matrix (checks interpolation map for replacement)
- **G_VTX**: Load vertices for transformation
- **G_TRI1/G_TRI2**: Rasterize triangles
- **G_SETTILE/G_TEXTURE**: Texture configuration
- **G_PIPESYNC/G_FULLSYNC**: Pipeline synchronization

### EndFrame (line ~4478)
1. `EndFrame()` — finalize GPU command buffer
2. `SwapBuffersBegin()` — VSync sync + buffer swap
3. `FinishRender()` — wait for GPU completion
4. `SwapBuffersEnd()` — post-swap cleanup

## Menu/Pause Timing

**File:** `soh/src/overlays/misc/ovl_kaleido_scope/z_kaleido_scope_PAL.c`

### R_PAUSE_MENU_MODE States
| Value | State | Effect |
|-------|-------|--------|
| 0     | Normal gameplay | Standard update + draw |
| 1     | Pause setup | Capture frame for pause background |
| 2     | Pause active | **Game draw skipped** (line 341 of game.c), only menu renders |
| 3     | Pause with special rendering | Specific visual effects |

When `R_PAUSE_MENU_MODE == 2`, `GameState_Draw()` is skipped — the game world stops rendering but the pause menu overlay continues at full frame rate.

On menu close, `R_UPDATE_RATE` resets to 3 (20 FPS gameplay).

## Display List Buffer Organization

The game builds 4 display list buffers per frame:

| Buffer | Purpose |
|--------|---------|
| WORK_DISP | Graphics setup, initialization commands |
| POLY_OPA_DISP | Opaque 3D geometry |
| POLY_XLU_DISP | Transparent/translucent geometry |
| OVERLAY_DISP | HUD, menus, 2D overlays |

These are chained together: `WORK → OPA → XLU → OVERLAY → END`

## Key Source Files

| File | Purpose |
|------|---------|
| `soh/src/code/graph.c` | Main loop, RunFrame state machine |
| `soh/src/code/game.c` | GameState_Update, R_UPDATE_RATE |
| `soh/soh/frame_interpolation.cpp` | Matrix interpolation recording/playback |
| `soh/soh/OTRGlobals.cpp` | Graph_StartFrame, Graph_ProcessGfxCommands |
| `libultraship/src/fast/interpreter.cpp` | Display list execution |
| `libultraship/src/fast/Fast3dWindow.cpp` | Frame rendering coordination |
| `libultraship/src/fast/backends/gfx_sdl2.cpp` | VSync, frame pacing |
| `soh/include/regs.h` | R_UPDATE_RATE definition |
