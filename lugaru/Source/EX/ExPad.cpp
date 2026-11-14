/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "EX/ExPad.hpp"

#include "EX/ExConfig.hpp"
#include "EX/ExLighting.hpp"
#include "EX/ExLocal.hpp"
#include "Game.hpp"
#include "Menu/Menu.hpp"
#include "Utils/Input.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern float screenwidth, screenheight;
extern float usermousesensitivity;
extern int mainmenu;

namespace EX
{
// ---------------------------------------------------------------- names
// Keyboard/mouse names are SDL scancode names ("W", "Space", "Left Shift")
// plus "Mouse Left/Middle/Right/4/5". Pad names are SDL's button names
// ("a", "b", "rightshoulder", ...) plus "lefttrigger"/"righttrigger".

static std::string keyName(int code)
{
    switch (code) {
        case MOUSEBUTTON_LEFT:
            return "Mouse Left";
        case MOUSEBUTTON_MIDDLE:
            return "Mouse Middle";
        case MOUSEBUTTON_RIGHT:
            return "Mouse Right";
        case MOUSEBUTTON_X1:
            return "Mouse 4";
        case MOUSEBUTTON_X2:
            return "Mouse 5";
    }
    if (code > 0 && code < SDL_NUM_SCANCODES) {
        return SDL_GetScancodeName((SDL_Scancode)code);
    }
    return "";
}

static int keyFromName(const std::string& n)
{
    if (n == "Mouse Left") {
        return MOUSEBUTTON_LEFT;
    }
    if (n == "Mouse Middle") {
        return MOUSEBUTTON_MIDDLE;
    }
    if (n == "Mouse Right") {
        return MOUSEBUTTON_RIGHT;
    }
    if (n == "Mouse 4") {
        return MOUSEBUTTON_X1;
    }
    if (n == "Mouse 5") {
        return MOUSEBUTTON_X2;
    }
    if (n.empty()) {
        return 0;
    }
    return (int)SDL_GetScancodeFromName(n.c_str());
}

static const int PAD_LT = 100, PAD_RT = 101;

static int padFromName(const std::string& n)
{
    if (n == "lefttrigger") {
        return PAD_LT;
    }
    if (n == "righttrigger") {
        return PAD_RT;
    }
    if (n.empty()) {
        return -1;
    }
    SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(n.c_str());
    return (b == SDL_CONTROLLER_BUTTON_INVALID) ? -1 : (int)b;
}

// ---------------------------------------------------------------- bindings
struct KeyAction
{
    const char* name;
    unsigned short* var; // the game's own key variable (nullptr = LugaruEX-only action)
};

static int g_lightingKey = SDL_SCANCODE_R;

static std::vector<KeyAction>& keyActions()
{
    static std::vector<KeyAction> a = {
        { "Forward", &Game::forwardkey }, { "Back", &Game::backkey }, { "Left", &Game::leftkey }, { "Right", &Game::rightkey },
        { "Jump", &Game::jumpkey }, { "Crouch", &Game::crouchkey }, { "Draw", &Game::drawkey }, { "Throw", &Game::throwkey },
        { "Attack", &Game::attackkey }, { "Console", &Game::consolekey },
    };
    return a;
}

void ApplyKeyBindings()
{
    for (auto& a : keyActions()) {
        const int code = keyFromName(IniString("Keys", a.name));
        if (code > 0) {
            *a.var = (unsigned short)code;
        }
    }
    const int lk = keyFromName(IniString("Keys", "Lighting", "R"));
    if (lk > 0) {
        g_lightingKey = lk;
    }
}

void SaveKeyBindings()
{
    if (!cfg.loaded) {
        return;
    }
    for (auto& a : keyActions()) {
        const std::string n = keyName(*a.var);
        if (!n.empty() && n != IniString("Keys", a.name)) {
            SetIniValue("Keys", a.name, n);
        }
    }
}

int LightingKey()
{
    return g_lightingKey;
}

struct PadBinds
{
    // defaults: A jump, LT crouch / block, RT attack, X throw / pick up, Y draw / put away
    int jump = SDL_CONTROLLER_BUTTON_A, crouch = PAD_LT, attack = PAD_RT;
    int draw = SDL_CONTROLLER_BUTTON_Y, thrw = SDL_CONTROLLER_BUTTON_X;
    int lighting = SDL_CONTROLLER_BUTTON_BACK, pause = SDL_CONTROLLER_BUTTON_START;
    float lookSpeed = 1.f;
    bool vibration = true; // [Pad] Vibration (launcher Controls tab)
};
static PadBinds g_binds;

static void loadPadBinds()
{
    auto get = [](const char* k, int& v) {
        const int c = padFromName(IniString("Pad", k, "?"));
        if (c != -1) {
            v = c;
        }
    };
    get("Jump", g_binds.jump);
    get("Crouch", g_binds.crouch);
    get("Attack", g_binds.attack);
    get("Draw", g_binds.draw);
    get("Throw", g_binds.thrw);
    get("Lighting", g_binds.lighting);
    get("Pause", g_binds.pause);
    const std::string ls = IniString("Pad", "LookSpeed", "1");
    g_binds.lookSpeed = std::max(0.2f, std::min(3.f, (float)atof(ls.c_str())));
    g_binds.vibration = atoi(IniString("Pad", "Vibration", "1").c_str()) != 0;
}

// ---------------------------------------------------------------- controllers
struct Pad
{
    SDL_GameController* gc;
    SDL_JoystickID jid;
    std::string id;
    Uint32 rumbleEnd = 0; // current rumble (a weaker one never cuts a stronger one short)
    float rumbleStrength = 0;
};
static std::vector<Pad> g_pads;

std::string PadIdentity(SDL_GameController* gc)
{
    // serial number when the driver exposes one, else the device path (for
    // XInput pads this is the XInput slot), else type + player index
    const char* serial = SDL_GameControllerGetSerial(gc);
    if (serial && *serial) {
        return std::string("serial:") + serial;
    }
    const char* path = SDL_GameControllerPath(gc);
    if (path && *path) {
        return std::string("path:") + path;
    }
    char b[64];
    snprintf(b, sizeof(b), "slot:%d", SDL_GameControllerGetPlayerIndex(gc));
    return b;
}

static void openPad(int deviceIndex)
{
    if (!SDL_IsGameController(deviceIndex)) {
        return;
    }
    SDL_GameController* gc = SDL_GameControllerOpen(deviceIndex);
    if (!gc) {
        return;
    }
    SDL_JoystickID jid = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
    for (auto& p : g_pads) {
        if (p.jid == jid) {
            return;
        }
    }
    g_pads.push_back({ gc, jid, PadIdentity(gc) });
    printf("LugaruEX: controller connected: %s\n", SDL_GameControllerName(gc)); // no serials in the log (people share logs)
}

// test hook: LUGARUEX_FAKE_PADS=N attaches N virtual controllers (no hardware
// needed). Pad 1 stays idle; the others walk in circles, look around and attack.
static int g_fakePads = 0;
static std::vector<SDL_JoystickID> g_fakeIds;

static void animateFakePads()
{
    if (!g_fakePads) {
        return;
    }
    const float t = SDL_GetTicks() / 1000.f;
    // LUGARUEX_TEST_UNPLUG=s: the last test pad is unplugged s seconds after start
    static const char* unplug = getenv("LUGARUEX_TEST_UNPLUG");
    if (unplug && !g_fakeIds.empty() && t > atof(unplug)) {
        for (int d = 0; d < SDL_NumJoysticks(); d++) {
            if (SDL_JoystickGetDeviceInstanceID(d) == g_fakeIds.back() && SDL_JoystickIsVirtual(d)) {
                SDL_JoystickDetachVirtual(d);
                printf("LugaruEX: test pad unplugged\n");
                break;
            }
        }
        g_fakeIds.pop_back();
        unplug = nullptr;
    }
    for (size_t n = 0; n < g_fakeIds.size(); n++) {
        SDL_Joystick* j = SDL_JoystickFromInstanceID(g_fakeIds[n]);
        if (!j || n == 0) {
            continue;
        }
        const float ph = n * 1.7f;
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTY, -32767); // run forward
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_RIGHTX, (Sint16)(sinf(t * 0.7f + ph) * 26000));
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, ((int)(t * 2) + (int)n) % 5 == 0 ? 32767 : 0); // attack now and then
    }
}

