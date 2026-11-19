/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Extras: Endless and PvP on levels made from scratch - ground, builds of crates, trees,
// rocks, camps, enemies with patrols, weapons. The level is written as a normal map file
// (JSON) with its ground heights and mode inside, then loaded like any other level.

#include "EX/ExGen.hpp"

#include "Audio/Sounds.hpp"
#include "EX/ExConfig.hpp"
#include "EX/ExLocal.hpp"
#include "EX/ExMods.hpp"
#include "EX/ExNet.hpp"
#include "EX/ExResource.hpp"
#include "Environment/Terrain.hpp"
#include "Environment/Lights.hpp"
#include "Graphic/Sprite.hpp"
#include "EX/ExInterp.hpp"
#include "Game.hpp"
#include "Level/Awards.hpp"
#include "Menu/Menu.hpp"
#include "Objects/Object.hpp"
#include "Objects/Person.hpp"
#include "Objects/PersonType.hpp"
#include "Objects/Weapons.hpp"

#include <json/writer.h>
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <set>
#include <vector>

#include "fsutil.h"
#include "outfits.h"

extern Terrain terrain;
extern Light light;
extern float texdetail;
extern bool skyboxtexture;
extern int numenvsounds;
extern XYZ viewer, cameraloc;

namespace EX
{
// ------------------------------------------------------------------ the loaded level
static int g_mode = GEN_NONE, g_wave = 1, g_env = grassyenvironment;
static uint32_t g_seed = 0;
static std::string g_name; // its map name (restarts load it again)
static std::set<const Person*> g_seen; // players already given their start spot and weapon
static int g_seenOrder = 0;
static bool g_looseDone = false;
static int g_maxHumans = 0; // PvP: the most players the round had
static float g_ringR = 45;  // PvP: how far from the middle the players start (this round)
static int g_weaponRule = 0; // PvP: 0 as usual, 1 none, 2 knives, 3 staves, 4 swords, 5 a pile lying about
// Endless: where the window lies on the endless land (its first cell), the parts the players came near
static int g_ox = 0, g_oz = 0;
static std::set<std::pair<int, int>> g_visited;
static float g_nextLook = 0;
static void makePaths();
static float g_lastAlive = 0; // the level time someone was last standing (the run's time)

int GenMode()
{
    return g_mode;
}

bool Versus()
{
    return g_mode == GEN_PVP;
}

bool Partners(const Person& a, const Person& b)
{
    return &a != &b && a.isPlayerControlled() && b.isPlayerControlled() && !Versus();
}

int GenWave()
{
    return g_wave;
}

// ------------------------------------------------------------------ randomness
struct Rng
{
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}
    uint32_t next()
    {
        s += 0x9E3779B97F4A7C15ull; // splitmix64
        uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return (uint32_t)((z ^ (z >> 31)) >> 32);
    }
    float f() { return (next() >> 8) / 16777216.f; }             // 0..1
    float r(float a, float b) { return a + (b - a) * f(); }
    int i(int n) { return n <= 0 ? 0 : (int)(next() % (uint32_t)n); } // 0..n-1
    bool chance(float p) { return f() < p; }
};

static float hash2(int x, int y, uint32_t seed)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xffffff) / 16777215.f;
}

static float smooth(float t)
{
    return t * t * (3 - 2 * t);
}

static float vnoise(float x, float y, uint32_t seed)
{
    const int xi = (int)floorf(x), yi = (int)floorf(y);
    const float fx = smooth(x - xi), fy = smooth(y - yi);
    const float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed);
    const float c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

static float fbm(float x, float y, int oct, uint32_t seed)
{
    float sum = 0, amp = 1, norm = 0;
    for (int o = 0; o < oct; o++) {
        sum += vnoise(x, y, seed + o * 101) * amp;
        norm += amp;
        amp *= .5f;
        x *= 2.03f;
        y *= 2.03f;
    }
    return sum / norm;
}

static float smoothstep(float a, float b, float x)
{
    const float t = std::max(0.f, std::min(1.f, (x - a) / (b - a)));
    return smooth(t);
}

// ------------------------------------------------------------------ making a level
static const int kN = 256;         // ground grid (like the game's own heightmap)
static const float kCell = 6.f;    // world units per grid cell (terrain.scale)
static const float kArena = 20.f;  // where the fight is, in cells from the middle (120 units)
static const float kPx = .6f;      // world height of one heightmap step (/5 * .5 * scale)

// Endless: the land's height (heightmap steps) at an absolute grid cell - the same wherever
// the window onto it is, on every PC (only arithmetic that comes out identical everywhere:
// no sin / cos / pow). Plains and hills come and go over long distances.
static float heightAbs(int ax, int az, int env, uint32_t seed)
{
    const float x = (float)ax, z = (float)az;
    const float n1 = fbm(x / 38.f, z / 38.f, 4, seed), n2 = fbm(x / 11.f + 50, z / 11.f + 50, 3, seed + 7);
    const float n3 = fbm(x / 160.f + 9, z / 160.f + 9, 2, seed + 13); // (the lie of the land, far apart)
    const float hills = .35f + 1.1f * smoothstep(.3f, .75f, n3);
    float v;
    if (env == desertenvironment) {
        float u = (x * .8f + z * .6f) / 6.5f + n1 * 6;
        u -= floorf(u);
        const float tri = 1 - fabsf(2 * u - 1); // (dune ridges)
        v = 66 + tri * tri * 14 * hills + (n1 - .5f) * 26 * hills + (n2 - .5f) * 3;
    } else if (env == snowyenvironment) {
        const float ridge = 1 - fabsf(2 * n2 - 1);
        v = 72 + (n1 - .5f) * 50 * hills + ridge * 9 * hills;
    } else {
        v = 70 + (n1 - .5f) * 44 * hills + (n2 - .5f) * 5;
    }
    v += (n3 - .5f) * 60;
    return std::max(2.f, std::min(250.f, v));
}

// the window's heightmap image bytes (origin = its first cell, absolute)
static std::vector<uint8_t> windowBytes(int env, uint32_t seed, int ox, int oz)
{
    std::vector<uint8_t> b((size_t)kN * kN);
    for (int x = 0; x < kN; x++) {
        for (int z = 0; z < kN; z++) {
            b[(size_t)(kN - 1 - x) + (size_t)z * kN] = (uint8_t)lroundf(heightAbs(ox + x, oz + z, env, seed));
        }
    }
    return b;
}

static std::string base64(const std::vector<uint8_t>& b)
{
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((b.size() + 2) / 3 * 4);
    for (size_t i = 0; i < b.size(); i += 3) {
        const uint32_t v = (uint32_t)b[i] << 16 | (i + 1 < b.size() ? b[i + 1] << 8 : 0) | (i + 2 < b.size() ? b[i + 2] : 0);
        out += t[v >> 18 & 63];
        out += t[v >> 12 & 63];
        out += i + 1 < b.size() ? t[v >> 6 & 63] : '=';
        out += i + 2 < b.size() ? t[v & 63] : '=';
    }
    return out;
}

static const int kChunk = 16;                  // Endless: the land comes in parts of 16 x 16 cells (96 units)
static const float kChunkW = kChunk * kCell;
static const int kWinChunks = kN / kChunk;     // the window holds 16 x 16 of them
static const int kThingsFrom = 4, kThingsTo = 13; // things stand only in the middle 9 x 9 (beyond, nobody sees them)

// the measured box (a box at scale 1); measured once
static float g_boxW = 0, g_boxH = 1, g_boxBottom = 0;

struct Gen
{
    int mode, env, wave;
    uint32_t seed;
    Rng rng;
    std::vector<float> h; // PvP arena: heightmap steps 0..255 as the image holds them, [x * kN + z]
    bool absMode = false;  // Endless: the endless land - heights from heightAbs, world = absolute coordinates
    uint32_t world = 0;    // the land's seed (the generator's own rng may be a part's)
    struct Item
    {
        int type;
        float x, y, z, yaw, pitch, scale; // (y over the ground)
    };
    std::vector<Item> items;
    struct Spot
    {
        float x, z, r;
    };
    std::vector<Spot> taken;
    Json::Value people{ Json::arrayValue }, paths{ Json::arrayValue };
    float cx = kN / 2 * kCell, cz = kN / 2 * kCell;
    // measured models (world size at scale 1)
    float boxW = 1, boxH = 1, boxBottom = 0;
    // PvP: this round's arena - its kind, how hilly the middle is, how far apart players start, the
    // weapons (0 as usual, 1 none, 2 knives, 3 staves, 4 swords, 5 a pile lying about), time of day
    int theme = -1, weaponRule = 0, sky = 0;
    float hills = 1, ringR = 45;

    Gen(int m, int e, int w, uint32_t s) : mode(m), env(e), wave(w), seed(s), rng(s), world(s) {}

    float height(int gx, int gz) const
    {
        if (absMode) {
            return heightAbs(gx, gz, env, world);
        }
        gx = std::max(0, std::min(kN - 1, gx));
        gz = std::max(0, std::min(kN - 1, gz));
        return h.empty() ? 0.f : h[(size_t)gx * kN + gz];
    }
    // ground at a world point (in heightmap steps, before the environment's own touches)
    float ground(float x, float z) const
    {
        const float gx = x / kCell, gz = z / kCell;
        const int ix = (int)floorf(gx), iz = (int)floorf(gz);
        const float fx = gx - ix, fz = gz - iz;
        return (height(ix, iz) * (1 - fx) + height(ix + 1, iz) * fx) * (1 - fz) + (height(ix, iz + 1) * (1 - fx) + height(ix + 1, iz + 1) * fx) * fz;
    }
    // how far the ground climbs within r of a point (world units)
    float rough(float x, float z, float r) const
    {
        const float g = ground(x, z);
        float m = 0;
        for (int a = 0; a < 8; a++) {
            static const float dir[8][2] = { { 1, 0 }, { .70710678f, .70710678f }, { 0, 1 }, { -.70710678f, .70710678f }, { -1, 0 }, { -.70710678f, -.70710678f }, { 0, -1 }, { .70710678f, -.70710678f } };
            m = std::max(m, fabsf(ground(x + dir[a][0] * r, z + dir[a][1] * r) - g) * kPx); // (no trig: every PC finds the same)
        }
        return m;
    }
    bool free(float x, float z, float r) const
    {
        if (absMode) { // (Endless: room where the players start)
            const float sx = x - kN / 2 * kCell, sz = z - kN / 2 * kCell;
            if (sx * sx + sz * sz < (16 + r) * (16 + r)) {
                return false;
            }
        }
        for (const Spot& s : taken) {
            const float dx = x - s.x, dz = z - s.z;
            if (dx * dx + dz * dz < (r + s.r) * (r + s.r)) {
                return false;
            }
        }
        return true;
    }
    void take(float x, float z, float r)
    {
        taken.push_back({ x, z, r });
    }
    // a random free spot in the arena (ring rMin..rMax from the middle, in world units)
    bool spot(float rMin, float rMax, float clear, float maxRough, float& x, float& z)
    {
        for (int tries = 0; tries < 200; tries++) {
            const float a = rng.r(0, 6.2831853f), d = sqrtf(rng.r(rMin * rMin, rMax * rMax));
            x = cx + cosf(a) * d;
            z = cz + sinf(a) * d;
            if (free(x, z, clear) && rough(x, z, clear) < maxRough) {
                return true;
            }
        }
        return false;
    }

