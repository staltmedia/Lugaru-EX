/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "EX/ExLighting.hpp"

#include "EX/ExConfig.hpp"
#include "EX/ExLocal.hpp"
#include "Game.hpp"
#include "Objects/Object.hpp"
#include "Objects/Person.hpp"
#include "Objects/Weapons.hpp"
#include "Graphic/Stereo.hpp"
#include "Environment/Lights.hpp"
#include "Math/XYZ.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern float screenwidth, screenheight;
extern XYZ viewer;
extern int mainmenu;
extern Light light;
extern int environment;

namespace EX
{
// ------------------------------------------------------------------ looks
struct Look
{
    const char* name;
    float ao, aoRadius;              // ambient occlusion strength, radius (world units)
    float bloom, bloomThreshold;     // bloom amount and brightness threshold
    float fire;                      // fire-as-light strength
    float shafts;                    // sun shafts
    float exposure, contrast, saturation;
    float tint[3];
    float vignette;
    float fog;                       // extra distance fog density
    float tonemap;                   // 1 = filmic curve
};

static const Look kLooks[] = {
    { "Original", 0, 0, 0, 1, 0, 0, 1, 1, 1, { 1, 1, 1 }, 0, 0, 0 },
    { "Enhanced", 0.65f, 1.0f, 0.22f, 0.78f, 1.0f, 0.0f, 1.0f, 1.05f, 1.06f, { 1.0f, 1.0f, 1.0f }, 0.18f, 0.0f, 0 },
};
static const int kNumLooks = sizeof(kLooks) / sizeof(kLooks[0]);

// ------------------------------------------------------------------ GL entry points
// (loaded at run time; named ex_* so they don't clash with glext.h prototypes)
static PFNGLCREATESHADERPROC ex_glCreateShader;
static PFNGLSHADERSOURCEPROC ex_glShaderSource;
static PFNGLCOMPILESHADERPROC ex_glCompileShader;
static PFNGLGETSHADERIVPROC ex_glGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC ex_glGetShaderInfoLog;
static PFNGLDELETESHADERPROC ex_glDeleteShader;
static PFNGLCREATEPROGRAMPROC ex_glCreateProgram;
static PFNGLATTACHSHADERPROC ex_glAttachShader;
static PFNGLLINKPROGRAMPROC ex_glLinkProgram;
static PFNGLGETPROGRAMIVPROC ex_glGetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC ex_glGetProgramInfoLog;
static PFNGLUSEPROGRAMPROC ex_glUseProgram;
static PFNGLGETUNIFORMLOCATIONPROC ex_glGetUniformLocation;
static PFNGLUNIFORM1IPROC ex_glUniform1i;
static PFNGLUNIFORM1FPROC ex_glUniform1f;
static PFNGLUNIFORM2FPROC ex_glUniform2f;
static PFNGLUNIFORM3FPROC ex_glUniform3f;
static PFNGLUNIFORM4FPROC ex_glUniform4f;
static PFNGLUNIFORM4FVPROC ex_glUniform4fv;
static PFNGLUNIFORMMATRIX4FVPROC ex_glUniformMatrix4fv;
static PFNGLACTIVETEXTUREPROC ex_glActiveTexture;
static PFNGLGENFRAMEBUFFERSPROC ex_glGenFramebuffers;
static PFNGLDELETEFRAMEBUFFERSPROC ex_glDeleteFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC ex_glBindFramebuffer;
static PFNGLFRAMEBUFFERTEXTURE2DPROC ex_glFramebufferTexture2D;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC ex_glCheckFramebufferStatus;
static PFNGLBLITFRAMEBUFFERPROC ex_glBlitFramebuffer;
static PFNGLDRAWBUFFERSPROC ex_glDrawBuffers;

template <typename T>
static bool load(T& fn, const char* name)
{
    fn = (T)SDL_GL_GetProcAddress(name);
    if (!fn) {
        fprintf(stderr, "LugaruEX lighting: missing %s\n", name);
    }
    return fn != nullptr;
}

// ------------------------------------------------------------------ shaders
static const char* kVS =
    "#version 120\n"
    "varying vec2 uv;\n"
    "void main() { uv = gl_MultiTexCoord0.xy; gl_Position = gl_Vertex; }\n";

static const char* kCommon =
    "#version 120\n"
    "varying vec2 uv;\n"
    "uniform sampler2D uDepth;\n"
    "uniform vec4 uProj;   // P[10], P[14], 1/P[0], 1/P[5]\n"
    "uniform vec2 uTexel;  // 1/screen size\n"
    "float linZ(float d) { return -uProj.y / ((d * 2.0 - 1.0) + uProj.x); }\n"
    "vec3 viewPos(vec2 t, float d) { float z = linZ(d); vec2 n = t * 2.0 - 1.0; return vec3(n.x * -z * uProj.z, n.y * -z * uProj.w, z); }\n"
    "vec3 posAt(vec2 t) { return viewPos(t, texture2D(uDepth, t).r); }\n"
    "vec3 normalAt(vec2 t, vec3 p) {\n"
    "  vec3 pr = posAt(t + vec2(uTexel.x, 0.0)), pl = posAt(t - vec2(uTexel.x, 0.0));\n"
    "  vec3 pu = posAt(t + vec2(0.0, uTexel.y)), pd = posAt(t - vec2(0.0, uTexel.y));\n"
    "  vec3 dx = (abs(pr.z - p.z) < abs(p.z - pl.z)) ? pr - p : p - pl;\n"
    "  vec3 dy = (abs(pu.z - p.z) < abs(p.z - pd.z)) ? pu - p : p - pd;\n"
    "  return normalize(cross(dx, dy));\n"
    "}\n"
    "vec3 octDec(vec2 e) {\n"
    "  vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));\n"
    "  if (n.z < 0.0) { vec2 s = vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0); n.xy = (1.0 - abs(n.yx)) * s; }\n"
    "  return normalize(n);\n"
    "}\n";

// Ground-truth ambient occlusion (GTAO, Jimenez et al. 2016): per pixel, a few slices
// through the view direction; in each, the horizon on both sides is found by marching
// the depth buffer, and the cosine-weighted visible arc between the horizons is
// integrated exactly against the (projected) surface normal. Gives the visible share of
// the sky (the ambient light really reaching the point), the bent normal (the mean
// unoccluded direction, which the sky light comes from) and, for one bounce of indirect
// light, the mean colour of the surfaces that block the sky. Target 0: visibility +
// bent normal (view xy), target 1: occluder colour. 4x4 pixel pattern, averaged by the
// composite's 4x4 filter.
static const char* kAO =
    "uniform float uRadius;      // world units\n"
    "uniform float uPixPerUnit;  // pixels per world unit at depth 1\n"
    "uniform float uUseG;\n"
    "uniform sampler2D uColor, uGBuf;\n"
    "const float PI = 3.14159265;\n"
    "float facos(float x) { float r = (-0.156583 * abs(x) + PI * 0.5) * sqrt(1.0 - abs(x)); return x >= 0.0 ? r : PI - r; }\n"
    "void main() {\n"
    "  // half size: read the depth at one full-size pixel's centre (the top-left of this 2x2\n"
    "  // block). The quad's uv lands exactly between two depth pixels, and with nearest\n"
    "  // sampling rounding picked a different one for the centre and its neighbours on some\n"
    "  // rows (at 2560x1440 every 45 pixels): the ground tilted there and got dark lines.\n"
    "  vec2 tc = (floor(gl_FragCoord.xy) * 2.0 + 0.5) * uTexel;\n"
    "  float d = texture2D(uDepth, tc).r;\n"
    "  if (d >= 0.99999) { gl_FragData[0] = vec4(1.0, 0.5, 0.5, 1.0); gl_FragData[1] = vec4(0.0); return; }\n"
    "  vec3 p = viewPos(tc, d);\n"
    "  vec3 V = normalize(-p);\n"
    "  vec3 n = normalAt(tc, p);\n"
    "  if (uUseG > 0.5) {\n"
    "    vec4 g = texture2D(uGBuf, tc);\n"
    "    if (g.b >= 1.0 && abs(g.a + p.z) < 0.06 + 0.02 * -p.z) n = octDec(g.rg);\n"
    "  }\n"
    "  if (dot(n, V) < 0.0) n = -n;\n"
    "  float rPix = uRadius * uPixPerUnit / -p.z;\n"
    "  if (rPix < 2.0) { gl_FragData[0] = vec4(1.0, n.xy * 0.5 + 0.5, 1.0); gl_FragData[1] = vec4(0.0); return; }\n"
    "  rPix = min(rPix, 0.12 / uTexel.y);\n"
    "  vec2 f = mod(floor(gl_FragCoord.xy), 4.0);\n"
    "  float rot = (f.x * 4.0 + mod(f.y + f.x * 2.0, 4.0) + 0.5) / 16.0;\n"
    "  float jit = fract((f.y * 4.0 + f.x) * 0.618034 + 0.31);\n"
    "  const int SLICES = 3;\n"
    "  const int STEPS = 6;\n"
    "  float vis = 0.0, bw = 0.0;\n"
    "  vec3 bent = vec3(0.0), bounce = vec3(0.0);\n"
    "  float fall = 1.0 / (0.6 * uRadius);\n"
    "  for (int i = 0; i < SLICES; i++) {\n"
    "    float phi = (float(i) + rot) / float(SLICES) * PI;\n"
    "    vec2 dir2 = vec2(cos(phi), sin(phi));\n"
    "    vec3 dir = vec3(dir2, 0.0);\n"
    "    vec3 ortho = normalize(dir - dot(dir, V) * V);\n"
    "    vec3 axis = cross(ortho, V);\n"
    "    vec3 pn = n - axis * dot(n, axis);\n"
    "    float pnLen = length(pn);\n"
    "    float cosN = clamp(dot(pn, V) / max(pnLen, 0.0001), 0.0, 1.0);\n"
    "    float nAng = (dot(ortho, pn) >= 0.0 ? 1.0 : -1.0) * facos(cosN);\n"
    "    float low0 = cos(nAng + PI * 0.5), low1 = cos(nAng - PI * 0.5);\n"
    "    float hc0 = low0, hc1 = low1;  // side 0 = +dir, side 1 = -dir\n"
    "    for (int j = 0; j < STEPS; j++) {\n"
    "      float s = (float(j) + jit) / float(STEPS);\n"
    "      vec2 o = dir2 * max(rPix * s * s, 1.5 + float(j)) * uTexel;\n"
    "      vec2 t0 = tc + o, t1 = tc - o;\n"
    "      vec3 d0 = posAt(t0) - p, d1 = posAt(t1) - p;\n"
    "      float l0 = length(d0), l1 = length(d1);\n"
    "      float c0 = mix(low0, dot(d0, V) / max(l0, 0.0001), clamp((uRadius - l0) * fall, 0.0, 1.0));\n"
    "      float c1 = mix(low1, dot(d1, V) / max(l1, 0.0001), clamp((uRadius - l1) * fall, 0.0, 1.0));\n"
    "      if (c0 > hc0) { bounce += texture2D(uColor, t0).rgb * (c0 - hc0); bw += c0 - hc0; hc0 = c0; }\n"
    "      if (c1 > hc1) { bounce += texture2D(uColor, t1).rgb * (c1 - hc1); bw += c1 - hc1; hc1 = c1; }\n"
    "    }\n"
    "    float h0 = -facos(hc1), h1 = facos(hc0);\n"
    "    h0 = nAng + max(h0 - nAng, -PI * 0.5);\n"
    "    h1 = nAng + min(h1 - nAng, PI * 0.5);\n"
    "    float sinN = sin(nAng);\n"
    "    vis += pnLen * ((cosN + 2.0 * h0 * sinN - cos(2.0 * h0 - nAng)) + (cosN + 2.0 * h1 * sinN - cos(2.0 * h1 - nAng))) * 0.25;\n"
    "    float hb = (h0 + h1) * 0.5;\n"
    "    bent += (ortho * sin(hb) + V * cos(hb)) * pnLen;\n"
    "  }\n"
    "  vis = clamp(vis / float(SLICES), 0.0, 1.0);\n"
    "  bent = normalize(bent + n * 0.001);\n"
    "  gl_FragData[0] = vec4(vis, bent.xy * 0.5 + 0.5, 1.0);\n"
    "  gl_FragData[1] = vec4(bw > 0.0 ? bounce / bw : vec3(0.0), 1.0);\n"
    "}\n";

static const char* kBright =
    "uniform sampler2D uColor;\n"
    "uniform float uThresh;\n"
    "uniform vec2 uSrcTexel;\n"
    "void main() {\n"
    "  vec3 c = vec3(0.0);\n"
    "  c += texture2D(uColor, uv + uSrcTexel * vec2(-1.0, -1.0)).rgb;\n"
    "  c += texture2D(uColor, uv + uSrcTexel * vec2(1.0, -1.0)).rgb;\n"
    "  c += texture2D(uColor, uv + uSrcTexel * vec2(-1.0, 1.0)).rgb;\n"
    "  c += texture2D(uColor, uv + uSrcTexel * vec2(1.0, 1.0)).rgb;\n"
    "  c *= 0.25;\n"
    "  float l = max(c.r, max(c.g, c.b));\n"
    "  float k = smoothstep(uThresh, uThresh + 0.25, l);\n"
    "  if (texture2D(uDepth, uv).r >= 0.99999) k *= 0.35;  // keep the sky from glowing too much\n"
    "  gl_FragColor = vec4(c * k, 1.0);\n"
    "}\n";

static const char* kBlur =
    "uniform sampler2D uSrc;\n"
    "uniform vec2 uDir;\n"
    "void main() {\n"
    "  vec3 c = texture2D(uSrc, uv).rgb * 0.2270270;\n"
    "  c += texture2D(uSrc, uv + uDir * 1.3846153).rgb * 0.3162162;\n"
    "  c += texture2D(uSrc, uv - uDir * 1.3846153).rgb * 0.3162162;\n"
    "  c += texture2D(uSrc, uv + uDir * 3.2307692).rgb * 0.0702702;\n"
    "  c += texture2D(uSrc, uv - uDir * 3.2307692).rgb * 0.0702702;\n"
    "  gl_FragColor = vec4(c, 1.0);\n"
    "}\n";

static const char* kComposite =
    "uniform sampler2D uColor;\n"
    "uniform sampler2D uAO;\n"
    "uniform sampler2D uBloom;\n"
    "uniform sampler2D uGBuf;\n"
    "uniform sampler2D uShadow0;\n"
    "uniform sampler2D uShadow1;\n"
    "uniform float uAOStr, uBloomStr, uFire, uExposure, uContrast, uSat, uVignette, uFog, uShafts, uTonemap;\n"
    "uniform vec3 uTint, uFogColor, uSunColor, uSun;  // uSun: xy = screen uv, z = visibility\n"
    "uniform int uNumLights;\n"
    "uniform vec4 uLightPos[16];  // view-space xyz, radius\n"
    "uniform vec4 uLightCol[16];\n"
    "// engine lighting (Enhanced): sun shadow maps + surface pass\n"
    "uniform float uEngine;\n"
    "uniform mat4 uShadowMat0, uShadowMat1;  // view space -> shadow map uv + depth\n"
    "uniform mat4 uInvView;                  // view -> world\n"
    "uniform vec4 uShadowInfo;               // 1/size, texel world size cascade 0, 1, depth units per world unit\n"
    "uniform vec3 uSunV;                     // direction to the sun, view space\n"
    "uniform vec3 uLightCol3;                // the sun's colour\n"
    "uniform float uAmbFrac;                 // ambient share of the game's lighting\n"
    "uniform vec3 uSkyCol, uGroundCol;\n"
    "uniform float uHemi, uRim, uSpec, uContact, uVolume, uShadowStr;\n"
    "uniform sampler2D uBounce;\n"
    "uniform float uLightScale, uBounceStr;\n"
    "uniform int uDebug;  // LUGARUEX_LIGHT_DEBUG: 1 sky visibility, 2 bounce light, 3 bent normal, 4 normal,\n"
    "                     // 5 game light (red: has a surface), 6 relight gain, 7 the game's colour, 8 surfaces (red) over it\n"
    "float hash(vec2 s) { return fract(sin(dot(gl_FragCoord.xy + s, vec2(12.9898, 78.233))) * 43758.5453); }\n"
    "float pcf(sampler2D sm, vec3 sc, float bias, float spread) {\n"
    "  float ang = hash(vec2(0.0)) * 6.2831853, sum = 0.0;\n"
    "  for (int i = 0; i < 12; i++) {\n"
    "    float t = (float(i) + 0.5) / 12.0, a = ang + float(i) * 2.3999632;\n"
    "    vec2 o = vec2(cos(a), sin(a)) * sqrt(t) * spread * uShadowInfo.x;\n"
    "    sum += step(sc.z - bias, texture2D(sm, sc.xy + o).r);\n"
    "  }\n"
    "  return sum / 12.0;\n"
    "}\n"
    "float sunShadow(vec3 p, vec3 n, float ndl) {\n"
    "  float slope = 1.0 - clamp(ndl, 0.0, 1.0);\n"
    "  vec4 s0 = uShadowMat0 * vec4(p + n * uShadowInfo.y * 1.5, 1.0);\n"
    "  if (s0.x > 0.04 && s0.y > 0.04 && s0.x < 0.96 && s0.y < 0.96 && s0.z < 1.0)\n"
    "    return pcf(uShadow0, s0.xyz, uShadowInfo.w * (0.03 + 0.06 * slope), 2.4);\n"
    "  vec4 s1 = uShadowMat1 * vec4(p + n * uShadowInfo.z * 1.5, 1.0);\n"
    "  if (s1.x < 0.0 || s1.y < 0.0 || s1.x > 1.0 || s1.y > 1.0 || s1.z >= 1.0) return 1.0;\n"
    "  float sh = pcf(uShadow1, s1.xyz, uShadowInfo.w * (0.08 + 0.2 * slope), 1.6);\n"
    "  vec2 e = min(s1.xy, 1.0 - s1.xy);\n"
    "  return mix(1.0, sh, clamp(min(e.x, e.y) * 10.0, 0.0, 1.0));\n"
    "}\n"
    "float contactShadow(vec3 p) {\n"
    "  if (-p.z > 30.0) return 1.0;\n"
    "  vec3 step3 = uSunV * 0.04;\n"
    "  vec3 r = p + step3 * (0.5 + hash(vec2(3.1, 7.7)));\n"
    "  // soft: a hit counts less the further along the ray and the nearer the edges of the\n"
    "  // thickness window it is (a hard yes/no per pixel left a speckled edge by the feet)\n"
    "  float occ = 0.0;\n"
    "  for (int i = 0; i < 10; i++) {\n"
    "    r += step3;\n"
    "    vec2 t = vec2(r.x / (-r.z * uProj.z), r.y / (-r.z * uProj.w)) * 0.5 + 0.5;\n"
    "    if (t.x < 0.0 || t.y < 0.0 || t.x > 1.0 || t.y > 1.0) break;\n"
    "    float dz = linZ(texture2D(uDepth, t).r) - r.z;\n"
    "    float hit = smoothstep(0.01, 0.04, dz) * (1.0 - smoothstep(0.2, 0.3, dz));\n"
    "    occ = max(occ, hit * (1.0 - float(i) / 10.0));\n"
    "  }\n"
    "  return 1.0 - occ;\n"
    "}\n"
    "vec3 lightShafts(vec3 p, bool sky) {\n"
    "  float len = sky ? 60.0 : min(length(p), 60.0);\n"
    "  vec3 dir = normalize(p);\n"
    "  float j = hash(vec2(5.3, 1.9)), lit = 0.0;\n"
    "  for (int i = 0; i < 16; i++) {\n"
    "    vec4 s = uShadowMat1 * vec4(dir * ((float(i) + j) / 16.0 * len), 1.0);\n"
    "    lit += (s.x > 0.0 && s.y > 0.0 && s.x < 1.0 && s.y < 1.0 && s.z < 1.0) ? step(s.z - uShadowInfo.w * 0.1, texture2D(uShadow1, s.xy).r) : 1.0;\n"
    "  }\n"
    "  lit /= 16.0;\n"
    "  float g = 0.7, cosT = dot(dir, uSunV);\n"
    "  float hg = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * cosT, 1.5) * 0.0796;\n"
    "  return uLightCol3 * uVolume * lit * hg * (1.0 - exp(-len * 0.035));\n"
    "}\n"
    "void main() {\n"
    "  vec3 c = texture2D(uColor, uv).rgb;\n"
    "  float d = texture2D(uDepth, uv).r;\n"
    "  bool sky = d >= 0.99999;\n"
    "  vec3 p = viewPos(uv, d);\n"
    "  if (!sky) {\n"
    "    // ambient occlusion, bent normal and bounce colour, filtered over the 4x4 noise pattern\n"
    "    vec3 aoS = vec3(1.0, 0.5, 0.5), bnc = vec3(0.0);\n"
    "    if (uAOStr > 0.0) {\n"
    "      vec3 sum = vec3(0.0), bsum = vec3(0.0);\n"
    "      float wsum = 0.0;\n"
    "      for (int y = -1; y <= 2; y++) for (int x = -1; x <= 2; x++) {\n"
    "        vec2 o = (vec2(float(x), float(y)) - 0.5) * uTexel * 2.0;  // the AO is half size: 4x4 of its texels\n"
    "        float dz = abs(linZ(texture2D(uDepth, uv + o).r) - p.z);\n"
    "        float w = exp(-dz / (0.02 * -p.z + 0.05));\n"
    "        sum += texture2D(uAO, uv + o).rgb * w; wsum += w;\n"
    "        if (uEngine > 0.5) bsum += texture2D(uBounce, uv + o).rgb * w;\n"
    "      }\n"
    "      aoS = sum / max(wsum, 0.0001);\n"
    "      bnc = bsum / max(wsum, 0.0001);\n"
    "      if (uDebug == 1) { gl_FragColor = vec4(vec3(aoS.r), 1.0); return; }\n"
    "      if (uDebug == 2) { gl_FragColor = vec4(bnc * (1.0 - aoS.r), 1.0); return; }\n"
    "      if (uDebug == 3) { gl_FragColor = vec4(aoS.gb, 1.0, 1.0); return; }\n"
    "      if (uEngine < 0.5) c *= mix(1.0, aoS.r, uAOStr);\n"
    "    }\n"
    "    vec3 n = normalAt(uv, p);\n"
    "    if (uEngine > 0.5) {\n"
    "      vec3 cIn = c;  // the game's own colour for this pixel\n"
    "      if (uDebug == 7) { gl_FragColor = vec4(cIn, 1.0); return; }\n"
    "      vec4 g = texture2D(uGBuf, uv);\n"
    "      bool gv = g.b >= 1.0 && abs(g.a + p.z) < 0.06 + 0.02 * -p.z;\n"
    "      if (gv) n = octDec(g.rg);\n"
    "      if (uDebug == 8) { gl_FragColor = vec4(gv ? mix(cIn, vec3(1.0, 0.0, 0.0), 0.45) : (g.b >= 1.0 ? mix(cIn, vec3(0.0, 0.0, 1.0), 0.45) : cIn), 1.0); return; }\n"
    "      float mat = gv ? floor(g.b) : 0.0;\n"
    "      float ndlv = gv ? fract(g.b) * 1.001 : 0.0;\n"
    "      float ndl = max(dot(n, uSunV), 0.0);\n"
    "      float s = sunShadow(p, n, ndl);\n"
    "      if (uContact > 0.0 && ndl > 0.0 && s > 0.0) s = min(s, mix(1.0, contactShadow(p), uContact));\n"
    "      if (mat == 3.0) s = mix(0.5, 1.0, s);  // leaves let light through: a crown never goes black\n"
    "      s = mix(1.0, s, uShadowStr);\n"
    "      float a = uAmbFrac;\n"
    "      bool relit = mat == 1.0 || mat == 2.0 || mat == 4.0;\n"
    "      float lam = (mat == 3.0) ? 0.75 : ndl;  // the game lights leaves flat\n"
    "      float fo = a + (1.0 - a) * (relit ? ndlv : lam);\n"
    "      if (uDebug == 4) { gl_FragColor = vec4(n * 0.5 + 0.5, 1.0); return; }\n"
    "      if (uDebug == 5) { gl_FragColor = vec4(vec3(fo), 1.0); if (gv) gl_FragColor.r = 1.0; return; }\n"
    "      vec3 alb = c / max(fo, 0.06);  // the surface under the game's full light\n"
    "      // sky light: only the visible part of the sky (GTAO), arriving along the bent normal\n"
    "      float vis = (uAOStr > 0.0) ? aoS.r : 1.0;\n"
    "      vec2 bxy = aoS.gb * 2.0 - 1.0;\n"
    "      vec3 bn = (uAOStr > 0.0) ? vec3(bxy, sqrt(max(0.0, 1.0 - dot(bxy, bxy)))) : n;\n"
    "      vec3 bw = (uInvView * vec4(bn, 0.0)).xyz;\n"
    "      float up = clamp(bw.y * 0.5 + 0.5, 0.0, 1.0);\n"
    "      vec3 hemi = mix(uGroundCol, uSkyCol, up);\n"
    "      hemi = mix(vec3(1.0), hemi / max(dot(hemi, vec3(0.299, 0.587, 0.114)), 0.05), 0.4) * mix(0.82, 1.12, up);  // a hint of colour; lighter from above, darker from below\n"
    "      vec3 skyL = a * vis * mix(vec3(1.0), hemi, uHemi);\n"
    "      // one bounce: the blocked part of the sky is replaced by the light the blockers reflect\n"
    "      vec3 bounceL = (1.0 - vis) * bnc / uLightScale * uBounceStr;\n"
    "      // ...but never more than the sky light it replaces: under a hoof on bright sand or\n"
    "      // snow it outshone the open ground and left a glowing rim round the feet\n"
    "      float bLum = dot(bounceL, vec3(0.299, 0.587, 0.114)), bMax = (1.0 - vis) * a;\n"
    "      if (bLum > bMax) bounceL *= bMax / bLum;\n"
    "      c = alb * (skyL + bounceL + vec3((1.0 - a) * lam * s));\n"
    "      if (relit) {\n"
    "        vec3 V = normalize(-p);\n"
    "        float nv = max(dot(n, V), 0.001);\n"
    "        float nh = max(dot(n, normalize(uSunV + V)), 0.0);\n"
    "        float sp = (mat == 4.0) ? pow(nh, 48.0) * 1.6 : (mat == 1.0 ? pow(nh, 10.0) * 0.07 : pow(nh, 16.0) * 0.05);\n"
    "        c += uLightCol3 * sp * s * step(0.001, ndl) * uSpec;\n"
    "        // specular occlusion (Lagarde): reflections of the sky are blocked like its light\n"
    "        float so = clamp(pow(nv + vis, 2.0) - 1.0 + vis, 0.0, 1.0);\n"
    "        if (mat == 4.0) {\n"
    "          // metal reflects the sky / ground around it (Schlick Fresnel)\n"
    "          vec3 R = (uInvView * vec4(reflect(-V, n), 0.0)).xyz;\n"
    "          vec3 env = mix(uGroundCol * 0.5, uSkyCol, smoothstep(-0.2, 0.3, R.y));\n"
    "          float F = 0.5 + 0.5 * pow(1.0 - nv, 5.0);\n"
    "          c += env * F * so * a * 0.6 * uSpec;\n"
    "        }\n"
    "        if (mat == 1.0) {\n"
    "          // fur: a cloth-like sheen (Charlie distribution, Estevez & Kulla), lit and\n"
    "          // shadowed by the sun like everything else\n"
    "          float sh2 = max(1.0 - nh * nh, 0.0078125);\n"
    "          float D = (2.0 + 1.0 / 0.55) * pow(sh2, 0.5 / 0.55) * 0.159155;\n"
    "          float Vs = 1.0 / (4.0 * (ndl + nv - ndl * nv));\n"
    "          c += uLightCol3 * alb * D * Vs * ndl * s * uRim;\n"
    "        }\n"
    "      }\n"
    "      if (uDebug == 6) { float r = dot(c, vec3(0.333)) / max(dot(cIn, vec3(0.333)), 0.004); gl_FragColor = vec4(relit ? r / 4.0 : 0.0, r > 2.0 && relit ? 1.0 : 0.0, 0.0, 1.0); return; }\n"
    "    }\n"
    "    if (uNumLights > 0 && uFire > 0.0) {\n"
    "      vec3 add = vec3(0.0);\n"
    "      for (int i = 0; i < 16; i++) {\n"
    "        if (i >= uNumLights) break;\n"
    "        vec3 L = uLightPos[i].xyz - p;\n"
    "        float dist = length(L);\n"
    "        float q = clamp(dist / uLightPos[i].w, 0.0, 1.0);\n"
    "        float att = (1.0 - q * q) * (1.0 - q * q) / (1.0 + 4.0 * q * q);  // smooth, reaches the radius\n"
    "        float ndl = max(dot(n, L / max(dist, 0.001)), 0.0);\n"
    "        add += uLightCol[i].rgb * att * (0.3 + 0.7 * ndl);\n"
    "      }\n"
    "      c += (c * 1.6 + vec3(0.03)) * add * uFire;\n"
    "    }\n"
    "    if (uFog > 0.0) c = mix(c, uFogColor, clamp(1.0 - exp(-uFog * -p.z), 0.0, 0.85));\n"
    "  }\n"
    "  if (uShafts > 0.0 && uSun.z > 0.0) {\n"
    "    vec2 delta = (uSun.xy - uv) / 32.0;\n"
    "    vec2 s = uv; float acc = 0.0, w = 1.0;\n"
    "    for (int i = 0; i < 32; i++) {\n"
    "      s += delta;\n"
    "      acc += step(0.99999, texture2D(uDepth, s).r) * w;\n"
    "      w *= 0.955;\n"
    "    }\n"
    "    c += uSunColor * (acc / 32.0) * uShafts * uSun.z;\n"
    "  }\n"
    "  if (uEngine > 0.5 && uVolume > 0.0) c += lightShafts(p, sky);\n"
    "  if (uBloomStr > 0.0) c += texture2D(uBloom, uv).rgb * uBloomStr;\n"
    "  c *= uExposure;\n"
    "  if (uTonemap > 0.5) c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);\n"
    "  c = (c - 0.5) * uContrast + 0.5;\n"
    "  float l = dot(c, vec3(0.299, 0.587, 0.114));\n"
    "  c = mix(vec3(l), c, uSat) * uTint;\n"
    "  vec2 q = uv - 0.5;\n"
    "  c *= 1.0 - uVignette * dot(q, q) * 2.0;\n"
    "  gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);\n"
    "}\n";

// sun shadow maps: depth from the sun; leaves and bushes cut out by their texture
static const char* kShadowVS =
    "#version 120\n"
    "varying vec2 vUV;\n"
    "void main() { gl_Position = ftransform(); vUV = gl_MultiTexCoord0.xy; }\n";
static const char* kShadowFS =
    "#version 120\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uAlphaTest;\n"
    "void main() { if (uAlphaTest > 0.0 && texture2D(uTex, vUV).a < uAlphaTest) discard; gl_FragColor = vec4(1.0); }\n";

// surface pass: per-pixel normal (view space, octahedral), material + the game's own
// per-vertex sun term (what fixed-function lighting used), and linear depth to match
// the pixel against the finished frame
static const char* kSurfVS =
    "#version 120\n"
    "varying vec3 vN, vP;\n"
    "varying vec2 vUV;\n"
    "varying float vNdl;\n"
    "uniform vec3 uSunV;\n"
    "void main() {\n"
    "  gl_Position = ftransform();\n"
    "  vP = (gl_ModelViewMatrix * gl_Vertex).xyz;\n"
    "  vN = normalize(gl_NormalMatrix * gl_Normal);\n"
    "  vNdl = max(dot(vN, uSunV), 0.0);\n"
    "  vUV = gl_MultiTexCoord0.xy;\n"
    "}\n";
static const char* kSurfFS =
    "#version 120\n"
    "varying vec3 vN, vP;\n"
    "varying vec2 vUV;\n"
    "varying float vNdl;\n"
    "uniform sampler2D uTex;\n"
    "uniform sampler2D uSceneDepth;  // the frame's own depth: only what is really seen gets a surface\n"
    "uniform vec2 uInvSize;\n"
    "uniform float uAlphaTest, uMat;\n"
    "vec2 octEnc(vec3 n) {\n"
    "  n /= abs(n.x) + abs(n.y) + abs(n.z);\n"
    "  vec2 e = n.xy;\n"
    "  if (n.z < 0.0) e = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);\n"
    "  return e;\n"
    "}\n"
    "void main() {\n"
    "  // hidden behind something the models pass doesn't draw (the ground: feet sink into it a\n"
    "  // little) - no surface here, or the ground next to a foot was lit as if it were the foot\n"
    "  float sd = texture2D(uSceneDepth, gl_FragCoord.xy * uInvSize).r;\n"
    "  // (slack: MSAA keeps one sample's depth, up to half a pixel from the centre)\n"
    "  if (gl_FragCoord.z > sd + 0.000002 + 0.75 * fwidth(gl_FragCoord.z)) discard;\n"
    "  if (uAlphaTest > 0.0 && texture2D(uTex, vUV).a < uAlphaTest) discard;\n"
    "  vec3 n = normalize(vN);\n"
    "  if (uMat == 3.0 && dot(n, -vP) < 0.0) n = -n;  // leaves: both sides\n"
    "  gl_FragColor = vec4(octEnc(n), uMat + min(vNdl, 0.998), -vP.z);\n"
    "}\n";

// red-green colour-blind filter (deuteranopia "daltonize"): simulate what a
// deuteranope sees, then move the red/green difference they lose into
// lightness and blue, which they can tell apart
static const char* kColorblind =
    "uniform sampler2D uColor;\n"
    "uniform int uMode; // 1 deuteranopia, 2 protanopia, 3 tritanopia\n"
    "void main() {\n"
    "  vec3 c = texture2D(uColor, uv).rgb;\n"
    "  float L = 17.8824 * c.r + 43.5161 * c.g + 4.11935 * c.b;\n"
    "  float M = 3.45565 * c.r + 27.1554 * c.g + 3.86714 * c.b;\n"
    "  float S = 0.0299566 * c.r + 0.184309 * c.g + 1.46709 * c.b;\n"
    "  float l = L, m = M, s = S;\n"
    "  if (uMode == 2) l = 2.02344 * M - 2.52581 * S;\n"
    "  else if (uMode == 3) s = -0.395913 * L + 0.801109 * M;\n"
    "  else m = 0.494207 * L + 1.24827 * S;\n"
    "  vec3 sim = vec3(0.0809444479 * l - 0.130504409 * m + 0.116721066 * s,\n"
    "                  -0.0102485335 * l + 0.0540193266 * m - 0.113614708 * s,\n"
    "                  -0.000365296938 * l - 0.00412161469 * m + 0.693511405 * s);\n"
    "  vec3 e = c - sim;\n"
    "  // red-green types: move the lost difference into green/blue; tritanopia: into red/green\n"
    "  vec3 corr = (uMode == 3) ? vec3(e.r + 0.7 * e.b, e.g + 0.7 * e.b, 0.0) : vec3(0.0, 0.7 * e.r + e.g, 0.7 * e.r + e.b);\n"
    "  gl_FragColor = vec4(clamp(c + corr, 0.0, 1.0), 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const std::string& src)
{
    GLuint s = ex_glCreateShader(type);
    const char* p = src.c_str();
    ex_glShaderSource(s, 1, &p, NULL);
    ex_glCompileShader(s);
    GLint ok = 0;
    ex_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        ex_glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "LugaruEX lighting: shader compile failed:\n%s\n", log);
        ex_glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint programVF(const char* vert, const char* frag)
{
    GLuint vs = compile(GL_VERTEX_SHADER, vert);
    GLuint fs = compile(GL_FRAGMENT_SHADER, frag);
    if (!vs || !fs) {
        return 0;
    }
    GLuint p = ex_glCreateProgram();
    ex_glAttachShader(p, vs);
    ex_glAttachShader(p, fs);
    ex_glLinkProgram(p);
    GLint ok = 0;
    ex_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        ex_glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "LugaruEX lighting: link failed:\n%s\n", log);
        return 0;
    }
    return p;
}

