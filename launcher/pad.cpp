// LugaruEX launcher - game controller polling (SDL2, linked in) (GPLv2+)
#include "pad.h"

#include <SDL.h>

#include <cmath>
#include <cstdio>

static std::vector<PadState> g_pads;
static bool g_ok = false;

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

static std::wstring widen(const char* s)
{
    if (!s) {
        return L"Controller";
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    std::wstring w(n > 0 ? n - 1 : 0, 0);
    if (n > 1) {
        MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    }
    return w;
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
    const int jid = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
    const std::string id = identity(gc);
    for (auto& p : g_pads) {
        if (p.id == id) { // same controller plugged back in
            p.gc = gc;
            p.jid = jid;
            p.connected = true;
            p.name = widen(SDL_GameControllerName(gc));
            return;
        }
    }
    PadState s;
    s.id = id;
    s.name = widen(SDL_GameControllerName(gc));
    s.connected = true;
    s.gc = gc;
    s.jid = jid;
    g_pads.push_back(s);
}

bool Pad_Init()
{
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        return false;
    }
    g_ok = true;
    // debug: LUGARUEX_FAKE_PADS=N attaches N virtual controllers that wiggle (for testing without hardware)
    char fake[8] = {};
    if (GetEnvironmentVariableA("LUGARUEX_FAKE_PADS", fake, sizeof(fake)) > 0) {
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
    return true;
}

static void animateFakePads()
{
    static bool fake = GetEnvironmentVariableA("LUGARUEX_FAKE_PADS", NULL, 0) > 0;
    if (!fake) {
        return;
    }
    const float t = GetTickCount() / 1000.f;
    int n = 0;
    for (auto& p : g_pads) {
        if (!p.connected) {
            continue;
        }
        SDL_Joystick* j = SDL_GameControllerGetJoystick((SDL_GameController*)p.gc);
        if (p.name.rfind(L"Test Pad", 0) != 0) { // only the virtual test pads
            n++;
            continue;
        }
        const float ph = n * 1.7f;
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTX, (Sint16)(sinf(t * 1.3f + ph) * 30000));
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTY, (Sint16)(cosf(t * 1.3f + ph) * 30000));
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_RIGHTX, (Sint16)(sinf(t * 2.1f + ph) * 20000));
        SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, (Sint16)((sinf(t * 1.1f + ph) * 0.5f + 0.5f) * 32767));
        const int b = ((int)(t * 2) + n) % 4; // cycle A, B, X, Y
        for (int k = 0; k < 4; k++) {
            SDL_JoystickSetVirtualButton(j, k, k == b);
        }
        SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, ((int)(t * 3) + n) % 3 == 0);
        n++;
    }
}

void Pad_Update()
{
    if (!g_ok) {
        return;
    }
    animateFakePads();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_CONTROLLERDEVICEADDED) {
            open(e.cdevice.which);
        } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
            for (auto& p : g_pads) {
                if (p.connected && p.jid == e.cdevice.which) {
                    SDL_GameControllerClose((SDL_GameController*)p.gc);
                    p.gc = nullptr;
                    p.connected = false;
                    p.lx = p.ly = p.rx = p.ry = p.lt = p.rt = 0;
                    memset(p.btn, 0, sizeof(p.btn));
                }
            }
        }
    }
    SDL_GameControllerUpdate();
    const DWORD now = GetTickCount();
    for (auto& p : g_pads) {
        memcpy(p.prevBtn, p.btn, sizeof(p.btn));
        p.prevLT = p.lt;
        p.prevRT = p.rt;
        if (!p.connected) {
            continue;
        }
        SDL_GameController* gc = (SDL_GameController*)p.gc;
        auto ax = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(gc, a) / 32767.f; };
        p.lx = ax(SDL_CONTROLLER_AXIS_LEFTX);
        p.ly = ax(SDL_CONTROLLER_AXIS_LEFTY);
        p.rx = ax(SDL_CONTROLLER_AXIS_RIGHTX);
        p.ry = ax(SDL_CONTROLLER_AXIS_RIGHTY);
        p.lt = ax(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        p.rt = ax(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        bool any = false;
        for (int b = 0; b < PAD_BUTTONS && b < SDL_CONTROLLER_BUTTON_MAX; b++) {
            p.btn[b] = SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)b) != 0;
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
    SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(n.c_str());
    return b == SDL_CONTROLLER_BUTTON_INVALID ? -1 : (int)b;
}

