#ifdef __EMSCRIPTEN__

#include "MarioRenderer.h"
#include <stdio.h>
#include <string.h>
#include <libsm64.h>

static MarioRenderer sRendererInstance;
MarioRenderer* MarioRenderer::Get() { return &sRendererInstance; }

// ---------------------------------------------------------------------------
// GLSL shaders
// ---------------------------------------------------------------------------

static const char* kVertSrc = R"GLSL(
attribute vec3 aPos;
attribute vec3 aNorm;
attribute vec3 aColor;
attribute vec2 aUV;

uniform mat4 uMVP;

varying vec3 vColor;
varying vec2 vUV;
varying float vLight;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
    vUV   = aUV;
    // Simple diffuse light from above-front
    vec3 lightDir = normalize(vec3(0.3, 1.0, 0.5));
    vLight = clamp(dot(normalize(aNorm), lightDir), 0.15, 1.0);
}
)GLSL";

static const char* kFragSrc = R"GLSL(
precision mediump float;

uniform sampler2D uTex;

varying vec3  vColor;
varying vec2  vUV;
varying float vLight;

void main() {
    vec4 tex = texture2D(uTex, vUV);
    if (tex.a < 0.1) discard;
    gl_FragColor = vec4(tex.rgb * vColor * vLight, tex.a);
}
)GLSL";

// ---------------------------------------------------------------------------
// GL helper
// ---------------------------------------------------------------------------

GLuint MarioRenderer::CompileShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        fprintf(stderr, "[MarioRenderer] Shader compile error: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

void MarioRenderer::InitGL() {
    if (mInitted) return;
    mInitted = true;

    GLuint vs = CompileShader(GL_VERTEX_SHADER,   kVertSrc);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragSrc);
    if (!vs || !fs) {
        fprintf(stderr, "[MarioRenderer] Shader compilation failed.\n");
        return;
    }

    mProgram = glCreateProgram();
    glAttachShader(mProgram, vs);
    glAttachShader(mProgram, fs);
    glLinkProgram(mProgram);

    GLint ok = 0;
    glGetProgramiv(mProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(mProgram, sizeof(log), nullptr, log);
        fprintf(stderr, "[MarioRenderer] Program link error: %s\n", log);
        glDeleteProgram(mProgram);
        mProgram = 0;
        return;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);

    mLocMVP   = glGetUniformLocation(mProgram, "uMVP");
    mLocTex   = glGetUniformLocation(mProgram, "uTex");
    mLocPos   = glGetAttribLocation(mProgram,  "aPos");
    mLocNorm  = glGetAttribLocation(mProgram,  "aNorm");
    mLocColor = glGetAttribLocation(mProgram,  "aColor");
    mLocUV    = glGetAttribLocation(mProgram,  "aUV");

    // Allocate VBOs
    GLuint bufs[4];
    glGenBuffers(4, bufs);
    mVBOPos   = bufs[0];
    mVBONorm  = bufs[1];
    mVBOColor = bufs[2];
    mVBOUV    = bufs[3];

    printf("[MarioRenderer] GL resources initialised (program=%u).\n", mProgram);
}

// ---------------------------------------------------------------------------
// Upload texture atlas
// ---------------------------------------------------------------------------

void MarioRenderer::UploadTextureAtlas(const uint8_t* atlasRGBA) {
    InitGL();
    if (!mProgram) return;

    glGenTextures(1, &mTexture);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 SM64_TEXTURE_WIDTH, SM64_TEXTURE_HEIGHT, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, atlasRGBA);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    mReady = true;
    printf("[MarioRenderer] Texture atlas uploaded (%d x %d RGBA).\n",
           SM64_TEXTURE_WIDTH, SM64_TEXTURE_HEIGHT);
}

// ---------------------------------------------------------------------------
// Draw Mario's dynamic mesh
// ---------------------------------------------------------------------------

void MarioRenderer::Draw(const SM64MarioGeometryBuffers& geo, const float* mvp) {
    if (!mReady || !mProgram || geo.numTrianglesUsed == 0) return;

    int numVerts = (int)geo.numTrianglesUsed * 3;

    // Upload dynamic geometry
    glBindBuffer(GL_ARRAY_BUFFER, mVBOPos);
    glBufferData(GL_ARRAY_BUFFER, numVerts * 3 * sizeof(float),
                 geo.position, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, mVBONorm);
    glBufferData(GL_ARRAY_BUFFER, numVerts * 3 * sizeof(float),
                 geo.normal, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, mVBOColor);
    glBufferData(GL_ARRAY_BUFFER, numVerts * 3 * sizeof(float),
                 geo.color, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, mVBOUV);
    glBufferData(GL_ARRAY_BUFFER, numVerts * 2 * sizeof(float),
                 geo.uv, GL_DYNAMIC_DRAW);

    glUseProgram(mProgram);

    // MVP: OoT stores MtxF in row-major; pass GL_TRUE to have GLSL transpose it
    glUniformMatrix4fv(mLocMVP, 1, GL_TRUE, mvp);
    glUniform1i(mLocTex, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mTexture);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Bind each attribute
    if (mLocPos >= 0) {
        glBindBuffer(GL_ARRAY_BUFFER, mVBOPos);
        glEnableVertexAttribArray(mLocPos);
        glVertexAttribPointer(mLocPos, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    if (mLocNorm >= 0) {
        glBindBuffer(GL_ARRAY_BUFFER, mVBONorm);
        glEnableVertexAttribArray(mLocNorm);
        glVertexAttribPointer(mLocNorm, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    if (mLocColor >= 0) {
        glBindBuffer(GL_ARRAY_BUFFER, mVBOColor);
        glEnableVertexAttribArray(mLocColor);
        glVertexAttribPointer(mLocColor, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    if (mLocUV >= 0) {
        glBindBuffer(GL_ARRAY_BUFFER, mVBOUV);
        glEnableVertexAttribArray(mLocUV);
        glVertexAttribPointer(mLocUV, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    }

    glDrawArrays(GL_TRIANGLES, 0, numVerts);

    // Clean up state
    if (mLocPos   >= 0) glDisableVertexAttribArray(mLocPos);
    if (mLocNorm  >= 0) glDisableVertexAttribArray(mLocNorm);
    if (mLocColor >= 0) glDisableVertexAttribArray(mLocColor);
    if (mLocUV    >= 0) glDisableVertexAttribArray(mLocUV);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

void MarioRenderer::Shutdown() {
    if (mTexture) { glDeleteTextures(1, &mTexture); mTexture = 0; }
    GLuint bufs[4] = { mVBOPos, mVBONorm, mVBOColor, mVBOUV };
    glDeleteBuffers(4, bufs);
    mVBOPos = mVBONorm = mVBOColor = mVBOUV = 0;
    if (mProgram) { glDeleteProgram(mProgram); mProgram = 0; }
    mReady   = false;
    mInitted = false;
}

#endif // __EMSCRIPTEN__