    void makeGround()
    {
        h.assign((size_t)kN * kN, 0.f);
        const float c = kN / 2.f;
        const float dir = rng.r(0, 3.14159f), cs = cosf(dir), sn = sinf(dir);
        for (int x = 0; x < kN; x++) {
            for (int z = 0; z < kN; z++) {
                const float dx = x - c, dz = z - c, r = sqrtf(dx * dx + dz * dz);
                const float n1 = fbm(x / 38.f, z / 38.f, 4, seed), n2 = fbm(x / 11.f + 50, z / 11.f + 50, 3, seed + 7);
                const float amp = std::min(1.f, .3f * hills + .7f * smoothstep(kArena * .7f, kArena * 1.5f, r)); // gentle where the fight is (PvP hills: less so)
                float v;
                if (env == desertenvironment) {
                    const float dune = powf(sinf((x * cs + z * sn) / 6.5f + n1 * 6) * .5f + .5f, 2);
                    v = 55 + dune * 14 * amp + (n1 - .5f) * 26 * amp + (n2 - .5f) * 3;
                } else if (env == snowyenvironment) {
                    const float ridge = 1 - fabsf(2 * n2 - 1);
                    v = 62 + (n1 - .5f) * 50 * amp + ridge * 9 * amp;
                } else {
                    v = 60 + (n1 - .5f) * 44 * amp + (n2 - .5f) * 5;
                }
                v += smoothstep(kArena * 1.25f, kArena * 2.4f, r) * (60 + 60 * n1); // hills all round
                v += smoothstep(100, 126, r) * 40;                                   // and the map's edge
                h[(size_t)x * kN + z] = std::max(2.f, std::min(250.f, v));
            }
        }
    }

    // ------------------------------------------------------------ objects
    void object(int type, float x, float yRel, float z, float yaw, float pitch, float scale)
    {
        items.push_back({ type, x, yRel, z, yaw, pitch, scale });
    }
    void box(float x, float z, float level, float yaw, float s)
    {
        object(boxtype, x, -boxBottom * s - .15f + level * boxH * s, z, yaw, 0, s);
    }
    // along a wall / around a build: local (u, v) to world
    static void local(float bx, float bz, float yaw, float u, float v, float& x, float& z)
    {
        const float a = yaw * 3.14159265f / 180.f;
        x = bx + u * cosf(a) + v * sinf(a);
        z = bz - u * sinf(a) + v * cosf(a);
    }

    void cratePile(float bx, float bz)
    {
        const int n = 3 + rng.i(5);
        for (int k = 0; k < n; k++) {
            const float s = rng.r(.07f, .11f), yaw = rng.r(0, 90); // (a box at scale 1 is a 15-unit block)
            float x = bx + rng.r(-2.5f, 2.5f), z = bz + rng.r(-2.5f, 2.5f);
            box(x, z, 0, yaw, s);
            if (rng.chance(.35f)) {
                box(x, z, 1, yaw + rng.r(-10, 10), s);
            }
        }
        take(bx, bz, 6);
    }
    void wall(float bx, float bz)
    {
        const float yaw = rng.r(0, 180), s = .16f, w = boxW * s;
        const int len =  5 + rng.i(5);
        for (int k = 0; k < len; k++) {
            if (rng.chance(.18f) && k > 0 && k < len - 1) {
                continue; // a gap
            }
            const int high = (k == 0 || k == len - 1) ? 1 : 1 + rng.i(2);
            float x, z;
            local(bx, bz, yaw, (k - (len - 1) / 2.f) * w, 0, x, z);
            for (int l = 0; l < high; l++) {
                box(x, z, l, yaw, s);
            }
        }
        take(bx, bz, len * w / 2 + 2);
    }
    void hut(float bx, float bz)
    {
        const float yaw = rng.r(0, 90), s = .14f, w = boxW * s;
        const int side =  4 + rng.i(2), door = rng.i(4);
        const float half = (side - 1) * w / 2;
        for (int edge = 0; edge < 4; edge++) {
            for (int k = 0; k < side; k++) {
                if (edge == door && k == side / 2) {
                    continue;
                }
                float u = (k - (side - 1) / 2.f) * w, v = half;
                float lu = edge == 0 ? u : edge == 1 ? v : edge == 2 ? -u : -v;
                float lv = edge == 0 ? v : edge == 1 ? -u : edge == 2 ? -v : u;
                if (k == side - 1 && edge != door) {
                    continue; // (the corner belongs to the next side)
                }
                float x, z;
                local(bx, bz, yaw, lu, lv, x, z);
                box(x, z, 0, yaw, s);
                box(x, z, 1, yaw, s);
            }
        }
        take(bx, bz, half + w + 2);
    }
    void tower(float bx, float bz)
    {
        const float yaw = rng.r(0, 90), s = .22f, w = boxW * s;
        const int high =  3 + rng.i(2);
        for (int a = 0; a < 2; a++) {
            for (int b = 0; b < 2; b++) {
                float x, z;
                local(bx, bz, yaw, (a - .5f) * w, (b - .5f) * w, x, z);
                for (int l = 0; l < high - (a + b == 2 ? rng.i(2) : 0); l++) {
                    box(x, z, l, yaw, s);
                }
            }
        }
        take(bx, bz, w + 3);
    }
    // a blockhouse: the big boxes Lugaru's own levels build with
    void blockhouse(float bx, float bz)
    {
        const float yaw = rng.r(0, 90), s = rng.r(.3f, .42f);
        box(bx, bz, 0, yaw, s);
        float r = boxW * s * .75f;
        if (rng.chance(.5f)) {
            const float s2 = s * rng.r(.4f, .6f);
            float x, z;
            local(bx, bz, yaw, boxW * (s + s2) / 2, rng.r(-1, 1) * boxW * (s - s2) / 2, x, z);
            box(x, z, 0, yaw, s2);
            r += boxW * s2;
        }
        take(bx, bz, r + 2);
    }
    void stoneCircle(float bx, float bz)
    {
        const int n = 6 + rng.i(4);
        const float rad = rng.r(7, 11);
        for (int k = 0; k < n; k++) {
            const float a = k * 6.2831853f / n + rng.r(-.15f, .15f);
            const float s = rng.r(1.f, 1.7f);
            object(rocktype, bx + cosf(a) * rad, -s * .5f, bz + sinf(a) * rad, rng.r(0, 360), 0, s);
        }
        take(bx, bz, rad + 2);
    }
    // a camp: a fire ringed with stones, crates; its enemies sit round it
    void camp(float bx, float bz, std::vector<Spot>& camps)
    {
        object(firetype, bx, 0, bz, 0, 0, .25f);
        for (int k = 0; k < 7; k++) {
            const float a = k * .8976f;
            object(rocktype, bx + cosf(a) * 1.4f, -.1f, bz + sinf(a) * 1.4f, rng.r(0, 360), 0, .25f);
        }
        const int crates = 1 + rng.i(3);
        for (int k = 0; k < crates; k++) {
            const float a = rng.r(0, 6.283f);
            box(bx + cosf(a) * 5, bz + sinf(a) * 5, 0, rng.r(0, 90), rng.r(.08f, .12f));
        }
        take(bx, bz, 7);
        camps.push_back({ bx, bz, 7 });
    }

    void tree(float x, float z)
    {
        const float s = rng.r(.95f, 1.35f);
        object(treetrunktype, x, -.4f, z, rng.r(0, 360), 0, s);
        object(treeleavestype, x, -.4f, z, rng.r(-180, 180), 0, s); // (the trunk's scale: maps ignore it, a live one needs it)
        take(x, z, 2.5f);
    }

    // ------------------------------------------------------------ people
    static Json::Value v3(float x, float y, float z)
    {
        Json::Value a(Json::arrayValue);
        a.append(x);
        a.append(y);
        a.append(z);
        return a;
    }
    // a start weapon: mostly none; the stronger, the rarer
    int weapon()
    {
        const int r = rng.i(100);
        return r < 58 ? 0 : r < 80 ? knife : r < 92 ? staff : sword;
    }
    void look(Json::Value& p, int& creature)
    {
        const int nSkins = (int)PersonType::types[creature].skins.size();
        p["whichskin"] = nSkins > 0 ? rng.i(nSkins) : 0;
        p["clothes"] = Json::Value(Json::arrayValue);
        if (creature == rabbittype && rng.chance(.85f)) {
            const auto& list = Outfits();
            const OutfitDef& o = list[1 + rng.i((int)list.size() - 1)];
            for (const OutfitPiece& pc : o.pieces) {
                Json::Value c;
                c["path"] = pc.tex;
                c["tintr"] = pc.r;
                c["tintg"] = pc.g;
                c["tintb"] = pc.b;
                p["clothes"].append(c);
            }
        }
    }
    Json::Value person(int creature, float x, float z, float yaw, bool player)
    {
        Json::Value p;
        p["creature"] = creature;
        look(p, creature);
        p["coords"] = v3(x, 0, z); // (over the ground: GenGroundPeople)
        p["yaw"] = yaw;
        p["power"] = 1.0;
        p["speedmult"] = 1.0;
        if (player) {
            p["targetyaw"] = yaw;
        } else {
            p["scale"] = creature == wolftype ? .23 : rng.r(.19f, .215f);
            for (int k = 0; k < 4; k++) {
                p["proportions"][k] = rng.r(.97f, 1.03f);
            }
            p["howactive"] = typeactive;
            p["immobile"] = false;
            p["waypoint"] = 0;
            const int w = creature == wolftype ? 0 : weapon();
            p["weapons"] = Json::Value(Json::arrayValue);
            if (w) {
                p["weapons"].append(w);
            }
        }
        for (const char* part : { "head", "high", "low" }) {
            p["armor"][part] = 1.0;
            p["protection"][part] = 1.0;
            p["metal"][part] = false;
        }
        return p;
    }
    // a walk round the place it guards
    void patrol(Json::Value& p, float hx, float hz)
    {
        const int n = 2 + rng.i(4);
        p["waypoints"] = Json::Value(Json::arrayValue);
        for (int k = 0; k < n; k++) {
            float x = hx, z = hz;
            for (int t = 0; t < 30; t++) {
                const float a = rng.r(0, 6.283f), d = rng.r(8, 28);
                x = hx + cosf(a) * d;
                z = hz + sinf(a) * d;
                const float dx = x - cx, dz = z - cz;
                if (dx * dx + dz * dz < (kArena * kCell * .95f) * (kArena * kCell * .95f) && freeOfBuilds(x, z)) {
                    break;
                }
            }
            Json::Value wp;
            wp["type"] = rng.chance(.25f) ? wppause : wpkeepwalking;
            wp["pos"] = v3(x, 0, z);
            p["waypoints"].append(wp);
        }
    }
    std::vector<Spot> builds;
    bool freeOfBuilds(float x, float z) const
    {
        for (const Spot& s : builds) {
            const float dx = x - s.x, dz = z - s.z;
            if (dx * dx + dz * dz < (s.r + 1) * (s.r + 1)) {
                return false;
            }
        }
        return true;
    }
    bool lineClear(float x0, float z0, float x1, float z1) const
    {
        for (const Spot& s : builds) {
            const float dx = x1 - x0, dz = z1 - z0, l2 = dx * dx + dz * dz;
            float t = l2 > 0 ? ((s.x - x0) * dx + (s.z - z0) * dz) / l2 : 0;
            t = std::max(0.f, std::min(1.f, t));
            const float px = x0 + dx * t - s.x, pz = z0 + dz * t - s.z;
            if (px * px + pz * pz < (s.r + 1.5f) * (s.r + 1.5f)) {
                return false;
            }
        }
        return true;
    }

