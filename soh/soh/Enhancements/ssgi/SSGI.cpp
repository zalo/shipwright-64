// GT-VBGI Screen-Space Global Illumination for Shipwright
// Original algorithm: https://www.shadertoy.com/view/XfcBDl by TinyTexel (CC0/MIT)

#include "SSGI.h"

#include <libultraship/bridge.h>
#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>
#include <fast/backends/gfx_opengl.h>
#include <ship/Context.h>
#ifdef ENABLE_DX11
#include <fast/backends/gfx_direct3d_common.h>
#include <wrl/client.h>
#include <d3dcompiler.h>
using Microsoft::WRL::ComPtr;
#endif

#include "soh/ShipInit.hpp"

#include <string>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <memory>

// =========================================================================
// CVar names
// =========================================================================
#define CVAR_SSGI_ENABLED    "gEnhancements.SSGI.Enabled"
#define CVAR_SSGI_GI_SCALE   "gEnhancements.SSGI.GIScale"
#define CVAR_SSGI_AO_STRENGTH "gEnhancements.SSGI.AOStrength"
#define CVAR_SSGI_THICKNESS  "gEnhancements.SSGI.Thickness"
#define CVAR_SSGI_RAY_LENGTH "gEnhancements.SSGI.RayLength"
#define CVAR_SSGI_DIRS       "gEnhancements.SSGI.Dirs"
#define CVAR_SSGI_TEMPORAL   "gEnhancements.SSGI.Temporal"

// =========================================================================
// OpenGL helpers
// =========================================================================
#ifdef USE_OPENGLES
#include <GLES3/gl3.h>
#else
#ifdef _WIN32
#include <GL/glew.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <SDL2/SDL_opengl.h>
#endif
#endif

// Embedded GLSL sources — used on all platforms so the shaders don't need
// to be readable from the filesystem (important for the Emscripten/web build).

static const char* kVertSrc = R"GLSL(
#ifdef GL_ES
precision highp float;
#endif
out vec2 v_uv;
void main() {
    float x = float((gl_VertexID & 1) * 4) - 1.0;
    float y = float((gl_VertexID >> 1) * 4) - 1.0;
    gl_Position = vec4(x, y, 0.0, 1.0);
    v_uv = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);
}
)GLSL";

static const char* kCompositeSrc = R"GLSL(
#ifdef GL_ES
precision highp float;
#endif
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_colorTex;
uniform sampler2D u_ssgiTex;
uniform float u_giScale;
uniform float u_aoStrength;
void main() {
    vec3  color = texture(u_colorTex, v_uv).rgb;
    vec4  ssgi  = texture(u_ssgiTex,  v_uv);
    vec3  gi    = ssgi.rgb;
    float ao    = ssgi.a;
    float aoFactor    = mix(1.0, ao, u_aoStrength);
    vec3  albedoApprox = sqrt(max(color, vec3(0.0)));
    vec3  indirect     = albedoApprox * gi * u_giScale;
    fragColor = vec4(color * aoFactor + indirect, 1.0);
}
)GLSL";

// The GT-VBGI shader is also embedded. The .glsl files in the ssgi/ folder
// serve as editable references; changes there require re-embedding below.
static const char* kGtgiSrc = R"GLSL(
#ifdef GL_ES
precision highp float;
precision highp int;
#endif
in vec2 v_uv;
out vec4 fragColor;
uniform sampler2D u_colorTex;
uniform highp sampler2D u_depthTex;
uniform sampler2D u_prevSsgiTex;
uniform highp sampler2D u_prevDepthTex;
uniform sampler2D u_velocityTex;
uniform highp mat4 u_invProjMat;
uniform highp mat4 u_projMat;
uniform highp mat4 u_reprojMat;
uniform vec2  u_resolution;
uniform uint  u_frame;
uniform float u_thickness;
uniform float u_rayLength;
uniform uint  u_sampleDirs;
uniform bool  u_temporalEnabled;
uniform float u_frameAccu;
const float Pi = 3.14159265359;
const float SAMPLE_COUNT = 32.0;
float Float01(uint x){return float(x)*(1.0/4294967296.0);}
const uint lcgM_orig=2891336453u;
const uvec4 SEED_ORIG=uvec4(0x5C995C6Du,0x6A3C6A57u,0xC65536CBu,0x3563995Fu);
uvec3 pcg3Mix(uvec3 h){h.x+=h.y*h.z;h.y+=h.z*h.x;h.z+=h.x*h.y;return h;}
uvec3 pcg3Permute(uvec3 h){h=pcg3Mix(h);h^=h>>16u;return pcg3Mix(h);}
uvec3 pcg3_orig(uvec3 h,uint seed){uvec3 c=(seed<<1u)^SEED_ORIG.xyz;return pcg3Permute(h*lcgM_orig+c);}
vec4 Rnd01x4(vec2 uv,uint n){uvec2 uu=uvec2(uv);uvec3 r=pcg3_orig(uvec3(uu,n),0x9CF617FAu);return vec4(Float01(r.x),0.0,Float01(r.y),Float01(r.z));}
uint CountBits(uint v){v=v-((v>>1u)&0x55555555u);v=(v&0x33333333u)+((v>>2u)&0x33333333u);return((v+(v>>4u)&0xF0F0F0Fu)*0x1010101u)>>24u;}
vec4 GetQuaternion(vec3 to){vec3 xyz=vec3(-to.y,to.x,0.0);float s=to.z;float u=inversesqrt(max(0.0,s*0.5+0.5));s=1.0/u;xyz*=u*0.5;return vec4(xyz,s);}
vec3 Transform_Vz0Qz0(vec2 v,vec4 q){float o=q.x*v.y;float c=q.y*v.x;vec3 b=vec3(o-c,-o+c,o-c);return vec3(v,0.0)+2.0*(b*q.yxw);}
highp vec3 VPosFromUVDepth(vec2 px,float d){vec2 uv=px/u_resolution;highp vec4 clip=vec4(uv*2.0-1.0,d*2.0-1.0,1.0);highp vec4 vp=u_invProjMat*clip;return vp.xyz/vp.w;}
vec2 PixelPosFromVPos(highp vec3 vp){highp vec4 clip=u_projMat*vec4(vp,1.0);vec2 ndc=clip.xy/clip.w;return(ndc*0.5+0.5)*u_resolution;}
vec3 ReconstructNormal(vec2 px,vec3 vpos){
    vec3 ppx=VPosFromUVDepth(px+vec2(1,0),textureLod(u_depthTex,(px+vec2(1,0))/u_resolution,0.0).r);
    vec3 ppy=VPosFromUVDepth(px+vec2(0,1),textureLod(u_depthTex,(px+vec2(0,1))/u_resolution,0.0).r);
    vec3 N=normalize(cross(ppy-vpos,ppx-vpos));
    if(N.z<0.0)N=-N;return N;}
