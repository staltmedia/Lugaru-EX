/*
Copyright (C) 2003, 2010 - Wolfire Games
Copyright (C) 2010-2017 - Lugaru contributors (see AUTHORS file)

This file is part of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

Lugaru is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Lugaru.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Utils/Input.hpp"
#include "EX/ExNet.hpp"
#include "EX/ExPad.hpp"
#include "EX/ExLocal.hpp"
#include "Game.hpp"
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern int mainmenu;

bool keyDown[SDL_NUM_SCANCODES + 6];
bool keyPressed[SDL_NUM_SCANCODES + 6];

// LugaruEX test hook LUGARUEX_TEST_PRESS="key@from-to,key@from-to,...": player 1's keys held
// from..to seconds into the level (key: forward back left right jump crouch attack draw throw),
// through the normal input path - for testing attacks and moves for real
static void exTestPress(bool* now)
{
    using namespace Game;
    struct Press
    {
        unsigned short* key;
        float from, to;
    };
    static std::vector<Press> list;
    static bool parsed = false;
    if (!parsed) {
        parsed = true;
        const char* env = getenv("LUGARUEX_TEST_PRESS");
        std::string s = env ? env : "";
        size_t at = 0;
        while (at < s.size()) {
            size_t end = s.find(',', at);
            if (end == std::string::npos) {
                end = s.size();
            }
            const std::string item = s.substr(at, end - at);
            at = end + 1;
            const size_t a = item.find('@'), d = item.find('-', a == std::string::npos ? 0 : a);
            if (a == std::string::npos || d == std::string::npos) {
                continue;
            }
            const std::string name = item.substr(0, a);
            static const struct
            {
                const char* name;
                unsigned short* key;
            } keys[] = { { "forward", &forwardkey }, { "back", &backkey }, { "left", &leftkey }, { "right", &rightkey }, { "jump", &jumpkey },
                         { "crouch", &crouchkey }, { "attack", &attackkey }, { "draw", &drawkey }, { "throw", &throwkey } };
            for (const auto& k : keys) {
                if (name == k.name) {
                    list.push_back({ k.key, (float)atof(item.substr(a + 1, d - a - 1).c_str()), (float)atof(item.substr(d + 1).c_str()) });
                }
            }
        }
    }
    if (list.empty() || !Game::gameon || Game::loading) {
        return;
    }
    for (const Press& p : list) {
        if (Game::leveltime >= p.from && Game::leveltime < p.to && *p.key < SDL_NUM_SCANCODES + 6) {
            now[*p.key] = true;
        }
    }
}

void Input::Tick()
{
    SDL_PumpEvents();
    // LugaruEX: gather keyboard, mouse and controller into one "held" state,
    // then derive the pressed-this-tick edges from it
    static bool now[SDL_NUM_SCANCODES + 6];
    memset(now, 0, sizeof(now));
    int numkeys;
    const Uint8* keyState = SDL_GetKeyboardState(&numkeys);
    for (int i = 0; i < numkeys && i < SDL_NUM_SCANCODES; i++) {
        now[i] = keyState[i] != 0;
    }
    Uint8 mb = SDL_GetMouseState(NULL, NULL);
    for (int i = 1; i < 6; i++) {
        now[SDL_NUM_SCANCODES + i] = (mb & SDL_BUTTON(i)) != 0;
    }
    if (EX::ChatBlocksKeys()) {
        memset(now, 0, SDL_NUM_SCANCODES); // typing a chat line: the keyboard is the chat's (mouse and pads still play)
    }
    if (Game::console && !mainmenu && (EX::NetIsHost() || EX::NetIsGuest() || EX::LocalSplit())) {
        // the console open in a game that runs on behind it (online, split screen): the
        // keyboard types into the console, only its own key (to close it) still counts
        const bool closeKey = Game::consolekey < SDL_NUM_SCANCODES && now[Game::consolekey];
        memset(now, 0, SDL_NUM_SCANCODES);
        if (Game::consolekey < SDL_NUM_SCANCODES) {
            now[Game::consolekey] = closeKey;
        }
    }
    EX::PadContribute(now, SDL_NUM_SCANCODES + 6);
    static const bool noInput = getenv("LUGARUEX_TEST_NOINPUT") != nullptr; // test hook: ignore real keys/buttons
    if (noInput) {
        memset(now, 0, sizeof(now));
    }
    exTestPress(now);
    for (int i = 0; i < SDL_NUM_SCANCODES + 6; i++) {
        keyPressed[i] = !keyDown[i] && now[i];
        keyDown[i] = now[i];
    }
}

bool Input::isKeyDown(int k)
{
    if (k >= SDL_NUM_SCANCODES + 6) {
        return false;
    }
    return keyDown[k];
}

bool Input::isKeyPressed(int k)
{
    if (k >= SDL_NUM_SCANCODES + 6) {
        return false;
    }
    return keyPressed[k];
}

const char* Input::keyToChar(unsigned short i)
{
    if (i < SDL_NUM_SCANCODES) {
        return SDL_GetKeyName(SDL_GetKeyFromScancode(SDL_Scancode(i)));
    } else if (i == MOUSEBUTTON_LEFT) {
        return "mouse left button";
    } else if (i == MOUSEBUTTON_RIGHT) {
        return "mouse right button";
    } else if (i == MOUSEBUTTON_MIDDLE) {
        return "mouse middle button";
    } else if (i == MOUSEBUTTON_X1) {
        return "mouse button 4";
    } else if (i == MOUSEBUTTON_X2) {
        return "mouse button 5";
    } else {
        return "unknown";
    }
}

bool Input::MouseClicked()
{
    return isKeyPressed(MOUSEBUTTON_LEFT);
}
