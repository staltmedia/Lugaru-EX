/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef _EX_NET_HPP_
#define _EX_NET_HPP_

#include <string>
#include "Math/XYZ.hpp"

class XYZ;

namespace EX
{
/**
 * Online co-op over UDP, host-authoritative.
 *
 * The host runs the game as usual; every online guest is one more player
 * character on the host (an ExLocal "remote" slot) driven by the buttons and
 * camera angles the guest sends 60 times a second. The host sends back, 60
 * times a second, a compact picture of the level: each character's position,
 * angles and animation state (the guest rebuilds the pose with the game's own
 * animation code), ragdoll joints only while bodies move, loose weapons, and
 * sound / score events. Guests don't simulate anything themselves; their
 * camera, menus, HUD and listener are their own, so it plays like single player.
 */
void NetInit();      // after LocalInit, from [Multiplayer] in LugaruEX.ini
void NetShutdown();
bool NetIsHost();
bool NetIsGuest();
/** host: online guests connected right now (0 when not hosting) */
int NetConnectedGuests();
/** Extras Endless (guest): the window moved by -d - the kept snapshots move along */
void NetGuestShift(const XYZ& d);
/** difficulty the level is played at: the host's on a guest (minimap on Insane), else this PC's */
int NetDifficulty();
/** guest: the host's awards for the end-of-level screen (count; -1 = not a guest, use award_awards) */
int NetGuestAwards(int* awards);
/** once per rendered frame, before the simulation: network in, discovery, timeouts */
void NetFrame(double dt);
/** guest: once per simulation step, before the game's own ticks: place everyone */
void NetGuestStep();
/** host: after every simulation step: snapshots / level info out */
void NetAfterSimStep(double dt);
/** host: a level was loaded (guests follow) */
void NetLevelLoaded(const std::string& name, bool tutorial);
/** online guest: the host's copy of this level was fetched (it differs here): 1 = .json, 2 = binary, 0 = none */
int NetSyncedMap(const std::string& name);
/** host: record a sound for the guests (pos null = not positional) */
void NetSoundEvent(int sound, const XYZ* pos, float volume);
/** host: a player earned a bonus (the pop-up shows on their own screen) */
void NetBonusEvent(int person, int bonus, int value);
/** host: a blood effect happened (kind 0 DoBlood, 1 DoBloodBig, 2 DoBloodBigWhere) - guests replay it */
void NetBloodEvent(int person, int kind, float howmuch, int which, const XYZ* where, bool spurt);
/** host: rumble a player's controller (online guests get it on their own pad) */
void NetRumbleEvent(int person, float strength, int ms);
/** true while a guest replays a host effect (its sounds are not played twice) */
bool NetMuted();
/** Effects the host's simulation makes - sprites (puffs, sprays, dust, splinters, breath)
    and ground decals (footprints, blood pools) - go to guests as events while a NetFxShare
    is alive (the simulation step). NetFxLocal marks code whose effects a guest makes itself
    from the state it gets (flames, the tutorial ghost's breath, snowfall, weapon drips and
    glints), so they are not sent twice. */
struct NetFxShare
{
    NetFxShare();
    ~NetFxShare();
};
struct NetFxLocal
{
    NetFxLocal();
    ~NetFxLocal();
};
/** Sprite::MakeSprite: record it for guests; true = don't make it here (a guest's blood
    replay: its sprays arrive from the host as they were made there) */
bool NetSpriteHook(int type, const XYZ& pos, const XYZ& vel, float r, float g, float b, float size, float opacity);
/** Sprite::setLastSprite*: 0 special, 1 speed, 2 alive time */
void NetSpriteLast(int what, float value);
/** Terrain::MakeDecal: record it for guests */
void NetDecalHook(int type, const XYZ& pos, float size, float opacity, float rotation);
/** the stats display (ping, loss, jitter, delay, fps; fps only offline): the console key cycles console -> display -> off */
bool NetStatsShown();
void NetShowStats(bool on);
/** guest: "Waiting for host..." etc. over the menu */
void NetDrawOverlay();
/** hosting or connected to a host */
bool NetOnline();
/** chat: send a line from this PC's player (shown to everyone, this PC too) */
void NetChatSend(const std::string& text);
/** host console: kick (mode 0), ban (1) or unban (2) a player by name (whole name or its
    start, any case); bans last for this hosting session. Returns the line to show. */
std::string NetConsoleKick(const std::string& who, int mode);

/** Chat (ExChat.cpp): T opens a line to type (when T isn't bound to anything); messages
    go to the console log as "name: text" and float over the speaker's head, fading out. */
bool ChatHandleEvent(const void* sdlEvent); // true = the event was the chat's (typing)
bool ChatBlocksKeys();                      // typing (or Enter / Escape still held after it)
bool ChatTyping();                          // this PC's chat line is open
void ChatReceived(int person, const std::string& name, const std::string& text);
void ChatCapture3D(); // in each view, with the 3D camera still set: remember it for the bubbles
void ChatDraw();      // in each view, after the world: bubbles; typing line on the shared HUD
void ChatDrawHint();  // in each view, last: "Press jump to restart" once every player is down (co-op)
}

#endif
