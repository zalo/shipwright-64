// GT-VBGI – adapted for Shipwright DX11 backend
// Original algorithm: https://www.shadertoy.com/view/XfcBDl by TinyTexel (CC0/MIT)
//
// Fullscreen-triangle vertex shader (no vertex buffer needed).

void VSMain(uint id       : SV_VertexID,
            out float4 pos : SV_POSITION,
            out float2 uv  : TEXCOORD0) {
    float x = (id == 1) ? 3.0f : -1.0f;
    float y = (id == 2) ? -3.0f : 1.0f;
    pos    = float4(x, y, 0.0f, 1.0f);
    uv.x   = x * 0.5f + 0.5f;
    uv.y   = 1.0f - (y * 0.5f + 0.5f);   // DX: NDC y-up → UV y-down
}

// ---------- Textures & samplers ----------
Texture2D<float4> t_color     : register(t0);  // game color (RGB)
Texture2D<float>  t_depth     : register(t1);  // game depth [0,1] (DX11 convention)
Texture2D<float4> t_prevSsgi  : register(t2);  // previous SSGI result (RGBA16F)
Texture2D<float>  t_prevDepth : register(t3);  // previous frame depth (R32F)
Texture2D<float2> t_velocity  : register(t4);  // per-pixel velocity (RG16F, DX11 UV space)

SamplerState s_point : register(s0);  // nearest – for depth/velocity
SamplerState s_linear: register(s1);  // bilinear – for color/ssgi

// ---------- Constant buffer ----------
cbuffer CB : register(b0) {
    row_major float4x4 invProjDX;  // inverse of DX11-adjusted projection matrix
    row_major float4x4 projDX;     // DX11-adjusted projection matrix
    row_major float4x4 reprojDX;   // inv(VP_curr_dx) × VP_prev_dx
    float2  resolution;
    float   thickness;             // surface thickness in view-space units
    float   rayLength;             // screen-space ray length in pixels
    uint    frame;
    uint    sampleDirs;
    bool    temporalEnabled;
    float   frameAccu;
};

// ---------- Constants ----------
static const float Pi = 3.14159265359f;

// Raymarching — matches original Shadertoy defaults exactly
static const float SAMPLE_COUNT_H = 32.0f; // Raymarching_SampleCount from original

// ---------- Hash (exact match to original common.glsl) ----------
// Reference: shadertoy.com/view/XfcBDl

float Float01(uint x) { return float(x) * (1.0f / 4294967296.0f); }

static const uint  lcgM_h  = 2891336453u;
static const uint4 SEED_h  = uint4(0x5C995C6Du, 0x6A3C6A57u, 0xC65536CBu, 0x3563995Fu);

uint3 pcg3Mix_h(uint3 h) {
    h.x += h.y * h.z;
    h.y += h.z * h.x;
    h.z += h.x * h.y;
    return h;
}

uint3 pcg3Permute_h(uint3 h) {
    h = pcg3Mix_h(h);
    h ^= h >> 16u;
    return pcg3Mix_h(h);
}

// Exact match to original: pcg3(uvec3 h, uint seed)
uint3 pcg3_h(uint3 h, uint seed) {
    uint3 c = (seed << 1u) ^ SEED_h.xyz;
    return pcg3Permute_h(h * lcgM_h + c);
}

// Exact match to original Rnd01x4 (white-noise path):
//   rnd01.xzw = Hash01x3(uvec3(uvu, n), 0x9CF617FAu)
//   rnd01.y is unused (reserved for importance-sampled direction)
float4 Rnd01x4(float2 uv, uint n) {
    uint3 r = pcg3_h(uint3((uint2)uv, n), 0x9CF617FAu);
    return float4(Float01(r.x), 0.0f, Float01(r.y), Float01(r.z));
}

uint CountBits(uint v) { return countbits(v); }

// ---------- Camera ----------
// Reconstruct view-space position from DX11 UV and depth.
float3 VPosFromUVDepth(float2 uv, float d) {
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, d, 1.0f);
    float4 vp   = mul(clip, invProjDX);   // row-vector × matrix (N64 convention)
    return vp.xyz / vp.w;
}

