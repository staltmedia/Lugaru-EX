/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef _EX_CAPTURE_HPP_
#define _EX_CAPTURE_HPP_

// F12 screenshot, F11 start / stop recording. Both go to LugaruEX/Screenshots & Recording.
// A short notice fades out in the top-right corner; it is drawn after the picture is taken,
// so it never shows up in a screenshot or a video.
// Recording: the GPU converts the frame to NV12 and reads it back without stalling (pixel
// buffers, a frame late); a worker thread encodes. Windows: H.264 MP4 through Media
// Foundation (the graphics card's encoder) with the game's own sound (WASAPI process
// loopback, Windows 10 2004+). Linux: through ffmpeg when it is installed.
namespace EX
{
void CaptureFrame();    // right before presenting a frame (after the colour-blind filter)
void CaptureShutdown(); // at exit: finish a recording so the file is complete
/** console "fps": a frames-per-second number in the top-right corner */
void SetFpsCounter(bool on);
bool FpsCounter();
bool CaptureRecording();
}

#endif
