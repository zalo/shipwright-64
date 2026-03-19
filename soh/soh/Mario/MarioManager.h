#pragma once
#ifdef __EMSCRIPTEN__

#include <vector>
#include <cstdint>
#include <libsm64.h>

typedef struct PlayState PlayState;

class MarioManager {
public:
    static MarioManager* Get();

    // Called from web_main.cpp after SM64 ROM is written to VFS
    int  InitFromRomFile(const char* romPath, int romSize);
    bool IsReady() const { return mInitialized; }
    void Shutdown();

    // Called by GameInteractor hooks
    void OnSceneInit(int16_t sceneNum);
    void OnPlayDestroy();
    void OnGameFrameUpdate();
    void OnPlayDrawEnd();

    // Called on boot to register all hooks
    static void RegisterHooks();

    bool IsMarioModeEnabled() const;

    // Accessed by MarioRenderer
    const SM64MarioState&           GetMarioState()   const { return mState; }
    const SM64MarioGeometryBuffers& GetGeometry()     const { return mGeometry; }
    const uint8_t*                  GetTextureAtlas()  const { return mTextureAtlas; }

private:
    MarioManager() = default;

    void SpawnMario();
    void DespawnMario();

    bool     mInitialized  = false;
    int32_t  mMarioId      = -1;
    bool     mSceneLoaded  = false;
    float    mTickAccum    = 0.0f;

    SM64MarioState           mState{};
    SM64MarioGeometryBuffers mGeometry{};

    // CPU-side geometry storage (buffers passed into SM64 tick)
    std::vector<float>    mPosBuf;
    std::vector<float>    mNormBuf;
    std::vector<float>    mColorBuf;
    std::vector<float>    mUVBuf;

    // Mario texture atlas extracted from SM64 ROM (64*11 × 64 RGBA)
    uint8_t mTextureAtlas[SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT * 4];

    static constexpr float SM64_TICK_DT = 1.0f / 30.0f;
    static constexpr float OOT_FRAME_DT = 1.0f / 60.0f;
};

#endif // __EMSCRIPTEN__
