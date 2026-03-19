// GT-VBGI - Image (image)
// Inputs:
//   channel 3: buffer /media/previz/buffer00.png
//   channel 1: buffer /media/previz/buffer01.png
//   channel 0: buffer /media/previz/buffer02.png
//   channel 2: buffer /media/previz/buffer03.png
// Outputs: channel 0

// https://twitter.com/Mirko_Salm/status/1865464707282243843
/*
    Optimized version of uniformly weighted GT-VBGI (https://www.shadertoy.com/view/lfdBWn).
    No more acos calls.
    
    This utilizes the same optimizations applied here: https://www.shadertoy.com/view/4cdfzf
    
    
    Buffer A: scene normals    (xyz) | input logic (w)
    Buffer B: scene irradiance (xyz) | scene depth (w)
    Buffer C: GT-VBGI
    Buffer D: reference ray marcher/caster
    Image   : documentation + presentation

    Controls:
    1: split screen comparison of GT-VBGI (left) and reference ray marcher (right)
    2: GT-VBGI
    3: reference ray marcher
    4: grey-scale difference between GT-VBGI and reference (white: too bright | black: too dark)
    5: colored    difference between GT-VBGI and reference (red  : too bright | blue : too dark)
    6: blurred version of 4
    7: blurred version of 5
    -> options 4-7 only make sense to use with converged results

    toggles:
    
    Tab  : toggle temporal accumulation on/off
    Ctrl : toggle ortho cam off/on

    U    : toggle UI off/on
    N    : reset camera position
    M    : reset camera to alternative position
    
    Camera controls via mouse + shift key.
    
    
    Related/Sources:
    
        -  bidirectional GT-VBGI: https://www.shadertoy.com/view/lfdBWn
        - unidirectional GT-VBGI: https://www.shadertoy.com/view/XcdBWf
        
        - optimized uniformly weighted, bidirectional GT-VBGI: https://www.shadertoy.com/view/XfcBDl
    
        -  bidirectional GT-VBAO: https://www.shadertoy.com/view/XXGSDd
        - unidirectional GT-VBAO: https://www.shadertoy.com/view/Xc3yzs
        -  bidirectional GTAO+  : https://www.shadertoy.com/view/lctBzH
        - unidirectional GTAO+  : https://www.shadertoy.com/view/4ctBRH

        - optimized uniformly weighted, bidirectional GT-VBAO: https://www.shadertoy.com/view/4cdfzf

        - Screen Space Indirect Lighting with Visibility Bitmask: SSILVB, the original 'VBGI' (and VBAO)
          https://arxiv.org/abs/2301.11376 (slides: https://cdrinmatane.github.io/posts/cgspotlight-slides/)
          
        - An overview of the original VBAO including source code (Olivier Therrien's blog): my initial implementation was based on this
          https://cdrinmatane.github.io/posts/ssaovb-code/
          
        - Practical Real-Time Strategies for Accurate Indirect Occlusion: introduces GTAO, which VBAO is a variant of
          https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
          
        - Horizon Based Indirect Lighting (HBIL)
          https://github.com/Patapom/GodComplex/tree/master/Tests/TestHBIL
*/


