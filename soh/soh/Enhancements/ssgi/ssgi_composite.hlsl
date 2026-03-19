// GT-VBGI composite pass (DX11)
// Blends the SSGI result with the original game color.

void VSMain(uint id        : SV_VertexID,
            out float4 pos : SV_POSITION,
            out float2 uv  : TEXCOORD0) {
    float x = (id == 1) ? 3.0f : -1.0f;
    float y = (id == 2) ? -3.0f : 1.0f;
    pos  = float4(x, y, 0.0f, 1.0f);
    uv.x = x * 0.5f + 0.5f;
    uv.y = 1.0f - (y * 0.5f + 0.5f);
}

Texture2D<float4> t_color : register(t0);   // game color
Texture2D<float4> t_ssgi  : register(t1);   // SSGI: RGB=gi, A=ao
SamplerState      s_linear : register(s0);

cbuffer CB : register(b0) {
    float giScale;
    float aoStrength;
    float2 padding;
};

float4 PSMain(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float3 color = t_color.SampleLevel(s_linear, uv, 0).rgb;
    float4 ssgi  = t_ssgi.SampleLevel(s_linear, uv, 0);
    float3 gi    = ssgi.rgb;
    float  ao    = ssgi.a;

    float  aoFactor    = lerp(1.0f, ao, aoStrength);
    float3 albedoApprox = sqrt(max(color, float3(0, 0, 0)));
    float3 indirect    = albedoApprox * gi * giScale;

    return float4(color * aoFactor + indirect, 1.0f);
}
