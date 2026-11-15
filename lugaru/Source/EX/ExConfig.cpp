/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "EX/ExConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <windows.h>
#else
#include <dlfcn.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

extern float screenwidth, screenheight;

namespace EX
{
Config cfg;

static const char* kIniName = "LugaruEX.ini";
static std::map<std::string, std::string> g_ini; // everything read from the ini

static std::string lowerStr(std::string s)
{
    for (auto& c : s) {
        c = (char)tolower((unsigned char)c);
    }
    return s;
}

std::string IniString(const std::string& section, const std::string& key, const std::string& def)
{
    auto it = g_ini.find(lowerStr(section) + "." + lowerStr(key));
    return it == g_ini.end() ? def : it->second;
}

std::string ExeDir()
{
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH);
    std::string p(buf, n);
    size_t s = p.find_last_of("\\/");
    return (s == std::string::npos) ? std::string(".\\") : p.substr(0, s + 1);
#else
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return "./";
    }
    std::string p(buf, (size_t)n);
    const size_t s = p.find_last_of('/');
    return (s == std::string::npos) ? std::string("./") : p.substr(0, s + 1);
#endif
}


static bool isDirectory(const std::string& p)
{
#ifdef _WIN32
    const DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

// The game folder (the one with Data\). LugaruEX.exe normally sits in <game>\LugaruEX\,
// next to everything else of LugaruEX, so the game folder is then its parent.
std::string GameDir()
{
    static std::string g;
    if (!g.empty()) {
        return g;
    }
    const std::string e = ExeDir();
    g = e;
    // (LugaruEX/Data only holds a few extra textures: look for the game's Data/Maps)
    if (!isDirectory(e + "Data/Maps") && e.size() > 1) {
        const std::string t = e.substr(0, e.size() - 1);
        const size_t s = t.find_last_of("\\/");
        if (s != std::string::npos && isDirectory(t.substr(0, s + 1) + "Data/Maps")) {
            g = t.substr(0, s + 1);
        }
    }
    return g;
}

std::string ExFolder()
{
    return GameDir() + "LugaruEX/";
}

// LugaruEX\LugaruEX.ini (older versions kept it in the game folder itself)
static std::string iniPath()
{
    const std::string a = ExFolder() + "LugaruEX.ini";
    if (std::ifstream(a)) {
        return a;
    }
    const std::string b = GameDir() + "LugaruEX.ini";
    return std::ifstream(b) ? b : a;
}
void ChdirToExe()
{
#ifdef _WIN32
    _chdir(GameDir().c_str());
#else
    if (chdir(GameDir().c_str()) != 0) {
        perror("LugaruEX: chdir to the game folder");
    }
#endif
}

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) {
        return "";
    }
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

