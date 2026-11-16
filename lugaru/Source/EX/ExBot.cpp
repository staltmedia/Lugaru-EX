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
#include "Environment/Terrain.hpp"
#include "Level/Dialog.hpp"
#include "Math/Random.hpp"
#include "Tutorial.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

extern int difficulty;
extern int hostile;
extern bool winfreeze;
extern Terrain terrain;

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
    // also in Solo (offline and local split screen); never on a guest or in the tutorial
    return !NetIsGuest() && cfg.mpMode != "join" && !Tutorial::active && cfg.mpExpandedAI;
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

static int weaponOf(Person& p)
{
    if (!p.hasWeapon() || p.weaponactive < 0 || p.weaponactive > 1) {
        return 0;
    }
    const int w = p.weaponids[p.weaponactive];
    return w >= 0 && w < (int)weapons.size() ? weapons[w].getType() : 0;
}

// standing in a state a player could start an attack from (GameTick's attack block)
static bool ready(const Person& p)
{
    return !Animation::animations[p.animTarget].attack && p.staggerdelay <= 0 &&
           (p.isIdle() || p.isRun() || p.isCrouch() || p.animTarget == walkanim || p.animTarget == sneakanim);
}

// v's back is turned to p
static bool behind(const Person& p, const Person& v, float t)
{
    const float dx = v.coords.x - p.coords.x, dz = v.coords.z - p.coords.z;
    const float l = sqrtf(dx * dx + dz * dz), lf = sqrtf(v.facing.x * v.facing.x + v.facing.z * v.facing.z);
    return l > 1e-4f && lf > 1e-4f && (dx * v.facing.x + dz * v.facing.z) / (l * lf) > t;
}

// a player who could be grabbed from behind: on their feet, not fighting, not running
static bool sneakable(const Person& v)
{
    return !v.dead && !v.skeleton.free && Animation::animations[v.animTarget].attack == neutral &&
           (v.isIdle() || v.isCrouch() || v.isStop() || v.animTarget == walkanim || v.animTarget == sneakanim) &&
           v.animTarget != getupfrombackanim && v.animTarget != getupfromfrontanim;
}

static bool livingHumanWithin(const Person& p, float r2)
{
    for (auto& h : Person::players) {
        if (h.get() != &p && h->isPlayerControlled() && !h->dead && dist2(h->coords, p.coords) < r2) {
            return true;
        }
    }
    return false;
}

static void goTo(Person& p, const XYZ& where)
{
    p.targetyaw = roughDirectionTo(p.coords, where);
    p.lookyaw = p.targetyaw;
    p.forwardkeydown = 1;
    p.backkeydown = p.leftkeydown = p.rightkeydown = 0;
    p.attackkeydown = p.jumpkeydown = p.crouchkeydown = 0;
}

static void holdFacing(Person& p, const XYZ& where)
{
    p.targetyaw = roughDirectionTo(p.coords, where);
    p.lookyaw = p.targetyaw;
    p.forwardkeydown = p.backkeydown = p.leftkeydown = p.rightkeydown = 0;
    p.attackkeydown = p.jumpkeydown = p.crouchkeydown = 0;
}

static void clearDeadDecals()
{
    terrain.deleteDeadDecals();
    for (unsigned l = 0; l < Object::objects.size(); l++) {
        if (Object::objects[l]->model.type == decalstype) {
            Object::objects[l]->model.deleteDeadDecals();
        }
    }
}

