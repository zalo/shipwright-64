// GT-VBGI (uniformly weighted) - Adapted for Shipwright
// Original: https://www.shadertoy.com/view/XfcBDl by TinyTexel
// License: CC0 / MIT dual-license (see original for full text)
//
// Adaptation: replaces the shadertoy scene with the game's color/depth buffers.
// Normals are reconstructed from depth buffer derivatives.
//
// Inputs (from Shipwright renderer):
//   u_colorTex   - game color (RGB)
//   u_depthTex   - game depth buffer [0,1]  (GL_DEPTH_COMPONENT)
//   u_prevSsgiTex - previous frame SSGI (RGBA, .a = frame accumulation count)
//   u_invProjMat - inverse projection matrix
//   u_projMat    - projection matrix

#ifdef GL_ES
precision highp float;
precision highp int;
#endif

in vec2 v_uv;
out vec4 fragColor;

// ==================== Uniforms ====================
uniform sampler2D u_colorTex;
uniform highp sampler2D u_depthTex;
uniform sampler2D u_prevSsgiTex;
uniform highp sampler2D u_prevDepthTex;  // previous frame NDC depth (R32F colour texture)
// Per-pixel previous-frame UV written by the geometry pass (velocity buffer).
// Values are in [0,1].  (0,0) = no geometry / use camera-reproj fallback.
uniform sampler2D u_velocityTex;
uniform highp mat4 u_invProjMat;
uniform highp mat4 u_projMat;
// Transforms current clip-space position to previous-frame clip-space.
// Precomputed as inv(VP_curr) × VP_prev, passed row-major (GL_TRUE).
uniform highp mat4 u_reprojMat;
uniform vec2  u_resolution;
uniform uint  u_frame;
uniform float u_thickness;       // surface thickness in view-space units (tune per game)
uniform float u_rayLength;       // max screen-space ray length in pixels
uniform uint  u_sampleDirs;      // GT-VBGI directions per pixel (1-4)
uniform bool  u_temporalEnabled;
uniform float u_frameAccu;       // frames accumulated (capped; resets on scene change)

// ==================== Constants ====================
const float Pi      = 3.14159265359;
const float Pi2     = Pi * 2.0;

// Raymarching config — matches original Shadertoy defaults exactly
const float SAMPLE_COUNT = 32.0;  // Raymarching_SampleCount from original

// ==================== Hash utilities (exact match to original common.glsl) ====================
// Reference: shadertoy.com/view/XfcBDl  common.glsl

float Float01(uint x) { return float(x) * (1.0 / 4294967296.0); }

// LCG multiplier from original
const uint lcgM_orig = 2891336453u;
// SEED constants from original
const uvec4 SEED_ORIG = uvec4(0x5C995C6Du, 0x6A3C6A57u, 0xC65536CBu, 0x3563995Fu);

uvec3 pcg3Mix(uvec3 h) {
    h.x += h.y * h.z;
    h.y += h.z * h.x;
    h.z += h.x * h.y;
    return h;
}

uvec3 pcg3Permute(uvec3 h) {
    h = pcg3Mix(h);
    h ^= h >> 16u;
    return pcg3Mix(h);
}

// Exact match to original: pcg3(uvec3 h, uint seed)
uvec3 pcg3_orig(uvec3 h, uint seed) {
    uvec3 c = (seed << 1u) ^ SEED_ORIG.xyz;
    return pcg3Permute(h * lcgM_orig + c);
}

// Exact match to original: Hash01x3(uvec3 v, uint seed)
// pcg3_orig produces all three components in a single call.
vec3 Hash01x3_orig(uvec3 v, uint seed) {
    uvec3 r = pcg3_orig(v, seed);
    return vec3(Float01(r.x), Float01(r.y), Float01(r.z));
}

// Exact match to original Rnd01x4 (white-noise path):
//   rnd01.xzw = Hash01x3(uvec3(uvu, n), 0x9CF617FAu);
//   rnd01.y is unused (left 0; reserved for importance-sampled direction)
vec4 Rnd01x4(vec2 uvPixels, uint n) {
    uvec2 uvu = uvec2(uvPixels);
    uvec3 r   = pcg3_orig(uvec3(uvu, n), 0x9CF617FAu);
    return vec4(Float01(r.x), 0.0, Float01(r.y), Float01(r.z));
    // x → slice-direction angle, z → ray jitter, w → radial jitter
}