void LoadConfig()
{
    std::ifstream f(iniPath());
    if (!f) {
        std::cerr << "LugaruEX: no " << kIniName << ", using defaults" << std::endl;
        return;
    }
    cfg.loaded = true;
    std::map<std::string, std::string>& kv = g_ini; // "section.key" -> value (lower-case keys)
    kv.clear();
    std::string line, section;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line[0] == '[') {
            section = lower(trim(line.substr(1, line.find(']') - 1)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        kv[section + "." + lower(trim(line.substr(0, eq)))] = trim(line.substr(eq + 1));
    }

    auto geti = [&](const char* k, int& out) {
        auto it = kv.find(k);
        if (it != kv.end() && !it->second.empty()) {
            out = atoi(it->second.c_str());
        }
    };
    auto getf = [&](const char* k, float& out) {
        auto it = kv.find(k);
        if (it != kv.end() && !it->second.empty()) {
            out = (float)atof(it->second.c_str());
        }
    };
    auto gets = [&](const char* k, std::string& out) {
        auto it = kv.find(k);
        if (it != kv.end()) {
            out = it->second;
        }
    };

    geti("display.width", cfg.width);
    geti("display.height", cfg.height);
    std::string mode;
    gets("display.mode", mode);
    mode = lower(mode);
    if (mode == "borderless") {
        cfg.displayMode = DISPLAY_BORDERLESS;
    } else if (mode == "fullscreen" || mode == "exclusive") {
        cfg.displayMode = DISPLAY_FULLSCREEN;
    } else if (!mode.empty()) {
        cfg.displayMode = DISPLAY_WINDOWED;
    }
    geti("display.monitor", cfg.monitor);
    geti("display.refreshrate", cfg.refreshRate);
    geti("display.vsync", cfg.vsync);
    geti("display.fpscap", cfg.fpsCap);
    getf("display.fov", cfg.fov);
    geti("display.msaa", cfg.msaa);
    geti("display.anisotropy", cfg.anisotropy);
    geti("display.texturequality", cfg.textureQuality);
    geti("display.textureoverrides", cfg.textureOverrides);
    cfg.hudAspect = 1; // LugaruEX: HUD and menus always keep their proportions (no stretched 4:3 option)
    geti("display.shaders", cfg.shaders);
    geti("display.colorblind", cfg.colorblind);
    geti("engine.simrate", cfg.simRate);
    geti("engine.interpolation", cfg.interpolation);
    gets("multiplayer.mode", cfg.mpMode);
    cfg.mpMode = lower(cfg.mpMode);
    geti("multiplayer.port", cfg.mpPort);
    gets("multiplayer.address", cfg.mpAddress);
    geti("multiplayer.lan", cfg.mpLan);
    geti("multiplayer.sharemods", cfg.mpShareMods);
    gets("multiplayer.name", cfg.mpName);
    gets("multiplayer.servername", cfg.mpServerName);
    gets("multiplayer.hostpassword", cfg.mpHostPassword);
    {
        int use = 1; // the launcher's Password checkbox (the typed password is kept while it is off)
        geti("multiplayer.usepassword", use);
        if (!use) {
            cfg.mpHostPassword.clear();
        }
    }
    if (const char* pw = getenv("LUGARUEX_JOIN_PASSWORD")) {
        cfg.mpJoinPassword = pw; // the launcher's password window (not saved anywhere)
    }
    geti("multiplayer.maxplayers", cfg.mpMaxPlayers);
    cfg.mpMaxPlayers = cfg.mpMaxPlayers <= 0 ? 0 : std::max(2, std::min(250, cfg.mpMaxPlayers)); // 0 = no limit
    geti("multiplayer.guestcharacters", cfg.mpGuestCharacters);
    geti("multiplayer.steam", cfg.mpSteam);
    gets("multiplayer.code", cfg.mpCode);
    geti("multiplayer.listed", cfg.mpListed);
    geti("multiplayer.friendlyfire", cfg.mpFriendlyFire);
    geti("multiplayer.afktimer", cfg.mpAfkTimer);
    cfg.mpAfkTimer = cfg.mpAfkTimer <= 0 ? 0 : std::max(10, std::min(3600, cfg.mpAfkTimer));
    geti("multiplayer.expandedai", cfg.mpExpandedAI);
    geti("player.default", cfg.playerDefault);
    gets("player.model", cfg.playerModel);
    cfg.playerModel = lower(cfg.playerModel);
    gets("player.skin", cfg.playerSkin);
    gets("player.clothes", cfg.playerClothes);
    cfg.playerClothes = lower(cfg.playerClothes);

    // sanity
    cfg.fov = std::max(40.f, std::min(140.f, cfg.fov));
    if (cfg.simRate != 0) {
        cfg.simRate = std::max(30, std::min(500, cfg.simRate));
    }
    if (cfg.msaa != 0 && cfg.msaa != 2 && cfg.msaa != 4 && cfg.msaa != 8 && cfg.msaa != 16) {
        cfg.msaa = 0;
    }
    cfg.anisotropy = std::max(0, std::min(16, cfg.anisotropy));
    if (cfg.fpsCap < -1) {
        cfg.fpsCap = -1;
    }

    std::cout << "LugaruEX: config " << cfg.width << "x" << cfg.height << " mode=" << cfg.displayMode
              << " msaa=" << cfg.msaa << " af=" << cfg.anisotropy << " vsync=" << cfg.vsync
              << " fpscap=" << cfg.fpsCap << " fov=" << cfg.fov << " sim=" << cfg.simRate << "Hz" << std::endl;
}

void SetIniValue(const std::string& section, const std::string& key, const std::string& value)
{
    const std::string path = iniPath();
    std::vector<std::string> lines;
    {
        std::ifstream f(path);
        std::string l;
        while (std::getline(f, l)) {
            if (!l.empty() && l.back() == '\r') {
                l.pop_back();
            }
            lines.push_back(l);
        }
    }
    const std::string wantSec = lower(section), wantKey = lower(key);
    std::string cur;
    int secEnd = -1;
    bool inSec = false, done = false;
    for (size_t i = 0; i < lines.size() && !done; i++) {
        std::string t = trim(lines[i]);
        if (!t.empty() && t[0] == '[') {
            if (inSec) {
                break;
            }
            cur = lower(trim(t.substr(1, t.find(']') - 1)));
            inSec = (cur == wantSec);
            if (inSec) {
                secEnd = (int)i + 1;
            }
            continue;
        }
        if (inSec) {
            if (!t.empty()) {
                secEnd = (int)i + 1;
            }
            size_t eq = t.find('=');
            if (eq != std::string::npos && lower(trim(t.substr(0, eq))) == wantKey) {
                lines[i] = key + "=" + value;
                done = true;
            }
        }
    }
    if (!done) {
        if (secEnd < 0) {
            lines.push_back("");
            lines.push_back("[" + section + "]");
            lines.push_back(key + "=" + value);
        } else {
            lines.insert(lines.begin() + secEnd, key + "=" + value);
        }
    }
    std::ofstream o(path, std::ios::trunc);
    for (auto& l : lines) {
        o << l << "\r\n";
    }
}

float HudWidth()
{
    if (cfg.hudAspect && screenwidth > screenheight * 4.f / 3.f) {
        return screenheight * 4.f / 3.f;
    }
    return screenwidth;
}

float MenuExtraX()
{
    if (!cfg.hudAspect || screenheight <= 0) {
        return 0;
    }
    float virtualW = 480.f * screenwidth / screenheight; // width of the screen in 480-tall units
    return (virtualW > 640.f) ? (virtualW - 640.f) / 2.f : 0.f;
}

std::string CodeAddress(const void* p)
{
    char buf[300];
#ifdef _WIN32
    HMODULE mod = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)p, &mod) && mod) {
        char full[MAX_PATH] = "?";
        GetModuleFileNameA(mod, full, MAX_PATH);
        const char* slash = strrchr(full, '\\');
        snprintf(buf, sizeof(buf), "%s+0x%llX", slash ? slash + 1 : full, (unsigned long long)((uintptr_t)p - (uintptr_t)mod));
        return buf;
    }