    // ------------------------------------------------------------ the whole level
    Json::Value make()
    {
        measure();
        // PvP: every round its own kind of arena
        static const char* kThemes[] = { "open field", "forest", "fort", "boulders", "camp", "hills" };
        float treeMul = 1, rockMul = 1, bigRocks = .25f, bushMul = 1;
        int nCamps = mode == GEN_PVP ? rng.i(2) : 1 + rng.i(2);
        int nBuilds = 3 + rng.i(4);
        if (mode == GEN_PVP) {
            const int weights[6] = { 20, env == desertenvironment ? 4 : 20, 18, 16, 12, 14 };
            int r = rng.i(20 + weights[1] + 18 + 16 + 12 + 14);
            theme = 0;
            while (theme < 5 && r >= weights[theme]) {
                r -= weights[theme++];
            }
            ringR = rng.r(30, 58); // (how far apart the players start)
            switch (theme) {
                case 0: nBuilds = 1 + rng.i(3), nCamps = 0, treeMul = .35f, bushMul = .6f; break;
                case 1: nBuilds = rng.i(3), nCamps = rng.i(2), treeMul = 2.4f, bushMul = 1.8f, ringR = rng.r(28, 45); break;
                case 2: nBuilds = 8 + rng.i(5), nCamps = rng.i(2), treeMul = .5f, ringR = rng.r(30, 48); break;
                case 3: nBuilds = 1 + rng.i(3), nCamps = 0, rockMul = 2.6f, bigRocks = .55f; break;
                case 4: nBuilds = 2 + rng.i(3), nCamps = 2 + rng.i(2); break;
                default: hills = rng.r(2.f, 3.2f); break;
            }
            const int w = rng.i(100);
            weaponRule = w < 55 ? 0 : w < 67 ? 1 : w < 79 ? 2 : w < 87 ? 3 : w < 94 ? 4 : 5;
            const int s = rng.i(100);
            sky = s < 45 ? 0 : s < 60 ? 1 : s < 75 ? 2 : s < 90 ? 3 : 4;
            static const char* kWeapons[] = { "as usual", "none", "knives", "staves", "swords", "a pile lying about" };
            static const char* kSkies[] = { "day", "dawn", "dusk", "overcast", "night" };
            printf("LugaruEX: PvP round - %s, players %.0f apart, weapons %s, %s\n", kThemes[theme], ringR * 2, kWeapons[weaponRule], kSkies[sky]);
        }
        makeGround();
        const float A = kArena * kCell;
        // where players start: one spot (Endless) or a ring facing the middle (PvP)
        float px = cx, pz = cz, pyaw = 0;
        std::vector<Spot> ring;
        if (mode == GEN_PVP) {
            const float R = ringR;
            for (int k = 0; k < 8; k++) {
                const float a = k * .785398f;
                ring.push_back({ cx + cosf(a) * R, cz + sinf(a) * R, 0 });
                take(ring.back().x, ring.back().z, 5);
            }
            px = ring[0].x;
            pz = ring[0].z;
        } else {
            const float a = rng.r(0, 6.283f);
            px = cx + cosf(a) * A * .55f;
            pz = cz + sinf(a) * A * .55f;
            take(px, pz, 9); // room for the others to appear next to player 1
        }
        XYZ from, to;
        from.x = px, from.y = 0, from.z = pz;
        to.x = cx, to.y = 0, to.z = cz;
        pyaw = roughDirectionTo(from, to);
        take(cx, cz, mode == GEN_PVP ? 6 : 0);

        // builds
        std::vector<Spot> camps;
        for (int k = 0; k < nCamps + nBuilds; k++) {
            float x, z;
            if (!spot(15, A * .85f, 9, 1.5f, x, z)) {
                continue;
            }
            const size_t before = taken.size();
            if (k < nCamps) {
                camp(x, z, camps);
            } else if (theme == 4 && k < nCamps + 2) {
                hut(x, z); // (a camp: huts round the fires)
            } else {
                // (a fort: walls, towers and blockhouses)
                const int kind = theme == 2 ? 1 + rng.i(4) : rng.i(env == snowyenvironment || env == grassyenvironment ? 6 : 5);
                if (kind == 0) {
                    cratePile(x, z);
                } else if (kind == 1) {
                    wall(x, z);
                } else if (kind == 2) {
                    hut(x, z);
                } else if (kind == 3) {
                    tower(x, z);
                } else if (kind == 4) {
                    blockhouse(x, z);
                } else {
                    stoneCircle(x, z);
                }
            }
            for (size_t t = before; t < taken.size(); t++) {
                builds.push_back(taken[t]);
            }
        }
        // trees in groves, bushes, rocks
        const int nTrees = (int)((env == desertenvironment ? 6 + rng.i(7) : env == snowyenvironment ? 16 + rng.i(10) : 24 + rng.i(16)) * treeMul);
        for (int k = 0, tries = 0; k < nTrees && tries < 600; tries++) {
            float x, z;
            if (!spot(10, A * 1.25f, 3, 2.5f, x, z)) {
                break;
            }
            if (fbm(x / 60.f, z / 60.f, 2, seed + 31) < .45f && rng.chance(.7f)) {
                continue; // (groves, not an even sprinkle)
            }
            tree(x, z);
            k++;
        }
        const int nBushes = (int)((env == desertenvironment ? 6 + rng.i(6) : 15 + rng.i(15)) * bushMul);
        for (int k = 0; k < nBushes; k++) {
            float x, z;
            if (spot(6, A * 1.2f, 1.2f, 3, x, z)) {
                object(bushtype, x, 0, z, rng.r(0, 360), 0, rng.r(.26f, .38f));
                take(x, z, 1);
            }
        }
        const int nRocks = (int)((14 + rng.i(14)) * rockMul);
        for (int k = 0; k < nRocks; k++) {
            float x, z;
            const bool big = rng.chance(bigRocks);
            if (spot(6, A * 1.3f, big ? 4 : 1.5f, 6, x, z)) {
                const float s = big ? rng.r(1.1f, 2.f) : rng.r(.3f, .9f);
                object(rocktype, x, -s * .45f, z, rng.r(0, 360), 0, s);
                take(x, z, s * 1.6f);
            }
        }

        // player 1 (others join next to them, or round the ring), then the enemies
        people.append(person(rng.chance(.3f) ? wolftype : rabbittype, px, pz, pyaw, true));
        const int nEnemies = 0; // (PvP: players only)
        int campSeat = 0;
        for (int k = 0; k < nEnemies; k++) {
            float x, z;
            bool atCamp = false;
            if (!camps.empty() && k < (int)camps.size() * 2 && rng.chance(.7f)) {
                const Spot& c = camps[k % camps.size()];
                const float a = rng.r(0, 6.283f);
                x = c.x + cosf(a) * 3.2f;
                z = c.z + sinf(a) * 3.2f;
                atCamp = true;
            } else if (!spot(20, A * .9f, 3, 3, x, z)) {
                continue;
            }
            const float dx = x - px, dz = z - pz;
            if (mode != GEN_PVP && dx * dx + dz * dz < 40 * 40) {
                if (!spot(40, A * .95f, 3, 3, x, z)) { // (not on top of the players at the start)
                    continue;
                }
                atCamp = false;
            }
            const int creature = rng.chance(env == snowyenvironment ? .25f : .15f) ? wolftype : rabbittype;
            XYZ at, mid;
            at.x = x, at.y = 0, at.z = z;
            mid = at;
            if (atCamp) {
                mid.x = camps[k % camps.size()].x, mid.z = camps[k % camps.size()].z;
            } else {
                mid.x = px, mid.z = pz;
            }
            Json::Value e = person(creature, x, z, roughDirectionTo(at, mid), false);
            if (atCamp && creature == rabbittype && rng.chance(.6f)) {
                e["howactive"] = rng.chance(.7f) ? typesitting : typesleeping; // resting by the fire
                campSeat++;
            } else {
                patrol(e, x, z);
            }
            take(x, z, 1.5f);
            people.append(e);
        }

        // path points the enemies find their way with (a grid round the builds)
        const float step = A * .8f / 2;
        int index[5][5];
        for (int a = 0; a < 5; a++) {
            for (int b = 0; b < 5; b++) {
                index[a][b] = -1;
                const float x = cx + (a - 2) * step, z = cz + (b - 2) * step;
                if (!freeOfBuilds(x, z)) {
                    continue;
                }
                index[a][b] = (int)paths.size();
                Json::Value p;
                p["pos"] = v3(x, 0, z);
                p["connect"] = Json::Value(Json::arrayValue);
                paths.append(p);
            }
        }
        for (int a = 0; a < 5; a++) {
            for (int b = 0; b < 5; b++) {
                if (index[a][b] < 0) {
                    continue;
                }
                for (int da = -1; da <= 1; da++) {
                    for (int db = -1; db <= 1; db++) {
                        const int na = a + da, nb = b + db;
                        if ((da || db) && na >= 0 && nb >= 0 && na < 5 && nb < 5 && index[na][nb] >= 0 &&
                            lineClear(cx + (a - 2) * step, cz + (b - 2) * step, cx + (na - 2) * step, cz + (nb - 2) * step)) {
                            paths[index[a][b]]["connect"].append(index[na][nb]);
                        }
                    }
                }
            }
        }

        Json::Value map;
        map["type"] = mapkilleveryone;
        map["hostile"] = 1;
        map["viewdistance"] = 170.0;
        map["fadestart"] = .6;
        map["environment"] = env;
        map["skybox"]["texture"] = true;
        {
            // PvP: the time of day tints the sky and the sun (day = the game's own colours)
            static const float kSky[5][6] = {
                { 1, 1, 1, 1, 1, 1 },                 // day
                { 1, .82f, .7f, 1, .8f, .62f },       // dawn
                { .9f, .7f, .85f, .95f, .68f, .7f },  // dusk
                { .78f, .8f, .84f, .8f, .82f, .86f }, // overcast
                { .38f, .44f, .66f, .5f, .56f, .8f }, // night
            };
            const float* c = kSky[std::max(0, std::min(4, sky))];
            const char* keys[6] = { "r", "g", "b", "lightr", "lightg", "lightb" };
            for (int k = 0; k < 6; k++) {
                map["skybox"][keys[k]] = c[k] * (sky ? rng.r(.96f, 1.04f) : 1.f);
            }
            if (sky == 3) {
                map["viewdistance"] = rng.r(110.f, 140.f); // (overcast: closer fog)
                map["fadestart"] = .4;
            }
        }
        map["objects"] = itemsJson(items, 0, 0);
        map["hotspots"] = Json::Value(Json::arrayValue);
        map["dialogs"] = Json::Value(Json::arrayValue);
        map["players"] = people;
        map["pathpoints"] = paths;
        map["center"] = v3(cx, 0, cz);
        map["radius"] = A;
        Json::Value gen;
        gen["mode"] = mode;
        gen["wave"] = wave;
        gen["seed"] = (Json::UInt)seed;
        gen["size"] = kN;
        gen["heights"] = heightsBase64();
        if (mode == GEN_PVP) {
            gen["ring"] = ringR;
            gen["weapons"] = weaponRule;
            gen["theme"] = theme;
        }
        map["exGen"] = gen;
        Json::Value doc;
        doc["version"] = 13;
        doc["map"] = map;
        return doc;
    }

