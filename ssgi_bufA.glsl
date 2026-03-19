// GT-VBGI - Buffer A (buffer)
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
    Input logic
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
    //if(uv0.x >= 256.0 || uv0.y >= 8.0) discard;
    
    Resolution = iResolution.xy;

    uvec2 uv = uvec2(uv0.xy - 0.5);

    outCol = vec4(0.0);
    
    // blit key board texture
    outCol.w = texelFetch(KeyBoard, ivec2(uv0 - 0.5), 0).r;
    
    // program state:
    {
        vec4 iMouseLast     = ReadVar4(0, VAR_ROW);
        vec4 iMouseAccuLast = ReadVar4(1, VAR_ROW);
        vec4 wasdAccuLast   = ReadVar4(2, VAR_ROW);
        float frameAccuLast = ReadVar (3, VAR_ROW);
        float num           = ReadVar (4, VAR_ROW);
        

        bool shift = ReadKey(KEY_SHIFT) != 0.0;

        float kW = ReadKey(KEY_W);
        float kA = ReadKey(KEY_A);
        float kS = ReadKey(KEY_S);
        float kD = ReadKey(KEY_D);

        float left  = ReadKey(KEY_LEFT);
        float right = ReadKey(KEY_RIGHT);
        float up    = ReadKey(KEY_UP);
        float down  = ReadKey(KEY_DOWN);
        
        if(ReadKey(KEY_N1) != 0.0) num = 0.0;
        if(ReadKey(KEY_N2) != 0.0) num = 1.0;
        if(ReadKey(KEY_N3) != 0.0) num = 2.0;
        if(ReadKey(KEY_N4) != 0.0) num = 3.0;
        if(ReadKey(KEY_N5) != 0.0) num = 4.0;
        if(ReadKey(KEY_N6) != 0.0) num = 5.0;
        if(ReadKey(KEY_N7) != 0.0) num = 6.0;
        
        bool anyK = false;
        
        anyK = anyK || iMouse.z > 0.0;
        anyK = anyK || shift;
        anyK = anyK || kW != 0.0;
        anyK = anyK || kA != 0.0;
        anyK = anyK || kS != 0.0;
        anyK = anyK || kD != 0.0;
        anyK = anyK || left  != 0.0;
        anyK = anyK || right != 0.0;
        anyK = anyK || up    != 0.0;
        anyK = anyK || down  != 0.0;
        anyK = anyK || ReadKey(KEY_TAB) != 0.0;
        anyK = anyK || ReadKey(KEY_SHIFT) != 0.0;
        anyK = anyK || ReadKey(KEY_SPACE) != 0.0;
        anyK = anyK || ReadKey(KEY_CTRL) != 0.0;
        
        float frameAccu = frameAccuLast;
        //if(anyK) frameAccuLast = 0.0;
        

        vec4 wasdAccu = wasdAccuLast;
        wasdAccu += vec4(kW, kA, kS, kD);
        wasdAccu += vec4(up, left, down, right);        

        
        vec2 mouseDelta = iMouse.xy - iMouseLast.xy;



        bool cond0 = iMouse.z > 0.0 &&iMouseLast.z > 0.0;
        vec2 mouseDelta2 = cond0 &&!shift ? mouseDelta.xy : vec2(0.0);
        vec2 mouseDelta3 = cond0 &&shift ? mouseDelta.xy : vec2(0.0);

        vec4 iMouseAccu = iMouseAccuLast + vec4(mouseDelta2, mouseDelta3);

        if(cond0 &&dot(abs(mouseDelta), vec2(1.0)) != 0.0) frameAccu = 0.0;
        if(ReadKey(KEY_TAB ) != 0.0) frameAccu = 0.0;
        if(ReadKey(KEY_CTRL) != 0.0) frameAccu = 0.0;
        if(ReadKey(KEY_R   ) != 0.0) frameAccu = 0.0;
        if(ReadKey(KEY_W   ) != 0.0) frameAccu = 0.0;
        if(ReadKey(KEY_SPACE)!= 0.0) frameAccu = 0.0;
        if(ReadKey(KEY_N   ) != 0.0) frameAccu = 0.0;
        if(ReadKey(KEY_M   ) != 0.0) frameAccu = 0.0;
        
        frameAccu += 1.0;
        
        vec4 toggles = vec4(
        ReadKeyToggle(KEY_TAB  ) != 0.0 ? 1.0 : 0.0,
        ReadKeyToggle(KEY_SHIFT) != 0.0 ? 1.0 : 0.0,
        ReadKeyToggle(KEY_SPACE) != 0.0 ? 1.0 : 0.0,
        ReadKeyToggle(KEY_CTRL ) != 0.0 ? 1.0 : 0.0);
        
        if(ReadKey(KEY_N) != 0.0)
        {
            iMouseAccu = vec4(0.0);
            iMouseAccuLast = iMouseAccu;
        }
        
        if(ReadKey(KEY_M) != 0.0)
        {
            iMouseAccu = vec4(Pi/0.008, 0.0, 0.0, -6.0);
            iMouseAccuLast = iMouseAccu;
        }
        
        WriteVar4(iMouse,        0, VAR_ROW);
        WriteVar4(iMouseAccu,    1, VAR_ROW);
        WriteVar4(wasdAccu,      2, VAR_ROW);
        WriteVar (frameAccu,     3, VAR_ROW);
        WriteVar (num,           4, VAR_ROW);
        WriteVar4(iMouseAccuLast,5, VAR_ROW);
        WriteVar4(toggles       ,6, VAR_ROW);
        
        vec4 mouseAccu = iMouseAccu;
        
        PrepareCam(mouseAccu, USE_PERSPECTIVE_CAM_COND);
    }
    
    // compute scene normal and write to outCol.rgb
    {

        vec3 rp, rd;
        GetRay(uv0, /*out*/ rp, rd);

        float t; vec3 N; vec3 a;
        Intersect_Scene(rp, rd, /*out:*/ t, N, a);    

        // N : world space normal.xyz
        outCol.xyz = N;
    }
}