static GLuint program(const char* frag)
{
    GLuint vs = compile(GL_VERTEX_SHADER, kVS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, std::string(kCommon) + frag);
    if (!vs || !fs) {
        return 0;
    }
    GLuint p = ex_glCreateProgram();
    ex_glAttachShader(p, vs);
    ex_glAttachShader(p, fs);
    ex_glLinkProgram(p);
    GLint ok = 0;
    ex_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        ex_glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "LugaruEX lighting: link failed:\n%s\n", log);
        return 0;
    }
    return p;
}

// ------------------------------------------------------------------ state
static bool g_ready = false;
static bool g_shadersOff = false;
static int g_look = 1;
static Uint32 g_nameShownAt = 0;
static GLuint g_progAO = 0, g_progBright = 0, g_progBlur = 0, g_progComp = 0, g_progCB = 0;
static GLuint g_cbTex = 0, g_cbFbo = 0;
static int g_cbW = 0, g_cbH = 0;
static GLuint g_texColor = 0, g_texDepth = 0, g_texAO = 0, g_texBounce = 0, g_texBloomA = 0, g_texBloomB = 0;
static GLuint g_fboScene = 0, g_fboAO = 0, g_fboBloomA = 0, g_fboBloomB = 0;
static GLuint g_texG = 0, g_texGDepth = 0, g_fboG = 0; // surface pass (Enhanced engine lighting)
static int g_w = 0, g_h = 0;
static bool g_warnedBlit = false;
static float g_camMV[16], g_camProj[16];
static bool g_haveCam = false;

