// LugaruEX launcher - Local tab (split-screen players from connected controllers)
#pragma once
#include "pad.h"

#include <string>

void Local_Register(HINSTANCE inst);
void Local_Init(HWND page, const std::wstring& iniPath);
INT_PTR Local_Proc(HWND page, UINT msg, WPARAM wp, LPARAM lp);
void Local_Tick();
void Local_Save(const std::wstring& iniPath); // writes [Local]