void PadInit()
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "LugaruEX: controller support unavailable: %s\n", SDL_GetError());
        return;
    }
    loadPadBinds();
    if (const char* fake = getenv("LUGARUEX_FAKE_PADS")) {
        g_fakePads = std::max(0, std::min(4, atoi(fake)));
        for (int i = 0; i < g_fakePads; i++) {
            SDL_VirtualJoystickDesc d;
            SDL_zero(d);
            d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
            d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
            d.naxes = SDL_CONTROLLER_AXIS_MAX;
            d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
            char name[32];
            snprintf(name, sizeof(name), "Test Pad %d", i + 1);
            d.name = name;
            const int dev = SDL_JoystickAttachVirtualEx(&d);
            if (dev >= 0) {
                g_fakeIds.push_back(SDL_JoystickGetDeviceInstanceID(dev));
            }
        }
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        openPad(i);
    }
}

int PadCount()
{
    return (int)g_pads.size();
}

std::string PadIdAt(int i)
{
    return (i >= 0 && i < (int)g_pads.size()) ? g_pads[i].id : std::string();
}

static SDL_GameController* padById(const std::string& id)
{
    if (id.empty()) {
        return nullptr;
    }
    for (auto& p : g_pads) {
        if (p.id == id) {
            return p.gc;
        }
    }
    return nullptr;
}