// Project view-space position to DX11 UV.
float2 UVFromVPos(float3 vpos) {
    float4 clip = mul(float4(vpos, 1.0f), projDX);
    float2 ndc  = clip.xy / clip.w;
    return float2(ndc.x * 0.5f + 0.5f, 1.0f - (ndc.y * 0.5f + 0.5f));
}

// Reconstruct view-space normal from depth neighbours.
float3 ReconstructNormal(float2 uv, float3 vpos) {
    float2 dx = float2(1.0f / resolution.x, 0.0f);
    float2 dy = float2(0.0f, 1.0f / resolution.y);
    float3 px = VPosFromUVDepth(uv + dx, t_depth.SampleLevel(s_point, uv + dx, 0));
    float3 py = VPosFromUVDepth(uv + dy, t_depth.SampleLevel(s_point, uv + dy, 0));
    float3 ddx2 = px - vpos;
    float3 ddy2 = py - vpos;
    float3 N = normalize(cross(ddy2, ddx2));
    // In DX11 view-space, camera looks in +Z (opposite of GL).
    // Ensure normal faces camera (N.z < 0 for front-facing in DX11 view space).
    if (N.z > 0.0f) N = -N;
    return N;
}

// Quaternion from (0,0,1) to `to`  (note: DX11 +Z forward, opposite of GL)
float4 GetQuaternion(float3 to) {
    float3 xyz = float3(-to.y, to.x, 0.0f);
    float  s   =  to.z;
    float  u   = rsqrt(max(0.0f, s * 0.5f + 0.5f));
    s    = 1.0f / u;
    xyz *= u * 0.5f;
    return float4(xyz, s);
}

float3 Transform_Vz0Qz0(float2 v, float4 q) {
    float o = q.x * v.y;
    float c = q.y * v.x;
    float3 b = float3(o - c, -o + c, o - c);
    return float3(v, 0.0f) + 2.0f * (b * q.yxw);
}

// ---------- GT-VBGI ----------
float4 hemiGTVBGI(float2 uvPixels, float3 posVS, float3 N, uint dirs) {
    // In DX11 view-space, camera is at origin and +Z is forward.
    float3 V = -normalize(posVS);   // toward camera

    float2 rayStart = UVFromVPos(posVS) * resolution;

    uint frm = temporalEnabled ? frame : 0u;

    float  ao = 0.0f;
    float3 gi = float3(0, 0, 0);

    for (uint i = 0u; i < dirs; ++i) {
        uint  n    = frm * dirs + i;
        float4 rnd = Rnd01x4(uvPixels, n);

        // Slice direction
        float2 dir = float2(cos(rnd.x * Pi), sin(rnd.x * Pi));
        float3 smplVS = float3(dir, 0.0f);
        {
            float4 Q  = GetQuaternion(V);
            smplVS    = Transform_Vz0Qz0(dir, Q);
            float nearZ = abs(VPosFromUVDepth(resolution * 0.5f / resolution, 0.0f).z) * 0.001f;
            float2 p0 = UVFromVPos(posVS) * resolution;
            float2 p1 = UVFromVPos(posVS + smplVS * nearZ) * resolution;
            float2 sd = p1 - p0;
            float  sl = length(sd);
            if (sl > 0.0001f) dir = sd / sl;
        }

        // Slice geometry
        float3 sliceN, projN, T;
        float  cosN, sinN, projNRcpLen;
        {
            sliceN = cross(V, smplVS);
            projN  = N - sliceN * dot(N, sliceN);
            float pnl2 = dot(projN, projN);
            if (pnl2 == 0.0f) return float4(0, 0, 0, 1);
            T          = cross(sliceN, projN);
            projNRcpLen = rsqrt(pnl2);
            cosN = dot(projN, V) * projNRcpLen;
            sinN = dot(T,     V) * projNRcpLen;
        }

        float3 gi0     = float3(0, 0, 0);
        uint   occBits = 0u;

        for (float d = -1.0f; d <= 1.0f; d += 2.0f) {
            float2 rayDir = dir * d;

            const float count = SAMPLE_COUNT_H;
            const float s     = pow(rayLength, 1.0f / count);
            float t = pow(s, rnd.z);
            rnd.z   = 1.0f - rnd.z;

            for (float si = 0.0f; si < count; si += 1.0f) {
                float2 sPos = rayStart + rayDir * t;
                t *= s;

                if (sPos.x < 0 || sPos.x >= resolution.x ||
                    sPos.y < 0 || sPos.y >= resolution.y) break;

                float2 sUV     = sPos / resolution;
                float  sDep    = t_depth.SampleLevel(s_point, sUV, 0);
                if (sDep >= 1.0f) continue;

                float3 sRad    = t_color.SampleLevel(s_linear, sUV, 0).rgb;
                float3 sPosVS  = VPosFromUVDepth(sUV, sDep);

                float3 dFront  = sPosVS - posVS;
                float3 dBack   = dFront - V * thickness;

                float2 horCos  = float2(dot(normalize(dFront), V),
                                        dot(normalize(dBack),  V));
                horCos = (d >= 0.0f) ? horCos.xy : horCos.yx;

                float2 hor01;
                {
                    float d05 = d * 0.5f;
                    hor01 = ((0.5f + 0.5f * sinN) + d05) - d05 * horCos;
                }
                hor01 = clamp(hor01 + rnd.w * (1.0f / 32.0f), 0.0f, 1.0f);

                uint occBits0;
                {
                    uint2 horInt = (uint2)(hor01 * 32.0f);
                    uint  OxFF   = 0xFFFFFFFFu;
                    uint  mX     = (horInt.x < 32u) ? (OxFF << horInt.x) : 0u;
                    uint  mY     = (horInt.y != 0u)  ? (OxFF >> (32u - horInt.y)) : 0u;
                    occBits0 = mX & mY;
                }

                uint visBits0 = occBits0 & (~occBits);
                if (visBits0 != 0u) {
                    float vis0 = float(CountBits(visBits0)) * (1.0f / 32.0f);
                    gi0 += sRad * vis0;
                }
                occBits |= occBits0;
            }
        }

        float occ0 = float(CountBits(occBits)) * (1.0f / 32.0f);
        ao += 1.0f - occ0;
        gi += gi0;
    }

    float norm = 1.0f / float(dirs);
    return float4(gi, ao) * norm;
}

