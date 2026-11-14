/*
LugaruEX launcher for Linux (and the Steam Deck) - the same "configure before
playing" dialog as the Windows launcher, built on SDL2 + Dear ImGui so it runs
on any desktop and can be driven with a controller alone:
LB / RB switch tabs, D-pad / left stick move, A selects, B goes back, Start plays.

It writes the same LugaruEX/LugaruEX.ini the Windows launcher writes and starts
LugaruEX/lugaruex. The banner and the 3D character preview are made from the
user's own game files at run time.

Copyright (C) 2026 - LugaruEX contributors. GPLv2 or later.
*/

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui.h"
#include "imgui_impl_opengl2.h"
#include "imgui_impl_sdl2.h"

#include "../launcher/preview_core.h"
#include "../launcher/skins.h"
#include "banner.h"
#include "common.h"
#include "pad.h"
#include "fsutil.h"
#include "modpack.h"
#include "setup.h"
#include "outfits.h"
#include "update.h"
#include "netinfo.h"
#include "serverquery.h"
#include "adminlink.h"

#include <algorithm>
#include <sstream>
#include <atomic>
#include <cctype>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <commdlg.h>
#include <shellapi.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define LUGARUEX_VERSION "0.5.0"

namespace
{
// ============================================================================ paths
std::string g_gameDir; // the folder with Data/ (this program sits in <game>/LugaruEX/)
std::string g_exDir;   // <game>/LugaruEX/
std::string IniPath() { return g_exDir + "LugaruEX.ini"; }
std::string EnginePath()
{
#ifdef _WIN32
    return g_exDir + "LugaruEX.exe";
#else
    return g_exDir + "lugaruex";
#endif
}

void FindFolders()
{
    char* b = SDL_GetBasePath();
    std::string base = b ? b : "./";
    SDL_free(b);
    std::replace(base.begin(), base.end(), '\\', '/');
    if (base.empty() || base.back() != '/') {
        base += '/';
    }
    g_gameDir = base;
    if (!DirExists(base + "Data/Maps")) {
        const std::string t = base.substr(0, base.size() - 1);
        const size_t s = t.find_last_of('/');
        if (s != std::string::npos && DirExists(t.substr(0, s + 1) + "Data/Maps")) {
            g_gameDir = t.substr(0, s + 1);
        }
    }
    g_exDir = g_gameDir + "LugaruEX/";
}

std::string Ascii(const wchar_t* w)
{
    std::string s;
    for (; w && *w; w++) {
        s += (*w > 0 && *w < 128) ? (char)*w : '_';
    }
    return s;
}

// ============================================================================ settings
struct Settings
{
    int width = 0, height = 0; // 0 = native desktop
    std::string mode = "borderless";
    int monitor = -1, refresh = 0;
    int vsync = 1, fpscap = -1, fov = 90;
    int msaa = 4, af = 16;
    int texq = 1, overrides = 1;
    int colorblind = 0;
    int lighting = 1; // set in game with R (kept, not shown)
    int shaders = 1;
    int simrate = 0, interp = 1;
    std::string mpmode = "off";
    int lan = 0;
    int port = 27015;
    std::string addr; // join address: empty until the player types the host's
    std::string name = "User", serverName, hostPassword, recent, code;
    int usePassword = -1; // host: the Password box (-1: on when a password was saved before)
    int maxPlayers = 0, guestChars = 1, mpAdvanced = 0; // maxPlayers 0 = no limit
    int steam = 1; // through Steam (Valve's relay, Steam server list) - 0 = direct (IP / LAN / VPN)
    int chDefault = 0;
    std::string chModel = "rabbit";
    std::string chSkin = "random";
    std::string chClothes = "default";
    int shareMods = 1;
    int autoUpdate = 1;
    int tab = 0; // [Launcher] Tab: the tab that was open last time
};
Settings g_set;
int g_openTab = 0; // the tab open now (saved as [Launcher] Tab)
Ini g_ini; // as read (other sections are written back unchanged)

void LoadSettings()
{
    g_ini = ReadIni(IniPath());
    Settings& s = g_set;
    const Ini& i = g_ini;
    s.width = i.GetInt("Display", "Width", s.width);
    s.height = i.GetInt("Display", "Height", s.height);
    s.mode = Lower(i.Get("Display", "Mode", s.mode));
    s.monitor = i.GetInt("Display", "Monitor", s.monitor);
    s.refresh = i.GetInt("Display", "RefreshRate", s.refresh);
    s.vsync = i.GetInt("Display", "VSync", s.vsync);
    s.fpscap = i.GetInt("Display", "FPSCap", s.fpscap);
    s.fov = i.GetInt("Display", "FOV", s.fov);
    s.msaa = i.GetInt("Display", "MSAA", s.msaa);
    s.af = i.GetInt("Display", "Anisotropy", s.af);
    s.texq = i.GetInt("Display", "TextureQuality", s.texq);
    s.overrides = i.GetInt("Display", "TextureOverrides", s.overrides);
    s.colorblind = i.GetInt("Display", "Colorblind", s.colorblind);
    s.lighting = i.GetInt("Display", "Lighting", s.lighting);
    s.shaders = i.GetInt("Display", "Shaders", s.shaders);
    s.simrate = i.GetInt("Engine", "SimRate", s.simrate);
    s.interp = i.GetInt("Engine", "Interpolation", s.interp);
    s.mpmode = Lower(i.Get("Multiplayer", "Mode", s.mpmode));
    s.port = i.GetInt("Multiplayer", "Port", s.port);
    s.lan = i.GetInt("Multiplayer", "LAN", s.lan);
    s.addr = i.Get("Multiplayer", "Address", s.addr);
    s.name = i.Get("Multiplayer", "Name", s.name);
    s.serverName = i.Get("Multiplayer", "ServerName", s.serverName);
    s.hostPassword = i.Get("Multiplayer", "HostPassword", s.hostPassword);
    s.usePassword = i.GetInt("Multiplayer", "UsePassword", s.hostPassword.empty() ? 0 : 1);
    s.maxPlayers = i.GetInt("Multiplayer", "MaxPlayers", s.maxPlayers);
    s.guestChars = i.GetInt("Multiplayer", "GuestCharacters", s.guestChars);
    s.mpAdvanced = i.GetInt("Multiplayer", "Advanced", s.mpAdvanced);
    s.recent = i.Get("Multiplayer", "Recent", s.recent);
    s.steam = i.GetInt("Multiplayer", "Steam", s.steam);
    s.code = i.Get("Multiplayer", "Code", s.code);
    s.chDefault = i.GetInt("Player", "Default", s.chDefault);
    s.chModel = Lower(i.Get("Player", "Model", s.chModel));
    s.chSkin = i.Get("Player", "Skin", s.chSkin);
    s.chClothes = i.Get("Player", "Clothes", s.chClothes);
    s.shareMods = i.GetInt("Multiplayer", "ShareMods", s.shareMods);
    s.autoUpdate = i.GetInt("Launcher", "AutoUpdate", s.autoUpdate);
    s.tab = i.GetInt("Launcher", "Tab", s.tab);
}

// ============================================================================ monitors
struct Monitor
{
    std::string label;
    int curW = 0, curH = 0, curHz = 0;
    std::vector<std::pair<int, int>> res;
    std::map<std::pair<int, int>, std::set<int>> rates;
};
std::vector<Monitor> g_mons;

void EnumMonitors()
{
    g_mons.clear();
    const int n = SDL_GetNumVideoDisplays();
    for (int d = 0; d < n; d++) {
        Monitor m;
        SDL_DisplayMode cur;
        if (SDL_GetDesktopDisplayMode(d, &cur) == 0) {
            m.curW = cur.w;
            m.curH = cur.h;
            m.curHz = cur.refresh_rate;
        }
        std::set<std::pair<int, int>> uniq;
        for (int k = 0; k < SDL_GetNumDisplayModes(d); k++) {
            SDL_DisplayMode md;
            if (SDL_GetDisplayMode(d, k, &md) != 0 || md.w < 640 || md.h < 480) {
                continue;
            }
            uniq.insert({ md.w, md.h });
            if (md.refresh_rate > 1) {
                m.rates[{ md.w, md.h }].insert(md.refresh_rate);
            }
        }
        if (m.curW) {
            uniq.insert({ m.curW, m.curH });
        }
        m.res.assign(uniq.begin(), uniq.end());
        std::sort(m.res.begin(), m.res.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
            return a.first != b.first ? a.first > b.first : a.second > b.second;
        });
        const char* name = SDL_GetDisplayName(d);
        char lbl[256];
        snprintf(lbl, sizeof(lbl), "%d: %s  (%dx%d @ %d Hz)", d + 1, name ? name : "Display", m.curW, m.curH, m.curHz);
        m.label = lbl;
        g_mons.push_back(m);
    }
    if (g_mons.empty()) {
        Monitor m;
        m.label = "1: Default display";
        m.curW = 1920;
        m.curH = 1080;
        m.res = { { 1920, 1080 }, { 1280, 720 }, { 1024, 768 } };
        g_mons.push_back(m);
    }
}

std::string AspectName(int w, int h)
{
    if (w <= 0 || h <= 0) {
        return "";
    }
    const double r = (double)w / h;
    struct A
    {
        double r;
        const char* n;
    } known[] = { { 4.0 / 3, "4:3" }, { 5.0 / 4, "5:4" }, { 3.0 / 2, "3:2" }, { 16.0 / 10, "16:10" }, { 16.0 / 9, "16:9" },
                  { 64.0 / 27, "21:9" }, { 43.0 / 18, "21:9" }, { 12.0 / 5, "21:9" }, { 32.0 / 9, "32:9" }, { 32.0 / 10, "32:10" } };
    for (auto& a : known) {
        if (fabs(r - a.r) < 0.012) {
            return a.n;
        }
    }
    char b[32];
    snprintf(b, sizeof(b), "%.2f:1", r);
    return b;
}

// ============================================================================ UI state
// Graphics
int g_monSel = 0, g_resSel = 0, g_modeSel = 1, g_msaaSel = 2, g_afSel = 4, g_fpsSel = 0, g_vsyncSel = 1, g_texqSel = 1, g_cbSel = 0;
int g_custW = 1920, g_custH = 1080, g_fov = 90;
bool g_overrides = true, g_shaders = true;
// Multiplayer
int g_mpSel = 0, g_port = 27015;
char g_addr[128] = "";
bool g_lan = false, g_shareMods = true;
char g_name[64] = "User", g_serverName[96] = "", g_hostPw[64] = "";
bool g_showHostPw = false, g_mpAdvanced = false, g_guestChars = true, g_usePw = false;
bool g_pickSteam = false; // the picked server (g_pickedServer) is a Steam lobby: its code
std::vector<ServerEntry> g_steamServers;
std::string g_steamListError;
std::atomic<int> g_steamListState(0); // 0 idle, 1 asking, 2 answer waiting
std::vector<ServerEntry> g_steamListNew;
std::string g_steamListNewError;
int g_maxPlayers = 0;
ServerQuery g_servers;
std::string g_pickedServer; // the server list's selection (address)
bool g_askModShare = false; // Play: the "this server shares mods" question is open
bool g_autoUpdate = true;
// Character
bool g_chDefault = false;
int g_modelSel = 0;
std::string g_skin = "random", g_clothes = "default";
std::string g_previewKey; // what the preview shows now
// popups / messages
std::string g_message; // a modal message box
std::string g_importModel; // "New skin...": the image path popup (when no file picker exists)
char g_importPath[512] = "";
std::string g_importFor; // "char" | "local:<i>" | "clothes"

const int kMsaa[] = { 0, 2, 4, 8 };
const int kAf[] = { 0, 2, 4, 8, 16 };

void SelectResolution(int w, int h)
{
    const Monitor& m = g_mons[std::min(g_monSel, (int)g_mons.size() - 1)];
    if (w <= 0 || h <= 0) {
        w = m.curW;
        h = m.curH;
    }
    g_resSel = (int)m.res.size(); // Custom...
    for (size_t i = 0; i < m.res.size(); i++) {
        if (m.res[i].first == w && m.res[i].second == h) {
            g_resSel = (int)i;
        }
    }
    g_custW = w;
    g_custH = h;
}

void SelectedRes(int& w, int& h)
{
    const Monitor& m = g_mons[std::min(g_monSel, (int)g_mons.size() - 1)];
    if (g_resSel >= 0 && g_resSel < (int)m.res.size()) {
        w = m.res[g_resSel].first;
        h = m.res[g_resSel].second;
    } else {
        w = g_custW;
        h = g_custH;
    }
}

int IndexOf(const int* v, int n, int want, int fallback)
{
    for (int i = 0; i < n; i++) {
        if (v[i] == want) {
            return i;
        }
    }
    return fallback;
}

void InitFromSettings()
{
    const Settings& s = g_set;
    g_monSel = s.monitor;
    if (g_monSel < 0 || g_monSel >= (int)g_mons.size()) {
        g_monSel = 0; // first run: the monitor with the highest refresh rate
        for (int i = 1; i < (int)g_mons.size(); i++) {
            if (g_mons[i].curHz > g_mons[g_monSel].curHz) {
                g_monSel = i;
            }
        }
    }
    SelectResolution(s.width, s.height);
    g_modeSel = s.mode == "fullscreen" ? 2 : (s.mode == "windowed" ? 0 : 1);
    g_msaaSel = IndexOf(kMsaa, 4, s.msaa, 0);
    g_afSel = IndexOf(kAf, 5, s.af, 0);
    g_fpsSel = s.fpscap == 0 ? 1 : 0;
    g_vsyncSel = std::max(0, std::min(2, s.vsync));
    g_fov = std::max(60, std::min(120, s.fov));
    g_texqSel = s.texq ? 1 : 0;
    g_overrides = s.overrides != 0;
    g_cbSel = std::max(0, std::min(3, s.colorblind));
    g_shaders = s.shaders != 0;
    g_mpSel = s.mpmode == "host" ? 1 : (s.mpmode == "join" ? 2 : 0);
    g_port = s.port;
    snprintf(g_addr, sizeof(g_addr), "%s", s.addr.c_str());
    g_lan = s.lan != 0;
    g_shareMods = s.shareMods != 0;
    snprintf(g_name, sizeof(g_name), "%s", s.name.c_str());
    snprintf(g_serverName, sizeof(g_serverName), "%s", s.serverName.c_str());
    snprintf(g_hostPw, sizeof(g_hostPw), "%s", s.hostPassword.c_str());
    g_usePw = s.usePassword != 0;
    g_maxPlayers = std::max(0, s.maxPlayers);
    g_guestChars = s.guestChars != 0;
    g_mpAdvanced = s.mpAdvanced != 0;
    if (s.mpmode == "join" && s.addr.empty() && !s.code.empty()) { // the Steam server picked last time
        g_pickedServer = s.code;
        g_pickSteam = true;
    }
    g_autoUpdate = s.autoUpdate != 0;
    g_chDefault = s.chDefault != 0;
    g_modelSel = s.chModel == "wolf" ? 1 : 0;
    g_skin = s.chSkin;
    g_clothes = s.chClothes;
}

// ============================================================================ skins
struct CustomSkin
{
    std::string model, enginePath, label, file;
};

std::vector<CustomSkin> CustomSkins_List()
{
    std::vector<CustomSkin> v;
    for (const char* model : { "rabbit", "wolf", "clothes" }) {
        const std::string dir = g_exDir + "Data/Textures/Custom/" + model;
        for (const std::string& n : ListDir(dir, ".png")) {
            CustomSkin s;
            s.model = model;
            s.file = dir + "/" + n;
            s.label = n.substr(0, n.rfind('.'));
            s.enginePath = std::string("Textures/Custom/") + model + "/" + n;
            v.push_back(s);
        }
    }
    return v;
}

bool FindSkinFile(const SkinInfo& s, std::string& out)
{
    // Overrides (either name, png or jpg), then the original HD file in Data
    const std::string oss = std::string(s.path).substr(9); // strip "Textures/"
    const std::string hd = Ascii(s.hdFile);
    const std::string base[2] = { oss.substr(0, oss.rfind('.')), hd.substr(0, hd.rfind('.')) };
    for (auto& b : base) {
        for (const char* e : { ".png", ".jpg" }) {
            const std::string p = FindFileCI(g_exDir + "Overrides/Textures", b + e);
            if (!p.empty()) {
                out = p;
                return true;
            }
        }
    }
    for (const std::string& f : { hd, oss }) {
        const std::string p = FindFileCI(g_gameDir + "Data/Textures", f);
        if (!p.empty()) {
            out = p;
            return true;
        }
    }
    return false;
}

std::string SafeName(const std::string& n)
{
    std::string r;
    for (unsigned char c : n) {
        r += (c < 128 && (isalnum(c) || c == ' ' || c == '-' || c == '_')) ? (char)c : '_'; // the game opens files by ASCII name
    }
    return r.empty() ? "Skin" : r;
}

// any image stb_image reads (png, jpg, bmp, gif, tga) -> PNG in LugaruEX/Data/Textures/Custom/<model>/
bool ImportImage(const std::string& src, const std::string& model, CustomSkin& out)
{
    Image img;
    if (!ReadImage(src, img)) {
        g_message = "Could not read that image (PNG, JPG, BMP, GIF or TGA).";
        return false;
    }
    std::string name = src.substr(src.find_last_of("/\\") + 1);
    name = SafeName(name.substr(0, name.rfind('.')));
    const std::string dir = g_exDir + "Data/Textures/Custom/" + model;
    MakeDirs(dir);
    std::string file = name + ".png";
    for (int n = 2; FileExists(dir + "/" + file); n++) {
        file = name + " " + std::to_string(n) + ".png";
    }
    if (!SavePNG(dir + "/" + file, img)) {
        g_message = "Could not save the image into " + dir + ".";
        return false;
    }
    out.model = model;
    out.file = dir + "/" + file;
    out.label = file.substr(0, file.size() - 4);
    out.enginePath = "Textures/Custom/" + model + "/" + file;
    return true;
}

