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

#include "Tutorial.hpp"
#include "EX/ExPad.hpp"
#include "EX/ExConfig.hpp"
#include "Audio/Sounds.hpp"
#include "Audio/openal_wrapper.hpp"
#include "Game.hpp"
#include "Level/Awards.hpp"
#include "Objects/Person.hpp"
#include "Utils/Input.hpp"
#include "EX/ExLocal.hpp"
#include "EX/ExNet.hpp"
#include "Objects/Weapons.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

extern bool reversaltrain;
extern bool canattack;
extern bool cananger;
extern int bonus;
extern float damagedealt;
extern bool againbonus;
extern float screenwidth, screenheight;
extern int mainmenu;

bool Tutorial::active = false;
int Tutorial::stage = 0;
float Tutorial::stagetime = 0;
float Tutorial::maxtime = 0;
float Tutorial::success = 0;

// ------------------------------------------------------------------ LugaruEX co-op
// Every player (local split screen and online) is in the tutorial. Each step is
// checked per player; it moves on once all of them have done it. Players 2+ get
// their own knife / sword where the tutorial hands player 1 one, and the sparring
// partner goes after whoever still has to do the current step.
namespace
{
std::vector<float> g_succ;     // per person: progress on this step (>= 1 = done)
std::vector<int> g_bonusOf;    // per person: bonus earned during this step
std::vector<int> g_knifeOf;    // per person (players 2+): their own knife
std::vector<int> g_swordOf;    // per person (players 2+): their own sword
int g_sword0 = 1;              // player 1's sword (was always weapons[1])
int g_remoteDone = -1, g_remoteTotal = 0; // online guest: the host's progress

void ensureSize()
{
    const size_t n = Person::players.size();
    if (g_succ.size() < n) {
        g_succ.resize(n, 0.f);
        g_bonusOf.resize(n, -1);
        g_knifeOf.resize(n, -1);
        g_swordOf.resize(n, -1);
    }
}

std::vector<int> humans()
{
    std::vector<int> h;
    for (size_t i = 0; i < Person::players.size(); i++) {
        if (Person::players[i]->isPlayerControlled()) {
            h.push_back((int)i);
        }
    }
    return h;
}

// where the tutorial drops its weapons, a little apart for each extra player
XYZ weaponSpot(int k)
{
    XYZ a, b;
    a.x = 1011;
    a.y = 84;
    a.z = 491;
    b.x = 1025;
    b.y = 75;
    b.z = 447;
    XYZ p = (a + b) / 2;
    p.x += 0.9f * k;
    return p;
}

int pushWeapon(int type, int k)
{
    Weapon w(type, -1);
    w.position = weaponSpot(k);
    w.tippoint = weaponSpot(k);
    w.velocity = 0.1;
    w.tipvelocity = 0.1;
    w.missed = 1;
    w.hitsomething = 0;
    w.freetime = 0;
    w.firstfree = 1;
    w.physics = 1;
    weapons.push_back(w);
    return (int)weapons.size() - 1;
}

void give(Person& p, int w)
{
    if (w < 0 || w >= (int)weapons.size()) {
        return;
    }
    weapons[w].owner = p.id;
    p.weaponids[0] = w;
    p.num_weapons = 1;
    p.weaponactive = 0;
}

// takes a weapon out of play (out of reach, nobody holding it)
void park(int w)
{
    if (w < 0 || w >= (int)weapons.size()) {
        return;
    }
    weapons[w].owner = -1;
    weapons[w].position = 1000;
    weapons[w].tippoint = 1000;
}

void emptyHands(Person& p)
{
    p.num_weapons = 0;
    p.weaponactive = -1;
}


// player 1 turned the camera (mouse or right stick) since the last check
bool p1Looking()
{
    static float lastYaw = 0, lastPitch = 0;
    const bool moved = fabsf(Game::yaw - lastYaw) > 0.05f || fabsf(Game::pitch - lastPitch) > 0.05f;
    lastYaw = Game::yaw;
    lastPitch = Game::pitch;
    return moved;
}
// has this player done the current step just now
bool doneNow(int h, float multiplier, float& progress)
{
    Person& p = *Person::players[h];
    switch (Tutorial::stage) {
        case 3:
            if (h == 0 ? (Game::deltah || Game::deltav || p1Looking()) : EX::LocalLooking(h)) { // mouse or right stick
                progress += multiplier;
            }
            return progress >= 1;
        case 4:
            if (p.forwardkeydown || p.backkeydown || p.leftkeydown || p.rightkeydown) {
                progress += multiplier;
            }
            return progress >= 1;
        case 5:
            return p.jumpkeydown;
        case 6:
            return p.isCrouch();
        case 7:
            return p.animTarget == rollanim;
        case 8:
            if (p.animTarget == sneakanim) {
                progress += multiplier;
            }
            return progress >= 1;
        case 9:
            if (p.animTarget == rabbitrunninganim || p.animTarget == wolfrunninganim) {
                progress += multiplier;
            }
            return progress >= 1;
        case 11:
            return p.isWallJump();
        case 12:
            return p.animTarget == flipanim;
        case 15:
            return p.animTarget == upunchanim || p.animTarget == winduppunchanim;
        case 16:
            return p.animTarget == winduppunchanim;
        case 17:
            return p.animTarget == spinkickanim;
        case 18:
            return p.animTarget == sweepanim;
        case 19:
            return p.animTarget == dropkickanim;
        case 20:
            return p.animTarget == rabbitkickanim;
        case 21:
            return g_bonusOf[h] == cannon;
        case 22:
            return g_bonusOf[h] == spinecrusher;
        case 23:
            return p.animTarget == walljumprightkickanim || p.animTarget == walljumpleftkickanim;
        case 24:
            return p.animTarget == rabbittacklinganim;
        case 25:
            return p.animTarget == backhandspringanim;
        case 28:
            return Animation::animations[p.animTarget].attack == reversed && p.feint;
        case 29:
            return p.escapednum == 2;
        case 33:
        case 34:
        case 35:
        case 44:
        case 45:
        case 46:
            return Animation::animations[p.animTarget].attack == reversal;
        case 40:
            return p.num_weapons > 0;
        case 41:
            return !p.hasWeapon() && p.num_weapons > 0;
        case 43:
            return p.animTarget == knifeslashstartanim;
        case 49: {
            // this player's own knife is stuck in the sparring partner
            Person& n = *Person::players[1];
            const int mine = h == 0 ? 0 : g_knifeOf[h];
            return n.weaponstuck >= 0 && n.weaponstuck < n.num_weapons && n.weaponids[n.weaponstuck] == mine;
        }
        default:
            return false;
    }
}

// someone finished their part of the step: undo what would stop the next player
void afterDone(int h)
{
    Person& n = *Person::players[1];
    switch (Tutorial::stage) {
        case 44:
        case 45:
        case 46: { // they took the partner's weapon: it goes back to the partner
            Person& p = *Person::players[h];
            emptyHands(p);
            give(n, 0);
            break;
        }
        case 49: // their knife comes out of the partner, so the next one can stick
            if (n.weaponstuck >= 0 && n.weaponstuck < n.num_weapons) {
                const int w = n.weaponids[n.weaponstuck];
                emptyHands(n);
                n.weaponstuck = -1;
                if (w >= 0 && w < (int)weapons.size()) {
                    weapons[w].owner = -1;
                    weapons[w].position = n.coords;
                    weapons[w].tippoint = n.coords;
                }
            }
            break;
        default:
            break;
    }
}
}

