/*
LugaruEX launcher - a classic "configure before playing" dialog for Lugaru HD.

Copyright (C) 2026 - LugaruEX contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

The banner is composed at run time from the user's own game files
(Data/Textures/Eyelid.png and Data/Textures/Lugaru.png); no game art is
embedded in this program.
*/

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <wincodec.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <random>
#include <shobjidl.h>

#include "resource.h"
#include "preview.h"
#include "preview_core.h"
#include "../shared/outfits.h"
#include "skins.h"
#include "pad.h"
#include <SDL.h> // controller button numbers (controller navigation)
#include "padviz.h"
#include "controls.h"
#include "customskins.h"
#include "local.h"
#include "mods.h"
#include "../shared/fsutil.h"
#include "../shared/setup.h"
#include "../shared/update.h"
#include "../shared/netinfo.h"
#include "../shared/serverquery.h"
#include "../shared/adminlink.h"
#include "../shared/looks.h"

#pragma comment(lib, "uxtheme.lib")

#define EX_LAUNCHER_VERSION "0.5.9" // also lugaru/Source/EX/ExConfig.hpp, launcher.rc, launcher-sdl and tools/package.ps1
enum
{
    WM_FREEDATA_DONE = WM_APP + 10, // the free game data arrived (installer)
    WM_UPDATE_STATE = WM_APP + 11,  // the update download moved on
    WM_NOGAME = WM_APP + 12         // the installer found no game: ask what to do
};
static void UpdateThread(HWND notify, std::string repo);
static void UpdateDownloadThread(HWND notify, std::string exDir, UpdateInfo info);
static bool ApplyPendingUpdate();
static std::string CurrentVersion();
static UpdateInfo g_updInfo;             // the release the player was asked about
static bool g_updBusy = false;           // asking or downloading: Play waits
static std::atomic<uint64_t> g_updBytes(0);

// ----------------------------------------------------------------------------
// Settings / ini

struct Settings
{
    int width = 0, height = 0; // 0 = native desktop
    std::string mode = "borderless";
    int monitor = -1, refresh = 0; // -1 = pick the fastest monitor
    int vsync = 1, fpscap = -1, fov = 90; // vsync: 0 off, 1 on, 2 adaptive; fpscap -1 = match display
    int msaa = 4, af = 16;
    int texq = 1, overrides = 1;
    int colorblind = 0; // 0 off, 1 deuteranopia, 2 protanopia, 3 tritanopia
    int lighting = 1; // set in game with R (kept, not shown in the launcher)
    int shaders = 1;  // lighting looks on/off
    int simrate = 0, interp = 1; // simrate 0 = match monitor refresh
    std::string mpmode = "off";
    int lan = 0; // LAN: hosts announce themselves, joiners find them automatically
    int port = 27015;
    std::string addr; // join address: empty until the player types the host's
    std::string name = "User";        // shown in server lists and chat
    std::string serverName;           // host: the name in server lists (colour codes ^0-^9)
    std::string hostPassword;
    int usePassword = -1;             // host: the Password box (-1: on when a password was saved before)
    int maxPlayers = 0;               // host: everyone, the host's own players too (0 = no limit)
    int guestChars = 1;               // host: guests choose their own character
    int friendlyFire = 0;             // host: players can hit each other
    int expandedAI = 0;               // host: the AI also uses the players' moves
    int afkTimer = 120;               // host: seconds before the AI plays for an idle player (0 = off)
    int mpAdvanced = 0;               // the Advanced options are open
    int steam = 1;                    // through Steam (codes, Valve's relay) - 0 = direct (IP / LAN)
    std::string code;                 // join through Steam: the host's code
    std::string recent;               // servers joined before: addr|addr|... (server list)
    int chDefault = 0; // new installs: Random (a skin nobody else in the level wears)
    std::string chModel = "rabbit";
    std::string chSkin = "random"; // engine path, e.g. Textures/FurBrown.jpg, or "random"
    std::string chClothes = "default"; // default (the level's own) / none / shirt / camo / ...
    int shareMods = 1;  // online: send this PC's mods when hosting, take the host's when joining
    int autoUpdate = 1; // check GitHub for a new LugaruEX when the launcher starts
    std::string updateRepo; // [Launcher] UpdateRepo: another GitHub repository to update from
    int tab = 0;            // [Launcher] Tab: the tab that was open last time
};


static std::wstring g_exeDir;
static Settings g_set;

// Everything of LugaruEX lives in <game>\LugaruEX\ (this launcher is the game's Lugaru.exe)
static std::wstring g_exDir;
static std::wstring IniPath() { return g_exDir + L"LugaruEX.ini"; }

// LugaruEX 0.2 put its files straight into the game folder: move the settings and
// texture overrides into LugaruEX\ and remove the old copies (never a file of the game itself)
static void TidyOldInstall()
{
    CreateDirectoryW(g_exDir.c_str(), NULL);
    const std::wstring oldIni = g_exeDir + L"LugaruEX.ini";
    if (PathFileExistsW(oldIni.c_str()) && !PathFileExistsW(IniPath().c_str())) {
        MoveFileW(oldIni.c_str(), IniPath().c_str());
    }
    const std::wstring oldOvr = g_exeDir + L"Overrides", newOvr = g_exDir + L"Overrides";
    if (PathIsDirectoryW(oldOvr.c_str())) {
        DeleteFileW((oldOvr + L"\\README.txt").c_str());
        if (!RemoveDirectoryW(oldOvr.c_str()) && !PathIsDirectoryW(newOvr.c_str())) {
            MoveFileW(oldOvr.c_str(), newOvr.c_str()); // textures were put there: keep them
        }
    }
    if (!PathIsDirectoryW(oldOvr.c_str())) {
        CreateDirectoryW(newOvr.c_str(), NULL); // where upscaled texture packs go
    }
    if (!PathFileExistsW((g_exDir + L"LugaruEX.exe").c_str())) {
        return; // a developer build run from elsewhere: leave the folder alone
    }
    static const wchar_t* kOld[] = { L"LugaruEX.exe", L"LugaruEX Launcher.exe", L"LugaruEX - Steam Play button.bat",
                                     L"LugaruEX - Uninstall.bat", L"LugaruEX-restore.bat", L"SDL2.dll", L"OpenAL32.dll",
                                     L"fmt.dll", L"jpeg62.dll", L"jsoncpp.dll", L"libpng16.dll", L"ogg.dll", L"vorbis.dll",
                                     L"vorbisfile.dll", L"z.dll", L"vcruntime140.dll", L"vcruntime140_1.dll", L"msvcp140.dll",
                                     L"msvcp140_atomic_wait.dll" };
    for (const wchar_t* f : kOld) {
        DeleteFileW((g_exeDir + f).c_str());
    }
}

static std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    PathRemoveFileSpecW(buf);
    return std::wstring(buf) + L"\\";
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
    for (auto& c : s) {
        c = (char)tolower((unsigned char)c);
    }
    return s;
}

static std::wstring widen(const std::string& s)
{
    if (s.empty()) {
        return L"";
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string narrow(const std::wstring& w)
{
    if (w.empty()) {
        return "";
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

static void LoadIni(Settings& s)
{
    std::ifstream f(IniPath());
    if (!f) {
        return;
    }
    std::map<std::string, std::string> kv;
    std::string line, sec;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line[0] == '[') {
            sec = lower(trim(line.substr(1, line.find(']') - 1)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            kv[sec + "." + lower(trim(line.substr(0, eq)))] = trim(line.substr(eq + 1));
        }
    }
    auto gi = [&](const char* k, int& v) {
        auto it = kv.find(k);
        if (it != kv.end() && !it->second.empty()) {
            v = atoi(it->second.c_str());
        }
    };
    auto gs = [&](const char* k, std::string& v) {
        auto it = kv.find(k);
        if (it != kv.end()) {
            v = it->second;
        }
    };
    gi("display.width", s.width);
    gi("display.height", s.height);
    gs("display.mode", s.mode);
    s.mode = lower(s.mode);
    gi("display.monitor", s.monitor);
    gi("display.refreshrate", s.refresh);
    gi("display.vsync", s.vsync);
    gi("display.fpscap", s.fpscap);
    gi("display.fov", s.fov);
    gi("display.msaa", s.msaa);
    gi("display.anisotropy", s.af);
    gi("display.texturequality", s.texq);
    gi("display.textureoverrides", s.overrides);
    gi("display.colorblind", s.colorblind);
    gi("display.lighting", s.lighting);
    gi("display.shaders", s.shaders);
    gi("engine.simrate", s.simrate);
    gi("engine.interpolation", s.interp);
    gs("multiplayer.mode", s.mpmode);
    s.mpmode = lower(s.mpmode);
    gi("multiplayer.port", s.port);
    gi("multiplayer.lan", s.lan);
    gs("multiplayer.address", s.addr);
    gs("multiplayer.name", s.name);
    gs("multiplayer.servername", s.serverName);
    gs("multiplayer.hostpassword", s.hostPassword);
    gi("multiplayer.usepassword", s.usePassword);
    if (s.usePassword < 0) {
        s.usePassword = s.hostPassword.empty() ? 0 : 1;
    }
    gi("multiplayer.maxplayers", s.maxPlayers);
    gi("multiplayer.guestcharacters", s.guestChars);
    gi("multiplayer.friendlyfire", s.friendlyFire);
    gi("multiplayer.expandedai", s.expandedAI);
    gi("multiplayer.afktimer", s.afkTimer);
    gi("multiplayer.advanced", s.mpAdvanced);
    gi("multiplayer.steam", s.steam);
    gs("multiplayer.code", s.code);
    gs("multiplayer.recent", s.recent);
    gi("player.default", s.chDefault);
    gs("player.model", s.chModel);
    s.chModel = lower(s.chModel);
    gs("player.skin", s.chSkin);
    gs("player.clothes", s.chClothes);
    gi("multiplayer.sharemods", s.shareMods);
    gi("launcher.autoupdate", s.autoUpdate);
    gs("launcher.updaterepo", s.updateRepo);
    gi("launcher.tab", s.tab);
}

static bool SaveIni(const Settings& s)
{
    std::ofstream o(IniPath(), std::ios::trunc | std::ios::binary);
    if (!o) {
        return false;
    }
    o << "; LugaruEX settings - written by the launcher (Lugaru.exe), read by LugaruEX.exe\r\n"
      << "[Display]\r\n"
      << "; Width/Height 0 = native desktop resolution\r\n"
      << "Width=" << s.width << "\r\n"
      << "Height=" << s.height << "\r\n"
      << "; windowed | borderless | fullscreen\r\n"
      << "Mode=" << s.mode << "\r\n"
      << "Monitor=" << s.monitor << "\r\n"
      << "RefreshRate=" << s.refresh << "\r\n"
      << "; 0 off, 1 on (no tearing), 2 adaptive (tears below refresh)\r\n"
      << "VSync=" << s.vsync << "\r\n"
      << "; -1 = match the display refresh rate, 0 = unlimited\r\n"
      << "FPSCap=" << s.fpscap << "\r\n"
      << "; vertical field of view in degrees (original game: 90)\r\n"
      << "FOV=" << s.fov << "\r\n"
      << "MSAA=" << s.msaa << "\r\n"
      << "Anisotropy=" << s.af << "\r\n"
      << "; 0 = half size, 1 = full size\r\n"
      << "TextureQuality=" << s.texq << "\r\n"
      << "TextureOverrides=" << s.overrides << "\r\n"
      << "; colour blind filter: 0 off, 1 deuteranopia, 2 protanopia, 3 tritanopia\r\n"
      << "Colorblind=" << s.colorblind << "\r\n"
      << "; 0 = shaders off (Original look only)\r\n"
      << "Shaders=" << s.shaders << "\r\n"
      << "\r\n[Engine]\r\n"
      << "; fixed simulation rate (steps per second, 0 = match the monitor) and render interpolation\r\n"
      << "SimRate=" << s.simrate << "\r\n"
      << "Interpolation=" << s.interp << "\r\n"
      << "\r\n[Multiplayer]\r\n"
      << "; off | host | join\r\n"
      << "Mode=" << s.mpmode << "\r\n"
      << "Port=" << s.port << "\r\n"
      << "Address=" << s.addr << "\r\n"
      << "; 1 = LAN: the host announces itself on the local network, Join finds it automatically\r\n"
      << "LAN=" << s.lan << "\r\n"
      << "; 1 = share mods online: send yours when hosting, download the host's when joining\r\n"
      << "ShareMods=" << s.shareMods << "\r\n"
      << "; your name (server lists, chat)\r\n"
      << "Name=" << s.name << "\r\n"
      << "; host: the name in server lists (^0-^9 colour the text after them)\r\n"
      << "ServerName=" << s.serverName << "\r\n"
      << "HostPassword=" << s.hostPassword << "\r\n"
      << "; 0 = the server has no password (HostPassword is kept for later)\r\n"
      << "UsePassword=" << s.usePassword << "\r\n"
      << "; host: players at most (yours included, 0 = no limit), and whether guests choose their character\r\n"
      << "MaxPlayers=" << s.maxPlayers << "\r\n"
      << "; host: players can hit each other, the AI uses the players' moves too, seconds before the AI plays for an idle player (0 = off)\r\n"
      << "FriendlyFire=" << s.friendlyFire << "\r\n"
      << "ExpandedAI=" << s.expandedAI << "\r\n"
      << "AfkTimer=" << s.afkTimer << "\r\n"
      << "GuestCharacters=" << s.guestChars << "\r\n"
      << "Advanced=" << s.mpAdvanced << "\r\n"
      << "; 1 = play through Steam (Steam server list, Valve's relay: nobody sees anyone's IP address); 0 = direct (IP / LAN)\r\n"
      << "Steam=" << s.steam << "\r\n"
      << "Listed=1\r\n"
      << "Code=" << s.code << "\r\n"
      << "; servers joined before (the launcher's server list asks them)\r\n"
      << "Recent=" << s.recent << "\r\n"
      << "\r\n[Player]\r\n"
      << "; Default=1 keeps the character each level defines\r\n"
      << "Default=" << s.chDefault << "\r\n"
      << "; rabbit | wolf\r\n"
      << "Model=" << s.chModel << "\r\n"
      << "Skin=" << s.chSkin << "\r\n"
      << "; default (the level's own) | none | shirt | camo | snowcamo | desertcamo | leather | gi\r\n"
      << "Clothes=" << s.chClothes << "\r\n"
      << "\r\n[Launcher]\r\n"
      << "; 1 = look for a new LugaruEX on GitHub when the launcher starts, and offer to install it\r\n"
      << "AutoUpdate=" << s.autoUpdate << "\r\n"
      << "Tab=" << s.tab << "\r\n"
      << (s.updateRepo.empty() ? std::string() : "UpdateRepo=" + s.updateRepo + "\r\n");
    return o.good();
}

// ----------------------------------------------------------------------------
// Monitors and modes (same order as SDL2: primary first, then the others)

struct Monitor
{
    std::wstring device; // \\.\DISPLAY1
    std::wstring label;
    int curW = 0, curH = 0, curHz = 0;
    std::vector<std::pair<int, int>> res; // sorted, unique
    std::map<std::pair<int, int>, std::set<int>> rates;
};

static std::vector<Monitor> g_mons;

static void EnumMonitors()
{
    g_mons.clear();
    for (int pass = 0; pass < 2; pass++) {
        DISPLAY_DEVICEW dd = { sizeof(dd) };
        for (DWORD i = 0; EnumDisplayDevicesW(NULL, i, &dd, 0); i++, dd.cb = sizeof(dd)) {
            if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) {
                continue;
            }
            const bool primary = (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;
            if ((pass == 0) != primary) {
                continue;
            }
            Monitor m;
            m.device = dd.DeviceName;
            DISPLAY_DEVICEW md = { sizeof(md) };
            std::wstring friendly = dd.DeviceString;
            if (EnumDisplayDevicesW(dd.DeviceName, 0, &md, 0) && md.DeviceString[0]) {
                friendly = md.DeviceString;
            }
            DEVMODEW dm = { 0 };
            dm.dmSize = sizeof(dm);
            if (EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
                m.curW = dm.dmPelsWidth;
                m.curH = dm.dmPelsHeight;
                m.curHz = dm.dmDisplayFrequency;
            }
            std::set<std::pair<int, int>> uniq;
            for (DWORD k = 0;; k++) {
                DEVMODEW d = { 0 };
                d.dmSize = sizeof(d);
                if (!EnumDisplaySettingsW(dd.DeviceName, k, &d)) {
                    break;
                }
                if ((int)d.dmPelsWidth < 640 || (int)d.dmPelsHeight < 480) {
                    continue;
                }
                auto key = std::make_pair((int)d.dmPelsWidth, (int)d.dmPelsHeight);
                uniq.insert(key);
                if (d.dmDisplayFrequency > 1) {
                    m.rates[key].insert((int)d.dmDisplayFrequency);
                }
            }
            if (m.curW) {
                uniq.insert({ m.curW, m.curH });
            }
            m.res.assign(uniq.begin(), uniq.end());
            std::sort(m.res.begin(), m.res.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                return a.first != b.first ? a.first > b.first : a.second > b.second;
            });
            wchar_t lbl[256];
            swprintf(lbl, 256, L"%d: %s  (%dx%d @ %d Hz)", (int)g_mons.size() + 1, friendly.c_str(), m.curW, m.curH, m.curHz);
            m.label = lbl;
            g_mons.push_back(m);
        }
    }
    if (g_mons.empty()) {
        Monitor m;
        m.label = L"1: Default display";
        m.curW = GetSystemMetrics(SM_CXSCREEN);
        m.curH = GetSystemMetrics(SM_CYSCREEN);
        m.res = { { m.curW, m.curH }, { 1920, 1080 }, { 1280, 720 }, { 1024, 768 } };
        g_mons.push_back(m);
    }
}

static std::wstring AspectName(int w, int h)
{
    if (w <= 0 || h <= 0) {
        return L"";
    }
    const double r = (double)w / h;
    struct A
    {
        double r;
        const wchar_t* n;
    } known[] = { { 4.0 / 3, L"4:3" }, { 5.0 / 4, L"5:4" }, { 3.0 / 2, L"3:2" }, { 16.0 / 10, L"16:10" }, { 16.0 / 9, L"16:9" },
                  { 64.0 / 27, L"21:9" }, { 43.0 / 18, L"21:9" }, { 12.0 / 5, L"21:9" }, { 32.0 / 9, L"32:9" }, { 32.0 / 10, L"32:10" } };
    for (auto& a : known) {
        if (fabs(r - a.r) < 0.012) {
            return a.n;
        }
    }
    wchar_t b[32];
    swprintf(b, 32, L"%.2f:1", r);
    return b;
}

// ----------------------------------------------------------------------------
// Banner (WIC image loading, composed with GDI)

struct Image
{
    int w = 0, h = 0;
    std::vector<BYTE> px; // 32bpp premultiplied BGRA, top-down
};

static bool LoadImageWIC(const std::wstring& path, Image& img)
{
    IWICImagingFactory* fac = NULL;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac)))) {
        return false;
    }
    bool ok = false;
    IWICBitmapDecoder* dec = NULL;
    IWICBitmapFrameDecode* frame = NULL;
    IWICFormatConverter* conv = NULL;
    if (SUCCEEDED(fac->CreateDecoderFromFilename(path.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(fac->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) {
        UINT w, h;
        conv->GetSize(&w, &h);
        img.w = w;
        img.h = h;
        img.px.resize((size_t)w * h * 4);
        ok = SUCCEEDED(conv->CopyPixels(NULL, w * 4, (UINT)img.px.size(), img.px.data()));
    }
    if (conv) {
        conv->Release();
    }
    if (frame) {
        frame->Release();
    }
    if (dec) {
        dec->Release();
    }
    fac->Release();
    return ok;
}

static HBITMAP MakeDib(HDC dc, int w, int h, void** bits)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(dc, &bi, DIB_RGB_COLORS, bits, NULL, 0);
}

static HBITMAP ImageToBitmap(HDC dc, const Image& img)
{
    void* bits = NULL;
    HBITMAP b = MakeDib(dc, img.w, img.h, &bits);
    if (b && bits) {
        memcpy(bits, img.px.data(), img.px.size());
    }
    return b;
}

static bool SavePNG(const std::wstring& path, const Image& img)
{
    IWICImagingFactory* fac = NULL;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac)))) {
        return false;
    }
    bool ok = false;
    IWICBitmap* bmp = NULL;
    IWICStream* stream = NULL;
    IWICBitmapEncoder* enc = NULL;
    IWICBitmapFrameEncode* frame = NULL;
    if (SUCCEEDED(fac->CreateBitmapFromMemory(img.w, img.h, GUID_WICPixelFormat32bppBGRA, img.w * 4, (UINT)img.px.size(), (BYTE*)img.px.data(), &bmp)) &&
        SUCCEEDED(fac->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(fac->CreateEncoder(GUID_ContainerFormatPng, NULL, &enc)) && SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(enc->CreateNewFrame(&frame, NULL)) && SUCCEEDED(frame->Initialize(NULL))) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        ok = SUCCEEDED(frame->SetSize(img.w, img.h)) && SUCCEEDED(frame->SetPixelFormat(&fmt)) && SUCCEEDED(frame->WriteSource(bmp, NULL)) &&
             SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
    }
    for (IUnknown* u : { (IUnknown*)frame, (IUnknown*)enc, (IUnknown*)stream, (IUnknown*)bmp, (IUnknown*)fac }) {
        if (u) {
            u->Release();
        }
    }
    return ok;
}

