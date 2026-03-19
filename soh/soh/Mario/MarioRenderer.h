#pragma once
#ifdef __EMSCRIPTEN__

#include <stdint.h>
#include <GLES2/gl2.h>
#include <libsm64.h>

class MarioRenderer {
public:
    static MarioRenderer* Get();

    // Upload Mario's RGBA texture atlas to GPU once after init
    void UploadTextureAtlas(const uint8_t* atlasRGBA);

    // Draw Mario using the geometry buffers from the last sm64_mario_tick()
    // mvp is a column-major 4×4 float array
    void Draw(const SM64MarioGeometryBuffers& geo, const float* mvp);

    void Shutdown();

private:
    MarioRenderer() = default;

    bool   mReady    = false;
    bool   mInitted  = false;

    GLuint mProgram  = 0;
    GLuint mVBOPos   = 0;
    GLuint mVBONorm  = 0;
    GLuint mVBOColor = 0;
    GLuint mVBOUV    = 0;
    GLuint mTexture  = 0;

    GLint  mLocMVP     = -1;
    GLint  mLocTex     = -1;
    GLint  mLocPos     = -1;
    GLint  mLocNorm    = -1;
    GLint  mLocColor   = -1;
    GLint  mLocUV      = -1;

    void InitGL();
    GLuint CompileShader(GLenum type, const char* src);
};

#endif // __EMSCRIPTEN__
