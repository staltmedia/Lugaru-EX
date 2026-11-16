/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef _EX_GEN_HPP_
#define _EX_GEN_HPP_

#include <json/value.h>
#include <string>

class Person;

namespace EX
{
/** Extras (main menu): game modes on levels made from scratch. A generated level is an
 * ordinary map file (Mods/.gen/Maps) carrying its ground heights and its mode, so the
 * loader, restarts and online guests (who fetch the host's copy) need nothing special. */
enum
{
    GEN_NONE = 0,
    GEN_ENDLESS = 1, // waves of enemies, a new land after each one cleared; co-op
    GEN_PVP = 2,     // players against each other, last one standing wins
};
int GenMode(); // the loaded level's mode (GEN_NONE: campaign, challenge, tutorial...)
inline bool GenActive()
{
    return GenMode() != GEN_NONE;
}
bool Versus(); // PvP: every player is everyone's enemy
/** two humans on the same side: players, and not in PvP */
bool Partners(const Person& a, const Person& b);
int GenWave();

/** the Extras menu: make a new level of this mode and land (environment) and start it */
bool GenStart(int mode, int environment);
/** host: after the end screen (next wave / next round) or when everyone is down (restart) */
void GenContinue(bool restart);

/** level loader hooks */
void GenReset();                         // before every level load (both map formats)
void GenMapRead(const Json::Value& map, const std::string& name); // the map file was read: its generated parts
void GenGroundObjects();                 // objects were made: onto the ground
void GenGroundPeople();                  // people and path points were made: onto the ground

/** host, every simulation step: players' start spots and weapons, loose weapons */
void GenTick();
/** PvP: the last player standing once there were two or more (-1 = none yet) */
int GenWinner();
/** PvP: the round is decided (one left) - the challenge "level cleared" flow follows */
bool GenVersusWon();
/** PvP with two or more players: the last-one-standing rule (alone, it is clear the enemies) */
bool GenVersusRound();
/** Endless: where the window lies on the endless land (host: for the snapshots) */
void GenOrigin(int& ox, int& oz);
/** online guest: the host's window moved - move along */
void GenGuestOrigin(int ox, int oz);
/** Endless: how long the run lasted (until the last player went down) */
float GenRunTime();
/** a player's own score (PvP: each player scores alone) */
void GenScore(int person, int value);

/** Endless: a player pressing / holding Revive (crouched, unarmed, over a fallen partner) */
void GenReviveInput(int person, bool held, bool pressed);
}

#endif