// "EX" in the style of the LUGARU letters: a classical capital (Sylfaen - the closest
// Trajan-like face every Windows has), thickened to LUGARU's stroke weight, with its
// deep-red-to-orange gradient (sampled row by row from the user's logo), a sculpted
// bevel lit from above, the glowing orange lip on the bottom edges, a soft sheen and
// a soft drop shadow. capH = the letters' height in pixels (serifs included); `put`
// blends a pixel at (x, y) = glyph top-left + offset. outW/outH: the glyphs' size.
static std::wstring ExFontFace(HDC dc)
{
    static std::wstring face;
    if (!face.empty()) {
        return face;
    }
    for (const wchar_t* f : { L"Trajan Pro 3", L"Trajan Pro", L"Perpetua Titling MT", L"Sylfaen", L"Palatino Linotype" }) {
        HFONT h = CreateFontW(-40, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, VARIABLE_PITCH, f);
        HGDIOBJ o = SelectObject(dc, h);
        wchar_t got[64] = {};
        GetTextFaceW(dc, 64, got);
        SelectObject(dc, o);
        DeleteObject(h);
        if (_wcsicmp(got, f) == 0) {
            face = f;
            return face;
        }
    }
    face = L"Times New Roman";
    return face;
}

// coverage (0..1) of "EX" at a font height, 4x supersampled, cropped to the glyphs plus
// `pad` pixels; strokes thickened to stemRatio x the glyph height
struct ExMask
{
    int w = 0, h = 0, gw = 0, gh = 0, pad = 0;
    std::vector<float> a;
    float at(int x, int y) const { return (x < 0 || y < 0 || x >= w || y >= h) ? 0.f : a[(size_t)y * w + x]; }
};

// LUGARU is set in small capitals: the L (and the last U) tall, U-G-A-R shorter on the same
// baseline (85 vs 101 pixels in the HD logo, like its own H and D). "EX" follows it: a
// capital E and a small-capital X this much of the E's height.
static const float g_exSmallCaps = 0.84f;

static ExMask RenderExMask(HDC screen, float fontPx, int pad, float stemRatio)
{
    const int SS = 4;
    const int MW = (int)(fontPx * 2.4f * SS) + 16, MH = (int)(fontPx * 1.6f * SS) + 16;
    void* mbits = NULL;
    HBITMAP mbmp = MakeDib(screen, MW, MH, &mbits);
    HDC mdc = CreateCompatibleDC(screen);
    HGDIOBJ mo = SelectObject(mdc, mbmp);
    memset(mbits, 0, (size_t)MW * MH * 4);
    auto font = [&](float px) {
        return CreateFontW(-(int)(px * SS), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, VARIABLE_PITCH, ExFontFace(screen).c_str());
    };
    HFONT ef = font(fontPx), xf = font(fontPx * g_exSmallCaps);
    HGDIOBJ eo = SelectObject(mdc, ef);
    SetBkMode(mdc, TRANSPARENT);
    SetTextColor(mdc, RGB(255, 255, 255));
    SetTextAlign(mdc, TA_BASELINE | TA_LEFT);
    TEXTMETRICW tm = {};
    GetTextMetricsW(mdc, &tm);
    const int baseline = 8 + tm.tmAscent; // both letters stand on it
    SIZE e = {};
    GetTextExtentPoint32W(mdc, L"E", 1, &e);
    TextOutW(mdc, 8 * SS, baseline, L"E", 1);
    SelectObject(mdc, xf);
    TextOutW(mdc, 8 * SS + e.cx, baseline, L"X", 1);
    GdiFlush();
    std::vector<BYTE> big((size_t)MW * MH);
    for (size_t i = 0; i < big.size(); i++) {
        big[i] = ((const BYTE*)mbits)[i * 4 + 1];
    }
    SelectObject(mdc, eo);
    DeleteObject(ef);
    DeleteObject(xf);
    SelectObject(mdc, mo);
    DeleteObject(mbmp);
    DeleteDC(mdc);

    int x0 = MW, x1 = -1, y0 = MH, y1 = -1;
    for (int y = 0; y < MH; y++) {
        for (int x = 0; x < MW; x++) {
            if (big[(size_t)y * MW + x] > 100) {
                x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
            }
        }
    }
    ExMask m;
    if (x1 < x0) {
        return m;
    }
    // thicken: measure the E's stem (first run in the middle row) and grow every stroke
    int stem = 0;
    for (int x = x0, ym = (y0 + y1) / 2; x <= x1; x++) {
        if (big[(size_t)ym * MW + x] > 127) {
            stem++;
        } else if (stem) {
            break;
        }
    }
    const int grow = std::max(0, (int)lroundf((stemRatio * (y1 - y0 + 1) - stem) / 2));
    if (grow > 0) {
        std::vector<BYTE> tmp(big.size());
        for (int y = 0; y < MH; y++) { // horizontal max
            for (int x = 0; x < MW; x++) {
                BYTE v = 0;
                for (int k = std::max(0, x - grow); k <= std::min(MW - 1, x + grow); k++) {
                    v = std::max(v, big[(size_t)y * MW + k]);
                }
                tmp[(size_t)y * MW + x] = v;
            }
        }
        for (int y = 0; y < MH; y++) { // vertical max
            for (int x = 0; x < MW; x++) {
                BYTE v = 0;
                for (int k = std::max(0, y - grow); k <= std::min(MH - 1, y + grow); k++) {
                    v = std::max(v, tmp[(size_t)k * MW + x]);
                }
                big[(size_t)y * MW + x] = v;
            }
        }
        x0 = std::max(0, x0 - grow), x1 = std::min(MW - 1, x1 + grow), y0 = std::max(0, y0 - grow), y1 = std::min(MH - 1, y1 + grow);
    }
    // down to 1x, glyph box from x0/y0, plus the pad
    m.pad = pad;
    m.gw = (x1 - x0 + SS) / SS;
    m.gh = (y1 - y0 + SS) / SS;
    m.w = m.gw + 2 * pad;
    m.h = m.gh + 2 * pad;
    m.a.assign((size_t)m.w * m.h, 0.f);
    for (int y = 0; y < m.gh; y++) {
        for (int x = 0; x < m.gw; x++) {
            int s = 0;
            for (int sy = 0; sy < SS; sy++) {
                for (int sx = 0; sx < SS; sx++) {
                    const int bx = x0 + x * SS + sx, by = y0 + y * SS + sy;
                    s += (bx < MW && by < MH) ? big[(size_t)by * MW + bx] : 0;
                }
            }
            m.a[(size_t)(y + pad) * m.w + (x + pad)] = s / (255.f * SS * SS);
        }
    }
    return m;
}

static std::vector<float> BoxBlur(const std::vector<float>& src, int w, int h, int r)
{
    std::vector<float> a = src, b(src.size());
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float s = 0;
                for (int k = -r; k <= r; k++) {
                    const int xx = std::max(0, std::min(w - 1, x + k));
                    s += a[(size_t)y * w + xx];
                }
                b[(size_t)y * w + x] = s / (2 * r + 1);
            }
        }
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                float s = 0;
                for (int k = -r; k <= r; k++) {
                    const int yy = std::max(0, std::min(h - 1, y + k));
                    s += b[(size_t)yy * w + x];
                }
                a[(size_t)y * w + x] = s / (2 * r + 1);
            }
        }
    }
    return a;
}

template <typename Put>
static void PaintEX(HDC screen, int capH, const float (*grad)[3], int x, int y, Put put, int* outW = nullptr, int* outH = nullptr)
{
    capH = std::max(8, capH);
    // size the font by the letters actually drawn (faces differ in cap height)
    float fontPx = capH / 0.66f;
    ExMask probe = RenderExMask(screen, fontPx, 0, 0.f);
    if (probe.gh > 0) {
        fontPx *= (float)capH / probe.gh;
    }
    const int pad = std::max(3, capH / 8);
    ExMask m = RenderExMask(screen, fontPx, pad, 0.105f); // LUGARU's stems are ~1/10 of its height
    if (m.gh <= 0) {
        return;
    }
    if (outW) {
        *outW = m.gw;
    }
    if (outH) {
        *outH = m.gh;
    }
    // soft drop shadow, down and to the right
    const int sr = std::max(1, capH / 30);
    const std::vector<float> shadow = BoxBlur(m.a, m.w, m.h, sr);
    const int so = std::max(1, capH / 28);
    for (int my = 0; my < m.h; my++) {
        for (int mx = 0; mx < m.w; mx++) {
            const float a = shadow[(size_t)my * m.w + mx];
            if (a > 0.004f) {
                put(x + mx - m.pad + so, y + my - m.pad + so, 0.f, 0.f, 0.f, a * 0.75f);
            }
        }
    }
    // bevel: a height field from the softened coverage, lit as a surface
    const int br = std::max(1, (int)lroundf(capH * 0.03f));
    const std::vector<float> hgt = BoxBlur(m.a, m.w, m.h, br);
    auto H = [&](int px, int py) { return (px < 0 || py < 0 || px >= m.w || py >= m.h) ? 0.f : hgt[(size_t)py * m.w + px]; };
    const float k = capH * 0.07f;                  // bevel steepness
    const float L[3] = { -0.28f, -0.78f, 0.56f }; // key light from above, a little left (y down)
    for (int my = 0; my < m.h; my++) {
        const int gy = my - m.pad;
        const int t = std::max(0, std::min(63, gy * 64 / std::max(1, m.gh)));
        const float* c = grad ? grad[t] : nullptr;
        const float base[3] = { c ? c[0] : 225.f, c ? c[1] : 20.f + 40.f * t / 64.f, c ? c[2] : 2.f };
        for (int mx = 0; mx < m.w; mx++) {
            const float a = m.at(mx, my);
            if (a <= 0.004f) {
                continue;
            }
            float nx = -(H(mx + 1, my) - H(mx - 1, my)) * k, ny = -(H(mx, my + 1) - H(mx, my - 1)) * k, nz = 1.f;
            const float nl = sqrtf(nx * nx + ny * ny + nz * nz);
            nx /= nl, ny /= nl, nz /= nl;
            const float diff = nx * L[0] + ny * L[1] + nz * L[2];
            float r = base[0] * (0.74f + 0.36f * diff), g = base[1] * (0.70f + 0.42f * diff), b = base[2] * (0.7f + 0.4f * diff);
            // the LUGARU letters glow orange along their bottom edges
            const float lip = std::min(1.f, powf(std::max(0.f, ny), 1.5f) * 0.95f + 0.2f * std::max(0.f, (float)gy / m.gh - 0.88f) / 0.12f);
            r = r + (255.f - r) * lip;
            g = g + (150.f - g) * lip;
            b = b + (40.f - b) * lip;
            // a soft sheen where the bevel faces the key light
            const float hx = L[0], hy = L[1], hz = L[2] + 1.f, hl = sqrtf(hx * hx + hy * hy + hz * hz);
            const float spec = powf(std::max(0.f, (nx * hx + ny * hy + nz * hz) / hl), 40.f) * 0.32f;
            r += 255.f * spec, g += 200.f * spec, b += 140.f * spec;
            put(x + mx - m.pad, y + my - m.pad, std::min(255.f, r), std::min(255.f, g), std::min(255.f, b), std::min(1.f, a));
        }
    }
}

static HBITMAP g_banner = NULL;
static bool g_setupMode = false; // running as the installer
static int g_bannerW = 0, g_bannerH = 0;

