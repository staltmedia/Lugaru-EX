/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef _EX_LIGHTING_HPP_
#define _EX_LIGHTING_HPP_

namespace EX
{
/**
 * Swappable lighting "looks" applied to the 3D world (not the HUD/menus).
 * Look 0 is the original 2005 renderer, untouched. The others run a shader
 * post-process chain on the rendered frame: ambient occlusion, fire as light
 * (burning characters, flaming weapons, campfires, burning plants), bloom,
 * sun shafts, fog and colour grading. Visual only - never touches gameplay.
 */
void LightingInit();    // once the GL context exists
void LightingCaptureCamera(); // call right after the camera transform is set up
void LightingApply();   // call right after the 3D world is drawn
void LightingCycle();   // next look (bound to R)
void LightingDrawName(); // fading look name, bottom-left; call at the end of the HUD
void ColorblindApply();  // red-green colour-blind filter on the finished frame; call right before presenting
int LightingLook();

/**
 * Enhanced look, engine lighting: each model the game draws is recorded (vertex array,
 * texture, exact matrix) between LightingCaptureCamera and LightingApply. From that the
 * lighting pass draws the sun's shadow maps (characters, weapons and every object, also
 * off screen) and a surface pass (per-pixel normals, material, the game's own per-vertex
 * sun term), then lights the frame: soft sun shadows + contact shadows, per-pixel sun
 * light, rim light on fur, highlights on blades, sky/ground bounce light, light shafts.
 */
enum
{
    LK_NONE = 0,    // not recorded
    LK_CHAR = 1,    // a character's body
    LK_OBJECT = 2,  // box, rock, tree trunk, staff...
    LK_FOLIAGE = 3, // leaves, bushes (alpha-tested, not lit by the game)
    LK_METAL = 4,   // knife, sword
    LK_STATIC = 8,  // flag: drawn by Object::draw (the sun pass draws all objects itself)
};
void LightingDrawKind(int kind); // what the next models drawn are (LK_NONE = not recorded)
void LightingRecordDraw(const float* vArray, int vertices, bool colorLayout, unsigned tex, const unsigned short* idx = nullptr); // Model::draw / drawdifftex (idx: vertices = index count)
bool LightingRecording(); // models drawn now are recorded (Enhanced)
// the next recorded draw's world matrix and whether the game lights it (saves asking the driver)
void LightingNextDraw(const float* world, bool lit);
/** this frame uses real sun shadows: the old blob shadows are not drawn */
bool LightingRealShadows();
}

#endif