// steps that wait for a player to do something (the others just show text for a while)
static bool stepNeedsEveryone(int st)
{
    switch (st) {
        case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 11: case 12:
        case 15: case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25:
        case 28: case 29: case 33: case 34: case 35: case 40: case 41: case 43: case 44: case 45: case 46: case 49:
            return true;
        default:
            return false;
    }
}

static uint32_t g_remoteDoneMask = 0; // online guest: which players the host counts as done

bool Tutorial::IsGhost(int personId)
{
    // the tutorial's sparring partner is always the level's person 1 (the tutorial
    // script itself uses players[1] for it). Not "anyone who isn't a player": a new
    // player isn't marked as one yet while its skeleton loads, and got the ghost's
    // texture mapping (the split-down-the-middle look) and see-through drawing.
    return active && personId == 1;
}

int Tutorial::PlayersIn()
{
    return EX::NetIsGuest() ? g_remoteTotal : (int)humans().size();
}

int Tutorial::PlayersDone()
{
    if (EX::NetIsGuest()) {
        return g_remoteDone;
    }
    int n = 0;
    for (int h : humans()) {
        n += DoneBy(h) ? 1 : 0;
    }
    return n;
}

bool Tutorial::DoneBy(int personId)
{
    if (EX::NetIsGuest()) {
        return personId >= 0 && personId < 32 && (g_remoteDoneMask >> personId) & 1;
    }
    return personId >= 0 && personId < (int)g_succ.size() && g_succ[personId] >= 1;
}

unsigned int Tutorial::DoneMask()
{
    uint32_t m = 0;
    for (size_t i = 0; i < g_succ.size() && i < 32; i++) {
        if (g_succ[i] >= 1) {
            m |= 1u << i;
        }
    }
    return m;
}

void Tutorial::SetRemoteProgress(int done, int total, unsigned int doneMask)
{
    g_remoteDone = done;
    g_remoteTotal = total;
    g_remoteDoneMask = doneMask;
}

void Tutorial::OnBonus(int personId, int b)
{
    ensureSize();
    if (personId >= 0 && personId < (int)g_bonusOf.size()) {
        g_bonusOf[personId] = b;
    }
}

