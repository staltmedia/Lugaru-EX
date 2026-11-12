/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "EX/ExInterp.hpp"

#include "Game.hpp"
#include "Objects/Person.hpp"
#include "Objects/Weapons.hpp"

#include <cmath>
#include <vector>

extern XYZ viewer;

namespace EX
{
struct PersonSnap
{
    const Person* who = nullptr;
    XYZ coords;
    float yaw = 0, tilt = 0, tilt2 = 0;
    int skelFree = 0;
    std::vector<XYZ> joints;
};

struct WeaponSnap
{
    int owner = -1;
    XYZ position, tippoint;
};

struct Snapshot
{
    bool valid = false;
    XYZ viewer;
    float camYaw = 0, camPitch = 0;
    std::vector<PersonSnap> people;
    std::vector<WeaponSnap> weapons;
};

static Snapshot prevS, curS, backupS;
static bool applied = false;
static bool invalidated = true;

static void capture(Snapshot& s)
{
    s.valid = true;
    s.viewer = viewer;
    s.camYaw = Game::yaw;
    s.camPitch = Game::pitch;
    s.people.resize(Person::players.size());
    for (size_t i = 0; i < Person::players.size(); i++) {
        const Person& p = *Person::players[i];
        PersonSnap& ps = s.people[i];
        ps.who = &p;
        ps.coords = p.coords;
        ps.yaw = p.yaw;
        ps.tilt = p.tilt;
        ps.tilt2 = p.tilt2;
        ps.skelFree = p.skeleton.free;
        ps.joints.resize(p.skeleton.joints.size());
        for (size_t j = 0; j < p.skeleton.joints.size(); j++) {
            ps.joints[j] = p.skeleton.joints[j].position;
        }
    }
    s.weapons.resize(weapons.size());
    for (size_t i = 0; i < weapons.size(); i++) {
        s.weapons[i].owner = weapons[i].owner;
        s.weapons[i].position = weapons[i].position;
        s.weapons[i].tippoint = weapons[i].tippoint;
    }
}

void InterpCapture()
{
    if (invalidated || !curS.valid) {
        capture(curS);
        prevS = curS;
        invalidated = false;
        return;
    }
    std::swap(prevS, curS);
    capture(curS);
}

void InterpInvalidate()
{
    invalidated = true;
}

static inline float d2(const XYZ& a, const XYZ& b)
{
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x * x + y * y + z * z;
}

static inline float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline XYZ lerpv(const XYZ& a, const XYZ& b, float t)
{
    XYZ r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.z = a.z + (b.z - a.z) * t;
    return r;
}

static inline float lerpAngle(float a, float b, float t)
{
    float d = b - a;
    while (d > 180) {
        d -= 360;
    }
    while (d < -180) {
        d += 360;
    }
    return a + d * t;
}

void InterpApply(float alpha, float pendingYaw, float pendingPitch)
{
    applied = false;
    if (!prevS.valid || !curS.valid || invalidated) {
        return;
    }
    if (alpha < 0) {
        alpha = 0;
    }
    if (alpha > 1) {
        alpha = 1;
    }
    capture(backupS);
    applied = true;

    // camera: position is blended; rotation uses the newest value plus mouse
    // movement that has not been simulated yet, so looking around is immediate.
    if (d2(prevS.viewer, curS.viewer) < 25 * 25) {
        viewer = lerpv(prevS.viewer, curS.viewer, alpha);
    }
    Game::yaw += pendingYaw;
    Game::pitch += pendingPitch;
    if (Game::pitch > 90) {
        Game::pitch = 90;
    }
    if (Game::pitch < -70) {
        Game::pitch = -70;
    }

    const size_t n = Person::players.size();
    if (prevS.people.size() == n && curS.people.size() == n) {
        for (size_t i = 0; i < n; i++) {
            Person& p = *Person::players[i];
            const PersonSnap& a = prevS.people[i];
            const PersonSnap& b = curS.people[i];
            if (a.who != &p || b.who != &p || a.skelFree != b.skelFree || a.joints.size() != b.joints.size() ||
                b.joints.size() != p.skeleton.joints.size() || d2(a.coords, b.coords) > 5 * 5) {
                continue; // discontinuity: draw the current state
            }
            p.coords = lerpv(a.coords, b.coords, alpha);
            p.yaw = lerpAngle(a.yaw, b.yaw, alpha);
            p.tilt = lerpAngle(a.tilt, b.tilt, alpha);
            p.tilt2 = lerpAngle(a.tilt2, b.tilt2, alpha);
            for (size_t j = 0; j < b.joints.size(); j++) {
                p.skeleton.joints[j].position = lerpv(a.joints[j], b.joints[j], alpha);
            }
        }
    }

    // free weapons (thrown / dropped). Held weapons are positioned by
    // Person::DrawSkeleton from the (already blended) skeleton.
    if (prevS.weapons.size() == weapons.size() && curS.weapons.size() == weapons.size()) {
        for (size_t i = 0; i < weapons.size(); i++) {
            const WeaponSnap& a = prevS.weapons[i];
            const WeaponSnap& b = curS.weapons[i];
            if (weapons[i].owner != -1 || a.owner != -1 || b.owner != -1) {
                continue;
            }
            if (d2(a.position, b.position) > 10 * 10) {
                continue;
            }
            weapons[i].position = lerpv(a.position, b.position, alpha);
            weapons[i].tippoint = lerpv(a.tippoint, b.tippoint, alpha);
        }
    }
}

void InterpRestore()
{
    if (!applied) {
        return;
    }
    applied = false;
    viewer = backupS.viewer;
    Game::yaw = backupS.camYaw;
    Game::pitch = backupS.camPitch;
    // yaw/pitch: put back the simulated values (pending mouse is applied by the sim later)
    const size_t n = Person::players.size();
    if (backupS.people.size() == n) {
        for (size_t i = 0; i < n; i++) {
            Person& p = *Person::players[i];
            const PersonSnap& s = backupS.people[i];
            if (s.who != &p || s.joints.size() != p.skeleton.joints.size()) {
                continue;
            }
            p.coords = s.coords;
            p.yaw = std::isfinite(s.yaw) ? s.yaw : 0;
            p.tilt = std::isfinite(s.tilt) ? s.tilt : 0;
            p.tilt2 = std::isfinite(s.tilt2) ? s.tilt2 : 0;
            for (size_t j = 0; j < s.joints.size(); j++) {
                p.skeleton.joints[j].position = s.joints[j];
            }
        }
    }
    if (backupS.weapons.size() == weapons.size()) {
        for (size_t i = 0; i < weapons.size(); i++) {
            if (weapons[i].owner != -1 || backupS.weapons[i].owner != -1) {
                continue;
            }
            weapons[i].position = backupS.weapons[i].position;
            weapons[i].tippoint = backupS.weapons[i].tippoint;
        }
    }
}
}
