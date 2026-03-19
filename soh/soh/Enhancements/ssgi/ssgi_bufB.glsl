// GT-VBGI - Buffer B (buffer)
// Inputs:
//   channel 1: keyboard /presets/tex00.jpg
//   channel 3: buffer /media/previz/buffer00.png
// Outputs: channel 0

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

/*
    G-buffer rendering: vec4(world space normal.xyz, linear depth)
    
    Camera controls via mouse + shift key.
*/


#define KeyBoard iChannel1

float ReadKey(int keyCode) {return texelFetch(KeyBoard, ivec2(keyCode, 0), 0).x;}
float ReadKeyToggle(int keyCode) {return texelFetch(KeyBoard, ivec2(keyCode, 2), 0).x;}

#define VarTex iChannel3
#define OutCol outCol
#define OutChannel w

#define WriteVar(v, cx, cy) {if(uv.x == uint(cx) &&uv.y == uint(cy)) OutCol.OutChannel = v;}
#define WriteVar4(v, cx, cy) {WriteVar(v.x, cx, cy) WriteVar(v.y, cx, cy + 1) WriteVar(v.z, cx, cy + 2) WriteVar(v.w, cx, cy + 3)}

float ReadVar(int cx, int cy) {return texelFetch(VarTex, ivec2(cx, cy), 0).OutChannel;}
vec4 ReadVar4(int cx, int cy) {return vec4(ReadVar(cx, cy), ReadVar(cx, cy + 1), ReadVar(cx, cy + 2), ReadVar(cx, cy + 3));}


void mainImage(out vec4 outCol, in vec2 uv0)
{     
    Resolution = iResolution.xy;
    
    uvec2 uv = uvec2(uv0.xy - 0.5);
	vec2 tex = uv0.xy / iResolution.xy;
    vec2 tex21 = tex * 2.0 - vec2(1.0);
    
    uint frame = uint(iFrame);
    
    vec4 mouseAccu = ReadVar4(1, VAR_ROW);

    PrepareCam(mouseAccu, USE_PERSPECTIVE_CAM_COND);
    
    vec3 rp, rd;
    GetRay(uv0, /*out*/ rp, rd);
    
    float t; vec3 N; vec3 a;
    Intersect_Scene(rp, rd, /*out:*/ t, N, a);    
    
    vec3 wpos = rp + rd * t;
    
    vec3 vpos = VPos_from_WPos(wpos);

    vec3 rad = Eval_Lighting(wpos, N, a);
    
    // N     : world space normal.xyz
    // vpos.z: linear depth
    outCol = vec4(rad, vpos.z);
}