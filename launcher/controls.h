// LugaruEX launcher - Controls tab (bindings, presets, controller visualizer)
#pragma once
#include "pad.h"

#include <string>

void Controls_Register(HINSTANCE inst);
void Controls_Init(HWND page, const std::wstring& exeDir, const std::wstring& iniPath);
INT_PTR Controls_Proc(HWND page, UINT msg, WPARAM wp, LPARAM lp);
void Controls_Tick();
void Controls_Save(const std::wstring& iniPath); // writes [Keys] and [Pad]
bool Controls_Capturing();      // waiting for a key / button for a binding
bool Controls_CapturingKeys();  // ... for a keyboard key (a controller can only cancel it)
void Controls_CancelCapture();
void Controls_ActivateRow();    // controller "A" on the list: rebind the selected row