// a move started the way GameTick's attack code starts it for a player
static void startMove(Person& P, int vi, int anim)
{
    Person& V = *Person::players[vi];
    P.victim = Person::players[vi];
    P.hasvictim = 1;
    P.animTarget = anim;
    P.frameTarget = 0;
    P.target = 0;
    XYZ targetpoint = V.coords;
    if (anim == crouchstabanim || anim == swordgroundstabanim || anim == staffgroundsmashanim) {
        targetpoint += (V.jointPos(abdomen) + V.jointPos(neck)) / 2 * V.scale;
    }
    P.targetyaw = roughDirectionTo(P.coords, targetpoint);
    P.targettilt2 = pitchTo(P.coords, targetpoint);
    if (anim == crouchstabanim || anim == swordgroundstabanim) {
        P.targetyaw += (float)(abs(Random() % 100) - 50) / 4; // the player's aim spread: some stabs miss
    }
    if (anim == staffgroundsmashanim) {
        P.targettilt2 += 10;
    }
    P.lastattack3 = P.lastattack2;
    P.lastattack2 = P.lastattack;
    P.lastattack = anim;
    if (anim == swordgroundstabanim) {
        P.targetyaw += 30;
    }
    if (anim == killanim || anim == dropkickanim) {
        clearDeadDecals();
        Person::players[vi]->exAiFlungUntil = Game::leveltime + 3; // (what it hits earns nobody a bonus)
    }
    P.exAiBody = vi;
}

static void startSneak(Person& P, int vi, int weapon)
{
    Person& V = *Person::players[vi];
    if (!weapon) {
        P.animCurrent = P.animTarget = sneakattackanim;
        V.animCurrent = V.animTarget = sneakattackedanim;
        P.oldcoords = P.coords;
        P.coords = V.coords;
    } else {
        P.animCurrent = P.animTarget = weapon == knife ? knifesneakattackanim : swordsneakattackanim;
        V.animCurrent = V.animTarget = weapon == knife ? knifesneakattackedanim : swordsneakattackedanim;
        V.oldcoords = V.coords;
        V.coords = P.coords;
    }
    P.victim = Person::players[vi];
    P.hasvictim = 1;
    V.targettilt2 = 0;
    V.frameTarget = 1;
    V.frameCurrent = 0;
    V.target = 0;
    V.velocity = 0;
    P.targettilt2 = V.targettilt2;
    P.frameCurrent = V.frameCurrent;
    P.frameTarget = V.frameTarget;
    P.target = V.target;
    P.velocity = 0;
    P.targetyaw = V.yaw;
    P.yaw = V.yaw;
    V.targetyaw = V.yaw;
    P.exAiBody = vi;
}

static void startKnifeFollow(Person& P, int vi)
{
    Person& V = *Person::players[vi];
    P.targetyaw = roughDirectionTo(P.coords, V.coords);
    P.targettilt2 = pitchTo(P.coords, V.coords);
    P.victim = Person::players[vi];
    P.hasvictim = 1;
    V.animTarget = V.animCurrent = knifefollowedanim;
    V.targettilt2 = P.targettilt2;
    V.frameTarget = 1;
    V.frameCurrent = 0;
    V.target = 0;
    V.velocity = 0;
    P.animCurrent = P.animTarget = knifefollowanim;
    P.targettilt2 = V.targettilt2;
    P.frameCurrent = V.frameCurrent;
    P.frameTarget = V.frameTarget;
    P.target = V.target;
    P.velocity = 0;
    P.oldcoords = P.coords;
    V.coords = P.coords;
    V.targetyaw = P.targetyaw;
    V.yaw = P.targetyaw;
    P.yaw = P.targetyaw;
    P.exAiBody = vi;
}

// a ground move on a body or a downed player in reach; false when out of reach
static bool tryGround(Person& P, int vi, int weapon, bool body)
{
    Person& V = *Person::players[vi];
    if (!ready(P) || !V.skeleton.free) {
        return false;
    }
    const float s2 = (P.scale * 5) * (P.scale * 5);
    const float d2 = distsq(&P.coords, &V.coords);
    int anim = -1;
    if (weapon == knife && d2 < 1.5f * s2) {
        anim = crouchstabanim;
    } else if (weapon == sword && d2 < 1.5f * s2) {
        anim = swordgroundstabanim;
    } else if (weapon == staff && d2 < 3.5f * s2) {
        anim = staffgroundsmashanim;
    }
    if (body && d2 < 3.5f && (anim < 0 || Random() % 3 == 0)) {
        // a body: kick it up and punch it (unarmed, the player's crouch move), or kick it around
        anim = !weapon && V.dead && V.skeleton.longdead > 1000 && d2 < 2.5f && Random() % 2 ? killanim : dropkickanim;
    } else if (!body && !weapon && d2 < 3.5f && V.skeleton.longdead < 300 && P.lastattack != spinkickanim) {
        anim = dropkickanim;
    }
    if (anim < 0) {
        return false;
    }
    startMove(P, vi, anim);
    static const char* const names[] = { "stabs", "stabs", "smashes", "kicks up and punches", "kicks" };
    const int n = anim == crouchstabanim ? 0 : anim == swordgroundstabanim ? 1 : anim == staffgroundsmashanim ? 2 : anim == killanim ? 3 : 4;
    printf("LugaruEX: Expanded AI - person %d %s %s %d\n", (int)P.id, names[n], body ? "the body of" : "downed player", vi);
    return true;
}