static void BuildBanner(HWND ctl)
{
    RECT rc;
    GetClientRect(ctl, &rc);
    const int W = rc.right, H = rc.bottom;
    HDC screen = GetDC(ctl);
    HDC dc = CreateCompatibleDC(screen);
    void* bits = NULL;
    g_banner = MakeDib(screen, W, H, &bits);
    g_bannerW = W;
    g_bannerH = H;
    HGDIOBJ old = SelectObject(dc, g_banner);

    // dark red-black gradient fallback
    for (int y = 0; y < H; y++) {
        BYTE* row = (BYTE*)bits + (size_t)y * W * 4;
        for (int x = 0; x < W; x++) {
            row[x * 4 + 0] = 8;
            row[x * 4 + 1] = 8;
            row[x * 4 + 2] = (BYTE)(20 + 30 * y / H);
            row[x * 4 + 3] = 255;
        }
    }

    HDC src = CreateCompatibleDC(screen);
    Image eye, logo;
    const std::wstring tex = g_exeDir + L"Data\\Textures\\";
    if (LoadImageWIC(tex + L"Eyelid.png", eye) && eye.w > 0) {
        HBITMAP b = ImageToBitmap(screen, eye);
        HGDIOBJ o = SelectObject(src, b);
        // scale to cover, centred
        const double s = std::max((double)W / eye.w, (double)H / eye.h);
        const int sw = (int)(W / s), sh = (int)(H / s);
        SetStretchBltMode(dc, HALFTONE);
        StretchBlt(dc, 0, 0, W, H, src, (eye.w - sw) / 2, (eye.h - sh) / 2, sw, sh, SRCCOPY);
        SelectObject(src, o);
        DeleteObject(b);
    }
    if (LoadImageWIC(tex + L"Lugaru.png", logo) && logo.w > 0) {
        // The logo is drawn on black without an alpha channel: derive alpha from
        // brightness (colours are then already premultiplied over black).
        bool opaque = true;
        for (size_t i = 3; i < logo.px.size(); i += 4) {
            if (logo.px[i] != 255) {
                opaque = false;
                break;
            }
        }
        if (opaque) {
            for (size_t i = 0; i < logo.px.size(); i += 4) {
                BYTE* p = &logo.px[i];
                p[3] = std::max(p[0], std::max(p[1], p[2]));
            }
        }
        // Find the red "LUGARU" letters and the cream "HD" in the user's logo.
        // The HD is erased and "EX" takes its place, painted with the LUGARU
        // letters' own colours (sampled row by row) so it matches the logo.
        auto px = [&](int x, int y) { return &logo.px[((size_t)y * logo.w + x) * 4]; };
        int hdL = logo.w, hdR = 0, hdT = logo.h, hdB = 0;
        for (int y = 0; y < logo.h; y++) {
            for (int x = 0; x < logo.w; x++) {
                const BYTE* p = px(x, y);
                if (p[2] > 160 && p[1] > 130 && p[0] > 90) { // cream-white
                    hdL = std::min(hdL, x), hdR = std::max(hdR, x), hdT = std::min(hdT, y), hdB = std::max(hdB, y);
                }
            }
        }
        // LUGARU: red-to-orange letter pixels left of the HD (so the HD's orange
        // foot doesn't count); redB = the letters' baseline
        int redT = logo.h, redB = 0, redL = logo.w, redR = 0;
        auto letter = [](const BYTE* p) { return p[2] > 90 && p[0] < p[2] * 0.45f && p[1] < p[2] * 0.9f; };
        for (int y = 0; y < logo.h; y++) {
            for (int x = 0; x < logo.w; x++) {
                const BYTE* p = px(x, y);
                if (letter(p)) {
                    if (x < hdL - 2) {
                        redT = std::min(redT, y), redB = std::max(redB, y), redL = std::min(redL, x);
                    }
                    if (y < hdT) {
                        redR = std::max(redR, x);
                    }
                }
            }
        }
        const bool found = redB > redT && hdR > hdL && hdB > hdT;
        if (found) {
            // Erase the HD. It is drawn over the LUGARU letters: where it covered them (the
            // R's leg crossing into the U) they are rebuilt from the letter on both sides,
            // and the R's tail below the baseline stays. Zone = the HD and a margin.
            const int zx0 = std::max(0, hdL - 10), zx1 = std::min(logo.w - 1, hdR + 6), zy0 = std::max(0, hdT - 3);
            auto inZone = [&](int x, int y) { return x >= zx0 && x <= zx1 && y >= zy0 && y < logo.h; };
            auto lum = [&](int x, int y) { const BYTE* p = px(x, y); return (int)std::max(p[0], std::max(p[1], p[2])); };
            // 1. the HD's own colours: cream / white / grey, and its orange-tinted parts
            //    (LUGARU's orange has almost no blue - under 15% of its red - the HD's tint has more)
            std::vector<char> erased((size_t)logo.w * logo.h, 0);
            for (int y = zy0; y < logo.h; y++) {
                for (int x = zx0; x <= zx1; x++) {
                    BYTE* p = px(x, y);
                    const int mx = std::max(p[0], std::max(p[1], p[2])), mn = std::min(p[0], std::min(p[1], p[2]));
                    if (mx > 12 && (p[0] > p[2] * 0.28f || mx - mn < mx * 0.35f)) {
                        if (mx > 30) {
                            hdB = std::max(hdB, y); // the real foot of the HD
                        }
                        erased[(size_t)y * logo.w + x] = 1;
                        p[0] = p[1] = p[2] = p[3] = 0;
                    }
                }
            }
            // 2. rebuild letter strokes the HD covered: across the gap along the row, the
            //    stroke's diagonals or the column, whichever finds letter on both sides closest
            auto solid = [&](int x, int y) {
                return x >= 0 && y >= 0 && x < logo.w && y < logo.h && !erased[(size_t)y * logo.w + x] && lum(x, y) > 150; // stroke interiors, not their dark edges
            };
            std::vector<BYTE> fill(logo.px);
            const int dirs[4][2] = { { 1, 0 }, { 1, 1 }, { 1, -1 }, { 0, 1 } };
            for (int y = zy0; y <= std::min(logo.h - 1, redB + 12); y++) {
                for (int x = zx0; x <= zx1; x++) {
                    if (!erased[(size_t)y * logo.w + x]) {
                        continue;
                    }
                    int best = 99, ax = 0, ay = 0, bx = 0, by = 0, da = 0;
                    for (const auto& d : dirs) {
                        int a = -1, b = -1;
                        for (int s = 1; s <= 9 && a < 0; s++) {
                            if (solid(x - d[0] * s, y - d[1] * s)) {
                                a = s;
                            }
                        }
                        for (int s = 1; s <= 9 && b < 0; s++) {
                            if (solid(x + d[0] * s, y + d[1] * s)) {
                                b = s;
                            }
                        }
                        if (a > 0 && b > 0 && a + b < best) {
                            best = a + b, da = a;
                            ax = x - d[0] * a, ay = y - d[1] * a, bx = x + d[0] * b, by = y + d[1] * b;
                        }
                    }
                    if (best <= 10) { // a gap no wider than the HD's stems
                        const float t = (float)da / best;
                        const BYTE *pa = px(ax, ay), *pb = px(bx, by);
                        BYTE* q = &fill[((size_t)y * logo.w + x) * 4];
                        for (int c = 0; c < 4; c++) {
                            q[c] = (BYTE)(pa[c] + (pb[c] - pa[c]) * t);
                        }
                    }
                }
            }
            logo.px.swap(fill);
            // 3. what is left in the zone stays only if it belongs to a LUGARU letter: lit and
            //    connected to the letters outside the zone (the R's tail may hang a little
            //    below the baseline; the HD's orange shading further down goes)
            const int tailB = std::min(logo.h - 1, redB + std::max(4, (redB - redT) / 9));
            std::vector<char> keep((size_t)logo.w * logo.h, 0);
            std::vector<std::pair<int, int>> st;
            for (int y = zy0; y <= tailB; y++) {
                for (int x = zx0; x <= zx1; x++) {
                    if (lum(x, y) <= 25) {
                        continue;
                    }
                    bool edge = false; // touches a lit pixel outside the zone
                    for (int dy = -1; dy <= 1 && !edge; dy++) {
                        for (int dx = -1; dx <= 1 && !edge; dx++) {
                            const int nx = x + dx, ny = y + dy;
                            edge = nx >= 0 && ny >= 0 && nx < logo.w && ny < logo.h && !inZone(nx, ny) && lum(nx, ny) > 25;
                        }
                    }
                    if (edge) {
                        st.push_back({ x, y });
                    }
                }
            }
            while (!st.empty()) {
                const auto q = st.back();
                st.pop_back();
                const int x = q.first, y = q.second;
                if (!inZone(x, y) || y > tailB) {
                    continue;
                }
                char& k = keep[(size_t)y * logo.w + x];
                if (k || lum(x, y) <= 25) {
                    continue;
                }
                k = 1;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx || dy) {
                            st.push_back({ x + dx, y + dy });
                        }
                    }
                }
            }
            for (int y = zy0; y < logo.h; y++) {
                for (int x = zx0; x <= zx1; x++) {
                    if (!keep[(size_t)y * logo.w + x]) {
                        BYTE* p = px(x, y);
                        p[0] = p[1] = p[2] = p[3] = 0;
                    }
                }
            }
        }
        float grad[64][3] = {};
        if (found) {
            bool valid[64] = {};
            for (int t = 0; t < 64; t++) {
                const int y0 = redT + (redB - redT) * t / 64, y1 = std::max(y0 + 1, redT + (redB - redT) * (t + 1) / 64);
                double s[3] = {}, n = 0;
                for (int y = y0; y < y1; y++) {
                    for (int x = 0; x < hdL - 2; x++) {
                        const BYTE* p = &logo.px[((size_t)y * logo.w + x) * 4];
                        if (p[2] > 150 && letter(p)) { // solid letter body, not the soft edges
                            s[0] += p[2], s[1] += p[1], s[2] += p[0], n++;
                        }
                    }
                }
                valid[t] = n >= 12; // a thin serif row alone gives a dull colour
                for (int c = 0; c < 3; c++) {
                    grad[t][c] = n > 0 ? (float)(s[c] / n) : 0.f;
                }
            }
            for (int t = 0; t < 64; t++) { // fill weak rows from the nearest good one
                if (valid[t]) {
                    continue;
                }
                for (int d = 1; d < 64; d++) {
                    const int a = t - d, b2 = t + d;
                    const int k = (b2 < 64 && valid[b2]) ? b2 : ((a >= 0 && valid[a]) ? a : -1);
                    if (k >= 0) {
                        grad[t][0] = grad[k][0], grad[t][1] = grad[k][1], grad[t][2] = grad[k][2];
                        break;
                    }
                }
            }
            // smooth it (rows full of serifs read a little off: EX showed bands)
            float sm[64][3];
            for (int t = 0; t < 64; t++) {
                float s[3] = {}, n = 0;
                for (int d = -5; d <= 5; d++) {
                    const int k = std::max(0, std::min(63, t + d));
                    for (int c = 0; c < 3; c++) {
                        s[c] += grad[k][c];
                    }
                    n++;
                }
                for (int c = 0; c < 3; c++) {
                    sm[t][c] = s[c] / n;
                }
            }
            memcpy(grad, sm, sizeof(sm));
        }

        const int lh = (int)(H * 0.92);
        const float sc = (float)lh / logo.h;
        const int lw = (int)(logo.w * sc);

        // The game's main menu gets the same edit at the logo's own size
        // (LugaruEX\Data\Textures\LugaruEX-Logo.png, made from the user's file;
        // the menu draws it additively, so it stays opaque on black like the original)
        // EX ends where LUGARU ends (under the "RU"), so it fits the logo's width
        int exW = 0, exH = 0;
        int capLogo = hdB - hdT + 1; // as tall as the HD was
        const int exTopLogo = redB + std::max(3, (redB - redT) / 22); // just under LUGARU's baseline
        if (found) {
            // fit under LUGARU inside the logo image (the menu's texture): letters + shadow + a margin
            capLogo = std::max(12, std::min(capLogo, (int)((logo.h - 3 - exTopLogo) / 1.12f)));
            PaintEX(screen, capLogo, nullptr, -10000, -10000, [](int, int, float, float, float, float) {}, &exW, &exH); // measure
        }
        const int exLogoX = found ? std::max(2, std::min(hdL, redR - exW)) : 0;
        if (found) {
            Image menu = logo;
            for (size_t i = 3; i < menu.px.size(); i += 4) {
                menu.px[i] = 255;
            }
            PaintEX(screen, capLogo, grad, exLogoX, exTopLogo, [&](int x, int y, float r, float g, float bl, float a) {
                if (x < 0 || y < 0 || x >= menu.w || y >= menu.h || a <= 0) {
                    return;
                }
                BYTE* d = &menu.px[((size_t)y * menu.w + x) * 4];
                d[2] = (BYTE)std::min(255.f, d[2] * (1 - a) + r * a);
                d[1] = (BYTE)std::min(255.f, d[1] * (1 - a) + g * a);
                d[0] = (BYTE)std::min(255.f, d[0] * (1 - a) + bl * a);
            });
            // the same ember glow as the launcher's banner (the menu adds the image over its
            // backdrop, so the glow shows as a warm haze around the letters)
            {
                std::vector<float> cv((size_t)menu.w * menu.h);
                for (size_t i = 0; i < cv.size(); i++) {
                    const BYTE* p = &menu.px[i * 4];
                    cv[i] = std::min(1.f, std::max(p[0], std::max(p[1], p[2])) / 160.f);
                }
                const std::vector<float> gl = BoxBlur(cv, menu.w, menu.h, std::max(2, menu.h / 22));
                for (size_t i = 0; i < cv.size(); i++) {
                    const float g = gl[i] * (1.f - cv[i]) * 0.42f;
                    BYTE* p = &menu.px[i * 4];
                    p[2] = (BYTE)std::min(255.f, p[2] + 255.f * g);
                    p[1] = (BYTE)std::min(255.f, p[1] + 66.f * g);
                    p[0] = (BYTE)std::min(255.f, p[0] + 16.f * g);
                }
            }
            if (!g_setupMode) { // (the installer only borrows the game's art for its banner)
                const std::wstring dir = g_exeDir + L"LugaruEX\\Data\\Textures";
                SHCreateDirectoryExW(NULL, dir.c_str(), NULL);
                SavePNG(dir + L"\\LugaruEX-Logo.png", menu);
            }
        }

        // "EX" for the banner, cap height = the HD's height
        const int capH = found ? (int)(capLogo * sc) : (int)(H * 0.3f);
        int gw = 0, gh = 0;
        PaintEX(screen, capH, nullptr, -10000, -10000, [](int, int, float, float, float, float) {}, &gw, &gh); // measure
        const int gap = std::max(2, capH / 7);

        // centre LUGARU + EX together (EX sits where the HD was)
        const int contentL = (int)((found ? redL : 0) * sc);
        const int exLeft = found ? (int)(exLogoX * sc) : (int)(logo.w * 0.7f * sc) + gap;
        const int contentR = std::max(found ? (int)(redR * sc) : lw, exLeft + gw);
        const int exTop = found ? (int)(exTopLogo * sc) : (int)(logo.h * 0.6f * sc); // EX hangs just below LUGARU's baseline
        const int contentT = found ? (int)(redT * sc) : 0, contentB = found ? exTop + gh : lh;
        const int lx = (W - (contentR - contentL)) / 2 - contentL, ly = (H - (contentB - contentT)) / 2 - contentT;

        HBITMAP b = ImageToBitmap(screen, logo);
        HGDIOBJ o = SelectObject(src, b);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        SetStretchBltMode(dc, HALFTONE);
        AlphaBlend(dc, lx, ly, lw, lh, src, 0, 0, logo.w, logo.h, bf);
        SelectObject(src, o);
        DeleteObject(b);
        GdiFlush();

        // where the letters are (LUGARU from the logo's alpha, EX as it is painted): for the glow
        std::vector<float> cover((size_t)W * H, 0.f);
        for (int y = std::max(0, ly); y < std::min(H, ly + lh); y++) {
            for (int x = std::max(0, lx); x < std::min(W, lx + lw); x++) {
                const int sx = std::min(logo.w - 1, (int)((x - lx) / sc)), sy = std::min(logo.h - 1, (int)((y - ly) / sc));
                cover[(size_t)y * W + x] = logo.px[((size_t)sy * logo.w + sx) * 4 + 3] / 255.f;
            }
        }

        // paint EX: logo colours by height, light from the top-left like the letters, soft shadow
        BYTE* D = (BYTE*)bits;
        PaintEX(screen, capH, found ? grad : nullptr, lx + exLeft, ly + exTop, [&](int x, int y, float r, float g, float bl, float a) {
            if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) {
                return;
            }
            BYTE* d = &D[((size_t)y * W + x) * 4];
            d[2] = (BYTE)std::min(255.f, d[2] * (1 - a) + r * a);
            d[1] = (BYTE)std::min(255.f, d[1] * (1 - a) + g * a);
            d[0] = (BYTE)std::min(255.f, d[0] * (1 - a) + bl * a);
            if (r + g + bl > 0) { // (not the shadow)
                float& cv = cover[(size_t)y * W + x];
                cv = std::max(cv, a);
            }
        });
        // an ember glow around the letters, like the game's bloom
        const std::vector<float> glow = BoxBlur(cover, W, H, std::max(2, H / 16));
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                const float gl = glow[(size_t)y * W + x] * (1.f - cover[(size_t)y * W + x]) * 0.55f;
                BYTE* d = &D[((size_t)y * W + x) * 4];
                d[2] = (BYTE)std::min(255.f, d[2] + 255.f * gl);
                d[1] = (BYTE)std::min(255.f, d[1] + 70.f * gl);
                d[0] = (BYTE)std::min(255.f, d[0] + 18.f * gl);
            }
        }
    } else {
        // no game art here yet (the installer before a game is picked): a plain title
        HFONT f = CreateFontW(-(H * 2 / 5), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, VARIABLE_PITCH, ExFontFace(screen).c_str());
        HGDIOBJ of = SelectObject(dc, f);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(225, 60, 20));
        RECT tr = { 0, 0, W, H };
        DrawTextW(dc, L"LUGARU EX", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, of);
        DeleteObject(f);
        GdiFlush();
    }
    // a gentle vignette draws the eye to the middle
    {
        BYTE* D = (BYTE*)bits;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                const float u = (x + .5f) / W - .5f, v = ((y + .5f) / H - .5f) * 0.6f;
                const float k = 1.f - 0.55f * std::min(1.f, (u * u + v * v) * 2.6f);
                BYTE* d = &D[((size_t)y * W + x) * 4];
                d[0] = (BYTE)(d[0] * k), d[1] = (BYTE)(d[1] * k), d[2] = (BYTE)(d[2] * k);
            }
        }
    }
    DeleteDC(src);

    // test hook LUGARUEX_SAVE_BANNER=<file.png>: write the finished banner out
    if (const char* sp = getenv("LUGARUEX_SAVE_BANNER")) {
        Image im;
        im.w = W;
        im.h = H;
        im.px.assign((const BYTE*)bits, (const BYTE*)bits + (size_t)W * H * 4);
        for (size_t i = 3; i < im.px.size(); i += 4) {
            im.px[i] = 255;
        }
        SavePNG(std::wstring(sp, sp + strlen(sp)), im);
    }

    // GDI text leaves alpha at 0 - the bitmap is only BitBlt'ed so that's fine.
    SelectObject(dc, old);
    DeleteDC(dc);
    ReleaseDC(ctl, screen);
}

// ----------------------------------------------------------------------------
// Dialog

static HWND g_main = NULL, g_tabs = NULL, g_gfx = NULL, g_mp = NULL, g_ct = NULL, g_lc = NULL, g_md = NULL;


static int ComboSel(HWND page, int id)
{
    return (int)SendDlgItemMessageW(page, id, CB_GETCURSEL, 0, 0);
}
static LPARAM ComboData(HWND page, int id)
{
    int s = ComboSel(page, id);
    return s < 0 ? -1 : SendDlgItemMessageW(page, id, CB_GETITEMDATA, s, 0);
}
static int ComboAdd(HWND page, int id, const std::wstring& text, LPARAM data)
{
    int i = (int)SendDlgItemMessageW(page, id, CB_ADDSTRING, 0, (LPARAM)text.c_str());
    SendDlgItemMessageW(page, id, CB_SETITEMDATA, i, data);
    return i;
}
static void ComboSelectData(HWND page, int id, LPARAM data, int fallback = 0)
{
    int n = (int)SendDlgItemMessageW(page, id, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; i++) {
        if (SendDlgItemMessageW(page, id, CB_GETITEMDATA, i, 0) == data) {
            SendDlgItemMessageW(page, id, CB_SETCURSEL, i, 0);
            return;
        }
    }
    SendDlgItemMessageW(page, id, CB_SETCURSEL, fallback, 0);
}

static const LPARAM CUSTOM_RES = 0x7fffffff;

static void GetSelectedRes(int& w, int& h)
{
    LPARAM d = ComboData(g_gfx, IDC_RES);
    if (d == CUSTOM_RES || d < 0) {
        w = GetDlgItemInt(g_gfx, IDC_CUSTW, NULL, FALSE);
        h = GetDlgItemInt(g_gfx, IDC_CUSTH, NULL, FALSE);
    } else {
        w = (int)(d & 0xFFFF);
        h = (int)((d >> 16) & 0xFFFF);
    }
}

static void UpdateAspectLabel()
{
    int w, h;
    GetSelectedRes(w, h);
    std::wstring t;
    if (w > 0 && h > 0) {
        t = L"Aspect " + AspectName(w, h);
        if (ComboData(g_gfx, IDC_RES) == CUSTOM_RES) {
            t += L"  -  custom sizes work best in Windowed or Borderless";
        }
    }
    SetDlgItemTextW(g_gfx, IDC_ASPECT, t.c_str());
}

// FPS cap choices: the detected refresh rate of the chosen display (or the
// chosen exclusive-fullscreen rate), unlimited, or a fixed number of frames.
// want: -1 match, 0 unlimited, n = n fps, -2 keep the current choice.
// (Frames only: the game's simulation runs at its own rate, so a cap never
// changes how the game plays, nor what other players see online.)
static const int kFpsCaps[] = { 30, 60, 75, 90, 120, 144, 165, 240, 360 };
static void FillFpsCap(int want)
{
    if (want == -2) {
        want = (int)ComboData(g_gfx, IDC_FPSCAP);
    }
    int mon = std::max(0, ComboSel(g_gfx, IDC_MONITOR));
    int hz = (mon < (int)g_mons.size()) ? g_mons[mon].curHz : 0;
    if (ComboData(g_gfx, IDC_MODE) == 2 && mon < (int)g_mons.size()) {
        if (ComboData(g_gfx, IDC_REFRESH) > 0) {
            hz = (int)ComboData(g_gfx, IDC_REFRESH);
        } else { // "Default" = the engine picks the highest rate for this size
            int w, h;
            GetSelectedRes(w, h);
            auto it = g_mons[mon].rates.find({ w, h });
            if (it != g_mons[mon].rates.end() && !it->second.empty()) {
                hz = *it->second.rbegin();
            }
        }
    }
    SendDlgItemMessageW(g_gfx, IDC_FPSCAP, CB_RESETCONTENT, 0, 0);
    ComboAdd(g_gfx, IDC_FPSCAP, hz > 0 ? L"Match display (" + std::to_wstring(hz) + L" Hz)" : std::wstring(L"Match display"), -1);
    ComboAdd(g_gfx, IDC_FPSCAP, L"Unlimited", 0);
    bool listed = want <= 0;
    for (int c : kFpsCaps) {
        ComboAdd(g_gfx, IDC_FPSCAP, std::to_wstring(c) + L" fps", c);
        listed = listed || c == want;
    }
    if (!listed) { // a number typed into the ini by hand stays
        ComboAdd(g_gfx, IDC_FPSCAP, std::to_wstring(want) + L" fps", want);
    }
    ComboSelectData(g_gfx, IDC_FPSCAP, want, 0);
}

static void FillRefresh(int wantHz)
{
    HWND c = GetDlgItem(g_gfx, IDC_REFRESH);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    ComboAdd(g_gfx, IDC_REFRESH, L"Default", 0);
    int mon = std::max(0, ComboSel(g_gfx, IDC_MONITOR));
    int w, h;
    GetSelectedRes(w, h);
    if (mon < (int)g_mons.size()) {
        auto it = g_mons[mon].rates.find({ w, h });
        if (it != g_mons[mon].rates.end()) {
            for (auto r = it->second.rbegin(); r != it->second.rend(); ++r) {
                ComboAdd(g_gfx, IDC_REFRESH, std::to_wstring(*r) + L" Hz", *r);
            }
        }
    }
    (void)wantHz;
    ComboSelectData(g_gfx, IDC_REFRESH, 0, 0); // always "Default" (the choice is hidden)
    const bool fs = ComboData(g_gfx, IDC_MODE) == 2;
    EnableWindow(c, fs);
    FillFpsCap(-2); // the detected rate may have changed
}

static void FillResolutions(int wantW, int wantH)
{
    HWND c = GetDlgItem(g_gfx, IDC_RES);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    int mon = std::max(0, ComboSel(g_gfx, IDC_MONITOR));
    const Monitor& m = g_mons[std::min(mon, (int)g_mons.size() - 1)];
    if (wantW <= 0 || wantH <= 0) {
        wantW = m.curW;
        wantH = m.curH;
    }
    bool found = false;
    for (auto& r : m.res) {
        wchar_t b[96];
        swprintf(b, 96, L"%d x %d   (%s)%s", r.first, r.second, AspectName(r.first, r.second).c_str(),
                 (r.first == m.curW && r.second == m.curH) ? L"  native" : L"");
        int i = ComboAdd(g_gfx, IDC_RES, b, (LPARAM)((r.first & 0xFFFF) | ((r.second & 0xFFFF) << 16)));
        if (r.first == wantW && r.second == wantH) {
            SendMessageW(c, CB_SETCURSEL, i, 0);
            found = true;
        }
    }
    int ci = ComboAdd(g_gfx, IDC_RES, L"Custom...", CUSTOM_RES);
    if (!found) {
        SendMessageW(c, CB_SETCURSEL, ci, 0);
    }
    SetDlgItemInt(g_gfx, IDC_CUSTW, wantW, FALSE);
    SetDlgItemInt(g_gfx, IDC_CUSTH, wantH, FALSE);
    const bool custom = ComboData(g_gfx, IDC_RES) == CUSTOM_RES;
    EnableWindow(GetDlgItem(g_gfx, IDC_CUSTW), custom);
    EnableWindow(GetDlgItem(g_gfx, IDC_CUSTH), custom);
    UpdateAspectLabel();
}

static void UpdateFovLabel()
{
    int v = (int)SendDlgItemMessageW(g_gfx, IDC_FOV, TBM_GETPOS, 0, 0);
    wchar_t b[64];
    swprintf(b, 64, L"%d\u00B0%s", v, v == 90 ? L" (original)" : L"");
    SetDlgItemTextW(g_gfx, IDC_FOVLBL, b);
}

// ---- Multiplayer page: Solo / Host / Connect, host options, one server list (Steam +
// LAN / VPN + servers joined before). The launcher stays open while an online game runs:
// it shows who is on your server (right click: kick / ban) and asks for passwords.
static ServerQuery g_servers;
static int g_advShift = 0;            // pixels the lower part moves down when Host options are open
static std::map<int, RECT> g_mpRects; // the lower part's places with Host options closed

static std::vector<std::string> RecentServers()
{
    std::vector<std::string> out;
    std::stringstream ss(g_set.recent);
    std::string a;
    while (std::getline(ss, a, '|')) {
        a = trim(a);
        if (!a.empty() && std::find(out.begin(), out.end(), a) == out.end()) {
            out.push_back(a);
        }
    }
    return out;
}

static void RememberServer(const std::string& addr)
{
    std::vector<std::string> r = RecentServers();
    r.erase(std::remove(r.begin(), r.end(), addr), r.end());
    r.insert(r.begin(), addr);
    r.resize(std::min<size_t>(r.size(), 8));
    std::string s;
    for (auto& a : r) {
        s += (s.empty() ? "" : "|") + a;
    }
    g_set.recent = s;
}