vec4 hemiGTVBGI(vec2 uv0,vec3 posVS,vec3 N,uint dirs){
    vec3 V=-normalize(posVS);
    vec2 rayStart=PixelPosFromVPos(posVS);
    uint frm=u_temporalEnabled?u_frame:0u;
    float ao=0.0;vec3 gi=vec3(0.0);
    for(uint i=0u;i<dirs;++i){
        uint n=frm*dirs+i;
        vec4 rnd=Rnd01x4(uv0,n);
        vec2 dir=vec2(cos(rnd.x*Pi),sin(rnd.x*Pi));
        vec3 sd=vec3(dir,0.0);
        {vec4 Q=GetQuaternion(V);sd=Transform_Vz0Qz0(dir,Q);
         float nz=abs(VPosFromUVDepth(u_resolution*0.5,0.0).z)*0.001;
         vec2 p0=PixelPosFromVPos(posVS),p1=PixelPosFromVPos(posVS+sd*nz);
         vec2 s=p1-p0;float sl=length(s);if(sl>0.0001)dir=s/sl;}
        vec3 slN,prN,T;float cN,sN,pNrl;
        {slN=cross(V,sd);prN=N-slN*dot(N,slN);float pl=dot(prN,prN);
         if(pl==0.0)return vec4(0,0,0,1);T=cross(slN,prN);pNrl=inversesqrt(pl);
         cN=dot(prN,V)*pNrl;sN=dot(T,V)*pNrl;}
        vec3 gi0=vec3(0.0);uint occ=0u;
        for(float d=-1.0;d<=1.0;d+=2.0){
            vec2 rd=dir*d;
            const float cnt=SAMPLE_COUNT;
            const float s2=pow(u_rayLength,1.0/cnt);
            float t=pow(s2,rnd.z);rnd.z=1.0-rnd.z;
            for(float si=0.0;si<cnt;si+=1.0){
                vec2 sp=rayStart+rd*t;t*=s2;
                if(sp.x<0.0||sp.x>=u_resolution.x||sp.y<0.0||sp.y>=u_resolution.y)break;
                vec2 suv=sp/u_resolution;
                float sd2=textureLod(u_depthTex,suv,0.0).r;
                if(sd2>=1.0)continue;
                vec3 sRad=textureLod(u_colorTex,suv,0.0).rgb;
                vec3 sPVS=VPosFromUVDepth(sp,sd2);
                vec3 dF=sPVS-posVS,dB=dF-V*u_thickness;
                vec2 hc=vec2(dot(normalize(dF),V),dot(normalize(dB),V));
                hc=d>=0.0?hc.xy:hc.yx;
                vec2 h01;{float d05=d*0.5;h01=((0.5+0.5*sN)+d05)-d05*hc;}
                h01=clamp(h01+rnd.w*(1.0/32.0),0.0,1.0);
                uint ob;{uvec2 hi=uvec2(floor(h01*32.0));uint FF=0xFFFFFFFFu;
                    uint mx=hi.x<32u?(FF<<hi.x):0u,my=hi.y!=0u?(FF>>(32u-hi.y)):0u;ob=mx&my;}
                uint vb=ob&(~occ);
                if(vb!=0u)gi0+=sRad*float(CountBits(vb))*(1.0/32.0);
                occ|=ob;}}
        ao+=1.0-float(CountBits(occ))*(1.0/32.0);gi+=gi0;}
    float norm=1.0/float(dirs);return vec4(gi,ao)*norm;}
void main(){
    vec2 px=v_uv*u_resolution;
    float depth=textureLod(u_depthTex,v_uv,0.0).r;
    if(depth>=1.0){fragColor=vec4(0);return;}
    vec3 posVS=VPosFromUVDepth(px,depth);
    vec3 N=ReconstructNormal(px,posVS);
    posVS+=N*(u_thickness*0.01);
    vec4 giAO=hemiGTVBGI(px,posVS,N,u_sampleDirs);
    vec3 gi=giAO.rgb;float ao=giAO.a;
    if(u_temporalEnabled){
        vec2 vel=textureLod(u_velocityTex,v_uv,0.0).rg;
        bool hv=(vel.x>0.0||vel.y>0.0);
        vec2 puv;float ePD;
        if(hv){puv=vel;highp vec4 cc=vec4(v_uv*2.0-1.0,depth*2.0-1.0,1.0);highp vec4 pc=u_reprojMat*cc;ePD=pc.w>0.0?pc.z/pc.w*0.5+0.5:2.0;}
        else{highp vec4 cc=vec4(v_uv*2.0-1.0,depth*2.0-1.0,1.0);highp vec4 pc=u_reprojMat*cc;if(pc.w<=0.0){fragColor=vec4(gi,ao);return;}puv=pc.xy/pc.w*0.5+0.5;ePD=pc.z/pc.w*0.5+0.5;}
        bool valid=all(greaterThanEqual(puv,vec2(0.0)))&&all(lessThan(puv,vec2(1.0)));
        if(valid){float aPD=textureLod(u_prevDepthTex,puv,0.0).r;float th=0.02*(1.0-ePD);if(aPD<ePD-th)valid=false;}
        if(valid&&u_frameAccu>1.0){vec4 prev=textureLod(u_prevSsgiTex,puv,0.0);float a=1.0/min(u_frameAccu,64.0);gi=mix(prev.rgb,gi,a);ao=mix(prev.a,ao,a);}}
    fragColor=vec4(gi,ao);}
)GLSL";

