/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef _EX_LOCAL_HPP_
#define _EX_LOCAL_HPP_

#include <string>
#include <vector>

class Person;

namespace EX
{
/**
 * Local split-screen co-op (up to 4 players on one PC).
 *
 * Player 1 is Person 0 as always (keyboard, mouse and its controller). Every
 * other connected controller adds a player: an extra player-controlled Person
 * appended to Person::players after each level load, with its own camera and
 * its own part of the screen ([Local] in LugaruEX.ini, set up in the
 * launcher's Local tab). A player whose controller unplugs stays in the game
 * and waits for that same controller; a new controller drops in at any time.
 */
void LocalInit();                 // after PadInit
void LocalFrame(double dt);       // once per rendered frame: drop-in, stick look
void LocalLevelLoaded(const std::string& levelName, bool tutorial); // spawn the extra players
void LocalSimLook();              // once per simulation step (after TickOnce)
void LocalTickInput();            // every Tick: controller -> extra players' keys
void LocalCameras(void (*camera)()); // run the camera update for players 2..4
void LocalCaptureViews();         // after every simulation step (interpolation)
void LocalAudioEars();            // after the game sets the sound listener: one ear per player in split screen
void LocalInvalidateViews();      // teleports / level loads: no camera blending

/** more than one local player is in the current level (split screen is on) */
bool LocalSplit();
/** Person index whose camera is being updated / drawn right now (0 = player 1) */
int ViewPerson();
/** which local player's view is active (0 = player 1) */
int ViewSlot();
/** a dead player's view moves on to the next player (and back to themselves) */
void SpectateNext(int slot);
/** GL viewport origin of the view being drawn (0,0 outside split screen) */
int ViewX();
int ViewY();
/** draw every player's view; `draw` is the game's whole scene + HUD */
void DrawViews(float alpha, void (*draw)());
/** true while DrawViews is drawing the views (the frame is presented once at the end) */
bool DrawingViews();
/** shared HUD (score): only drawn in the top-left view */
bool DrawSharedHud();
/** this view is the bottom-left one (window corner): the lighting look name shows only here */
bool DrawBottomLeftHud();
/** height to size the shared HUD by: the whole window, even inside a split view */
float SharedHudHeight();
/** projection tweak for the view being drawn: <1 zooms in (small views) / trims height (very wide views) */
float ViewFovScale();

/** camera yaw / pitch a human player is looking along */
float YawOf(int person);
float PitchOf(int person);
/** this PC's player 1: person 0, or an online guest's own character (screen flash, blood dim) */
bool PlayerOneHere(int person);
/** a human player's attack button (player 1: keyboard/mouse/pad) */
bool AttackKeyDown(int person);
/** dialogue scenes in co-op: every human can start one by talking (attack near the
    character); whoever started it clicks through it. Automatic ones (walking into a
    hotspot, a body, a scripted scene) belong to player 1 / the host. */
void DialogInputStep();          // once per simulation step, before dialogues start or advance
bool TalkPressed(int person);    // this human pressed attack this step
void DialogStarted(int owner);   // a dialogue just started, owned by this person
int DialogOwner();               // who clicks through it (falls back to 0 if they left)
bool DialogAdvancePressed();     // the owner pressed attack this step
/** during a dialogue scene: a co-op player who has no part in it is not drawn (nor their
    weapons and shadow), so idle partners don't stand in the middle of the shots */
bool HiddenInScene(int person);
/** console firstperson / thirdperson: the camera at the eyes of a view's own player */
void SetFirstPerson(bool on);
bool FirstPerson();
/** that person looks through its own eyes (a local player: this PC's setting; an online guest: theirs) */
bool FirstPersonOf(int person);
bool FirstPersonActive(int person); // this view looks through this person's eyes now
/** the human player AI `self` goes after (closest living one, with hysteresis) */
int AITarget(Person& self);
bool AllHumansDead();

/** controller id of player 1 ("" = none yet) */
std::string LocalPadOfP1();

/** right after a level loads (before anyone's launcher look is applied): remember the
    look the level gives its player, for the "default" skin */
void NoteLevelLook(const Person& p1);
/** character of the launcher's Character tab / Local tab: model "rabbit"/"wolf", skin path
    ("default" = the level's own skin for its player; on another creature, that creature's first) */
void ApplyCharacter(Person& p, const std::string& model, const std::string& skin, bool tutorial);
/** player 1 with the launcher's "Random" skin: one nobody else in the level wears */
void ApplyRandomLook(Person& p, const std::string& model, bool tutorial);
/** the launcher's Clothes outfit ("" / "default" = keep the level's own) */
void ApplyOutfit(Person& p, const std::string& key);

// ---- online play (EX/ExNet.cpp drives these)
struct PadInput;
/** host: an online guest joins (or comes back: same token = same seat); returns its slot.
    character: "random", "default" or "<model>:<skin path>" (their launcher choice) */
int LocalAddRemote(unsigned long long token, const std::string& character);
/** host: latest buttons / camera angles of an online guest */
void LocalRemoteInput(int slot, const PadInput& in, float yaw, float pitch, bool connected);
/** host: the character of a slot in the current level (-1 = none) */
int LocalSlotPerson(int slot);
/** host: a kicked guest's seat goes, and its character becomes an enemy */
bool LocalRemoteToEnemy(int slot);
/** an online guest's player here on the host (no camera on this PC) */
bool RemotePerson(int person);
/** vibrate the controller of whoever plays this person (local pad, or sent to an online guest) */
void Rumble(int person, float strength, int ms);
/** tutorial: has this player's camera turned since the last call */
bool LocalLooking(int person);
/** how each human player looks, so guests can build the same characters */
struct NetLook
{
    int person, creature, whichskin;
    std::string skin; // custom texture path, "" = the creature's own skins[whichskin]
    std::string outfit; // launcher Clothes ("" = the level's own)
    std::string model;  // a custom model "rabbit/Name" ("" = the creature's own)
};
void LocalRoster(std::vector<NetLook>& out);
/** guest: after loading the host's level, add the other players; mine[k] = this PC's k-th player's character */
void LocalGuestEnter(const std::vector<int>& mine, const std::vector<NetLook>& roster, bool force = false); // force: dress everyone again (their looks arrived)
/** host: dress the online guests' characters again (their skins / models / clothes arrived) */
void LocalReapplyRemoteLooks();
/** guest: how many players are on this PC, what each looks like, and their buttons / camera */
int LocalPlayersHere();
std::string LocalCharacterOf(int k);
void LocalInputOf(int k, PadInput& in, float& yaw, float& pitch);
}

#endif