/*
    This work is licensed under a dual license, public domain and MIT, unless noted otherwise. Choose the one that best suits your needs:
     
    CC0 1.0 Universal https://creativecommons.org/publicdomain/zero/1.0/
    To the extent possible under law, the author has waived all copyrights and related or neighboring rights to this work.
    
    or
    
    The MIT License
    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), 
    to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
    and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: 
    The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. 
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
    DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR 
    THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#define KeyBoard iChannel3
#define KeyBoardChannel w

float ReadKey(int keyCode) {return texelFetch(KeyBoard, ivec2(keyCode, 0), 0).KeyBoardChannel;}
float ReadKeyToggle(int keyCode) {return texelFetch(KeyBoard, ivec2(keyCode, 2), 0).KeyBoardChannel;}

#define VarTex iChannel3
#define OutCol outCol
#define OutChannel w

float ReadVar(int cx, int cy) {return texelFetch(VarTex, ivec2(cx, cy), 0).OutChannel;}
vec4 ReadVar4(int cx, int cy) {return vec4(ReadVar(cx, cy), ReadVar(cx, cy + 1), ReadVar(cx, cy + 2), ReadVar(cx, cy + 3));}

/////////////////////////////////////////////////////////////////////////////////// UI
//===============================================================================//

float glyph2(bool style, vec2 tc)
{
    float r0 = 10.0;
    float r1 = 1.;
    float b = max(abs(tc.x), abs(tc.y)) - r0;
    float c = length(tc) - r0*0.9;
    
    b = max(b, -c);
    
    if(!style) if(r1 != 0.0) b = abs(b) - r1;
    
    b = max(b, -(abs(tc.x) - r1));
    b = max(b, -(abs(tc.y) - r1));
    
    return 1.0 - clamp(b + 0.5, 0.0, 1.0);
}

float glyph(bool type, vec2 tc, float r0, float r1, bool separator)
{
    float b = (type ? length(tc) : max(abs(tc.x), abs(tc.y))) - r0;
    
    if(r1 != 0.0) b = abs(b) - r1;
    
    if(separator) b = max(b, -(abs(tc.x) - r1));
    
    return 1.0 - clamp(b + 0.5, 0.0, 1.0);
}

vec2 EvalUI(vec2 uv0, float id, bvec2 modes)
{
    vec2 s = vec2(30.0, 30.0);

    vec2 uv = uv0;

    vec2 uvI = floor(uv / s);
    vec2 uvF = uv - uvI * s;

    if(uvI.y > 0.0 || uvI.x 
<0.0 || uvI.x>
    3.0) { return vec2(0.0); }

    vec2 tc = uvF-s*0.5;

    float k = 0.35;
    if(uvI.x == id) k = 0.9;

    bool isSolid = false;

    if(uvI.x == 0.0) 
    isSolid = tc.x   
    <0.0 ? modes.x : modes.y;
 else
 isSolid=uvI.x== 1.0 ? modes.x : modes.y;
 
 bool type=uvI.x== 1.0;
 if(uvI.x== 0.0) type=tc.x < 0.0;

    float v = glyph(type, tc, 8.0, isSolid ? 0.0 : 2.0, uvI.x == 0.0);
    if(uvI.x == 3.0) v = glyph2(modes.x || modes.y, tc);

    //if(uvI.y > 0.0 || uvI.x <0.0 || uvI.x>
        3.0) { v = 0.0; k = 0.0; }

    return vec2(v, k);
}

//===============================================================================//
///////////////////////////////////////////////////////////////////////////////////

void mainImage(out vec4 outCol, in vec2 uv0)
{
    Resolution = iResolution.xy;

    float num = ReadVar(4, VAR_ROW);

    ivec2 uv = ivec2(uv0.xy - 0.5);
	vec2 tex = uv0.xy / iResolution.xy;
    
    vec3 col = vec3(0.0);
        
    vec3 refgi = textureLod(iChannel2, tex, 0.0).rgb;
    vec3    gi = textureLod(iChannel0, tex, 0.0).rgb;
    
    //num=1.0;
    if(num == 0.0)// split screen: gtvbgi | ray-marched reference
    {
        col = vec3(uv0.x > iResolution.x * 0.5 ? refgi : gi);
    }
    else if(num == 1.0)// gtvbgi
    {
        col = gi;
    }
    else if(num == 2.0)// ray-marched reference
    {
        col = refgi;
    }
    else if(num == 3.0 || num == 4.0)// visualize error
    {
        float diff = dot((gi - refgi), vec3(0.21, 0.71, 0.08)) * 8.0;
        
        if(num == 3.0)
        {
            col = vec3(0.5 + diff*2.0);
        }
        else// num == 4.0: gtvbgi brighter than ref -> red | gtvbgi darker than ref -> blue
        {
            col = abs(diff) * 3.0 * (diff > 0.0 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.18, 1.0));
        }
    }
    else if(num == 5.0 || num == 6.0)// visualize error blurred
    {
        float count = 10.0;
        float wa = 0.0;
        
        for(float j = -count; j 
        <=count; ++j)
 for(float i=-count; i <= count; ++i)
        {
            vec2 o = vec2(i, j);
            vec2 w2 = pow(1.0 - pow(abs(o)/(count+0.5), vec2(2.0)), vec2(1.0));
            
            float w = 1.0;
            w = w2.x * w2.y;
            
            vec3 refgi = textureLod(iChannel2, (uv0 + o) / iResolution.xy, 0.0).rgb;
            vec3    gi = textureLod(iChannel0, (uv0 + o) / iResolution.xy, 0.0).rgb;
            
            float diff = dot((gi - refgi), vec3(0.21, 0.71, 0.08)) * 8.0;

            vec3 col0;
            
            if(num == 5.0)
            {
                col0 = vec3(abs(diff));
                col0 = vec3(0.5 + diff*2.0);
            }
            else
            {
                col0 = abs(diff) * 3.0 * (diff > 0.0 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.18, 1.0));
            }
        
            col += col0 * w;
            wa += w;
        }
        
        col /= wa;
    }
    
    #if 0
    if(num <3.0 && ReadKeyToggle(KEY_S) !=0.0)
 {
 / / isolines
 col=1.0-(cos(col * 32.0) * 0.5 + 0.5);
 }
 #endif
 
 #if 1
 if(num <= 2.0)
    {
        col = Tonemap(col * 8.0);
    }
    #endif
    
    // highlight pixels green if they clip beneath 0 or above 1: 
    if(ReadKeyToggle(KEY_G) != 0.0)
    if(col.r <0.0 || col.r>
            1.0 || col.g 
            <0.0 || col.g>
                1.0 || col.b 
                <0.0 || col.b>
                    1.0) col.rgb = vec3(0.0, 1.0, 0.0);
    //col.rgb=textureLod(iChannel3, uv0/iResolution.xy, 0.0).rgb;
    outCol = vec4(pow(clamp01(col), vec3(1.0/2.2)), 0.0);
    
    
    if(SHOW_UI_COND)
    {
        bvec2 modes = bvec2(true);
        
        vec2 ui = EvalUI(uv0, min(num, 3.0), modes.xx);
        
        outCol.rgb = mix(outCol.rgb, vec3(ui.y) * mix(vec3(0., 0.6, 1.0), vec3(1.0), 0.9), ui.x);
    }    
}