static std::string LoadShaderFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static GLuint CompileShader(GLenum type, const std::string& src, const std::string& label) {
    GLuint sh = glCreateShader(type);
#ifdef USE_OPENGLES
    const char* versionLine = "#version 300 es\n";
#else
    const char* versionLine = "#version 330 core\n";
#endif
    const char* srcs[2] = { versionLine, src.c_str() };
    glShaderSource(sh, 2, srcs, nullptr);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        printf("[SSGI] Shader compile error (%s): %s\n", label.c_str(), log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint LinkProgram(GLuint vs, GLuint fs, const std::string& label) {
    if (!vs || !fs) return 0;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        printf("[SSGI] Program link error (%s): %s\n", label.c_str(), log);
        glDeleteProgram(prog);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

// 4×4 matrix multiply res = a * b  (row-major)
static void MatMul(float res[4][4], const float a[4][4], const float b[4][4]) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            res[r][c] = 0.f;
            for (int k = 0; k < 4; ++k) res[r][c] += a[r][k] * b[k][c];
        }
}

// 4×4 matrix inversion (general, no special-case for projection)
static bool MatInv(float inv[4][4], const float m[4][4]) {
    float tmp[16];
    const float* src = &m[0][0];
    tmp[0]  =  src[5]*src[10]*src[15] - src[5]*src[11]*src[14] - src[9]*src[6]*src[15] + src[9]*src[7]*src[14] + src[13]*src[6]*src[11] - src[13]*src[7]*src[10];
    tmp[4]  = -src[4]*src[10]*src[15] + src[4]*src[11]*src[14] + src[8]*src[6]*src[15] - src[8]*src[7]*src[14] - src[12]*src[6]*src[11] + src[12]*src[7]*src[10];
    tmp[8]  =  src[4]*src[9] *src[15] - src[4]*src[11]*src[13] - src[8]*src[5]*src[15] + src[8]*src[7]*src[13] + src[12]*src[5]*src[11] - src[12]*src[7]*src[9];
    tmp[12] = -src[4]*src[9] *src[14] + src[4]*src[10]*src[13] + src[8]*src[5]*src[14] - src[8]*src[6]*src[13] - src[12]*src[5]*src[10] + src[12]*src[6]*src[9];
    tmp[1]  = -src[1]*src[10]*src[15] + src[1]*src[11]*src[14] + src[9]*src[2]*src[15] - src[9]*src[3]*src[14] - src[13]*src[2]*src[11] + src[13]*src[3]*src[10];
    tmp[5]  =  src[0]*src[10]*src[15] - src[0]*src[11]*src[14] - src[8]*src[2]*src[15] + src[8]*src[3]*src[14] + src[12]*src[2]*src[11] - src[12]*src[3]*src[10];
    tmp[9]  = -src[0]*src[9] *src[15] + src[0]*src[11]*src[13] + src[8]*src[1]*src[15] - src[8]*src[3]*src[13] - src[12]*src[1]*src[11] + src[12]*src[3]*src[9];
    tmp[13] =  src[0]*src[9] *src[14] - src[0]*src[10]*src[13] - src[8]*src[1]*src[14] + src[8]*src[2]*src[13] + src[12]*src[1]*src[10] - src[12]*src[2]*src[9];
    tmp[2]  =  src[1]*src[6] *src[15] - src[1]*src[7] *src[14] - src[5]*src[2]*src[15] + src[5]*src[3]*src[14] + src[13]*src[2]*src[7]  - src[13]*src[3]*src[6];
    tmp[6]  = -src[0]*src[6] *src[15] + src[0]*src[7] *src[14] + src[4]*src[2]*src[15] - src[4]*src[3]*src[14] - src[12]*src[2]*src[7]  + src[12]*src[3]*src[6];
    tmp[10] =  src[0]*src[5] *src[15] - src[0]*src[7] *src[13] - src[4]*src[1]*src[15] + src[4]*src[3]*src[13] + src[12]*src[1]*src[7]  - src[12]*src[3]*src[5];
    tmp[14] = -src[0]*src[5] *src[14] + src[0]*src[6] *src[13] + src[4]*src[1]*src[14] - src[4]*src[2]*src[13] - src[12]*src[1]*src[6]  + src[12]*src[2]*src[5];
    tmp[3]  = -src[1]*src[6] *src[11] + src[1]*src[7] *src[10] + src[5]*src[2]*src[11] - src[5]*src[3]*src[10] - src[9]*src[2]*src[7]   + src[9]*src[3]*src[6];
    tmp[7]  =  src[0]*src[6] *src[11] - src[0]*src[7] *src[10] - src[4]*src[2]*src[11] + src[4]*src[3]*src[10] + src[8]*src[2]*src[7]   - src[8]*src[3]*src[6];
    tmp[11] = -src[0]*src[5] *src[11] + src[0]*src[7] *src[9]  + src[4]*src[1]*src[11] - src[4]*src[3]*src[9]  - src[8]*src[1]*src[7]   + src[8]*src[3]*src[5];
    tmp[15] =  src[0]*src[5] *src[10] - src[0]*src[6] *src[9]  - src[4]*src[1]*src[10] + src[4]*src[2]*src[9]  + src[8]*src[1]*src[6]   - src[8]*src[2]*src[5];
    float det = src[0]*tmp[0] + src[1]*tmp[4] + src[2]*tmp[8] + src[3]*tmp[12];
    if (fabsf(det) < 1e-20f) return false;
    float invDet = 1.0f / det;
    for (int i = 0; i < 16; ++i) (&inv[0][0])[i] = tmp[i] * invDet;
    return true;
}

// =========================================================================
// SSGI state (singleton)
// =========================================================================
namespace {

struct SSGIState {
    // Shader programs
    GLuint progGTGI      = 0;  // GT-VBGI pass
    GLuint progComposite  = 0;  // composite pass
    GLuint progDepthCopy  = 0;  // copies game depth to prevDepthTex (R32F)

    // Ping-pong SSGI result FBOs
    GLuint ssgiFbo[2]    = {};
    GLuint ssgiTex[2]    = {};
    int    pingPong      = 0;

    // Previous-frame depth (R32F colour texture, copied from game depth each frame)
    GLuint prevDepthFbo  = 0;
    GLuint prevDepthTex  = 0;

    // Output FBO (final composited result — replaces game FBO in GUI)
    GLuint outFbo        = 0;
    GLuint outTex        = 0;

    // Geometry (fullscreen triangle, no VBO needed — use gl_VertexID)
    GLuint vao           = 0;

    uint32_t lastWidth   = 0;
    uint32_t lastHeight  = 0;

    // Temporal / reprojection state
    float  frameAccu        = 1.0f;
    float  prev_VP[4][4]    = {};   // previous frame VP (N64 row-major: V×P)
    bool   prevVPValid      = false;

    std::string shaderDir;

    bool Init(const std::string& shaderDirectory);
    void Resize(uint32_t w, uint32_t h);
    void Apply(int gameFb, Fast::GfxRenderingAPIOGL* rapi,
               uint32_t width, uint32_t height,
               float proj[4][4], float view[4][4],
               uintptr_t& outFrameBuffer);
    void Shutdown();
};

SSGIState g_ssgi;

// ---- FBO helpers ----
static void CreateRGBA16FTexFbo(GLuint& fbo, GLuint& tex, uint32_t w, uint32_t h) {
    if (!fbo) glGenFramebuffers(1, &fbo);
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---- Uniform helpers ----
static void SetUniformMat4(GLuint prog, const char* name, const float m[4][4]) {
    GLint loc = glGetUniformLocation(prog, name);
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_TRUE, &m[0][0]); // GL_TRUE = row-major
}
static void SetUniform1i(GLuint prog, const char* name, int v) {
    GLint loc = glGetUniformLocation(prog, name);
    if (loc >= 0) glUniform1i(loc, v);
}
static void SetUniform1f(GLuint prog, const char* name, float v) {
    GLint loc = glGetUniformLocation(prog, name);
    if (loc >= 0) glUniform1f(loc, v);
}
static void SetUniform2f(GLuint prog, const char* name, float x, float y) {
    GLint loc = glGetUniformLocation(prog, name);
    if (loc >= 0) glUniform2f(loc, x, y);
}
static void SetUniform1ui(GLuint prog, const char* name, unsigned v) {
    GLint loc = glGetUniformLocation(prog, name);
    if (loc >= 0) glUniform1ui(loc, v);
}

// Inline depth-copy fragment shader: reads a depth texture and writes it as a
// red-channel colour value (R32F target).  Used to preserve previous-frame depth.
static const char* kDepthCopySrc =
    "in vec2 v_uv;\n"
    "out float fragDepth;\n"
    "uniform highp sampler2D u_srcDepth;\n"
    "void main() { fragDepth = texture(u_srcDepth, v_uv).r; }\n";

// ---- Compile shaders from files ----
bool SSGIState::Init(const std::string& shaderDirectory) {
    shaderDir = shaderDirectory;
    // Use embedded shader sources — no filesystem access required (works on web).
    std::string vertSrc = kVertSrc;
    std::string gtgiSrc = kGtgiSrc;
    std::string compSrc = kCompositeSrc;
    (void)shaderDirectory; // kept for API compatibility

    GLuint vs = CompileShader(GL_VERTEX_SHADER, vertSrc, "vert");
    GLuint fsGtgi = CompileShader(GL_FRAGMENT_SHADER, gtgiSrc, "gtgi");
    progGTGI = LinkProgram(vs, fsGtgi, "gtgi");

    vs = CompileShader(GL_VERTEX_SHADER, vertSrc, "vert");
    GLuint fsComp = CompileShader(GL_FRAGMENT_SHADER, compSrc, "composite");
    progComposite = LinkProgram(vs, fsComp, "composite");

    vs = CompileShader(GL_VERTEX_SHADER, vertSrc, "vert");
    GLuint fsDCopy = CompileShader(GL_FRAGMENT_SHADER, kDepthCopySrc, "depthcopy");
    progDepthCopy = LinkProgram(vs, fsDCopy, "depthcopy");

    if (!progGTGI || !progComposite || !progDepthCopy) {
        printf("[SSGI] Shader compilation failed.\n");
        return false;
    }

    // VAO for fullscreen triangle (no VBO, uses gl_VertexID)
    glGenVertexArrays(1, &vao);

    printf("[SSGI] Initialized.\n");
    return true;
}

void SSGIState::Resize(uint32_t w, uint32_t h) {
    if (w == lastWidth && h == lastHeight) return;
    lastWidth  = w;
    lastHeight = h;

    // Recreate SSGI ping-pong textures
    CreateRGBA16FTexFbo(ssgiFbo[0], ssgiTex[0], w, h);
    CreateRGBA16FTexFbo(ssgiFbo[1], ssgiTex[1], w, h);

    // Previous-frame depth (R32F colour texture used as depth store)
    if (!prevDepthFbo) glGenFramebuffers(1, &prevDepthFbo);
    if (!prevDepthTex) glGenTextures(1, &prevDepthTex);
    glBindTexture(GL_TEXTURE_2D, prevDepthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, prevDepthFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, prevDepthTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Recreate output composite texture
    if (!outFbo) glGenFramebuffers(1, &outFbo);
    if (!outTex) glGenTextures(1, &outTex);
    glBindTexture(GL_TEXTURE_2D, outTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, outFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Reset temporal when resolution changes
    frameAccu = 1.0f;
}

void SSGIState::Apply(int gameFb, Fast::GfxRenderingAPIOGL* rapi,
                      uint32_t width, uint32_t height,
                      float proj[4][4], float view[4][4],
                      uintptr_t& outFrameBuffer) {
    if (!progGTGI || !progComposite || !progDepthCopy) return;

    Resize(width, height);

    GLuint colorTex    = (GLuint)(uintptr_t)rapi->GetFramebufferTextureId(gameFb);
    GLuint depthTex    = rapi->GetDepthTextureId(gameFb);
    GLuint velocityTex = rapi->GetVelocityTextureId(gameFb);
    if (!colorTex || !depthTex) return;

    // ---- Compute current VP = V × P (N64 row-major convention) ----
    float curr_VP[4][4];
    MatMul(curr_VP, view, proj);

    // ---- Reprojection matrix: inv(VP_curr) × VP_prev ----
    // Passed as GL_TRUE (row-major) → GLSL receives T(reproj),
    // which correctly transforms curr_clip → prev_clip.
    // On the very first frame prev_VP = curr_VP → reproj = Identity.
    float inv_curr_VP[4][4];
    if (!MatInv(inv_curr_VP, curr_VP)) {
        // Degenerate matrix (e.g. during loading) — skip this frame.
        memcpy(prev_VP, curr_VP, sizeof(curr_VP));
        prevVPValid = true;
        return;
    }
    if (!prevVPValid) memcpy(prev_VP, curr_VP, sizeof(curr_VP));

    float reproj[4][4];
    MatMul(reproj, inv_curr_VP, prev_VP);  // = inv(VP_curr) × VP_prev

    // Detect scene discontinuities (room transitions, cutscene cuts) by
    // checking whether the reprojection moves nearly every pixel off-screen.
    // A simple heuristic: compare diagonal of reproj to identity.
    bool sceneChange = false;
    for (int i = 0; i < 4 && !sceneChange; ++i)
        if (fabsf(reproj[i][i] - 1.0f) > 0.5f) sceneChange = true;
    if (sceneChange) {
        frameAccu = 1.0f;
        memcpy(prev_VP, curr_VP, sizeof(curr_VP));
        MatMul(reproj, inv_curr_VP, prev_VP);  // now = Identity
    }

    // Inverse projection for view-space reconstruction
    float invProj[4][4];
    if (!MatInv(invProj, proj)) return;

    // ---- Save / modify GL state ----
    GLint prevFbo, prevViewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLboolean depthTestWas = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blendWas     = glIsEnabled(GL_BLEND);
    GLboolean scissorWas   = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, width, height);
    glBindVertexArray(vao);

    int cur = pingPong;
    int prv = 1 - pingPong;

    // ============================================================
    // Pass 1: GT-VBGI with motion-vector advection → ssgiFbo[cur]
    // ============================================================
    glBindFramebuffer(GL_FRAMEBUFFER, ssgiFbo[cur]);
    glUseProgram(progGTGI);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, colorTex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, depthTex);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, ssgiTex[prv]);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, prevDepthTex);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, velocityTex ? velocityTex : 0);

    SetUniform1i(progGTGI, "u_colorTex",     0);
    SetUniform1i(progGTGI, "u_depthTex",     1);
    SetUniform1i(progGTGI, "u_prevSsgiTex",  2);
    SetUniform1i(progGTGI, "u_prevDepthTex", 3);
    SetUniform1i(progGTGI, "u_velocityTex",  4);

    SetUniformMat4(progGTGI, "u_invProjMat", invProj);
    SetUniformMat4(progGTGI, "u_projMat",    proj);
    SetUniformMat4(progGTGI, "u_reprojMat",  reproj);

    SetUniform2f (progGTGI, "u_resolution",     (float)width, (float)height);
    SetUniform1ui(progGTGI, "u_frame",           (unsigned)frameAccu);
    SetUniform1f (progGTGI, "u_thickness",       CVarGetFloat(CVAR_SSGI_THICKNESS,  200.0f));
    SetUniform1f (progGTGI, "u_rayLength",       CVarGetFloat(CVAR_SSGI_RAY_LENGTH, 512.0f));
    SetUniform1ui(progGTGI, "u_sampleDirs",      (unsigned)CVarGetInteger(CVAR_SSGI_DIRS, 1));
    SetUniform1i (progGTGI, "u_temporalEnabled", CVarGetInteger(CVAR_SSGI_TEMPORAL, 1) ? 1 : 0);
    SetUniform1f (progGTGI, "u_frameAccu",       frameAccu);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    // ============================================================
    // Pass 2: Composite → outFbo
    // ============================================================
    glBindFramebuffer(GL_FRAMEBUFFER, outFbo);
    glUseProgram(progComposite);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, colorTex);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, ssgiTex[cur]);

    SetUniform1i(progComposite, "u_colorTex",   0);
    SetUniform1i(progComposite, "u_ssgiTex",    1);
    SetUniform1f(progComposite, "u_giScale",    CVarGetFloat(CVAR_SSGI_GI_SCALE,    0.5f));
    SetUniform1f(progComposite, "u_aoStrength", CVarGetFloat(CVAR_SSGI_AO_STRENGTH, 0.6f));

    glDrawArrays(GL_TRIANGLES, 0, 3);

    // ============================================================
    // Pass 3: Store current depth → prevDepthTex (for next frame)
    // ============================================================
    glBindFramebuffer(GL_FRAMEBUFFER, prevDepthFbo);
    glUseProgram(progDepthCopy);

    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, depthTex);
    SetUniform1i(progDepthCopy, "u_srcDepth", 0);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    // ---- Advance temporal state ----
    pingPong  = prv;            // current accumulation becomes history next frame
    frameAccu = sceneChange ? 1.0f : (frameAccu + 1.0f);
    memcpy(prev_VP, curr_VP, sizeof(curr_VP));
    prevVPValid = true;

    // ---- Restore GL state ----
    glBindVertexArray(0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (depthTestWas) glEnable(GL_DEPTH_TEST);
    if (blendWas)     glEnable(GL_BLEND);
    if (scissorWas)   glEnable(GL_SCISSOR_TEST);

    // Tell the GUI to display our composited output
    outFrameBuffer = (uintptr_t)outTex;
}