void Tutorial::Do(float multiplier)
{
    if (stagetime > maxtime) {
        stage++;
        success = 0;
        if (stage <= 1) {
            canattack = 0;
            cananger = 0;
            reversaltrain = 0;
        }
        switch (stage) {
            case 1:
                maxtime = 5;
                break;
            case 2:
            case 10:
            case 13:
            case 26:
                maxtime = 2;
                break;
            case 3:
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
                maxtime = 600;
                break;
            case 4:
            case 11:
            case 12:
                maxtime = 1000;
                break;
            case 14: {
                maxtime = 3;

                XYZ temp, temp2;

                temp.x = 1011;
                temp.y = 84;
                temp.z = 491;
                temp2.x = 1025;
                temp2.y = 75;
                temp2.z = 447;

                Person::players[1]->coords = (temp + temp2) / 2;

                emit_sound_at(fireendsound, Person::players[1]->coords);

                for (unsigned i = 0; i < Person::players[1]->skeleton.joints.size(); i++) {
                    if (Random() % 2 == 0) {
                        if (!Person::players[1]->skeleton.free) {
                            temp2 = (Person::players[1]->coords - Person::players[1]->oldcoords) / multiplier / 2; //velocity/2;
                        }
                        if (Person::players[1]->skeleton.free) {
                            temp2 = Person::players[1]->skeleton.joints[i].velocity * Person::players[1]->scale / 2;
                        }
                        if (!Person::players[1]->skeleton.free) {
                            temp = DoRotation(DoRotation(DoRotation(Person::players[1]->skeleton.joints[i].position, 0, 0, Person::players[1]->tilt), Person::players[1]->tilt2, 0, 0), 0, Person::players[1]->yaw, 0) * Person::players[1]->scale + Person::players[1]->coords;
                        }
                        if (Person::players[1]->skeleton.free) {
                            temp = Person::players[1]->skeleton.joints[i].position * Person::players[1]->scale + Person::players[1]->coords;
                        }
                        Sprite::MakeSprite(breathsprite, temp, temp2, 1, 1, 1, .6 + (float)abs(Random() % 100) / 200 - .25, 1);
                    }
                }
            } break;
            case 15:
            case 16:
            case 17:
            case 18:
            case 20:
            case 22:
            case 23:
            case 24:
            case 25:
                maxtime = 500;
                break;
            case 19:
                stage = 20;
                break;
            case 21:
                maxtime = 500;
                if (bonus == cannon) {
                    bonus = Slicebonus;
                    againbonus = 1;
                } else {
                    againbonus = 0;
                }
                break;
            case 27:
                maxtime = 4;
                reversaltrain = 1;
                cananger = 1;
                Person::players[1]->aitype = attacktypecutoff;
                break;
            case 28:
            case 34:
            case 35:
                maxtime = 400;
                break;
            case 29:
                maxtime = 400;
                Person::players[0]->escapednum = 0;
                break;
            case 30:
                maxtime = 4;
                reversaltrain = 0;
                cananger = 0;
                Person::players[1]->aitype = passivetype;
                break;
            case 31:
                maxtime = 13;
                break;
            case 32:
            case 42:
                maxtime = 8;
                break;
            case 33:
                maxtime = 400;
                cananger = 1;
                canattack = 1;
                Person::players[1]->aitype = attacktypecutoff;
                break;
            case 36:
                maxtime = 2;
                reversaltrain = 0;
                cananger = 0;
                Person::players[1]->aitype = passivetype;
                break;
            case 37:
                damagedealt = 0;
                damagetaken = 0;
                maxtime = 50;
                cananger = 1;
                canattack = 1;
                Person::players[1]->aitype = attacktypecutoff;
                break;
            case 38:
                maxtime = 4;
                canattack = 0;
                cananger = 0;
                Person::players[1]->aitype = passivetype;
                break;
            case 39: {
                XYZ temp, temp2;

                temp.x = 1011;
                temp.y = 84;
                temp.z = 491;
                temp2.x = 1025;
                temp2.y = 75;
                temp2.z = 447;

                Weapon w(knife, -1);
                w.position = (temp + temp2) / 2;
                w.tippoint = (temp + temp2) / 2;

                w.velocity = 0.1;
                w.tipvelocity = 0.1;
                w.missed = 1;
                w.hitsomething = 0;
                w.freetime = 0;
                w.firstfree = 1;
                w.physics = 1;

                weapons.push_back(w);
            } break;
            case 40:
            case 41:
            case 43:
                maxtime = 300;
                break;
            case 44:
                weapons[0].owner = 1;
                Person::players[0]->weaponactive = -1;
                Person::players[0]->num_weapons = 0;
                Person::players[1]->weaponactive = 0;
                Person::players[1]->num_weapons = 1;
                Person::players[1]->weaponids[0] = 0;

                cananger = 1;
                canattack = 1;
                Person::players[1]->aitype = attacktypecutoff;

                maxtime = 300;
                break;
            case 45:
                weapons[0].owner = 1;
                Person::players[0]->weaponactive = -1;
                Person::players[0]->num_weapons = 0;
                Person::players[1]->weaponactive = 0;
                Person::players[1]->num_weapons = 1;
                Person::players[1]->weaponids[0] = 0;

                maxtime = 300;
                break;
            case 46:
                weapons[0].owner = 1;
                Person::players[0]->weaponactive = -1;
                Person::players[0]->num_weapons = 0;
                Person::players[1]->weaponactive = 0;
                Person::players[1]->num_weapons = 1;
                Person::players[1]->weaponids[0] = 0;

                weapons[0].setType(sword);

                maxtime = 300;
                break;
            case 47: {
                maxtime = 10;

                XYZ temp, temp2;

                temp.x = 1011;
                temp.y = 84;
                temp.z = 491;
                temp2.x = 1025;
                temp2.y = 75;
                temp2.z = 447;

                Weapon w(sword, -1);
                w.position = (temp + temp2) / 2;
                w.tippoint = (temp + temp2) / 2;

                w.velocity = 0.1;
                w.tipvelocity = 0.1;
                w.missed = 1;
                w.hitsomething = 0;
                w.freetime = 0;
                w.firstfree = 1;
                w.physics = 1;

                weapons.push_back(w);
                g_sword0 = (int)weapons.size() - 1; // LugaruEX: other players' knives come before it

                weapons[0].owner = 1;
                weapons[g_sword0].owner = 0;
                Person::players[0]->weaponactive = 0;
                Person::players[0]->num_weapons = 1;
                Person::players[0]->weaponids[0] = g_sword0;
                Person::players[1]->weaponactive = 0;
                Person::players[1]->num_weapons = 1;
                Person::players[1]->weaponids[0] = 0;

            } break;
            case 48:
                canattack = 0;
                cananger = 0;
                Person::players[1]->aitype = passivetype;

                maxtime = 15;

                weapons[0].owner = 1;
                weapons[g_sword0].owner = 0;
                Person::players[0]->weaponactive = 0;
                Person::players[0]->num_weapons = 1;
                Person::players[0]->weaponids[0] = g_sword0;
                Person::players[1]->weaponactive = 0;
                Person::players[1]->num_weapons = 1;
                Person::players[1]->weaponids[0] = 0;

                if (Person::players[0]->hasWeapon()) {
                    weapons[Person::players[0]->weaponids[Person::players[0]->weaponactive]].setType(staff);
                } else {
                    weapons[0].setType(staff);
                }
                break;
            case 49:
                canattack = 0;
                cananger = 0;
                Person::players[1]->aitype = passivetype;

                maxtime = 200;

                weapons[g_sword0].position = 1000;
                weapons[g_sword0].tippoint = 1000;

                weapons[0].setType(knife);

                weapons[0].owner = 0;
                Person::players[1]->weaponactive = -1;
                Person::players[1]->num_weapons = 0;
                Person::players[0]->weaponactive = 0;
                Person::players[0]->num_weapons = 1;
                Person::players[0]->weaponids[0] = 0;

                break;
            case 50: {
                maxtime = 8;

                XYZ temp, temp2;
                emit_sound_at(fireendsound, Person::players[1]->coords);

                for (unsigned i = 0; i < Person::players[1]->skeleton.joints.size(); i++) {
                    if (Random() % 2 == 0) {
                        if (!Person::players[1]->skeleton.free) {
                            temp2 = (Person::players[1]->coords - Person::players[1]->oldcoords) / multiplier / 2; //velocity/2;
                        }
                        if (Person::players[1]->skeleton.free) {
                            temp2 = Person::players[1]->skeleton.joints[i].velocity * Person::players[1]->scale / 2;
                        }
                        if (!Person::players[1]->skeleton.free) {
                            temp = DoRotation(DoRotation(DoRotation(Person::players[1]->skeleton.joints[i].position, 0, 0, Person::players[1]->tilt), Person::players[1]->tilt2, 0, 0), 0, Person::players[1]->yaw, 0) * Person::players[1]->scale + Person::players[1]->coords;
                        }
                        if (Person::players[1]->skeleton.free) {
                            temp = Person::players[1]->skeleton.joints[i].position * Person::players[1]->scale + Person::players[1]->coords;
                        }
                        Sprite::MakeSprite(breathsprite, temp, temp2, 1, 1, 1, .6 + (float)abs(Random() % 100) / 200 - .25, 1);
                    }
                }

                Person::players[1]->num_weapons = 0;
                Person::players[1]->weaponstuck = -1;
                Person::players[1]->weaponactive = -1;

                weapons.clear();
            } break;
            case 51:
                maxtime = 80000;
                break;
            default:
                break;
        }
        // LugaruEX co-op: a fresh step for everyone, and the same setup for players 2+
        ensureSize();
        std::fill(g_succ.begin(), g_succ.end(), 0.f);
        std::fill(g_bonusOf.begin(), g_bonusOf.end(), -1);
        int extra = 0;
        for (int h : humans()) {
            if (h == 0) {
                continue;
            }
            extra++;
            Person& p = *Person::players[h];
            switch (stage) {
                case 29:
                    p.escapednum = 0;
                    break;
                case 39: // their own knife on the ground, next to player 1's
                    g_knifeOf[h] = pushWeapon(knife, extra);
                    break;
                case 44:
                case 45:
                case 46: // empty-handed against the partner's weapon
                    if (g_knifeOf[h] >= 0) {
                        park(g_knifeOf[h]);
                    }
                    emptyHands(p);
                    break;
                case 47:
                    g_swordOf[h] = pushWeapon(sword, extra);
                    give(p, g_swordOf[h]);
                    break;
                case 48:
                    if (p.hasWeapon()) {
                        weapons[p.weaponids[p.weaponactive]].setType(staff);
                    }
                    break;
                case 49: // the sword goes away, their knife comes back to throw
                    if (g_swordOf[h] >= 0) {
                        park(g_swordOf[h]);
                    }
                    emptyHands(p);
                    if (g_knifeOf[h] >= 0) {
                        weapons[g_knifeOf[h]].setType(knife);
                        give(p, g_knifeOf[h]);
                    }
                    break;
                case 50:
                    emptyHands(p);
                    g_knifeOf[h] = g_swordOf[h] = -1;
                    break;
                default:
                    break;
            }
        }
        if (stage == 50) {
            emptyHands(*Person::players[0]);
        }
        if (stage <= 51) {
            stagetime = 0;
        }
    }

    //Tutorial success
    // LugaruEX co-op: each player has to do the step; it counts once all of them have
    if (stagetime < maxtime - 3) {
        ensureSize();
        const std::vector<int> hs = humans();
        const bool coop = hs.size() > 1;
        bool all = !hs.empty();
        for (int h : hs) {
            if (g_succ[h] < 1) {
                float progress = g_succ[h];
                if (doneNow(h, multiplier, progress)) {
                    progress = 1;
                }
                g_succ[h] = progress;
                if (g_succ[h] >= 1 && coop) {
                    afterDone(h);
                    emit_sound_np(consolesuccesssound); // that player's part is done
                }
            }
            all = all && g_succ[h] >= 1;
        }
        if (all) {
            success = 1;
            if (stage == 29 || stage == 35) {
                reversaltrain = 0;
                cananger = 0;
                Person::players[1]->aitype = passivetype;
            }
        }
        if (success >= 1) {
            stagetime = maxtime - 3;
        }

        if (stagetime == maxtime - 3) {
            emit_sound_np(consolesuccesssound);
        }

        if (success >= 1) {
            if (stage == 34 || stage == 35) {
                stagetime = maxtime - 1;
            }
        }
    }

    if (stage < 14 || stage >= 50) {
        Person::players[1]->coords.y = 300;
        Person::players[1]->velocity = 0;
    }
}