// Steam lobbies (asked through the helper on a thread; the page timer picks them up)
static std::vector<ServerEntry> g_steamServers;
static std::string g_steamListError;
static bool g_steamAnswered = false; // (re-asking quietly after the first answer)
static std::atomic<int> g_steamListState(0); // 0 idle, 1 asking, 2 answer waiting
static std::vector<ServerEntry> g_steamListNew;
static std::string g_steamListNewError;

// the server picked in the list for Connect (a Steam lobby has no address to type)
static ServerEntry g_pick;
static bool g_havePick = false;
static bool g_settingAddr = false; // (our own SetDlgItemText, not the player typing)

// ---- the link to the running game (LUGARUEX_ADMIN): status once a second, kick / ban / password back
static AdminLink g_link;               // shared/adminlink.cpp
static AdminStatus& g_admin = g_link.st;
static HANDLE g_engineProcess = NULL;
static bool g_filling = false;      // (FillServerList restoring the selection is no pick)
static std::string g_joinPassword; // typed in the password window: only in memory, given to the game
static bool g_pwAsking = false;
static DWORD g_pwAskedAt = 0;

static bool GameRunning()
{
    return g_engineProcess != NULL;
}

static bool MpCheck(int id)
{
    return IsDlgButtonChecked(g_mp, id) == BST_CHECKED;
}

static std::string MpText(int id)
{
    wchar_t b[256] = {};
    GetDlgItemTextW(g_mp, id, b, 256);
    return trim(narrow(b));
}

static void SetAddrText(const std::string& s)
{
    g_settingAddr = true;
    SetDlgItemTextW(g_mp, IDC_ADDR, widen(s).c_str());
    g_settingAddr = false;
}

static void Show(int id, bool on)
{
    ShowWindow(GetDlgItem(g_mp, id), on ? SW_SHOW : SW_HIDE);
}

static void UpdateHostStatus();