void SSGIState::Shutdown() {
    if (progGTGI)      { glDeleteProgram(progGTGI);       progGTGI      = 0; }
    if (progComposite) { glDeleteProgram(progComposite);   progComposite = 0; }
    if (progDepthCopy) { glDeleteProgram(progDepthCopy);   progDepthCopy = 0; }
    for (int i = 0; i < 2; ++i) {
        if (ssgiFbo[i]) { glDeleteFramebuffers(1, &ssgiFbo[i]); ssgiFbo[i] = 0; }
        if (ssgiTex[i]) { glDeleteTextures(1, &ssgiTex[i]);     ssgiTex[i] = 0; }
    }
    if (prevDepthFbo) { glDeleteFramebuffers(1, &prevDepthFbo); prevDepthFbo = 0; }
    if (prevDepthTex) { glDeleteTextures(1, &prevDepthTex);     prevDepthTex = 0; }
    if (outFbo) { glDeleteFramebuffers(1, &outFbo); outFbo = 0; }
    if (outTex) { glDeleteTextures(1, &outTex);     outTex = 0; }
    if (vao)    { glDeleteVertexArrays(1, &vao);    vao = 0; }
    lastWidth = lastHeight = 0;
    prevVPValid = false;
    frameAccu   = 1.0f;
}

} // anonymous namespace

