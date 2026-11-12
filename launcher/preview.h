// LugaruEX launcher - animated 3D character preview (GPLv2+)
#pragma once
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

// Registers the "LugaruEXPreview" window class used by the Character tab.
void Preview_Register(HINSTANCE inst, const std::wstring& gameDir);

// model: "rabbit" or "wolf"; skinFile: full path of the skin image.
// dimmed: draw greyed out (the Default box is ticked).
void Preview_Set(HWND wnd, const std::string& model, const std::wstring& skinFile, bool dimmed);
