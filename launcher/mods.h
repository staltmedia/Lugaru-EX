// LugaruEX launcher - Mods tab: install mod zips (drag and drop or Add mod...), switch
// them on and off, order them, delete them (GPLv2+)
#pragma once
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

void Mods_Init(HWND page, const std::wstring& exDir, const std::wstring& iniPath);
INT_PTR Mods_Proc(HWND page, UINT msg, WPARAM wp, LPARAM lp);
void Mods_Install(const std::vector<std::wstring>& zips); // dropped files (non-zips are ignored)
void Mods_Save(const std::wstring& iniPath);              // [Mods] List
// controller: A switches the selected mod on / off, X asks to delete it
void Mods_PadToggle();
void Mods_PadDelete();
