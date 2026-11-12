/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef _EX_INTERP_HPP_
#define _EX_INTERP_HPP_

namespace EX
{
/**
 * Render interpolation for the fixed-timestep loop.
 * After every simulation step call InterpCapture(). Before drawing call
 * InterpApply(alpha) (alpha = fraction of the next step already elapsed),
 * which temporarily writes blended positions into the live game objects;
 * call InterpRestore() right after drawing to put the simulation state back.
 */
void InterpCapture();
void InterpApply(float alpha, float pendingYaw, float pendingPitch);
void InterpRestore();
/** Forget history (level load, teleports): the next capture snaps. */
void InterpInvalidate();
}

#endif