// the desktop's file picker (zenity / kdialog); "" if cancelled, "?" if there is none
std::string PickImageFile(const std::string& title)
{
#ifdef _WIN32
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Images (*.png; *.jpg; *.jpeg; *.bmp; *.gif; *.tga)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tga\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    std::wstring t(title.begin(), title.end());
    ofn.lpstrTitle = t.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) {
        return "";
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, NULL, 0, NULL, NULL);
    std::string s(n > 0 ? n - 1 : 0, 0);
    WideCharToMultiByte(CP_UTF8, 0, file, -1, &s[0], n, NULL, NULL);
    return s;
#else
    const char* cmds[] = {
        "zenity --file-selection --title='%s' --file-filter='Images | *.png *.jpg *.jpeg *.bmp *.gif *.tga *.PNG *.JPG' 2>/dev/null",
        "kdialog --getopenfilename \"$HOME\" 'image/png image/jpeg image/bmp image/gif' --title '%s' 2>/dev/null",
    };
    for (const char* c : cmds) {
        const std::string tool = std::string(c).substr(0, std::string(c).find(' '));
        if (system(("command -v " + tool + " >/dev/null 2>&1").c_str()) != 0) {
            continue;
        }
        char cmd[512];
        snprintf(cmd, sizeof(cmd), c, title.c_str());
        FILE* p = popen(cmd, "r");
        if (!p) {
            continue;
        }
        char buf[2048] = {};
        const size_t n = fread(buf, 1, sizeof(buf) - 1, p);
        pclose(p);
        return Trim(std::string(buf, n));
    }
    return "?";
#endif
}

// ============================================================================ controls
struct Act
{
    const char* name;
    const char* label;
    const char* kbDef;  // nullptr = fixed
    const char* padDef; // nullptr = fixed
    const char* kbFixed;
    const char* padFixed;
};
const Act kActs[] = {
    { "Forward", "Move forward", "W", nullptr, nullptr, "Left stick" },
    { "Back", "Move back", "S", nullptr, nullptr, "Left stick" },
    { "Left", "Move left", "A", nullptr, nullptr, "Left stick" },
    { "Right", "Move right", "D", nullptr, nullptr, "Left stick" },
    { "Walk", "Walk (hold)", nullptr, nullptr, "Ctrl", "Light push" },
    { "Jump", "Jump", "Space", "a", nullptr, nullptr },
    { "Crouch", "Crouch / roll / block", "Left Shift", "lefttrigger", nullptr, nullptr },
    { "Attack", "Attack", "Mouse Left", "righttrigger", nullptr, nullptr },
    { "Draw", "Draw / put away", "E", "y", nullptr, nullptr },
    { "Throw", "Throw / pick up", "Q", "x", nullptr, nullptr },
    { "Lighting", "Shaders (next look)", "R", "back", nullptr, nullptr },
    { "Pause", "Pause menu", nullptr, "start", "Esc", nullptr },
    { "Console", "Console", "`", nullptr, nullptr, "-" },
    { "Screenshot", "Screenshot", nullptr, nullptr, "F12", "-" },
    { "Record", "Record video (start / stop)", nullptr, nullptr, "F11", "-" },
    { "Look", "Look around", nullptr, nullptr, "Mouse", "Right stick" },
};
const int kNumActs = sizeof(kActs) / sizeof(kActs[0]);
std::map<std::string, std::string> g_kb, g_pad;
bool g_vibration = true;
int g_look = 100;
int g_capRow = -1, g_capCol = 0; // capture: col 1 keyboard, 2 pad
bool g_capArmed = false;         // a pad capture waits until every button is up
std::string g_hint;
char g_presetName[96] = "Default";
std::vector<std::string> g_presets;
std::string g_vizPad;

void SetDefaultControls()
{
    for (const Act& a : kActs) {
        g_kb[a.name] = a.kbDef ? a.kbDef : "";
        g_pad[a.name] = a.padDef ? a.padDef : "";
    }
    g_vibration = true;
    g_look = 100;
}

void ReadControls(const Ini& ini, bool keepMissing)
{
    for (const Act& a : kActs) {
        if (a.kbDef) {
            g_kb[a.name] = ini.Get("Keys", a.name, keepMissing ? g_kb[a.name] : a.kbDef);
        }
        if (a.padDef) {
            g_pad[a.name] = ini.Get("Pad", a.name, keepMissing ? g_pad[a.name] : a.padDef);
        }
    }
    // the first LugaruEX defaults (B crouch, X attack, RB throw): move untouched setups to the current ones
    if (g_pad["Jump"] == "a" && g_pad["Crouch"] == "b" && g_pad["Attack"] == "x" && g_pad["Draw"] == "y" && g_pad["Throw"] == "rightshoulder") {
        for (const Act& a : kActs) {
            if (a.padDef) {
                g_pad[a.name] = a.padDef;
            }
        }
    }
    g_vibration = ini.GetInt("Pad", "Vibration", 1) != 0;
    const std::string ls = ini.Get("Pad", "LookSpeed");
    if (!ls.empty()) {
        g_look = std::max(20, std::min(300, (int)(atof(ls.c_str()) * 100 + 0.5)));
    }
}

std::string ControlsText()
{
    std::string t = "[Keys]\r\n";
    for (const Act& a : kActs) {
        if (a.kbDef) {
            t += std::string(a.name) + "=" + g_kb[a.name] + "\r\n";
        }
    }
    t += "\r\n[Pad]\r\n";
    for (const Act& a : kActs) {
        if (a.padDef) {
            t += std::string(a.name) + "=" + g_pad[a.name] + "\r\n";
        }
    }
    char b[32];
    snprintf(b, sizeof(b), "%.2f", g_look / 100.f);
    t += std::string("Vibration=") + (g_vibration ? "1" : "0") + "\r\nLookSpeed=" + b + "\r\n";
    return t;
}

std::string PresetDir() { return g_exDir + "Presets/"; }

void FillPresets()
{
    g_presets.clear();
    for (const std::string& n : ListDir(PresetDir(), ".ini")) {
        g_presets.push_back(n.substr(0, n.size() - 4));
    }
}

void Assign(int row, int col, const std::string& value)
{
    auto& m = (col == 1) ? g_kb : g_pad;
    const std::string name = kActs[row].name, old = m[name];
    for (const Act& a : kActs) { // swap with an action already using it
        if (a.name != name && m.count(a.name) && m[a.name] == value && ((col == 1) ? a.kbDef : a.padDef)) {
            m[a.name] = old;
        }
    }
    m[name] = value;
    g_capRow = -1;
    g_hint.clear();
}

// ============================================================================ local players
struct Player
{
    std::string id, name;
    std::string ch = "random";
    int seat = 0;
    int rank = 0; // saved player number (0 = player 1): order when several join at once
    float x = 0, y = 0, w = 1, h = 1;
};
const int kMaxLocal = 4;
const ImU32 kColor[kMaxLocal] = { IM_COL32(70, 140, 245, 255), IM_COL32(230, 70, 55, 255), IM_COL32(80, 200, 80, 255), IM_COL32(240, 190, 40, 255) };
std::vector<Player> g_pl;
std::vector<Player> g_known; // controllers seen before (saved, or switched off): their character comes back with them
bool g_sideBySide = false;
std::vector<std::pair<std::string, std::string>> g_chars; // key, label

void SeatRect(int seat, int n, float& x, float& y, float& w, float& h)
{
    x = 0, y = 0, w = 1, h = 1;
    if (n == 2) {
        if (g_sideBySide) {
            x = seat * 0.5f, w = 0.5f;
        } else {
            y = seat * 0.5f, h = 0.5f;
        }
    } else if (n == 3) {
        if (seat < 2) {
            x = seat * 0.5f, w = 0.5f, h = 0.5f;
        } else {
            y = 0.5f, h = 0.5f;
        }
    } else if (n >= 4) {
        x = (seat % 2) * 0.5f, y = (seat / 2) * 0.5f, w = 0.5f, h = 0.5f;
    }
}

void Layout()
{
    const int n = (int)g_pl.size();
    std::vector<int> order(n);
    for (int i = 0; i < n; i++) {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [](int a, int b) { return g_pl[a].seat < g_pl[b].seat; });
    for (int k = 0; k < n; k++) {
        g_pl[order[k]].seat = k;
    }
    for (auto& p : g_pl) {
        SeatRect(p.seat, n, p.x, p.y, p.w, p.h);
    }
}

void FillChars()
{
    g_chars.clear();
    g_chars.push_back({ "default", "Default" });
    g_chars.push_back({ "random", "Random" });
    for (const SkinInfo& s : kSkins) {
        g_chars.push_back({ std::string(s.model) + ":" + s.path, std::string(strcmp(s.model, "wolf") == 0 ? "Wolf - " : "Rabbit - ") + Ascii(s.label) });
    }
    for (const CustomSkin& s : CustomSkins_List()) {
        if (s.model != "clothes") {
            g_chars.push_back({ s.model + ":" + s.enginePath, std::string(s.model == "wolf" ? "Wolf - " : "Rabbit - ") + s.label });
        }
    }
    g_chars.push_back({ "new:rabbit", "New rabbit skin..." });
    g_chars.push_back({ "new:wolf", "New wolf skin..." });
}

const char* const kKeyboard = "keyboard"; // player 1 on keyboard and mouse only (their controller is another player)

void LoadLocal()
{
    const Ini& ini = g_ini;
    const int n = std::min(kMaxLocal, ini.GetInt("Local", "Count", 0));
    g_sideBySide = ini.Get("Local", "Split2", "topbottom") == "sidebyside";
    for (int i = 0; i < n; i++) {
        const std::string k = "P" + std::to_string(i + 1);
        Player p;
        p.id = ini.Get("Local", k);
        if (p.id.empty()) {
            continue;
        }
        p.name = ini.Get("Local", k + "Name", "Controller");
        p.ch = ini.Get("Local", k + "Char", "random");
        p.seat = ini.GetInt("Local", k + "Seat", i);
        p.rank = i;
        if (i == 0 && p.id == kKeyboard) {
            p.name = "Keyboard and mouse";
            g_pl.push_back(p); // player 1 chose keyboard and mouse only: always there
            continue;
        }
        g_known.push_back(p); // a player again once their controller is on
    }
    // controllers that were off when the settings were saved: id|character|name
    for (int i = 1; i <= 16; i++) {
        const std::string v = ini.Get("Local", "Known" + std::to_string(i));
        const size_t a = v.find('|'), b = a == std::string::npos ? a : v.find('|', a + 1);
        if (b == std::string::npos) {
            continue;
        }
        Player p;
        p.id = v.substr(0, a);
        p.ch = v.substr(a + 1, b - a - 1);
        p.name = v.substr(b + 1);
        p.rank = p.seat = 100 + i;
        if (!p.id.empty() && std::none_of(g_known.begin(), g_known.end(), [&](const Player& k) { return k.id == p.id; })) {
            g_known.push_back(p);
        }
    }
    Layout();
}

// the players are the controllers connected right now (none = player 1 on the keyboard):
// a controller switched on joins, one switched off leaves (its character is remembered)
void SyncLocal()
{
    bool changed = false;
    for (size_t i = 0; i < g_pl.size();) {
        PadState* s = Pad_Find(g_pl[i].id);
        if ((s && s->connected) || g_pl[i].id == kKeyboard) {
            i++;
            continue;
        }
        auto k = std::find_if(g_known.begin(), g_known.end(), [&](const Player& p) { return p.id == g_pl[i].id; });
        if (k != g_known.end()) {
            *k = g_pl[i];
        } else {
            g_known.push_back(g_pl[i]);
        }
        g_pl.erase(g_pl.begin() + i);
        changed = true;
    }
    std::vector<Player> joining;
    for (auto& s : Pad_List()) {
        if (!s.connected) {
            continue;
        }
        auto it = std::find_if(g_pl.begin(), g_pl.end(), [&](const Player& p) { return p.id == s.id; });
        if (it != g_pl.end()) {
            it->name = s.name;
            continue;
        }
        auto k = std::find_if(g_known.begin(), g_known.end(), [&](const Player& p) { return p.id == s.id; });
        Player p;
        if (k != g_known.end()) {
            p = *k;
        } else {
            p.seat = p.rank = 1000 + (int)joining.size();
        }
        p.id = s.id;
        p.name = s.name;
        joining.push_back(p);
    }
    // several at once (the launcher opening): in their saved order, so player 1 stays player 1
    std::stable_sort(joining.begin(), joining.end(), [](const Player& a, const Player& b) { return a.rank < b.rank; });
    const bool late = !g_pl.empty();
    for (Player& p : joining) {
        if ((int)g_pl.size() >= kMaxLocal) {
            break;
        }
        if (late) {
            p.seat = 1000 + (int)g_pl.size(); // joining later: the next free part of the screen
        }
        g_pl.push_back(p);
        changed = true;
    }
    if (changed) {
        Layout();
    }
}

std::string LocalText()
{
    std::string t = "[Local]\r\nCount=" + std::to_string(g_pl.size()) + "\r\nCustom=1\r\nSplit2=" + (g_sideBySide ? "sidebyside" : "topbottom") + "\r\n";
    for (size_t i = 0; i < g_pl.size(); i++) {
        const Player& p = g_pl[i];
        const std::string k = "P" + std::to_string(i + 1);
        char rect[96];
        snprintf(rect, sizeof(rect), "%.4f,%.4f,%.4f,%.4f", p.x, p.y, p.w, p.h);
        std::string name;
        for (unsigned char c : p.name) {
            name += (c > 0 && c < 128) ? (char)c : '_';
        }
        const std::string ch = i == 0 ? std::string("default") : p.ch; // player 1 = the Character tab
        t += k + "=" + p.id + "\r\n" + k + "Name=" + name + "\r\n" + k + "Char=" + ch + "\r\n" + k + "Seat=" + std::to_string(p.seat) +
             "\r\n" + k + "Rect=" + rect + "\r\n";
    }
    // controllers that are off now keep their character for next time (the game ignores these)
    int n = 0;
    for (const Player& p : g_known) {
        if (n >= 16 || std::any_of(g_pl.begin(), g_pl.end(), [&](const Player& q) { return q.id == p.id; })) {
            continue;
        }
        std::string name;
        for (unsigned char c : p.name) {
            name += (c >= 32 && c < 128 && c != '|') ? (char)c : '_';
        }
        t += "Known" + std::to_string(++n) + "=" + p.id + "|" + p.ch + "|" + name + "\r\n";
    }
    return t;
}

// ============================================================================ save + launch
// ============================================================================ mods
std::vector<ModEntry> g_modOrder;
std::vector<ModInfo> g_modInfo;
int g_modSel = -1;
std::string g_modDelete; // folder waiting for "Delete?" confirmation
std::string g_modNote;

std::string ModsDir() { return g_exDir + "Mods"; }

void RefreshMods()
{
    fsu::MakeDirs(ModsDir());
    g_modInfo = Mod_List(ModsDir());
    g_modOrder = Mod_Order(g_modOrder, g_modInfo);
    if (g_modSel >= (int)g_modOrder.size()) {
        g_modSel = (int)g_modOrder.size() - 1;
    }
}

const ModInfo* ModInfoOf(const std::string& folder)
{
    for (auto& m : g_modInfo) {
        if (m.folder == folder) {
            return &m;
        }
    }
    return nullptr;
}

std::string ModsText()
{
    return "[Mods]\r\n; mods in LugaruEX/Mods: + on, - off; the first one wins when two change the same file\r\nList=" +
           Mod_FormatList(g_modOrder) + "\r\n";
}

void InstallMods(const std::vector<std::string>& zips)
{
    std::string done, failed;
    for (const std::string& z : zips) {
        if (z.size() < 4 || Lower(z.substr(z.size() - 4)) != ".zip") {
            continue;
        }
        std::string folder, err;
        const std::string shown = z.substr(z.find_last_of("/\\") + 1);
        if (Mod_InstallZip(z, ModsDir(), folder, err)) {
            done += (done.empty() ? "" : ", ") + shown;
            g_modOrder.insert(g_modOrder.begin(), { folder, true });
            g_modSel = 0;
        } else {
            failed += "\n" + shown + ": " + err;
        }
    }
    RefreshMods();
    if (!failed.empty()) {
        g_message = "Some files could not be installed as mods:" + failed;
    }
    if (!done.empty()) {
        g_modNote = "Installed: " + done + ". New mods are on and at the top of the list.";
    }
}

void OpenFolder(const std::string& dir)
{
#ifdef _WIN32
    std::wstring w(dir.begin(), dir.end());
    ShellExecuteW(NULL, L"open", w.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
    const std::string cmd = "xdg-open '" + dir + "' >/dev/null 2>&1 &";
    if (system(cmd.c_str()) != 0) {
        g_message = "The mods are in " + dir;
    }
#endif
}

// the desktop's file picker for mod zips (several at once); empty if cancelled, {"?"} if there is none
std::vector<std::string> PickZips()
{
    std::vector<std::string> out;
#ifdef _WIN32
    std::vector<wchar_t> buf(32768, 0);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Mods (*.zip)\0*.zip\0All files\0*.*\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) {
        return out;
    }
    std::vector<std::wstring> parts;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) {
        parts.push_back(p);
    }
    auto u8 = [](const std::wstring& w) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
        std::string s(n > 0 ? n - 1 : 0, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, NULL, NULL);
        return s;
    };
    if (parts.size() == 1) {
        out.push_back(u8(parts[0]));
    }
    for (size_t i = 1; i < parts.size(); i++) {
        out.push_back(u8(parts[0] + L"\\" + parts[i]));
    }
    return out;