    // the heightmap image's bytes (the game reads column i, row j as heightmap[size-1-i][j])
    std::string heightsBase64() const
    {
        std::vector<uint8_t> b((size_t)kN * kN);
        for (int x = 0; x < kN; x++) {
            for (int z = 0; z < kN; z++) {
                b[(size_t)(kN - 1 - x) + (size_t)z * kN] = (uint8_t)lroundf(h[(size_t)x * kN + z]);
            }
        }
        return base64(b);
    }

    // the game's own box model, measured once (the open source and HD models may differ)
    void measure()
    {
        if (g_boxW <= 0) {
            XYZ zero;
            zero = 0;
            Object o(boxtype, zero, 0, 0, 1.f);
            float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
            for (int i = 0; i < o.model.vertexNum; i++) {
                const XYZ& v = o.model.vertex[i];
                lo[0] = std::min(lo[0], v.x), hi[0] = std::max(hi[0], v.x);
                lo[1] = std::min(lo[1], v.y), hi[1] = std::max(hi[1], v.y);
                lo[2] = std::min(lo[2], v.z), hi[2] = std::max(hi[2], v.z);
            }
            g_boxW = hi[0] > lo[0] ? hi[0] - lo[0] : 15.5f;
            g_boxH = hi[1] > lo[1] ? hi[1] - lo[1] : 15.f;
            g_boxBottom = hi[1] > lo[1] ? lo[1] : -7.5f;
            printf("LugaruEX: Extras: a box at scale 1 is %.2f wide, %.2f high\n", g_boxW, g_boxH);
        }
        boxW = g_boxW;
        boxH = g_boxH;
        boxBottom = g_boxBottom;
    }

    // items as map objects, positions in the window whose first cell is (ox, oz)
    static Json::Value itemsJson(const std::vector<Item>& list, int ox, int oz)
    {
        Json::Value out(Json::arrayValue);
        for (const Item& it : list) {
            Json::Value o(Json::arrayValue);
            o.append(it.type);
            o.append(it.yaw);
            o.append(it.pitch);
            o.append(it.scale);
            Json::Value p(Json::arrayValue);
            p.append(it.x - ox * kCell);
            p.append(it.y); // over the ground there (GenGroundObjects)
            p.append(it.z - oz * kCell);
            o.append(p);
            out.append(o);
        }
        return out;
    }

    // ------------------------------------------------------------ Endless: one part of the land
    // a random free spot inside a square (world units)
    bool spotRect(float x0, float z0, float x1, float z1, float clear, float maxRough, float& x, float& z)
    {
        for (int tries = 0; tries < 40; tries++) {
            x = rng.r(x0, x1);
            z = rng.r(z0, z1);
            if (free(x, z, clear) && rough(x, z, clear) < maxRough) {
                return true;
            }
        }
        return false;
    }
    // what stands in part (cx, cz) of the land - the same every time (its own rng), every
    // thing well inside its square (a part leaving the window takes all of its things along)
    void chunk(int cx, int cz, std::vector<Spot>& camps)
    {
        measure();
        const float x0 = cx * kChunkW, z0 = cz * kChunkW;
        const float forest = fbm(cx / 4.f + 3, cz / 4.f + 3, 2, world + 31);
        if (rng.chance(.07f)) {
            float x, z;
            if (spotRect(x0 + 22, z0 + 22, x0 + kChunkW - 22, z0 + kChunkW - 22, 8, 1.5f, x, z)) {
                camp(x, z, camps);
            }
        } else if (rng.chance(.18f)) {
            float x, z;
            if (spotRect(x0 + 24, z0 + 24, x0 + kChunkW - 24, z0 + kChunkW - 24, 9, 1.5f, x, z)) {
                const int kind = rng.i(env == snowyenvironment || env == grassyenvironment ? 6 : 5);
                if (kind == 0) {
                    cratePile(x, z);
                } else if (kind == 1) {
                    wall(x, z);
                } else if (kind == 2) {
                    hut(x, z);
                } else if (kind == 3) {
                    tower(x, z);
                } else if (kind == 4) {
                    blockhouse(x, z);
                } else {
                    stoneCircle(x, z);
                }
            }
        }
        const int trees = env == desertenvironment ? (rng.chance(.35f) ? 1 + rng.i(2) : 0)
                                                   : (int)(forest * forest * (env == snowyenvironment ? 9 : 13) + rng.f());
        for (int k = 0; k < trees; k++) {
            float x, z;
            if (spotRect(x0 + 3, z0 + 3, x0 + kChunkW - 3, z0 + kChunkW - 3, 3, 2.5f, x, z)) {
                tree(x, z);
            }
        }
        const int bushes = env == desertenvironment ? rng.i(2) : rng.i(4);
        for (int k = 0; k < bushes; k++) {
            float x, z;
            if (spotRect(x0 + 2, z0 + 2, x0 + kChunkW - 2, z0 + kChunkW - 2, 1.2f, 3, x, z)) {
                object(bushtype, x, 0, z, rng.r(0, 360), 0, rng.r(.26f, .38f));
                take(x, z, 1);
            }
        }
        const int rocks = 1 + rng.i(3);
        for (int k = 0; k < rocks; k++) {
            float x, z;
            const bool big = rng.chance(.2f);
            if (spotRect(x0 + 5, z0 + 5, x0 + kChunkW - 5, z0 + kChunkW - 5, big ? 4 : 1.5f, 6, x, z)) {
                const float s = big ? rng.r(1.1f, 2.f) : rng.r(.3f, .9f);
                object(rocktype, x, -s * .45f, z, rng.r(0, 360), 0, s);
                take(x, z, s * 1.6f);
            }
        }
    }

    // what sort of enemy: most are ordinary; some big, small, strong or young - size, strength,
    // speed and build go into the level file, so every PC sees the same
    void kind(Json::Value& e, int creature)
    {
        const int r = rng.i(100);
        float scale, power = 1, speed = 1, prop[4] = { 1, 1, 1, 1 };
        if (creature == wolftype) {
            if (r < 62) {
                scale = .23f;
            } else if (r < 82) {
                scale = .276f, power = 1.25f, speed = .95f; // a large wolf
            } else {
                scale = .184f, power = .7f, speed = 1.1f; // a young one
            }
        } else if (r < 55) {
            scale = rng.r(.19f, .215f);
        } else if (r < 67) {
            scale = rng.r(.235f, .255f), power = 1.15f, speed = .92f, prop[1] = 1.05f; // big
        } else if (r < 79) {
            scale = rng.r(.165f, .18f), power = .85f, speed = 1.1f; // small
        } else if (r < 92) {
            scale = rng.r(.205f, .22f), power = 1.35f, prop[1] = 1.06f, prop[2] = 1.12f; // strong
        } else {
            scale = rng.r(.135f, .15f), power = .6f, speed = 1.15f, prop[0] = 1.25f, prop[3] = .9f; // young
        }
        e["scale"] = scale;
        e["power"] = power;
        e["speedmult"] = speed;
        for (int k = 0; k < 4; k++) {
            e["proportions"][k] = prop[k] * rng.r(.98f, 1.02f);
        }
    }
    static const int kPool = 30, kPoolWolves = 8; // Endless: enemies in the game at most (they come and go)
    static float parkX(int k) { return 150 + (k % 6) * 4.f; }
    static float parkZ(int k) { return 150 + (k / 6) * 4.f; }
    static uint32_t partSeed(uint32_t world, int cx, int cz)
    {
        uint32_t h = world * 2654435761u ^ (uint32_t)cx * 374761393u ^ (uint32_t)cz * 668265263u;
        h = (h ^ (h >> 15)) * 2246822519u;
        return h ^ (h >> 13);
    }