// engine lighting (Enhanced): the frame's model draws, the sun's shadow maps, the surface pass
struct DrawRec
{
    const float* v;
    int n;
    bool colorLayout, lit;
    int kind;
    GLuint tex;
    float mv[16];
};
static std::vector<DrawRec> g_draws;
static bool g_recording = false;
static int g_drawKind = LK_NONE;
static bool g_engine = false; // shaders for shadows / surfaces compiled
static GLuint g_progShadow = 0, g_progSurf = 0;
static const int kShadowSize = 2048;
static GLuint g_shadowTex[2] = { 0, 0 }, g_shadowFbo[2] = { 0, 0 };
static bool g_shadowsThisFrame = false;

void LightingCaptureCamera()
{
    if (!g_ready || g_look == 0) {
        return;
    }
    glGetFloatv(GL_MODELVIEW_MATRIX, g_camMV);
    glGetFloatv(GL_PROJECTION_MATRIX, g_camProj);
    g_haveCam = true;
    g_draws.clear();
    g_recording = g_engine;
}

void LightingDrawKind(int kind)
{
    g_drawKind = kind;
}

void LightingRecordDraw(const float* vArray, int vertices, bool colorLayout)
{
    if (!g_recording || g_drawKind == LK_NONE || !vArray || vertices <= 0) {
        return;
    }
    DrawRec r;
    r.v = vArray;
    r.n = vertices;
    r.colorLayout = colorLayout;
    r.lit = glIsEnabled(GL_LIGHTING) && !colorLayout;
    r.kind = g_drawKind;
    GLint t = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &t);
    r.tex = (GLuint)t;
    glGetFloatv(GL_MODELVIEW_MATRIX, r.mv);
    g_draws.push_back(r);
}

