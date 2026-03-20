#ifdef __EMSCRIPTEN__

#include "MarioManager.h"
#include "MarioCollision.h"
#include "MarioController.h"
#include "MarioRenderer.h"

#include <stdio.h>
#include <string.h>
#include <cmath>

#include <libultraship/bridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
extern PlayState* gPlayState;
#include "z64.h"
#include "z64actor.h"
#include "macros.h"
}

#define CVAR_MARIO_MODE "gMarioModeEnabled"

MarioManager* MarioManager::Get() {
    static MarioManager sInstance;
    return &sInstance;
}

// ---------------------------------------------------------------------------
// Initialisation from SM64 ROM bytes
// ---------------------------------------------------------------------------

int MarioManager::InitFromRomFile(const char* romPath, int romSize) {
    FILE* f = fopen(romPath, "rb");
    if (!f) {
        fprintf(stderr, "[Mario] Failed to open ROM: %s\n", romPath);
        return -1;
    }

    std::vector<uint8_t> rom((size_t)romSize);
    size_t read = fread(rom.data(), 1, (size_t)romSize, f);
    fclose(f);

    if (read != (size_t)romSize) {
        fprintf(stderr, "[Mario] ROM read incomplete: %zu / %d bytes\n", read, romSize);
        return -2;
    }

    // Validate SM64 US ROM header: 80 37 12 40 (big-endian z64)
    if (rom.size() < 4 ||
        rom[0] != 0x80 || rom[1] != 0x37 ||
        rom[2] != 0x12 || rom[3] != 0x40) {
        fprintf(stderr, "[Mario] Invalid SM64 ROM header — expected US big-endian z64\n");
        return -3;
    }

    sm64_register_debug_print_function([](const char* msg) {
        printf("[libsm64] %s\n", msg);
    });
    sm64_register_play_sound_function([](uint32_t /*soundBits*/, float* /*pos*/) {
        // TODO: map SM64 sound IDs to OoT sounds
    });

    printf("[Mario] Calling sm64_global_init (%zu bytes ROM)...\n", rom.size());
    sm64_global_init(rom.data(), mTextureAtlas);
    printf("[Mario] sm64_global_init done.\n");

    // Allocate per-frame geometry buffers
    constexpr int V = SM64_GEO_MAX_TRIANGLES * 3;
    mPosBuf.assign(V * 3, 0.0f);
    mNormBuf.assign(V * 3, 0.0f);
    mColorBuf.assign(V * 3, 0.0f);
    mUVBuf.assign(V * 2, 0.0f);

    mGeometry.position = mPosBuf.data();
    mGeometry.normal   = mNormBuf.data();
    mGeometry.color    = mColorBuf.data();
    mGeometry.uv       = mUVBuf.data();

    // Upload atlas to GPU — WebGL context is alive at this point
    MarioRenderer::Get()->UploadTextureAtlas(mTextureAtlas);

    mInitialized = true;
    printf("[Mario] Initialised. Enable via CVar '%s'.\n", CVAR_MARIO_MODE);
    return 0;
}

// ---------------------------------------------------------------------------
// Scene lifecycle
// ---------------------------------------------------------------------------

void MarioManager::OnSceneInit(int16_t /*sceneNum*/) {
    if (!mInitialized || !IsMarioModeEnabled() || !gPlayState) return;

    auto surfaces = MarioCollision::BuildSurfacesFromScene(gPlayState);
    if (!surfaces.empty()) {
        sm64_static_surfaces_load(surfaces.data(), (uint32_t)surfaces.size());
        printf("[Mario] Loaded %zu collision surfaces.\n", surfaces.size());
    } else {
        // Clear previous scene's surfaces (nullptr / 0 is valid)
        sm64_static_surfaces_load(nullptr, 0);
        fprintf(stderr, "[Mario] Warning: no collision surfaces found for this scene.\n");
    }

    SpawnMario();
    mSceneLoaded = true;
    mTickAccum   = 0.0f;
}

void MarioManager::OnPlayDestroy() {
    DespawnMario();
    mSceneLoaded = false;
}