// ==================== Bit utilities ====================
// https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
uint CountBits(uint v) {
    v = v - ((v >> 1u) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2u) & 0x33333333u);
    return ((v + (v >> 4u) & 0xF0F0F0Fu) * 0x1010101u) >> 24u;
}

// ==================== Quaternion utilities ====================
// Rotation from vec3(0,0,-1) to `to`
vec4 GetQuaternion(vec3 to) {
    vec3 xyz = vec3(-to.y, to.x, 0.0);
    float s  =  to.z;
    float u  = inversesqrt(max(0.0, s * 0.5 + 0.5));
    s    = 1.0 / u;
    xyz *= u * 0.5;
    return vec4(xyz, s);
}

// Transform v by quaternion q.xyzs
vec3 Transform(vec3 v, vec4 q) {
    vec3 k = cross(q.xyz, v);
    return v + 2.0 * vec3(dot(vec3(q.wy, -q.z), k.xzy),
                          dot(vec3(q.wz, -q.x), k.yxz),
                          dot(vec3(q.wx, -q.y), k.zyx));
}

// Transform v.xy0 by quaternion q.xy0s
vec3 Transform_Vz0Qz0(vec2 v, vec4 q) {
    float o = q.x * v.y;
    float c = q.y * v.x;
    vec3 b  = vec3(o - c, -o + c, o - c);
    return vec3(v, 0.0) + 2.0 * (b * q.yxw);
}

// ==================== Camera utilities ====================
// Reconstruct view-space position from pixel coords and NDC depth [0,1]
highp vec3 VPos_from_UV_Depth(vec2 uvPixels, float depth) {
    vec2 uv = uvPixels / u_resolution;
    highp vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    highp vec4 vp   = u_invProjMat * clip;
    return vp.xyz / vp.w;
}

// Project view-space position to pixel coordinates
vec2 PixelPos_from_VPos(highp vec3 vpos) {
    highp vec4 clip = u_projMat * vec4(vpos, 1.0);
    vec2 ndc = clip.xy / clip.w;
    return (ndc * 0.5 + 0.5) * u_resolution;
}

// Sample depth texture at pixel coords
float SampleDepth(vec2 pixelPos) {
    return textureLod(u_depthTex, pixelPos / u_resolution, 0.0).r;
}

// Reconstruct view-space normal at pixel centre from depth neighbours
vec3 ReconstructNormal(vec2 pixelPos, vec3 vpos) {
    vec3 px = VPos_from_UV_Depth(pixelPos + vec2(1.0, 0.0), SampleDepth(pixelPos + vec2(1.0, 0.0)));
    vec3 py = VPos_from_UV_Depth(pixelPos + vec2(0.0, 1.0), SampleDepth(pixelPos + vec2(0.0, 1.0)));
    vec3 ddx = px - vpos;
    vec3 ddy = py - vpos;
    vec3 N = normalize(cross(ddy, ddx));
    // Ensure normal faces camera (view-space: camera at origin, surface in -Z)
    if (N.z < 0.0) N = -N;
    return N;
}

// ==================== GT-VBGI CDF utilities ====================
float SliceRelCDF_Uniform(float horCos, float sinN, bool isPhiLargerThanAngN) {
    bool  c  = isPhiLargerThanAngN;
    float m0 = c ? 1.0 : 0.0;
    float m1 = c ? -0.5 : 0.5;
    return m0 + m1 * horCos + (0.5 * sinN);
}