    // ------------------------------------------------------------ Endless: the start
    Json::Value makeEndless()
    {
        measure();
        absMode = true;
        // the window's parts (the window starts at the land's cell 0, 0)
        for (int cx = kThingsFrom; cx < kThingsTo; cx++) {
            for (int cz = kThingsFrom; cz < kThingsTo; cz++) {
                std::vector<Spot> camps;
                Gen part(mode, env, 1, partSeed(world, cx, cz));
                part.absMode = true;
                part.world = world;
                part.chunk(cx, cz, camps);
                items.insert(items.end(), part.items.begin(), part.items.end());
            }
        }
        // player 1 in the middle (the others join next to them); enemies come as the land does
        // - a pool of them waits far off, lying still (GenTick brings them in)
        const float mx = kN / 2 * kCell, mz = kN / 2 * kCell;
        people.append(person(rng.chance(.3f) ? wolftype : rabbittype, mx, mz, rng.r(0, 360), true));
        for (int k = 0; k < kPool; k++) {
            const int creature = k < kPoolWolves ? wolftype : rabbittype;
            Json::Value e = person(creature, parkX(k), parkZ(k), 0, false);
            kind(e, creature);
            e["weapons"] = Json::Value(Json::arrayValue);
            e["howactive"] = typedead1;
            e["immobile"] = true;
            people.append(e);
        }
        Json::Value map;
        map["type"] = mapkilleveryone;
        map["hostile"] = 1;
        map["viewdistance"] = 170.0;
        map["fadestart"] = .6;
        map["environment"] = env;
        map["skybox"]["texture"] = true;
        for (const char* k : { "r", "g", "b", "lightr", "lightg", "lightb" }) {
            map["skybox"][k] = 1.0;
        }
        map["objects"] = itemsJson(items, 0, 0);
        map["hotspots"] = Json::Value(Json::arrayValue);
        map["dialogs"] = Json::Value(Json::arrayValue);
        map["players"] = people;
        map["pathpoints"] = Json::Value(Json::arrayValue); // (made round the players: GenGroundPeople)
        map["center"] = v3(mx, 0, mz);
        map["radius"] = 200.0;
        Json::Value gen;
        gen["mode"] = mode;
        gen["wave"] = 1;
        gen["seed"] = (Json::UInt)world;
        gen["size"] = kN;
        gen["heights"] = base64(windowBytes(env, world, 0, 0));
        map["exGen"] = gen;
        Json::Value doc;
        doc["version"] = 13;
        doc["map"] = map;
        return doc;
    }
};

static std::string genDir()
{
    return ModsFolder() + ".gen";
}

// write a new level; its map name ("" if it could not be written)
static std::string writeLevel(int mode, int env, int wave, uint32_t seed)
{
    Gen* g = new Gen(mode, env, wave, seed); // (the grid is big for the stack)
    const Json::Value doc = mode == GEN_ENDLESS ? g->makeEndless() : g->make();
    delete g;
    char name[64];
    snprintf(name, sizeof(name), "Extras-%s-%08x", mode == GEN_PVP ? "PvP" : "Endless", (unsigned)seed);
    const std::string dir = genDir() + "/Maps";
    fsu::MakeDirs(dir);
    // older generated levels go (keep the one being played: a restart loads it again)
    for (const std::string& f : fsu::List(dir, false)) {
        if (f.rfind("Extras-", 0) == 0 && f != g_name + ".json") {
            fsu::RemoveTree(dir + "/" + f);
        }
    }
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    if (!fsu::WriteAll(dir + "/" + name + ".json", Json::writeString(w, doc))) {
        fprintf(stderr, "LugaruEX: could not write %s/%s.json\n", dir.c_str(), name);
        return std::string();
    }
    ModsSetGenRoot(genDir());
    ModsRescan();
    printf("LugaruEX: Extras made %s (%s, wave %d)\n", name, env == desertenvironment ? "desert" : env == snowyenvironment ? "tundra" : "grasslands", wave);
    return name;
}

static uint32_t newSeed()
{
    static uint32_t n = 0;
    return (uint32_t)time(nullptr) * 2654435761u ^ (uint32_t)SDL_GetTicks() * 40503u ^ (++n * 977u);
}

bool GenStart(int mode, int env)
{
    // test hook LUGARUEX_TEST_GENSEED=n: the same level every time
    static const char* fixed = getenv("LUGARUEX_TEST_GENSEED");
    const std::string name = writeLevel(mode, env, 1, fixed ? (uint32_t)strtoul(fixed, nullptr, 0) : newSeed());
    if (name.empty()) {
        return false;
    }
    return Menu::startLevelByName(name);
}

void GenContinue(bool)
{
    // Endless: everyone went down - a new run on a new land; PvP: the next round, new land too
    const std::string name = writeLevel(g_mode, g_env, 1, newSeed());
    if (!name.empty()) {
        startbonustotal = 0; // (a new run scores from nothing)
        bonustotal = 0;
        Game::LoadLevel(name);
    }
}

// ------------------------------------------------------------------ loading
void GenReset()
{
    g_mode = GEN_NONE;
    terrain.exHeights.clear();
    g_seen.clear();
    g_seenOrder = 0;
    g_looseDone = false;
    g_maxHumans = 0;
    g_ox = g_oz = 0;
    g_visited.clear();
    g_nextLook = 0;
    terrain.exAbsRandom = false;
    terrain.exAbsX = terrain.exAbsZ = 0;
    g_lastAlive = 0;
}

static std::vector<uint8_t> fromBase64(const std::string& s)
{
    std::vector<uint8_t> out;
    uint32_t v = 0;
    int bits = 0;
    for (char c : s) {
        int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (d < 0) {
            continue;
        }
        v = v << 6 | (uint32_t)d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)(v >> bits));
        }
    }
    return out;
}

void GenMapRead(const Json::Value& map, const std::string& name)
{
    const Json::Value& gen = map["exGen"];
    if (!gen.isObject()) {
        return;
    }
    const int mode = gen.get("mode", 0).asInt();
    const int size = gen.get("size", 0).asInt();
    std::vector<uint8_t> h = fromBase64(gen.get("heights", "").asString());
    if ((mode != GEN_ENDLESS && mode != GEN_PVP) || size < 2 || size > max_terrain_size || h.size() < (size_t)size * size) {
        fprintf(stderr, "LugaruEX: a generated level with a damaged ground - played as a normal level\n");
        return;
    }
    h.resize((size_t)size * size);
    g_mode = mode;
    g_wave = std::max(1, gen.get("wave", 1).asInt());
    g_seed = gen.get("seed", 0).asUInt();
    g_ringR = std::max(15.f, std::min(70.f, gen.get("ring", 45.0).asFloat()));
    g_weaponRule = std::max(0, std::min(5, gen.get("weapons", 0).asInt()));
    g_env = map["environment"].asInt();
    terrain.exHeights = h;
    g_name = name;
    terrain.exAbsRandom = mode == GEN_ENDLESS; // (the ground's noise tied to the land)
    terrain.exAbsX = terrain.exAbsZ = 0;
}

void GenGroundObjects()
{
    if (!GenActive()) {
        return;
    }
    for (auto& o : Object::objects) {
        if (o->type != bushtype) { // (a bush puts itself on the ground)
            o->position.y += terrain.getHeight(o->position.x, o->position.z);
        }
    }
}

void GenGroundPeople()
{
    if (!GenActive()) {
        return;
    }
    for (auto& pp : Person::players) {
        Person& p = *pp;
        p.coords.y += terrain.getHeight(p.coords.x, p.coords.z) + .1f;
        p.oldcoords = p.realoldcoords = p.coords;
        for (int k = 0; k < p.numwaypoints && k < 30; k++) {
            p.waypoints[k].y += terrain.getHeight(p.waypoints[k].x, p.waypoints[k].z);
        }
    }
    for (int j = 0; j < Game::numpathpoints && j < 30; j++) {
        Game::pathpoint[j].y += terrain.getHeight(Game::pathpoint[j].x, Game::pathpoint[j].z);
    }
    Game::mapcenter.y += terrain.getHeight(Game::mapcenter.x, Game::mapcenter.z);
    if (g_mode == GEN_ENDLESS) {
        makePaths();
    }

}

// ------------------------------------------------------------------ playing
static void giveWeapon(int person, int type)
{
    Person& p = *Person::players[person];
    if (!type || p.num_weapons >= 2) {
        return;
    }
    weapons.push_back(Weapon(type, person));
    p.weaponids[p.num_weapons++] = (int)weapons.size() - 1;
    p.weaponactive = -1; // (put away)
}

static void looseWeapon(float x, float z, int type)
{
    weapons.push_back(Weapon(type, -1));
    Weapon& w = weapons.back();
    w.position.x = x, w.position.z = z;
    w.position.y = terrain.getHeight(x, z) + 1.5f;
    w.tippoint = w.position;
    w.tippoint.y += .3f;
    XYZ down;
    down = 0;
    down.y = -1;
    w.drop(down, down);
}

// ------------------------------------------------------------------ Endless: the endless land
// The game's ground is one 256 x 256 grid (the window). The land goes on forever: when the
// players get far from the window's middle, the window moves along with them - the ground is
// made again for the new place (heightAbs), the parts of the land that came into the window get
// their things (Gen::chunk), the ones that left lose theirs, and everyone and everything that
// moves is moved back by the same step, so the players stay in the middle. Every PC does the
// same moves (an online guest when the host's snapshots tell it the window moved).
static const int kShiftCells = 32;                 // the window moves this far at a time (192 units)

static bool farFromPlayers(const XYZ& at, float d)
{
    for (int i = 0; i < (int)Person::players.size(); i++) {
        if (IsHuman(i) && !Person::players[i]->dead && distsqflat(&Person::players[i]->coords, const_cast<XYZ*>(&at)) < d * d) {
            return false;
        }
    }
    return true;
}

static XYZ ground(float x, float z, float over = .1f)
{
    XYZ p;
    p.x = x;
    p.z = z;
    p.y = terrain.getHeight(x, z) + over;
    return p;
}

// a person comes (back) into the game fresh: alive, unhurt, standing, a clean skin
static void freshen(Person& e)
{
    const bool wasDead = e.dead != 0;
    e.dead = 0;
    e.damage = e.permanentdamage = e.superpermanentdamage = 0;
    e.bloodloss = 0;
    e.deathbleeding = 0;
    e.unconscioustime = 0;
    e.onfire = false;
    e.burnt = 0;
    e.skeleton.free = 0;
    e.skeleton.longdead = 0;
    e.skeleton.broken = 0;
    e.velocity = 0;
    e.animCurrent = e.animTarget = e.getIdle();
    e.frameCurrent = 0;
    e.frameTarget = 1;
    e.target = 0;
    e.stunned = e.surprised = 0;
    e.numwaypoints = 0;
    e.waypoint = 0;
    e.exAiPlan = 0;
    e.exTarget = 0;
    e.exAiCruelty = -1; // (a new enemy: its own temper, no memory of a fight)
    e.exFightTime = 0;
    e.exLastFoe = -1;
    e.exAiBodyOf = -1;
    e.exAiBodyLeft = 0;
    if (wasDead) {
        e.addClothes(); // (the blood of its last life goes)
    }
}