static void UpdateMpEnable()
{
    const bool host = MpCheck(IDC_MP_HOST);
    const bool join = MpCheck(IDC_MP_JOIN);
    const bool opts = host && MpCheck(IDC_MP_ADVANCED);
    Show(IDC_MP_HOSTSTATUS, host);
    Show(IDC_MP_SOLOAI, !host && !join); // Solo: Expanded AI (the same setting as in the Host options)
    Show(IDC_MP_ADVANCED, host);
    SetDlgItemTextW(g_mp, IDC_MP_ADVANCED, opts ? L"Host options  v" : L"Host options  >");
    for (int id : { IDC_MP_SNAMELBL, IDC_MP_SERVERNAME, IDC_MP_GUESTCHARS, IDC_MP_PWON, IDC_MP_HOSTPW, IDC_MP_HOSTPW_SHOW, IDC_MP_MAXLBL,
                    IDC_MP_MAXPLAYERS, IDC_MP_MAXHINT, IDC_LAN, IDC_MP_PORTLBL, IDC_PORT, IDC_MP_FRIENDLY, IDC_MP_EXPANDEDAI,
                    IDC_MP_AFKLBL, IDC_MP_AFK }) {
        Show(id, opts);
    }
    const bool pw = MpCheck(IDC_MP_PWON);
    EnableWindow(GetDlgItem(g_mp, IDC_MP_HOSTPW), pw);
    EnableWindow(GetDlgItem(g_mp, IDC_MP_HOSTPW_SHOW), pw);
    EnableWindow(GetDlgItem(g_mp, IDC_PORT), MpCheck(IDC_LAN));
    EnableWindow(GetDlgItem(g_mp, IDC_MP_PORTLBL), MpCheck(IDC_LAN));
    for (int id : { IDC_MP_ADDRLBL, IDC_ADDR, IDC_MP_ADDRHINT }) {
        Show(id, join);
    }
    // the lower part moves down while the host options are open
    const int dy = opts ? g_advShift : 0;
    for (auto& kv : g_mpRects) {
        RECT r = kv.second;
        HWND h = GetDlgItem(g_mp, kv.first);
        if (kv.first == IDC_MP_SERVERS) {
            MoveWindow(h, r.left, r.top + dy, r.right - r.left, r.bottom - r.top - dy, TRUE);
        } else {
            MoveWindow(h, r.left, r.top + dy, r.right - r.left, r.bottom - r.top, TRUE);
        }
    }
    UpdateHostStatus();
    // repaint the whole page: hidden / moved checkboxes left black squares behind
    RedrawWindow(g_mp, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static void PasswordShown(HWND dlg, int edit, int box)
{
    const bool show = IsDlgButtonChecked(dlg, box) == BST_CHECKED;
    SendDlgItemMessageW(dlg, edit, EM_SETPASSWORDCHAR, show ? 0 : 0x25CF, 0);
    InvalidateRect(GetDlgItem(dlg, edit), NULL, TRUE);
}

// ---- the password window (joining a locked server, or the game says the password is wrong)
static std::wstring g_pwQuestion;
static std::string g_pwAnswer;

static INT_PTR CALLBACK PasswordProc(HWND d, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg) {
        case WM_INITDIALOG:
            SetDlgItemTextW(d, IDC_PW_TEXT, g_pwQuestion.c_str());
            SendDlgItemMessageW(d, IDC_PW_EDIT, EM_SETPASSWORDCHAR, 0x25CF, 0);
            SendDlgItemMessageW(d, IDC_PW_EDIT, EM_LIMITTEXT, 64, 0);
            SetForegroundWindow(d);
            SetFocus(GetDlgItem(d, IDC_PW_EDIT));
            return FALSE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_PW_SHOW) {
                PasswordShown(d, IDC_PW_EDIT, IDC_PW_SHOW);
                return TRUE;
            }
            if (LOWORD(wp) == IDOK) {
                wchar_t b[128] = {};
                GetDlgItemTextW(d, IDC_PW_EDIT, b, 128);
                g_pwAnswer = narrow(b);
                EndDialog(d, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(d, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

static bool AskPassword(const std::wstring& question, std::string& out)
{
    g_pwQuestion = question;
    g_pwAnswer.clear();
    if (DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_PASSWORD), g_main, PasswordProc, 0) != IDOK) {
        return false;
    }
    out = g_pwAnswer;
    return true;
}

static std::wstring ServerStatus(const ServerEntry& e)
{
    if (!e.answered) {
        return L"No answer";
    }
    if (e.info.version != EX_LAUNCHER_VERSION) {
        return L"Version " + widen(e.info.version); // (can't be joined from this version)
    }
    // "In game" / "Waiting", ", Private" when it has a password (nothing more: cleaner)
    std::wstring s = e.info.inGame ? L"In game" : L"Waiting";
    if (e.info.password) {
        s += L", Private";
    }
    return s;
}

// is this list entry the server this PC hosts right now
static bool IsOwnServer(const ServerEntry& e)
{
    return GameRunning() && g_admin.heard && g_admin.role == 1 && e.answered && e.info.id != 0 && e.info.id == g_admin.sessionId;
}

// every server: LAN / VPN first (closest), then Steam, then ones joined before; the same
// hosting session answering twice shows once
static std::vector<ServerEntry> ListSource()
{
    std::vector<ServerEntry> out;
    std::set<uint32_t> ids;
    auto add = [&](const ServerEntry& e) {
        if (e.answered && e.info.id != 0) {
            if (ids.count(e.info.id)) {
                return;
            }
            ids.insert(e.info.id);
        }
        out.push_back(e);
    };
    for (const ServerEntry& e : g_servers.Entries()) {
        if (e.lan) {
            add(e);
        }
    }
    for (const ServerEntry& e : g_steamServers) {
        add(e);
    }
    for (const ServerEntry& e : g_servers.Entries()) {
        if (!e.lan) {
            add(e);
        }
    }
    // the server I host first (it may not be in Steam's list yet)
    if (GameRunning() && g_admin.heard && g_admin.role == 1) {
        auto own = std::find_if(out.begin(), out.end(), IsOwnServer);
        ServerEntry mine;
        if (own != out.end()) {
            mine = *own;
            out.erase(own);
        } else {
            mine.addr = "(this PC)";
            mine.answered = true;
            mine.info.id = g_admin.sessionId;
            mine.info.version = EX_LAUNCHER_VERSION;
            mine.info.name = g_set.serverName.empty() ? netinfo::CleanName(g_set.name) + "'s game" : g_set.serverName;
            mine.info.players = 1 + (int)g_admin.players.size();
            mine.info.maxPlayers = g_set.maxPlayers;
        }
        out.insert(out.begin(), mine);
    }
    return out;
}

// list rows: a server, or (opened with a double click; your own server always) one of its players
struct MpRow
{
    int server = 0;
    int player = -1;    // -1 = the server's row
    uint32_t admin = 0; // a guest on your server (right click: kick / ban)
};
static std::vector<ServerEntry> g_shownServers;
static std::vector<MpRow> g_rows;
static std::set<std::string> g_opened;      // servers whose players show (by addr)
static std::set<std::string> g_closedOwn;   // your own server, closed by hand

static std::string RowKey(const ServerEntry& e)
{
    return e.info.id ? "#" + std::to_string(e.info.id) : e.addr;
}

static bool Opened(const ServerEntry& e)
{
    return IsOwnServer(e) ? !g_closedOwn.count(RowKey(e)) : g_opened.count(RowKey(e)) > 0;
}

static void FillServerList()
{
    HWND lv = GetDlgItem(g_mp, IDC_MP_SERVERS);
    std::vector<ServerEntry> list = ListSource();
    // answering servers first, then by ping (your own stays on top)
    const bool ownFirst = !list.empty() && IsOwnServer(list[0]);
    std::stable_sort(list.begin() + (ownFirst ? 1 : 0), list.end(), [](const ServerEntry& a, const ServerEntry& b) {
        if (a.answered != b.answered) {
            return a.answered;
        }
        return a.pingMs < b.pingMs;
    });
    std::string selKey;
    int selPlayer = -1;
    const int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < (int)g_rows.size()) {
        selKey = RowKey(g_shownServers[g_rows[sel].server]);
        selPlayer = g_rows[sel].player;
    }
    g_shownServers = list;
    g_filling = true;
    g_rows.clear();
    SendMessageW(lv, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(lv);
    auto addRow = [&](const MpRow& r, const std::wstring& c0, const std::wstring& c1, const std::wstring& c2, const std::wstring& c3) {
        const int i = (int)g_rows.size();
        g_rows.push_back(r);
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = (LPWSTR)c0.c_str();
        ListView_InsertItem(lv, &it);
        ListView_SetItemText(lv, i, 1, (LPWSTR)c1.c_str());
        ListView_SetItemText(lv, i, 2, (LPWSTR)c2.c_str());
        ListView_SetItemText(lv, i, 3, (LPWSTR)c3.c_str());
        if (RowKey(g_shownServers[r.server]) == selKey && r.player == selPlayer) {
            ListView_SetItemState(lv, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
    };
    for (int i = 0; i < (int)list.size(); i++) {
        const ServerEntry& e = list[i];
        const bool own = IsOwnServer(e);
        const int players = own ? 1 + (int)g_admin.players.size() : e.info.players;
        const std::wstring count = !e.answered ? L"-" : e.info.maxPlayers > 0 ? std::to_wstring(players) + L"/" + std::to_wstring(e.info.maxPlayers) : std::to_wstring(players);
        const std::wstring status = own ? L"Yours" + std::wstring(e.info.password ? L", Private" : L"") : ServerStatus(e);
        const std::wstring ping = own ? L"" : e.answered && e.pingMs >= 0 ? std::to_wstring(e.pingMs) + L" ms" : L"-";
        MpRow r;
        r.server = i;
        addRow(r, L"", count, status, ping); // (the name is drawn in its colours)
        if (!Opened(e) || !e.answered) {
            continue;
        }
        if (own) {
            MpRow me;
            me.server = i;
            me.player = 0;
            addRow(me, L"      " + widen(netinfo::CleanName(g_set.name)) + L" (you)", L"", L"Host", L"");
            for (size_t k = 0; k < g_admin.players.size(); k++) {
                const AdminPlayer& a = g_admin.players[k];
                MpRow pr;
                pr.server = i;
                pr.player = (int)k + 1;
                pr.admin = a.id;
                addRow(pr, L"      " + widen(a.name), a.seats > 1 ? std::to_wstring(a.seats) + L" players" : L"",
                       a.inGame ? L"In game" : L"Waiting", L"");
            }
        } else {
            for (size_t k = 0; k < e.info.list.size(); k++) {
                MpRow pr;
                pr.server = i;
                pr.player = (int)k;
                addRow(pr, L"      " + widen(e.info.list[k].name), L"", widen(netinfo::StateName(e.info.list[k].state)), L"");
            }
        }
    }
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
    g_filling = false;
}

// the line under the list
static void ShowServerDetails()
{
    HWND lv = GetDlgItem(g_mp, IDC_MP_SERVERS);
    const int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
    std::wstring t;
    if (sel < 0 || sel >= (int)g_rows.size()) {
        t = GameRunning() && g_admin.heard && g_admin.role == 2 && !g_admin.status.empty() ? L"Game: " + widen(g_admin.status)
            : g_steamListState == 1 && !g_steamAnswered ? L"Asking Steam..."
            : !g_steamListError.empty() ? widen(g_steamListError) + L" (LAN / VPN servers still show.)"
                                        : L"Pick a server to join it. Through Steam nobody sees anyone's IP address.";
    } else {
        const MpRow& r = g_rows[sel];
        const ServerEntry& e = g_shownServers[r.server];
        if (r.admin) {
            t = L"Right click a player to kick or ban them (they stay behind as an enemy).";
        } else if (IsOwnServer(e)) {
            t = L"Your server. Double click to show or hide its players.";
        } else if (!e.answered) {
            t = widen(e.addr) + L" did not answer.";
        } else if (e.info.version != EX_LAUNCHER_VERSION) {
            t = L"Version " + widen(e.info.version) + L" (yours: " + widen(EX_LAUNCHER_VERSION) + L"): can't be joined.";
        } else {
            t = (e.steam ? L"Through Steam" : e.lan ? L"On your network" : L"Direct") + std::wstring(L". ");
            t += e.info.password ? L"Password protected (players hidden)." : L"Double click to show who is there.";
        }
    }
    SetDlgItemTextW(g_mp, IDC_MPNOTE, t.c_str());
}

static void UpdateHostStatus()
{
    std::wstring t;
    if (GameRunning() && g_admin.heard && g_admin.role == 1) {
        t = !g_admin.status.empty() ? widen(g_admin.status)
            : g_admin.steam && g_admin.code.empty() ? L"Your server is starting..."
                                                    : L"Your server is up" + std::wstring(g_admin.steam ? L" in the Steam server list" : L" on your network") + L": " +
                                                          std::to_wstring(g_admin.players.size()) + (g_admin.players.size() == 1 ? L" guest." : L" guests.");
    } else if (GameRunning()) {
        t = L"The game is running.";
    } else {
        t = MpCheck(IDC_LAN) ? L"Play starts your server on your network (LAN, Radmin, Hamachi, ZeroTier...)."
                             : L"Play starts your server: in the Steam list, or join by IP.";
    }
    SetDlgItemTextW(g_mp, IDC_MP_HOSTSTATUS, t.c_str());
}

static void SteamListThread(std::string helper)
{
    std::vector<ServerEntry> list;
    std::string err;
    SteamList_Run(helper, list, err);
    g_steamListNew = list;
    g_steamListNewError = err;
    g_steamListState = 2;
}

static void RefreshServers()
{
    const std::wstring helper = g_exDir + L"lugaruex-steam.exe";
    if (g_steamListState == 0 && PathFileExistsW(helper.c_str())) {
        g_steamListState = 1;
        std::thread(SteamListThread, narrow(helper)).detach();
    }
    // LAN / VPN broadcast + servers joined before + the typed address
    std::vector<std::string> addrs = RecentServers();
    const std::string typed = MpText(IDC_ADDR);
    if (!typed.empty()) {
        addrs.insert(addrs.begin(), typed);
    }
    const int port = (int)GetDlgItemInt(g_mp, IDC_PORT, NULL, FALSE);
    g_servers.Refresh(addrs, port > 0 && port < 65536 ? port : 27015);
    ShowServerDetails();
}

// a name colour (^0-^9, from the game's dark palette) made readable on the white list
static COLORREF OnWhite(int colour, const unsigned char c[3])
{
    if (colour < 0 || colour == 7) {
        return RGB(20, 20, 20); // (white / plain: black)
    }
    return RGB(c[0] * 62 / 100, c[1] * 62 / 100, c[2] * 62 / 100);
}

// server names in their colours (^0-^9) on the list's white rows
static LRESULT ServerListDraw(NMLVCUSTOMDRAW* cd)
{
    switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
            return CDRF_NOTIFYSUBITEMDRAW;
        case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
            const int i = (int)cd->nmcd.dwItemSpec;
            if (i >= (int)g_rows.size()) {
                return CDRF_DODEFAULT;
            }
            const MpRow& row = g_rows[i];
            const ServerEntry& e = g_shownServers[row.server];
            cd->clrText = e.answered ? (row.player >= 0 ? RGB(90, 90, 90) : RGB(20, 20, 20)) : RGB(150, 150, 150);
            if (cd->iSubItem != 0 || row.player >= 0) {
                return CDRF_DODEFAULT;
            }
            HWND lv = cd->nmcd.hdr.hwndFrom;
            RECT r;
            ListView_GetSubItemRect(lv, i, 0, LVIR_LABEL, &r);
            const bool selected = (ListView_GetItemState(lv, i, LVIS_SELECTED) & LVIS_SELECTED) != 0;
            HBRUSH bg = CreateSolidBrush(selected ? GetSysColor(COLOR_HIGHLIGHT) : RGB(255, 255, 255));
            FillRect(cd->nmcd.hdc, &r, bg);
            DeleteObject(bg);
            SetBkMode(cd->nmcd.hdc, TRANSPARENT);
            r.left += 4;
            // an arrow when it has players to show
            const bool canOpen = e.answered && (IsOwnServer(e) || !e.info.list.empty());
            std::vector<netinfo::Segment> segs;
            segs.push_back({ -1, canOpen ? (Opened(e) ? "v " : "> ") : "  " });
            for (const netinfo::Segment& s : netinfo::ColourSegments(e.info.name)) {
                segs.push_back(s);
            }
            for (const netinfo::Segment& s : segs) {
                unsigned char c[3];
                netinfo::ColourRGB(s.colour, c);
                SetTextColor(cd->nmcd.hdc, selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : e.answered ? OnWhite(s.colour, c) : RGB(150, 150, 150));
                const std::wstring w = widen(s.text);
                RECT m = r;
                DrawTextW(cd->nmcd.hdc, w.c_str(), -1, &m, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_CALCRECT);
                DrawTextW(cd->nmcd.hdc, w.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
                r.left += m.right - m.left;
                if (r.left >= r.right) {
                    break;
                }
            }
            return CDRF_SKIPDEFAULT;
        }
    }
    return CDRF_DODEFAULT;
}

static void PickRow(int item)
{
    if (item < 0 || item >= (int)g_rows.size()) {
        return;
    }
    const ServerEntry& e = g_shownServers[g_rows[item].server];
    if (IsOwnServer(e) || e.addr == "(this PC)") {
        return;
    }
    // picking a server switches to Connect: a LAN / direct one fills in its address
    g_pick = e;
    g_havePick = true;
    SetAddrText(e.steam ? "" : e.addr);
    CheckRadioButton(g_mp, IDC_MP_OFF, IDC_MP_JOIN, IDC_MP_JOIN);
    UpdateMpEnable();
}

static void PlayerMenu(int item)
{
    if (item < 0 || item >= (int)g_rows.size() || !g_rows[item].admin) {
        return;
    }
    const uint32_t id = g_rows[item].admin;
    const std::string name = g_admin.players[g_rows[item].player - 1].name;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_MP_KICK, (L"Kick " + widen(name)).c_str());
    AppendMenuW(m, MF_STRING, IDM_MP_BAN, (L"Ban " + widen(name)).c_str());
    POINT pt;
    GetCursorPos(&pt);
    const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_mp, NULL);
    DestroyMenu(m);
    if (cmd == IDM_MP_KICK) {
        g_link.Kick(id, false);
    } else if (cmd == IDM_MP_BAN &&
               MessageBoxW(g_main, (L"Ban " + widen(name) + L" until you stop hosting? (\"unban " + widen(name) + L"\" in the game's console lifts it.)").c_str(),
                           L"LugaruEX", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        g_link.Kick(id, true);
    }
}

static INT_PTR MpProc(HWND p, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (id == IDC_MP_OFF || id == IDC_MP_HOST || id == IDC_MP_JOIN) {
                CheckRadioButton(p, IDC_MP_OFF, IDC_MP_JOIN, id);
                UpdateMpEnable();
            } else if (id == IDC_MP_SOLOAI || id == IDC_MP_EXPANDEDAI) {
                CheckDlgButton(p, id == IDC_MP_SOLOAI ? IDC_MP_EXPANDEDAI : IDC_MP_SOLOAI, MpCheck(id) ? BST_CHECKED : BST_UNCHECKED);
            } else if (id == IDC_MP_ADVANCED || id == IDC_MP_PWON || id == IDC_LAN) {
                UpdateMpEnable();
            } else if (id == IDC_MP_HOSTPW_SHOW) {
                PasswordShown(p, IDC_MP_HOSTPW, IDC_MP_HOSTPW_SHOW);
            } else if (id == IDC_MP_REFRESH) {
                RefreshServers();
            } else if (id == IDC_ADDR && code == EN_CHANGE) {
                if (!g_settingAddr) {
                    g_havePick = false; // typing an IP replaces the picked server
                }
            } else if (id == IDC_ADDR && code == EN_KILLFOCUS) {
                RefreshServers(); // a typed address is asked too
            } else {
                return FALSE;
            }
            return TRUE;
        }
        case WM_TIMER:
            if (wp == 1) {
                g_servers.Poll();
                bool changed = false;
                if (g_steamListState == 2) { // Steam's answer came
                    g_steamServers = g_steamListNew;
                    g_steamListError = g_steamListNewError;
                    g_steamAnswered = true;
                    g_steamListState = 0;
                    changed = true;
                }
                // redraw only when something changed
                std::string sig;
                for (auto& e : ListSource()) {
                    sig += e.addr + (e.answered ? "+" : "-") + std::to_string(e.info.players) + e.info.name + (e.info.inGame ? "g" : "m") +
                           std::to_string(e.info.list.size()) + std::to_string(e.pingMs / 20) + (Opened(e) ? "o" : "c") + "|";
                    for (auto& pl : e.info.list) {
                        sig += pl.name + std::to_string(pl.state);
                    }
                }
                for (auto& a : g_admin.players) {
                    sig += std::to_string(a.id) + a.name + (a.inGame ? "g" : "w");
                }
                static std::string lastSig;
                if (sig != lastSig || changed) {
                    lastSig = sig;
                    FillServerList();
                    ShowServerDetails();
                }
                // ask again every 15 s while the tab is open
                static DWORD last = GetTickCount(); // (the page asked when it opened)
                if (IsWindowVisible(p) && GetTickCount() - last > 15000u) {
                    last = GetTickCount();
                    RefreshServers();
                }
            }
            return TRUE;
        case WM_NOTIFY: {
            NMHDR* h = (NMHDR*)lp;
            if (h->idFrom != IDC_MP_SERVERS) {
                return FALSE;
            }
            if (h->code == NM_CUSTOMDRAW) {
                SetWindowLongPtrW(p, DWLP_MSGRESULT, ServerListDraw((NMLVCUSTOMDRAW*)lp));
                return TRUE;
            }
            if (h->code == NM_DBLCLK) {
                const int i = ((NMITEMACTIVATE*)lp)->iItem;
                if (i >= 0 && i < (int)g_rows.size() && g_rows[i].player < 0) {
                    const ServerEntry& e = g_shownServers[g_rows[i].server];
                    const std::string k = RowKey(e);
                    std::set<std::string>& s = IsOwnServer(e) ? g_closedOwn : g_opened;
                    if (s.count(k)) {
                        s.erase(k);
                    } else {
                        s.insert(k);
                    }
                    FillServerList();
                }
                return TRUE;
            }
            if (h->code == NM_RCLICK) {
                PlayerMenu(((NMITEMACTIVATE*)lp)->iItem);
                return TRUE;
            }
            if (h->code == LVN_ITEMCHANGED) {
                NMLISTVIEW* n = (NMLISTVIEW*)lp;
                if (!g_filling && (n->uNewState & LVIS_SELECTED) && !(n->uOldState & LVIS_SELECTED)) {
                    PickRow(n->iItem);
                }
                ShowServerDetails();
                return TRUE;
            }
            return FALSE;
        }
        case WM_CTLCOLORSTATIC:
            if ((HWND)lp == GetDlgItem(p, IDC_MP_MODWARN)) {
                SetTextColor((HDC)wp, RGB(190, 70, 30));
                SetBkMode((HDC)wp, TRANSPARENT);
                return (INT_PTR)GetStockObject(NULL_BRUSH);
            }
            return FALSE;
    }
    return FALSE;
}
static void InitGfxPage(HWND p)
{
    g_gfx = p;
    for (auto& m : g_mons) {
        ComboAdd(p, IDC_MONITOR, m.label, 0);
    }
    SendDlgItemMessageW(p, IDC_MONITOR, CB_SETCURSEL, (g_set.monitor < (int)g_mons.size()) ? g_set.monitor : 0, 0);
    EnableWindow(GetDlgItem(p, IDC_MONITOR), g_mons.size() > 1);

    ComboAdd(p, IDC_MODE, L"Windowed", 0);
    ComboAdd(p, IDC_MODE, L"Borderless window", 1);
    ComboAdd(p, IDC_MODE, L"Exclusive fullscreen", 2);
    ComboSelectData(p, IDC_MODE, g_set.mode == "fullscreen" ? 2 : (g_set.mode == "windowed" ? 0 : 1), 1);

    FillResolutions(g_set.width, g_set.height);
    FillRefresh(g_set.refresh);

    ComboAdd(p, IDC_MSAA, L"Off", 0);
    ComboAdd(p, IDC_MSAA, L"2x MSAA", 2);
    ComboAdd(p, IDC_MSAA, L"4x MSAA", 4);
    ComboAdd(p, IDC_MSAA, L"8x MSAA", 8);
    ComboSelectData(p, IDC_MSAA, g_set.msaa, 0);

    ComboAdd(p, IDC_AF, L"Off", 0);
    for (int a : { 2, 4, 8, 16 }) {
        ComboAdd(p, IDC_AF, std::to_wstring(a) + L"x", a);
    }
    ComboSelectData(p, IDC_AF, g_set.af, 0);

    // FPS cap: the display's own refresh rate (detected), unlimited or a number of frames
    FillFpsCap(g_set.fpscap < 0 ? -1 : g_set.fpscap);
    ComboAdd(p, IDC_VSYNC, L"Off", 0);
    ComboAdd(p, IDC_VSYNC, L"On (no tearing)", 1);
    ComboAdd(p, IDC_VSYNC, L"Adaptive", 2);
    ComboSelectData(p, IDC_VSYNC, g_set.vsync, 1);

    SendDlgItemMessageW(p, IDC_FOV, TBM_SETRANGE, TRUE, MAKELPARAM(60, 120));
    SendDlgItemMessageW(p, IDC_FOV, TBM_SETPOS, TRUE, std::max(60, std::min(120, g_set.fov)));
    UpdateFovLabel();

    ComboAdd(p, IDC_TEXQ, L"Low (half size)", 0);
    ComboAdd(p, IDC_TEXQ, L"High (full size)", 1);
    ComboSelectData(p, IDC_TEXQ, g_set.texq, 1);
    CheckDlgButton(p, IDC_OVERRIDES, g_set.overrides ? BST_CHECKED : BST_UNCHECKED);
    ComboAdd(p, IDC_COLORBLIND, L"Off", 0);
    ComboAdd(p, IDC_COLORBLIND, L"Deuteranopia", 1);
    ComboAdd(p, IDC_COLORBLIND, L"Protanopia", 2);
    ComboAdd(p, IDC_COLORBLIND, L"Tritanopia", 3);
    ComboSelectData(p, IDC_COLORBLIND, g_set.colorblind, 0);
    CheckDlgButton(p, IDC_SHADERS, g_set.shaders ? BST_CHECKED : BST_UNCHECKED);
}

static void InitMpPage(HWND p)
{
    g_mp = p;
    int id = g_set.mpmode == "host" ? IDC_MP_HOST : (g_set.mpmode == "join" ? IDC_MP_JOIN : IDC_MP_OFF);
    CheckRadioButton(p, IDC_MP_OFF, IDC_MP_JOIN, id);
    SetDlgItemTextW(p, IDC_MP_NAME, widen(g_set.name).c_str());
    SendDlgItemMessageW(p, IDC_MP_NAME, EM_LIMITTEXT, netinfo::kMaxName, 0);
    SetDlgItemTextW(p, IDC_MP_SERVERNAME, widen(g_set.serverName).c_str());
    SendDlgItemMessageW(p, IDC_MP_SERVERNAME, EM_LIMITTEXT, 60, 0);
    SetDlgItemTextW(p, IDC_MP_HOSTPW, widen(g_set.hostPassword).c_str());
    SendDlgItemMessageW(p, IDC_MP_HOSTPW, EM_SETPASSWORDCHAR, 0x25CF, 0);
    SendDlgItemMessageW(p, IDC_MP_HOSTPW, EM_LIMITTEXT, 64, 0);
    CheckDlgButton(p, IDC_MP_PWON, g_set.usePassword ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemInt(p, IDC_PORT, g_set.port, FALSE);
    SetDlgItemInt(p, IDC_MP_MAXPLAYERS, std::max(0, g_set.maxPlayers), FALSE);
    CheckDlgButton(p, IDC_MP_GUESTCHARS, g_set.guestChars ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(p, IDC_MP_FRIENDLY, g_set.friendlyFire ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(p, IDC_MP_EXPANDEDAI, g_set.expandedAI ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(p, IDC_MP_SOLOAI, g_set.expandedAI ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemInt(p, IDC_MP_AFK, std::max(0, g_set.afkTimer), FALSE);
    CheckDlgButton(p, IDC_MP_ADVANCED, g_set.mpAdvanced ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(p, IDC_LAN, g_set.lan ? BST_CHECKED : BST_UNCHECKED);
    SetAddrText(g_set.addr);
    if (!g_set.code.empty() && g_set.addr.empty()) {
        // the Steam server picked last time (joined again if it is still there)
        g_pick = ServerEntry();
        g_pick.addr = g_set.code;
        g_pick.steam = true;
        g_havePick = true;
    }
    CheckDlgButton(p, IDC_SHAREMODS, g_set.shareMods ? BST_CHECKED : BST_UNCHECKED);
    // the part below moves down while the host options are open
    RECT adv = { 0, 0, 0, 58 };
    MapDialogRect(p, &adv);
    g_advShift = adv.bottom;
    for (int c : { IDC_SHAREMODS, IDC_MP_MODWARN, IDC_MP_SERVERSLBL, IDC_MP_REFRESH, IDC_MP_SERVERS }) {
        RECT r;
        GetWindowRect(GetDlgItem(p, c), &r);
        MapWindowPoints(NULL, p, (POINT*)&r, 2);
        g_mpRects[c] = r;
    }
    // the server list: dark rows (server names can be coloured)
    HWND lv = GetDlgItem(p, IDC_MP_SERVERS);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ListView_SetBkColor(lv, RGB(255, 255, 255));
    ListView_SetTextBkColor(lv, RGB(255, 255, 255));
    ListView_SetTextColor(lv, RGB(20, 20, 20));
    RECT lr;
    GetClientRect(lv, &lr);
    const int w = lr.right - lr.left - GetSystemMetrics(SM_CXVSCROLL); // (room for the scroll bar)
    const wchar_t* cols[] = { L"Server", L"Players", L"Status", L"Ping" };
    const int widths[] = { w * 46 / 100, w * 14 / 100, w * 26 / 100, w * 14 / 100 - 2 };
    for (int i = 0; i < 4; i++) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = (LPWSTR)cols[i];
        c.cx = widths[i];
        ListView_InsertColumn(lv, i, &c);
    }
    g_link.Open();
    UpdateMpEnable();
    ShowServerDetails();
    RefreshServers();
    SetTimer(p, 1, 25, NULL); // (answers are timed when read: often, for a true ping)
}

// ---- Character page ----
static HWND g_ch = NULL;

// the mods that are on right now (their skins, clothes and models are offered)
static std::vector<std::string> ModsNow()
{
    return Mods_Enabled(g_exDir, IniPath());
}

// Model list: 0 Rabbit, 1 Wolf, kModelBase+n a custom model (shared/looks.h)
static const LPARAM kModelBase = 100;
static std::vector<LookModel> g_customModels;

static std::string ModelKey() // "rabbit", "wolf" or "rabbit/Name"
{
    const LPARAM d = ComboData(g_ch, IDC_CH_MODEL);
    if (d >= kModelBase && d < kModelBase + (LPARAM)g_customModels.size()) {
        return g_customModels[d - kModelBase].key;
    }
    return d == 1 ? "wolf" : "rabbit";
}

static std::string ModelBase() // the creature: "rabbit" | "wolf"
{
    return Looks_Base(ModelKey());
}

static const LookModel* CustomModel()
{
    const LPARAM d = ComboData(g_ch, IDC_CH_MODEL);
    return d >= kModelBase && d < kModelBase + (LPARAM)g_customModels.size() ? &g_customModels[d - kModelBase] : nullptr;
}

static void FillModels(const std::string& want)
{
    SendDlgItemMessageW(g_ch, IDC_CH_MODEL, CB_RESETCONTENT, 0, 0);
    ComboAdd(g_ch, IDC_CH_MODEL, L"Rabbit", 0);
    ComboAdd(g_ch, IDC_CH_MODEL, L"Wolf", 1);
    LPARAM sel = Looks_Base(want) == "wolf" ? 1 : 0; // (a model from a mod that is off: its creature)
    g_customModels = Looks_Models(narrow(g_exDir), ModsNow());
    for (size_t i = 0; i < g_customModels.size(); i++) {
        ComboAdd(g_ch, IDC_CH_MODEL, widen(g_customModels[i].label).c_str(), kModelBase + (LPARAM)i);
        if (_stricmp(want.c_str(), g_customModels[i].key.c_str()) == 0) {
            sel = kModelBase + (LPARAM)i;
        }
    }
    ComboSelectData(g_ch, IDC_CH_MODEL, sel, 0);
}


static bool FindSkinFile(const SkinInfo& s, std::wstring& out)
{
    // Overrides (either name, png or jpg), then the original HD file in Data
    std::wstring oss = widen(s.path).substr(9); // strip "Textures/"
    std::wstring base[2] = { oss.substr(0, oss.rfind(L'.')), std::wstring(s.hdFile).substr(0, std::wstring(s.hdFile).rfind(L'.')) };
    for (auto& b : base) {
        for (const wchar_t* e : { L".png", L".jpg" }) {
            std::wstring p = g_exDir + L"Overrides\\Textures\\" + b + e;
            if (PathFileExistsW(p.c_str())) {
                out = p;
                return true;
            }
        }
    }
    for (const std::wstring& f : { std::wstring(s.hdFile), oss }) {
        std::wstring p = g_exeDir + L"Data\\Textures\\" + f;
        if (PathFileExistsW(p.c_str())) {
            out = p;
            return true;
        }
    }
    return false;
}

// skin list item data: 0..15 built-in (kSkins), kCustomBase+n player-made, kNewSkin "New skin..."
static const LPARAM kCustomBase = 1000, kNewSkin = 900, kRandomSkin = 800; // 800 = "Random": one nobody else in the level wears
static const LPARAM kDefaultSkin = 700; // "Default": the skin the level gives its player
static std::vector<CustomSkin> g_customSkins;
static std::string g_lastSkin; // engine path of the last real choice (to go back to if "New skin..." is cancelled)

static std::string SkinPathOf(LPARAM d)
{
    if (d >= 0 && d < (LPARAM)(sizeof(kSkins) / sizeof(kSkins[0]))) {
        return kSkins[d].path;
    }
    if (d == kRandomSkin) {
        return "random";
    }
    if (d == kDefaultSkin) {
        return "default";
    }
    if (d >= kCustomBase && d < kCustomBase + (LPARAM)g_customSkins.size()) {
        return g_customSkins[d - kCustomBase].enginePath;
    }
    return "";
}

static std::vector<PreviewClothFile> PreviewClothes(); // the Clothes choice's images (below)

static void UpdatePreview()
{
    // animated 3D model (preview.cpp); greyed out while "Default" is ticked
    LPARAM d = ComboData(g_ch, IDC_CH_SKIN);
    std::wstring file;
    if (d == kRandomSkin || d == kDefaultSkin) { // Random / Default: show the model with its first skin
        const std::string m = ModelBase();
        for (const auto& k : kSkins) {
            if (m == k.model && FindSkinFile(k, file)) {
                break;
            }
            file.clear();
        }
    } else if (d >= kCustomBase && d < kCustomBase + (LPARAM)g_customSkins.size()) {
        file = g_customSkins[d - kCustomBase].file;
    } else if (!(d >= 0 && d < (LPARAM)(sizeof(kSkins) / sizeof(kSkins[0])) && FindSkinFile(kSkins[d], file))) {
        file.clear();
    }
    const LookModel* cm = CustomModel();
    if (cm && d == kDefaultSkin && !cm->skin.empty()) {
        file = widen(cm->skin); // a custom model's own skin
        std::replace(file.begin(), file.end(), L'/', L'\\');
    }
    const std::string model = ModelBase();
    const bool def = IsDlgButtonChecked(g_ch, IDC_CH_DEFAULT) == BST_CHECKED;
    Preview_Set(GetDlgItem(g_ch, IDC_CH_PREVIEW), cm ? cm->key : model, file,
                def || model == "wolf" ? std::vector<PreviewClothFile>() : PreviewClothes(), def, cm ? cm->dir : std::string());
}

static void FillSkins(const std::string& wantPath)
{
    SendDlgItemMessageW(g_ch, IDC_CH_SKIN, CB_RESETCONTENT, 0, 0);
    const std::string model = ModelBase();
    int sel = 0, n = 0;
    ComboAdd(g_ch, IDC_CH_SKIN, L"Random", kRandomSkin);
    ComboAdd(g_ch, IDC_CH_SKIN, L"Default", kDefaultSkin);
    if (_stricmp(wantPath.c_str(), "random") == 0) {
        sel = 0;
    } else if (_stricmp(wantPath.c_str(), "default") == 0) {
        sel = 1;
    } else {
        sel = 2; // the first real skin, unless the one asked for is found below
    }
    for (int i = 0; i < (int)(sizeof(kSkins) / sizeof(kSkins[0])); i++) {
        std::wstring f;
        if (model != kSkins[i].model || !FindSkinFile(kSkins[i], f)) {
            continue;
        }
        int idx = ComboAdd(g_ch, IDC_CH_SKIN, kSkins[i].label, i);
        if (_stricmp(wantPath.c_str(), kSkins[i].path) == 0) {
            sel = idx;
        }
        n++;
    }
    g_customSkins = CustomSkins_List(ModsNow());
    for (size_t i = 0; i < g_customSkins.size(); i++) {
        if (g_customSkins[i].model != model) {
            continue;
        }
        int idx = ComboAdd(g_ch, IDC_CH_SKIN, g_customSkins[i].label, kCustomBase + (LPARAM)i);
        if (_stricmp(wantPath.c_str(), g_customSkins[i].enginePath.c_str()) == 0) {
            sel = idx;
        }
    }
    ComboAdd(g_ch, IDC_CH_SKIN, L"New skin...", kNewSkin);
    SendDlgItemMessageW(g_ch, IDC_CH_SKIN, CB_SETCURSEL, sel, 0);
    g_lastSkin = SkinPathOf(ComboData(g_ch, IDC_CH_SKIN));
    UpdatePreview();
}

// "New skin...": pick an image, it is copied (as PNG) into LugaruEX\Data\Textures\Custom
static void NewSkin()
{
    const std::string model = ModelBase();
    CustomSkin s;
    if (CustomSkins_Import(g_main, model, s)) {
        FillSkins(s.enginePath);
    } else {
        FillSkins(g_lastSkin);
    }
}


// "Clothes": outfits made of the game's own clothing textures (wolves wear none),
// Random (a different outfit each level), and the player's own images
struct ClothesItem
{
    std::string key;
    std::wstring label;
};
// Default, Random, then every outfit of shared/outfits.h (the game's own list)
static const std::vector<ClothesItem>& ClothesItems()
{
    static std::vector<ClothesItem> v;
    if (v.empty()) {
        v.push_back({ "default", L"Default" });
        v.push_back({ "random", L"Random" });
        for (const OutfitDef& o : Outfits()) {
            v.push_back({ o.key, widen(o.label) });
        }
    }
    return v;
}
static const LPARAM kClothesCustomBase = 2000, kNewClothes = 1900;
static std::vector<CustomSkin> g_customClothes;
static std::string g_lastClothes = "default"; // to go back to if "New clothes..." is cancelled

static std::string ClothesKeyOf(LPARAM d)
{
    if (d >= 0 && d < (LPARAM)ClothesItems().size()) {
        return ClothesItems()[d].key;
    }
    if (d >= kClothesCustomBase && d < kClothesCustomBase + (LPARAM)g_customClothes.size()) {
        return "custom:" + g_customClothes[d - kClothesCustomBase].enginePath;
    }
    return "default";
}

static void FillClothes(const std::string& want)
{
    SendDlgItemMessageW(g_ch, IDC_CH_CLOTHES, CB_RESETCONTENT, 0, 0);
    int sel = 0;
    for (int i = 0; i < (int)ClothesItems().size(); i++) {
        const int idx = ComboAdd(g_ch, IDC_CH_CLOTHES, ClothesItems()[i].label.c_str(), i);
        if (_stricmp(want.c_str(), ClothesItems()[i].key.c_str()) == 0) {
            sel = idx;
        }
    }
    g_customClothes.clear();
    for (const CustomSkin& c : CustomSkins_List(ModsNow())) {
        if (c.model == "clothes") {
            g_customClothes.push_back(c);
        }
    }
    for (size_t i = 0; i < g_customClothes.size(); i++) {
        const int idx = ComboAdd(g_ch, IDC_CH_CLOTHES, g_customClothes[i].label, kClothesCustomBase + (LPARAM)i);
        if (_stricmp(want.c_str(), ("custom:" + g_customClothes[i].enginePath).c_str()) == 0) {
            sel = idx;
        }
    }
    ComboAdd(g_ch, IDC_CH_CLOTHES, L"New clothes...", kNewClothes);
    SendDlgItemMessageW(g_ch, IDC_CH_CLOTHES, CB_SETCURSEL, sel, 0);
    g_lastClothes = ClothesKeyOf(ComboData(g_ch, IDC_CH_CLOTHES));
    UpdatePreview();
}

static std::vector<PreviewClothFile> PreviewClothes()
{
    std::vector<PreviewClothFile> out;
    if (!g_ch) {
        return out;
    }
    const LPARAM d = ComboData(g_ch, IDC_CH_CLOTHES);
    if (d >= kClothesCustomBase && d < kClothesCustomBase + (LPARAM)g_customClothes.size()) {
        out.push_back({ g_customClothes[d - kClothesCustomBase].file, 1, 1, 1 }); // painted as is
        return out;
    }
    for (const PreviewCloth& c : PreviewCore_Outfit(ClothesKeyOf(d))) {
        // the game's own image: an override (png or jpg) first, then Data
        std::wstring rel = widen(c.tex);
        std::replace(rel.begin(), rel.end(), L'/', L'\\');
        const std::wstring base = rel.substr(0, rel.rfind(L'.'));
        std::wstring found;
        for (const std::wstring& p : { g_exDir + L"Overrides\\" + base + L".png", g_exDir + L"Overrides\\" + base + L".jpg", g_exeDir + L"Data\\" + rel,
                                       g_exeDir + L"Data\\" + base + L".jpg" }) {
            if (found.empty() && PathFileExistsW(p.c_str())) {
                found = p;
            }
        }
        if (!found.empty()) {
            out.push_back({ found, c.r, c.g, c.b });
        }
    }
    return out;
}

static void UpdateCharEnable()
{
    const bool def = IsDlgButtonChecked(g_ch, IDC_CH_DEFAULT) == BST_CHECKED;
    EnableWindow(GetDlgItem(g_ch, IDC_CH_MODEL), !def);
    EnableWindow(GetDlgItem(g_ch, IDC_CH_SKIN), !def);
    // clothes follow the same rules, and wolves wear none
    EnableWindow(GetDlgItem(g_ch, IDC_CH_CLOTHES), !def && ModelBase() != "wolf");
    UpdatePreview();
}

static void InitCharPage(HWND p)
{
    g_ch = p;
    CheckDlgButton(p, IDC_CH_DEFAULT, g_set.chDefault ? BST_CHECKED : BST_UNCHECKED);
    FillModels(g_set.chModel);
    FillSkins(g_set.chSkin);
    FillClothes(g_set.chClothes);
    UpdateCharEnable();
}

static INT_PTR CALLBACK PageProc(HWND p, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg != WM_INITDIALOG) {
        if (p == g_ct && g_ct) {
            if (INT_PTR r = Controls_Proc(p, msg, wp, lp)) {
                return r;
            }
            return FALSE;
        }
        if (p == g_md && g_md) {
            if (INT_PTR r = Mods_Proc(p, msg, wp, lp)) {
                return r;
            }
            return FALSE;
        }
        if (p == g_lc && g_lc) {
            if (INT_PTR r = Local_Proc(p, msg, wp, lp)) {
                return r;
            }
            return FALSE;
        }
        if (p == g_mp && g_mp) {
            if (INT_PTR r = MpProc(p, msg, wp, lp)) {
                return r;
            }
            return FALSE;
        }
    }
    switch (msg) {
        case WM_INITDIALOG:
            EnableThemeDialogTexture(p, ETDT_ENABLETAB);
            if (lp == IDD_PAGE_GFX) {
                InitGfxPage(p);
            } else if (lp == IDD_PAGE_CHAR) {
                InitCharPage(p);
            } else if (lp == IDD_PAGE_CONTROLS) {
                g_ct = p;
                Controls_Init(p, g_exeDir, IniPath());
            } else if (lp == IDD_PAGE_MODS) {
                g_md = p;
                Mods_Init(p, g_exDir, IniPath());
            } else if (lp == IDD_PAGE_LOCAL) {
                g_lc = p;
                Local_Init(p, IniPath());
            } else {
                InitMpPage(p);
            }
            return TRUE;
        case WM_HSCROLL:
            if ((HWND)lp == GetDlgItem(p, IDC_FOV)) {
                UpdateFovLabel();
            }
            return TRUE;
        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (p == g_gfx) {
                if (id == IDC_MONITOR && code == CBN_SELCHANGE) {
                    FillResolutions(0, 0);
                    FillRefresh(0);
                } else if (id == IDC_RES && code == CBN_SELCHANGE) {
                    const bool custom = ComboData(p, IDC_RES) == CUSTOM_RES;
                    EnableWindow(GetDlgItem(p, IDC_CUSTW), custom);
                    EnableWindow(GetDlgItem(p, IDC_CUSTH), custom);
                    if (!custom) {
                        int w, h;
                        GetSelectedRes(w, h);
                        SetDlgItemInt(p, IDC_CUSTW, w, FALSE);
                        SetDlgItemInt(p, IDC_CUSTH, h, FALSE);
                    }
                    UpdateAspectLabel();
                    FillRefresh((int)ComboData(p, IDC_REFRESH));
                } else if (id == IDC_REFRESH && code == CBN_SELCHANGE) {
                    FillFpsCap(-2);
                } else if ((id == IDC_CUSTW || id == IDC_CUSTH) && code == EN_CHANGE) {
                    UpdateAspectLabel();
                } else if (id == IDC_MODE && code == CBN_SELCHANGE) {
                    FillRefresh((int)ComboData(p, IDC_REFRESH));
                }
            } else if (p == g_ch) {
                if (id == IDC_CH_DEFAULT) {
                    UpdateCharEnable();
                } else if (id == IDC_CH_MODEL && code == CBN_SELCHANGE) {
                    FillSkins("");
                    UpdateCharEnable(); // wolves: no clothes
                } else if (id == IDC_CH_SKIN && code == CBN_SELCHANGE) {
                    if (ComboData(g_ch, IDC_CH_SKIN) == kNewSkin) {
                        NewSkin();
                    } else {
                        g_lastSkin = SkinPathOf(ComboData(g_ch, IDC_CH_SKIN));
                        UpdatePreview();
                    }
                } else if (id == IDC_CH_CLOTHES && code == CBN_SELCHANGE) {
                    if (ComboData(g_ch, IDC_CH_CLOTHES) == kNewClothes) {
                        // pick an image: it is saved as LugaruEX\Data\Textures\Custom\clothes\<name>.png
                        CustomSkin made;
                        if (CustomSkins_Import(g_main, "clothes", made)) {
                            FillClothes("custom:" + made.enginePath);
                        } else {
                            FillClothes(g_lastClothes);
                        }
                    } else {
                        g_lastClothes = ClothesKeyOf(ComboData(g_ch, IDC_CH_CLOTHES));
                        UpdatePreview();
                    }
                }
            }
            return TRUE;
        }
    }
    return FALSE;
}

static std::string ClothesKeyOf(LPARAM d);
static void ShowPage(int i)
{
    if (i == 1 && g_ch) {
        // the mods may have changed on the Mods tab: their skins, clothes and models come and go
        const std::string model = ModelKey(), skin = SkinPathOf(ComboData(g_ch, IDC_CH_SKIN));
        const std::string clothes = ClothesKeyOf(ComboData(g_ch, IDC_CH_CLOTHES));
        FillModels(model);
        FillSkins(skin);
        FillClothes(clothes);
        UpdateCharEnable();
    }
    ShowWindow(g_gfx, i == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_ch, i == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_ct, i == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_lc, i == 3 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_mp, i == 4 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_md, i == 5 ? SW_SHOW : SW_HIDE);
}

static bool CollectSettings()
{
    Settings& s = g_set;
    s.monitor = std::max(0, ComboSel(g_gfx, IDC_MONITOR));
    int w, h;
    GetSelectedRes(w, h);
    if (w < 640 || h < 480 || w > 16384 || h > 16384) {
        MessageBoxW(g_main, L"Please enter a resolution of at least 640 x 480.", L"LugaruEX", MB_ICONWARNING);
        return false;
    }
    s.width = w;
    s.height = h;
    LPARAM mode = ComboData(g_gfx, IDC_MODE);
    s.mode = mode == 2 ? "fullscreen" : (mode == 0 ? "windowed" : "borderless");
    s.refresh = 0; // the display's own / best refresh rate (the choice was removed)
    s.msaa = (int)std::max<LPARAM>(0, ComboData(g_gfx, IDC_MSAA));
    s.af = (int)std::max<LPARAM>(0, ComboData(g_gfx, IDC_AF));
    s.vsync = (int)std::max<LPARAM>(0, ComboData(g_gfx, IDC_VSYNC));
    // simulation rate is always automatic now (SimRate in the ini is kept for experts)
    s.fpscap = (int)ComboData(g_gfx, IDC_FPSCAP); // -1 = match display refresh, 0 = unlimited, n = n fps
    s.fov = (int)SendDlgItemMessageW(g_gfx, IDC_FOV, TBM_GETPOS, 0, 0);
    s.texq = (int)std::max<LPARAM>(0, ComboData(g_gfx, IDC_TEXQ));
    s.overrides = IsDlgButtonChecked(g_gfx, IDC_OVERRIDES) == BST_CHECKED;
    s.colorblind = (int)std::max<LPARAM>(0, ComboData(g_gfx, IDC_COLORBLIND));
    s.lan = IsDlgButtonChecked(g_mp, IDC_LAN) == BST_CHECKED;
    s.shareMods = IsDlgButtonChecked(g_mp, IDC_SHAREMODS) == BST_CHECKED;
    s.autoUpdate = IsDlgButtonChecked(g_main, IDC_AUTOUPDATE) == BST_CHECKED;
    s.tab = std::max(0, TabCtrl_GetCurSel(g_tabs));
    s.shaders = IsDlgButtonChecked(g_gfx, IDC_SHADERS) == BST_CHECKED;

    s.mpmode = IsDlgButtonChecked(g_mp, IDC_MP_HOST) == BST_CHECKED ? "host" : (IsDlgButtonChecked(g_mp, IDC_MP_JOIN) == BST_CHECKED ? "join" : "off");
    int port = GetDlgItemInt(g_mp, IDC_PORT, NULL, FALSE);
    s.port = (port > 0 && port < 65536) ? port : 27015;
    // host: through Steam unless LAN / VPN is ticked; connect: a typed IP is direct, a
    // server picked in the list goes the way it was found
    const std::string typed = MpText(IDC_ADDR);
    if (s.mpmode == "join") {
        s.addr = typed;
        const bool steamPick = typed.empty() && g_havePick && g_pick.steam;
        s.steam = steamPick ? 1 : 0;
        s.code = steamPick ? g_pick.addr : "";
    } else {
        s.addr = typed;
        s.steam = s.lan ? 0 : 1;
    }
    auto text = [&](int id) {
        wchar_t b[256] = {};
        GetDlgItemTextW(g_mp, id, b, 256);
        return trim(narrow(b));
    };
    s.name = netinfo::CleanName(text(IDC_MP_NAME)); // (empty = User)
    s.serverName = netinfo::CleanText(text(IDC_MP_SERVERNAME), netinfo::kMaxServerName, true);
    s.hostPassword = text(IDC_MP_HOSTPW);
    s.usePassword = MpCheck(IDC_MP_PWON) ? 1 : 0;
    if (s.mpmode == "host" && s.usePassword && s.hostPassword.empty()) {
        MessageBoxW(g_main, L"Type a password under Host options, or untick Password.", L"LugaruEX", MB_ICONWARNING);
        return false;
    }
    const int maxp = (int)GetDlgItemInt(g_mp, IDC_MP_MAXPLAYERS, NULL, FALSE);
    s.maxPlayers = maxp <= 0 ? 0 : std::max(2, std::min(250, maxp)); // 0 = no limit
    s.guestChars = IsDlgButtonChecked(g_mp, IDC_MP_GUESTCHARS) == BST_CHECKED;
    s.friendlyFire = IsDlgButtonChecked(g_mp, IDC_MP_FRIENDLY) == BST_CHECKED;
    s.expandedAI = IsDlgButtonChecked(g_mp, IDC_MP_EXPANDEDAI) == BST_CHECKED;
    {
        const int afk = (int)GetDlgItemInt(g_mp, IDC_MP_AFK, NULL, FALSE);
        s.afkTimer = afk <= 0 ? 0 : std::max(10, std::min(3600, afk)); // 0 = off
    }
    s.mpAdvanced = IsDlgButtonChecked(g_mp, IDC_MP_ADVANCED) == BST_CHECKED;
    s.chDefault = IsDlgButtonChecked(g_ch, IDC_CH_DEFAULT) == BST_CHECKED;
    s.chModel = ModelKey();
    LPARAM sk = ComboData(g_ch, IDC_CH_SKIN);
    s.chSkin = SkinPathOf(sk);
    s.chClothes = ClothesKeyOf(ComboData(g_ch, IDC_CH_CLOTHES));
    if (s.mpmode == "join" && s.addr.empty() && s.code.empty()) {
        MessageBoxW(g_main, L"Pick a server from the list, or type the host's IP (LAN / VPN).", L"LugaruEX", MB_ICONWARNING);
        return false;
    }
    return true;
}

// the listed server this Play would join (Steam: by code; direct: by address)
static const ServerEntry* JoinTarget()
{
    const bool steam = g_set.steam != 0;
    const std::string want = steam ? g_set.code : g_set.addr;
    if (want.empty()) {
        return nullptr;
    }
    const std::vector<ServerEntry>& pool = steam ? g_steamServers : g_servers.Entries();
    for (const ServerEntry& e : pool) {
        if (!e.answered) {
            continue;
        }
        if (e.addr == want || (!steam && want.find(':') == std::string::npos && e.addr.rfind(want + ":", 0) == 0)) {
            return &e;
        }
    }
    return nullptr;
}

// Play with Join: a server known to run another LugaruEX version can't be joined
static bool CheckJoinVersion()
{
    if (g_set.mpmode != "join") {
        return true;
    }
    const ServerEntry* e = JoinTarget();
    if (e && e->info.version != EX_LAUNCHER_VERSION) {
        MessageBoxW(g_main,
                    (L"\"" + widen(netinfo::StripColours(e->info.name)) + L"\" runs LugaruEX " + widen(e->info.version) + L", you have " +
                     widen(EX_LAUNCHER_VERSION) + L".\n\nBoth need the same version to play together.")
                        .c_str(),
                    L"LugaruEX - different version", MB_ICONWARNING);
        return false;
    }
    return true;
}

// Play with Join and Mod share on: the host's mods will be downloaded and used here
static bool ConfirmModShareJoin()
{
    if (g_set.mpmode != "join" || !g_set.shareMods) {
        return true;
    }
    const ServerEntry* known = JoinTarget();
    if (known && !known->info.shareMods) {
        return true; // that server shares no mods
    }
    if (known && known->info.version != EX_LAUNCHER_VERSION) {
        return true; // (the version question comes first)
    }
    std::wstring q;
    if (known) {
        q = L"\"" + widen(netinfo::StripColours(known->info.name)) + L"\" shares " + std::to_wstring(known->info.mods) +
            (known->info.mods == 1 ? L" mod" : L" mods") + L". With Mod share on, they are downloaded and used in your game.";
    } else {
        q = L"Mod share is on: if this server shares mods, they are downloaded and used in your game.";
    }
    q += L"\n\nOnly join servers of players you trust. Join?";
    return MessageBoxW(g_main, q.c_str(), L"LugaruEX - Mod share", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}

// ---- controller navigation (Steam Deck through Proton, couch play): D-pad / left stick
// move between controls, A selects, B closes a list, LB / RB switch tabs, Start plays
static void SendKey(WORD vk)
{
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

static std::wstring ClassOf(HWND h)
{
    wchar_t c[64] = {};
    GetClassNameW(h, c, 64);
    return c;
}

static HWND CurrentPage()
{
    HWND pages[] = { g_gfx, g_ch, g_ct, g_lc, g_mp, g_md };
    const int cur = TabCtrl_GetCurSel(g_tabs);
    return (cur >= 0 && cur < 6) ? pages[cur] : NULL;
}

// the page's controls in tab order, then Play! and Quit
static std::vector<HWND> Focusables()
{
    std::vector<HWND> v;
    HWND page = CurrentPage();
    for (HWND c = page ? GetWindow(page, GW_CHILD) : NULL; c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!IsWindowVisible(c) || !IsWindowEnabled(c)) {
            continue;
        }
        const std::wstring k = ClassOf(c);
        if (k == L"Button") {
            if ((GetWindowLongW(c, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX) {
                continue;
            }
        } else if (k != L"ComboBox" && k != L"Edit" && k != L"msctls_trackbar32" && k != L"SysListView32") {
            continue;
        }
        v.push_back(c);
    }
    v.push_back(GetDlgItem(g_main, IDOK));
    v.push_back(GetDlgItem(g_main, IDCANCEL));
    return v;
}

static void FocusOn(HWND h)
{
    SetFocus(h);
    SendMessageW(g_main, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS | UISF_HIDEACCEL), 0);
    if (ClassOf(h) == L"SysListView32" && ListView_GetNextItem(h, -1, LVNI_SELECTED) < 0) {
        ListView_SetItemState(h, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}

static void MoveFocus(int dir)
{
    const std::vector<HWND> v = Focusables();
    HWND f = GetFocus();
    int i = (int)(std::find(v.begin(), v.end(), f) - v.begin());
    if (i == (int)v.size() && f) { // e.g. the edit box inside a combo
        i = (int)(std::find(v.begin(), v.end(), GetParent(f)) - v.begin());
    }
    const int n = (int)v.size();
    i = (i >= n) ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n;
    FocusOn(v[i]);
}

static void PadNavigate()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId() || !g_tabs) {
        return; // only while this launcher (or one of its lists) is in front
    }
    bool held[4] = {}, a = false, b = false, x = false, lb = false, rb = false, start = false; // held: up, down, left, right
    for (auto& p : Pad_List()) {
        if (!p.connected) {
            continue;
        }
        held[0] |= p.btn[SDL_CONTROLLER_BUTTON_DPAD_UP] || p.ly < -0.6f;
        held[1] |= p.btn[SDL_CONTROLLER_BUTTON_DPAD_DOWN] || p.ly > 0.6f;
        held[2] |= p.btn[SDL_CONTROLLER_BUTTON_DPAD_LEFT] || p.lx < -0.6f;
        held[3] |= p.btn[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] || p.lx > 0.6f;
        auto pressed = [&](int k) { return p.btn[k] && !p.prevBtn[k]; };
        a |= pressed(SDL_CONTROLLER_BUTTON_A);
        b |= pressed(SDL_CONTROLLER_BUTTON_B);
        lb |= pressed(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        rb |= pressed(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        start |= pressed(SDL_CONTROLLER_BUTTON_START);
        x |= pressed(SDL_CONTROLLER_BUTTON_X);
    }
    if (g_updBusy) { // the update question (a message box) answers to A / B; nothing else meanwhile
        HWND fg = GetForegroundWindow();
        if (fg != g_main && (a || b)) {
            PostMessageW(fg, WM_COMMAND, a ? IDOK : IDCANCEL, 0);
        }
        return;
    }
    if (Controls_Capturing()) {
        if (Controls_CapturingKeys() && b) {
            Controls_CancelCapture(); // a controller can't type the key: B backs out
        }
        return; // the Controls tab takes the next button
    }
    if (start) {
        PostMessageW(g_main, WM_COMMAND, IDOK, 0);
        return;
    }
    if (lb || rb) {
        const int n = TabCtrl_GetItemCount(g_tabs), cur = TabCtrl_GetCurSel(g_tabs);
        const int next = (cur + (rb ? 1 : n - 1)) % n;
        TabCtrl_SetCurSel(g_tabs, next);
        ShowPage(next);
        const std::vector<HWND> v = Focusables();
        FocusOn(v.front());
        return;
    }
    HWND f = GetFocus();
    const std::wstring k = f ? ClassOf(f) : L"";
    const bool dropped = k == L"ComboBox" && SendMessageW(f, CB_GETDROPPEDSTATE, 0, 0);
    if (a) {
        if (!f || (GetAncestor(f, GA_ROOT) != g_main)) {
            MoveFocus(1);
        } else if (k == L"Button") {
            SendMessageW(f, BM_CLICK, 0, 0);
        } else if (k == L"ComboBox") {
            if (dropped) {
                SendKey(VK_RETURN);
            } else {
                SendMessageW(f, CB_SHOWDROPDOWN, TRUE, 0);
            }
        } else if (k == L"SysListView32") {
            if (CurrentPage() == g_md) {
                Mods_PadToggle();
            } else {
                Controls_ActivateRow();
            }
        }
    }
    if (x && f && k == L"SysListView32" && CurrentPage() == g_md) {
        Mods_PadDelete();
    }
    if (b && dropped) {
        SendMessageW(f, CB_SHOWDROPDOWN, FALSE, 0);
    }
    // directions, repeating while held
    static bool was[4] = {};
    static DWORD since[4] = {}, last[4] = {};
    const DWORD now = GetTickCount();
    for (int d = 0; d < 4; d++) {
        bool fire = false;
        if (held[d] && !was[d]) {
            fire = true;
            since[d] = last[d] = now;
        } else if (held[d] && now - since[d] > 400 && now - last[d] > 110) {
            fire = true;
            last[d] = now;
        }
        was[d] = held[d];
        if (!fire) {
            continue;
        }
        const bool vertical = d < 2, forward = (d == 1 || d == 3);
        if (dropped || (k == L"SysListView32" && vertical)) {
            if (vertical) {
                // lists: move inside, and leave them past the first / last row
                const int cnt = dropped ? (int)SendMessageW(f, CB_GETCOUNT, 0, 0) : ListView_GetItemCount(f);
                const int sel = dropped ? (int)SendMessageW(f, CB_GETCURSEL, 0, 0) : ListView_GetNextItem(f, -1, LVNI_SELECTED);
                if (!dropped && ((!forward && sel <= 0) || (forward && sel >= cnt - 1))) {
                    MoveFocus(forward ? 1 : -1);
                } else {
                    SendKey(forward ? VK_DOWN : VK_UP);
                }
            }
        } else if (!vertical && (k == L"msctls_trackbar32" || k == L"Edit")) {
            SendKey(forward ? VK_RIGHT : VK_LEFT);
        } else {
            MoveFocus(forward ? 1 : -1);
        }
    }
}


static bool LaunchEngine()
{
    std::wstring exe = g_exDir + L"LugaruEX.exe";
    if (!PathFileExistsW(exe.c_str())) {
        exe = g_exeDir + L"LugaruEX.exe"; // developer layout: engine next to the launcher
    }
    if (!PathFileExistsW(exe.c_str())) {
        MessageBoxW(g_main, (L"Could not find " + exe + L"\nPlease reinstall LugaruEX.").c_str(), L"LugaruEX", MB_ICONERROR);
        return false;
    }
    // pass through any extra command line arguments (e.g. -d for dev tools)
    std::wstring cmd = L"\"" + exe + L"\"";
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) {
        cmd += L" \"";
        cmd += argv[i];
        cmd += L"\"";
    }
    if (argv) {
        LocalFree(argv);
    }
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> cl(cmd.begin(), cmd.end());
    cl.push_back(0);
    // online: the link back to this window (kick / ban, password) and a typed password -
    // only for this game, in its environment, never written to a file
    const bool online = g_set.mpmode != "off";
    g_admin.heard = false;
    g_admin.players.clear();
    if (online && g_link.Open()) {
        SetEnvironmentVariableW(L"LUGARUEX_ADMIN", widen(g_link.NewSecret()).c_str());
    }
    if (online && g_set.mpmode == "join" && !g_joinPassword.empty()) {
        SetEnvironmentVariableW(L"LUGARUEX_JOIN_PASSWORD", widen(g_joinPassword).c_str());
    }
    const BOOL started = CreateProcessW(exe.c_str(), cl.data(), NULL, NULL, FALSE, 0, NULL, g_exeDir.c_str(), &si, &pi);
    SetEnvironmentVariableW(L"LUGARUEX_ADMIN", NULL);
    SetEnvironmentVariableW(L"LUGARUEX_JOIN_PASSWORD", NULL);
    if (!started) {
        MessageBoxW(g_main, L"Failed to start LugaruEX.exe.", L"LugaruEX", MB_ICONERROR);
        return false;
    }
    CloseHandle(pi.hThread);
    g_engineProcess = pi.hProcess; // (online: watched by the window; else waited for after it closes)
    return true;
}

// Play is off while the game runs (and while an update downloads)
static void UpdatePlayButton()
{
    EnableWindow(GetDlgItem(g_main, IDOK), !GameRunning() && !g_updBusy);
    SetDlgItemTextW(g_main, IDOK, GameRunning() ? L"Playing..." : L"Play!");
}

// online games keep this window open: who is on your server, passwords, and Play again after
static void WatchGame()
{
    if (!GameRunning()) {
        return;
    }
    if (g_link.Poll()) {
        UpdateHostStatus();
        ShowServerDetails();
    }
    if (WaitForSingleObject(g_engineProcess, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_engineProcess);
        g_engineProcess = NULL;
        g_admin.heard = false;
        g_admin.players.clear();
        UpdatePlayButton();
        UpdateHostStatus();
        FillServerList();
        RefreshServers();
        return;
    }
    // the server wants a password (none given, or the wrong one): ask here, send it to the game
    if (g_admin.heard && g_admin.role == 2 && g_admin.needPassword && !g_pwAsking && GetTickCount() - g_pwAskedAt > 1500) {
        g_pwAsking = true;
        std::string pw;
        FlashWindow(g_main, TRUE);
        const bool ok = AskPassword(g_admin.status.empty() ? L"This server needs a password." : widen(g_admin.status) + L".", pw);
        g_pwAsking = false;
        g_pwAskedAt = GetTickCount();
        g_admin.needPassword = false; // (until the game says so again)
        if (ok) {
            g_joinPassword = pw;
            g_link.Password(pw);
        }
    }
}

// the footer text next to Auto Update
static std::wstring VersionLabel()
{
    if (!PathFileExistsW((g_exeDir + L"Data").c_str())) {
        return L"No game Data folder here!";
    }
    return L"LugaruEX " + widen(EX_LAUNCHER_VERSION);
}

static INT_PTR CALLBACK MainProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_INITDIALOG: {
            g_main = d;
            HICON ic = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP));
            SendMessageW(d, WM_SETICON, ICON_BIG, (LPARAM)ic);
            SendMessageW(d, WM_SETICON, ICON_SMALL, (LPARAM)ic);
            BuildBanner(GetDlgItem(d, IDC_BANNER));

            g_tabs = GetDlgItem(d, IDC_TABS);
            TCITEMW ti = { TCIF_TEXT };
            ti.pszText = (LPWSTR)L"Graphics";
            TabCtrl_InsertItem(g_tabs, 0, &ti);
            ti.pszText = (LPWSTR)L"Character";
            TabCtrl_InsertItem(g_tabs, 1, &ti);
            ti.pszText = (LPWSTR)L"Controls";
            TabCtrl_InsertItem(g_tabs, 2, &ti);
            ti.pszText = (LPWSTR)L"Local";
            TabCtrl_InsertItem(g_tabs, 3, &ti);
            ti.pszText = (LPWSTR)L"Multiplayer";
            TabCtrl_InsertItem(g_tabs, 4, &ti);
            ti.pszText = (LPWSTR)L"Mods";
            TabCtrl_InsertItem(g_tabs, 5, &ti);

            CreateDialogParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_PAGE_GFX), d, PageProc, IDD_PAGE_GFX);
            CreateDialogParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_PAGE_CHAR), d, PageProc, IDD_PAGE_CHAR);
            CreateDialogParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_PAGE_CONTROLS), d, PageProc, IDD_PAGE_CONTROLS);
            CreateDialogParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_PAGE_LOCAL), d, PageProc, IDD_PAGE_LOCAL);
            CreateDialogParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_PAGE_MODS), d, PageProc, IDD_PAGE_MODS);
            CreateDialogParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_PAGE_MP), d, PageProc, IDD_PAGE_MP);
            RECT r;
            GetWindowRect(g_tabs, &r);
            MapWindowPoints(NULL, d, (POINT*)&r, 2);
            TabCtrl_AdjustRect(g_tabs, FALSE, &r);
            for (HWND pg : { g_gfx, g_ch, g_ct, g_lc, g_mp, g_md }) {
                SetWindowPos(pg, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, 0);
            }
            {
                // the tab that was open last time; test hook LUGARUEX_LAUNCHER_TAB=n: open on tab n (screenshots)
                const char* t = getenv("LUGARUEX_LAUNCHER_TAB");
                const int first = std::max(0, std::min(5, t ? atoi(t) : g_set.tab));
                TabCtrl_SetCurSel(g_tabs, first);
                ShowPage(first);
            }
            SetTimer(d, 7, 16, NULL); // controller polling for the Controls / Local tabs
            SetTimer(d, 9, 100, NULL); // the running game (online: status, passwords)

            CheckDlgButton(d, IDC_AUTOUPDATE, g_set.autoUpdate ? BST_CHECKED : BST_UNCHECKED);
            DragAcceptFiles(d, TRUE); // mod zips dropped anywhere on the window
            if (g_set.autoUpdate) { // look for a new LugaruEX meanwhile (the player is asked)
                std::thread(UpdateThread, d, g_set.updateRepo.empty() ? std::string(kUpdateRepo) : g_set.updateRepo).detach();
            }
            SetDlgItemTextW(d, IDC_VERSION, VersionLabel().c_str());
            return TRUE;
        }
        case WM_TIMER:
            if (wp == 7) {
                Pad_Update();
                Controls_Tick();
                Local_Tick();
                PadNavigate();
            } else if (wp == 9) {
                WatchGame();
            } else if (wp == 8 && g_updInfo.size > 0) { // download progress
                const int pct = (int)std::min<uint64_t>(100, g_updBytes * 100 / g_updInfo.size);
                SetDlgItemTextW(d, IDC_VERSION, (L"Downloading " + widen(g_updInfo.version) + L"... " + std::to_wstring(pct) + L"%").c_str());
            }
            return TRUE;
        case WM_UPDATE_STATE:
            if (wp == 3) { // a newer release: ask
                UpdateInfo* info = (UpdateInfo*)lp;
                g_updInfo = *info;
                delete info;
                if (g_updBusy) {
                    return TRUE;
                }
                g_updBusy = true;
                const std::wstring q = widen(g_updInfo.version) + L" is available. Current version is " + widen(CurrentVersion()) + L".\n\nUpdate now?";
                if (MessageBoxW(d, q.c_str(), L"LugaruEX Update", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
                    g_updBytes = 0;
                    EnableWindow(GetDlgItem(d, IDOK), FALSE);
                    SetDlgItemTextW(d, IDC_VERSION, (L"Downloading " + widen(g_updInfo.version) + L"...").c_str());
                    SetTimer(d, 8, 250, NULL);
                    std::thread(UpdateDownloadThread, d, narrow(g_exDir), g_updInfo).detach();
                } else {
                    g_updBusy = false;
                }
            } else if (wp == 2) { // downloaded: put it in place, the new launcher takes over
                KillTimer(d, 8);
                SetDlgItemTextW(d, IDC_VERSION, (L"Installing " + widen(g_updInfo.version) + L"...").c_str());
                SetCursor(LoadCursor(NULL, IDC_WAIT));
                if (CollectSettings() && SaveIni(g_set)) { // the new launcher opens with this session's choices
                    Controls_Save(IniPath());
                    Local_Save(IniPath());
                    Mods_Save(IniPath());
                }
                if (ApplyPendingUpdate()) {
                    EndDialog(d, IDCANCEL);
                    return TRUE;
                }
                g_updBusy = false;
                UpdatePlayButton();
                SetDlgItemTextW(d, IDC_VERSION, VersionLabel().c_str());
            } else if (wp == 0) {
                std::string* err = (std::string*)lp;
                KillTimer(d, 8);
                g_updBusy = false;
                UpdatePlayButton();
                SetDlgItemTextW(d, IDC_VERSION, VersionLabel().c_str());
                MessageBoxW(d, (L"The update could not be downloaded: " + widen(*err)).c_str(), L"LugaruEX Update", MB_ICONWARNING);
                delete err;
            }
            return TRUE;
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            std::vector<std::wstring> files;
            const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
            for (UINT i = 0; i < n; i++) {
                wchar_t f[MAX_PATH * 2];
                if (DragQueryFileW(drop, i, f, MAX_PATH * 2)) {
                    files.push_back(f);
                }
            }
            DragFinish(drop);
            TabCtrl_SetCurSel(g_tabs, 5); // show the Mods tab with the new mods
            ShowPage(5);
            Mods_Install(files);
            return TRUE;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
            if (di->CtlID == IDC_BANNER && g_banner) {
                HDC m = CreateCompatibleDC(di->hDC);
                HGDIOBJ o = SelectObject(m, g_banner);
                BitBlt(di->hDC, 0, 0, g_bannerW, g_bannerH, m, 0, 0, SRCCOPY);
                SelectObject(m, o);
                DeleteDC(m);
                return TRUE;
            }
            break;
        }
        case WM_NOTIFY: {
            NMHDR* n = (NMHDR*)lp;
            if (n->hwndFrom == g_tabs && n->code == TCN_SELCHANGE) {
                ShowPage(TabCtrl_GetCurSel(g_tabs));
            }
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                if (g_updBusy || GameRunning()) {
                    return TRUE; // updating (the game starts from the new launcher), or already playing
                }
                if (!CollectSettings() || !CheckJoinVersion() || !ConfirmModShareJoin()) {
                    return TRUE;
                }
                // a server known to be locked: its password first (a small window)
                g_joinPassword.clear();
                if (g_set.mpmode == "join") {
                    const ServerEntry* t = JoinTarget();
                    if (t && t->info.password &&
                        !AskPassword(L"\"" + widen(netinfo::StripColours(t->info.name)) + L"\" needs a password.", g_joinPassword)) {
                        return TRUE;
                    }
                }
                if (g_set.mpmode == "join" && !g_set.steam && !g_set.addr.empty()) {
                    RememberServer(g_set.addr); // the server list asks it next time
                }
                if (!SaveIni(g_set)) {
                    MessageBoxW(d, L"Could not write the settings file (LugaruEX\\LugaruEX.ini).", L"LugaruEX", MB_ICONWARNING);
                } else {
                    Controls_Save(IniPath()); // [Keys] [Pad]
                    Local_Save(IniPath());    // [Local]
                    Mods_Save(IniPath());     // [Mods]
                }
                if (LaunchEngine()) {
                    if (g_set.mpmode == "off") {
                        EndDialog(d, IDOK); // solo: the window goes (the game is waited for)
                    } else {
                        // online: stay open (who is on your server, passwords); Play again when it ends
                        UpdatePlayButton();
                        UpdateHostStatus();
                        CheckDlgButton(g_mp, IDC_MP_ADVANCED, BST_UNCHECKED); // (room for the player list)
                        UpdateMpEnable();
                        TabCtrl_SetCurSel(g_tabs, 4);
                        ShowPage(4);
                    }
                }
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(d, IDCANCEL);
                return TRUE;
            }
            break;
        case WM_DPICHANGED:
            // keep it simple: the dialog is created at the launch DPI
            break;
    }
    return FALSE;
}

