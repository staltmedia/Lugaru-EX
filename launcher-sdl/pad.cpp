// LugaruEX launcher for Linux - game controllers (GPLv2+)
#include "pad.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static std::vector<PadState> g_pads;

static std::string identity(SDL_GameController* gc)
{
    // identical to EX::PadIdentity in the game
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

static void open(int deviceIndex)
{
    if (!SDL_IsGameController(deviceIndex)) {
        return;
    }
    SDL_GameController* gc = SDL_GameControllerOpen(deviceIndex);
    if (!gc) {
        return;
    }
    const SDL_JoystickID jid = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
    for (auto& p : g_pads) {
        if (p.connected && p.jid == jid) {
            SDL_GameControllerClose(gc); // already open (added event after the initial scan)
            return;
        }
    }
    const std::string id = identity(gc);
    const char* nm = SDL_GameControllerName(gc);
    for (auto& p : g_pads) {
        if (p.id == id) { // same controller plugged back in
            p.gc = gc;
            p.jid = jid;
            p.connected = true;
            p.name = nm ? nm : "Controller";
            return;
        }
    }
    PadState s;
    s.id = id;
    s.name = nm ? nm : "Controller";
    s.connected = true;
    s.gc = gc;
    s.jid = jid;
    g_pads.push_back(s);
}

void Pad_Init()
{
    // debug: LUGARUEX_FAKE_PADS=N attaches N virtual controllers (for testing without hardware)
    if (const char* fake = getenv("LUGARUEX_FAKE_PADS")) {
        for (int i = 0; i < atoi(fake) && i < 4; i++) {
            SDL_VirtualJoystickDesc d;
            SDL_zero(d);
            d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
            d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
            d.naxes = SDL_CONTROLLER_AXIS_MAX;
            d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
            char name[32];
            snprintf(name, sizeof(name), "Test Pad %d", i + 1);
            d.name = name;
            SDL_JoystickAttachVirtualEx(&d);
        }
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        open(i);
    }
}

void Pad_Event(const SDL_Event& e)
{
    if (e.type == SDL_CONTROLLERDEVICEADDED) {
        open(e.cdevice.which);
    } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
        for (auto& p : g_pads) {
            if (p.connected && p.jid == e.cdevice.which) {
                SDL_GameControllerClose(p.gc);
                p.gc = nullptr;
                p.connected = false;
                p.lx = p.ly = p.rx = p.ry = p.lt = p.rt = 0;
                memset(p.btn, 0, sizeof(p.btn));
            }
        }
    }
}

static void animateFakePads()
{
    static const bool fake = getenv("LUGARUEX_FAKE_PADS") != nullptr;
    if (!fake) {
        return;
    }
    const float t = SDL_GetTicks() / 1000.f;
    int n = 0;
    for (auto& p : g_pads) {
        if (!p.connected || p.name.rfind("Test Pad", 0) != 0) {
            n += p.connected;
            continue;
        }
        SDL_Joystick* j = SDL_GameControllerGetJoystick(p.gc);
        const float ph = n * 1.7f;
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTX, (Sint16)(sinf(t * 1.3f + ph) * 30000));
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTY, (Sint16)(cosf(t * 1.3f + ph) * 30000));
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_RIGHTX, (Sint16)(sinf(t * 2.1f + ph) * 20000));
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, (Sint16)((sinf(t * 1.1f + ph) * 0.5f + 0.5f) * 32767));
        n++;
    }
}

void Pad_Update()
{
    animateFakePads();
    SDL_GameControllerUpdate();
    const Uint32 now = SDL_GetTicks();
    for (auto& p : g_pads) {
        memcpy(p.prevBtn, p.btn, sizeof(p.btn));
        p.prevLT = p.lt;
        p.prevRT = p.rt;
        if (!p.connected) {
            continue;
        }
        auto ax = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(p.gc, a) / 32767.f; };
        p.lx = ax(SDL_CONTROLLER_AXIS_LEFTX);
        p.ly = ax(SDL_CONTROLLER_AXIS_LEFTY);
        p.rx = ax(SDL_CONTROLLER_AXIS_RIGHTX);
        p.ry = ax(SDL_CONTROLLER_AXIS_RIGHTY);
        p.lt = ax(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        p.rt = ax(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        bool any = false;
        for (int b = 0; b < PAD_BUTTONS; b++) {
            p.btn[b] = SDL_GameControllerGetButton(p.gc, (SDL_GameControllerButton)b) != 0;
            any |= p.btn[b] && !p.prevBtn[b];
        }
        any |= (p.lt > 0.5f && p.prevLT <= 0.5f) || (p.rt > 0.5f && p.prevRT <= 0.5f);
        any |= fabsf(p.lx) > 0.6f || fabsf(p.ly) > 0.6f || fabsf(p.rx) > 0.6f || fabsf(p.ry) > 0.6f;
        if (any) {
            p.lastActive = now;
        }
    }
}

std::vector<PadState>& Pad_List()
{
    return g_pads;
}

PadState* Pad_Find(const std::string& id)
{
    for (auto& p : g_pads) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

int Pad_NewPress(const PadState& p)
{
    if (!p.connected) {
        return -1;
    }
    for (int b = 0; b < PAD_BUTTONS; b++) {
        if (p.btn[b] && !p.prevBtn[b]) {
            return b;
        }
    }
    if (p.lt > 0.5f && p.prevLT <= 0.5f) {
        return PAD_LT;
    }
    if (p.rt > 0.5f && p.prevRT <= 0.5f) {
        return PAD_RT;
    }
    return -1;
}

std::string Pad_CodeName(int code)
{
    if (code == PAD_LT) {
        return "lefttrigger";
    }
    if (code == PAD_RT) {
        return "righttrigger";
    }
    const char* s = (code >= 0) ? SDL_GameControllerGetStringForButton((SDL_GameControllerButton)code) : nullptr;
    return s ? s : "";
}

int Pad_CodeFromName(const std::string& n)
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
    const SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(n.c_str());
    return b == SDL_CONTROLLER_BUTTON_INVALID ? -1 : (int)b;
}

std::string Pad_CodeLabel(int code)
{
    switch (code) {
        case SDL_CONTROLLER_BUTTON_A:
            return "A";
        case SDL_CONTROLLER_BUTTON_B:
            return "B";
        case SDL_CONTROLLER_BUTTON_X:
            return "X";
        case SDL_CONTROLLER_BUTTON_Y:
            return "Y";
        case SDL_CONTROLLER_BUTTON_BACK:
            return "Back";
        case SDL_CONTROLLER_BUTTON_GUIDE:
            return "Guide";
        case SDL_CONTROLLER_BUTTON_START:
            return "Start";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
            return "Left stick click";
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            return "Right stick click";
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            return "LB";
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            return "RB";
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            return "D-pad up";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            return "D-pad down";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            return "D-pad left";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            return "D-pad right";
        case SDL_CONTROLLER_BUTTON_MISC1:
            return "Share";
        case SDL_CONTROLLER_BUTTON_PADDLE1:
            return "Paddle 1";
        case SDL_CONTROLLER_BUTTON_PADDLE2:
            return "Paddle 2";
        case SDL_CONTROLLER_BUTTON_PADDLE3:
            return "Paddle 3";
        case SDL_CONTROLLER_BUTTON_PADDLE4:
            return "Paddle 4";
        case SDL_CONTROLLER_BUTTON_TOUCHPAD:
            return "Touchpad";
        case PAD_LT:
            return "LT";
        case PAD_RT:
            return "RT";
        default:
            return code < 0 ? "(none)" : "?";
    }
}