bool LightingRealShadows()
{
    // (a sun too low for shadow maps keeps the old blob shadows)
    const float l = sqrtf(light.location.x * light.location.x + light.location.y * light.location.y + light.location.z * light.location.z);
    return g_ready && g_engine && g_look == 1 && l > 0 && light.location.y / l > .14f;
}

static GLint U(GLuint prog, const char* name)
{
    return ex_glGetUniformLocation(prog, name);
}

static GLuint makeTex(GLint internal, int w, int h, GLenum format, GLenum type, bool linear)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, NULL);
    return t;
}

static GLuint makeFbo(GLuint color, GLuint depthStencil)
{
    GLuint f;
    ex_glGenFramebuffers(1, &f);
    ex_glBindFramebuffer(GL_FRAMEBUFFER, f);
    ex_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    if (depthStencil) {
        ex_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthStencil, 0);
    }
    GLenum st = ex_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "LugaruEX lighting: framebuffer incomplete (0x%x)\n", st);
    }
    ex_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return f;
}

// colour (may be 0: depth only, e.g. a shadow map) + a plain depth texture
static GLuint makeDepthFbo(GLuint color, GLuint depth)
{
    GLuint f;
    ex_glGenFramebuffers(1, &f);
    ex_glBindFramebuffer(GL_FRAMEBUFFER, f);
    if (color) {
        ex_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    } else {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }
    ex_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    GLenum st = ex_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "LugaruEX lighting: framebuffer incomplete (0x%x)\n", st);
    }
    ex_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return st == GL_FRAMEBUFFER_COMPLETE ? f : 0;
}