// ----------------------------------------------------------------------------
// Installer (this program with a release zip appended = LugaruEX-<ver>-Setup.exe, or run
// from an unpacked release outside a game folder) and updates from GitHub

static std::string U8(const std::wstring& w)
{
    return narrow(w);
}

// the release zip appended to this exe: ... zip bytes, then "LGXPAYLD" + its offset (8 bytes)
static bool ExtractPayload(const std::wstring& exe, const std::string& zipOut)
{
    FILE* f = _wfopen(exe.c_str(), L"rb");
    if (!f) {
        return false;
    }
    bool ok = false;
    _fseeki64(f, 0, SEEK_END);
    const long long size = _ftelli64(f);
    char tag[8] = {};
    unsigned long long off = 0;
    if (size > 16 && _fseeki64(f, size - 16, SEEK_SET) == 0 && fread(tag, 1, 8, f) == 8 && fread(&off, 1, 8, f) == 8 &&
        memcmp(tag, "LGXPAYLD", 8) == 0 && off > 0 && (long long)off < size - 16) {
        FILE* o = fsu::Open(zipOut, "wb");
        if (o) {
            _fseeki64(f, (long long)off, SEEK_SET);
            std::vector<char> buf(1 << 16);
            long long left = size - 16 - (long long)off;
            ok = true;
            while (left > 0 && ok) {
                const size_t n = (size_t)std::min<long long>(left, (long long)buf.size());
                ok = fread(buf.data(), 1, n, f) == n && fwrite(buf.data(), 1, n, o) == n;
                left -= n;
            }
            ok = (fclose(o) == 0) && ok;
        }
    }
    fclose(f);
    return ok;
}

