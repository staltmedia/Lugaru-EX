/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef _EX_CONFIG_HPP_
#define _EX_CONFIG_HPP_

#include <string>

#define EX_VERSION_STRING "0.4.7" // also tools/package.ps1, launcher.rc, the launcher label and launcher-sdl/main.cpp

namespace EX
{
enum DisplayMode
{
    DISPLAY_WINDOWED = 0,
    DISPLAY_BORDERLESS = 1,
    DISPLAY_FULLSCREEN = 2
};

struct Config
{
    bool loaded = false; // LugaruEX.ini was found

    // [Display]
    int width = 0; // 0 = desktop size
    int height = 0;
    int displayMode = DISPLAY_WINDOWED;
    int monitor = 0;
    int refreshRate = 0; // 0 = default (exclusive fullscreen only)
    int vsync = 1; // 0 off, 1 on, 2 adaptive (tears below refresh)
    int fpsCap = 0; // 0 = unlimited, -1 = match the display refresh rate
    float fov = 90; // vertical FOV in degrees (original game: 90)
    int msaa = 0;   // 0, 2, 4, 8
    int anisotropy = 0; // 0 (off), 2, 4, 8, 16
    int textureQuality = 1; // 0 = half resolution, 1 = full
    int textureOverrides = 1; // load files from Overrides/ first
    int hudAspect = 1; // keep HUD/menus at 4:3 proportions on wide screens
    int colorblind = 0; // colour blind filter on the final image: 0 off, 1 deuteranopia, 2 protanopia, 3 tritanopia
    int lighting = 1;  // lighting look (0 = original); cycled in game with R
    int shaders = 1;   // 0 = lighting looks disabled (Original only)

    // [Engine]
    int simRate = 0; // fixed simulation steps per second, 0 = match monitor refresh
    int interpolation = 1;

    // [Multiplayer] (used by pass C)
    std::string mpMode = "off"; // off | host | join
    int mpPort = 27015;
    std::string mpAddress = "127.0.0.1:27015";
    int mpLan = 0;       // 1 = LAN: the host announces itself, join finds it
    int mpShareMods = 1; // 1 = online: the host sends its mods, guests download and use them

    // [Player] character chosen in the launcher
    int playerDefault = 1;              // 1 = use whatever the level defines
    std::string playerModel = "rabbit"; // rabbit | wolf
    std::string playerSkin;             // e.g. Textures/Fur3.jpg (empty = level default)
    std::string playerClothes; // LugaruEX launcher Clothes: "" / default = the level's own, none, shirt, camo, ...
};

/** Refresh rate (Hz) of the display the game window is on (0 = unknown). */
int RefreshRate();
/** Effective simulation rate after resolving "auto". */
int SimRate();
/** cap the automatic simulation rate (a PC that can't keep up) */
void LimitSimRate(int hz);

extern Config cfg;

/** Directory that holds the running executable (with trailing slash). */
std::string ExeDir();

/** chdir to the game folder so Data/ resolves. */
void ChdirToExe();
/** the game folder (has Data); LugaruEX.exe may sit in its LugaruEX subfolder */
std::string GameDir();
/** the game folder's LugaruEX subfolder: settings, overrides, presets, custom skins */
std::string ExFolder();

/** Load LugaruEX\LugaruEX.ini. Safe to call when it is missing. */
void LoadConfig();

/** Write one value back into LugaruEX.ini, keeping the rest of the file. */
void SetIniValue(const std::string& section, const std::string& key, const std::string& value);

/** Raw ini value (any section/key, case-insensitive) or def. */
std::string IniString(const std::string& section, const std::string& key, const std::string& def = "");

/** Logical HUD width: screen width clamped to 4:3 when hudAspect is on. */
float HudWidth();
/** Horizontal extension (each side) of the 640x480 menu space so it stays 4:3. */
float MenuExtraX();

/** "LugaruEX.exe+0x1234" for a code address (crash reports, matched to the build's .pdb) */
std::string CodeAddress(const void* p);
/** campaign map progress: how many levels of a campaign were beaten in order (every level
    can be played from Level Select; the map only moves on when the next one is beaten).
    Kept in %APPDATA%\LugaruEX\campaign-progress.txt */
int CampaignMapProgress(const std::string& campaign);
void CampaignBeat(const std::string& campaign, int levelIndex);
}

#endif