// an enemy leaves the game: it waits far off, lying still, until the land needs it
static void park(int i, int slot)
{
    Person& e = *Person::players[i];
    freshen(e);
    e.animCurrent = e.animTarget = e.getIdle();
    e.howactive = typedead1;
    e.immobile = true;
    e.aitype = passivetype;
    e.coords = e.oldcoords = e.realoldcoords = ground(Gen::parkX(slot), Gen::parkZ(slot));
}

static bool parked(const Person& e)
{
    return !e.dead && e.howactive >= typedead1;
}

// how far the players have come (km from the start), for how hard the land gets
static float kmFromStart(float absX, float absZ)
{
    const float dx = absX - kN / 2 * kCell, dz = absZ - kN / 2 * kCell;
    return sqrtf(dx * dx + dz * dz) / 1000.f;
}

// an enemy comes in at 'at': on watch round there (patrol), resting by a fire (sit), armed by
// the start odds, better armed and a little stronger the further the players have come
static void bringIn(Rng& rng, int i, const XYZ& at, float km, bool sit, const std::vector<XYZ>* route = nullptr)
{
    Person& e = *Person::players[i];
    freshen(e);
    e.howactive = sit ? (rng.chance(.7f) ? typesitting : typesleeping) : typeactive;
    e.animCurrent = e.animTarget = e.getIdle();
    e.immobile = false;
    e.coords = e.oldcoords = e.realoldcoords = at;
    e.yaw = e.targetyaw = rng.r(0, 360);
    e.aitype = passivetype;
    // its kind (from the level file: big, small, strong, young) - a little stronger far out;
    // a bigger body takes more, a young one less
    if (e.exBasePower <= 0) {
        e.exBasePower = e.power;
        const float normal = e.creature == wolftype ? .23f : .2f;
        e.exBaseTol = e.damagetolerance * std::max(.6f, std::min(1.35f, e.scale / normal));
    }
    e.power = e.exBasePower * (1 + std::min(km, 5.f) * .03f);
    e.damagetolerance = e.exBaseTol;
    if (!sit && route && !route->empty()) {
        // a group walks one round together (each a step to the side)
        const float ox = rng.r(-1.5f, 1.5f), oz = rng.r(-1.5f, 1.5f);
        const int n = std::min(30, (int)route->size());
        for (int k = 0; k < n; k++) {
            e.waypoints[k] = ground((*route)[k].x + ox, (*route)[k].z + oz, 0);
            e.waypointtype[k] = wpkeepwalking;
        }
        e.numwaypoints = n;
    } else if (!sit) {
        const int n = 2 + rng.i(4);
        for (int k = 0; k < n; k++) {
            const float a = rng.r(0, 6.283f), d = rng.r(8, 26);
            e.waypoints[k] = ground(at.x + cosf(a) * d, at.z + sinf(a) * d, 0);
            e.waypointtype[k] = rng.chance(.25f) ? wppause : wpkeepwalking;
        }
        e.numwaypoints = n;
    }
    if (e.creature == rabbittype && e.num_weapons == 0) {
        const int none = std::max(30, 58 - (int)(km * 6)), r = rng.i(100);
        const int type = r < none ? 0 : r < none + (100 - none) * 52 / 100 ? knife : r < none + (100 - none) * 81 / 100 ? staff : sword;
        if (type) {
            weapons.push_back(Weapon(type, i));
            e.weaponids[e.num_weapons++] = (int)weapons.size() - 1;
            e.weaponactive = -1;
        }
    }
}

// someone who can come in somewhere new: one waiting far off, else one left far behind (alive
// but out of the fight), else a body nobody is near
static int freeEnemy(int creature)
{
    int behind = -1, body = -1;
    for (int i = 0; i < (int)Person::players.size(); i++) {
        const Person& e = *Person::players[i];
        if (IsHuman(i) || e.creature != creature) {
            continue;
        }
        if (parked(e)) {
            return i;
        }
        if (!e.dead && e.aitype != attacktypecutoff && behind < 0 && farFromPlayers(e.coords, 320)) {
            behind = i;
        } else if (e.dead && body < 0 && farFromPlayers(e.coords, 90)) {
            body = i;
        }
    }
    return behind >= 0 ? behind : body;
}

// a loose weapon nobody will miss (far from everyone), else a new one (up to a limit)
static void weaponAt(float x, float z, int type)
{
    for (size_t k = 0; k < weapons.size(); k++) {
        Weapon& w = weapons[k];
        if (w.owner == -1 && farFromPlayers(w.position, 320)) {
            w.setType(type);
            w.position = ground(x, z, 1.5f);
            w.tippoint = w.position;
            w.tippoint.y += .3f;
            XYZ down;
            down = 0;
            down.y = -1;
            w.drop(down, down);
            return;
        }
    }
    if (weapons.size() < 64) {
        XYZ down;
        down = 0;
        down.y = -1;
        weapons.push_back(Weapon(type, -1));
        Weapon& w = weapons.back();
        w.position = ground(x, z, 1.5f);
        w.tippoint = w.position;
        w.tippoint.y += .3f;
        w.drop(down, down);
    }
}

// the parts of the land the players come near get their people: a camp's are by its fire, else
// now and then a few on watch; more and more the further the players go
static void lookAround(Rng& rng)
{
    XYZ mid;
    mid = 0;
    int n = 0;
    for (int i = 0; i < (int)Person::players.size(); i++) {
        if (IsHuman(i) && !Person::players[i]->dead) {
            mid += Person::players[i]->coords;
            n++;
        }
    }
    if (!n) {
        return;
    }
    mid /= (float)n;
    const int ccx = (int)floorf((mid.x + g_ox * kCell) / kChunkW), ccz = (int)floorf((mid.z + g_oz * kCell) / kChunkW);
    for (int cx = ccx - 3; cx <= ccx + 3; cx++) {
        for (int cz = ccz - 3; cz <= ccz + 3; cz++) {
            if (g_visited.count({ cx, cz })) {
                continue;
            }
            // (in the window, in local units)
            const float lx = cx * kChunkW - g_ox * kCell, lz = cz * kChunkW - g_oz * kCell;
            if (lx < 0 || lz < 0 || lx + kChunkW > kN * kCell || lz + kChunkW > kN * kCell) {
                continue;
            }
            XYZ c = ground(lx + kChunkW / 2, lz + kChunkW / 2);
            if (!farFromPlayers(c, 180)) {
                g_visited.insert({ cx, cz }); // (already in sight: it stays as it is)
                continue;
            }
            if (farFromPlayers(c, 300)) {
                continue; // (not near yet)
            }
            g_visited.insert({ cx, cz });
            const float km = kmFromStart(cx * kChunkW + kChunkW / 2, cz * kChunkW + kChunkW / 2);
            // the part's own things: a camp?
            std::vector<Gen::Spot> camps;
            Gen part(GEN_ENDLESS, g_env, 1, Gen::partSeed(g_seed, cx, cz));
            part.absMode = true;
            part.world = g_seed;
            part.chunk(cx, cz, camps);
            int brought = 0;
            for (const Gen::Spot& s : camps) {
                const int sitters = 2 + rng.i(2); // (a camp is never one alone)
                for (int k = 0; k < sitters; k++) {
                    const int e = freeEnemy(rabbittype); // (wolves don't sit by fires)
                    if (e < 0) {
                        break;
                    }
                    const float a = rng.r(0, 6.283f);
                    bringIn(rng, e, ground(s.x - g_ox * kCell + cosf(a) * 3.2f, s.z - g_oz * kCell + sinf(a) * 3.2f), km, true);
                    brought++;
                }
            }
            if (camps.empty() && rng.chance(std::min(.45f, .16f + .08f * km))) {
                // a group on watch, walking one round together: mostly 2-4, a lone one now and
                // then, bigger further out; rabbits mostly, wolves very seldom, the two together
                // the rarest of all
                const int r = rng.i(100);
                int group = r < 12 ? 1 : r < 38 ? 2 : r < 68 ? 3 : r < 88 ? 4 : 5;
                if (rng.chance(std::min(.5f, km * .15f))) {
                    group++;
                }
                const int which = rng.i(100);
                const int kind = which < 85 ? 0 : which < 95 ? 1 : 2; // rabbits, wolves, both
                if (kind == 1) {
                    group = std::min(group, 3);
                } else if (kind == 2) {
                    group = std::max(group, 2);
                }
                const float gx = lx + rng.r(20, kChunkW - 20), gz = lz + rng.r(20, kChunkW - 20);
                std::vector<XYZ> route;
                const int stops = 3 + rng.i(3);
                for (int k = 0; k < stops; k++) {
                    const float a = rng.r(0, 6.283f), d = rng.r(8, 30);
                    route.push_back(ground(gx + cosf(a) * d, gz + sinf(a) * d, 0));
                }
                for (int k = 0; k < group; k++) {
                    const int creature = kind == 0 ? rabbittype : kind == 1 ? wolftype : (k % 2 ? wolftype : rabbittype);
                    const int e = freeEnemy(creature);
                    if (e < 0) {
                        continue;
                    }
                    bringIn(rng, e, ground(gx + rng.r(-3, 3), gz + rng.r(-3, 3)), km, false, &route);
                    brought++;
                }
                printf("LugaruEX: Endless - a %s group of %d\n", kind == 0 ? "rabbit" : kind == 1 ? "wolf" : "rabbit and wolf", group);
            }
            if (rng.chance(.12f)) {
                const int r = rng.i(100);
                weaponAt(lx + rng.r(10, kChunkW - 10), lz + rng.r(10, kChunkW - 10), r < 60 ? knife : r < 85 ? staff : sword);
            }
            if (brought) {
                printf("LugaruEX: Endless - %d enem%s at (%d, %d), %.1f km out\n", brought, brought == 1 ? "y" : "ies", cx, cz, km);
            }
        }
    }
}

// path points the enemies find their way with: a grid round the window's middle
static void makePaths()
{
    const float c = kN / 2 * kCell, step = 60;
    Game::numpathpoints = 25;
    for (int a = 0; a < 5; a++) {
        for (int b = 0; b < 5; b++) {
            const int j = a * 5 + b;
            Game::pathpoint[j] = ground(c + (a - 2) * step, c + (b - 2) * step, 0);
            Game::numpathpointconnect[j] = 0;
            for (int da = -1; da <= 1; da++) {
                for (int db = -1; db <= 1; db++) {
                    const int na = a + da, nb = b + db;
                    if ((da || db) && na >= 0 && nb >= 0 && na < 5 && nb < 5) {
                        Game::pathpointconnect[j][Game::numpathpointconnect[j]++] = na * 5 + nb;
                    }
                }
            }
        }
    }
    Game::mapcenter = ground(c, c, 0);
}