void Tutorial::DrawTextInfo()
{
    std::string string = " ";
    std::string string2 = " ";
    std::string string3 = " ";

    switch (stage) {
        case 0:
        default:
            break;
        case 1:
            string = "Welcome to the Lugaru training level!";
            break;
        case 2:
            string = "BASIC MOVEMENT:";
            break;
        case 3:
            string = EX::ViewUsesPad() ? "You can move the right stick to rotate the camera." : "You can move the mouse to rotate the camera.";
            break;
        case 4:
            if (EX::ViewUsesPad()) {
                string = "Try using the left stick to move around.";
                string2 = "All movement is relative to the camera.";
                break;
            }
            string = std::string("Try using the ") + Input::keyToChar(Game::forwardkey);
            string += std::string(", ") + Input::keyToChar(Game::leftkey);
            string += std::string(", ") + Input::keyToChar(Game::backkey);
            string += std::string(" and ") + Input::keyToChar(Game::rightkey) + " keys to move around.";
            string2 = "All movement is relative to the camera.";
            break;
        case 5:
            string = std::string("Please press ") + EX::KeyText(Game::jumpkey) + " to jump.";
            string2 = "You can hold it longer to jump higher.";
            break;
        case 6:
            string = std::string("You can press ") + EX::KeyText(Game::crouchkey) + " to crouch.";
            string2 = "You can jump higher from a crouching position.";
            break;
        case 7:
            string = std::string("While running, you can press ") + EX::KeyText(Game::crouchkey) + " to roll.";
            break;
        case 8:
            string = "While crouching, you can sneak around silently";
            string2 = EX::ViewUsesPad() ? "using the left stick." : "using the movement keys.";
            break;
        case 9:
            string = EX::ViewUsesPad() ? "Release the crouch button while sneaking and keep pushing the stick" : "Release the crouch key while sneaking and hold the movement keys";
            string2 = "to run animal-style.";
            break;
        case 10:
            string = "ADVANCED MOVEMENT:";
            break;
        case 11:
            string = std::string("When you jump at a wall, you can hold ") + EX::KeyText(Game::jumpkey) + " again";
            string2 = "during impact to perform a walljump.";
            string3 = EX::ViewUsesPad() ? "Be sure to push the left stick against the wall" : "Be sure to use the movement keys to press against the wall";
            break;
        case 12:
            string = "While in the air, you can press crouch to flip.";
            string2 = "Walljumps and flips confuse enemies and give you more control.";
            break;
        case 13:
            string = "BASIC COMBAT:";
            break;
        case 14:
            string = "There is now an imaginary enemy";
            string2 = "in the middle of the training area.";
            break;
        case 15:
            string = std::string("Press ") + EX::KeyText(Game::attackkey) + " to attack when you are near an enemy.";
            string2 = "You can punch by standing still near an enemy and attacking.";
            break;
        case 16:
            string = "If you are close, you will perform a weak punch.";
            string2 = "The weak punch is excellent for starting attack combinations.";
            break;
        case 17:
            string = "Attacking while running results in a spin kick.";
            string2 = "This is one of your most powerful ground attacks.";
            break;
        case 18:
            string = "Sweep the enemy's legs out by attacking while crouched.";
            string2 = "This is a very fast attack, and easy to follow up.";
            break;
        case 19:
            string = "When an enemy is on the ground, you can deal some extra";
            string2 = "damage by running up and drop-kicking him.";
            string3 = "(Try knocking them down with a sweep first)";
            break;
        case 20:
            string = "Your most powerful individual attack is the rabbit kick.";
            string2 = std::string("Run at the enemy while holding ") + EX::KeyText(Game::attackkey) + ", and press";
            string3 = std::string("the jump ") + EX::KeyWord() + " (" + EX::KeyText(Game::jumpkey) + ") to attack.";
            break;
        case 21:
            string = "This attack is devastating if timed correctly.";
            string2 = "Even if timed incorrectly, it will knock the enemy over.";
            if (againbonus) {
                string3 = "Try rabbit-kicking the imaginary enemy again.";
            } else {
                string3 = "Try rabbit-kicking the imaginary enemy.";
            }
            break;
        case 22:
            string = "If you sneak behind an enemy unnoticed, you can kill";
            string2 = "him instantly. Move close behind this enemy";
            string3 = "and attack.";
            break;
        case 23:
            string = "Another important attack is the wall kick. When an enemy";
            string2 = "is near a wall, perform a walljump nearby and hold";
            string3 = std::string("the attack ") + EX::KeyWord() + " during impact with the wall.";
            break;
        case 24:
            string = "You can tackle enemies by running at them animal-style";
            string2 = std::string("and pressing jump (") + EX::KeyText(Game::jumpkey) + ") or attack (" + EX::KeyText(Game::attackkey) + ").";
            string3 = "This is especially useful when they are running away.";
            break;
        case 25:
            string = "Dodge by pressing back and attack. Dodging is essential";
            string2 = "against enemies with swords or other long weapons.";
            break;
        case 26:
            string = "REVERSALS AND COUNTER-REVERSALS";
            break;
        case 27:
            string = "The enemy can now reverse your attacks.";
            break;
        case 28:
            string = "If you attack, you will notice that the enemy now sometimes";
            string2 = "catches your attack and uses it against you. Hold";
            string3 = std::string("crouch (") + EX::KeyText(Game::crouchkey) + ") after attacking to escape from reversals.";
            break;
        case 29:
            string = "Try escaping from two more reversals in a row.";
            break;
        case 30:
            string = "Good!";
            break;
        case 31:
            string = std::string("To reverse an attack, you must tap crouch (") + EX::KeyText(Game::crouchkey) + ") during the";
            string2 = "enemy's attack. You must also be close to the enemy;";
            string3 = "this is especially important against armed opponents.";
            break;
        case 32:
            string = "The enemy can attack in " + to_string(int(maxtime - stagetime)) + " seconds.";
            string2 = "This imaginary opponents attacks will be highlighted";
            string3 = "to make this easier.";
            break;
        case 33:
            string = "Reverse three enemy attacks!";
            break;
        case 34:
            string = "Reverse two more enemy attacks!";
            break;
        case 35:
            string = "Reverse one more enemy attack!";
            break;
        case 36:
            string = "Excellent!";
            break;
        case 37:
            string = "Now spar with the enemy for " + to_string(int(maxtime - stagetime)) + " more seconds.";
            string2 = "Damage dealt: " + to_string(int(damagedealt));
            string3 = "Damage taken: " + to_string(int(damagetaken));
            break;
        case 38:
            string = "WEAPONS:";
            break;
        case 39:
            string = "There is now an imaginary knife";
            string2 = "in the center of the training area.";
            break;
        case 40:
            string = "Stand, roll or handspring over the knife";
            string2 = std::string("while pressing ") + EX::KeyText(Game::throwkey) + " to pick it up.";
            string3 = std::string("You can crouch and press the same ") + EX::KeyWord() + " to drop it again.";
            break;
        case 41:
            string = std::string("You can equip and unequip weapons using the ") + EX::KeyText(Game::drawkey) + " " + EX::KeyWord() + ".";
            string2 = "Sometimes it is best to keep them unequipped to";
            string3 = "prevent enemies from taking them. ";
            break;
        case 42:
            string = "The knife is the smallest weapon and the least encumbering.";
            string2 = "You can equip or unequip it while standing, crouching,";
            string3 = "running or flipping.";
            break;
        case 43:
            string = "You perform weapon attacks the same way as unarmed attacks,";
            string2 = "but sharp weapons cause permanent damage, instead of the";
            string3 = "temporary trauma from blunt weapons, fists and feet.";
            break;
        case 44:
            string = "The enemy now has your knife!";
            string2 = "Please reverse two of his knife attacks.";
            break;
        case 45:
            string = "Please reverse one more of his knife attacks.";
            break;
        case 46:
            string = "Now he has a sword!";
            string2 = "The sword has longer reach than your arms, so you";
            string3 = "must move close to reverse the sword slash.";
            break;
        case 47:
            string = "Long weapons like the sword and staff are also useful for defense;";
            string2 = std::string("you can parry enemy weapon attacks by pressing the attack ") + EX::KeyWord();
            string3 = "at the right time. Please try parrying the enemy's attacks!";
            break;
        case 48:
            string = "The staff is like the sword, but has two main attacks.";
            string2 = "The standing smash is fast and effective, and the running";
            string3 = "spin smash is slower and more powerful.";
            break;
        case 49:
            string = std::string("When facing an enemy, you can throw the knife with ") + EX::KeyText(Game::throwkey) + ".";
            string2 = "It is possible to throw the knife while flipping,";
            string3 = "but it is very inaccurate.";
            break;
        case 50:
            string = "You now know everything you can learn from training.";
            string2 = "Everything else you must learn from experience!";
            break;
        case 51:
            string = "Walk out of the training area to return to the main menu.";
            break;
    }

    float opacity = maxtime - stagetime;

    if (opacity > 1) {
        opacity = 1;
    }
    if (opacity < 0) {
        opacity = 0;
    }

    Game::text->glPrintOutlined(1, 1, 1, opacity, screenwidth / 2 - 7.6 * string.size() * EX::HudWidth() / 1024, screenheight / 16 + screenheight * 4 / 5, string, 1, 1.5 * EX::HudWidth() / 1024, screenwidth, screenheight);
    Game::text->glPrintOutlined(1, 1, 1, opacity, screenwidth / 2 - 7.6 * string2.size() * EX::HudWidth() / 1024, screenheight / 16 + screenheight * 4 / 5 - 20 * EX::HudWidth() / 1024, string2, 1, 1.5 * EX::HudWidth() / 1024, screenwidth, screenheight);
    Game::text->glPrintOutlined(1, 1, 1, opacity, screenwidth / 2 - 7.6 * string3.size() * EX::HudWidth() / 1024, screenheight / 16 + screenheight * 4 / 5 - 40 * EX::HudWidth() / 1024, string3, 1, 1.5 * EX::HudWidth() / 1024, screenwidth, screenheight);

    // LugaruEX co-op: how many players have done this step (it moves on when all have)
    const int total = PlayersIn();
    if (total > 1 && stepNeedsEveryone(stage) && stagetime < maxtime - 3) {
        const int done = std::max(0, PlayersDone());
        std::string p = DoneBy(EX::ViewPerson())
                            ? "Done - waiting for the others (" + std::to_string(done) + "/" + std::to_string(total) + ")"
                            : std::to_string(done) + "/" + std::to_string(total) + " players done";
        Game::text->glPrintOutlined(.7, 1, .7, opacity, screenwidth / 2 - 7.6 * p.size() * EX::HudWidth() / 1024 * .8, screenheight / 16 + screenheight * 4 / 5 - 64 * EX::HudWidth() / 1024, p, 1, 1.5 * EX::HudWidth() / 1024 * .8, screenwidth, screenheight);
    }

    string = EX::ViewUsesPad() ? "Click " + EX::SkipText() + " to skip to the next item." : "Press 'tab' to skip to the next item.";
    string2 = "Press " + EX::PauseText() + " at any time to";
    string3 = "pause or exit the tutorial.";

    Game::text->glPrintOutlined(0.5, 0.5, 0.5, 1, screenwidth / 2 - 7.6 * string.size() * EX::HudWidth() / 1024 * .8, 0 + screenheight * 1 / 10, string, 1, 1.5 * EX::HudWidth() / 1024 * .8, screenwidth, screenheight);
    Game::text->glPrintOutlined(0.5, 0.5, 0.5, 1, screenwidth / 2 - 7.6 * string2.size() * EX::HudWidth() / 1024 * .8, 0 + screenheight * 1 / 10 - 20 * .8 * EX::HudWidth() / 1024, string2, 1, 1.5 * EX::HudWidth() / 1024 * .8, screenwidth, screenheight);
    Game::text->glPrintOutlined(0.5, 0.5, 0.5, 1, screenwidth / 2 - 7.6 * string3.size() * EX::HudWidth() / 1024 * .8, 0 + screenheight * 1 / 10 - 40 * .8 * EX::HudWidth() / 1024, string3, 1, 1.5 * EX::HudWidth() / 1024 * .8, screenwidth, screenheight);
}