std::wstring Pad_CodeLabel(int code)
{
    switch (code) {
        case SDL_CONTROLLER_BUTTON_A:
            return L"A";
        case SDL_CONTROLLER_BUTTON_B:
            return L"B";
        case SDL_CONTROLLER_BUTTON_X:
            return L"X";
        case SDL_CONTROLLER_BUTTON_Y:
            return L"Y";
        case SDL_CONTROLLER_BUTTON_BACK:
            return L"Back";
        case SDL_CONTROLLER_BUTTON_GUIDE:
            return L"Guide";
        case SDL_CONTROLLER_BUTTON_START:
            return L"Start";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
            return L"Left stick click";
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            return L"Right stick click";
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            return L"LB";
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            return L"RB";
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            return L"D-pad up";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            return L"D-pad down";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            return L"D-pad left";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            return L"D-pad right";
        case PAD_LT:
            return L"LT";
        case PAD_RT:
            return L"RT";
    }
    return code < 0 ? L"-" : L"Button " + std::to_wstring(code);
}

std::string Key_NameFromMessage(WPARAM vk, LPARAM lParam)
{
    const bool ext = (lParam >> 24) & 1;
    const int scan = (lParam >> 16) & 0xFF;
    int sc = 0; // SDL scancode (USB HID usage)
    if (vk >= 'A' && vk <= 'Z') {
        sc = 4 + (int)(vk - 'A');
    } else if (vk >= '1' && vk <= '9') {
        sc = 30 + (int)(vk - '1');
    } else if (vk == '0') {
        sc = 39;
    } else if (vk >= VK_F1 && vk <= VK_F12) {
        sc = 58 + (int)(vk - VK_F1);
    } else if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9) {
        sc = 89 + (int)(vk - VK_NUMPAD1);
    } else {
        switch (vk) {
            case VK_RETURN:
                sc = ext ? 88 : 40;
                break;
            case VK_BACK:
                sc = 42;
                break;
            case VK_TAB:
                sc = 43;
                break;
            case VK_SPACE:
                sc = 44;
                break;
            case VK_OEM_MINUS:
                sc = 45;
                break;
            case VK_OEM_PLUS:
                sc = 46;
                break;
            case VK_OEM_4:
                sc = 47;
                break;
            case VK_OEM_6:
                sc = 48;
                break;
            case VK_OEM_5:
                sc = 49;
                break;
            case VK_OEM_1:
                sc = 51;
                break;
            case VK_OEM_7:
                sc = 52;
                break;
            case VK_OEM_3:
                sc = 53;
                break;
            case VK_OEM_COMMA:
                sc = 54;
                break;
            case VK_OEM_PERIOD:
                sc = 55;
                break;
            case VK_OEM_2:
                sc = 56;
                break;
            case VK_CAPITAL:
                sc = 57;
                break;
            case VK_INSERT:
                sc = 73;
                break;
            case VK_HOME:
                sc = 74;
                break;
            case VK_PRIOR:
                sc = 75;
                break;
            case VK_DELETE:
                sc = 76;
                break;
            case VK_END:
                sc = 77;
                break;
            case VK_NEXT:
                sc = 78;
                break;
            case VK_RIGHT:
                sc = 79;
                break;
            case VK_LEFT:
                sc = 80;
                break;
            case VK_DOWN:
                sc = 81;
                break;
            case VK_UP:
                sc = 82;
                break;
            case VK_DIVIDE:
                sc = 84;
                break;
            case VK_MULTIPLY:
                sc = 85;
                break;
            case VK_SUBTRACT:
                sc = 86;
                break;
            case VK_ADD:
                sc = 87;
                break;
            case VK_NUMPAD0:
                sc = 98;
                break;
            case VK_DECIMAL:
                sc = 99;
                break;
            case VK_SHIFT:
                sc = (scan == 0x36) ? 229 : 225;
                break;
            case VK_CONTROL:
                sc = ext ? 228 : 224;
                break;
            case VK_MENU:
                sc = ext ? 230 : 226;
                break;
        }
    }
    if (!sc) {
        return "";
    }
    const char* n = SDL_GetScancodeName((SDL_Scancode)sc);
    return n ? n : "";
}