// the window moves (dxc, dzc) cells along the land
static void worldShift(int dxc, int dzc)
{
    const Uint32 t0 = SDL_GetTicks();
    XYZ d;
    d.x = dxc * kCell;
    d.y = 0;
    d.z = dzc * kCell;
    const int oldCx = g_ox / kChunk, oldCz = g_oz / kChunk;
    g_ox += dxc;
    g_oz += dzc;
    const int newCx = g_ox / kChunk, newCz = g_oz / kChunk;
    // the ground of the new place (its noise tied to the land, not the window: the same ground
    // under the same spot whichever way the window lies)
    terrain.exHeights = windowBytes(g_env, g_seed, g_ox, g_oz);
    terrain.exAbsX = g_ox;
    terrain.exAbsZ = g_oz;
    const float temptexdetail = texdetail;
    texdetail = 1;
    terrain.load("Textures/HeightMap.png");
    texdetail = temptexdetail;
    // everyone and everything that moves comes along
    for (auto& pp : Person::players) {
        Person& p = *pp;
        p.coords -= d;
        p.oldcoords -= d;
        p.realoldcoords -= d;
        for (int k = 0; k < p.numwaypoints && k < 90; k++) {
            p.waypoints[k] -= d;
        }
        p.lastseen -= d;
        p.avoidwhere -= d;
        p.headtarget -= d;
        p.finalfinaltarget -= d;
        p.finaltarget -= d;
        p.laststanding = 0; // (an object number: the objects change)
        p.finalpathfindpoint = p.targetpathfindpoint = p.lastpathfindpoint = p.lastpathfindpoint2 = p.lastpathfindpoint3 = p.lastpathfindpoint4 = -1;
    }
    for (auto& w : weapons) {
        w.exShift(d);
        w.exInTree = false; // (its tree may be gone: it falls if nothing holds it)
    }
    viewer -= d;
    cameraloc -= d;
    Game::hawkcoords -= d;
    Game::realhawkcoords -= d;
    numenvsounds = 0;
    LocalShiftViews(d);
    Sprite::deleteSprites();
    terrain.decals.clear();
    // the things of the parts that left go, those that stay come along, new parts get theirs
    auto inWindow = [&](int cx, int cz) { return cx >= newCx + kThingsFrom && cz >= newCz + kThingsFrom && cx < newCx + kThingsTo && cz < newCz + kThingsTo; };
    auto& objs = Object::objects;
    objs.erase(std::remove_if(objs.begin(), objs.end(),
                              [&](const std::unique_ptr<Object>& o) {
                                  const int cx = (int)floorf((o->position.x + (g_ox - dxc) * kCell) / kChunkW);
                                  const int cz = (int)floorf((o->position.z + (g_oz - dzc) * kCell) / kChunkW);
                                  return !inWindow(cx, cz);
                              }),
               objs.end());
    for (auto& o : objs) {
        o->position -= d;
        o->model.decals.clear();
    }
    std::vector<Object*> fresh; // (the new things: their own shadows still to bake)
    for (int cx = newCx + kThingsFrom; cx < newCx + kThingsTo; cx++) {
        for (int cz = newCz + kThingsFrom; cz < newCz + kThingsTo; cz++) {
            if (cx >= oldCx + kThingsFrom && cz >= oldCz + kThingsFrom && cx < oldCx + kThingsTo && cz < oldCz + kThingsTo) {
                continue; // (it was there: its things came along)
            }
            std::vector<Gen::Spot> camps;
            Gen part(GEN_ENDLESS, g_env, 1, Gen::partSeed(g_seed, cx, cz));
            part.absMode = true;
            part.world = g_seed;
            part.chunk(cx, cz, camps);
            for (const Gen::Item& it : part.items) {
                XYZ at = ground(it.x - g_ox * kCell, it.z - g_oz * kCell, it.y);
                objs.emplace_back(new Object((object_type)it.type, at, it.yaw, it.pitch, it.scale));
                fresh.push_back(objs.back().get());
            }
        }
    }
    // the same order on every PC, however each got here (objects go by number in snapshots)
    std::stable_sort(objs.begin(), objs.end(), [](const std::unique_ptr<Object>& a, const std::unique_ptr<Object>& b) {
        if (a->position.x != b->position.x) {
            return a->position.x < b->position.x;
        }
        if (a->position.z != b->position.z) {
            return a->position.z < b->position.z;
        }
        return a->type < b->type;
    });
    // what knows where the things are
    for (int i = 0; i < subdivision; i++) {
        for (int j = 0; j < subdivision; j++) {
            terrain.patchobjects[i][j].clear();
        }
    }
    Object::AddObjectsToTerrain();
    terrain.DoShadows();
    XYZ lightloc = light.location;
    if (!skyboxtexture) {
        lightloc = 0;
    }
    lightloc.y += 10;
    Normalise(&lightloc);
    for (Object* o : fresh) {
        o->exBakeShadows(lightloc);
    }
    makePaths();
    // enemies left outside the window wait far off again (the host decides who is where)
    if (!NetIsGuest()) {
        int slot = 0;
        for (int i = 0; i < (int)Person::players.size(); i++) {
            Person& e = *Person::players[i];
            const bool out = e.coords.x < 60 || e.coords.z < 60 || e.coords.x > kN * kCell - 60 || e.coords.z > kN * kCell - 60;
            if (!IsHuman(i) && (parked(e) || out)) {
                park(i, slot++);
            }
        }
    } else {
        NetGuestShift(d);
    }
    InterpInvalidate();
    printf("LugaruEX: Endless - the land moved on to (%d, %d) in %u ms, %d things\n", g_ox, g_oz, SDL_GetTicks() - t0, (int)objs.size());
}

// the players got far from the window's middle: move it along (host; guests follow)
static void keepPlayersInTheMiddle()
{
    XYZ mid;
    mid = 0;
    int n = 0;
    for (int i = 0; i < (int)Person::players.size(); i++) {
        if (IsHuman(i) && !Person::players[i]->dead) {
            mid += Person::players[i]->coords;
            n++;
        }
    }
    if (!n) {
        return;
    }
    mid /= (float)n;
    const float c = kN / 2 * kCell, limit = kShiftCells * kCell;
    int dxc = 0, dzc = 0;
    while (mid.x - dxc * kCell - c > limit) {
        dxc += kShiftCells;
    }
    while (mid.x - dxc * kCell - c < -limit) {
        dxc -= kShiftCells;
    }
    while (mid.z - dzc * kCell - c > limit) {
        dzc += kShiftCells;
    }
    while (mid.z - dzc * kCell - c < -limit) {
        dzc -= kShiftCells;
    }
    if (dxc || dzc) {
        worldShift(dxc, dzc);
    }
}

static void endlessStep(Rng& rng)
{
    if (g_mode != GEN_ENDLESS) {
        return;
    }
    bool anyone = false;
    for (int i = 0; i < (int)Person::players.size(); i++) {
        anyone = anyone || (IsHuman(i) && !Person::players[i]->dead);
    }
    if (!anyone) {
        return; // (everyone is down: the run is over)
    }
    g_lastAlive = Game::leveltime;
    keepPlayersInTheMiddle();
    // a player who strayed far from the others (near where the land under the window ends)
    // is brought back to the nearest one still standing
    for (int i = 0; i < (int)Person::players.size(); i++) {
        Person& p = *Person::players[i];
        const float lo = 120, hi = kN * kCell - 120;
        if (!IsHuman(i) || p.dead || (p.coords.x > lo && p.coords.z > lo && p.coords.x < hi && p.coords.z < hi)) {
            continue;
        }
        int best = -1;
        float bestD = 1e30f;
        for (int j = 0; j < (int)Person::players.size(); j++) {
            const Person& q = *Person::players[j];
            if (j != i && IsHuman(j) && !q.dead && q.coords.x > lo && q.coords.z > lo && q.coords.x < hi && q.coords.z < hi) {
                const float dd = distsqflat(const_cast<XYZ*>(&q.coords), &p.coords);
                if (dd < bestD) {
                    bestD = dd;
                    best = j;
                }
            }
        }
        if (best >= 0) {
            const XYZ& q = Person::players[best]->coords;
            p.coords = p.oldcoords = p.realoldcoords = ground(q.x + 1.5f, q.z + 1.5f);
            p.velocity = 0;
            printf("LugaruEX: Endless - person %d strayed too far, back with person %d\n", i, best);
        }
    }
    if (Game::leveltime >= g_nextLook) {
        g_nextLook = Game::leveltime + .5f;
        lookAround(rng);
    }
}

void GenGuestOrigin(int ox, int oz)
{
    if (NetIsGuest() && g_mode == GEN_ENDLESS && (ox != g_ox || oz != g_oz)) {
        worldShift(ox - g_ox, oz - g_oz);
    }
}

void GenOrigin(int& ox, int& oz)
{
    ox = g_ox;
    oz = g_oz;
}

