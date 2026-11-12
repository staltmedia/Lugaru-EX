// LugaruEX launcher - player-made skins ("New skin..." in the skin lists) (GPLv2+)
#pragma once
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

// Imported skins live in <game>\LugaruEX\Data\Textures\Custom\<rabbit|wolf>\<name>.png
// and the game loads them as "Textures/Custom/<model>/<name>.png".
struct CustomSkin
{
    std::string model;      // "rabbit" | "wolf" | "clothes"
    std::string enginePath; // Textures/Custom/rabbit/Name.png
    std::wstring label;     // Name
    std::wstring file;      // full path on disk
};

std::vector<CustomSkin> CustomSkins_List();
// Opens a file picker, converts the chosen image to PNG into the custom folder.
// false if cancelled or the image couldn't be read.
bool CustomSkins_Import(HWND owner, const std::string& model, CustomSkin& out);
