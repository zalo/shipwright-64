// GT-VBGI - Buffer D (buffer)
// Inputs:
//   channel 3: buffer /media/previz/buffer00.png
//   channel 2: buffer /media/previz/buffer01.png
//   channel 0: buffer /media/previz/buffer03.png
//   channel 1: texture /media/a/cb49c003b454385aa9975733aff4571c62182ccdda480aaba9a8d250014f00ec.png
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

////////////////////////////////////////////////////////////////////////////////////// config
//==================================================================================//

 //#define REFAO_USE_WHITE_NOISE

 // use this to render a ground-truth reference:
 //#define USE_ANALYTICAL_RAYCASTING

//==================================================================================//
////////////////////////////////////////////////////////////////////////////////////// 


bool isLeft;// true on the left half of the screen; for debugging purposes

#define KeyBoard iChannel3
#define KeyBoardChannel w

float ReadKey(int keyCode) {return texelFetch(KeyBoard, ivec2(keyCode, 0), 0).KeyBoardChannel;}
float ReadKeyToggle(int keyCode) {return texelFetch(KeyBoard, ivec2(keyCode, 2), 0).KeyBoardChannel;}

#define VarTex iChannel3
#define OutCol outCol
#define OutChannel w

#define WriteVar(v, cx, cy) {if(uv.x == uint(cx) && uv.y == uint(cy)) OutCol.OutChannel = v;}
#define WriteVar4(v, cx, cy) {WriteVar(v.x, cx, cy) WriteVar(v.y, cx, cy + 1) WriteVar(v.z, cx, cy + 2) WriteVar(v.w, cx, cy + 3)}

float ReadVar(int cx, int cy) {return texelFetch(VarTex, ivec2(cx, cy), 0).OutChannel;}
vec4 ReadVar4(int cx, int cy) {return vec4(ReadVar(cx, cy), ReadVar(cx, cy + 1), ReadVar(cx, cy + 2), ReadVar(cx, cy + 3));}


////////////////////////////////////////////////////////////////////////////////////// reference GI
//==================================================================================//

vec4 ReferenceGI(vec2 uv0, vec3 wpos, vec3 N, uint dirCount)
{
    uvec2 uvu = uvec2(uv0);

   #ifndef REFAO_USE_WHITE_NOISE
    // randomly shift noise pattern around
    if(USE_TEMP_ACCU_COND) uvu += Hash(uvec2(iFrame, 0u), 0xBD1E0BB0u).xy;

    // linearize uv in a locality preserving way
    uint pxId = EvalHilbertCurve(uvu, 9u);
   #endif

    vec3 positionVS = VPos_from_WPos(wpos);

    uint frame = USE_TEMP_ACCU_COND ? uint(iFrame) : 0u;

    float occ = 0.0;
    vec3 gi = vec3(0.0);
    
    for(uint i = 0u; i < dirCount; ++i)
    {
        uint n = frame * dirCount + i;
        
      #ifdef REFAO_USE_WHITE_NOISE
        vec2 s = Hash11x2(uvec3(uvu, n), 0x3579A945u);
      #else
        uint h = pxId * dirCount + i;
        vec2 s = Float11(shuffled_scrambled_sobol_2d(h, 0xCC925D21u));
      #endif
        
        vec3 rayDir0 = Sample_Sphere(s); 

        // uniform weighted hemisphere
        rayDir0 -= N * min(dot(rayDir0, N) * 2.0, 0.0);// flip if on wrong side
        
       #ifdef USE_ANALYTICAL_RAYCASTING
        {
            float t; vec3 N; vec3 a;
            if(Intersect_Scene(wpos, rayDir0, /*out:*/ t, N, a) > 0.0)
            {
                occ += 1.0;
                
                vec3 wpos2 = wpos + rayDir0 * t;
            
                vec3 rad = Eval_Lighting(wpos2, N, a);
                
                gi += rad;
            }
        }
       #else
        // ray march in screen space
        if(isPerspectiveCam)
        {
            vec4 rayStart = PPos_from_WPos(wpos);
            vec4 rayEnd   = PPos_from_WPos(wpos + rayDir0 * (nearZ * 0.5));
            
            float rwStart = 1.0 / rayStart.w;
            float rwEnd   = 1.0 / rayEnd.w;
            
            vec2 tcStart = rayStart.xy * rwStart * 0.5 + 0.5;
            vec2 tcEnd   = rayEnd.xy   * rwEnd   * 0.5 + 0.5;
            
            vec2  tcDelta0 = tcEnd - tcStart;
            float rwDelta0 = rwEnd - rwStart;
            
            vec2  uvDelta0       = tcDelta0 * iResolution.xy;
            float uvDelta0RcpLen = inversesqrt(dot(uvDelta0, uvDelta0));

            // 1 px step size
            vec2  tcDelta = tcDelta0 * uvDelta0RcpLen;
            float rwDelta = rwDelta0 * uvDelta0RcpLen;
            
            float rnd01 = Hash01(uvec3(uvu, n), 0x2D56DA3Bu);

            const float count = Raymarching_SampleCount;
            
            const float s = pow(Raymarching_Width, 1.0/count);
            
            float t = pow(s, rnd01);// init t: [1, s]

            for (float i = 0.0; i < count; ++i)
            {
                vec2  tc = tcStart + tcDelta * t;
                float rw = rwStart + rwDelta * t;

                t *= s;

                float depth = 1.0 / rw;

                // handle oob
                if(tc.x < 0.0 || tc.x >= 1.0 || tc.y < 0.0 || tc.y >= 1.0) break;
                
                vec4 buff = textureLod(iChannel2, tc.xy, 0.0);
                
                vec3  sampleRad   = buff.rgb;
                float sampleDepth = buff.w;

                if(depth > sampleDepth && depth < sampleDepth + Thickness)
                {
                    occ += 1.0;
                    
                   #ifdef USE_BACKFACE_REJECTION
                    vec3 N0 = textureLod(iChannel3, tc.xy, 0.0).xyz;
                
                    if(dot(rayDir0, N0) > 0.0) break;
                   #endif
                   
                    gi += sampleRad;
                    
                    break;
                }
             }
        }
        else
        {
            vec3 rayStart = SPos_from_WPos(wpos);
            vec3 rayDir   = SVec_from_VVec_Ortho(VVec_from_WVec(rayDir0));
            
            // 1 px step size
            rayDir /= length(rayDir.xy);

            float rnd01 = Hash01(uvec3(uvu, n), 0x2D56DA3Bu);

            const float count = Raymarching_SampleCount;
                        
            const float s = pow(Raymarching_Width, 1.0/count);
            
            float t = pow(s, rnd01);// init t: [1, s]
            
            for (float i = 0.0; i < count; ++i)
            {
                vec3 samplePos = rayStart + rayDir * t;
                
                t *= s;

                float depth = samplePos.z;
                
                vec2 tc = samplePos.xy / iResolution.xy;

                // handle oob
                if(tc.x < 0.0 || tc.x >= 1.0 || tc.y < 0.0 || tc.y >= 1.0) break;
               
                vec4 buff = textureLod(iChannel2, tc.xy, 0.0);
                
                vec3  sampleRad   = buff.rgb;
                float sampleDepth = buff.w;

                if(depth > sampleDepth && depth < sampleDepth + Thickness)
                {
                    occ += 1.0;
                    
                   #ifdef USE_BACKFACE_REJECTION
                    vec3 N0 = textureLod(iChannel3, tc.xy, 0.0).xyz;
                
                    if(dot(rayDir0, N0) > 0.0) break;
                   #endif
               
                    gi += sampleRad;
                    
                    break;
                }
            }
        }
       #endif
    }
    
    float ao = 1.0 - occ;
    
    float norm = 1.0 / float(dirCount);
    
    return vec4(gi, ao) * norm;
}

