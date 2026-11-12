// LugaruEX launcher - animated 3D character preview: model loading, skinning, drawing (GPLv2+)
// Shared by the Windows launcher (preview.cpp gives it a window) and the SDL launcher.
#pragma once
#include <string>

void PreviewCore_SetDataDir(const std::string& dataDir); // UTF-8, ends with a slash: <game>/Data/
void PreviewCore_SetModel(const std::string& model);     // "rabbit" | "wolf" (loads the rig once per change)
void PreviewCore_SetTexture(const unsigned char* rgba, int w, int h); // top-down RGBA; nullptr = none. Needs the GL context.
void PreviewCore_SetDimmed(bool dimmed);                  // greyed out (Default is ticked)
void PreviewCore_FreeGL();

// input: drag to rotate, wheel to zoom; 3 s after the last input it glides back to the front view
void PreviewCore_DragBegin(int x, int y, unsigned nowMs);
void PreviewCore_DragMove(int x, int y, unsigned nowMs);
void PreviewCore_DragEnd(unsigned nowMs);
bool PreviewCore_Dragging();
void PreviewCore_Wheel(int dir, unsigned nowMs);
void PreviewCore_Turn(float dYaw, float dPitch, unsigned nowMs); // e.g. a controller's right stick

void PreviewCore_Advance(double dt, unsigned nowMs);
// draws into the current GL context: viewport (X, Y, W, H) in GL window coordinates (origin bottom-left)
void PreviewCore_Render(int X, int Y, int W, int H);
