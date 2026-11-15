/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef _EX_PAD_HPP_
#define _EX_PAD_HPP_

#include <SDL.h>

#include <string>

namespace EX
{
/**
 * Game controller support (SDL GameController: Xbox 360/One/Series, PlayStation,
 * Switch Pro...). Player 1's controller drives the same actions as the
 * keyboard (bindings from LugaruEX.ini [Pad]); left stick moves, right stick
 * looks. In menus the stick/D-pad moves between buttons, A clicks, B goes
 * back, Start opens/closes the pause menu.
 */
void PadInit();
void PadHandleEvent(const SDL_Event& e);
/** once per rendered frame: menu navigation, lighting button, look input */
void PadFrame(double dt, float& pendingMouseH, float& pendingMouseV);
/** inside Input::Tick: add controller-held "keys" to the new key state */
void PadContribute(bool* down, int numKeys);

/** Stable identity of a controller (shared with the launcher). */
std::string PadIdentity(SDL_GameController* gc);

/** One controller's gameplay actions (bindings applied), for local co-op players. */
struct PadInput
{
    bool fwd = false, back = false, left = false, right = false;
    bool jump = false, crouch = false, attack = false, draw = false, thrw = false;
    bool pause = false, lighting = false;
    bool walk = false;          // walk instead of run (a light push of the left stick; Ctrl on a keyboard)
    bool firstPerson = false;   // (online) this player looks through its eyes: the host turns its body at the limit
    float lookX = 0, lookY = 0; // right stick, shaped, -1..1
};
/** player 1 walks: Ctrl held, or player 1's controller stick pushed only lightly */
bool P1Walk();
int PadCount();
std::string PadIdAt(int i);
bool PadConnected(const std::string& id);
/** false (and everything released) when that controller is not connected */
bool PadRead(const std::string& id, PadInput& out);
/** vibrate a controller: strength 0..1 for ms milliseconds (a weaker rumble never cuts a stronger one short) */
void PadRumble(const std::string& id, float strength, int ms);
float PadLookSpeed();

/** keyboard key (SDL scancode) that cycles lighting looks, default R */
int LightingKey();

/** apply [Keys] bindings from LugaruEX.ini to the game's key variables */
void ApplyKeyBindings();
/** write the game's current key variables back to LugaruEX.ini [Keys] */
void SaveKeyBindings();
}

#endif
