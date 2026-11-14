// LugaruEX launcher - animated 3D character preview: model loading, skinning, drawing (GPLv2+)
// Shared by the Windows launcher (preview.cpp gives it a window) and the SDL launcher.
#pragma once
#include <string>
#include <vector>

void PreviewCore_SetDataDir(const std::string& dataDir); // UTF-8, ends with a slash: <game>/Data/
void PreviewCore_SetModel(const std::string& model);     // "rabbit" | "wolf" (loads the rig once per change)
void PreviewCore_SetTexture(const unsigned char* rgba, int w, int h); // top-down RGBA; nullptr = none. Needs the GL context.
void PreviewCore_SetDimmed(bool dimmed);                  // greyed out (Default is ticked)

// The launcher's Clothes choice, as the engine's ApplyOutfit puts it on: images
// (game-relative "Textures/..." paths) painted over the skin with these tints.
// default / random / none / custom:... give no list (custom images are the caller's).
struct PreviewCloth
{
    std::string tex;
    float r, g, b;
};
std::vector<PreviewCloth> PreviewCore_Outfit(const std::string& key);
// paints one clothes image (top-down RGBA, any size: resampled to the skin's) over the skin
void PreviewCore_PaintCloth(std::vector<unsigned char>& skin, int w, int h, const unsigned char* cloth, int cw, int chh, float r, float g, float b);
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
