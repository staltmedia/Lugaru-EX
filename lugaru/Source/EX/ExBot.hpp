/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef _EX_BOT_HPP_
#define _EX_BOT_HPP_

class Person;

namespace EX
{
struct PadInput;

/** host options (the launcher's Host settings; only while hosting, never on a guest) */
bool FriendlyFire(); // players can hit each other (aiming still prefers enemies)
bool ExpandedAI();   // the AI also uses the players' moves (host option, also in Solo)

/** true with a chance of perSecond per second for this simulation step (rate-independent) */
bool AIChance(float perSecond);

/** Expanded AI: an enemy's extra moves, once per simulation step right after its doAI */
void ExpandedAIStep(int person);
/** Expanded AI: an enemy in the air against a wall jumps off it (toward the player it chases) */
bool ExpandedAIWallJump(const Person& p);
/** Expanded AI: an enemy jumping off a wall kicks the player below */
bool ExpandedAIWallKick(const Person& p);
/** Expanded AI: run on all fours to catch up (unarmed rabbits, wolves on any difficulty) */
bool ExpandedAIAllFours(Person& p);
/** Expanded AI: dive at the chased player from the all-fours run, as a player tackles */
bool ExpandedAIDive(const Person& p);
/** Expanded AI: an enemy in one of its own ground / body / sneak moves keeps that move's victim (true; sets it) */
bool ExpandedAIKeepsVictim(Person& p);

/** the friendly AI that plays for an AFK player: its buttons and the way it looks (camera yaw) */
void BotInput(int person, PadInput& in, float& yaw);
}

#endif