#else
    const char* cmds[] = {
        "zenity --file-selection --multiple --separator='\n' --title='Choose mod zip files' --file-filter='Mods | *.zip *.ZIP' 2>/dev/null",
        "kdialog --getopenfilename \"$HOME\" '*.zip' --multiple --separate-output --title 'Choose mod zip files' 2>/dev/null",
    };
    for (const char* c : cmds) {
        const std::string tool = std::string(c).substr(0, std::string(c).find(' '));
        if (system(("command -v " + tool + " >/dev/null 2>&1").c_str()) != 0) {
            continue;
        }
        FILE* p = popen(c, "r");
        if (!p) {
            continue;
        }
        std::string all;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), p)) > 0) {
            all.append(buf, n);
        }
        pclose(p);
        size_t a = 0;
        while (a < all.size()) {
            size_t b = all.find('\n', a);
            if (b == std::string::npos) {
                b = all.size();
            }
            const std::string line = Trim(all.substr(a, b - a));
            if (!line.empty()) {
                out.push_back(line);
            }
            a = b + 1;
        }
        return out;
    }
    out.push_back("?");
    return out;
#endif
}

bool CollectSettings()
{
    Settings& s = g_set;
    s.monitor = g_monSel;
    int w, h;
    SelectedRes(w, h);
    if (w < 640 || h < 480 || w > 16384 || h > 16384) {
        g_message = "Please enter a resolution of at least 640 x 480.";
        return false;
    }
    s.width = w;
    s.height = h;
    s.mode = g_modeSel == 2 ? "fullscreen" : (g_modeSel == 0 ? "windowed" : "borderless");
    s.refresh = 0;
    s.msaa = kMsaa[g_msaaSel];
    s.af = kAf[g_afSel];
    s.vsync = g_vsyncSel;
    s.fpscap = g_fpsSel == 1 ? 0 : -1;
    s.fov = g_fov;
    s.texq = g_texqSel;
    s.overrides = g_overrides;
    s.colorblind = g_cbSel;
    s.shaders = g_shaders;
    s.lan = g_lan;
    s.mpmode = g_mpSel == 1 ? "host" : (g_mpSel == 2 ? "join" : "off");
    s.port = (g_port > 0 && g_port < 65536) ? g_port : 27015;
    s.addr = Trim(g_addr);
    s.name = netinfo::CleanName(g_name);
    s.serverName = netinfo::CleanText(g_serverName, netinfo::kMaxServerName, true);
    s.hostPassword = Trim(g_hostPw);
    s.usePassword = g_usePw ? 1 : 0;
    s.maxPlayers = g_maxPlayers <= 0 ? 0 : std::max(2, std::min(250, g_maxPlayers)); // 0 = no limit
    s.guestChars = g_guestChars;
    s.mpAdvanced = g_mpAdvanced;
    // host: through Steam unless LAN / VPN is ticked; connect: a typed IP is direct, a server
    // picked in the list goes the way it was found
    if (s.mpmode == "join") {
        const bool steamPick = s.addr.empty() && g_pickSteam && !g_pickedServer.empty();
        s.steam = steamPick ? 1 : 0;
        s.code = steamPick ? g_pickedServer : "";
    } else {
        s.steam = s.lan ? 0 : 1;
    }
    s.chDefault = g_chDefault;
    s.chModel = g_modelSel == 1 ? "wolf" : "rabbit";
    s.chSkin = g_skin;
    s.chClothes = g_clothes;
    s.shareMods = g_shareMods;
    s.autoUpdate = g_autoUpdate;
    s.tab = g_openTab;
    if (s.mpmode == "host" && s.usePassword && s.hostPassword.empty()) {
        g_message = "Type a password under Host options, or untick Password.";
        return false;
    }
    if (s.mpmode == "join" && s.addr.empty() && s.code.empty()) {
        g_message = "Pick a server from the list, or type the host's IP (LAN / VPN).";
        return false;
    }
    return true;
}

bool SaveIni()
{
    const Settings& s = g_set;
    std::string t;
    auto line = [&](const std::string& l) { t += l + "\r\n"; };
    auto kv = [&](const char* k, const std::string& v) { line(std::string(k) + "=" + v); };
    auto ki = [&](const char* k, int v) { kv(k, std::to_string(v)); };
    line("; LugaruEX settings - written by the launcher, read by the game");
    line("[Display]");
    line("; Width/Height 0 = native desktop resolution");
    ki("Width", s.width);
    ki("Height", s.height);
    line("; windowed | borderless | fullscreen");
    kv("Mode", s.mode);
    ki("Monitor", s.monitor);
    ki("RefreshRate", s.refresh);
    line("; 0 off, 1 on (no tearing), 2 adaptive (tears below refresh)");
    ki("VSync", s.vsync);
    line("; -1 = match the display refresh rate, 0 = unlimited");
    ki("FPSCap", s.fpscap);
    line("; vertical field of view in degrees (original game: 90)");
    ki("FOV", s.fov);
    ki("MSAA", s.msaa);
    ki("Anisotropy", s.af);
    line("; 0 = half size, 1 = full size");
    ki("TextureQuality", s.texq);
    ki("TextureOverrides", s.overrides);
    line("; HUD and menus always keep their proportions on wide screens");
    ki("HUDAspect", 1);
    line("; colour blind filter: 0 off, 1 deuteranopia, 2 protanopia, 3 tritanopia");
    ki("Colorblind", s.colorblind);
    line("; (old) the game now starts in the Enhanced look when Shaders=1; R switches looks for that game only");
    ki("Lighting", s.lighting);
    line("; 0 = shaders off (Original look only)");
    ki("Shaders", s.shaders);
    line("");
    line("[Engine]");
    line("; fixed simulation rate (steps per second, 0 = match the monitor) and render interpolation");
    ki("SimRate", s.simrate);
    ki("Interpolation", s.interp);
    line("");
    line("[Multiplayer]");
    line("; off | host | join");
    kv("Mode", s.mpmode);
    ki("Port", s.port);
    kv("Address", s.addr);
    line("; 1 = LAN: the host announces itself on the local network, Join finds it automatically");
    ki("LAN", s.lan);
    line("; 1 = share mods online: send yours when hosting, download the host's when joining");
    ki("ShareMods", s.shareMods);
    line("; your name (server lists, chat)");
    kv("Name", s.name);
    line("; host: the name in server lists (^0-^9 colour the text after them)");
    kv("ServerName", s.serverName);
    kv("HostPassword", s.hostPassword);
    line("; 0 = the server has no password (HostPassword is kept for later)");
    ki("UsePassword", s.usePassword);
    line("; host: players at most (yours included, 0 = no limit), and whether guests choose their character");
    ki("MaxPlayers", s.maxPlayers);
    ki("GuestCharacters", s.guestChars);
    ki("Advanced", s.mpAdvanced);
    line("; servers joined before (the launcher's server list asks them)");
    kv("Recent", s.recent);
    line("; 1 = play through Steam (Steam server list, Valve's relay: nobody sees anyone's IP address); 0 = direct (IP / LAN / VPN)");
    ki("Steam", s.steam);
    ki("Listed", 1);
    kv("Code", s.code);
    line("");
    line("[Player]");
    line("; Default=1 keeps the character each level defines");
    ki("Default", s.chDefault);
    line("; rabbit | wolf");
    kv("Model", s.chModel);
    kv("Skin", s.chSkin);
    line("; default (the level's own) | none | shirt | camo | snowcamo | desertcamo | leather | gi");
    kv("Clothes", s.chClothes);
    line("");
    line("[Launcher]");
    line("; 1 = look for a new LugaruEX on GitHub when the launcher starts, and install it");
    ki("AutoUpdate", s.autoUpdate);
    ki("Tab", s.tab);
    if (!g_ini.Get("Launcher", "UpdateRepo").empty()) {
        kv("UpdateRepo", g_ini.Get("Launcher", "UpdateRepo"));
    }
    line("");
    t += ControlsText() + "\r\n" + LocalText() + "\r\n" + ModsText();
    // sections this launcher doesn't manage stay as they were
    static const std::set<std::string> mine = { "display", "engine", "multiplayer", "player", "keys", "pad", "local", "launcher", "mods" };
    for (const auto& sec : g_ini.sections) {
        if (sec.first.empty() || mine.count(Lower(sec.first))) {
            continue;
        }
        t += "\r\n[" + sec.first + "]\r\n";
        for (const std::string& l : sec.second) {
            if (!Trim(l).empty()) {
                t += l + "\r\n";
            }
        }
    }
    MakeDirs(g_exDir.substr(0, g_exDir.size() - 1));
    FILE* f = fopen(IniPath().c_str(), "wb");
    if (!f) {
        return false;
    }
    const bool ok = fwrite(t.data(), 1, t.size(), f) == t.size();
    return fclose(f) == 0 && ok;
}

std::vector<std::string> g_passArgs; // command line options for the game (e.g. -d), not Steam's %command%

// (defined with the Multiplayer tab)
extern AdminLink g_link;
extern std::string g_joinPassword;
#ifdef _WIN32
extern HANDLE g_gameProc;
#else
extern pid_t g_gamePid;
#endif

bool LaunchEngine()
{
    const std::string exe = EnginePath();
    if (!FileExists(exe)) {
        g_message = "Could not find " + exe + "\nPlease reinstall LugaruEX.";
        return false;
    }
    // online: the link back to this window (kick / ban, password) and a typed password -
    // only for this game, in its environment, never written to a file
    const bool online = g_set.mpmode != "off";
    std::string admin;
    if (online && g_link.Open()) {
        admin = g_link.NewSecret();
    }
    const std::string pw = online && g_set.mpmode == "join" ? g_joinPassword : std::string();
#ifdef _WIN32
    std::string cmd = "\"" + exe + "\"";
    for (auto& a : g_passArgs) {
        cmd += " \"" + a + "\"";
    }
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::vector<char> cl(cmd.begin(), cmd.end());
    cl.push_back(0);
    if (!admin.empty()) {
        SetEnvironmentVariableA("LUGARUEX_ADMIN", admin.c_str());
    }
    if (!pw.empty()) {
        SetEnvironmentVariableA("LUGARUEX_JOIN_PASSWORD", pw.c_str());
    }
    const BOOL started = CreateProcessA(exe.c_str(), cl.data(), NULL, NULL, FALSE, 0, NULL, g_gameDir.c_str(), &si, &pi);
    SetEnvironmentVariableA("LUGARUEX_ADMIN", NULL);
    SetEnvironmentVariableA("LUGARUEX_JOIN_PASSWORD", NULL);
    if (!started) {
        g_message = "Failed to start the game.";
        return false;
    }
    CloseHandle(pi.hThread);
    g_gameProc = pi.hProcess;
    return true;
#else
    if (access(exe.c_str(), X_OK) != 0) {
        chmod(exe.c_str(), 0755); // unpacked without the executable bit
    }
    const pid_t pid = fork();
    if (pid < 0) {
        g_message = "Failed to start the game.";
        return false;
    }
    if (pid == 0) {
        if (chdir(g_gameDir.c_str()) != 0) {
            _exit(126);
        }
        if (!admin.empty()) {
            setenv("LUGARUEX_ADMIN", admin.c_str(), 1);
        }
        if (!pw.empty()) {
            setenv("LUGARUEX_JOIN_PASSWORD", pw.c_str(), 1);
        }
        std::vector<char*> argv;
        argv.push_back((char*)exe.c_str());
        for (auto& a : g_passArgs) {
            argv.push_back((char*)a.c_str());
        }
        argv.push_back(nullptr);
        execv(exe.c_str(), argv.data());
        _exit(127);
    }
    g_gamePid = pid; // (Steam waits for the program it started: the launcher stays until the game ends)
    return true;
#endif
}

// ============================================================================ drawing helpers
GLuint MakeTexture(const Image& img)
{
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.px.data());
    return t;
}

bool Combo(const char* id, int* sel, const std::vector<std::string>& items, float width = -1)
{
    if (width != 0) {
        ImGui::SetNextItemWidth(width);
    }
    bool changed = false;
    const char* cur = (*sel >= 0 && *sel < (int)items.size()) ? items[*sel].c_str() : "";
    if (ImGui::BeginCombo(id, cur)) {
        for (int i = 0; i < (int)items.size(); i++) {
            const bool s = (i == *sel);
            if (ImGui::Selectable(items[i].c_str(), s)) {
                *sel = i;
                changed = true;
            }
            if (s) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void Label(const char* text, float labelW)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
    ImGui::SameLine(labelW);
}

// simple live controller picture: sticks, face buttons, d-pad, shoulders and triggers
void DrawPad(ImDrawList* dl, ImVec2 a, ImVec2 b, const PadState* p, ImU32 accent)
{
    const float W = b.x - a.x, H = b.y - a.y;
    const ImU32 body = IM_COL32(58, 54, 54, 255), edge = IM_COL32(110, 104, 100, 255), off = IM_COL32(32, 30, 30, 255);
    auto P = [&](float x, float y) { return ImVec2(a.x + x * W, a.y + y * H); };
    const float r = std::min(W, H);
    dl->AddRectFilled(P(.08f, .22f), P(.92f, .78f), body, r * .18f);
    dl->AddRect(P(.08f, .22f), P(.92f, .78f), edge, r * .18f);
    auto btn = [&](int code) { return p && p->connected && code >= 0 && code < PAD_BUTTONS && p->btn[code]; };
    // shoulders + triggers
    const float lt = p ? p->lt : 0, rt = p ? p->rt : 0;
    dl->AddRectFilled(P(.12f, .08f), P(.32f, .16f), lt > .1f ? accent : off, 3);
    dl->AddRectFilled(P(.68f, .08f), P(.88f, .16f), rt > .1f ? accent : off, 3);
    dl->AddRectFilled(P(.12f, .17f), P(.32f, .21f), btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ? accent : off, 2);
    dl->AddRectFilled(P(.68f, .17f), P(.88f, .21f), btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ? accent : off, 2);
    // sticks
    auto stick = [&](float cx, float cy, float sx, float sy, bool click) {
        const ImVec2 c = P(cx, cy);
        dl->AddCircleFilled(c, r * .13f, off);
        dl->AddCircleFilled(ImVec2(c.x + sx * r * .07f, c.y + sy * r * .07f), r * .07f, click ? accent : edge);
    };
    stick(.28f, .42f, p ? p->lx : 0, p ? p->ly : 0, btn(SDL_CONTROLLER_BUTTON_LEFTSTICK));
    stick(.62f, .62f, p ? p->rx : 0, p ? p->ry : 0, btn(SDL_CONTROLLER_BUTTON_RIGHTSTICK));
    // d-pad
    const ImVec2 d = P(.38f, .63f);
    const float s = r * .05f;
    dl->AddRectFilled(ImVec2(d.x - s, d.y - 3 * s), ImVec2(d.x + s, d.y + 3 * s), off);
    dl->AddRectFilled(ImVec2(d.x - 3 * s, d.y - s), ImVec2(d.x + 3 * s, d.y + s), off);
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP)) dl->AddRectFilled(ImVec2(d.x - s, d.y - 3 * s), ImVec2(d.x + s, d.y - s), accent);
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) dl->AddRectFilled(ImVec2(d.x - s, d.y + s), ImVec2(d.x + s, d.y + 3 * s), accent);
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) dl->AddRectFilled(ImVec2(d.x - 3 * s, d.y - s), ImVec2(d.x - s, d.y + s), accent);
    if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) dl->AddRectFilled(ImVec2(d.x + s, d.y - s), ImVec2(d.x + 3 * s, d.y + s), accent);
    // face buttons
    const ImVec2 f = P(.74f, .42f);
    const float o = r * .085f, br = r * .045f;
    const struct
    {
        float x, y;
        int code;
        ImU32 col;
        const char* t;
    } face[] = { { 0, 1, SDL_CONTROLLER_BUTTON_A, IM_COL32(90, 190, 90, 255), "A" }, { 1, 0, SDL_CONTROLLER_BUTTON_B, IM_COL32(220, 70, 60, 255), "B" },
                 { -1, 0, SDL_CONTROLLER_BUTTON_X, IM_COL32(70, 130, 230, 255), "X" }, { 0, -1, SDL_CONTROLLER_BUTTON_Y, IM_COL32(230, 190, 50, 255), "Y" } };
    for (auto& q : face) {
        const ImVec2 c(f.x + q.x * o, f.y + q.y * o);
        dl->AddCircleFilled(c, br, btn(q.code) ? q.col : off);
        dl->AddCircle(c, br, q.col);
    }
    // back / start
    dl->AddRectFilled(P(.43f, .40f), P(.48f, .45f), btn(SDL_CONTROLLER_BUTTON_BACK) ? accent : off, 2);
    dl->AddRectFilled(P(.52f, .40f), P(.57f, .45f), btn(SDL_CONTROLLER_BUTTON_START) ? accent : off, 2);
}

// ============================================================================ preview (3D character)
ImVec2 g_pvMin, g_pvMax; // where the preview goes, in window coordinates
float g_fbScaleX = 1, g_fbScaleY = 1;
int g_fbH = 0;

void PreviewCallback(const ImDrawList*, const ImDrawCmd*)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    const int x = (int)(g_pvMin.x * g_fbScaleX), w = (int)((g_pvMax.x - g_pvMin.x) * g_fbScaleX);
    const int h = (int)((g_pvMax.y - g_pvMin.y) * g_fbScaleY), y = g_fbH - (int)(g_pvMax.y * g_fbScaleY);
    PreviewCore_Render(x, y, w, h);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
}