// a knife in this body can be pulled out the way it lies (GameTick's pick-up rule)
static bool knifeReachable(const Person& b)
{
    return (b.skeleton.forward.y < 0 && b.weaponstuckwhere == 0) || (b.skeleton.forward.y > 0 && b.weaponstuckwhere == 1) ||
           b.weaponstuck == -1 || b.num_weapons > 1;
}

// plans: 1 sneak up behind a player, 2 hit a downed player, 3 check / beat a body, 4 get a knife out of a body
enum { kSneak = 1, kDowned, kBody, kKnife };

void ExpandedAIStep(int person)
{
    if (!valid(person)) {
        return;
    }
    Person& P = *Person::players[person];
    if (!ExpandedAI() || !hostile || winfreeze || Dialog::inDialog() || P.isPlayerControlled() || P.dead || P.skeleton.free) {
        P.exAiPlan = 0;
        return;
    }
    const float dt = stepTime();
    if (P.exAiCruelty < 0) {
        // rolled once per enemy: mostly mild, now and then a vicious one (u^3: half below .13,
        // one in ten above .73)
        const float u = (float)(rand() % 1001) / 1000.f;
        P.exAiCruelty = u * u * u;
        if (const char* forced = getenv("LUGARUEX_TEST_CRUELTY")) {
            P.exAiCruelty = (float)atof(forced); // test hook: everyone this cruel
        }
    }
    P.exAiCooldown -= dt;
    P.exAiBodyWait -= dt;
    if (P.exAiPlan && (P.exAiPlanTime -= dt) <= 0) {
        P.exAiPlan = 0;
    }
    static const bool dbg = getenv("LUGARUEX_TEST_AIDEBUG") != nullptr;
    if (dbg) {
        static float tt[256] = {};
        float& t = tt[person & 255];
        if ((t += dt) > 1) {
            t = 0;
            printf("EXTEST ai %d: aitype=%d anim=%d ready=%d plan=%d bodyOf=%d left=%d wait=%.1f target=%d\n", person, P.aitype, P.animTarget, ready(P),
                   P.exAiPlan, P.exAiBodyOf, P.exAiBodyLeft, P.exAiBodyWait, P.exTarget);
        }
    }
    // fighting: every move; calm again (back on its rounds after a kill): only bodies
    const bool attacking = P.aitype == attacktypecutoff;
    if (attacking) {
        P.exFightTime += dt; // (how hard a fight it was, for what it does after)
        if (valid(P.exTarget) && Person::players[P.exTarget]->isPlayerControlled()) {
            P.exLastFoe = P.exTarget;
        }
    }
    const bool calm = P.aitype == passivetype || P.aitype == pathfindtype;
    if ((!attacking && !calm) || P.stunned > 0 || P.surprised > 0 || (!attacking && (P.exAiPlan == kSneak || P.exAiPlan == kDowned))) {
        P.exAiPlan = 0;
    }
    if ((!attacking && !calm) || P.stunned > 0 || P.surprised > 0) {
        return;
    }
    const float s2 = (P.scale * 5) * (P.scale * 5);
    const int weapon = weaponOf(P);

    // ---- carry on with a plan
    if (P.exAiPlan && valid(P.exAiPlanOn)) {
        Person& V = *Person::players[P.exAiPlanOn];
        const float d2 = distsq(&P.coords, &V.coords);
        if (P.exAiPlan == kSneak) {
            if (!sneakable(V) || !behind(P, V, .2f) || d2 > 30) {
                P.exAiPlan = 0;
            } else if (d2 < 1.5f * s2 && ready(P)) {
                startSneak(P, P.exAiPlanOn, weapon);
                printf("LugaruEX: Expanded AI - person %d sneak attack on %d\n", person, P.exAiPlanOn);
                P.exAiPlan = 0;
                return;
            } else if (ready(P)) {
                goTo(P, V.coords);
                return;
            }
        } else if (P.exAiPlan == kDowned) {
            if (V.dead || !V.skeleton.free) {
                P.exAiPlan = 0;
            } else {
                if (tryGround(P, P.exAiPlanOn, weapon, false)) {
                    P.exAiPlan = 0;
                } else if (ready(P)) {
                    goTo(P, V.coords);
                }
                return;
            }
        } else if (P.exAiPlan == kBody) {
            if (!V.dead || livingHumanWithin(P, 100)) {
                P.exAiPlan = 0;
            } else {
                if (tryGround(P, P.exAiPlanOn, weapon, true)) {
                    P.exAiPlan = 0;
                    P.exAiBodyLeft--;
                    P.exAiBodyWait = (1.f - P.exAiCruelty) * 4.f + .8f + (float)(rand() % 150) / 100.f; // (the cruel ones barely stop)
                } else if (ready(P)) {
                    goTo(P, V.coords);
                }
                return;
            }
        } else if (P.exAiPlan == kKnife) {
            if (P.hasWeapon() || !V.num_weapons || !V.skeleton.free || livingHumanWithin(P, 16)) {
                if (P.num_weapons > 0) {
                    printf("LugaruEX: Expanded AI - person %d got the knife out of body %d\n", person, P.exAiPlanOn);
                }
                P.exAiPlan = 0;
            } else if (ready(P) || P.animTarget == crouchremoveknifeanim) {
                if (dbg) {
                    static float kt = 0;
                    if ((kt += dt) > .25f) {
                        kt = 0;
                        printf("EXTEST knife plan: reach=%d d2=%.2f fwdy=%.2f where=%d stuck=%d anim=%d toggle=%d\n", knifeReachable(V), d2, V.skeleton.forward.y, V.weaponstuckwhere, V.weaponstuck, P.animTarget, (int)P.throwtogglekeydown);
                    }
                }
                if (knifeReachable(V)) {
                    if (d2 < 1.8f) { // GameTick's pick-up block pulls it out
                        holdFacing(P, V.coords);
                        P.throwkeydown = 1;
                        if (P.animTarget != crouchremoveknifeanim && P.animTarget != removeknifeanim) {
                            P.throwtogglekeydown = 0;
                        }
                    } else {
                        goTo(P, V.coords);
                    }
                } else if (d2 < 2.3f && ready(P)) {
                    // lying on the knife: kick the body up and over first (the kick-up move spins it)
                    startMove(P, P.exAiPlanOn, killanim);
                    printf("LugaruEX: Expanded AI - person %d kicks body %d over to get its knife\n", person, P.exAiPlanOn);
                } else {
                    goTo(P, V.coords);
                }
                return;
            }
        }
    }
    if (P.exAiPlan || !ready(P)) {
        return;
    }

    // ---- the player it chases
    const int vi = P.exTarget;
    Person* V = attacking && valid(vi) && Person::players[vi].get() != &P && Person::players[vi]->isPlayerControlled() ? Person::players[vi].get() : nullptr;
    if (V && !V->dead && !V->skeleton.free) {
        const float d2 = distsq(&P.coords, &V->coords);
        const bool vStanding = Animation::animations[V->animTarget].height != lowheight && V->animTarget != getupfrombackanim &&
                               V->animTarget != getupfromfrontanim;
        // sneak attack: the player's back is turned (a rabbit; a staff has no sneak attack)
        if (P.creature == rabbittype && weapon != staff && d2 < 25 && sneakable(*V) && behind(P, *V, .5f) && AIChance(.35f * rate())) {
            P.exAiPlan = kSneak;
            P.exAiPlanOn = vi;
            P.exAiPlanTime = 4;
            printf("LugaruEX: Expanded AI - person %d sneaks up on %d\n", person, vi);
            return;
        }
        if (P.exAiCooldown <= 0) {
            // leg cannon: running at the player from a few steps away (a miss throws the kicker
            // down, as it does a player)
            if (P.creature == rabbittype && P.isRun() && P.wasRun() && d2 < 12 * s2 && d2 > 7 * s2 && vStanding &&
                flatDot(P, V->coords) > .8f && AIChance(1.2f * rate())) {
                P.victim = Person::players[vi];
                P.hasvictim = 1;
                P.setTargetAnimation(rabbitkickanim);
                P.exAiFlungUntil = Game::leveltime + 3; // (a miss that knocks it out is no Heavy Impact for anyone)
                P.exAiCooldown = 3;
                printf("LugaruEX: Expanded AI - person %d leg cannon at %d\n", person, vi);
                return;
            }
            // knife throw: a knife in hand, the player in sight but out of reach
            if (hasKnifeOut(P) && (P.isIdle() || P.isRun()) && d2 > 9 && d2 < 100 && flatDot(P, V->coords) > .7f && AIChance(.25f * rate()) &&
                canSee(P, *V)) {
                P.throwtogglekeydown = 1;
                P.victim = Person::players[vi];
                P.hasvictim = 1;
                P.setTargetAnimation(knifethrowanim);
                P.targetyaw = roughDirectionTo(P.coords, V->coords);
                P.targettilt2 = pitchTo(P.coords, V->coords);
                P.exAiCooldown = 4;
                printf("LugaruEX: Expanded AI - person %d throws a knife at %d\n", person, vi);
                return;
            }
            // knife follow-up: a staggering, badly bleeding player
            if (weapon == knife && V->staggerdelay > 0 && V->bloodloss > V->damagetolerance / 2 && d2 < 2.5f * s2 && AIChance(1.5f * rate())) {
                startKnifeFollow(P, vi);
                P.exAiCooldown = 2;
                printf("LugaruEX: Expanded AI - person %d knife follow-up on %d\n", person, vi);
                return;
            }
            // wind-up punch, the player's close unarmed move
            if (P.creature == rabbittype && !weapon && d2 < 1.5f * s2 && vStanding && flatDot(P, V->coords) > .5f && AIChance(.6f * rate())) {
                startMove(P, vi, winduppunchanim);
                P.exAiBody = -1;
                P.exAiCooldown = 1.5f;
                printf("LugaruEX: Expanded AI - person %d wind-up punch at %d\n", person, vi);
                return;
            }
        }
    }
    // a player knocked down near it: go hit them on the ground (stab, smash, drop kick)
    if (P.creature != rabbittype) { // (wolves keep their own moves)
        return;
    }
    if (V && !V->dead && V->skeleton.free && distsq(&P.coords, &V->coords) < 36 && AIChance(1.2f * rate())) {
        P.exAiPlan = kDowned;
        P.exAiPlanOn = vi;
        P.exAiPlanTime = 2.5f;
        printf("LugaruEX: Expanded AI - person %d goes for downed player %d\n", person, vi);
        return;
    }

    // ---- bodies, when no living player is close
    if (livingHumanWithin(P, 100)) {
        return;
    }
    int body = -1, knifeBody = -1;
    float bodyD = 225, knifeD = 100;
    for (unsigned j = 0; j < Person::players.size(); j++) {
        Person& B = *Person::players[j];
        if ((int)j == person || !B.skeleton.free) {
            continue;
        }
        const float d = dist2(B.coords, P.coords);
        if (B.dead && B.isPlayerControlled() && d < bodyD) {
            body = j;
            bodyD = d;
        }
        if (dbg && B.dead && B.num_weapons > 0 && B.weaponstuck == -1 && getenv("LUGARUEX_TEST_AIDEBUG")[0] == 's') {
            // test: the body lies on its knife
            B.weaponstuck = 0;
            B.weaponstuckwhere = B.skeleton.forward.y < 0 ? 1 : 0;
            printf("EXTEST body %d now lies on its knife\n", j);
        }
        if (B.num_weapons > 0 && B.dead && d < knifeD) { // anyone's body
            knifeBody = j;
            knifeD = d;
        }
    }
    // a knife stuck in a body: an unarmed rabbit sometimes goes to get it (kicking the body
    // over first when it lies on it)
    if (knifeBody >= 0 && !P.hasWeapon() && P.num_weapons == 0 && P.creature == rabbittype && AIChance(.15f * rate())) {
        P.exAiPlan = kKnife;
        P.exAiPlanOn = knifeBody;
        P.exAiPlanTime = 7;
        printf("LugaruEX: Expanded AI - person %d goes to get the knife in body %d\n", person, knifeBody);
        return;
    }
    // a player's body: now and then the enemy who fought that player and came out on top checks
    // it, a few times - rarely after an easy win, likelier the harder the fight was (it got
    // hurt, friends of its lie dead round the body, it went on a long time). Each enemy has its
    // own cruelty (most never touch a body); difficulty adds to it.
    if (body >= 0) {
        if (P.exAiBodyOf != body) {
            P.exAiBodyOf = body;
            const bool fought = P.exLastFoe == body; // (bystanders leave it be)
            const float hurt = std::min(1.f, std::max(P.damage, P.permanentdamage) / std::max(1.f, P.damagetolerance));
            int fallen = 0;
            for (unsigned j = 0; j < Person::players.size(); j++) {
                const Person& f = *Person::players[j];
                if ((int)j != person && !f.isPlayerControlled() && f.dead && dist2(f.coords, Person::players[body]->coords) < 20 * 20) {
                    fallen++;
                }
            }
            const float hard = std::min(1.f, .5f * hurt + .25f * std::min(1.f, fallen / 3.f) + .25f * std::min(1.f, P.exFightTime / 30.f));
            const float diff = difficulty <= 0 ? .6f : difficulty == 1 ? .8f : 1.f;
            const float chance = fought ? std::min(1.f, P.exAiCruelty * (.15f + 1.1f * hard)) * diff : 0.f;
            const int most = 1 + (int)(P.exAiCruelty * P.exAiCruelty * (5 + std::max(0, difficulty)) * (.5f + hard)); // (a vicious one keeps at it)
            P.exAiBodyLeft = (float)(rand() % 1000) < chance * 1000 ? 1 + rand() % most : 0;
            P.exAiBodyWait = 1.f + (float)(rand() % 400) / 100.f;
            printf("LugaruEX: Expanded AI - person %d by body %d: fought %d, cruelty %.2f, fight %.2f (hurt %.2f, %d fallen, %.0f s) -> %d checks\n",
                   person, body, (int)fought, P.exAiCruelty, hard, hurt, fallen, P.exFightTime, P.exAiBodyLeft);
        }
        if (P.exAiBodyLeft > 0 && P.exAiBodyWait <= 0) { // (several may check the same body)
            P.exAiPlan = kBody;
            P.exAiPlanOn = body;
            P.exAiPlanTime = 6;
        }
    }
}

bool ExpandedAIKeepsVictim(Person& p)
{
    const int a = p.animTarget;
    if (valid(p.exAiBody) && Animation::animations[a].attack != neutral &&
        (a == crouchstabanim || a == swordgroundstabanim || a == staffgroundsmashanim || a == dropkickanim || a == killanim ||
         a == sneakattackanim || a == knifesneakattackanim || a == swordsneakattackanim || a == knifefollowanim)) {
        p.victim = Person::players[p.exAiBody];
        return true;
    }
    p.exAiBody = -1;
    return false;
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