// =========================================================================
// DX11 SSGI implementation
// =========================================================================
#ifdef ENABLE_DX11

// Constant buffer layout must match the HLSL CB definitions.
struct SSGIConstantBufferDX11 {
    float invProjDX[4][4];
    float projDX[4][4];
    float reprojDX[4][4];
    float resolution[2];
    float thickness;
    float rayLength;
    uint32_t frame;
    uint32_t sampleDirs;
    uint32_t temporalEnabled;
    float frameAccu;
};

struct SSGICompositeCB {
    float giScale;
    float aoStrength;
    float padding[2];
};

namespace {

// DX11 coordinate-conversion matrix (maps GL clip → DX11 clip).
// In the VBO, z = (gl_z + gl_w) / 2 and y = -gl_y, so:
//   C.col[1][1] = -1   (flip y)
//   C.col[2][2] = 0.5, C.col[3][2] = 0.5  (z remapping; N64 row-major)
static void BuildDXConvMatrix(float C[4][4]) {
    memset(C, 0, 16 * sizeof(float));
    C[0][0] = 1.f; C[1][1] = -1.f;
    C[2][2] = 0.5f; C[3][2] = 0.5f; C[3][3] = 1.f;
}

static ComPtr<ID3DBlob> CompileHLSL(const std::string& src, const char* entry,
                                     const char* target, HWND hwnd) {
    HMODULE mod = GetModuleHandleW(L"D3DCompiler_47.dll");
    if (!mod) mod = LoadLibraryW(L"D3DCompiler_47.dll");
    if (!mod) return nullptr;
    auto d3dCompile = (pD3DCompile)GetProcAddress(mod, "D3DCompile");
    if (!d3dCompile) return nullptr;

    ComPtr<ID3DBlob> blob, err;
    HRESULT hr = d3dCompile(src.c_str(), src.size(), nullptr, nullptr, nullptr,
                             entry, target,
                             D3DCOMPILE_OPTIMIZATION_LEVEL2, 0,
                             blob.GetAddressOf(), err.GetAddressOf());
    if (FAILED(hr)) {
        if (err) printf("[SSGI DX11] %s compile error: %s\n", entry, (char*)err->GetBufferPointer());
        return nullptr;
    }
    return blob;
}

struct SSGIStateDX11 {
    ComPtr<ID3D11VertexShader>   vsGtgi, vsComposite, vsDepthCopy;
    ComPtr<ID3D11PixelShader>    psGtgi, psComposite, psDepthCopy;
    ComPtr<ID3D11Buffer>         cbGtgi, cbComposite;