void Tutorial::DoStuff(float multiplier)
{
    XYZ temp;
    XYZ temp2;
    XYZ temp3;
    XYZ oldtemp;
    XYZ oldtemp2;
    temp.x = 1011;
    temp.y = 84;
    temp.z = 491;
    temp2.x = 1025;
    temp2.y = 75;
    temp2.z = 447;
    temp3.x = 1038;
    temp3.y = 76;
    temp3.z = 453;
    oldtemp = temp;
    oldtemp2 = temp2;
    if (stage >= 51) {
        if (distsq(&temp, &Person::players[0]->coords) >= distsq(&temp, &temp2) - 1 || distsq(&temp3, &Person::players[0]->coords) < 4) {
            OPENAL_StopSound(OPENAL_ALL); // hack...OpenAL renderer isn't stopping music after tutorial goes to level menu...
            OPENAL_SetFrequency(OPENAL_ALL);

            emit_stream_np(stream_menutheme);

            Game::gameon = 0;
            mainmenu = 5;

            Game::fireSound();

            Game::flash();
        }
    } else {
        if (distsq(&temp, &Person::players[0]->coords) >= distsq(&temp, &temp2) - 1 || distsq(&temp3, &Person::players[0]->coords) < 4) {
            emit_sound_at(fireendsound, Person::players[0]->coords);

            Person::players[0]->coords = (oldtemp + oldtemp2) / 2;

            Game::flash();
        }
    }
    if (stage >= 14 && stage < 50) {
        if (distsq(&temp, &Person::players[1]->coords) >= distsq(&temp, &temp2) - 1 || distsq(&temp3, &Person::players[1]->coords) < 4) {
            emit_sound_at(fireendsound, Person::players[1]->coords);

            for (unsigned i = 0; i < Person::players[1]->skeleton.joints.size(); i++) {
                if (Random() % 2 == 0) {
                    if (!Person::players[1]->skeleton.free) {
                        temp2 = (Person::players[1]->coords - Person::players[1]->oldcoords) / multiplier / 2; //velocity/2;
                    }
                    if (Person::players[1]->skeleton.free) {
                        temp2 = Person::players[1]->skeleton.joints[i].velocity * Person::players[1]->scale / 2;
                    }
                    if (!Person::players[1]->skeleton.free) {
                        temp = DoRotation(DoRotation(DoRotation(Person::players[1]->skeleton.joints[i].position, 0, 0, Person::players[1]->tilt), Person::players[1]->tilt2, 0, 0), 0, Person::players[1]->yaw, 0) * Person::players[1]->scale + Person::players[1]->coords;
                    }
                    if (Person::players[1]->skeleton.free) {
                        temp = Person::players[1]->skeleton.joints[i].position * Person::players[1]->scale + Person::players[1]->coords;
                    }
                    Sprite::MakeSprite(breathsprite, temp, temp2, 1, 1, 1, .6 + (float)abs(Random() % 100) / 200 - .25, 1);
                }
            }

            Person::players[1]->coords = (oldtemp + oldtemp2) / 2;
            for (unsigned i = 0; i < Person::players[1]->skeleton.joints.size(); i++) {
                Person::players[1]->skeleton.joints[i].velocity = 0;
                if (Random() % 2 == 0) {
                    if (!Person::players[1]->skeleton.free) {
                        temp2 = (Person::players[1]->coords - Person::players[1]->oldcoords) / multiplier / 2; //velocity/2;
                    }
                    if (Person::players[1]->skeleton.free) {
                        temp2 = Person::players[1]->skeleton.joints[i].velocity * Person::players[1]->scale / 2;
                    }
                    if (!Person::players[1]->skeleton.free) {
                        temp = DoRotation(DoRotation(DoRotation(Person::players[1]->skeleton.joints[i].position, 0, 0, Person::players[1]->tilt), Person::players[1]->tilt2, 0, 0), 0, Person::players[1]->yaw, 0) * Person::players[1]->scale + Person::players[1]->coords;
                    }
                    if (Person::players[1]->skeleton.free) {
                        temp = Person::players[1]->skeleton.joints[i].position * Person::players[1]->scale + Person::players[1]->coords;
                    }
                    Sprite::MakeSprite(breathsprite, temp, temp2, 1, 1, 1, .6 + (float)abs(Random() % 100) / 200 - .25, 1);
                }
            }
        }
    }
}