//==================================================================================//
//////////////////////////////////////////////////////////////////////////////////////


vec3 Read_Albedo(vec2 uv0)
{
    // in a practical setting we would do a buffer read here, but because of shadertoy's
//limitations it is easier and cleaner to just re-render the scene here once to get the albedo
 vec4 mouseAccu=ReadVar4(1,VAR_ROW);

 uvec2 uv=uvec2(uv0.xy-0.5);
 vec2 tex=uv0.xy/iResolution.xy;
 vec2 tex21=tex*2.0-vec2(1.0);

 PrepareCam(mouseAccu,USE_PERSPECTIVE_CAM_COND);

 vec3 rp,rd;
 GetRay(uv0,/*out*/rp,rd);

 float t;vec3 N,a;
 Intersect_Scene(rp,rd,/*out:*/t,N,a);
 
 return a;
}

void mainImage(out vec4 outCol,in vec2 uv0)
{
 Resolution=iResolution.xy;
 isLeft=uv0.x < iResolution.x * 0.5;
    
    vec4 mouseAccu  = ReadVar4(1, VAR_ROW);
    float frameAccu = ReadVar (3, VAR_ROW);

    vec3 a    = Read_Albedo(uv0);
    vec3 N    = textureLod(iChannel3, uv0 / iResolution.xy, 0.0).xyz;
    vec4 buff = textureLod(iChannel2, uv0 / iResolution.xy, 0.0);
    vec3 rad  = buff.rgb;
  float depth = buff.w;
    
    if(depth >= exp2(10.0))
    {
        outCol = vec4(0.0);
        
        return;
    }
    
    vec3 spos = vec3(uv0, depth);
    
    vec3 wpos = WPos_from_SPos(spos);
         // need a larger offset here when using uniform hemisphere weighting 
         // due to increased potential for self-shadowing
         wpos += N * (4.0/1024.0);    
    
    vec3 col = vec3(0.0);

    vec3 refgi;
    {
      #ifndef GTVBGI_IS_UNIDIRECTIONAL
        uint count = 2u;// set to 2 to make comparison fair since GTVBGI marches bidirectionally
      #else
        uint count = 1u;
      #endif
        
        refgi = ReferenceGI(uv0, wpos, N, count).rgb;
    }
    
    col = rad + a * refgi;
    
    // accumulate frames
    if(USE_TEMP_ACCU_COND)
    {
        vec2 tc = uv0.xy / iResolution.xy;
    
        vec4 colLast = textureLod(iChannel0, tc, 0.0);

        col = mix(colLast.rgb, col, 1.0 / (frameAccu));
        
        frameAccu += 1.0;
        
        outCol = vec4(col.rgb, frameAccu);
        
        return;
    }
   
    outCol = vec4(col, 0.0);
}