    // Ping-pong SSGI textures (RGBA16F: RGB=gi, A=ao)
    ComPtr<ID3D11Texture2D>          ssgiTex[2];
    ComPtr<ID3D11RenderTargetView>   ssgiRtv[2];
    ComPtr<ID3D11ShaderResourceView> ssgiSrv[2];
    int pingPong = 0;

    // Previous depth (R32F)
    ComPtr<ID3D11Texture2D>          prevDepthTex;
    ComPtr<ID3D11RenderTargetView>   prevDepthRtv;
    ComPtr<ID3D11ShaderResourceView> prevDepthSrv;

    // Depth-copy pixel shader (reads depth SRV, writes R32F)
    ComPtr<ID3D11PixelShader>        psDepthCopyImpl;

    // Final composited output (RGBA8)
    ComPtr<ID3D11Texture2D>          outTex;
    ComPtr<ID3D11RenderTargetView>   outRtv;
    ComPtr<ID3D11ShaderResourceView> outSrv;

    // Shared rendering state
    ComPtr<ID3D11SamplerState>       sampPoint, sampLinear;
    ComPtr<ID3D11RasterizerState>    rsState;
    ComPtr<ID3D11DepthStencilState>  dsState;
    ComPtr<ID3D11BlendState>         blendOpaque;

    uint32_t lastWidth = 0, lastHeight = 0;
    float    frameAccu = 1.f;
    float    prevVP[4][4] = {};
    bool     prevVPValid  = false;

    static void CreateTex2D(ID3D11Device* dev, uint32_t w, uint32_t h, DXGI_FORMAT fmt,
                             UINT bind, ComPtr<ID3D11Texture2D>& tex,
                             ComPtr<ID3D11RenderTargetView>* rtv,
                             ComPtr<ID3D11ShaderResourceView>* srv) {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1;
        d.Format = fmt; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = bind;
        tex.Reset();
        if (FAILED(dev->CreateTexture2D(&d, nullptr, tex.GetAddressOf()))) return;
        if (rtv) dev->CreateRenderTargetView(tex.Get(), nullptr, rtv->ReleaseAndGetAddressOf());
        if (srv) dev->CreateShaderResourceView(tex.Get(), nullptr, srv->ReleaseAndGetAddressOf());
    }

    bool Init(ID3D11Device* dev, const std::string& shaderDir) {
        auto gtgiSrc = LoadShaderFile(shaderDir + "/ssgi_gtgi.hlsl");
        auto compSrc = LoadShaderFile(shaderDir + "/ssgi_composite.hlsl");
        if (gtgiSrc.empty() || compSrc.empty()) {
            printf("[SSGI DX11] Failed to load HLSL shaders from: %s\n", shaderDir.c_str());
            return false;
        }

        // Depth-copy shader: reads depth SRV, writes to R32F RTV
        static const char kDepthCopyHLSL[] =
            "Texture2D<float> t:register(t0); SamplerState s:register(s0);\n"
            "void VS(uint id:SV_VertexID,out float4 p:SV_POSITION,out float2 uv:TEXCOORD0){\n"
            "  float x=(id==1)?3:-1; float y=(id==2)?-3:1;\n"
            "  p=float4(x,y,0,1); uv=float2(x*0.5+0.5,1-(y*0.5+0.5));}\n"
            "float PS(float4 p:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET{\n"
            "  return t.SampleLevel(s,uv,0);}\n";

        auto compileVS = [&](const std::string& src, const char* entry) -> ComPtr<ID3D11VertexShader> {
            auto b = CompileHLSL(src, entry, "vs_4_0", nullptr);
            if (!b) return nullptr;
            ComPtr<ID3D11VertexShader> vs;
            dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, vs.GetAddressOf());
            return vs;
        };
        auto compilePS = [&](const std::string& src, const char* entry) -> ComPtr<ID3D11PixelShader> {
            auto b = CompileHLSL(src, entry, "ps_4_0", nullptr);
            if (!b) return nullptr;
            ComPtr<ID3D11PixelShader> ps;
            dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, ps.GetAddressOf());
            return ps;
        };

        vsGtgi     = compileVS(gtgiSrc,           "VSMain");
        psGtgi     = compilePS(gtgiSrc,           "PSMain");
        vsComposite= compileVS(compSrc,           "VSMain");
        psComposite= compilePS(compSrc,           "PSMain");
        vsDepthCopy= compileVS(kDepthCopyHLSL,   "VS");
        psDepthCopyImpl = compilePS(kDepthCopyHLSL, "PS");

        if (!vsGtgi || !psGtgi || !vsComposite || !psComposite || !vsDepthCopy || !psDepthCopyImpl) {
            printf("[SSGI DX11] Shader compilation failed.\n");
            return false;
        }

        // Constant buffers
        auto makeCB = [&](UINT sz) -> ComPtr<ID3D11Buffer> {
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = (sz + 15) & ~15u;  // 16-byte aligned
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            ComPtr<ID3D11Buffer> buf;
            dev->CreateBuffer(&bd, nullptr, buf.GetAddressOf());
            return buf;
        };
        cbGtgi     = makeCB(sizeof(SSGIConstantBufferDX11));
        cbComposite= makeCB(sizeof(SSGICompositeCB));

        // Samplers
        D3D11_SAMPLER_DESC sd = {};
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        dev->CreateSamplerState(&sd, sampPoint.ReleaseAndGetAddressOf());
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        dev->CreateSamplerState(&sd, sampLinear.ReleaseAndGetAddressOf());

        // Rasterizer: no cull, no depth clip
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = false;
        dev->CreateRasterizerState(&rd, rsState.ReleaseAndGetAddressOf());