static void freeTargets()
{
    GLuint texs[] = { g_texColor, g_texDepth, g_texAO, g_texBounce, g_texBloomA, g_texBloomB, g_texG, g_texGDepth };
    glDeleteTextures(8, texs);
    GLuint fbos[] = { g_fboScene, g_fboAO, g_fboBloomA, g_fboBloomB, g_fboG };
    ex_glDeleteFramebuffers(5, fbos);
    g_texColor = g_texDepth = g_texAO = g_texBounce = g_texBloomA = g_texBloomB = g_texG = g_texGDepth = 0;
    g_fboScene = g_fboAO = g_fboBloomA = g_fboBloomB = g_fboG = 0;
}

// split screen draws several view sizes per frame: keep one set of targets per size
struct TargetSet
{
    int w, h;
    GLuint texColor, texDepth, texAO, texBounce, texBloomA, texBloomB, texG, texGDepth;
    GLuint fboScene, fboAO, fboBloomA, fboBloomB, fboG;
};
static std::vector<TargetSet> g_sets;

static void ensureTargets(int w, int h)
{
    if (w == g_w && h == g_h && g_fboScene) {
        return;
    }
    for (auto& s : g_sets) {
        if (s.w == w && s.h == h) {
            g_w = w;
            g_h = h;
            g_texColor = s.texColor, g_texDepth = s.texDepth, g_texAO = s.texAO, g_texBounce = s.texBounce, g_texBloomA = s.texBloomA, g_texBloomB = s.texBloomB, g_texG = s.texG, g_texGDepth = s.texGDepth;
            g_fboScene = s.fboScene, g_fboAO = s.fboAO, g_fboBloomA = s.fboBloomA, g_fboBloomB = s.fboBloomB, g_fboG = s.fboG;
            return;
        }
    }
    if (g_sets.size() >= 6) { // window resized a lot: start over
        for (auto& s : g_sets) {
            g_texColor = s.texColor, g_texDepth = s.texDepth, g_texAO = s.texAO, g_texBounce = s.texBounce, g_texBloomA = s.texBloomA, g_texBloomB = s.texBloomB, g_texG = s.texG, g_texGDepth = s.texGDepth;
            g_fboScene = s.fboScene, g_fboAO = s.fboAO, g_fboBloomA = s.fboBloomA, g_fboBloomB = s.fboBloomB, g_fboG = s.fboG;
            freeTargets();
        }
        g_sets.clear();
    }
    g_w = w;
    g_h = h;
    const int bw = std::max(1, w / 4), bh = std::max(1, h / 4);
    g_texColor = makeTex(GL_RGBA8, w, h, GL_RGBA, GL_UNSIGNED_BYTE, true);
    g_texDepth = makeTex(GL_DEPTH24_STENCIL8, w, h, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, false);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE);
    g_texAO = makeTex(GL_RGBA8, (w + 1) / 2, (h + 1) / 2, GL_RGBA, GL_UNSIGNED_BYTE, true);
    g_texBloomA = makeTex(GL_RGBA8, bw, bh, GL_RGBA, GL_UNSIGNED_BYTE, true);
    g_texBloomB = makeTex(GL_RGBA8, bw, bh, GL_RGBA, GL_UNSIGNED_BYTE, true);
    g_fboScene = makeFbo(g_texColor, g_texDepth);
    g_texBounce = makeTex(GL_RGBA8, (w + 1) / 2, (h + 1) / 2, GL_RGBA, GL_UNSIGNED_BYTE, true);
    g_fboAO = makeFbo(g_texAO, 0);
    { // second target: the colour of what blocks the sky (one bounce)
        ex_glBindFramebuffer(GL_FRAMEBUFFER, g_fboAO);
        ex_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, g_texBounce, 0);
        const GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        ex_glDrawBuffers(2, bufs);
        ex_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    g_fboBloomA = makeFbo(g_texBloomA, 0);
    g_fboBloomB = makeFbo(g_texBloomB, 0);
    g_texG = g_texGDepth = g_fboG = 0;
    if (g_engine) {
        g_texG = makeTex(GL_RGBA32F, w, h, GL_RGBA, GL_FLOAT, false);
        g_texGDepth = makeTex(GL_DEPTH_COMPONENT24, w, h, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, false);
        g_fboG = makeDepthFbo(g_texG, g_texGDepth);
    }
    g_sets.push_back({ w, h, g_texColor, g_texDepth, g_texAO, g_texBounce, g_texBloomA, g_texBloomB, g_texG, g_texGDepth, g_fboScene, g_fboAO, g_fboBloomA, g_fboBloomB, g_fboG });
}

void LightingInit()
{
    g_look = std::max(0, std::min(kNumLooks - 1, cfg.lighting));
    if (!cfg.shaders && !cfg.colorblind) {
        g_shadersOff = true; // launcher "Shaders" box unticked: Original look only
        g_look = 0;
        printf("LugaruEX lighting: shaders turned off in the launcher\n");
        return;
    }
    bool ok = load(ex_glCreateShader, "glCreateShader") && load(ex_glShaderSource, "glShaderSource") &&
              load(ex_glCompileShader, "glCompileShader") && load(ex_glGetShaderiv, "glGetShaderiv") &&
              load(ex_glGetShaderInfoLog, "glGetShaderInfoLog") && load(ex_glDeleteShader, "glDeleteShader") &&
              load(ex_glCreateProgram, "glCreateProgram") && load(ex_glAttachShader, "glAttachShader") &&
              load(ex_glLinkProgram, "glLinkProgram") && load(ex_glGetProgramiv, "glGetProgramiv") &&
              load(ex_glGetProgramInfoLog, "glGetProgramInfoLog") && load(ex_glUseProgram, "glUseProgram") &&
              load(ex_glGetUniformLocation, "glGetUniformLocation") && load(ex_glUniform1i, "glUniform1i") &&
              load(ex_glUniform1f, "glUniform1f") && load(ex_glUniform2f, "glUniform2f") &&
              load(ex_glUniform3f, "glUniform3f") && load(ex_glUniform4f, "glUniform4f") &&
              load(ex_glUniform4fv, "glUniform4fv") && load(ex_glUniformMatrix4fv, "glUniformMatrix4fv") &&
              load(ex_glActiveTexture, "glActiveTexture") &&
              load(ex_glGenFramebuffers, "glGenFramebuffers") && load(ex_glDeleteFramebuffers, "glDeleteFramebuffers") &&
              load(ex_glBindFramebuffer, "glBindFramebuffer") && load(ex_glFramebufferTexture2D, "glFramebufferTexture2D") &&
              load(ex_glCheckFramebufferStatus, "glCheckFramebufferStatus") && load(ex_glBlitFramebuffer, "glBlitFramebuffer") &&
              load(ex_glDrawBuffers, "glDrawBuffers");
    if (!ok) {
        fprintf(stderr, "LugaruEX lighting: OpenGL 3 features missing, only the Original look is available\n");
        return;
    }
    if (cfg.colorblind) {
        g_progCB = program(kColorblind);
        printf("LugaruEX: colour blind filter (mode %d) %s\n", cfg.colorblind, g_progCB ? "on" : "FAILED");
    }
    if (!cfg.shaders) {
        g_shadersOff = true; // the colour filter still works; the looks stay off
        g_look = 0;
        printf("LugaruEX lighting: shaders turned off in the launcher\n");
        return;
    }
    g_progAO = program(kAO);
    g_progBright = program(kBright);
    g_progBlur = program(kBlur);
    g_progComp = program(kComposite);
    g_ready = g_progAO && g_progBright && g_progBlur && g_progComp;
    // engine lighting for Enhanced: the sun's shadow maps and the surface pass
    if (g_ready && !getenv("LUGARUEX_NO_ENGINE_LIGHT")) {
        g_progShadow = programVF(kShadowVS, kShadowFS);
        g_progSurf = programVF(kSurfVS, kSurfFS);
        bool ok2 = g_progShadow && g_progSurf;
        for (int i = 0; i < 2 && ok2; i++) {
            g_shadowTex[i] = makeTex(GL_DEPTH_COMPONENT24, kShadowSize, kShadowSize, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, false);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
            glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE);
            g_shadowFbo[i] = makeDepthFbo(0, g_shadowTex[i]);
            ok2 = g_shadowFbo[i] != 0;
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        g_engine = ok2;
        printf("LugaruEX lighting: sun shadows and per-pixel light %s\n", g_engine ? "ready" : "FAILED (Enhanced without them)");
    }
    printf("LugaruEX lighting: %s, look '%s'\n", g_ready ? "ready" : "FAILED (Original only)", kLooks[g_look].name);
}