// ==================== GT-VBGI core ====================
// Returns vec4(gi.rgb, ao)
// positionVS: view-space position of the current pixel
// N:          view-space normal (reconstructed from depth)
vec4 hemiGTVBGI(vec2 uv0Pixels, vec3 positionVS, vec3 N, uint dirCount) {
    // View vector: from surface to camera (in view space, camera is at origin)
    vec3 V = -normalize(positionVS);

    // Screen-space start position
    vec2 rayStart = PixelPos_from_VPos(positionVS);

    uint frame = u_temporalEnabled ? u_frame : 0u;

    float ao = 0.0;
    vec3  gi = vec3(0.0);

    for (uint i = 0u; i < dirCount; ++i) {
        uint n = frame * dirCount + i;
        vec4 rnd01 = Rnd01x4(uv0Pixels, n);

        // ---- Slice direction sampling ----
        vec2 dir;          // screen-space ray direction
        vec3 smplDirVS;    // corresponding view-space direction

        dir      = vec2(cos(rnd01.x * Pi), sin(rnd01.x * Pi));
        smplDirVS = vec3(dir, 0.0);

        // Perspective correction: map 2D screen-space dir to view-space tangent
        {
            vec4 Q_toV = GetQuaternion(V);
            smplDirVS  = Transform_Vz0Qz0(dir, Q_toV);

            // Recompute screen-space dir from the projected view-space tangent
            // (small step in view space, then project to get accurate screen dir)
            float nearZ = abs(VPos_from_UV_Depth(vec2(u_resolution * 0.5), 0.0).z) * 0.001;
            vec2 p0 = PixelPos_from_VPos(positionVS);
            vec2 p1 = PixelPos_from_VPos(positionVS + smplDirVS * nearZ);
            vec2 screenDir = p1 - p0;
            float sdLen = length(screenDir);
            if (sdLen > 0.0001) dir = screenDir / sdLen;
        }

        // ---- Construct slice plane ----
        vec3 sliceN, projN, T;
        float cosN, sinN, projNRcpLen;
        {
            sliceN = cross(V, smplDirVS);
            projN  = N - sliceN * dot(N, sliceN);
            float projNSqrLen = dot(projN, projN);
            if (projNSqrLen == 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
            T         = cross(sliceN, projN);
            projNRcpLen = inversesqrt(projNSqrLen);
            cosN = dot(projN, V) * projNRcpLen;
            sinN = dot(T,     V) * projNRcpLen;
        }

        // ---- Horizon scanning in both half-directions ----
        vec3 gi0      = vec3(0.0);
        uint occBits  = 0u;

        for (float d = -1.0; d <= 1.0; d += 2.0) {
            vec2 rayDir = dir * d;

            const float count = SAMPLE_COUNT;
            const float s = pow(u_rayLength, 1.0 / count);

            float t = pow(s, rnd01.z);  // jitter start: [1, s]
            rnd01.z = 1.0 - rnd01.z;

            for (float si = 0.0; si < count; si += 1.0) {
                vec2 samplePos = rayStart + rayDir * t;
                t *= s;

                // Out-of-bounds check
                if (samplePos.x < 0.0 || samplePos.x >= u_resolution.x ||
                    samplePos.y < 0.0 || samplePos.y >= u_resolution.y) break;

                vec2 sampleUV = samplePos / u_resolution;
                float sampleDepthNDC = textureLod(u_depthTex, sampleUV, 0.0).r;

                // Skip sky/background (depth == 1.0)
                if (sampleDepthNDC >= 1.0) continue;

                vec3 sampleRad  = textureLod(u_colorTex, sampleUV, 0.0).rgb;
                vec3 samplePosVS = VPos_from_UV_Depth(samplePos, sampleDepthNDC);

                vec3 deltaPosFront = samplePosVS - positionVS;
                // Thin-slab back face: assume surface is u_thickness units deep
                vec3 deltaPosBack  = deltaPosFront - V * u_thickness;

                // Project onto unit hemisphere and find horizon cosines
                vec2 horCos = vec2(dot(normalize(deltaPosFront), V),
                                   dot(normalize(deltaPosBack),  V));

                // Sampling direction flips min/max
                horCos = d >= 0.0 ? horCos.xy : horCos.yx;

                // Map to slice-relative CDF [0,1]
                vec2 hor01;
                {
                    float d05 = d * 0.5;
                    hor01 = ((0.5 + 0.5 * sinN) + d05) - d05 * horCos;
                }

                // Jitter sample + clamp
                hor01 = clamp(hor01 + rnd01.w * (1.0 / 32.0), 0.0, 1.0);

                // Turn arc into bitmask
                uint occBits0;
                {
                    uvec2 horInt = uvec2(floor(hor01 * 32.0));
                    uint OxFFFF  = 0xFFFFFFFFu;
                    uint mX = horInt.x < 32u ? (OxFFFF << horInt.x) : 0u;
                    uint mY = horInt.y != 0u  ? (OxFFFF >> (32u - horInt.y)) : 0u;
                    occBits0 = mX & mY;
                }

                // GI contribution for newly-visible bits
                uint visBits0 = occBits0 & (~occBits);
                if (visBits0 != 0u) {
                    float vis0 = float(CountBits(visBits0)) * (1.0 / 32.0);
                    gi0 += sampleRad * vis0;
                }

                occBits |= occBits0;
            }
        }

        float occ0 = float(CountBits(occBits)) * (1.0 / 32.0);
        ao += 1.0 - occ0;
        gi += gi0;
    }

    float norm = 1.0 / float(dirCount);
    return vec4(gi, ao) * norm;
}

// ==================== Main ====================
void main() {
    vec2 pixelPos = v_uv * u_resolution;
    vec2 uv       = v_uv;

    float depth = textureLod(u_depthTex, uv, 0.0).r;

    // Skip background/sky pixels
    if (depth >= 1.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    vec3 positionVS = VPos_from_UV_Depth(pixelPos, depth);
    vec3 N          = ReconstructNormal(pixelPos, positionVS);

    // Nudge position slightly along normal to avoid self-shadowing
    positionVS += N * (u_thickness * 0.01);

    // Run GT-VBGI
    vec4 giAO = hemiGTVBGI(pixelPos, positionVS, N, u_sampleDirs);
    vec3 gi   = giAO.rgb;
    float ao  = giAO.a;

    // ---- Motion-vector advection + disocclusion-aware temporal accumulation ----
    if (u_temporalEnabled) {
        // --- Determine previous-frame UV ---
        // Priority 1: per-vertex velocity buffer (accounts for object motion).
        // Priority 2: camera-reprojection matrix (handles camera-only motion).
        vec2 vel = textureLod(u_velocityTex, uv, 0.0).rg;
        bool haveVelocity = (vel.x > 0.0 || vel.y > 0.0); // (0,0) = no geometry data

        vec2 prev_uv;
        float expectedPrevNDC;

        if (haveVelocity) {
            // Velocity texture stores the previous-frame UV directly.
            prev_uv        = vel;
            // Re-derive the expected NDC depth from camera reprojection for
            // the disocclusion check (depth stored in camera-reproj clip space).
            highp vec4 curr_clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
            highp vec4 prev_clip = u_reprojMat * curr_clip;
            expectedPrevNDC = prev_clip.w > 0.0 ? prev_clip.z / prev_clip.w * 0.5 + 0.5 : 2.0;
        } else {
            // Fall back to camera-only reprojection.
            highp vec4 curr_clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
            highp vec4 prev_clip = u_reprojMat * curr_clip;
            if (prev_clip.w <= 0.0) { fragColor = vec4(gi, ao); return; }
            prev_uv         = prev_clip.xy / prev_clip.w * 0.5 + 0.5;
            expectedPrevNDC = prev_clip.z  / prev_clip.w * 0.5 + 0.5;
        }

        // Viewport bounds check
        bool valid = all(greaterThanEqual(prev_uv, vec2(0.0))) &&
                     all(lessThan(prev_uv, vec2(1.0)));

        // Disocclusion check: if the actual previous depth at prev_uv is
        // closer than expected, the surface was occluded last frame.
        if (valid) {
            float actualPrevNDC = textureLod(u_prevDepthTex, prev_uv, 0.0).r;
            float threshold = 0.02 * (1.0 - expectedPrevNDC);
            if (actualPrevNDC < expectedPrevNDC - threshold) {
                valid = false; // disoccluded — freshly visible pixel
            }
        }

        if (valid && u_frameAccu > 1.0) {
            vec4 prev   = textureLod(u_prevSsgiTex, prev_uv, 0.0);
            float alpha = 1.0 / min(u_frameAccu, 64.0);
            gi = mix(prev.rgb, gi, alpha);
            ao = mix(prev.a,   ao, alpha);
        }
    }
    fragColor = vec4(gi, ao);
}