void UpdatePreview()
{
    const std::string model = g_modelSel == 1 ? "wolf" : "rabbit";
    std::string file;
    if (g_skin == "random" || g_skin == "default") {
        for (const auto& k : kSkins) {
            if (model == k.model && FindSkinFile(k, file)) {
                break;
            }
            file.clear();
        }
    } else {
        bool found = false;
        for (const auto& k : kSkins) {
            if (Lower(g_skin) == Lower(k.path)) {
                found = FindSkinFile(k, file);
            }
        }
        if (!found) {
            for (const CustomSkin& c : CustomSkins_List()) {
                if (Lower(c.enginePath) == Lower(g_skin)) {
                    file = c.file;
                }
            }
        }
    }
    // the Clothes choice painted over the skin, as the game does it (wolves wear none)
    std::vector<PreviewCloth> clothes;
    if (!g_chDefault && model != "wolf") {
        if (g_clothes.compare(0, 7, "custom:") == 0) {
            for (const CustomSkin& c : CustomSkins_List()) {
                if (c.model == "clothes" && Lower(c.enginePath) == Lower(g_clothes.substr(7))) {
                    clothes.push_back({ c.file, 1, 1, 1 });
                }
            }
        } else {
            for (PreviewCloth c : PreviewCore_Outfit(g_clothes)) {
                const size_t slash = c.tex.rfind('/');
                const std::string name = c.tex.substr(slash + 1), base = name.substr(0, name.rfind('.'));
                std::string p;
                for (const char* e : { ".png", ".jpg" }) {
                    if (p.empty()) {
                        p = FindFileCI(g_exDir + "Overrides/Textures", base + e);
                    }
                }
                if (p.empty()) {
                    p = FindFileCI(g_gameDir + "Data/Textures", name);
                }
                if (!p.empty()) {
                    c.tex = p;
                    clothes.push_back(c);
                }
            }
        }
    }
    std::string key = model + "|" + file;
    for (const PreviewCloth& c : clothes) {
        key += "|" + c.tex;
    }
    PreviewCore_SetModel(model);
    PreviewCore_SetDimmed(g_chDefault);
    if (key != g_previewKey) {
        g_previewKey = key;
        Image img, cl;
        if (!file.empty() && ReadImage(file, img)) {
            for (const PreviewCloth& c : clothes) {
                if (ReadImage(c.tex, cl)) {
                    PreviewCore_PaintCloth(img.px, img.w, img.h, cl.px.data(), cl.w, cl.h, c.r, c.g, c.b);
                }
            }
            PreviewCore_SetTexture(img.px.data(), img.w, img.h);
        } else {
            PreviewCore_SetTexture(nullptr, 0, 0);
        }
    }
}

// ============================================================================ tabs
const float kLabelW = 150;

void TabGraphics(float s)
{
    const float lw = kLabelW * s, cw = 230 * s;
    std::vector<std::string> mons;
    for (auto& m : g_mons) {
        mons.push_back(m.label);
    }
    Label("Monitor:", lw);
    ImGui::BeginDisabled(g_mons.size() < 2);
    if (Combo("##mon", &g_monSel, mons, 380 * s)) {
        SelectResolution(0, 0);
    }
    ImGui::EndDisabled();

    const Monitor& m = g_mons[std::min(g_monSel, (int)g_mons.size() - 1)];
    std::vector<std::string> res;
    for (auto& r : m.res) {
        char b[96];
        snprintf(b, sizeof(b), "%d x %d   (%s)%s", r.first, r.second, AspectName(r.first, r.second).c_str(),
                 (r.first == m.curW && r.second == m.curH) ? "  native" : "");
        res.push_back(b);
    }
    res.push_back("Custom...");
    Label("Screen resolution:", lw);
    if (Combo("##res", &g_resSel, res, cw)) {
        if (g_resSel < (int)m.res.size()) {
            g_custW = m.res[g_resSel].first;
            g_custH = m.res[g_resSel].second;
        }
    }
    const bool custom = g_resSel >= (int)m.res.size();
    if (custom) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70 * s);
        ImGui::InputInt("##cw", &g_custW, 0, 0);
        ImGui::SameLine();
        ImGui::TextUnformatted("x");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70 * s);
        ImGui::InputInt("##ch", &g_custH, 0, 0);
    }
    int w, h;
    SelectedRes(w, h);
    ImGui::SetCursorPosX(lw);
    ImGui::TextDisabled("Aspect %s%s", AspectName(w, h).c_str(), custom ? "  -  custom sizes work best in Windowed or Borderless" : "");

    Label("Display mode:", lw);
    Combo("##mode", &g_modeSel, { "Windowed", "Borderless window", "Exclusive fullscreen" }, cw);
    Label("Antialiasing:", lw);
    Combo("##msaa", &g_msaaSel, { "Off", "2x MSAA", "4x MSAA", "8x MSAA" }, cw);
    Label("Anisotropic:", lw);
    Combo("##af", &g_afSel, { "Off", "2x", "4x", "8x", "16x" }, cw);
    int hz = m.curHz;
    if (g_modeSel == 2) {
        auto it = m.rates.find({ w, h });
        if (it != m.rates.end() && !it->second.empty()) {
            hz = *it->second.rbegin();
        }
    }
    Label("FPS cap:", lw);
    Combo("##fps", &g_fpsSel, { hz > 0 ? "Match display (" + std::to_string(hz) + " Hz)" : std::string("Match display"), "Unlimited" }, cw);
    Label("V-Sync:", lw);
    Combo("##vsync", &g_vsyncSel, { "Off", "On (no tearing)", "Adaptive" }, cw);
    Label("Field of view:", lw);
    ImGui::SetNextItemWidth(cw);
    ImGui::SliderInt("##fov", &g_fov, 60, 120, g_fov == 90 ? "%d\xC2\xB0 (original)" : "%d\xC2\xB0");
    Label("Texture quality:", lw);
    Combo("##texq", &g_texqSel, { "Low (half size)", "High (full size)" }, cw);
    Label("Colour blind mode:", lw);
    Combo("##cb", &g_cbSel, { "Off", "Deuteranopia", "Protanopia", "Tritanopia" }, cw);
    ImGui::SetCursorPosX(lw);
    ImGui::Checkbox("Shaders", &g_shaders);
    ImGui::SameLine();
    ImGui::Checkbox("High Resolution Textures", &g_overrides);
}

// skin choices for the Character tab: Random, Default, the game's own, player-made, New skin...
struct Choice
{
    std::string key, label;
};

std::vector<Choice> SkinChoices(const std::string& model)
{
    std::vector<Choice> v = { { "random", "Random" }, { "default", "Default" } };
    for (const SkinInfo& k : kSkins) {
        std::string f;
        if (model == k.model && FindSkinFile(k, f)) {
            v.push_back({ k.path, Ascii(k.label) });
        }
    }
    for (const CustomSkin& c : CustomSkins_List()) {
        if (c.model == model) {
            v.push_back({ c.enginePath, c.label });
        }
    }
    v.push_back({ "new", "New skin..." });
    return v;
}


std::vector<Choice> ClothesChoices()
{
    std::vector<Choice> v;
    v.push_back({ "default", "Default" });
    v.push_back({ "random", "Random" });
    for (const OutfitDef& o : Outfits()) { // the game's own list (shared/outfits.h)
        v.push_back({ o.key, o.label });
    }
    for (const CustomSkin& c : CustomSkins_List()) {
        if (c.model == "clothes") {
            v.push_back({ "custom:" + c.enginePath, c.label });
        }
    }
    v.push_back({ "new", "New clothes..." });
    return v;
}

bool ChoiceCombo(const char* id, const std::vector<Choice>& v, std::string& key, float width, bool* pickedNew)
{
    int sel = 0;
    for (int i = 0; i < (int)v.size(); i++) {
        if (Lower(v[i].key) == Lower(key)) {
            sel = i;
        }
    }
    std::vector<std::string> labels;
    for (auto& c : v) {
        labels.push_back(c.label);
    }
    if (Combo(id, &sel, labels, width)) {
        if (v[sel].key == "new") {
            *pickedNew = true;
        } else {
            key = v[sel].key;
        }
        return true;
    }
    return false;
}

void StartImport(const std::string& model, const std::string& forWhat)
{
    const std::string title = model == "wolf" ? "Choose a wolf skin image" : model == "clothes" ? "Choose a clothes image" : "Choose a rabbit skin image";
    const std::string file = PickImageFile(title);
    if (file == "?") { // no file picker on this desktop: ask for the path
        g_importModel = model;
        g_importFor = forWhat;
        g_importPath[0] = 0;
        return;
    }
    if (file.empty()) {
        return;
    }
    CustomSkin made;
    if (ImportImage(file, model, made)) {
        if (forWhat == "char") {
            g_skin = made.enginePath;
        } else if (forWhat == "clothes") {
            g_clothes = "custom:" + made.enginePath;
        } else if (forWhat.rfind("local:", 0) == 0) {
            const int i = atoi(forWhat.c_str() + 6);
            if (i >= 0 && i < (int)g_pl.size()) {
                g_pl[i].ch = model + ":" + made.enginePath;
            }
        }
        FillChars();
    }
}

void TabCharacter(float s)
{
    const float avail = ImGui::GetContentRegionAvail().x, ph = ImGui::GetContentRegionAvail().y - 4 * s;
    const float pw = std::min(avail * 0.42f, ph * 0.62f);
    ImGui::BeginGroup();
    ImGui::Checkbox("Default", &g_chDefault);
    ImGui::SameLine();
    ImGui::TextDisabled("(each level's own character)");
    ImGui::Spacing();
    const float lw = 80 * s, cw = std::max(120 * s, avail - pw - lw - 24 * s);
    ImGui::BeginDisabled(g_chDefault);
    Label("Model:", lw);
    if (Combo("##model", &g_modelSel, { "Rabbit", "Wolf" }, cw)) {
        g_skin = "random";
    }
    const std::string model = g_modelSel == 1 ? "wolf" : "rabbit";
    bool pickNew = false;
    Label("Skin:", lw);
    ChoiceCombo("##skin", SkinChoices(model), g_skin, cw, &pickNew);
    if (pickNew) {
        StartImport(model, "char");
    }
    ImGui::BeginDisabled(g_modelSel == 1); // wolves wear no clothes
    pickNew = false;
    Label("Clothes:", lw);
    ChoiceCombo("##clothes", ClothesChoices(), g_clothes, cw, &pickNew);
    if (pickNew) {
        StartImport("clothes", "clothes");
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::PushTextWrapPos(avail - pw - 12 * s);
    ImGui::TextDisabled("Random picks a skin nobody else in the level wears. Default is the one the level gives its player. "
                        "New skin... turns any image into a skin.");
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    UpdatePreview();
    ImGui::SameLine(avail - pw + ImGui::GetStyle().WindowPadding.x);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##preview", ImVec2(pw, ph));
    const unsigned now = SDL_GetTicks();
    const ImVec2 mp = ImGui::GetIO().MousePos;
    if (ImGui::IsItemActivated()) {
        PreviewCore_DragBegin((int)mp.x, (int)mp.y, now);
    }
    if (ImGui::IsItemActive()) {
        PreviewCore_DragMove((int)mp.x, (int)mp.y, now);
    } else if (PreviewCore_Dragging()) {
        PreviewCore_DragEnd(now);
    }
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0) {
        PreviewCore_Wheel(ImGui::GetIO().MouseWheel > 0 ? 1 : -1, now);
    }
    // right stick turns it too (controller only)
    for (auto& p : Pad_List()) {
        if (p.connected && (fabsf(p.rx) > 0.25f || fabsf(p.ry) > 0.25f)) {
            PreviewCore_Turn(p.rx * 3.f, p.ry * 2.f, now);
            break;
        }
    }
    g_pvMin = a;
    g_pvMax = ImVec2(a.x + pw, a.y + ph);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCallback(PreviewCallback, nullptr);
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    dl->AddRect(a, g_pvMax, IM_COL32(90, 60, 50, 255));
}

void TabControls(float s)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    const float right = 220 * s;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Preset:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170 * s);
    ImGui::InputText("##preset", g_presetName, sizeof(g_presetName));
    ImGui::SameLine(0, 2);
    if (ImGui::BeginCombo("##presets", "", ImGuiComboFlags_NoPreview)) {
        if (ImGui::Selectable("Default")) {
            snprintf(g_presetName, sizeof(g_presetName), "Default");
            SetDefaultControls();
            g_hint = "Loaded the default controls.";
        }
        for (auto& n : g_presets) {
            if (ImGui::Selectable(n.c_str())) {
                snprintf(g_presetName, sizeof(g_presetName), "%s", n.c_str());
                SetDefaultControls();
                ReadControls(ReadIni(PresetDir() + n + ".ini"), false);
                g_hint = "Loaded preset \"" + n + "\".";
            }
        }
        ImGui::EndCombo();
    }
    std::string name;
    for (const char* c = g_presetName; *c; c++) {
        if (!strchr("\\/:*?\"<>|", *c)) {
            name += *c;
        }
    }
    name = Trim(name);
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        if (name.empty() || Lower(name) == "default") {
            g_message = "Type a name for the preset in the box first.";
        } else {
            MakeDirs(PresetDir().substr(0, PresetDir().size() - 1));
            if (FILE* f = fopen((PresetDir() + name + ".ini").c_str(), "wb")) {
                const std::string t = ControlsText();
                fwrite(t.data(), 1, t.size(), f);
                fclose(f);
            }
            FillPresets();
            g_hint = "Saved preset \"" + name + "\".";
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && !name.empty() && Lower(name) != "default") {
        remove((PresetDir() + name + ".ini").c_str());
        FillPresets();
        snprintf(g_presetName, sizeof(g_presetName), "Default");
        g_hint = "Deleted preset \"" + name + "\".";
    }
    ImGui::SameLine();
    if (ImGui::Button("Defaults")) {
        SetDefaultControls();
        snprintf(g_presetName, sizeof(g_presetName), "Default");
        g_capRow = -1;
        g_hint.clear();
    }

    const float tableH = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.2f;
    ImGui::BeginChild("##binds", ImVec2(avail - right - 10 * s, tableH));
    if (ImGui::BeginTable("##bt", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Keyboard / mouse", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (int i = 0; i < kNumActs; i++) {
            const Act& a = kActs[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(a.label);
            for (int col = 1; col <= 2; col++) {
                ImGui::TableSetColumnIndex(col);
                const bool fixed = col == 1 ? !a.kbDef : !a.padDef;
                std::string t;
                if (fixed) {
                    t = col == 1 ? a.kbFixed : a.padFixed;
                } else if (col == 1) {
                    const std::string& k = g_kb[a.name];
                    t = k.empty() ? "(none)" : (k == "`" ? "` (tilde key)" : k);
                } else {
                    t = Pad_CodeLabel(Pad_CodeFromName(g_pad[a.name]));
                }
                if (i == g_capRow && col == g_capCol) {
                    t = "press...";
                }
                ImGui::PushID(i * 4 + col);
                if (fixed) {
                    ImGui::TextDisabled("%s", t.c_str());
                } else if (ImGui::Selectable(t.c_str(), i == g_capRow && col == g_capCol)) {
                    g_capRow = i;
                    g_capCol = col;
                    g_capArmed = false;
                    g_hint = col == 1 ? "Press a key or mouse button for \"" + std::string(a.label) + "\" (Esc or B cancels)."
                                      : "Press a button on any controller for \"" + std::string(a.label) + "\" (Esc cancels).";
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();
    std::vector<std::string> pads;
    std::vector<std::string> ids;
    int sel = 0;
    for (auto& p : Pad_List()) {
        if (p.connected) {
            if (p.id == g_vizPad) {
                sel = (int)ids.size();
            }
            pads.push_back(p.name);
            ids.push_back(p.id);
        }
    }
    if (pads.empty()) {
        pads.push_back("(no controller connected)");
    }
    ImGui::TextUnformatted("Controller:");
    if (Combo("##pad", &sel, pads, right) && sel < (int)ids.size()) {
        g_vizPad = ids[sel];
    }
    if (sel < (int)ids.size()) {
        g_vizPad = ids[sel];
    }
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(right, right * 0.72f));
    ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + right, a.y + right * 0.72f), IM_COL32(24, 22, 22, 255), 4);
    DrawPad(ImGui::GetWindowDrawList(), ImVec2(a.x + 6, a.y + 6), ImVec2(a.x + right - 6, a.y + right * 0.72f - 6), Pad_Find(g_vizPad), IM_COL32(225, 60, 40, 255));
    ImGui::Checkbox("Vibration", &g_vibration);
    ImGui::TextUnformatted("Look speed:");
    ImGui::SetNextItemWidth(right);
    ImGui::SliderInt("##look", &g_look, 20, 300, "%d%%");
    ImGui::EndGroup();
    ImGui::TextWrapped("%s", g_hint.empty() ? "Click an entry to change it." : g_hint.c_str());
}

int g_dragSeat = -1;

void TabLocal(float s)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    const float leftW = 250 * s;
    ImGui::BeginChild("##players", ImVec2(leftW, 0));
    if (g_pl.empty()) {
        ImGui::TextWrapped("1 player (keyboard and mouse). Each controller you switch on joins as a player.");
    }
    std::vector<std::string> labels;
    for (auto& c : g_chars) {
        labels.push_back(c.second);
    }
    // the connected controllers (numbered when two have the same name)
    std::vector<std::pair<std::string, std::string>> pads;
    for (auto& ps : Pad_List()) {
        if (ps.connected) {
            pads.push_back({ ps.id, ps.name });
        }
    }
    for (size_t a = 0; a < pads.size(); a++) {
        int same = 0, mine = 0;
        for (size_t b = 0; b < pads.size(); b++) {
            if (pads[b].second == pads[a].second) {
                same++;
                mine += b <= a ? 1 : 0;
            }
        }
        if (same > 1) {
            pads[a].second += " " + std::to_string(mine);
        }
    }
    for (int i = 0; i < (int)g_pl.size(); i++) {
        Player& p = g_pl[i];
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColor[i]), "P%d", i + 1);
        ImGui::SameLine(30 * s);
        // the input this player uses; picking another player's controller swaps them
        std::vector<std::string> ids, names;
        if (i == 0) {
            ids.push_back(kKeyboard);
            names.push_back("Keyboard and mouse");
            for (auto& pd : pads) {
                ids.push_back(pd.first);
                names.push_back("Keyboard + " + pd.second);
            }
        } else {
            for (auto& pd : pads) {
                ids.push_back(pd.first);
                names.push_back(pd.second);
            }
        }
        int dev = 0;
        for (int k = 0; k < (int)ids.size(); k++) {
            if (ids[k] == p.id) {
                dev = k;
            }
        }
        if (Combo("##dev", &dev, names, leftW - 70 * s) && dev < (int)ids.size() && ids[dev] != p.id) {
            const std::string want = ids[dev];
            auto other = std::find_if(g_pl.begin(), g_pl.end(), [&](const Player& o) { return o.id == want; });
            if (other != g_pl.end()) {
                const int j = (int)(other - g_pl.begin());
                if (p.id == kKeyboard) {
                    p.id = want; // player 1 takes that controller back: its player leaves
                    p.name = other->name;
                    g_pl.erase(g_pl.begin() + j);
                } else {
                    std::swap(g_pl[i].id, g_pl[j].id);
                    std::swap(g_pl[i].name, g_pl[j].name);
                }
            } else {
                p.id = want; // keyboard only: the controller it had becomes the next player
                if (want == kKeyboard) {
                    p.name = "Keyboard and mouse";
                }
            }
            SyncLocal();
            Layout();
            ImGui::PopID();
            break; // (the list changed)
        }
        ImGui::SetCursorPosX(30 * s);
        int sel = 0;
        for (int k = 0; k < (int)g_chars.size(); k++) {
            if (g_chars[k].first == p.ch) {
                sel = k;
            }
        }
        if (i == 0) { // player 1's character is the Character tab's
            int one = 0;
            ImGui::BeginDisabled();
            Combo("##ch", &one, { "As on the Character tab" }, leftW - 70 * s);
            ImGui::EndDisabled();
        } else if (Combo("##ch", &sel, labels, leftW - 70 * s)) {
            const std::string key = g_chars[sel].first;
            if (key.rfind("new:", 0) == 0) {
                StartImport(key.substr(4), "local:" + std::to_string(i));
            } else {
                p.ch = key;
            }
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();
    const float cw = avail - leftW - 12 * s;
    float sw = cw, sh = cw * 9 / 16;
    const float maxH = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 3;
    if (sh > maxH) {
        sh = maxH;
        sw = sh * 16 / 9;
    }
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##canvas", ImVec2(sw, sh));
    const bool active = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + sw, a.y + sh), IM_COL32(12, 12, 14, 255));
    const ImVec2 mp = ImGui::GetIO().MousePos;
    auto region = [&](const Player& p, ImVec2& r0, ImVec2& r1) {
        r0 = ImVec2(a.x + p.x * sw, a.y + p.y * sh);
        r1 = ImVec2(a.x + (p.x + p.w) * sw, a.y + (p.y + p.h) * sh);
    };
    int hover = -1;
    for (int i = 0; i < (int)g_pl.size(); i++) {
        ImVec2 r0, r1;
        region(g_pl[i], r0, r1);
        if (mp.x >= r0.x && mp.x < r1.x && mp.y >= r0.y && mp.y < r1.y) {
            hover = i;
        }
    }
    if (ImGui::IsItemActivated() && g_pl.size() > 1) {
        g_dragSeat = hover;
    }
    if (active && g_dragSeat >= 0 && g_pl.size() == 2) {
        // two players: the screen goes where it is dragged - toward a side gives side by
        // side, toward the top or bottom gives top / bottom
        const float nx = (mp.x - a.x) / sw - 0.5f, ny = (mp.y - a.y) / sh - 0.5f;
        if (fabsf(nx) > 0.08f || fabsf(ny) > 0.08f) {
            const bool side = fabsf(nx) * 16.f > fabsf(ny) * 9.f;
            const int seat = side ? (nx < 0 ? 0 : 1) : (ny < 0 ? 0 : 1);
            g_sideBySide = side;
            g_pl[g_dragSeat].seat = seat;
            g_pl[1 - g_dragSeat].seat = 1 - seat;
            Layout();
        }
    }
    if (!active && g_dragSeat >= 0) {
        if (g_pl.size() != 2 && hover >= 0 && hover != g_dragSeat) {
            std::swap(g_pl[g_dragSeat].seat, g_pl[hover].seat); // the two players trade places
            Layout();
        }
        g_dragSeat = -1;
    }
    const Uint32 now = SDL_GetTicks();
    for (int i = 0; i < (int)g_pl.size(); i++) {
        const Player& p = g_pl[i];
        ImVec2 r0, r1;
        region(p, r0, r1);
        PadState* st = Pad_Find(p.id);
        const bool keyboard = p.id == kKeyboard;
        const bool conn = keyboard || (st && st->connected);
        const bool flash = st && st->connected && now - st->lastActive < 350;
        const bool target = g_dragSeat >= 0 && i == hover && i != g_dragSeat;
        const ImU32 c = kColor[i];
        const float t = (flash || target) ? 0.45f : (i == g_dragSeat ? 0.1f : 0.22f);
        auto mixc = [&](int ch) { return (int)(20 * (1 - t) + ((c >> (ch * 8)) & 255) * t); };
        dl->AddRectFilled(r0, r1, IM_COL32(mixc(0), mixc(1), mixc(2), 255));
        dl->AddRect(ImVec2(r0.x + 1, r0.y + 1), ImVec2(r1.x - 1, r1.y - 1), target ? IM_COL32_WHITE : (conn ? c : IM_COL32(110, 110, 110, 255)), 0, 0, flash ? 4.f : 2.f);
        char tag[8];
        snprintf(tag, sizeof(tag), "P%d", i + 1);
        dl->AddText(ImVec2(r0.x + 6, r0.y + 3), conn ? c : IM_COL32(150, 150, 150, 255), tag);
        const float pw = r1.x - r0.x, ph = r1.y - r0.y;
        if (keyboard) {
            const char* kt = "Keyboard";
            const ImVec2 ts = ImGui::CalcTextSize(kt);
            dl->AddText(ImVec2((r0.x + r1.x - ts.x) / 2, (r0.y + r1.y - ts.y) / 2), IM_COL32(220, 220, 230, 255), kt);
        } else {
            DrawPad(dl, ImVec2(r0.x + pw / 6, r0.y + ph / 6), ImVec2(r1.x - pw / 6, r1.y - ph / 8), st, c);
        }
    }
    if (g_pl.empty()) {
        const char* t = "P1 - keyboard and mouse";
        const ImVec2 ts = ImGui::CalcTextSize(t);
        dl->AddText(ImVec2(a.x + (sw - ts.x) / 2, a.y + (sh - ts.y) / 2), IM_COL32(170, 170, 170, 255), t);
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw);
    ImGui::TextDisabled("Click and drag a player's screen to move it.");
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
}