#else
    Dl_info di;
    if (dladdr(p, &di) && di.dli_fname) {
        const char* slash = strrchr(di.dli_fname, '/');
        snprintf(buf, sizeof(buf), "%s+0x%llX", slash ? slash + 1 : di.dli_fname, (unsigned long long)((uintptr_t)p - (uintptr_t)di.dli_fbase));
        return buf;
    }
#endif
    snprintf(buf, sizeof(buf), "%p", p);
    return buf;
}
}

// ------------------------------------------------------------------ campaign map progress
#include "Utils/Folders.hpp"

namespace EX
{
static std::map<std::string, int> g_campProgress;
static bool g_campLoaded = false;

static std::string campaignProgressFile()
{
    return Folders::getUserDataPath() + "campaign-progress.txt";
}

static void loadCampaignProgress()
{
    if (g_campLoaded) {
        return;
    }
    g_campLoaded = true;
    std::ifstream f(campaignProgressFile());
    std::string line;
    while (std::getline(f, line)) {
        const size_t eq = line.rfind('=');
        if (eq != std::string::npos && eq > 0) {
            g_campProgress[line.substr(0, eq)] = std::max(0, atoi(line.c_str() + eq + 1));
        }
    }
}

int CampaignMapProgress(const std::string& campaign)
{
    loadCampaignProgress();
    const auto it = g_campProgress.find(campaign);
    return it == g_campProgress.end() ? 0 : it->second;
}

void CampaignBeat(const std::string& campaign, int levelIndex)
{
    loadCampaignProgress();
    int& p = g_campProgress[campaign];
    if (levelIndex != p) {
        printf("LugaruEX: campaign '%s' level %d beaten (map stays at %d: not the next one in order)\n", campaign.c_str(), levelIndex + 1, p);
        return;
    }
    p = levelIndex + 1;
    std::ofstream f(campaignProgressFile());
    for (const auto& kv : g_campProgress) {
        f << kv.first << "=" << kv.second << "\n";
    }
    printf("LugaruEX: campaign '%s' map progress -> %d levels\n", campaign.c_str(), p);
}
}
