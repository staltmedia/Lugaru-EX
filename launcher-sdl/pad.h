// LugaruEX launcher for Linux - game controllers (SDL GameController, same identity rule as the game) (GPLv2+)
#pragma once
#include <SDL.h>

#include <string>
#include <vector>

// pad "codes": SDL_GameControllerButton values, plus these two for the triggers
enum
{
    PAD_LT = 100,
    PAD_RT = 101
};
static const int PAD_BUTTONS = SDL_CONTROLLER_BUTTON_MAX;

struct PadState
{
    std::string id;   // stable identity (same rule as the game)
    std::string name; // e.g. "Steam Deck"
    bool connected = false;
    SDL_GameController* gc = nullptr;
    SDL_JoystickID jid = -1;
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
    bool btn[PAD_BUTTONS] = {};
    bool prevBtn[PAD_BUTTONS] = {};
    float prevLT = 0, prevRT = 0;
    Uint32 lastActive = 0; // SDL_GetTicks() of the last press, for "who's who" highlights
};

void Pad_Init();                     // after SDL_Init(SDL_INIT_GAMECONTROLLER)
void Pad_Event(const SDL_Event& e);  // device added / removed
void Pad_Update();                   // poll the states; once a frame
std::vector<PadState>& Pad_List();   // every controller seen this session (connected or not)
PadState* Pad_Find(const std::string& id);
int Pad_NewPress(const PadState& p); // code pressed this update, or -1

std::string Pad_CodeName(int code);  // ini name: "a", "rightshoulder", "righttrigger"...
int Pad_CodeFromName(const std::string& n);
std::string Pad_CodeLabel(int code); // shown to the user: "A", "RB", "RT", "Start"...