static std::vector<GameFolder> g_games;
static std::string g_package; // UTF-8 folder holding Lugaru.exe + LugaruEX\ to install
static HWND g_setup = NULL;
static std::atomic<uint64_t> g_dlBytes(0);
static std::atomic<bool> g_dlBusy(false);

static void FillGames(int select)
{
    HWND l = GetDlgItem(g_setup, IDC_SU_LIST);
    SendMessageW(l, LB_RESETCONTENT, 0, 0);
    for (const GameFolder& g : g_games) {
        std::wstring t = widen(g.kind) + L"   " + widen(g.path) + (g.hasLugaruEX ? L"   (update)" : L"");
        SendMessageW(l, LB_ADDSTRING, 0, (LPARAM)t.c_str());
    }
    if (!g_games.empty()) {
        SendMessageW(l, LB_SETCURSEL, std::max(0, std::min(select, (int)g_games.size() - 1)), 0);
    }
    const int s = (int)SendMessageW(l, LB_GETCURSEL, 0, 0);
    const bool steam = s >= 0 && g_games[s].steam;
    EnableWindow(GetDlgItem(g_setup, IDC_SU_STEAM), steam);
    SetDlgItemTextW(g_setup, IDC_SU_STATUS, g_games.empty() ? L"No game found." : L"");
}

static std::wstring PickFolder(HWND owner, const wchar_t* title = L"Choose your Lugaru folder (the one with Data in it)", const std::wstring& start = L"")
{
    std::wstring out;
    IFileOpenDialog* d = NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) {
        DWORD o = 0;
        d->GetOptions(&o);
        d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        d->SetTitle(title);
        IShellItem* at = NULL;
        if (!start.empty() && SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), NULL, IID_PPV_ARGS(&at)))) {
            d->SetFolder(at); // opens here
            at->Release();
        }
        if (SUCCEEDED(d->Show(owner))) {
            IShellItem* it = NULL;
            if (SUCCEEDED(d->GetResult(&it))) {
                PWSTR p = NULL;
                if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                    out = p;
                    CoTaskMemFree(p);
                }
                it->Release();
            }
        }
        d->Release();
    }
    return out;
}