int LightingLook()
{
    return g_ready ? g_look : 0;
}

void LightingCycle()
{
    if (!g_ready) {
        g_look = 0;
    } else {
        g_look = (g_look + 1) % kNumLooks;
    }
    g_nameShownAt = SDL_GetTicks();
    if (cfg.loaded && g_ready) { // never overwrite the saved look while shaders are off
        SetIniValue("Display", "Lighting", std::to_string(g_look));
    }
    cfg.lighting = g_look;
}

static void quad()
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(-1, -1);
    glTexCoord2f(1, 0);
    glVertex2f(1, -1);
    glTexCoord2f(1, 1);
    glVertex2f(1, 1);
    glTexCoord2f(0, 1);
    glVertex2f(-1, 1);
    glEnd();
}

static void bindTex(int unit, GLuint tex)
{
    ex_glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

static void common(GLuint prog, const float* proj, int w, int h)
{
    ex_glUseProgram(prog);
    ex_glUniform1i(U(prog, "uDepth"), 1);
    ex_glUniform4f(U(prog, "uProj"), proj[10], proj[14], 1.f / proj[0], 1.f / proj[5]);
    ex_glUniform2f(U(prog, "uTexel"), 1.f / w, 1.f / h);
}

struct FireLight
{
    XYZ pos;
    float radius, intensity, seed;
    float d2;
};

// ------------------------------------------------------------------ engine lighting
static void mat4Mul(const float* a, const float* b, float* o) // o = a * b (column-major)
{
    float r[16];
    for (int c = 0; c < 4; c++) {
        for (int k = 0; k < 4; k++) {
            r[c * 4 + k] = a[0 * 4 + k] * b[c * 4 + 0] + a[1 * 4 + k] * b[c * 4 + 1] + a[2 * 4 + k] * b[c * 4 + 2] + a[3 * 4 + k] * b[c * 4 + 3];
        }
    }
    memcpy(o, r, sizeof(r));
}

static void mat4InvRigid(const float* m, float* o) // the camera: rotation + translation
{
    float r[16] = { m[0], m[4], m[8], 0, m[1], m[5], m[9], 0, m[2], m[6], m[10], 0, 0, 0, 0, 1 };
    r[12] = -(m[0] * m[12] + m[1] * m[13] + m[2] * m[14]);
    r[13] = -(m[4] * m[12] + m[5] * m[13] + m[6] * m[14]);
    r[14] = -(m[8] * m[12] + m[9] * m[13] + m[10] * m[14]);
    memcpy(o, r, sizeof(r));
}

static XYZ xyz(float x, float y, float z)
{
    XYZ v;
    v.x = x, v.y = y, v.z = z;
    return v;
}

static void drawArrays(const float* v, int n, bool colorLayout)
{
    glInterleavedArrays(colorLayout ? GL_T2F_C3F_V3F : GL_T2F_N3F_V3F, 8 * sizeof(GLfloat), v);
    glDrawArrays(GL_TRIANGLES, 0, n);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

// what the composite needs from the engine passes
struct EngineFrame
{
    bool on = false;
    float shadowMat[2][16], invView[16];
    float info[4];
    float sunV[3];
    float ambFrac = .3f;
    float shadowStr = 0, volume = 0;
};

// Sun shadow maps: two cascades around the camera (near: sharp, far: wide), each an
// orthographic view along the sunlight, snapped to its texels so shadows don't crawl.
// Every object of the level is drawn (with its sway) - trees behind the camera still
// throw shadows into view - plus the characters and weapons this frame drew.
// Then the surface pass: the frame's own models again, from the camera, writing
// per-pixel normal, material, the game's per-vertex sun term and depth.
static void enginePasses(int w, int h, const float* proj, const float* mv, EngineFrame& ef)
{
    XYZ L = light.location;
    const float ll = sqrtf(L.x * L.x + L.y * L.y + L.z * L.z);
    if (ll <= 0) {
        return;
    }
    L = L / ll;
    mat4InvRigid(mv, ef.invView);
    XYZ cam = xyz(ef.invView[12], ef.invView[13], ef.invView[14]);
    XYZ fwd = xyz(-mv[2], -mv[6], -mv[10]);
    ef.sunV[0] = mv[0] * L.x + mv[4] * L.y + mv[8] * L.z;
    ef.sunV[1] = mv[1] * L.x + mv[5] * L.y + mv[9] * L.z;
    ef.sunV[2] = mv[2] * L.x + mv[6] * L.y + mv[10] * L.z;
    const float lumA = light.ambient[0] * .299f + light.ambient[1] * .587f + light.ambient[2] * .114f;
    const float lumD = light.color[0] * .299f + light.color[1] * .587f + light.color[2] * .114f;
    ef.ambFrac = std::max(.15f, std::min(.85f, lumA / std::max(.01f, lumA + lumD)));
    // a low sun: no shadows, they would be endless
    const float sunUp = std::max(0.f, std::min(1.f, (L.y - .08f) / .12f));
    ef.shadowStr = .88f * sunUp;
    ef.volume = .55f * sunUp;

    const float radius[2] = { 14.f, 70.f };
    const float nearZ = 1.f, farZ = 260.f;
    const XYZ upv = fabsf(L.y) > .99f ? xyz(0, 0, 1) : xyz(0, 1, 0);
    XYZ f = L * -1.f;
    XYZ s;
    CrossProduct(f, upv, &s);
    Normalise(&s);
    XYZ u;
    CrossProduct(s, f, &u);
    float lightView[2][16], lightProj[2][16];
    for (int i = 0; i < 2; i++) {
        XYZ c = cam + fwd * (radius[i] * (i == 0 ? .55f : .7f));
        const float texel = 2 * radius[i] / kShadowSize;
        const float cs = dotproduct(&s, &c), cu = dotproduct(&u, &c);
        c += s * (floorf(cs / texel) * texel - cs) + u * (floorf(cu / texel) * texel - cu);
        const XYZ e = c + L * (farZ / 2);
        float* V = lightView[i];
        V[0] = s.x, V[4] = s.y, V[8] = s.z, V[12] = -dotproduct(&s, &e);
        V[1] = u.x, V[5] = u.y, V[9] = u.z, V[13] = -dotproduct(&u, &e);
        V[2] = -f.x, V[6] = -f.y, V[10] = -f.z, V[14] = dotproduct(&f, &e);
        V[3] = V[7] = V[11] = 0, V[15] = 1;
        float* P = lightProj[i];
        memset(P, 0, 64);
        P[0] = 1 / radius[i], P[5] = 1 / radius[i], P[10] = -2 / (farZ - nearZ), P[14] = -(farZ + nearZ) / (farZ - nearZ), P[15] = 1;
        // composite: view space -> shadow uv/depth
        const float bias[16] = { .5f, 0, 0, 0, 0, .5f, 0, 0, 0, 0, .5f, 0, .5f, .5f, .5f, 1 };
        float t[16];
        mat4Mul(bias, P, t);
        mat4Mul(t, V, t);
        mat4Mul(t, ef.invView, ef.shadowMat[i]);
    }
    ef.info[0] = 1.f / kShadowSize;
    ef.info[1] = 2 * radius[0] / kShadowSize;
    ef.info[2] = 2 * radius[1] / kShadowSize;
    ef.info[3] = 1.f / (farZ - nearZ);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    ex_glActiveTexture(GL_TEXTURE0);
    if (ef.shadowStr > 0) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 3.f);
        ex_glUseProgram(g_progShadow);
        ex_glUniform1i(U(g_progShadow, "uTex"), 0);
        const GLint uAlpha = U(g_progShadow, "uAlphaTest");
        for (int i = 0; i < 2; i++) {
            ex_glBindFramebuffer(GL_FRAMEBUFFER, g_shadowFbo[i]);
            glViewport(0, 0, kShadowSize, kShadowSize);
            glClear(GL_DEPTH_BUFFER_BIT);
            glMatrixMode(GL_PROJECTION);
            glLoadMatrixf(lightProj[i]);
            glMatrixMode(GL_MODELVIEW);
            const XYZ c = cam + fwd * (radius[i] * (i == 0 ? .55f : .7f));
            float m[16], wm[16];
            for (auto& op : Object::objects) {
                const Object& o = *op;
                if (o.type == firetype || (o.model.type != normaltype && o.model.type != decalstype) || !o.model.vArray) {
                    continue;
                }
                const float dx = o.position.x - c.x, dz = o.position.z - c.z;
                const float reach = radius[i] * 1.5f + o.model.boundingsphereradius + 10.f; // tall things reach in from outside
                if (dx * dx + dz * dz > reach * reach) {
                    continue;
                }
                const bool foliage = o.type == treeleavestype || o.type == bushtype;
                ex_glUniform1f(uAlpha, foliage ? .5f : 0.f);
                if (foliage) {
                    o.exBindTexture();
                }
                o.exWorldMatrix(wm);
                mat4Mul(lightView[i], wm, m);
                glLoadMatrixf(m);
                drawArrays(o.model.vArray, (int)o.model.Triangles.size() * 3, o.model.color);
            }
            ex_glUniform1f(uAlpha, 0.f);
            for (const DrawRec& r : g_draws) {
                if (r.kind & LK_STATIC) {
                    continue; // drawn above, also when off screen
                }
                mat4Mul(ef.invView, r.mv, wm);
                mat4Mul(lightView[i], wm, m);
                glLoadMatrixf(m);
                drawArrays(r.v, r.n, r.colorLayout);
            }
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
        g_shadowsThisFrame = true;
    }

    // surface pass
    ex_glBindFramebuffer(GL_FRAMEBUFFER, g_fboG);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj);
    glMatrixMode(GL_MODELVIEW);
    ex_glUseProgram(g_progSurf);
    ex_glUniform1i(U(g_progSurf, "uTex"), 0);
    ex_glUniform3f(U(g_progSurf, "uSunV"), ef.sunV[0], ef.sunV[1], ef.sunV[2]);
    bindTex(5, g_texDepth); // (copied from the frame before this pass)
    ex_glActiveTexture(GL_TEXTURE0);
    ex_glUniform1i(U(g_progSurf, "uSceneDepth"), 5);
    ex_glUniform2f(U(g_progSurf, "uInvSize"), 1.f / w, 1.f / h);
    const GLint uAlpha = U(g_progSurf, "uAlphaTest"), uMat = U(g_progSurf, "uMat");
    for (const DrawRec& r : g_draws) {
        const int k = r.kind & 7;
        float mat = k == LK_CHAR ? 1.f : k == LK_METAL ? 4.f : k == LK_FOLIAGE ? 3.f : 2.f;
        if (!r.lit && mat != 3.f) {
            mat = 5.f; // drawn without the game's lighting: keep its look, only shadows
        }
        ex_glUniform1f(uMat, mat);
        ex_glUniform1f(uAlpha, k == LK_FOLIAGE ? .5f : 0.f);
        if (k == LK_FOLIAGE) {
            glBindTexture(GL_TEXTURE_2D, r.tex);
        }
        glLoadMatrixf(r.mv);
        drawArrays(r.v, r.n, r.colorLayout);
    }
    ex_glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    ef.on = true;
}