bool PadConnected(const std::string& id)
{
    return padById(id) != nullptr;
}

float PadLookSpeed()
{
    return g_binds.lookSpeed;
}

void PadHandleEvent(const SDL_Event& e)
{
    if (e.type == SDL_CONTROLLERDEVICEADDED) {
        openPad(e.cdevice.which);
    } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
        for (size_t i = 0; i < g_pads.size(); i++) {
            if (g_pads[i].jid == e.cdevice.which) {
                printf("LugaruEX: controller disconnected\n");
                SDL_GameControllerClose(g_pads[i].gc);
                g_pads.erase(g_pads.begin() + i);
                break;
            }
        }
    }
}

// Player 1's controller (local player slot 1, see ExLocal)
static SDL_GameController* activePad()
{
    return padById(LocalPadOfP1());
}

static bool held(SDL_GameController* gc, int code)
{
    if (code == PAD_LT) {
        return SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000;
    }
    if (code == PAD_RT) {
        return SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000;
    }
    if (code < 0 || code >= SDL_CONTROLLER_BUTTON_MAX) {
        return false;
    }
    return SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)code) != 0;
}

static float axis(SDL_GameController* gc, SDL_GameControllerAxis a)
{
    return SDL_GameControllerGetAxis(gc, a) / 32767.f;
}

