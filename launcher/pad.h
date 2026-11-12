// LugaruEX launcher - game controller polling (SDL GameController, same as the game)
#pragma once
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

// pad "codes": SDL_GameControllerButton values, plus these two for the triggers
enum
{
    PAD_LT = 100,
    PAD_RT = 101
};
static const int PAD_BUTTONS = 21; // SDL_CONTROLLER_BUTTON_MAX in SDL 2.32

struct PadState
{
    std::string id;     // stable identity (same rule as the game)
    std::wstring name;  // e.g. "Xbox 360 Controller"
    bool connected = false;
    void* gc = nullptr; // SDL_GameController*
    int jid = -1;       // SDL_JoystickID
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
    bool btn[PAD_BUTTONS] = {};
    bool prevBtn[PAD_BUTTONS] = {};
    float prevLT = 0, prevRT = 0;
    DWORD lastActive = 0; // GetTickCount() of the last press, for "who's who" highlights
};

bool Pad_Init();   // false if controller support could not start
void Pad_Update(); // poll; call ~60 times a second
std::vector<PadState>& Pad_List(); // every controller seen this session (connected or not)
PadState* Pad_Find(const std::string& id);
int Pad_NewPress(const PadState& p); // code pressed this update, or -1

std::string Pad_CodeName(int code);   // ini name: "a", "rightshoulder", "righttrigger"...
int Pad_CodeFromName(const std::string& n);
std::wstring Pad_CodeLabel(int code); // shown to the user: "A", "RB", "RT", "Start"...

// keyboard: Windows key message -> SDL scancode name ("W", "Left Shift"...)
std::string Key_NameFromMessage(WPARAM vk, LPARAM lParam);
