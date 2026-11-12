/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef _EX_GRAPHICS_HPP_
#define _EX_GRAPHICS_HPP_

#include <SDL.h>

namespace EX
{
/** Set GL attributes (MSAA) before the window is created. */
void PreWindowGLAttributes(int msaa);

/**
 * Create the game window according to cfg (windowed / borderless / exclusive
 * fullscreen, monitor, refresh rate, MSAA with fallback). Updates width/height
 * to the actual drawable size. Returns NULL on failure.
 */
SDL_Window* CreateGameWindow(int& width, int& height, bool grabInput, SDL_GLContext* outCtx);

/** Called once the GL context is current: vsync, multisample, aniso query. */
void InitGLExtras();

/** Apply the configured anisotropic filtering to the bound 2D texture. */
void ApplyAnisotropy();

/** glFinish after the swap when vsync is on (steady frame pacing). */
void AfterSwap();

/** Sleep to honour the FPS cap. Call once per rendered frame, after swap. */
void LimitFrameRate();

/** Scale a vertical FOV so the user FOV setting applies but zoom ratios stay. */
float AdjustFov(float fov);
}

#endif