// left stick -> the game's 8 movement directions. Past a small dead zone the stick moves;
// a light push walks, further out runs. The direction is the stick's 45-degree sector
// (a full push gives about the same directions as the old per-axis 0.42 test).
static void stickMove(SDL_GameController* gc, bool& fwd, bool& back, bool& left, bool& right, bool& walk)
{
    fwd = back = left = right = walk = false;
    const float lx = axis(gc, SDL_CONTROLLER_AXIS_LEFTX), ly = axis(gc, SDL_CONTROLLER_AXIS_LEFTY);
    const float m = sqrtf(lx * lx + ly * ly);
    const float kDead = 0.3f, kWalk = 0.65f; // (a dead zone big enough for worn, drifting sticks)
    if (m >= kDead) {
        const float s = 0.3827f * m; // sin(22.5 degrees): a component this big counts
        fwd = ly < -s;
        back = ly > s;
        left = lx < -s;
        right = lx > s;
        walk = m < kWalk;
    }
    // the D-pad always runs
    const bool du = held(gc, SDL_CONTROLLER_BUTTON_DPAD_UP), dd = held(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    const bool dl = held(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT), dr = held(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    if (du || dd || dl || dr) {
        walk = false;
    }
    fwd = fwd || du;
    back = back || dd;
    left = left || dl;
    right = right || dr;
}

static bool g_p1PadWalk = false; // player 1's controller asks for a walk this frame

bool P1Walk()
{
    // a Ctrl key bound to a game action is that action, not walk (someone crouching on Ctrl
    // would otherwise walk instead of run, and lose the running moves)
    auto ctrlWalks = [](int sc) {
        using namespace Game;
        const unsigned short bound[] = { forwardkey, backkey, leftkey, rightkey, jumpkey, crouchkey, attackkey, drawkey, throwkey };
        for (unsigned short b : bound) {
            if (b == sc) {
                return false;
            }
        }
        return Input::isKeyDown(sc);
    };
    return !mainmenu && (g_p1PadWalk || ctrlWalks(SDL_SCANCODE_LCTRL) || ctrlWalks(SDL_SCANCODE_RCTRL));
}

bool PadRead(const std::string& id, PadInput& o)
{
    o = PadInput();
    SDL_GameController* gc = padById(id);
    if (!gc) {
        return false;
    }
    stickMove(gc, o.fwd, o.back, o.left, o.right, o.walk);
    o.jump = held(gc, g_binds.jump);
    o.crouch = held(gc, g_binds.crouch);
    o.attack = held(gc, g_binds.attack);
    o.draw = held(gc, g_binds.draw);
    o.thrw = held(gc, g_binds.thrw);
    o.pause = held(gc, g_binds.pause);
    o.lighting = held(gc, g_binds.lighting);
    auto shape = [](float v) {
        const float dz = 0.15f;
        const float a = fabsf(v);
        if (a < dz) {
            return 0.f;
        }
        const float n = (a - dz) / (1 - dz);
        return (v < 0 ? -1.f : 1.f) * n * n;
    };
    o.lookX = shape(axis(gc, SDL_CONTROLLER_AXIS_RIGHTX));
    o.lookY = shape(axis(gc, SDL_CONTROLLER_AXIS_RIGHTY));
    return true;
}

void PadContribute(bool* down, int numKeys)
{
    g_p1PadWalk = false;
    auto set = [&](int k) {
        if (k > 0 && k < numKeys) {
            down[k] = true;
        }
    };
    // any controller can drive the menus and open the pause menu
    for (auto& p : g_pads) {
        if (mainmenu) {
            if (held(p.gc, SDL_CONTROLLER_BUTTON_A)) {
                set(MOUSEBUTTON_LEFT); // click the button under the menu cursor
            }
            if (held(p.gc, SDL_CONTROLLER_BUTTON_B) || held(p.gc, g_binds.pause)) {
                set(SDL_SCANCODE_ESCAPE); // back / resume
            }
        } else if (held(p.gc, g_binds.pause)) {
            set(SDL_SCANCODE_ESCAPE); // Start opens the pause menu
        }
    }
    SDL_GameController* gc = activePad();
    if (!gc || mainmenu) {
        return;
    }
    if (held(gc, g_binds.jump)) {
        set(Game::jumpkey);
    }
    if (held(gc, g_binds.crouch)) {
        set(Game::crouchkey);
    }
    if (held(gc, g_binds.attack)) {
        set(Game::attackkey);
    }
    if (held(gc, g_binds.draw)) {
        set(Game::drawkey);
    }
    if (held(gc, g_binds.thrw)) {
        set(Game::throwkey);
    }
    // left stick moves (the game's movement is digital; 8 directions; a light push walks)
    bool fwd, back, left, right;
    stickMove(gc, fwd, back, left, right, g_p1PadWalk);
    if (fwd) {
        set(Game::forwardkey);
    }
    if (back) {
        set(Game::backkey);
    }
    if (left) {
        set(Game::leftkey);
    }
    if (right) {
        set(Game::rightkey);
    }
}

// ---------------------------------------------------------------- menus
static void moveMenuCursor(int dx, int dy)
{
    const float extra = MenuExtraX();
    const float spanX = 640 + 2 * extra;
    // cursor in menu space
    const float mx = -extra + Game::mousecoordh * spanX / screenwidth;
    const float my = 480 - Game::mousecoordv * 480 / screenheight;

    struct Cand
    {
        float x, y;
    };
    std::vector<Cand> c;
    int current = -1;
    for (auto& it : Menu::allItems()) {
        if (it.type != MenuItem::BUTTON && it.type != MenuItem::IMAGEBUTTON && it.type != MenuItem::MAPMARKER) {
            continue;
        }
        if (it.w <= 0 || it.h <= 0) {
            continue;
        }
        if (mx >= it.x && mx < it.x + it.w && my >= it.y && my < it.y + it.h) {
            current = (int)c.size();
        }
        c.push_back({ it.x + it.w / 2.f, it.y + it.h / 2.f });
    }
    if (c.empty()) {
        return;
    }
    int best = -1;
    if (current < 0) {
        // nothing selected yet: start at the top-most, left-most button
        float bestScore = 1e9f;
        for (size_t i = 0; i < c.size(); i++) {
            const float s = -c[i].y * 4 + c[i].x;
            if (s < bestScore) {
                bestScore = s;
                best = (int)i;
            }
        }
    } else {
        // nearest button in the pressed direction (menu y grows upwards)
        const float dirx = (float)dx, diry = (float)-dy;
        float bestScore = 1e9f;
        for (size_t i = 0; i < c.size(); i++) {
            if ((int)i == current) {
                continue;
            }
            const float vx = c[i].x - c[current].x, vy = c[i].y - c[current].y;
            const float along = vx * dirx + vy * diry;
            if (along <= 2) {
                continue;
            }
            const float across = fabsf(vx * diry - vy * dirx);
            const float score = along + across * 2.5f;
            if (score < bestScore) {
                bestScore = score;
                best = (int)i;
            }
        }
        if (best < 0) {
            return;
        }
    }
    Game::mousecoordh = (int)((c[best].x + extra) * screenwidth / spanX);
    Game::mousecoordv = (int)((480 - c[best].y) * screenheight / 480);
}

void PadFrame(double dt, float& pendingMouseH, float& pendingMouseV)
{
    animateFakePads();
    if (g_pads.empty()) {
        return;
    }
    // the lighting look is shared: any controller's Back cycles it
    static bool lightingWasDown = false;
    bool lightingDown = false;
    for (auto& p : g_pads) {
        lightingDown = lightingDown || held(p.gc, g_binds.lighting);
    }
    if (lightingDown && !lightingWasDown && !mainmenu) {
        LightingCycle();
    }
    lightingWasDown = lightingDown;

    if (!mainmenu) {
        SDL_GameController* gc = activePad();
        if (!gc) {
            return;
        }
        // right stick looks around (same path as mouse movement)
        auto shape = [](float v) {
            const float dz = 0.15f;
            const float a = fabsf(v);
            if (a < dz) {
                return 0.f;
            }
            const float n = (a - dz) / (1 - dz);
            return (v < 0 ? -1.f : 1.f) * n * n;
        };
        const float rx = shape(axis(gc, SDL_CONTROLLER_AXIS_RIGHTX));
        const float ry = shape(axis(gc, SDL_CONTROLLER_AXIS_RIGHTY));
        const float rate = 514.f * g_binds.lookSpeed / std::max(0.05f, usermousesensitivity); // ~360 deg/s at full tilt
        pendingMouseH += rx * rate * (float)dt;
        pendingMouseV += ry * rate * 0.7f * (float)dt;
        return;
    }

    // menus: stick / D-pad of any controller move between buttons, with key-repeat
    int dx = 0, dy = 0;
    for (auto& p : g_pads) {
        SDL_GameController* gc = p.gc;
        const float lx = axis(gc, SDL_CONTROLLER_AXIS_LEFTX), ly = axis(gc, SDL_CONTROLLER_AXIS_LEFTY);
        if (held(gc, SDL_CONTROLLER_BUTTON_DPAD_UP) || ly < -0.6f) {
            dy = -1;
        } else if (held(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly > 0.6f) {
            dy = 1;
        } else if (held(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || lx < -0.6f) {
            dx = -1;
        } else if (held(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx > 0.6f) {
            dx = 1;
        }
        if (dx || dy) {
            break;
        }
    }
    static int lastDx = 0, lastDy = 0;
    static double repeatIn = 0;
    if (dx || dy) {
        if (dx != lastDx || dy != lastDy) {
            moveMenuCursor(dx, dy);
            repeatIn = 0.38;
        } else {
            repeatIn -= dt;
            if (repeatIn <= 0) {
                moveMenuCursor(dx, dy);
                repeatIn = 0.13;
            }
        }
    }
    lastDx = dx;
    lastDy = dy;
}
}

namespace EX
{
// LugaruEX: vibration - strength 0..1 (low-frequency motor full, high-frequency a bit less)
void PadRumble(const std::string& id, float strength, int ms)
{
    if (!g_binds.vibration || id.empty() || strength <= 0 || ms <= 0) {
        return;
    }
    strength = std::min(1.f, strength);
    for (auto& p : g_pads) {
        if (p.id != id) {
            continue;
        }
        const Uint32 now = SDL_GetTicks();
        if (now < p.rumbleEnd && strength < p.rumbleStrength) {
            return;
        }
        p.rumbleEnd = now + (Uint32)ms;
        p.rumbleStrength = strength;
        const Uint16 lo = (Uint16)(strength * 65535.f);
        const Uint16 hi = (Uint16)(std::min(1.f, strength * 0.8f) * 65535.f);
        SDL_GameControllerRumble(p.gc, lo, hi, (Uint32)ms);
        return;
    }
}
}
