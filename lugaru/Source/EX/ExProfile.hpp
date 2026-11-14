/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef _EX_PROFILE_HPP_
#define _EX_PROFILE_HPP_

// Frame-time breakdown for finding what slow PCs spend their time on. Off unless the
// environment variable LUGARUEX_PROFILE is set; then every 5 seconds the log gets the
// average CPU and GPU milliseconds per frame of each part ("sim", "terrain", ...).
// Each mark ends the part that began at the previous mark.
namespace EX
{
bool ProfOn();
void ProfFrameStart();            // start of a frame (before the simulation steps)
void ProfMark(const char* name);  // the time since the previous mark belongs to 'name'
void ProfFrameEnd();              // after the swap
// switch on from the next frame (a game running slowly: its log then shows why)
void ProfAuto(const char* why);
void SamplerStart();  // test hook LUGARUEX_TEST_SAMPLE (Windows): sampling profiler
void SamplerReport();
}

#define EX_PROF(name)          \
    do {                       \
        if (EX::ProfOn()) {    \
            EX::ProfMark(name); \
        }                      \
    } while (0)

#endif