void MarioManager::SpawnMario() {
    DespawnMario();
    if (!gPlayState) return;

    Player* link = GET_PLAYER(gPlayState);
    float x = link->actor.world.pos.x;
    float y = link->actor.world.pos.y;
    float z = link->actor.world.pos.z;

    mMarioId = sm64_mario_create(x, y, z);
    if (mMarioId < 0) {
        fprintf(stderr, "[Mario] sm64_mario_create failed at (%.1f, %.1f, %.1f)\n", x, y, z);
    } else {
        printf("[Mario] Mario spawned at (%.1f, %.1f, %.1f) id=%d\n", x, y, z, mMarioId);
    }
}

void MarioManager::DespawnMario() {
    if (mMarioId >= 0) {
        sm64_mario_delete(mMarioId);
        mMarioId = -1;
    }
}

// ---------------------------------------------------------------------------
// Per-frame physics update
// ---------------------------------------------------------------------------

void MarioManager::OnGameFrameUpdate() {
    if (!mInitialized || !IsMarioModeEnabled() || !mSceneLoaded || !gPlayState) return;
    if (mMarioId < 0) return;

    Player* link = GET_PLAYER(gPlayState);

    // Suppress Link's own draw while Mario mode is active
    // OoT skips Actor_Draw() when actor.draw == NULL
    link->actor.draw = nullptr;

    // During cutscenes keep Mario glued to Link without SM64 physics
    bool inCutscene = (gPlayState->csCtx.state != 0);
    if (inCutscene) {
        sm64_set_mario_position(mMarioId,
            link->actor.world.pos.x,
            link->actor.world.pos.y,
            link->actor.world.pos.z);
        return;
    }

    // Build SM64 inputs from OoT controller + camera
    SM64MarioInputs inputs = MarioController::BuildInputs(gPlayState);

    // Fixed 30 Hz tick accumulator
    mTickAccum += OOT_FRAME_DT;
    while (mTickAccum >= SM64_TICK_DT) {
        sm64_mario_tick(mMarioId, &inputs, &mState, &mGeometry);
        mTickAccum -= SM64_TICK_DT;
    }

    // Write Mario's world position back to Link so OoT triggers/doors work
    link->actor.world.pos.x = mState.position[0];
    link->actor.world.pos.y = mState.position[1];
    link->actor.world.pos.z = mState.position[2];

    // SM64 faceAngle (radians) → OoT s16 angle units (0..65535 = 0..2π)
    link->actor.shape.rot.y = (int16_t)(mState.faceAngle * (32768.0f / (float)M_PI));
}

// ---------------------------------------------------------------------------
// Post-draw: render Mario's mesh via OpenGL ES 2.0
// ---------------------------------------------------------------------------

void MarioManager::OnPlayDrawEnd() {
    if (!mInitialized || !IsMarioModeEnabled() || !mSceneLoaded || !gPlayState) return;
    if (mMarioId < 0 || mGeometry.numTrianglesUsed == 0) return;

    // play->viewProjectionMtxF is computed by OoT every frame in z_play.c:
    //   Matrix_MtxToMtxF(&play->view.projection, &play->viewProjectionMtxF);
    //   Matrix_Mult(&play->viewProjectionMtxF, MTXMODE_NEW);
    // It is a MtxF (float mf[4][4]), row-major order.
    // OpenGL expects column-major, so we pass GL_TRUE to transpose.
    const float* vp = (const float*)gPlayState->viewProjectionMtxF.mf;
    MarioRenderer::Get()->Draw(mGeometry, vp);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool MarioManager::IsMarioModeEnabled() const {
    return CVarGetInteger(CVAR_MARIO_MODE, 0) != 0;
}

void MarioManager::Shutdown() {
    DespawnMario();
    if (mInitialized) {
        sm64_global_terminate();
        mInitialized = false;
    }
}

// ---------------------------------------------------------------------------
// Hook registration at boot
// ---------------------------------------------------------------------------

static void RegisterMarioHooks() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(
        [](int16_t sceneNum) { MarioManager::Get()->OnSceneInit(sceneNum); });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDestroy>(
        []() { MarioManager::Get()->OnPlayDestroy(); });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(
        []() { MarioManager::Get()->OnGameFrameUpdate(); });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayDrawEnd>(
        []() { MarioManager::Get()->OnPlayDrawEnd(); });

    printf("[Mario] GameInteractor hooks registered.\n");
}

static RegisterShipInitFunc sMarioHookInit(RegisterMarioHooks);

#endif // __EMSCRIPTEN__