// brightness of the game's full light (ambient + sun)
static float lumLight()
{
    return light.ambient[0] * .299f + light.ambient[1] * .587f + light.ambient[2] * .114f + light.color[0] * .299f +
           light.color[1] * .587f + light.color[2] * .114f;
}

static void collectFires(std::vector<FireLight>& out)
{
    out.clear();
    for (size_t i = 0; i < Person::players.size(); i++) {
        const Person& p = *Person::players[i];
        if (p.onfire) {
            XYZ c = p.coords;
            c.y += 1.0f;
            out.push_back({ c, 16.f, 1.5f, (float)i * 1.7f, 0 });
        }
    }
    for (size_t i = 0; i < weapons.size(); i++) {
        if (weapons[i].isOnFire()) {
            XYZ c = (weapons[i].position + weapons[i].tippoint) / 2;
            out.push_back({ c, 9.f, 0.9f, 10.f + (float)i * 2.3f, 0 });
        }
    }
    for (size_t i = 0; i < Object::objects.size(); i++) {
        const Object& o = *Object::objects[i];
        if (o.type == firetype || o.onfire) {
            XYZ c = o.position;
            c.y += (o.type == firetype) ? 0.8f : 2.0f;
            out.push_back({ c, (o.type == firetype) ? 20.f : 16.f, 1.4f, 30.f + (float)i * 3.1f, 0 });
        }
    }
    for (auto& f : out) {
        XYZ d = f.pos - viewer;
        f.d2 = d.x * d.x + d.y * d.y + d.z * d.z;
    }
    std::sort(out.begin(), out.end(), [](const FireLight& a, const FireLight& b) { return a.d2 < b.d2; });
    if (out.size() > 16) {
        out.resize(16);
    }
}