std::vector<std::string> RecentServers()
{
    std::vector<std::string> out;
    std::stringstream ss(g_set.recent);
    std::string a;
    while (std::getline(ss, a, '|')) {
        a = Trim(a);
        if (!a.empty() && std::find(out.begin(), out.end(), a) == out.end()) {
            out.push_back(a);
        }
    }
    return out;
}

void RememberServer(const std::string& addr)
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

// ---- the game this launcher started. Online games keep the launcher open: who is on your
// server (right click: kick / ban), passwords, and Play again when the game ends.
AdminLink g_link; // shared/adminlink.cpp
#ifdef _WIN32
HANDLE g_gameProc = NULL;
#else
pid_t g_gamePid = 0;
#endif
std::string g_joinPassword; // typed in the password window: only in memory, given to the game
bool g_steamAnswered = false; // (re-asking Steam quietly after its first answer)
std::set<std::string> g_opened, g_closedOwn; // servers whose players show / your server, closed by hand

bool GameRunning()
{
#ifdef _WIN32
    return g_gameProc != NULL;
#else
    return g_gamePid > 0;
#endif
}

// true when the game just ended
bool GameEnded()
{
#ifdef _WIN32
    if (g_gameProc && WaitForSingleObject(g_gameProc, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_gameProc);
        g_gameProc = NULL;
        return true;
    }
#else
    int status = 0;
    if (g_gamePid > 0 && waitpid(g_gamePid, &status, WNOHANG) == g_gamePid) {
        g_gamePid = 0;
        return true;
    }
#endif
    return false;
}

bool Hosting()
{
    return GameRunning() && g_link.st.heard && g_link.st.role == 1;
}

void SteamListThread(std::string helper)
{
    std::vector<ServerEntry> list;
    std::string err;
    SteamList_Run(helper, list, err);
    g_steamListNew = list;
    g_steamListNewError = err;
    g_steamListState = 2;
}

// every server: Steam lobbies, the LAN and virtual LANs (broadcast), servers joined before
// and the typed address
void RefreshServers()
{
#ifdef _WIN32
    const std::string helper = g_exDir + "lugaruex-steam.exe";
#else
    const std::string helper = g_exDir + "lugaruex-steam";
#endif
    if (g_steamListState == 0 && FileExists(helper)) {
        g_steamListState = 1;
        std::thread(SteamListThread, helper).detach();
    }
    std::vector<std::string> addrs = RecentServers();
    const std::string typed = Trim(g_addr);
    if (!typed.empty()) {
        addrs.insert(addrs.begin(), typed);
    }
    g_servers.Refresh(addrs, g_port > 0 && g_port < 65536 ? g_port : 27015);
}

// the listed server this Play would join (Steam: by code; direct: by address)
const ServerEntry* JoinTarget()
{
    const bool steam = g_set.steam != 0;
    const std::string want = steam ? g_set.code : g_set.addr;
    if (want.empty()) {
        return nullptr;
    }
    const std::vector<ServerEntry>& pool = steam ? g_steamServers : g_servers.Entries();
    for (const ServerEntry& e : pool) {
        if (e.answered && (e.addr == want || (!steam && want.find(':') == std::string::npos && e.addr.rfind(want + ":", 0) == 0))) {
            return &e;
        }
    }
    return nullptr;
}

bool IsOwnServer(const ServerEntry& e)
{
    return Hosting() && e.answered && e.info.id != 0 && e.info.id == g_link.st.sessionId;
}

// LAN / VPN first (closest), then Steam, then servers joined before; the same hosting
// session answering twice shows once; the server you host on top
std::vector<ServerEntry> ListSource()
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
    std::stable_sort(out.begin(), out.end(), [](const ServerEntry& a, const ServerEntry& b) {
        if (a.answered != b.answered) {
            return a.answered;
        }
        return a.pingMs < b.pingMs;
    });
    if (Hosting()) {
        auto own = std::find_if(out.begin(), out.end(), IsOwnServer);
        ServerEntry mine;
        if (own != out.end()) {
            mine = *own;
            out.erase(own);
        } else { // (not in a list yet)
            mine.addr = "(this PC)";
            mine.answered = true;
            mine.info.id = g_link.st.sessionId;
            mine.info.version = LUGARUEX_VERSION;
            mine.info.name = g_set.serverName.empty() ? netinfo::CleanName(g_set.name) + "'s game" : g_set.serverName;
            mine.info.maxPlayers = g_set.maxPlayers;
        }
        out.insert(out.begin(), mine);
    }
    return out;
}

std::string RowKey(const ServerEntry& e)
{
    return e.info.id ? "#" + std::to_string(e.info.id) : e.addr;
}

bool Opened(const ServerEntry& e)
{
    return IsOwnServer(e) ? !g_closedOwn.count(RowKey(e)) : g_opened.count(RowKey(e)) > 0;
}

// a server name in its colours (^0-^9)
void ColouredText(const std::string& name, bool dim)
{
    bool first = true;
    for (const netinfo::Segment& seg : netinfo::ColourSegments(name)) {
        unsigned char c[3];
        netinfo::ColourRGB(seg.colour, c);
        if (!first) {
            ImGui::SameLine(0, 0);
        }
        first = false;
        ImGui::TextColored(dim ? ImVec4(.5f, .5f, .5f, 1) : ImVec4(c[0] / 255.f, c[1] / 255.f, c[2] / 255.f, 1), "%s", seg.text.c_str());
    }
}

// a password box with Show
void PasswordBox(const char* id, char* buf, size_t size, bool* show, float w, float s)
{
    ImGui::SetNextItemWidth(w);
    ImGui::InputText(id, buf, size, *show ? 0 : ImGuiInputTextFlags_Password);
    ImGui::SameLine(0, 6 * s);
    ImGui::PushID(id);
    ImGui::Checkbox("Show", show);
    ImGui::PopID();
}

// Solo / Host / Connect: buttons that stay pressed
bool ModeButton(const char* label, int mode, float w)
{
    const bool on = g_mpSel == mode;
    if (on) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    const bool hit = ImGui::Button(label, ImVec2(w, 0));
    if (on) {
        ImGui::PopStyleColor();
    }
    if (hit) {
        g_mpSel = mode;
    }
    return hit;
}