void GenTick()
{
    if (!GenActive() || NetIsGuest() || Person::players.empty()) {
        return;
    }
    GenBodies();
    static Rng rng(1);
    rng.s ^= (uint64_t)SDL_GetTicks() * 0x2545F4914F6CDD1Dull;
    int humans = 0;
    for (int i = 0; i < (int)Person::players.size(); i++) {
        if (!IsHuman(i)) {
            continue;
        }
        humans++;
        Person& p = *Person::players[i];
        if (g_seen.count(&p)) {
            continue;
        }
        g_seen.insert(&p);
        const int k = g_seenOrder++;
        if (Versus() && !p.dead) {
            // PvP: round the ring, facing the middle (opposite sides first)
            static const int order[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
            // (more than 8: the next ones between them, a little further in)
            const float a = order[k % 8] * .785398f + ((k / 8) & 1) * .3927f, R = g_ringR * ((k / 8) & 1 ? .7f : 1.f);
            XYZ c = Game::mapcenter, at = Game::mapcenter;
            at.x = c.x + cosf(a) * R;
            at.z = c.z + sinf(a) * R;
            at.y = terrain.getHeight(at.x, at.z) + .1f;
            p.coords = p.oldcoords = p.realoldcoords = at;
            p.velocity = 0;
            p.yaw = p.targetyaw = roughDirectionTo(at, c);
            LocalFaceView(i, p.yaw);
        }
        // a start weapon now and then: mostly none, the stronger the rarer
        static const bool noWeapons = getenv("LUGARUEX_TEST_NOWEAPONS") != nullptr; // test hook: players start unarmed
        if (p.num_weapons == 0 && !noWeapons) {
            const int r = rng.i(100);
            const int rule = Versus() ? g_weaponRule : 0; // (PvP: this round's weapons)
            giveWeapon(i, rule == 1 || rule == 5 ? 0 : rule == 2 ? knife : rule == 3 ? staff : rule == 4 ? sword : r < 58 ? 0 : r < 80 ? knife : r < 92 ? staff : sword);
        }
    }
    g_maxHumans = std::max(g_maxHumans, humans);
    endlessStep(rng);
    // test hook LUGARUEX_TEST_REVIVEAT=s: at s seconds player 1 stands next to a fallen partner
    static const float reviveAt = getenv("LUGARUEX_TEST_REVIVEAT") ? (float)atof(getenv("LUGARUEX_TEST_REVIVEAT")) : -1.f;
    static float lastLt = 0;
    static bool moved = false;
    if (Game::leveltime < lastLt) {
        moved = false;
    }
    lastLt = Game::leveltime;
    if (reviveAt >= 0 && !moved && Game::leveltime > reviveAt) {
        for (int i = 1; i < (int)Person::players.size(); i++) {
            Person& b = *Person::players[i];
            if (IsHuman(i) && b.dead && b.skeleton.free) {
                Person& p = *Person::players[0];
                p.coords = b.coords;
                p.coords.x += .8f;
                p.coords.y = terrain.getHeight(p.coords.x, p.coords.z) + .1f;
                p.oldcoords = p.realoldcoords = p.coords;
                p.velocity = 0;
                moved = true;
                printf("EXTEST player 1 next to the body of person %d\n", i);
                break;
            }
        }
    }
    // a few weapons lying about
    if (!g_looseDone && Game::leveltime > .5f) {
        g_looseDone = true;
        // (PvP: by this round's rule - none, one kind, or a pile of them)
        const int rule = Versus() ? g_weaponRule : 0;
        const int n = !Versus() ? 1 + rng.i(2) : rule == 1 ? 0 : rule == 5 ? 8 + rng.i(5) : rule == 4 ? 2 + rng.i(2) : 2 + rng.i(3);
        for (int k = 0; k < n; k++) {
            const float a = rng.r(0, 6.283f), d = rng.r(10, std::max(20.f, Versus() ? g_ringR * 1.1f : kArena * kCell * .6f));
            const int r = rng.i(100);
            const int type = rule == 2 ? knife : rule == 3 ? staff : rule == 4 ? sword : r < 60 ? knife : r < 85 ? staff : sword;
            looseWeapon(Game::mapcenter.x + cosf(a) * d, Game::mapcenter.z + sinf(a) * d, type);
        }
    }
}

int GenWinner()
{
    if (!Versus()) {
        return -1;
    }
    int alive = -1, living = 0, all = 0;
    for (int i = 0; i < (int)Person::players.size(); i++) {
        if (IsHuman(i)) {
            all++;
            if (!Person::players[i]->dead) {
                living++;
                alive = i;
            }
        }
    }
    g_maxHumans = std::max(g_maxHumans, all);
    // alone (no guest on the way): the only player standing is the winner
    const bool solo = g_maxHumans == 1 && Game::leveltime > 3 && !NetIsGuest() && NetConnectedGuests() == 0;
    return ((g_maxHumans >= 2 || solo) && living == 1) ? alive : -1;
}

bool GenVersusWon()
{
    return GenWinner() >= 0;
}

void GenScore(int person, int value)
{
    if (Versus() && person >= 0 && person < (int)Person::players.size()) {
        Person::players[person]->exScore += value;
    }
}

// ------------------------------------------------------------------ players' bodies
// Like the Endless enemies' kinds: rabbits 55% normal, 12% big, 12% small, 13% strong, 8% young;
// wolves 62% normal, 20% large, 18% young. From the level's seed and the player's number, so
// every PC gives the same player the same body; a new round / run rolls again.
static int bodyRoll(int person, int creature)
{
    uint32_t h = g_seed * 2654435761u ^ (uint32_t)(person + 1) * 2246822519u ^ 0x9E3779B9u;
    h = (h ^ (h >> 15)) * 2246822519u;
    h ^= h >> 13;
    const int r = (int)(h % 100);
    if (creature == wolftype) {
        return r < 62 ? 0 : r < 82 ? 1 : 4;
    }
    return r < 55 ? 0 : r < 67 ? 1 : r < 79 ? 2 : r < 92 ? 3 : 4;
}

void GenBodies()
{
    if (!GenActive()) {
        return;
    }
    static const char* kNames[5] = { "normal", "big", "small", "strong", "young" };
    for (int i = 0; i < (int)Person::players.size(); i++) {
        if (!IsHuman(i)) {
            continue;
        }
        Person& p = *Person::players[i];
        const bool wolf = p.creature == wolftype;
        const int body = bodyRoll(i, p.creature);
        float scale = wolf ? .23f : .2f, power = 1, speed = 1, prop[4] = { 1, 1, 1, 1 };
        switch (body) {
            case 1: scale = wolf ? .276f : .245f, power = wolf ? 1.25f : 1.15f, speed = wolf ? .95f : .92f, prop[1] = wolf ? 1 : 1.05f; break;
            case 2: scale = .172f, power = .85f, speed = 1.1f; break;
            case 3: scale = .212f, power = 1.35f, prop[1] = 1.06f, prop[2] = 1.12f; break;
            case 4: scale = wolf ? .184f : .142f, power = wolf ? .7f : .6f, speed = wolf ? 1.1f : 1.15f, prop[0] = wolf ? 1 : 1.25f, prop[3] = wolf ? 1 : .9f; break;
            default: break;
        }
        bool same = p.exBody == body && fabsf(p.scale - scale) < 1e-4f;
        for (int k = 0; k < 4; k++) {
            same = same && fabsf(p.getProportion(k) - prop[k]) < 1e-4f;
        }
        if (same || p.skeleton.free) {
            continue; // (a body lying about keeps its size)
        }
        const float normal = wolf ? .23f : .2f;
        p.scale = scale;
        p.setProportions(prop[0], prop[1], prop[2], prop[3]);
        p.power = power;
        p.speedmult = speed;
        p.damagetolerance = PersonType::types[p.creature].defaultDamageTolerance * std::max(.6f, std::min(1.35f, scale / normal));
        if (p.exBody != body) {
            printf("LugaruEX: player %d is %s %s this level\n", i, kNames[body], wolf ? "(wolf)" : "(rabbit)");
        }
        p.exBody = body;
    }
}

// ------------------------------------------------------------------ Endless: revive
struct Revive
{
    int body = -1, stabs = 0, need = 0;
    bool counted = false;
};
static std::vector<Revive> g_revive;

void GenReviveInput(int person, bool held, bool pressed)
{
    if (g_mode != GEN_ENDLESS || person < 0 || person >= (int)Person::players.size()) {
        return;
    }
    if ((int)g_revive.size() < (int)Person::players.size()) {
        g_revive.resize(Person::players.size());
    }
    Revive& st = g_revive[person];
    Person& p = *Person::players[person];
    if (st.body >= (int)Person::players.size()) {
        st = Revive();
    }
    // a stab under way: each one counts at its strike
    if (p.animCurrent == crouchstabanim && st.body >= 0 && !p.hasWeapon()) {
        const bool strike = p.currentFrame().label == 5;
        if (strike && !st.counted) {
            st.counted = true;
            Person& b = *Person::players[st.body];
            if (b.dead && b.skeleton.free) {
                st.stabs++;
                emit_sound_at(thudsound, b.coords, 96.);
                if (st.stabs >= st.need) {
                    b.dead = 0;
                    b.damage = b.permanentdamage = b.damagetolerance * .5f;
                    b.superpermanentdamage = 0;
                    b.bloodloss = 0;
                    b.deathbleeding = 0;
                    b.unconscioustime = 0;
                    b.skeleton.free = 1; // gets up like after any knock-over
                    b.skeleton.longdead = 0;
                    printf("LugaruEX: person %d brought person %d back (%d tries)\n", person, st.body, st.stabs);
                    st = Revive();
                }
            }
        }
        if (!strike) {
            st.counted = false;
        }
        return;
    }
    const bool going = held; // (holding the button keeps at it, or starts once in reach)
    static const bool dbg = getenv("LUGARUEX_TEST_REVIVEDEBUG") != nullptr;
    if (dbg && (pressed || held)) {
        static float t = 0;
        if (Game::leveltime - t > .5f) {
            t = Game::leveltime;
            printf("EXTEST revive %d: pressed=%d held=%d dead=%d free=%d weapon=%d crouch=%d anim=%d body=%d stabs=%d\n", person, pressed, held, p.dead, p.skeleton.free, p.hasWeapon(), p.isCrouch(), p.animTarget, st.body, st.stabs);
        }
    }
    if (!pressed && !going) {
        return;
    }
    if (p.dead || p.skeleton.free || p.hasWeapon() || !p.isCrouch()) {
        return;
    }
    int best = -1;
    float bestD = 1e30f;
    const float reach = 6.f * (p.scale * 5) * (p.scale * 5); // (flat, to the middle of a body ~2 units long)
    for (int i = 0; i < (int)Person::players.size(); i++) {
        Person& b = *Person::players[i];
        if (i == person || !IsHuman(i) || !b.dead || !b.skeleton.free) {
            continue;
        }
        const float d = distsqflat(&p.coords, &b.coords);
        if (fabsf(p.coords.y - b.coords.y) > 2) {
            continue;
        }
        if (d < reach && d < bestD) {
            best = i;
            bestD = d;
        }
    }
    if (best < 0) {
        return;
    }
    if (st.body != best) {
        st = Revive();
        st.body = best;
        st.need = 3 + rand() % 3;
    }
    Person& b = *Person::players[best];
    // the knife-stab move, with no knife: pressing on the chest
    p.victim = Person::players[best];
    p.hasvictim = 0;
    p.animTarget = crouchstabanim;
    p.frameTarget = 0;
    p.target = 0;
    XYZ chest = b.coords + (b.jointPos(abdomen) + b.jointPos(neck)) / 2 * b.scale;
    p.targetyaw = roughDirectionTo(p.coords, chest);
    p.targettilt2 = pitchTo(p.coords, chest);
    st.counted = false;
}
}

namespace EX
{
bool GenVersusRound()
{
    GenWinner(); // (counts the players)
    return Versus(); // (players only: one left standing - or alone - wins)
}
}

namespace EX
{
float GenRunTime()
{
    return g_lastAlive > 0 ? g_lastAlive : Game::leveltime;
}
}