void LightingApply()
{
    if (!g_ready || g_look == 0 || stereomode != stereoNone) {
        return;
    }
    const Look& L = kLooks[g_look];
    const int w = (int)screenwidth, h = (int)screenheight;
    if (w <= 0 || h <= 0) {
        return;
    }

    if (!g_haveCam) {
        return;
    }
    g_haveCam = false;
    float proj[16], mv[16], sunPos[4], fogColor[4];
    memcpy(proj, g_camProj, sizeof(proj));
    memcpy(mv, g_camMV, sizeof(mv));
    glGetLightfv(GL_LIGHT0, GL_POSITION, sunPos); // already in eye space
    glGetFloatv(GL_FOG_COLOR, fogColor);
    if (proj[0] == 0 || proj[5] == 0 || proj[11] > -0.5f) {
        return; // not a perspective projection right now
    }

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glDisable(GL_SCISSOR_TEST); // split screen: the off-screen passes cover the whole target
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glColorMask(1, 1, 1, 1);
    glColor4f(1, 1, 1, 1);

    ensureTargets(w, h);

    // 1. copy (and MSAA-resolve) the rendered world: colour + depth
    ex_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    ex_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_fboScene);
    while (glGetError() != GL_NO_ERROR) {
    }
    const int vx = ViewX(), vy = ViewY(); // split screen: this view's corner on the window
    ex_glBlitFramebuffer(vx, vy, vx + w, vy + h, 0, 0, w, h, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);
    if (glGetError() != GL_NO_ERROR) {
        // depth formats differ on this driver: copy colour only, depth effects off
        ex_glBlitFramebuffer(vx, vy, vx + w, vy + h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        if (!g_warnedBlit) {
            fprintf(stderr, "LugaruEX lighting: depth copy unsupported, depth effects disabled\n");
            g_warnedBlit = true;
        }
    }

    // 1b. engine lighting (Enhanced): sun shadow maps + surface pass from this frame's draws
    EngineFrame ef;
    g_recording = false;
    g_shadowsThisFrame = false;
    if (g_engine && g_look == 1 && g_fboG && !g_warnedBlit) {
        enginePasses(w, h, proj, mv, ef);
    }

    bindTex(1, g_texDepth);

    // 2. ambient occlusion
    if (L.ao > 0) {
        ex_glBindFramebuffer(GL_FRAMEBUFFER, g_fboAO);
        glViewport(0, 0, (w + 1) / 2, (h + 1) / 2); // half resolution, upsampled by the composite
        common(g_progAO, proj, w, h);
        ex_glUniform1f(U(g_progAO, "uRadius"), L.aoRadius);
        ex_glUniform1f(U(g_progAO, "uPixPerUnit"), .5f * proj[5] * h);
        bindTex(0, g_texColor);
        ex_glUniform1i(U(g_progAO, "uColor"), 0);
        ex_glUniform1f(U(g_progAO, "uUseG"), ef.on ? 1.f : 0.f);
        if (ef.on) {
            bindTex(4, g_texG);
            ex_glUniform1i(U(g_progAO, "uGBuf"), 4);
        }
        quad();
    }

    // 3. bloom: bright pass at quarter size, then two blur passes
    const int bw = std::max(1, w / 4), bh = std::max(1, h / 4);
    if (L.bloom > 0) {
        ex_glBindFramebuffer(GL_FRAMEBUFFER, g_fboBloomA);
        glViewport(0, 0, bw, bh);
        common(g_progBright, proj, w, h);
        bindTex(0, g_texColor);
        ex_glUniform1i(U(g_progBright, "uColor"), 0);
        ex_glUniform1f(U(g_progBright, "uThresh"), L.bloomThreshold);
        ex_glUniform2f(U(g_progBright, "uSrcTexel"), 1.f / w, 1.f / h);
        quad();
        common(g_progBlur, proj, w, h);
        ex_glUniform1i(U(g_progBlur, "uSrc"), 0);
        for (int pass = 0; pass < 2; pass++) {
            const float spread = pass == 0 ? 1.f : 2.5f;
            ex_glBindFramebuffer(GL_FRAMEBUFFER, g_fboBloomB);
            bindTex(0, g_texBloomA);
            ex_glUniform2f(U(g_progBlur, "uDir"), spread / bw, 0);
            quad();
            ex_glBindFramebuffer(GL_FRAMEBUFFER, g_fboBloomA);
            bindTex(0, g_texBloomB);
            ex_glUniform2f(U(g_progBlur, "uDir"), 0, spread / bh);
            quad();
        }
    }

    // fire lights -> eye space
    static std::vector<FireLight> fires;
    collectFires(fires);
    float lp[16 * 4], lc[16 * 4];
    const float t = SDL_GetTicks() / 1000.f;
    for (size_t i = 0; i < fires.size(); i++) {
        const XYZ& p = fires[i].pos;
        lp[i * 4 + 0] = mv[0] * p.x + mv[4] * p.y + mv[8] * p.z + mv[12];
        lp[i * 4 + 1] = mv[1] * p.x + mv[5] * p.y + mv[9] * p.z + mv[13];
        lp[i * 4 + 2] = mv[2] * p.x + mv[6] * p.y + mv[10] * p.z + mv[14];
        lp[i * 4 + 3] = fires[i].radius;
        const float s = fires[i].seed;
        const float flicker = 0.82f + 0.10f * sinf(t * 11.f + s) + 0.06f * sinf(t * 23.f + s * 2.1f) + 0.04f * sinf(t * 37.f + s * 0.7f);
        const float k = fires[i].intensity * flicker;
        lc[i * 4 + 0] = 1.00f * k;
        lc[i * 4 + 1] = 0.55f * k;
        lc[i * 4 + 2] = 0.20f * k;
        lc[i * 4 + 3] = 1;
    }

    // sun on screen (for shafts)
    float sunUV[3] = { 0, 0, 0 };
    {
        float dx = sunPos[0], dy = sunPos[1], dz = sunPos[2];
        const float len = sqrtf(dx * dx + dy * dy + dz * dz);
        if (len > 0 && sunPos[3] == 0) {
            dx /= len;
            dy /= len;
            dz /= len;
            if (dz < -0.05f) { // in front of the camera
                const float cx = proj[0] * dx, cy = proj[5] * dy, cw = -dz;
                sunUV[0] = (cx / cw) * 0.5f + 0.5f;
                sunUV[1] = (cy / cw) * 0.5f + 0.5f;
                const float edge = std::max(fabsf(sunUV[0] - 0.5f), fabsf(sunUV[1] - 0.5f));
                sunUV[2] = std::max(0.f, std::min(1.f, (1.2f - edge) / 0.7f)) * std::min(1.f, -dz * 2.f);
            }
        }
    }

    // 4. composite back onto the screen (the HUD is drawn on top afterwards)
    ex_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(vx, vy, w, h);
    const GLuint c = g_progComp;
    common(c, proj, w, h);
    bindTex(0, g_texColor);
    bindTex(2, g_texAO);
    bindTex(3, g_texBloomA);
    bindTex(1, g_texDepth);
    ex_glUniform1i(U(c, "uColor"), 0);
    ex_glUniform1i(U(c, "uAO"), 2);
    ex_glUniform1i(U(c, "uBloom"), 3);
    // engine lighting
    ex_glUniform1f(U(c, "uEngine"), ef.on ? 1.f : 0.f);
    if (ef.on) {
        bindTex(4, g_texG);
        bindTex(5, g_shadowTex[0]);
        bindTex(6, g_shadowTex[1]);
        ex_glUniform1i(U(c, "uGBuf"), 4);
        ex_glUniform1i(U(c, "uShadow0"), 5);
        ex_glUniform1i(U(c, "uShadow1"), 6);
        ex_glUniformMatrix4fv(U(c, "uShadowMat0"), 1, GL_FALSE, ef.shadowMat[0]);
        ex_glUniformMatrix4fv(U(c, "uShadowMat1"), 1, GL_FALSE, ef.shadowMat[1]);
        ex_glUniformMatrix4fv(U(c, "uInvView"), 1, GL_FALSE, ef.invView);
        ex_glUniform4f(U(c, "uShadowInfo"), ef.info[0], ef.info[1], ef.info[2], ef.info[3]);
        ex_glUniform3f(U(c, "uSunV"), ef.sunV[0], ef.sunV[1], ef.sunV[2]);
        ex_glUniform3f(U(c, "uLightCol3"), light.color[0], light.color[1], light.color[2]);
        ex_glUniform1f(U(c, "uAmbFrac"), ef.ambFrac);
        ex_glUniform3f(U(c, "uSkyCol"), fogColor[0] * .85f, fogColor[1] * .95f, std::min(1.f, fogColor[2] * 1.15f + .05f));
        if (environment == snowyenvironment) {
            ex_glUniform3f(U(c, "uGroundCol"), .85f, .88f, .92f);
        } else if (environment == desertenvironment) {
            ex_glUniform3f(U(c, "uGroundCol"), .78f, .62f, .42f);
        } else {
            ex_glUniform3f(U(c, "uGroundCol"), .42f, .40f, .27f);
        }
        bindTex(7, g_texBounce);
        ex_glUniform1i(U(c, "uBounce"), 7);
        ex_glUniform1f(U(c, "uLightScale"), std::max(.3f, lumLight()));
        // (the frame the bounce is gathered from has no sun shadows yet, and the game's flat
        // ambient already holds an average bounce: a little over half of it)
        ex_glUniform1f(U(c, "uBounceStr"), .6f);
        ex_glUniform1f(U(c, "uHemi"), .6f);
        ex_glUniform1f(U(c, "uRim"), .5f);
        ex_glUniform1f(U(c, "uSpec"), 1.f);
        ex_glUniform1f(U(c, "uContact"), .8f);
        ex_glUniform1f(U(c, "uVolume"), ef.volume);
        ex_glUniform1f(U(c, "uShadowStr"), ef.shadowStr);
    }
    ex_glUniform1f(U(c, "uAOStr"), L.ao);
    static const int dbg = getenv("LUGARUEX_LIGHT_DEBUG") ? atoi(getenv("LUGARUEX_LIGHT_DEBUG")) : 0;
    ex_glUniform1i(U(c, "uDebug"), dbg);
    ex_glUniform1f(U(c, "uBloomStr"), L.bloom);
    ex_glUniform1f(U(c, "uFire"), L.fire);
    ex_glUniform1f(U(c, "uExposure"), L.exposure);
    ex_glUniform1f(U(c, "uContrast"), L.contrast);
    ex_glUniform1f(U(c, "uSat"), L.saturation);
    ex_glUniform1f(U(c, "uVignette"), L.vignette);
    ex_glUniform1f(U(c, "uFog"), L.fog);
    ex_glUniform1f(U(c, "uShafts"), L.shafts);
    ex_glUniform1f(U(c, "uTonemap"), L.tonemap);
    ex_glUniform3f(U(c, "uTint"), L.tint[0], L.tint[1], L.tint[2]);
    ex_glUniform3f(U(c, "uFogColor"), fogColor[0], fogColor[1], fogColor[2]);
    ex_glUniform3f(U(c, "uSunColor"), 1.0f, 0.92f, 0.75f);
    ex_glUniform3f(U(c, "uSun"), sunUV[0], sunUV[1], sunUV[2]);
    ex_glUniform1i(U(c, "uNumLights"), (int)fires.size());
    if (!fires.empty()) {
        ex_glUniform4fv(U(c, "uLightPos"), (GLsizei)fires.size(), lp);
        ex_glUniform4fv(U(c, "uLightCol"), (GLsizei)fires.size(), lc);
    }
    quad();

    // restore everything the game expects
    ex_glUseProgram(0);
    for (int u = 7; u >= 0; u--) {
        bindTex(u, 0);
    }
    ex_glActiveTexture(GL_TEXTURE0);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
}

void ColorblindApply()
{
    if (!g_progCB || stereomode != stereoNone) {
        return;
    }
    const int w = (int)screenwidth, h = (int)screenheight;
    if (w <= 0 || h <= 0) {
        return;
    }
    if (w != g_cbW || h != g_cbH || !g_cbFbo) {
        if (g_cbFbo) {
            ex_glDeleteFramebuffers(1, &g_cbFbo);
            glDeleteTextures(1, &g_cbTex);
        }
        g_cbTex = makeTex(GL_RGBA8, w, h, GL_RGBA, GL_UNSIGNED_BYTE, false);
        g_cbFbo = makeFbo(g_cbTex, 0);
        g_cbW = w;
        g_cbH = h;
    }
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    // copy (and MSAA-resolve) the finished frame, then draw it back through the filter
    ex_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    ex_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_cbFbo);
    glDisable(GL_SCISSOR_TEST);
    ex_glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    ex_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glColorMask(1, 1, 1, 1);
    ex_glUseProgram(g_progCB);
    bindTex(0, g_cbTex);
    ex_glUniform1i(U(g_progCB, "uColor"), 0);
    ex_glUniform1i(U(g_progCB, "uMode"), std::max(1, std::min(3, cfg.colorblind)));
    quad();
    ex_glUseProgram(0);
    bindTex(0, 0);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
}

void LightingDrawName()
{
    if (!g_nameShownAt || mainmenu || !DrawBottomLeftHud()) {
        return; // split screen: only in the bottom-left view, i.e. the window's bottom-left corner
    }
    const float age = (SDL_GetTicks() - g_nameShownAt) / 1000.f;
    const float hold = 1.6f, fade = 0.9f;
    if (age > hold + fade) {
        g_nameShownAt = 0;
        return;
    }
    const float a = age < hold ? 1.f : 1.f - (age - hold) / fade;
    std::string s = std::string("Lighting: ") + (g_ready ? kLooks[g_look].name : (g_shadersOff ? "Original (shaders off in launcher)" : "Original (shaders unavailable)"));
    const float fullH = SharedHudHeight(); // same size as on one screen, whatever the view size
    const float size = 1.5f * fullH / 768.f;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glEnable(GL_TEXTURE_2D); // the font is a texture; this point of the frame may have it off
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    Game::text->glPrintOutlined(1, 1, 1, a, 20 * fullH / 768.f, 20 * fullH / 768.f, s, 1, size, screenwidth, screenheight);
    glPopAttrib();
}
}
