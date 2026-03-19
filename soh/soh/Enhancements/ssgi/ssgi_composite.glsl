// GT-VBGI composite pass
// Blends the SSGI result (GI color + AO) with the game's rendered color.
#ifdef GL_ES
precision highp float;
#endif

in vec2 v_uv;
out vec4 fragColor;

uniform sampler2D u_colorTex;   // original game color
uniform sampler2D u_ssgiTex;    // SSGI result: RGB=indirect light, A=ambient occlusion
uniform float u_giScale;        // indirect light intensity scale
uniform float u_aoStrength;     // AO darkening strength [0,1]

void main() {
    vec3  color = texture(u_colorTex, v_uv).rgb;
    vec4  ssgi  = texture(u_ssgiTex,  v_uv);
    vec3  gi    = ssgi.rgb;
    float ao    = ssgi.a;

    // Ambient occlusion: lerp between full brightness and occluded
    // ao=1.0 means unoccluded, ao=0.0 means fully occluded
    float aoFactor = mix(1.0, ao, u_aoStrength);

    // Indirect GI: scale GI by game color as approximate albedo proxy
    // Using sqrt(color) desaturates slightly for more natural look
    vec3 albedoApprox = sqrt(max(color, vec3(0.0)));
    vec3 indirect = albedoApprox * gi * u_giScale;

    // Combine: darken by AO, add indirect light
    vec3 result = color * aoFactor + indirect;

    fragColor = vec4(result, 1.0);
}