void TabMultiplayer(float s)
{
    static bool asked = false;
    static Uint32 lastAsk = 0;
    if (!asked || SDL_GetTicks() - lastAsk > 15000u) { // when the tab opens, then every 15 s
        asked = true;
        lastAsk = SDL_GetTicks();
        RefreshServers();
    }
    if (g_steamListState == 2) { // Steam's answer came
        g_steamServers = g_steamListNew;
        g_steamListError = g_steamListNewError;
        g_steamListState = 0;
        g_steamAnswered = true;
    }
    const float lab = 92 * s;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name:");
    ImGui::SameLine(lab);
    ImGui::SetNextItemWidth(170 * s);
    ImGui::InputText("##name", g_name, netinfo::kMaxName + 1);

    ModeButton("Solo", 0, 110 * s);
    ImGui::SameLine();
    ModeButton("Host", 1, 110 * s);
    ImGui::SameLine();
    ModeButton("Connect", 2, 110 * s);

    if (g_mpSel == 1) {
        const AdminStatus& a = g_link.st;
        std::string t;
        if (Hosting()) {
            t = !a.status.empty() ? a.status
                : a.steam && a.code.empty() ? "Your server is starting..."
                                            : std::string("Your server is up") + (a.steam ? " in the Steam server list: " : " on your network: ") +
                                                  std::to_string(a.players.size()) + (a.players.size() == 1 ? " guest." : " guests.");
        } else if (GameRunning()) {
            t = "The game is running.";
        } else {
            t = g_lan ? "Play starts your server on your network (LAN, Radmin, Hamachi, ZeroTier...)."
                      : "Play starts your server: it shows in the Steam server list.";
        }
        ImGui::TextUnformatted(t.c_str());
        ImGui::SetNextItemOpen(g_mpAdvanced);
        g_mpAdvanced = ImGui::TreeNodeEx("Host options", ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_FramePadding);
        if (g_mpAdvanced) {
            ImGui::BeginDisabled(GameRunning());
            ImGui::Indent(16 * s);
            const float col = ImGui::GetContentRegionAvail().x / 2;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Server name:");
            ImGui::SameLine(lab + 16 * s);
            ImGui::SetNextItemWidth(col - lab - 30 * s);
            ImGui::InputText("##sname", g_serverName, sizeof(g_serverName));
            ImGui::SameLine(col + 16 * s);
            ImGui::Checkbox("Allow custom players", &g_guestChars);
            ImGui::Checkbox("Password", &g_usePw);
            ImGui::SameLine(lab + 16 * s);
            ImGui::BeginDisabled(!g_usePw);
            PasswordBox("##hpw", g_hostPw, sizeof(g_hostPw), &g_showHostPw, col - lab - 90 * s, s);
            ImGui::EndDisabled();
            ImGui::SameLine(col + 16 * s);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Max players:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70 * s);
            ImGui::InputInt("##maxp", &g_maxPlayers, 0, 0);
            g_maxPlayers = std::max(0, std::min(250, g_maxPlayers));
            ImGui::SameLine();
            ImGui::TextDisabled("(0 = no limit)");
            ImGui::Checkbox("LAN / VPN (no Steam)", &g_lan);
            ImGui::SameLine(col + 16 * s);
            ImGui::BeginDisabled(!g_lan);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("UDP port:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90 * s);
            ImGui::InputInt("##port", &g_port, 0, 0);
            ImGui::EndDisabled();
            ImGui::Unindent(16 * s);
            ImGui::EndDisabled();
        }
    } else if (g_mpSel == 2) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Host IP:");
        ImGui::SameLine(lab);
        ImGui::SetNextItemWidth(220 * s);
        if (ImGui::InputText("##addr", g_addr, sizeof(g_addr))) {
            g_pickSteam = false; // typing an IP replaces the picked Steam server
        }
        ImGui::SameLine();
        ImGui::TextDisabled("LAN / VPN only. Steam servers: pick below.");
    }

    ImGui::Checkbox("Mod share", &g_shareMods);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.f, .5f, .3f, 1), "(Only use with players you trust)");

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Servers");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 90 * s);
    if (ImGui::Button("Refresh", ImVec2(90 * s, 0))) {
        lastAsk = SDL_GetTicks();
        RefreshServers();
    }
    const std::vector<ServerEntry> list = ListSource();
    const float listH = std::max(60 * s, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 2.2f);
    std::string note;
    if (ImGui::BeginTable("##servers", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_ScrollY, ImVec2(0, listH))) {
        ImGui::TableSetupColumn("Server", ImGuiTableColumnFlags_WidthStretch, 2.2f);
        ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableHeadersRow();
        for (const ServerEntry& e : list) {
            const bool own = IsOwnServer(e) || e.addr == "(this PC)";
            const bool canOpen = e.answered && (own || !e.info.list.empty());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(RowKey(e).c_str());
            const bool sel = !own && e.addr == g_pickedServer;
            if (ImGui::Selectable("##row", sel, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(0) && canOpen) { // show / hide its players
                    std::set<std::string>& set = own ? g_closedOwn : g_opened;
                    if (!set.erase(RowKey(e))) {
                        set.insert(RowKey(e));
                    }
                } else if (!own) { // picking a server = connecting to it
                    g_pickedServer = e.addr;
                    g_pickSteam = e.steam;
                    snprintf(g_addr, sizeof(g_addr), "%s", e.steam ? "" : e.addr.c_str());
                    g_mpSel = 2;
                }
            }
            if (ImGui::IsItemHovered()) {
                note = own ? "Your server. Double click to show or hide its players."
                       : !e.answered ? e.addr + " did not answer."
                       : e.info.version != LUGARUEX_VERSION ? "Version " + e.info.version + " (yours: " LUGARUEX_VERSION "): can't be joined."
                       : std::string(e.steam ? "Through Steam. " : e.lan ? "On your network. " : "Direct. ") +
                             (e.info.password ? "Password protected (players hidden)." : "Double click to show who is there.");
            }
            ImGui::SameLine(0, 0);
            ImGui::TextDisabled("%s", canOpen ? (Opened(e) ? "v " : "> ") : "  ");
            ImGui::SameLine(0, 0);
            ColouredText(e.info.name, !e.answered);
            ImGui::TableNextColumn();
            const int players = own ? 1 + (int)g_link.st.players.size() : e.info.players;
            const std::string count = !e.answered ? "-" : e.info.maxPlayers > 0 ? std::to_string(players) + "/" + std::to_string(e.info.maxPlayers) : std::to_string(players);
            ImGui::TextUnformatted(count.c_str());
            ImGui::TableNextColumn();
            std::string st = own ? "Yours" : !e.answered ? "No answer" : e.info.version != LUGARUEX_VERSION ? "Version " + e.info.version : (e.info.inGame ? "In game" : "In menu");
            if (e.answered && e.info.password) {
                st += ", locked";
            }
            if (!own && e.answered && e.info.shareMods) {
                st += ", mods";
            }
            ImGui::TextUnformatted(st.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(own ? "" : e.answered && e.pingMs >= 0 ? (std::to_string(e.pingMs) + " ms").c_str() : "-");
            // its players
            if (canOpen && Opened(e)) {
                if (own) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("      %s (you)", netinfo::CleanName(g_set.name).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("Host");
                    ImGui::TableNextColumn();
                    for (const AdminPlayer& a : g_link.st.players) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::PushID((int)a.id);
                        ImGui::Selectable(("      " + a.name).c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                        if (ImGui::IsItemHovered()) {
                            note = "Right click a player to kick or ban them (they stay behind as an enemy).";
                        }
                        if (ImGui::BeginPopupContextItem("##player")) {
                            if (ImGui::MenuItem(("Kick " + a.name).c_str())) {
                                g_link.Kick(a.id, false);
                            }
                            if (ImGui::MenuItem(("Ban " + a.name + " (until you stop hosting)").c_str())) {
                                g_link.Kick(a.id, true);
                            }
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                        ImGui::TableNextColumn();
                        if (a.seats > 1) {
                            ImGui::TextDisabled("%d players", a.seats);
                        }
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", a.inGame ? "In game" : "Waiting");
                        ImGui::TableNextColumn();
                    }
                } else {
                    for (const netinfo::Player& pl : e.info.list) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("      %s", pl.name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", netinfo::StateName(pl.state));
                        ImGui::TableNextColumn();
                    }
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (note.empty()) {
        note = GameRunning() && g_link.st.heard && g_link.st.role == 2 && !g_link.st.status.empty() ? "Game: " + g_link.st.status
               : g_steamListState == 1 && !g_steamAnswered ? "Asking Steam..."
               : !g_steamListError.empty() ? g_steamListError + " (LAN / VPN servers still show.)"
                                           : "Pick a server to join it. Through Steam nobody sees anyone's IP address.";
    }
    ImGui::TextWrapped("%s", note.c_str());
}

// Play with Join and Mod share on: ask when the server shares mods (or isn't known)
bool ModShareQuestion(std::string& text)
{
    if (g_set.mpmode != "join" || !g_set.shareMods) {
        return false;
    }
    const ServerEntry* known = nullptr;
    known = JoinTarget();
    if (known && !known->info.shareMods) {
        return false;
    }
    if (known) {
        text = "\"" + netinfo::StripColours(known->info.name) + "\" shares " + std::to_string(known->info.mods) + (known->info.mods == 1 ? " mod" : " mods") +
               ". With Mod share on, they are downloaded and used in your game.";
    } else {
        text = "Mod share is on: if this server shares mods, they are downloaded and used in your game.";
    }
    text += "\n\nOnly join servers of players you trust.";
    return true;
}
void TabMods(float s)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    const float listH = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() - ImGui::GetTextLineHeightWithSpacing() * 2.6f;
    if (ImGui::BeginTable("##mods", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter,
                          ImVec2(avail, listH))) {
        ImGui::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("Version / author", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        int openMenu = -1;
        for (int i = 0; i < (int)g_modOrder.size(); i++) {
            ModEntry& e = g_modOrder[i];
            const ModInfo* m = ModInfoOf(e.folder);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(i);
            ImGui::Checkbox("##on", &e.on);
            if (ImGui::IsItemFocused()) {
                g_modSel = i;
                if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceLeft, false) || ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                    openMenu = i; // controller X / Delete: the same menu as a right-click
                }
            }
            ImGui::SameLine();
            if (ImGui::Selectable(m ? m->name.c_str() : e.folder.c_str(), g_modSel == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                g_modSel = i;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                g_modSel = i;
                openMenu = i;
            }
            if (m && !m->description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("%s", m->description.c_str());
            }
            ImGui::TableSetColumnIndex(1);
            if (m) {
                ImGui::Text(m->bytes >= 1048576 ? "%.1f MB" : "%.0f KB", m->bytes >= 1048576 ? m->bytes / 1048576.0 : m->bytes / 1024.0 + 0.5);
            }
            ImGui::TableSetColumnIndex(2);
            if (m) {
                const std::string by = m->version.empty() ? m->author : (m->author.empty() ? m->version : m->version + " - " + m->author);
                ImGui::TextUnformatted(by.c_str());
            }
            ImGui::PopID();
        }
        if (openMenu >= 0) {
            ImGui::OpenPopup("##modmenu");
        }
        if (ImGui::BeginPopup("##modmenu")) {
            if (g_modSel >= 0 && g_modSel < (int)g_modOrder.size()) {
                ModEntry& e = g_modOrder[g_modSel];
                if (ImGui::MenuItem(e.on ? "Switch off" : "Switch on")) {
                    e.on = !e.on;
                }
                if (ImGui::MenuItem("Open its folder")) {
                    OpenFolder(fsu::Join(ModsDir(), e.folder));
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete...")) {
                    g_modDelete = e.folder;
                }
            }
            ImGui::EndPopup();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Add mod...")) {
        const std::vector<std::string> files = PickZips();
        if (files.size() == 1 && files[0] == "?") {
            g_importModel = "mod"; // no file picker here: type the path
            g_importFor = "mods";
            g_importPath[0] = 0;
        } else {
            InstallMods(files);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(g_modSel <= 0);
    if (ImGui::Button("Up")) {
        std::swap(g_modOrder[g_modSel], g_modOrder[g_modSel - 1]);
        g_modSel--;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(g_modSel < 0 || g_modSel + 1 >= (int)g_modOrder.size());
    if (ImGui::Button("Down")) {
        std::swap(g_modOrder[g_modSel], g_modOrder[g_modSel + 1]);
        g_modSel++;
    }
    ImGui::EndDisabled();
    ImGui::SameLine(avail - ImGui::CalcTextSize("Mods folder").x - ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button("Mods folder")) {
        OpenFolder(ModsDir());
    }
    ImGui::PushTextWrapPos(avail);
    ImGui::TextDisabled("%s", !g_modNote.empty() ? g_modNote.c_str()
                              : g_modOrder.empty() ? "No mods yet. Drag mod .zip files onto this window, or use Add mod... A mod is a zip of game files (Textures, Maps, Sounds, Campaigns...)."
                                                   : "Ticked mods are on; the higher one wins when two change the same file. Right-click a mod (X on a controller) to delete it.");
    ImGui::PopTextWrapPos();
    (void)s;
}

// ============================================================================ installer + updates
// Run from an unpacked release outside a game folder, this program is the installer:
// it finds the game (Steam, the open-source version, or the free data it can download)
// and puts LugaruEX in. Updates are looked for when the launcher opens; the player is
// asked, then the update is downloaded, put in place and the new launcher started.
bool g_setupMode = false;
std::string g_package; // the unpacked release (holds LugaruEX/)
std::vector<GameFolder> g_games;
int g_gameSel = 0;
bool g_setupShortcut = true;
std::atomic<int> g_freeState(0); // free data: 0 idle, 1 downloading, 2 ready, 3 failed
std::string g_freeError;
std::string g_installed;       // after installing: the installed launcher
std::string g_installedNote;
// 0 nothing, 3 a newer release (ask), 1 downloading, 2 downloaded (install), 4 download failed
std::atomic<int> g_updState(0);
UpdateInfo g_updInfo;   // written before g_updState becomes 3
std::string g_updError; // written before g_updState becomes 4
std::atomic<uint64_t> g_updBytes(0);

// this launcher's version (test hook LUGARUEX_TEST_UPDATE_FROM=<version>: pretend to be older)
std::string CurrentVersion()
{
    const char* e = getenv("LUGARUEX_TEST_UPDATE_FROM");
    return e && *e ? e : LUGARUEX_VERSION;
}

void DecideSetupMode(const std::string& exeDir)
{
    if (DirExists(g_gameDir + "Data/Maps") || !FileExists(exeDir + "lugaruex")) {
        return; // the launcher of an installed LugaruEX (or a developer build)
    }
    g_package = fsu::Parent(exeDir);
    g_setupMode = true;
    g_games = Setup_FindGames();
    if (getenv("LUGARUEX_TEST_NOGAMES")) { // test hook: as on a computer without the game
        g_games.clear();
    }
}

std::string g_freeDir; // where "Download game free" puts the game (the player can change it)

void FreeDataThread()
{
    const std::string dir = g_freeDir, zip = dir + "/lugaru-data.zip";
    fsu::MakeDirs(dir);
    std::string err;
    const bool ok = Http_GetFile(Setup_FreeDataUrl(), zip, nullptr, err) && Setup_UnpackFreeData(zip, dir, err);
    fsu::RemoveTree(zip);
    if (ok) {
        Setup_MarkOwnCopy(dir); // (listed next time, and may be deleted from here)
    }
    g_freeError = err;
    g_freeState = ok ? 2 : 3;
}

std::string PickFolderDialog(const std::string& title = "Choose your Lugaru folder")
{
#ifndef _WIN32
    const std::string z = "zenity --file-selection --directory --title='" + title + "' 2>/dev/null",
                      k = "kdialog --getexistingdirectory \"$HOME\" --title '" + title + "' 2>/dev/null";
    const char* cmds[] = { z.c_str(), k.c_str() };
    for (const char* c : cmds) {
        const std::string tool = std::string(c).substr(0, std::string(c).find(' '));
        if (system(("command -v " + tool + " >/dev/null 2>&1").c_str()) != 0) {
            continue;
        }
        if (FILE* p = popen(c, "r")) {
            char buf[4096] = {};
            const size_t n = fread(buf, 1, sizeof(buf) - 1, p);
            pclose(p);
            return Trim(std::string(buf, n));
        }
    }
#endif
    return "?";
}

void DoInstall()
{
    if (g_gameSel < 0 || g_gameSel >= (int)g_games.size()) {
        g_message = "Pick a game folder first.";
        return;
    }
    GameFolder g = g_games[g_gameSel];
    std::string err, launcher;
    if (!g.writable) {
        // the system's copy of the open-source game: LugaruEX goes into the player's own
        // folder, with the game data linked in
        const std::string own = Setup_FreeGameFolder();
        if (!Setup_LinkSystemData(Setup_IsGameFolder(g.path) ? g.path + "/Data" : g.path, own, err)) {
            g_message = "Installing failed: " + err;
            return;
        }
        g = Setup_Describe(own);
    }
    SetupOptions o;
    o.shortcut = g_setupShortcut;
    if (!Setup_Install(g_package, g, o, launcher, err)) {
        g_message = "Installing failed: " + err;
        return;
    }
    g_installed = launcher;
    g_installedNote = "Installed in " + g.path + ".";
    if (g.steam) {
        g_installedNote += "\n\nFor Steam's Play button, paste this into Lugaru HD > Properties > Launch options:\n\n\"" + launcher + "\" %command%";
    }
}

// the installed launcher of a game folder takes over (this program ends)
void StartInstalled(const std::string& gameDir)
{
#ifndef _WIN32
    const std::string l = fsu::Join(fsu::Join(gameDir, "LugaruEX"), Setup_LauncherName());
    if (chdir(gameDir.c_str()) == 0) {
        execl(l.c_str(), l.c_str(), (char*)nullptr);
    }
    g_message = "Could not start " + l;
#else
    (void)gameDir;
#endif
}

// Add to Steam: Steam must be closed while its list of non-Steam games is changed
std::atomic<int> g_steamState(0); // 0 idle, 1 closing Steam + adding, 2 done
std::string g_steamResult;
void AddToSteamThread(std::string launcher, std::string gameDir)
{
    std::string err;
    if (!Setup_CloseSteam()) {
        g_steamResult = "Steam did not close. Close it yourself (Steam menu > Exit), then press Add to Steam again.";
        g_steamState = 2;
        return;
    }
    const int r = Setup_AddToSteam(launcher, gameDir, err);
    Setup_StartSteam();
    g_steamResult = r == 0 ? "Added to Steam as \"Lugaru EX\". Steam is starting again; you'll find it in your library (Non-Steam)."
                  : r == 1 ? "Lugaru EX is already in your Steam library."
                           : "Could not add it to Steam: " + err;
    g_steamState = 2;
}

void AddToSteam(const std::string& gameDir)
{
    const std::string launcher = fsu::Join(fsu::Join(gameDir, "LugaruEX"), Setup_LauncherName());
    std::string err;
    const int r = Setup_AddToSteam(launcher, gameDir, err);
    if (r == 0) {
        g_message = "Added to Steam as \"Lugaru EX\" (in your library under Non-Steam). It opens this launcher, which keeps LugaruEX up to date.";
    } else if (r == 1) {
        g_message = "Lugaru EX is already in your Steam library.";
    } else if (r == 2) {
        ImGui::OpenPopup("Steam is running");
    } else {
        g_message = "Could not add it to Steam: " + err;
    }
}

// "Steam is running": close it, add, start it again (in the background)
void SteamPopups(float btnW, const std::string& gameDir)
{
    if (ImGui::BeginPopupModal("Steam is running", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const int st = g_steamState;
        if (st == 0) {
            ImGui::TextUnformatted("Steam is open. It closes for a moment while Lugaru EX is added\nto your library, then starts again by itself.");
            ImGui::Spacing();
            if (ImGui::Button("Close Steam and add", ImVec2(btnW * 1.8f, 0))) {
                g_steamState = 1;
                const std::string launcher = fsu::Join(fsu::Join(gameDir, "LugaruEX"), Setup_LauncherName());
                std::thread(AddToSteamThread, launcher, gameDir).detach();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(btnW, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
                ImGui::CloseCurrentPopup();
            }
        } else if (st == 1) {
            ImGui::TextUnformatted("Closing Steam and adding Lugaru EX...");
        } else {
            g_message = g_steamResult;
            g_steamState = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// true = keep the window open
bool SetupScreen(float s, float bannerH, float btnW)
{
    ImGuiIO& io = ImGui::GetIO();
    const float pad = 14 * s;
    ImGui::SetCursorPos(ImVec2(pad, bannerH + 10 * s));
    ImGui::BeginChild("##setup", ImVec2(io.DisplaySize.x - 2 * pad, io.DisplaySize.y - bannerH - 20 * s), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    if (!g_installed.empty()) {
        ImGui::TextUnformatted("Done!");
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextUnformatted(g_installedNote.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Start LugaruEX", ImVec2(btnW * 1.6f, 0))) {
            StartInstalled(fsu::Parent(fsu::Parent(g_installed)));
            ImGui::EndChild();
            return false;
        }
#ifndef _WIN32
        ImGui::SameLine();
        if (ImGui::Button("Add to Steam", ImVec2(btnW * 1.4f, 0))) {
            AddToSteam(fsu::Parent(fsu::Parent(g_installed)));
        }
        SteamPopups(btnW, fsu::Parent(fsu::Parent(g_installed)));
#endif
        ImGui::SameLine();
        const bool quit = ImGui::Button("Quit", ImVec2(btnW, 0));
        ImGui::EndChild();
        return !quit;
    }
    // nothing found: ask straight away - pick the folder, or get the free game
    static bool asked = false;
    bool pickNow = false, freeNow = false;
    if (g_games.empty() && !asked && g_freeState == 0) {
        asked = true;
        ImGui::OpenPopup("No game found");
    }
    if (ImGui::BeginPopupModal("No game found", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Spacing();
        if (ImGui::Button("Pick game folder", ImVec2(btnW * 2, 0))) {
            pickNow = true;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Download game free", ImVec2(btnW * 2, 0))) {
            freeNow = true;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::Button("Not now", ImVec2(btnW * 2, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // first time: the folder that already has this LugaruEX (then Play needs no install)
    static bool picked = false;
    if (!picked && !g_games.empty()) {
        picked = true;
        g_gameSel = 0;
        for (int i = (int)g_games.size() - 1; i >= 0; i--) {
            if (g_games[i].hasLugaruEX) {
                g_gameSel = i;
            }
        }
        for (int i = (int)g_games.size() - 1; i >= 0; i--) {
            if (g_games[i].exVersion == CurrentVersion()) {
                g_gameSel = i;
            }
        }
    }
    ImGui::TextUnformatted("Game folders:");
    const float listH = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 5.4f - ImGui::GetTextLineHeightWithSpacing() * 2;
    ImGui::BeginChild("##games", ImVec2(0, listH), ImGuiChildFlags_Borders);
    for (int i = 0; i < (int)g_games.size(); i++) {
        const GameFolder& g = g_games[i];
        std::string status;
        if (g.hasLugaruEX) {
            const int c = g.exVersion == "?" ? -1 : Setup_CompareVersions(g.exVersion, CurrentVersion());
            status = "    LugaruEX " + (g.exVersion == "?" ? std::string("(older)") : g.exVersion) + (c < 0 ? " - update to " LUGARUEX_VERSION : c > 0 ? " (newer)" : " installed");
        }
        const std::string label = g.kind + "    " + g.path + status;
        ImGui::PushID(i);
        if (ImGui::Selectable(label.c_str(), g_gameSel == i)) {
            g_gameSel = i;
        }
        ImGui::PopID();
    }
    if (g_games.empty()) {
        ImGui::TextWrapped("No game found.");
    }
    ImGui::EndChild();
    // what the picked folder can do
    const bool have = g_gameSel >= 0 && g_gameSel < (int)g_games.size();
    const GameFolder sel = have ? g_games[g_gameSel] : GameFolder();
    const bool installed = have && sel.hasLugaruEX;
    const bool same = installed && sel.exVersion == CurrentVersion();
    ImGui::BeginDisabled(!installed);
    if (ImGui::Button("Play", ImVec2(btnW, 0)) || (same && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false))) {
        StartInstalled(sel.path); // (already installed: nothing to copy)
    }
#ifndef _WIN32
    ImGui::SameLine();
    if (ImGui::Button("Add to Steam", ImVec2(btnW * 1.4f, 0))) {
        AddToSteam(sel.path);
    }
#endif
    ImGui::EndDisabled();
#ifndef _WIN32
    SteamPopups(btnW, sel.path);
#endif
    ImGui::SameLine();
    ImGui::BeginDisabled(!installed && !(have && sel.ownCopy));
    if (ImGui::Button("Remove...", ImVec2(btnW, 0))) {
        ImGui::OpenPopup("Remove");
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("Remove", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(460 * s);
        if (installed) {
            ImGui::TextWrapped("Remove LugaruEX from %s?\n\nIts folder %s/LugaruEX goes, with the settings, mods, presets and screenshots in it. "
                               "The game itself stays.", sel.path.c_str(), sel.path.c_str());
        }
        if (sel.ownCopy) {
            ImGui::TextWrapped("%s is a free copy of the game LugaruEX downloaded. You can delete the whole folder.", sel.path.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        std::string err;
        bool done = false, ok = false;
        if (installed && ImGui::Button("Remove LugaruEX", ImVec2(btnW * 1.6f, 0))) {
            done = true;
            ok = Setup_Remove(sel, false, err);
        }
        if (sel.ownCopy) {
            if (installed) {
                ImGui::SameLine();
            }
            if (ImGui::Button("Delete whole folder", ImVec2(btnW * 1.6f, 0))) {
                done = true;
                ok = Setup_Remove(sel, true, err);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(btnW, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
            ImGui::CloseCurrentPopup();
        }
        if (done) {
            g_message = ok ? "Removed." : "Could not remove it: " + err;
            g_games = Setup_FindGames();
            g_gameSel = g_games.empty() ? -1 : 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::Button("Browse...") || pickNow) {
        const std::string p = PickFolderDialog();
        if (p == "?") {
            g_message = "No folder picker here. Put the LugaruEX folder in your game folder and run it from there.";
        } else if (!p.empty()) {
            GameFolder g = Setup_Describe(p);
            if (g.path.empty()) {
                g_message = "No game in that folder.";
            } else {
                g_games.insert(g_games.begin(), g);
                g_gameSel = 0;
            }
        }
    }
    ImGui::SameLine();
    const int fs = g_freeState;
    // where the free game goes: the player's choice (an empty or new folder as it is,
    // else a "Lugaru" folder in it)
    static char freeDir[1024] = "";
    if (!freeDir[0]) {
        snprintf(freeDir, sizeof(freeDir), "%s", Setup_FreeGameFolder().c_str());
    }
    ImGui::BeginDisabled(fs == 1);
    if (ImGui::Button(fs == 1 ? "Downloading game..." : "Download game free") || (freeNow && fs == 0)) {
        std::string dir = Trim(freeDir);
        while (dir.size() > 1 && dir.back() == '/') {
            dir.pop_back();
        }
        if (dir.empty()) {
            dir = Setup_FreeGameFolder();
        }
        if (dir != Setup_FreeGameFolder() && fsu::IsDir(dir) && (!fsu::List(dir, true).empty() || !fsu::List(dir, false).empty())) {
            dir = fsu::Join(dir, "Lugaru");
        }
        const std::string probe = fsu::Join(dir, ".lugaruex-write-test");
        if (!fsu::MakeDirs(dir) || !fsu::WriteAll(probe, "x")) {
            g_message = "Can't write to " + dir + ". Pick a folder of your own.";
        } else {
            fsu::RemoveTree(probe);
            g_freeDir = dir;
            g_freeState = 1;
            std::thread(FreeDataThread).detach();
        }
    }
    ImGui::TextUnformatted("Download to:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW * 0.6f - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputText("##freedir", freeDir, sizeof(freeDir));
    ImGui::SameLine();
    if (ImGui::Button("Change...", ImVec2(btnW * 0.6f, 0))) {
        const std::string p = PickFolderDialog("Choose where to put the game");
        if (!p.empty() && p != "?") {
            snprintf(freeDir, sizeof(freeDir), "%s", p.c_str());
        }
    }
    ImGui::EndDisabled();
    if (fs >= 2) {
        if (fs == 2) {
            g_games = Setup_FindGames();
            g_gameSel = -1;
            for (int i = 0; i < (int)g_games.size(); i++) {
                if (g_games[i].path == g_freeDir) {
                    g_gameSel = i;
                }
            }
            if (g_gameSel < 0) { // a folder of the player's choosing: not one setup looks in by itself
                const GameFolder g = Setup_Describe(g_freeDir);
                if (!g.path.empty()) {
                    g_games.insert(g_games.begin(), g);
                }
                g_gameSel = 0;
            }
        } else {
            g_message = "Download failed: " + g_freeError;
        }
        g_freeState = 0;
    }
    ImGui::Checkbox("Add to app menu", &g_setupShortcut);
    ImGui::Spacing();
    ImGui::BeginDisabled(fs == 1);
    {
        const bool curSame = g_gameSel >= 0 && g_gameSel < (int)g_games.size() && g_games[g_gameSel].exVersion == CurrentVersion();
        const bool curHas = g_gameSel >= 0 && g_gameSel < (int)g_games.size() && g_games[g_gameSel].hasLugaruEX;
        const char* label = curSame ? "Reinstall" : curHas ? "Update" : "Install";
        if (ImGui::Button(label, ImVec2(btnW, 0)) || (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) && fs != 1 && !curSame)) {
            DoInstall();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool quit = ImGui::Button("Quit", ImVec2(btnW, 0));
    ImGui::EndChild();
    return !quit;
}

// looked for when the launcher opens; the player is asked before anything is downloaded
void UpdateThread(std::string repo)
{
    UpdateInfo info;
    std::string err;
    if (!Update_Latest(repo, Update_Platform(), info, err)) {
        printf("LugaruEX launcher: no update information: %s\n", err.c_str());
        fflush(stdout);
    } else if (Update_IsNewer(info.version, CurrentVersion())) {
        g_updInfo = info;
        g_updState = 3;
    }
}

void UpdateDownloadThread(UpdateInfo info)
{
    const std::string dir = g_exDir + "update";
    fsu::RemoveTree(dir);
    fsu::MakeDirs(dir);
    std::string err;
    if (Update_Download(info, dir + "/" + info.asset, &g_updBytes, err) && fsu::WriteAll(dir + "/ready.txt", info.version + "\n" + info.asset + "\n")) {
        g_updState = 2;
    } else {
        fsu::RemoveTree(dir);
        g_updError = err;
        g_updState = 4;
    }
}

// a downloaded update: put it in place (false: nothing waiting, or err says why it failed)
bool InstallPendingUpdate(std::string& err)
{
    const std::string dir = g_exDir + "update";
    std::string ready;
    if (!fsu::ReadAll(dir + "/ready.txt", ready)) {
        return false;
    }
    const std::string version = Trim(ready.substr(0, ready.find('\n')));
    const std::string asset = Trim(ready.substr(ready.find('\n') + 1));
    std::string launcher;
    bool ok = false;
    if (Update_IsNewer(version, CurrentVersion()) && !asset.empty() && asset.find_first_of("/\\") == std::string::npos) {
        GameFolder g;
        g.path = g_gameDir.substr(0, g_gameDir.size() - 1);
        SetupOptions o;
        o.shortcut = false;
        ok = Setup_Unpack(dir + "/" + asset, dir + "/unpacked", err) && Setup_Install(dir + "/unpacked", g, o, launcher, err);
    }
    fsu::RemoveTree(dir); // used or not, never twice
    return ok;
}

// this program's path, taken at start (an update replaces the file while it runs)
std::string g_selfPath;

// start the (new) launcher at this program's path with the same arguments
void RelaunchSelf(char** argv)
{
#ifndef _WIN32
    const std::string self = g_selfPath.empty() ? Setup_SelfPath() : g_selfPath;
    execv(self.c_str(), argv);
    fprintf(stderr, "LugaruEX launcher: could not start the new launcher (%s): %s\n", self.c_str(), strerror(errno));
#endif
    (void)argv;
}

// at start: an update downloaded earlier goes in, then the new launcher (true = this one is done)
bool ApplyPendingUpdate(char** argv)
{
    std::string err;
    if (!InstallPendingUpdate(err)) {
        if (!err.empty()) {
            fprintf(stderr, "LugaruEX launcher: the update could not be installed: %s\n", err.c_str());
        }
        return false;
    }
    RelaunchSelf(argv);
    return false;
}

} // namespace

// ============================================================================ main
int main(int argc, char** argv)
{
    SDL_SetMainReady();
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') { // game options (Steam adds its own command after them: not ours)
            g_passArgs.push_back(argv[i]);
        }
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "LugaruEX launcher: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    FindFolders();
    g_selfPath = Setup_SelfPath();
    Setup_CleanOld(g_selfPath); // left by an update
    {
        char* bp = SDL_GetBasePath();
        std::string exeDir = bp ? bp : "./";
        SDL_free(bp);
        std::replace(exeDir.begin(), exeDir.end(), '\\', '/');
        DecideSetupMode(exeDir); // run from an unpacked release: the installer
    }
    MakeDirs(g_exDir + "Overrides");
    LoadSettings();
    if (!g_setupMode && g_set.autoUpdate && ApplyPendingUpdate(argv)) {
        return 0; // (the new launcher took over)
    }
    if (getenv("LUGARUEX_TEST_ADDTOSTEAM") && !g_setupMode) {
        // test hook: add this install to Steam (as the button does) and say how it went
        const std::string dir = g_gameDir.substr(0, g_gameDir.size() - 1);
        const std::string l = fsu::Join(fsu::Join(dir, "LugaruEX"), Setup_LauncherName());
        std::string err;
        const int r = Setup_AddToSteam(l, dir, err);
        printf("LugaruEX launcher: add to Steam -> %d%s%s, in Steam: %d\n", r, err.empty() ? "" : " ", err.c_str(), Setup_InSteam(l) ? 1 : 0);
        fflush(stdout);
    }
    EnumMonitors();
    InitFromSettings();
    SetDefaultControls();
    ReadControls(g_ini, true);
    FillPresets();
    FillChars();
    LoadLocal();
    Pad_Init();
    PreviewCore_SetDataDir(g_gameDir + "Data/");
    g_modOrder = Mod_ParseList(g_ini.Get("Mods", "List"));
    RefreshMods();

    // size: a classic dialog, scaled for high-DPI screens and never taller than the screen
    float ddpi = 96;
    if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) != 0 || ddpi < 50) {
        ddpi = 96;
    }
    float scale = std::max(1.f, std::min(2.5f, ddpi / 96.f));
    if (const char* e = getenv("LUGARUEX_LAUNCHER_SCALE")) {
        scale = (float)atof(e);
    }
    SDL_DisplayMode dm;
    int baseW = 680, baseH = 720;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
        scale = std::min(scale, (dm.h - 60) / (float)baseH);
        scale = std::max(scale, 0.8f);
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* win = SDL_CreateWindow(g_setupMode ? "LugaruEX Setup" : "Lugaru HD - LugaruEX Configuration", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       (int)(baseW * scale), (int)(baseH * scale), SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        fprintf(stderr, "LugaruEX launcher: no window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gl);
    SDL_GL_SetSwapInterval(1);
    {
        Image icon;
        const std::string ip = g_exDir + "lugaruex.png";
        if (ReadImage(ip, icon)) {
            if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(icon.px.data(), icon.w, icon.h, 32, icon.w * 4, SDL_PIXELFORMAT_RGBA32)) {
                SDL_SetWindowIcon(win, s);
                SDL_FreeSurface(s);
            }
        }
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 0;
    st.FrameRounding = 3;
    st.GrabRounding = 3;
    st.TabRounding = 3;
    st.FramePadding = ImVec2(8, 5);
    st.ItemSpacing = ImVec2(8, 7);
    ImVec4* c = st.Colors;
    const ImVec4 red(0.62f, 0.13f, 0.08f, 1), redHi(0.80f, 0.22f, 0.12f, 1), redLo(0.40f, 0.09f, 0.06f, 1);
    c[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.08f, 0.08f, 1);
    c[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.10f, 0.10f, 1);
    c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.10f, 0.10f, 0.98f);
    c[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.15f, 0.14f, 1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.17f, 0.14f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.34f, 0.18f, 0.14f, 1);
    c[ImGuiCol_Button] = redLo;
    c[ImGuiCol_ButtonHovered] = red;
    c[ImGuiCol_ButtonActive] = redHi;
    c[ImGuiCol_Header] = redLo;
    c[ImGuiCol_HeaderHovered] = red;
    c[ImGuiCol_HeaderActive] = redHi;
    c[ImGuiCol_Tab] = ImVec4(0.20f, 0.10f, 0.08f, 1);
    c[ImGuiCol_TabHovered] = red;
    c[ImGuiCol_TabSelected] = ImVec4(0.52f, 0.12f, 0.07f, 1);
    c[ImGuiCol_TabSelectedOverline] = ImVec4(1.f, 0.55f, 0.2f, 1);
    c[ImGuiCol_CheckMark] = ImVec4(1.f, 0.55f, 0.25f, 1);
    c[ImGuiCol_SliderGrab] = redHi;
    c[ImGuiCol_SliderGrabActive] = ImVec4(1.f, 0.45f, 0.2f, 1);
    c[ImGuiCol_NavCursor] = ImVec4(1.f, 0.6f, 0.25f, 1);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.20f, 0.13f, 0.11f, 1);
    c[ImGuiCol_Separator] = ImVec4(0.30f, 0.20f, 0.17f, 1);
    st.ScaleAllSizes(scale);
    const std::string uiFont = FindFontFile(false);
    if (uiFont.empty() || !io.Fonts->AddFontFromFileTTF(uiFont.c_str(), 17.f * scale)) {
        ImFontConfig fc;
        fc.SizePixels = 13.f * scale;
        io.Fonts->AddFontDefault(&fc);
    }
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL2_Init();

    // the banner, made from the user's Data (and the main menu's logo with it)
    GLuint bannerTex = 0;
    int bannerW = 0, bannerH = 0;
    {
        int dw, dh;
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        bannerW = dw;
        bannerH = (int)(dw * 0.25f);
        Image banner;
        // (the installer borrows the art of the first game it found, and writes nothing)
        BuildBanner(g_setupMode && !g_games.empty() ? g_games[0].path + "/" : g_gameDir, g_setupMode ? std::string() : g_exDir, bannerW, bannerH, banner);
        bannerTex = MakeTexture(banner);
        if (const char* sp = getenv("LUGARUEX_SAVE_BANNER")) {
            SavePNG(sp, banner);
        }
    }
    const bool haveData = DirExists(g_gameDir + "Data/Maps");
    if (!g_setupMode && g_set.autoUpdate) { // look for a new LugaruEX meanwhile (the player is asked)
        const std::string repo = g_ini.Get("Launcher", "UpdateRepo");
        std::thread(UpdateThread, repo.empty() ? std::string(kUpdateRepo) : repo).detach();
    }
    // test hook LUGARUEX_LAUNCHER_SHOT=<file.png>[,<tab 0-4>]: save a screenshot of a tab and quit
    std::string shotFile;
    int shotTab = -1, frames = 0;
    if (const char* e = getenv("LUGARUEX_LAUNCHER_SHOT")) {
        shotFile = e;
        const size_t comma = shotFile.find(',');
        if (comma != std::string::npos) {
            shotTab = atoi(shotFile.c_str() + comma + 1);
            shotFile = shotFile.substr(0, comma);
        }
    }

    int tab = 0, wantTab = shotTab >= 0 ? shotTab : std::max(0, std::min(5, g_set.tab)); // last time's tab
    bool running = true, play = false, applyUpdate = false, updAsked = false, modShareOk = false, pwOk = false;
    int pwAsk = 0; // the password window: 1 before joining (OK plays), 2 the running game asks
    std::string pwText;
    char pwBuf[80] = "";
    bool pwShow = false;
    Uint32 pwAskedAt = 0;
    std::string modShareText;
    Uint32 updAskedAt = 0;
    Uint64 last = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            Pad_Event(e);
            // capturing a key / mouse button for a binding: keep it away from the UI
            if (g_capRow >= 0 && g_capCol == 1) {
                if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                        g_capRow = -1;
                        g_hint.clear();
                    } else if (const char* n = SDL_GetScancodeName(e.key.keysym.scancode)) {
                        if (*n) {
                            Assign(g_capRow, 1, n);
                        }
                    }
                    continue;
                }
                if (e.type == SDL_MOUSEBUTTONDOWN) {
                    const char* n = e.button.button == SDL_BUTTON_LEFT ? "Mouse Left" : e.button.button == SDL_BUTTON_RIGHT ? "Mouse Right"
                                  : e.button.button == SDL_BUTTON_MIDDLE ? "Mouse Middle" : e.button.button == SDL_BUTTON_X1 ? "Mouse 4" : "Mouse 5";
                    Assign(g_capRow, 1, n);
                    continue;
                }
                if (e.type == SDL_KEYUP || e.type == SDL_TEXTINPUT || e.type == SDL_MOUSEBUTTONUP) {
                    continue;
                }
                if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                    g_capRow = -1; // controller users can back out of a keyboard binding
                    g_hint.clear();
                    continue;
                }
            }
            if (g_capRow >= 0 && g_capCol == 2 && e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                g_capRow = -1;
                g_hint.clear();
                continue;
            }
            const bool capturingPad = g_capRow >= 0 && g_capCol == 2;
            if (!capturingPad && e.type == SDL_CONTROLLERBUTTONDOWN) {
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
                    wantTab = (tab + 5) % 6;
                } else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                    wantTab = (tab + 1) % 6;
                } else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_START && g_message.empty() && g_importModel.empty() && g_updState == 0) {
                    play = true;
                }
            }
            if (e.type == SDL_DROPFILE) { // mod zips dropped on the window
                std::vector<std::string> files = { e.drop.file ? e.drop.file : "" };
                SDL_free(e.drop.file);
                InstallMods(files);
                wantTab = 5;
                continue;
            }
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE)) {
                running = false;
            }
        }
        Pad_Update();
        // pad binding: the first button pressed after every button was let go
        if (g_capRow >= 0 && g_capCol == 2) {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
            bool anyHeld = false;
            for (auto& p : Pad_List()) {
                for (int b = 0; b < PAD_BUTTONS; b++) {
                    anyHeld |= p.connected && p.btn[b];
                }
                anyHeld |= p.connected && (p.lt > 0.5f || p.rt > 0.5f);
            }
            if (!g_capArmed) {
                g_capArmed = !anyHeld;
            } else {
                for (auto& p : Pad_List()) {
                    const int code = Pad_NewPress(p);
                    if (code >= 0) {
                        Assign(g_capRow, 2, Pad_CodeName(code));
                        break;
                    }
                }
            }
        } else {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        }
        const Uint64 now = SDL_GetPerformanceCounter();
        PreviewCore_Advance((double)(now - last) / SDL_GetPerformanceFrequency(), SDL_GetTicks());
        last = now;
        if (!g_setupMode) {
            SyncLocal();       // local players follow the controllers, whichever tab is open
            g_servers.Poll();  // server list answers (timed as they are read: every frame)
        }

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        int ww, wh, dw, dh;
        SDL_GetWindowSize(win, &ww, &wh);
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        g_fbScaleX = ww > 0 ? (float)dw / ww : 1;
        g_fbScaleY = wh > 0 ? (float)dh / wh : 1;
        g_fbH = dh;
        const float s = scale;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                            ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();
        const float bh = io.DisplaySize.x * bannerH / std::max(1, bannerW);
        ImGui::Image((ImTextureID)(intptr_t)bannerTex, ImVec2(io.DisplaySize.x, bh));
        if (g_setupMode && g_games.empty()) { // no game art here yet: a plain title
            const float size = bh * 0.38f;
            const char* t = "LUGARU EX";
            const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, t);
            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), size, ImVec2((io.DisplaySize.x - ts.x) / 2, (bh - ts.y) / 2), IM_COL32(225, 60, 20, 255), t);
        }
        const float pad = 10 * s;
        const float btnW = 96 * s;
        if (g_setupMode) {
            if (!SetupScreen(s, bh, btnW)) {
                running = false;
            }
        } else {
        ImGui::SetCursorPos(ImVec2(pad, bh + 6 * s));
        const float footer = ImGui::GetFrameHeightWithSpacing() + 12 * s;
        ImGui::BeginChild("##tabs", ImVec2(io.DisplaySize.x - 2 * pad, io.DisplaySize.y - bh - footer - 6 * s), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
        if (ImGui::BeginTabBar("##tabbar")) {
            const char* names[] = { "Graphics", "Character", "Controls", "Local", "Multiplayer", "Mods" };
            for (int i = 0; i < 6; i++) {
                const ImGuiTabItemFlags f = (wantTab == i) ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(names[i], nullptr, f)) {
                    tab = i;
                    g_openTab = i;
                    ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
                    ImGui::Spacing();
                    switch (i) {
                        case 0:
                            TabGraphics(s);
                            break;
                        case 1:
                            TabCharacter(s);
                            break;
                        case 2:
                            TabControls(s);
                            break;
                        case 3:
                            TabLocal(s);
                            break;
                        case 4:
                            TabMultiplayer(s);
                            break;
                        default:
                            TabMods(s);
                            break;
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            wantTab = -1;
            ImGui::EndTabBar();
        }
        ImGui::EndChild();
        // footer: version, Play!, Quit
        ImGui::SetCursorPos(ImVec2(pad, io.DisplaySize.y - footer + 4 * s));
        ImGui::AlignTextToFramePadding();
        ImGui::Checkbox("Auto Update", &g_autoUpdate);
        ImGui::SameLine();
#ifndef _WIN32
        // Steam Deck / Linux: this launcher as a game in the Steam library (once)
        static int inSteam = -1;
        const std::string gameDirNoSlash = g_gameDir.substr(0, g_gameDir.size() - 1);
        if (inSteam < 0 || (g_steamState == 0 && !g_message.empty())) {
            inSteam = Setup_InSteam(fsu::Join(fsu::Join(gameDirNoSlash, "LugaruEX"), Setup_LauncherName())) ? 1 : 0;
        }
        const bool steamBtn = haveData && inSteam == 0;
        const float steamW = steamBtn ? btnW * 1.3f + 8 * s : 0;
#else
        const float steamW = 0;
#endif
        const float textW = io.DisplaySize.x - ImGui::GetCursorPosX() - pad - 2 * btnW - 20 * s - steamW;
        if (haveData) {
            // with a controller in, how to drive the launcher with it (when there's room)
            bool anyPad = false;
            for (auto& p : Pad_List()) {
                anyPad |= p.connected;
            }
            const char* full = "LugaruEX " LUGARUEX_VERSION "  -  LB / RB tabs, Start plays";
            const char* base = "LugaruEX " LUGARUEX_VERSION;
            const int us = g_updState;
            std::string upd;
            if (us == 1) {
                const uint64_t total = g_updInfo.size;
                upd = "Downloading " + g_updInfo.version + "..." + (total ? " " + std::to_string((int)std::min<uint64_t>(100, g_updBytes * 100 / total)) + "%" : std::string());
            } else if (us == 2) {
                upd = "Installing " + g_updInfo.version + "...";
            }
            ImGui::TextDisabled("%s", !upd.empty() ? upd.c_str() : (anyPad && ImGui::CalcTextSize(full).x <= textW) ? full : base);
        } else {
            ImGui::PushTextWrapPos(pad + textW);
            ImGui::TextColored(ImVec4(1, 0.45f, 0.3f, 1), "No Data folder: put LugaruEX in the Lugaru HD folder");
            ImGui::PopTextWrapPos();
        }
#ifndef _WIN32
        if (steamBtn) {
            ImGui::SameLine(io.DisplaySize.x - pad - 2 * btnW - 8 * s - steamW);
            if (ImGui::Button("Add to Steam", ImVec2(btnW * 1.3f, 0))) {
                AddToSteam(gameDirNoSlash);
            }
        }
        SteamPopups(btnW, gameDirNoSlash);
#endif
        ImGui::SameLine(io.DisplaySize.x - pad - 2 * btnW - 8 * s);
        ImGui::BeginDisabled(g_updState == 1 || g_updState == 2 || GameRunning()); // updating (the game starts from the new launcher), or playing
        if (ImGui::Button(GameRunning() ? "Playing..." : "Play!", ImVec2(btnW, 0))) {
            play = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Quit", ImVec2(btnW, 0))) {
            running = false;
        }
        } // (not the installer)
        ImGui::End();

        // message box
        if (!g_message.empty()) {
            ImGui::OpenPopup("LugaruEX");
        }
        if (ImGui::BeginPopupModal("LugaruEX", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos(420 * s);
            ImGui::TextUnformatted(g_message.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button("OK", ImVec2(btnW, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                g_message.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        // Play with Join + Mod share: this server's mods would be downloaded and used
        if (g_askModShare) {
            g_askModShare = false;
            ImGui::OpenPopup("Mod share");
        }
        if (ImGui::BeginPopupModal("Mod share", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos(420 * s);
            ImGui::TextUnformatted(modShareText.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            if (ImGui::Button("Join", ImVec2(btnW, 0))) {
                modShareOk = true;
                play = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(btnW, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        // the password window: before joining a locked server, or when the game says it's wrong
        if (pwAsk && !ImGui::IsPopupOpen("Password")) {
            ImGui::OpenPopup("Password");
        }
        if (ImGui::BeginPopupModal("Password", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(pwText.c_str());
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(260 * s);
            const bool enter = ImGui::InputText("##pw", pwBuf, sizeof(pwBuf), (pwShow ? 0 : ImGuiInputTextFlags_Password) | ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            ImGui::Checkbox("Show", &pwShow);
            ImGui::Spacing();
            const bool ok = ImGui::Button("OK", ImVec2(btnW, 0)) || enter;
            ImGui::SameLine();
            const bool cancel = ImGui::Button("Cancel", ImVec2(btnW, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight);
            if (ok || cancel) {
                if (ok) {
                    g_joinPassword = pwBuf;
                    if (pwAsk == 1) {
                        pwOk = true; // Play goes on
                        play = true;
                    } else {
                        g_link.Password(pwBuf);
                    }
                }
                memset(pwBuf, 0, sizeof(pwBuf));
                pwAsk = 0;
                pwAskedAt = SDL_GetTicks();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        // a newer LugaruEX: ask once
        if (g_updState == 3 && !updAsked && g_message.empty()) {
            updAsked = true;
            updAskedAt = SDL_GetTicks();
            printf("LugaruEX launcher: update %s available (this is %s), asking\n", g_updInfo.version.c_str(), CurrentVersion().c_str());
            fflush(stdout);
            ImGui::OpenPopup("LugaruEX Update");
        }
        if (ImGui::BeginPopupModal("LugaruEX Update", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s is available. Current version is %s.", g_updInfo.version.c_str(), CurrentVersion().c_str());
            ImGui::Spacing();
            ImGui::TextUnformatted("Update now?");
            ImGui::Spacing();
            // test hook LUGARUEX_TEST_UPDATE_ANSWER=ok|cancel[,seconds]: answer by itself (default after 5 s)
            static const char* testAnswer = getenv("LUGARUEX_TEST_UPDATE_ANSWER");
            const bool testDue = testAnswer && SDL_GetTicks() - updAskedAt > (Uint32)(1000 * (strchr(testAnswer, ',') ? atof(strchr(testAnswer, ',') + 1) : 5.0));
            const bool testOk = testDue && strncmp(testAnswer, "ok", 2) == 0;
            if (ImGui::Button("OK", ImVec2(btnW, 0)) || testOk) {
                printf("LugaruEX launcher: update accepted, downloading %s\n", g_updInfo.asset.c_str());
                fflush(stdout);
                g_updBytes = 0;
                g_updState = 1;
                std::thread(UpdateDownloadThread, g_updInfo).detach();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(btnW, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || (testDue && !testOk)) {
                printf("LugaruEX launcher: update declined\n");
                fflush(stdout);
                g_updState = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (g_updState == 4) {
            printf("LugaruEX launcher: update download failed: %s\n", g_updError.c_str());
            fflush(stdout);
            g_message = "The update could not be downloaded: " + g_updError;
            g_updState = 0;
        } else if (g_updState == 2 && !applyUpdate) {
            // downloaded: keep this session's choices, put it in place, then the new launcher takes over
            if (CollectSettings()) {
                SaveIni();
            }
            std::string err;
            if (InstallPendingUpdate(err)) {
                printf("LugaruEX launcher: update %s installed, starting the new launcher\n", g_updInfo.version.c_str());
                fflush(stdout);
                applyUpdate = true;
                running = false;
            } else {
                printf("LugaruEX launcher: update could not be installed: %s\n", err.c_str());
                fflush(stdout);
                g_message = "The update could not be installed: " + (err.empty() ? std::string("unknown error") : err);
                g_updState = 0;
            }
        }
        // "Delete..." on a mod: ask first
        if (!g_modDelete.empty()) {
            ImGui::OpenPopup("Delete mod");
        }
        if (ImGui::BeginPopupModal("Delete mod", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const ModInfo* m = ModInfoOf(g_modDelete);
            ImGui::Text("Delete the mod \"%s\"? Its files are removed.", m ? m->name.c_str() : g_modDelete.c_str());
            if (ImGui::Button("Delete", ImVec2(btnW, 0))) {
                if (!Mod_Delete(ModsDir(), g_modDelete)) {
                    g_message = "Could not delete all of its files (is the game running?).";
                }
                g_modDelete.clear();
                RefreshMods();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep it", ImVec2(btnW, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
                g_modDelete.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        // "New skin..." without a file picker on this desktop: type the image's path
        if (!g_importModel.empty()) {
            ImGui::OpenPopup("Image file");
        }
        if (ImGui::BeginPopupModal("Image file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(g_importFor == "mods" ? "Full path of the mod's .zip file:" : "Full path of the image (PNG, JPG, BMP, GIF or TGA):");
            ImGui::SetNextItemWidth(420 * s);
            ImGui::InputText("##imgpath", g_importPath, sizeof(g_importPath));
            if (ImGui::Button("Import", ImVec2(btnW, 0))) {
                CustomSkin made;
                const std::string model = g_importModel, forWhat = g_importFor;
                g_importModel.clear();
                ImGui::CloseCurrentPopup();
                if (forWhat == "mods") {
                    InstallMods({ Trim(g_importPath) });
                } else if (ImportImage(Trim(g_importPath), model, made)) {
                    if (forWhat == "char") {
                        g_skin = made.enginePath;
                    } else if (forWhat == "clothes") {
                        g_clothes = "custom:" + made.enginePath;
                    } else if (forWhat.rfind("local:", 0) == 0) {
                        const int i = atoi(forWhat.c_str() + 6);
                        if (i >= 0 && i < (int)g_pl.size()) {
                            g_pl[i].ch = model + ":" + made.enginePath;
                        }
                    }
                    FillChars();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(btnW, 0))) {
                g_importModel.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Render();
        glViewport(0, 0, dw, dh);
        glClearColor(0.09f, 0.08f, 0.08f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        if (!shotFile.empty() && ++frames == 20) {
            Image shot;
            shot.w = dw;
            shot.h = dh;
            shot.px.resize((size_t)dw * dh * 4);
            glReadPixels(0, 0, dw, dh, GL_RGBA, GL_UNSIGNED_BYTE, shot.px.data());
            for (int y = 0; y < dh / 2; y++) {
                std::swap_ranges(shot.px.begin() + (size_t)y * dw * 4, shot.px.begin() + (size_t)(y + 1) * dw * 4, shot.px.begin() + (size_t)(dh - 1 - y) * dw * 4);
            }
            for (size_t i = 3; i < shot.px.size(); i += 4) {
                shot.px[i] = 255;
            }
            SavePNG(shotFile, shot);
            running = false;
        }
        SDL_GL_SwapWindow(win);

        // the running game: ended (Play again), or it wants a password (none given / wrong)
        if (GameRunning()) {
            if (g_link.Poll() && g_link.st.role == 2 && g_link.st.needPassword && !pwAsk && SDL_GetTicks() - pwAskedAt > 1500) {
                pwAsk = 2;
                pwText = g_link.st.status.empty() ? "This server needs a password." : g_link.st.status + ".";
                pwBuf[0] = 0;
            }
            if (GameEnded()) {
                g_link.st = AdminStatus();
                RefreshServers();
            }
        }
        if (play && GameRunning()) {
            play = false; // (Play is off while the game runs)
        }
        if (play) {
            play = false;
            const bool asked = modShareOk; // (Join was clicked in the mod share question)
            modShareOk = false;
            const bool pwAsked = pwOk; // (OK was clicked in the password window)
            pwOk = false;
            bool go = CollectSettings();
            if (go && g_set.mpmode == "join") {
                // a server known to run another version can't be joined
                const ServerEntry* e = JoinTarget();
                if (e && e->info.version != LUGARUEX_VERSION) {
                    g_message = "\"" + netinfo::StripColours(e->info.name) + "\" runs LugaruEX " + e->info.version + ", you have " LUGARUEX_VERSION
                                ".\n\nBoth need the same version to play together.";
                    go = false;
                }
            }
            if (go && !asked && ModShareQuestion(modShareText)) {
                g_askModShare = true; // ask first; Join comes back here
                go = false;
            }
            if (go && !pwAsked) {
                // a server known to be locked: its password first (a small window)
                g_joinPassword.clear();
                const ServerEntry* t = g_set.mpmode == "join" ? JoinTarget() : nullptr;
                if (t && t->info.password) {
                    pwAsk = 1; // OK comes back here
                    pwText = "\"" + netinfo::StripColours(t->info.name) + "\" needs a password.";
                    pwBuf[0] = 0;
                    go = false;
                }
            }
            if (go) {
                if (g_set.mpmode == "join" && !g_set.steam && !g_set.addr.empty()) {
                    RememberServer(g_set.addr); // the server list asks it next time
                }
                if (!SaveIni()) {
                    g_message = "Could not write the settings file (" + IniPath() + ").";
                } else if (!LaunchEngine()) {
                } else if (g_set.mpmode != "off") {
                    // online: stay open (who is on your server, passwords); Play again when it ends
                    g_mpAdvanced = false; // (room for the player list)
                    wantTab = 4;
                } else {
                    running = false;
#ifndef _WIN32
                    // close the window now, and stay until the game ends: Steam treats the
                    // program it started as the game
                    ImGui_ImplOpenGL2_Shutdown();
                    ImGui_ImplSDL2_Shutdown();
                    ImGui::DestroyContext();
                    SDL_GL_DeleteContext(gl);
                    SDL_DestroyWindow(win);
                    SDL_Quit();
                    int status = 0;
                    wait(&status);
                    return 0;
#endif
                }
            }
        }
    }
    PreviewCore_FreeGL();
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
#ifndef _WIN32
    if (GameRunning()) { // closed while an online game runs: stay (no window) until it ends, for Steam
        int status = 0;
        waitpid(g_gamePid, &status, 0);
    }
#endif
    if (applyUpdate) {
        RelaunchSelf(argv); // (Linux: this process becomes the new launcher)
    }
    return 0;
}
