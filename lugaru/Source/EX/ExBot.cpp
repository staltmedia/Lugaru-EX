/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Host options that change how characters fight: Friendly fire, Expanded AI (enemies also
// use the players' moves) and the friendly AI that plays for an AFK player. The AFK bot
// presses the same buttons a player would, so everything a player can do and every
// co-op rule (targeting, awards, spectating) stays exactly as for a real player.

#include "EX/ExBot.hpp"

#include "EX/ExConfig.hpp"
#include "EX/ExNet.hpp"
#include "EX/ExPad.hpp"
#include "Animation/Animation.hpp"
#include "Game.hpp"
#include "Objects/Object.hpp"
#include "Objects/Person.hpp"
#include "Objects/Weapons.hpp"
#include "Tutorial.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

extern int difficulty;
extern int hostile;
extern bool winfreeze;

namespace EX
{
static bool hostRules()
{
    return !NetIsGuest() && cfg.mpMode == "host" && !Tutorial::active;
}

bool FriendlyFire()
{
    return hostRules() && cfg.mpFriendlyFire;
}

bool ExpandedAI()
{
    return hostRules() && cfg.mpExpandedAI;
}

static float stepTime()
{
    return 1.f / (float)std::max(30, SimRate());
}

bool AIChance(float perSecond)
{
    const float p = perSecond * stepTime();
    return (float)(rand() % 10000) < p * 10000.f;
}

// how eager the extra moves are: Easy half as often, Insane twice
static float rate()
{
    return difficulty <= 0 ? .5f : difficulty == 1 ? 1.f : 2.f;
}

static bool valid(int i)
{
    return i >= 0 && i < (int)Person::players.size();
}

static float flatDot(const Person& a, const XYZ& to)
{
    XYZ d;
    d.x = to.x - a.coords.x;
    d.z = to.z - a.coords.z;
    d.y = 0;
    XYZ f = a.facing;
    f.y = 0;
    const float ld = sqrtf(d.x * d.x + d.z * d.z), lf = sqrtf(f.x * f.x + f.z * f.z);
    if (ld < 1e-4f || lf < 1e-4f) {
        return 0;
    }
    return (d.x * f.x + d.z * f.z) / (ld * lf);
}

static float dist2(const XYZ& a, const XYZ& b)
{
    return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
}

static bool canSee(Person& a, Person& b)
{
    return -1 == Object::checkcollide(DoRotation(a.jointPos(head), 0, a.yaw, 0) * a.scale + a.coords,
                                      DoRotation(b.jointPos(head), 0, b.yaw, 0) * b.scale + b.coords);
}

static bool hasKnifeOut(Person& p)
{
    return p.hasWeapon() && p.weaponactive == 0 && p.weaponids[0] >= 0 && p.weaponids[0] < (int)weapons.size() &&
           weapons[p.weaponids[0]].getType() == knife;
}

// ------------------------------------------------------------------ Expanded AI (enemies)
static Person* chased(const Person& p)
{
    if (!valid(p.exTarget)) {
        return nullptr;
    }
    Person* v = Person::players[p.exTarget].get();
    if (v == &p || !v->isPlayerControlled() || v->dead || v->skeleton.free) {
        return nullptr;
    }
    return v;
}

static bool fighting(const Person& p)
{
    return ExpandedAI() && hostile && !winfreeze && !p.isPlayerControlled() && !p.dead && !p.skeleton.free &&
           p.aitype == attacktypecutoff && p.stunned <= 0 && p.surprised <= 0;
}

void ExpandedAIStep(int person)
{
    if (!valid(person)) {
        return;
    }
    Person& P = *Person::players[person];
    if (!fighting(P)) {
        return;
    }
    P.exAiCooldown -= stepTime();
    if (P.exAiCooldown > 0) {
        return;
    }
    Person* V = chased(P);
    if (!V) {
        return;
    }
    const float s2 = (P.scale * 5) * (P.scale * 5);
    const float d2 = distsq(&P.coords, &V->coords);
    const bool vStanding = Animation::animations[V->animTarget].height != lowheight && V->animTarget != getupfrombackanim &&
                           V->animTarget != getupfromfrontanim;
    // leg cannon: running at the player from a few steps away (a miss throws the kicker
    // down, as it does a player)
    if (P.creature == rabbittype && P.isRun() && P.wasRun() && d2 < 12 * s2 && d2 > 7 * s2 && vStanding &&
        flatDot(P, V->coords) > .8f && AIChance(1.2f * rate())) {
        P.victim = Person::players[P.exTarget];
        P.hasvictim = 1;
        P.setTargetAnimation(rabbitkickanim);
        P.exAiCooldown = 3;
        printf("LugaruEX: Expanded AI - person %d leg cannon at %d\n", person, P.exTarget);
        return;
    }
    // knife throw: a knife in hand, the player in sight but out of reach
    if (hasKnifeOut(P) && (P.isIdle() || P.isRun()) && d2 > 9 && d2 < 100 && flatDot(P, V->coords) > .7f && AIChance(.25f * rate()) &&
        canSee(P, *V)) {
        P.throwtogglekeydown = 1;
        P.victim = Person::players[P.exTarget];
        P.hasvictim = 1;
        P.setTargetAnimation(knifethrowanim);
        P.targetyaw = roughDirectionTo(P.coords, V->coords);
        P.targettilt2 = pitchTo(P.coords, V->coords);
        P.exAiCooldown = 4;
        printf("LugaruEX: Expanded AI - person %d throws a knife at %d\n", person, P.exTarget);
    }
}

bool ExpandedAIWallJump(const Person& p)
{
    if (!fighting(p) || p.creature != rabbittype) {
        return false;
    }
    const Person* V = chased(p);
    return V && dist2(p.coords, V->coords) < 100;
}

bool ExpandedAIWallKick(const Person& p)
{
    if (!fighting(p)) {
        return false;
    }
    const Person* V = chased(p);
    return V && V->coords.y < p.coords.y && dist2(p.coords, V->coords) < 16;
}

// ------------------------------------------------------------------ the AFK bot
struct BotState
{
    float press = 0;  // attack held this long so far (0 = released)
    float wait = 0;   // time until the next attack press
    float stuck = 0;  // pushing into something
    int lastFoe = -1; // (logged when it changes)
};
static std::vector<BotState> g_bot;

void BotInput(int person, PadInput& in, float& yaw)
{
    in = PadInput();
    if (!valid(person)) {
        return;
    }
    if ((int)g_bot.size() <= person) {
        g_bot.resize(person + 1);
    }
    BotState& st = g_bot[person];
    const float dt = stepTime();
    Person& me = *Person::players[person];
    const float s2 = (me.scale * 5) * (me.scale * 5);

    // an enemy to fight: an alert one nearby, or any one within a few steps
    int foe = -1;
    float foeD = 1e30f;
    for (int j = 0; j < (int)Person::players.size(); j++) {
        Person& e = *Person::players[j];
        if (j == person || e.isPlayerControlled() || e.dead || e.howactive >= typedead1) {
            continue;
        }
        const float d = distsq(&me.coords, &e.coords);
        if (d > (e.aitype == attacktypecutoff ? 400.f : 36.f) || d >= foeD) {
            continue;
        }
        foe = j;
        foeD = d;
    }
    // keep fighting the same enemy unless another one is clearly closer (no dithering)
    if (foe >= 0 && st.lastFoe >= 0 && st.lastFoe != foe && st.lastFoe < (int)Person::players.size()) {
        Person& o = *Person::players[st.lastFoe];
        const float d = distsq(&me.coords, &o.coords);
        if (!o.isPlayerControlled() && !o.dead && o.howactive < typedead1 && d < 400 && foeD > d * .6f) {
            foe = st.lastFoe;
            foeD = d;
        }
    }
    // else stay with the nearest player still playing
    int mate = -1;
    float mateD = 1e30f;
    if (foe < 0) {
        for (int j = 0; j < (int)Person::players.size(); j++) {
            Person& h = *Person::players[j];
            if (j == person || !h.isPlayerControlled() || h.dead || h.exAfk == 255) {
                continue;
            }
            const float d = distsq(&me.coords, &h.coords);
            if (d < mateD) {
                mate = j;
                mateD = d;
            }
        }
    }
    if (foe < 0 && mate < 0) {
        st.press = 0;
        return; // nobody around: stand still
    }
    if (foe != st.lastFoe) {
        if (foe >= 0) {
            printf("LugaruEX: the AI playing for AFK person %d goes for enemy %d\n", person, foe);
        }
        st.lastFoe = foe;
    }
    Person& goal = *Person::players[foe >= 0 ? foe : mate];
    yaw = 180 - roughDirectionTo(me.coords, goal.coords); // (players face along -camera yaw + 180)
    const float d2 = foe >= 0 ? foeD : mateD;

    if (foe < 0) {
        in.fwd = d2 > 16;
        in.walk = d2 < 49;
        st.press = 0;
    } else {
        Person& e = goal;
        const bool expanded = ExpandedAI();
        // close in; stand to strike (punches and slashes need no forward key)
        in.fwd = d2 > 2.2f * s2;
        // draw a weapon carried on the belt / back
        if (me.num_weapons > 0 && me.weaponactive == -1 && d2 < 100 && AIChance(1.f)) {
            in.draw = true;
        }
        // an enemy swinging at me: press attack to counter it (a player's reversal)
        const bool threatened = Animation::animations[e.animTarget].attack == normalattack && e.victim.get() == &me && d2 < 9;
        // attack in short presses (the game wants the button released between attacks)
        st.wait -= dt;
        bool want = false;
        if (threatened && AIChance(6.f)) {
            want = true;
        } else if (d2 < 4.5f * s2 && st.wait <= 0) {
            want = true;
        }
        if (want && st.press <= 0) {
            st.press = dt;
            st.wait = .35f + (rand() % 40) / 100.f;
        }
        if (st.press > 0) {
            in.attack = true;
            st.press += dt;
            if (st.press > .09f) {
                st.press = 0;
                in.attack = false;
            }
        }
        if (expanded) {
            // leg cannon: running at it from a few steps away (attack + jump while running)
            if (me.isRun() && d2 > 7 * s2 && d2 < 12 * s2 && Animation::animations[e.animTarget].height != lowheight &&
                AIChance(1.5f * rate())) {
                in.attack = true;
                in.jump = true;
            }
            // knife throw at an enemy out of reach (the throw button aims at the one in front)
            if (hasKnifeOut(me) && d2 > 9 && d2 < 100 && AIChance(.3f * rate()) && canSee(me, e)) {
                in.thrw = true;
            }
        }
    }
    // blocked by something on the way: hop over it
    st.stuck = (in.fwd && me.collided > .8f) ? st.stuck + dt : 0;
    if (st.stuck > .4f) {
        in.jump = true;
        st.stuck = 0;
    }
}
}