        // Depth-stencil: no depth test
        D3D11_DEPTH_STENCIL_DESC dsd = {};
        dsd.DepthEnable = false; dsd.StencilEnable = false;
        dev->CreateDepthStencilState(&dsd, dsState.ReleaseAndGetAddressOf());

        // Blend: no blending
        D3D11_BLEND_DESC bld = {};
        bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        dev->CreateBlendState(&bld, blendOpaque.ReleaseAndGetAddressOf());

        printf("[SSGI DX11] Initialized.\n");
        return true;
    }

    void Resize(ID3D11Device* dev, uint32_t w, uint32_t h) {
        if (w == lastWidth && h == lastHeight) return;
        lastWidth = w; lastHeight = h;

        auto B = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        CreateTex2D(dev, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, B, ssgiTex[0], &ssgiRtv[0], &ssgiSrv[0]);
        CreateTex2D(dev, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, B, ssgiTex[1], &ssgiRtv[1], &ssgiSrv[1]);
        CreateTex2D(dev, w, h, DXGI_FORMAT_R32_FLOAT,          B, prevDepthTex, &prevDepthRtv, &prevDepthSrv);
        CreateTex2D(dev, w, h, DXGI_FORMAT_R8G8B8A8_UNORM,    B, outTex, &outRtv, &outSrv);

        frameAccu = 1.f;
    }

    void UpdateCB(ID3D11DeviceContext* ctx, ID3D11Buffer* buf, const void* data, UINT sz) {
        D3D11_MAPPED_SUBRESOURCE mr;
        if (SUCCEEDED(ctx->Map(buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mr))) {
            memcpy(mr.pData, data, sz);
            ctx->Unmap(buf, 0);
        }
    }

    // Run a fullscreen pass: bind VS/PS, SRVs, CB, draw 3 vertices, output to rtv.
    void RunPass(ID3D11DeviceContext* ctx,
                 ID3D11VertexShader* vs, ID3D11PixelShader* ps,
                 ID3D11RenderTargetView* rtv,
                 ID3D11ShaderResourceView* const* srvs, UINT srvCount,
                 ID3D11Buffer* cb,
                 uint32_t w, uint32_t h) {
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        ctx->OMSetRenderTargets(1, &rtv, nullptr);

        D3D11_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 };
        ctx->RSSetViewports(1, &vp);

        ctx->PSSetShaderResources(0, srvCount, srvs);
        if (cb) ctx->PSSetConstantBuffers(0, 1, &cb);

        ID3D11SamplerState* samps[] = { sampPoint.Get(), sampLinear.Get() };
        ctx->PSSetSamplers(0, 2, samps);

        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

        ctx->Draw(3, 0);
    }

    void Apply(Fast::GfxRenderingAPIDX11* dx11, int gameFb,
               uint32_t w, uint32_t h,
               float proj[4][4], float view[4][4],
               uintptr_t& outFrameBuffer) {

        auto* dev = dx11->mDevice.Get();
        auto* ctx = dx11->mContext.Get();

        Resize(dev, w, h);

        auto* colorSrv   = (ID3D11ShaderResourceView*)dx11->GetFramebufferTextureId(gameFb);
        auto* depthSrv   = dx11->GetDepthSrv(gameFb);
        auto* velocitySrv= dx11->GetVelocitySrv(gameFb);
        if (!colorSrv || !depthSrv) return;

        // ---- Compute DX11-adjusted matrices ----
        float C[4][4];
        BuildDXConvMatrix(C);

        float curr_VP[4][4], proj_dx[4][4], inv_proj_dx[4][4];
        MatMul(curr_VP, view, proj);
        MatMul(proj_dx, proj, C);
        if (!MatInv(inv_proj_dx, proj_dx)) return;

        if (!prevVPValid) memcpy(prevVP, curr_VP, sizeof(curr_VP));

        float prev_VP_dx[4][4], curr_VP_dx[4][4], inv_curr_VP_dx[4][4], reproj_dx[4][4];
        MatMul(curr_VP_dx, curr_VP,  C);
        MatMul(prev_VP_dx, prevVP,   C);
        MatInv(inv_curr_VP_dx, curr_VP_dx);
        MatMul(reproj_dx, inv_curr_VP_dx, prev_VP_dx);

        bool sceneChange = false;
        for (int r = 0; r < 4 && !sceneChange; ++r)
            if (fabsf(reproj_dx[r][r] - 1.f) > 0.5f) sceneChange = true;
        if (sceneChange) { frameAccu = 1.f; memcpy(prevVP, curr_VP, sizeof(curr_VP)); MatMul(reproj_dx, inv_curr_VP_dx, curr_VP_dx); }

        // ---- Save and set shared D3D state ----
        // (save current render targets / viewport so game state is preserved)
        ComPtr<ID3D11RenderTargetView> prevRtv;
        ComPtr<ID3D11DepthStencilView> prevDsv;
        D3D11_VIEWPORT prevVP_vp;  UINT numVP = 1;
        ctx->OMGetRenderTargets(1, prevRtv.GetAddressOf(), prevDsv.GetAddressOf());
        ctx->RSGetViewports(&numVP, &prevVP_vp);

        ctx->RSSetState(rsState.Get());
        ctx->OMSetDepthStencilState(dsState.Get(), 0);
        const float blendFactor[4] = {};
        ctx->OMSetBlendState(blendOpaque.Get(), blendFactor, 0xffffffff);

        int cur = pingPong, prv = 1 - pingPong;

        // ---- Pass 1: GT-VBGI → ssgiTex[cur] ----
        {
            SSGIConstantBufferDX11 cb = {};
            memcpy(cb.invProjDX, inv_proj_dx, sizeof(inv_proj_dx));
            memcpy(cb.projDX,    proj_dx,     sizeof(proj_dx));
            memcpy(cb.reprojDX,  reproj_dx,   sizeof(reproj_dx));
            cb.resolution[0]   = (float)w;  cb.resolution[1] = (float)h;
            cb.thickness       = CVarGetFloat(CVAR_SSGI_THICKNESS, 200.f);
            cb.rayLength       = CVarGetFloat(CVAR_SSGI_RAY_LENGTH, 512.0f);
            cb.frame           = (uint32_t)frameAccu;
            cb.sampleDirs      = (uint32_t)CVarGetInteger(CVAR_SSGI_DIRS, 1);
            cb.temporalEnabled = CVarGetInteger(CVAR_SSGI_TEMPORAL, 1) ? 1u : 0u;
            cb.frameAccu       = frameAccu;
            UpdateCB(ctx, cbGtgi.Get(), &cb, sizeof(cb));

            // Unbind SRVs that might be bound as RTVs
            ID3D11ShaderResourceView* nullSrvs[5] = {};
            ctx->PSSetShaderResources(0, 5, nullSrvs);

            ID3D11ShaderResourceView* srvs[5] = {
                colorSrv, depthSrv, ssgiSrv[prv].Get(), prevDepthSrv.Get(),
                velocitySrv ? velocitySrv : nullptr
            };
            RunPass(ctx, vsGtgi.Get(), psGtgi.Get(), ssgiRtv[cur].Get(),
                    srvs, 5, cbGtgi.Get(), w, h);
        }

        // ---- Pass 2: Composite → outTex ----
        {
            SSGICompositeCB cb = {};
            cb.giScale    = CVarGetFloat(CVAR_SSGI_GI_SCALE,    0.5f);
            cb.aoStrength = CVarGetFloat(CVAR_SSGI_AO_STRENGTH, 0.6f);
            UpdateCB(ctx, cbComposite.Get(), &cb, sizeof(cb));

            ID3D11ShaderResourceView* srvs[2] = { colorSrv, ssgiSrv[cur].Get() };
            RunPass(ctx, vsComposite.Get(), psComposite.Get(), outRtv.Get(),
                    srvs, 2, cbComposite.Get(), w, h);
        }

        // ---- Pass 3: Store depth for next frame ----
        {
            ID3D11ShaderResourceView* srvs[1] = { depthSrv };
            RunPass(ctx, vsDepthCopy.Get(), psDepthCopyImpl.Get(), prevDepthRtv.Get(),
                    srvs, 1, nullptr, w, h);
        }

        // ---- Advance state ----
        pingPong  = prv;
        frameAccu = sceneChange ? 1.f : (frameAccu + 1.f);
        memcpy(prevVP, curr_VP, sizeof(curr_VP));
        prevVPValid = true;

        // ---- Restore D3D state ----
        {
            ID3D11ShaderResourceView* nullSrvs[5] = {};
            ctx->PSSetShaderResources(0, 5, nullSrvs);
        }
        ctx->OMSetRenderTargets(1, prevRtv.GetAddressOf(), prevDsv.Get());
        ctx->RSSetViewports(1, &prevVP_vp);
        ctx->RSSetState(nullptr);
        ctx->OMSetDepthStencilState(nullptr, 0);
        ctx->OMSetBlendState(nullptr, blendFactor, 0xffffffff);

        // Return the SRV so the GUI displays the composited output
        outFrameBuffer = (uintptr_t)outSrv.Get();
    }

    void Shutdown() {
        vsGtgi.Reset(); psGtgi.Reset();
        vsComposite.Reset(); psComposite.Reset();
        vsDepthCopy.Reset(); psDepthCopyImpl.Reset();
        cbGtgi.Reset(); cbComposite.Reset();
        for (int i = 0; i < 2; i++) { ssgiTex[i].Reset(); ssgiRtv[i].Reset(); ssgiSrv[i].Reset(); }
        prevDepthTex.Reset(); prevDepthRtv.Reset(); prevDepthSrv.Reset();
        outTex.Reset(); outRtv.Reset(); outSrv.Reset();
        sampPoint.Reset(); sampLinear.Reset(); rsState.Reset(); dsState.Reset(); blendOpaque.Reset();
        lastWidth = lastHeight = 0;
        prevVPValid = false; frameAccu = 1.f;
    }
};