// ---------- Main ----------
float4 PSMain(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float depth = t_depth.SampleLevel(s_point, uv, 0);
    if (depth >= 1.0f) return float4(0, 0, 0, 0);

    float3 posVS = VPosFromUVDepth(uv, depth);
    float3 N     = ReconstructNormal(uv, posVS);

    posVS += N * (thickness * 0.01f);

    float4 giAO = hemiGTVBGI(uv * resolution, posVS, N, sampleDirs);
    float3 gi   = giAO.rgb;
    float  ao   = giAO.a;

    // ---- Motion-vector-advected temporal accumulation ----
    if (temporalEnabled) {
        float2 vel = t_velocity.SampleLevel(s_point, uv, 0);
        bool   haveVel = (vel.x > 0.0f || vel.y > 0.0f);

        float2 prev_uv;
        float  expectedPrevD;

        if (haveVel) {
            prev_uv      = vel;
            float4 cc    = float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1);
            float4 pc    = mul(cc, reprojDX);
            expectedPrevD = (pc.w > 0.0f) ? pc.z / pc.w : 2.0f;
        } else {
            float4 cc    = float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1);
            float4 pc    = mul(cc, reprojDX);
            if (pc.w <= 0.0f) return float4(gi, ao);
            float2 pndc  = pc.xy / pc.w;
            prev_uv      = float2(pndc.x * 0.5f + 0.5f, 1.0f - (pndc.y * 0.5f + 0.5f));
            expectedPrevD = pc.z / pc.w;
        }

        bool valid = (prev_uv.x >= 0.0f && prev_uv.x < 1.0f &&
                      prev_uv.y >= 0.0f && prev_uv.y < 1.0f);

        if (valid) {
            float actualPrevD = t_prevDepth.SampleLevel(s_point, prev_uv, 0);
            float threshold   = 0.02f * (1.0f - expectedPrevD);
            if (actualPrevD < expectedPrevD - threshold) valid = false;
        }

        if (valid && frameAccu > 1.0f) {
            float4 prev  = t_prevSsgi.SampleLevel(s_linear, prev_uv, 0);
            float  alpha = 1.0f / min(frameAccu, 64.0f);
            gi = lerp(prev.rgb, gi, alpha);
            ao = lerp(prev.a,   ao, alpha);
        }
    }

    return float4(gi, ao);
}