static std::string g_freeDir; // where "Download game free" puts the game (the player picks it)

static void FreeDataThread(HWND d)
{
    const std::string dir = g_freeDir, zip = dir + "\\lugaru-data.zip";
    fsu::MakeDirs(dir);
    std::string err;
    bool ok = Http_GetFile(Setup_FreeDataUrl(), zip, &g_dlBytes, err) && Setup_UnpackFreeData(zip, dir, err);
    fsu::RemoveTree(zip);
    g_dlBusy = false;
    PostMessageW(d, WM_FREEDATA_DONE, ok ? 1 : 0, (LPARAM) new std::string(err));
}

// install into the chosen folder; a folder that needs admin rights: run this again elevated
static bool InstallInto(const GameFolder& g, bool steamButton, bool shortcut, HWND owner, bool elevated)
{
    if (!g.writable && !elevated) {
        std::wstring args = L"--setup-to \"" + widen(g.path) + L"\" --package \"" + widen(g_package) + L"\"";
        args += steamButton ? L"" : L" --no-steam";
        args += shortcut ? L"" : L" --no-shortcut";
        SHELLEXECUTEINFOW se = { sizeof(se) };
        se.fMask = SEE_MASK_NOCLOSEPROCESS;
        se.hwnd = owner;
        se.lpVerb = L"runas";
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(NULL, self, MAX_PATH);
        se.lpFile = self;
        se.lpParameters = args.c_str();
        se.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&se) || !se.hProcess) {
            return false;
        }
        WaitForSingleObject(se.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(se.hProcess, &code);
        CloseHandle(se.hProcess);
        return code == 0;
    }
    SetupOptions o;
    o.steamPlayButton = steamButton;
    o.shortcut = shortcut;
    std::string launcher, err;
    GameFolder w = g;
    w.writable = true;
    if (!Setup_Install(g_package, w, o, launcher, err)) {
        MessageBoxW(owner, (L"Installing failed: " + widen(err)).c_str(), L"LugaruEX Setup", MB_ICONERROR);
        return false;
    }
    return true;
}

static INT_PTR CALLBACK SetupProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_INITDIALOG: {
            g_setup = d;
            HICON ic = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_APP));
            SendMessageW(d, WM_SETICON, ICON_BIG, (LPARAM)ic);
            SendMessageW(d, WM_SETICON, ICON_SMALL, (LPARAM)ic);
            BuildBanner(GetDlgItem(d, IDC_BANNER));
            CheckDlgButton(d, IDC_SU_STEAM, BST_CHECKED);
            CheckDlgButton(d, IDC_SU_SHORTCUT, BST_CHECKED);
            FillGames(0);
            if (g_games.empty()) {
                PostMessageW(d, WM_NOGAME, 0, 0); // ask once the window is up
            }
            return TRUE;
        }
        case WM_NOGAME: {
            // nothing found: pick the folder, or get the free game
            TASKDIALOG_BUTTON b[] = { { 1001, L"Pick game folder" }, { 1002, L"Download game free" } };
            TASKDIALOGCONFIG c = { sizeof(c) };
            c.hwndParent = d;
            c.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
            c.dwCommonButtons = TDCBF_CLOSE_BUTTON;
            c.pszWindowTitle = L"LugaruEX Setup";
            c.pszMainIcon = TD_INFORMATION_ICON;
            c.pszMainInstruction = L"No game found";

            c.cButtons = 2;
            c.pButtons = b;
            int pick = 0;
            if (SUCCEEDED(TaskDialogIndirect(&c, &pick, NULL, NULL))) {
                if (pick == 1001) {
                    PostMessageW(d, WM_COMMAND, IDC_SU_BROWSE, 0);
                } else if (pick == 1002) {
                    PostMessageW(d, WM_COMMAND, IDC_SU_FREE, 0);
                }
            }
            return TRUE;
        }
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
            if (di->CtlID == IDC_BANNER && g_banner) {
                HDC m = CreateCompatibleDC(di->hDC);
                HGDIOBJ o = SelectObject(m, g_banner);
                BitBlt(di->hDC, 0, 0, g_bannerW, g_bannerH, m, 0, 0, SRCCOPY);
                SelectObject(m, o);
                DeleteDC(m);
                return TRUE;
            }
            break;
        }
        case WM_TIMER:
            if (g_dlBusy) {
                wchar_t t[128];
                swprintf(t, 128, L"Downloading game... %.1f MB", g_dlBytes / 1048576.0);
                SetDlgItemTextW(d, IDC_SU_STATUS, t);
            }
            return TRUE;
        case WM_FREEDATA_DONE: {
            std::string* err = (std::string*)lp;
            KillTimer(d, 1);
            EnableWindow(GetDlgItem(d, IDC_SU_FREE), TRUE);
            EnableWindow(GetDlgItem(d, IDOK), TRUE);
            if (wp) {
                g_games = Setup_FindGames();
                int sel = -1;
                for (int i = 0; i < (int)g_games.size(); i++) {
                    if (fsu::Lower(g_games[i].path) == fsu::Lower(g_freeDir)) {
                        sel = i;
                    }
                }
                if (sel < 0) { // a folder of the player's choosing: not one setup looks in by itself
                    const GameFolder g = Setup_Describe(g_freeDir);
                    if (!g.path.empty()) {
                        g_games.insert(g_games.begin(), g);
                    }
                    sel = 0;
                }
                FillGames(sel);
                SetDlgItemTextW(d, IDC_SU_STATUS, L"Game downloaded. Press Install.");
            } else {
                SetDlgItemTextW(d, IDC_SU_STATUS, (L"Download failed: " + widen(*err)).c_str());
            }
            delete err;
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_SU_LIST:
                    if (HIWORD(wp) == LBN_SELCHANGE) {
                        const int s = (int)SendDlgItemMessageW(d, IDC_SU_LIST, LB_GETCURSEL, 0, 0);
                        EnableWindow(GetDlgItem(d, IDC_SU_STEAM), s >= 0 && s < (int)g_games.size() && g_games[s].steam);
                    }
                    return TRUE;
                case IDC_SU_BROWSE: {
                    const std::wstring p = PickFolder(d);
                    if (p.empty()) {
                        return TRUE;
                    }
                    GameFolder g = Setup_Describe(U8(p));
                    if (g.path.empty()) {
                        MessageBoxW(d, L"No game in that folder.", L"LugaruEX Setup", MB_ICONWARNING);
                        return TRUE;
                    }
                    g_games.insert(g_games.begin(), g);
                    FillGames(0);
                    return TRUE;
                }
                case IDC_SU_FREE:
                    if (!g_dlBusy) {
                        // where the game goes: an empty (or new) folder as it is, else a "Lugaru" folder in it
                        const std::string def = Setup_FreeGameFolder();
                        const std::wstring p = PickFolder(d, L"Choose where to put the game", widen(fsu::Parent(def)));
                        if (p.empty()) {
                            return TRUE;
                        }
                        std::string dir = U8(p);
                        while (dir.size() > 3 && (dir.back() == '\\' || dir.back() == '/')) {
                            dir.pop_back();
                        }
                        if (fsu::IsDir(dir) && (!fsu::List(dir, true).empty() || !fsu::List(dir, false).empty())) {
                            dir = fsu::Join(dir, "Lugaru");
                        }
                        const std::string probe = fsu::Join(dir, ".lugaruex-write-test");
                        if (!fsu::MakeDirs(dir) || !fsu::WriteAll(probe, "x")) {
                            MessageBoxW(d, (L"Can't write to " + widen(dir) + L".\n\nPick a folder of your own, for example in Documents.").c_str(), L"LugaruEX Setup", MB_ICONWARNING);
                            return TRUE;
                        }
                        fsu::RemoveTree(probe);
                        g_freeDir = dir;
                        g_dlBusy = true;
                        g_dlBytes = 0;
                        EnableWindow(GetDlgItem(d, IDC_SU_FREE), FALSE);
                        EnableWindow(GetDlgItem(d, IDOK), FALSE);
                        SetTimer(d, 1, 250, NULL);
                        std::thread(FreeDataThread, d).detach();
                    }
                    return TRUE;
                case IDOK: {
                    const int s = (int)SendDlgItemMessageW(d, IDC_SU_LIST, LB_GETCURSEL, 0, 0);
                    if (s < 0 || s >= (int)g_games.size()) {
                        MessageBoxW(d, L"Pick a game folder first.", L"LugaruEX Setup", MB_ICONINFORMATION);
                        return TRUE;
                    }
                    const GameFolder g = g_games[s];
                    SetCursor(LoadCursor(NULL, IDC_WAIT));
                    if (InstallInto(g, IsDlgButtonChecked(d, IDC_SU_STEAM) == BST_CHECKED, IsDlgButtonChecked(d, IDC_SU_SHORTCUT) == BST_CHECKED, d, false)) {
                        const std::wstring launcher = widen(fsu::Join(fsu::Join(g.path, "LugaruEX"), Setup_LauncherName()));
                        MessageBoxW(d, (L"Installed in " + widen(g.path) + L".").c_str(), L"LugaruEX Setup", MB_ICONINFORMATION);
                        ShellExecuteW(NULL, L"open", launcher.c_str(), NULL, widen(g.path).c_str(), SW_SHOWNORMAL);
                        EndDialog(d, IDOK);
                    }
                    return TRUE;
                }
                case IDCANCEL:
                    EndDialog(d, IDCANCEL);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

// ---- updates: looked for when the launcher opens; the player is asked, then it is
// downloaded, put in place and the new launcher started

// this launcher's version (test hook LUGARUEX_TEST_UPDATE_FROM=<version>: pretend to be older)
static std::string CurrentVersion()
{
    const char* e = getenv("LUGARUEX_TEST_UPDATE_FROM");
    return e && *e ? e : EX_LAUNCHER_VERSION;
}

// WM_UPDATE_STATE: 3 = a newer release (lp: UpdateInfo*), 2 = downloaded, 0 = failed (lp: error string)
static void UpdateThread(HWND notify, std::string repo)
{
    UpdateInfo info;
    std::string err;
    if (Update_Latest(repo, Update_Platform(), info, err) && Update_IsNewer(info.version, CurrentVersion())) {
        PostMessageW(notify, WM_UPDATE_STATE, 3, (LPARAM) new UpdateInfo(info));
    }
}

static void UpdateDownloadThread(HWND notify, std::string exDir, UpdateInfo info)
{
    const std::string dir = exDir + "update";
    fsu::RemoveTree(dir);
    fsu::MakeDirs(dir);
    std::string err;
    if (Update_Download(info, dir + "/" + info.asset, &g_updBytes, err) && fsu::WriteAll(dir + "/ready.txt", info.version + "\n" + info.asset + "\n")) {
        PostMessageW(notify, WM_UPDATE_STATE, 2, 0);
    } else {
        fsu::RemoveTree(dir);
        PostMessageW(notify, WM_UPDATE_STATE, 0, (LPARAM) new std::string(err));
    }
}

// a downloaded update waiting: put it in place and start the new launcher (true = this one quits)
static bool ApplyPendingUpdate()
{
    const std::string ex = narrow(g_exDir), dir = ex + "update";
    std::string ready;
    if (!fsu::ReadAll(dir + "/ready.txt", ready)) {
        return false;
    }
    const std::string version = fsu::Trim(ready.substr(0, ready.find('\n')));
    const std::string asset = fsu::Trim(ready.substr(ready.find('\n') + 1));
    std::string err, launcher;
    bool ok = false;
    if (Update_IsNewer(version, CurrentVersion()) && !asset.empty() && asset.find_first_of("/\\") == std::string::npos) {
        const std::string tmp = dir + "/unpacked";
        GameFolder g;
        g.path = narrow(g_exeDir);
        g.path.pop_back(); // (no trailing slash)
        g.writable = true;
        SetupOptions o;
        o.steamPlayButton = false; // (an earlier install decides: Lugaru.exe is updated when it is ours)
        o.shortcut = false;
        ok = Setup_Unpack(dir + "/" + asset, tmp, err) && Setup_Install(tmp, g, o, launcher, err);
    }
    fsu::RemoveTree(dir); // used or not, never try it twice
    if (!ok) {
        if (!err.empty()) {
            MessageBoxW(NULL, (L"The LugaruEX update could not be installed: " + widen(err)).c_str(), L"LugaruEX", MB_ICONWARNING);
        }
        return false;
    }
    // start the new launcher (it is at this program's path now) with the same arguments
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = GetCommandLineW();
    std::vector<wchar_t> cl(cmd.begin(), cmd.end());
    cl.push_back(0);
    if (CreateProcessW(self, cl.data(), NULL, NULL, FALSE, 0, NULL, g_exeDir.c_str(), &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
    return false;
}

// before anything else: installer? (returns true when this run was the installer)
static bool RunSetupIfNeeded(HINSTANCE inst)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring setupTo, package;
    bool noSteam = false, noShortcut = false;
    for (int i = 1; argv && i < argc; i++) {
        const std::wstring a = argv[i];
        if (a == L"--setup-to" && i + 1 < argc) {
            setupTo = argv[++i];
        } else if (a == L"--package" && i + 1 < argc) {
            package = argv[++i];
        } else if (a == L"--no-steam") {
            noSteam = true;
        } else if (a == L"--no-shortcut") {
            noShortcut = true;
        }
    }
    if (argv) {
        LocalFree(argv);
    }
    if (!setupTo.empty()) { // the elevated half of an install into a protected folder
        g_package = narrow(package);
        GameFolder g = Setup_Describe(narrow(setupTo));
        g.writable = true;
        ExitProcess(InstallInto(g, !noSteam, !noShortcut, NULL, true) ? 0 : 1);
    }
    wchar_t selfW[MAX_PATH];
    GetModuleFileNameW(NULL, selfW, MAX_PATH);
    const std::string tmpZip = narrow(std::wstring(_wgetenv(L"TEMP") ? _wgetenv(L"TEMP") : L".")) + "\\LugaruEX-setup.zip";
    const std::string tmpDir = tmpZip.substr(0, tmpZip.size() - 4);
    std::string err;
    if (ExtractPayload(selfW, tmpZip) && Setup_Unpack(tmpZip, tmpDir, err)) {
        g_package = tmpDir; // LugaruEX-<ver>-Setup.exe
    } else if (!Setup_IsGameFolder(narrow(g_exeDir)) && fsu::IsFile(narrow(g_exeDir) + "LugaruEX\\LugaruEX.exe")) {
        g_package = narrow(g_exeDir); // the release zip unpacked somewhere else
        g_package.pop_back();
    } else {
        return false; // the launcher of an installed LugaruEX
    }
    g_setupMode = true;
    g_games = Setup_FindGames();
    if (getenv("LUGARUEX_TEST_NOGAMES")) { // test hook: as on a PC without the game
        g_games.clear();
    }
    // the banner borrows the art of the first game found (nothing is written there)
    const std::wstring keep = g_exeDir;
    if (!g_games.empty() && !g_games[0].path.empty()) {
        g_exeDir = widen(g_games[0].path) + L"\\";
    }
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_SETUP), NULL, SetupProc, 0);
    g_exeDir = keep;
    fsu::RemoveTree(tmpZip);
    fsu::RemoveTree(tmpDir);
    return true;
}

// Text boxes get a plain right-click menu (Undo, Cut, Copy, Paste, Delete, Select all)
// instead of Windows' own, which adds reading order, Unicode control characters and IME items.
static LRESULT CALLBACK PlainEditProc(HWND w, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (msg == WM_CONTEXTMENU) {
        enum { kUndo = 1, kCut, kCopy, kPaste, kDelete, kAll };
        const LONG style = GetWindowLongW(w, GWL_STYLE);
        const bool readOnly = (style & ES_READONLY) != 0, secret = (style & ES_PASSWORD) != 0;
        DWORD a = 0, b = 0;
        SendMessageW(w, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
        const bool sel = b > a, text = GetWindowTextLengthW(w) > 0;
        const bool canPaste = !readOnly && IsClipboardFormatAvailable(CF_UNICODETEXT);
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, (!readOnly && SendMessageW(w, EM_CANUNDO, 0, 0)) ? MF_STRING : MF_GRAYED, kUndo, L"Undo");
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, (sel && !readOnly && !secret) ? MF_STRING : MF_GRAYED, kCut, L"Cut");
        AppendMenuW(m, (sel && !secret) ? MF_STRING : MF_GRAYED, kCopy, L"Copy");
        AppendMenuW(m, canPaste ? MF_STRING : MF_GRAYED, kPaste, L"Paste");
        AppendMenuW(m, (sel && !readOnly) ? MF_STRING : MF_GRAYED, kDelete, L"Delete");
        AppendMenuW(m, MF_SEPARATOR, 0, NULL);
        AppendMenuW(m, text ? MF_STRING : MF_GRAYED, kAll, L"Select all");
        POINT p = { (short)LOWORD(lp), (short)HIWORD(lp) };
        if (lp == (LPARAM)-1) { // Shift+F10 / menu key: under the box
            RECT r;
            GetWindowRect(w, &r);
            p.x = r.left;
            p.y = r.bottom;
        }
        if (GetFocus() != w) {
            SetFocus(w);
        }
        const int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, w, NULL);
        DestroyMenu(m);
        switch (cmd) {
            case kUndo: SendMessageW(w, WM_UNDO, 0, 0); break;
            case kCut: SendMessageW(w, WM_CUT, 0, 0); break;
            case kCopy: SendMessageW(w, WM_COPY, 0, 0); break;
            case kPaste: SendMessageW(w, WM_PASTE, 0, 0); break;
            case kDelete: SendMessageW(w, WM_CLEAR, 0, 0); break;
            case kAll: SendMessageW(w, EM_SETSEL, 0, -1); break;
        }
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(w, PlainEditProc, 7);
    }
    return DefSubclassProc(w, msg, wp, lp);
}

static LRESULT CALLBACK PlainEditHook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HCBT_CREATEWND) {
        wchar_t cls[16] = {};
        GetClassNameW((HWND)wp, cls, 16);
        if (lstrcmpiW(cls, L"Edit") == 0) {
            SetWindowSubclass((HWND)wp, PlainEditProc, 7, 0);
        }
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    // every text box made on this thread (dialog edits, combo box edits) gets the plain menu
    SetWindowsHookExW(WH_CBT, PlainEditHook, NULL, GetCurrentThreadId());
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    g_exeDir = ExeDir();
    {
        // installed as <game>\LugaruEX\LugaruEX Launcher.exe: the game folder is the parent
        const std::wstring parent = g_exeDir.substr(0, g_exeDir.find_last_of(L'\\', g_exeDir.size() - 2) + 1);
        if (!PathIsDirectoryW((g_exeDir + L"Data\\Maps").c_str()) && PathIsDirectoryW((parent + L"Data\\Maps").c_str())) {
            g_exeDir = parent;
        }
    }
    g_exDir = g_exeDir + L"LugaruEX\\";
    CustomSkins_SetExDir(g_exDir);
    CustomSkins_SetMods(ModsNow);
    Setup_CleanOld(Setup_SelfPath()); // left by an update
    if (RunSetupIfNeeded(inst)) {     // LugaruEX-<ver>-Setup.exe, or an unpacked release
        CoUninitialize();
        return 0;
    }
    TidyOldInstall();
    LoadIni(g_set);
    if (g_set.autoUpdate && ApplyPendingUpdate()) { // downloaded last time: the new launcher takes over
        CoUninitialize();
        return 0;
    }
    EnumMonitors();
    if (g_set.monitor < 0 || g_set.monitor >= (int)g_mons.size()) {
        // first run: default to the monitor with the highest refresh rate
        g_set.monitor = 0;
        for (int i = 1; i < (int)g_mons.size(); i++) {
            if (g_mons[i].curHz > g_mons[g_set.monitor].curHz) {
                g_set.monitor = i;
            }
        }
    }
    Preview_Register(inst, g_exeDir);
    Controls_Register(inst);
    Local_Register(inst);
    Pad_Init(); // controllers for the Controls / Local tabs and controller navigation
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_MAIN), NULL, MainProc, 0);
    if (g_banner) {
        DeleteObject(g_banner);
    }
    if (g_engineProcess) {
        // stay (without a window) until the game ends: Steam - and Proton on Linux - treat
        // the program they started as the game, so it shows as running the whole time
        WaitForSingleObject(g_engineProcess, INFINITE);
        CloseHandle(g_engineProcess);
    }
    CoUninitialize();
    return 0;
}