SSGIStateDX11 g_ssgi_dx11;

} // anonymous namespace (DX11)
#endif // ENABLE_DX11

// =========================================================================
// Registration
// =========================================================================
namespace SSGI {

// Resolve the base path to this file's directory at runtime
static std::string GetShaderDir() {
    // Shaders live in the same directory as this source file.
    // At runtime we look for them relative to the working directory.
    return "soh/soh/Enhancements/ssgi";
}

void Register() {
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(
        Ship::Context::GetInstance()->GetWindow());
    if (!wnd) return;

    auto interp = wnd->GetInterpreterWeak().lock();
    if (!interp) return;

    // NOTE: mForceRenderToFb is set dynamically inside the callback each frame
    // so that normal rendering is completely unaffected when SSGI is disabled.

    // Initialize SSGI shaders (deferred until first frame so GL context is ready)
    bool initialized = false;

    interp->mPostProcessCallback = [initialized](
        int gameFb, Fast::GfxRenderingAPI* rapi,
        uint32_t width, uint32_t height,
        float proj[4][4], float view[4][4],
        uintptr_t& outFrameBuffer) mutable
    {
        bool ssgiEnabled = CVarGetInteger(CVAR_SSGI_ENABLED, 0) != 0;

        // Keep the interpreter's force-FBO flag in sync with the CVar so that
        // when SSGI is off the game renders normally (no off-screen FBO detour).
        {
            auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(
                Ship::Context::GetInstance()->GetWindow());
            if (wnd) {
                if (auto interp = wnd->GetInterpreterWeak().lock())
                    interp->mForceRenderToFb = ssgiEnabled;
            }
        }

        // ---- OpenGL path ----
        if (auto* oglRapi = dynamic_cast<Fast::GfxRenderingAPIOGL*>(rapi)) {
            if (!ssgiEnabled) {
                // Disable MRT velocity write so non-SSGI shaders don't error.
                oglRapi->SetVelocityBufferEnabled(false);
                if (initialized) {
                    g_ssgi.Shutdown();
                    initialized = false;
                }
                return;
            }
            // Enable MRT velocity write for next frame's game render.
            oglRapi->SetVelocityBufferEnabled(true);
            if (!initialized) {
                if (!g_ssgi.Init(GetShaderDir())) return;
                initialized = true;
            }
            g_ssgi.Apply(gameFb, oglRapi, width, height, proj, view, outFrameBuffer);
            return;
        }

#ifdef ENABLE_DX11
        // ---- DirectX 11 path ----
        if (auto* dx11Rapi = dynamic_cast<Fast::GfxRenderingAPIDX11*>(rapi)) {
            if (!ssgiEnabled) {
                if (initialized) { g_ssgi_dx11.Shutdown(); initialized = false; }
                return;
            }
            if (!initialized) {
                if (!g_ssgi_dx11.Init(dx11Rapi->mDevice.Get(), GetShaderDir())) return;
                initialized = true;
            }
            g_ssgi_dx11.Apply(dx11Rapi, gameFb, width, height, proj, view, outFrameBuffer);
            return;
        }
#endif
    };
}

} // namespace SSGI

// =========================================================================
// ShipInit auto-registration
// =========================================================================
static void RegisterSSGI() {
    SSGI::Register();
}

static RegisterShipInitFunc ssgiInitFunc(RegisterSSGI, { CVAR_SSGI_ENABLED });
