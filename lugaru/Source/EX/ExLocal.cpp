/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "EX/ExLocal.hpp"

#include "EX/ExConfig.hpp"
#include "EX/ExPad.hpp"
#include "EX/ExNet.hpp"
#include "Audio/openal_wrapper.hpp"
#include "Game.hpp"
#include "Level/Dialog.hpp"
#include "Objects/Object.hpp"
#include "Objects/Person.hpp"
#include "Objects/PersonType.hpp"
#include "Objects/Weapons.hpp"
#include "Tutorial.hpp"
#include "Utils/Input.hpp"
#include "Utils/Folders.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "outfits.h"
#include "looks.h"

extern XYZ viewer, viewerfacing, cameraloc;
extern float cameradist, camerashake, woozy, blackout;
extern float screenwidth, screenheight;
extern float multiplier;
extern int mainmenu;
extern int kContextWidth, kContextHeight;
extern Terrain terrain;

namespace EX
{
static const int kMaxLocal = 4;

struct Camera
{
    XYZ viewer, viewerfacing, cameraloc;
    float yaw = 0, pitch = 0, cameradist = 0;
    float shake = 0, woozy = 0, blackout = 0;
};

struct Slot
{
    std::string padId;
    std::string ch = "default"; // default | random | <model>:<skin path>
    float rx = 0, ry = 0, rw = 1, rh = 1; // screen part, 0..1 from the top-left
    bool hasRect = false;

    int person = -1;          // index in Person::players while in a level
    Person* who = nullptr;    // to detect a stale index after a level change
    Camera cam;
    XYZ prevViewer, curViewer;
    bool viewValid = false;
    float pendYaw = 0, pendPitch = 0; // stick look not simulated yet (degrees)

    std::string rndLevel; // the random skin is kept while the same level is replayed
    int rndSkin = -1;
    int rndCreature = -1;

    int spec = -1;          // while dead: the player this view watches (-1 = yourself)
    bool specBtn = false;   // attack button last step (to cycle on a fresh press)

    // online guest playing on this host: no screen here, input comes from the network
    bool remote = false;
    unsigned long long token = 0;
    PadInput net;
    bool netConnected = false;
    bool removed = false; // kicked by the host: its character became an enemy, the seat is gone
    float lookYaw = 0, lookPitch = 0; // tutorial: camera at the last "looking around" check
    XYZ spawnAt;            // where it was put next to player 1
    bool following = false; // not moved by its player yet: kept beside player 1 when the game moves player 1 (scenes)
};

static std::vector<Slot> g_slots;
static bool g_custom = false;
static bool g_sideBySide = false; // two players side by side instead of top / bottom
static int g_launchCount = 0;
static int g_view = 0; // slot whose camera is active
static int g_vx = 0, g_vy = 0;
static std::string g_level;

// ------------------------------------------------------------------ helpers
static bool personValid(const Slot& s)
{
    return s.person >= 0 && s.person < (int)Person::players.size() && Person::players[s.person].get() == s.who;
}

// players with a screen on this PC
static int activeCount()
{
    int n = 0;
    for (auto& s : g_slots) {
        if (personValid(s) && !s.remote) {
            n++;
        }
    }
    return n;
}

// every human player in the level, online guests included
static int humanCount()
{
    int n = 0;
    for (auto& s : g_slots) {
        if (personValid(s)) {
            n++;
        }
    }
    return n;
}

static int localSlotCount()
{
    int n = 0;
    for (auto& s : g_slots) {
        n += s.remote ? 0 : 1;
    }
    return n;
}

static Slot* slotOfPerson(int person)
{
    for (auto& s : g_slots) {
        if (s.person == person && personValid(s)) {
            return &s;
        }
    }
    return nullptr;
}

static void autoLayout()
{
    std::vector<Slot*> local;
    for (auto& s : g_slots) {
        if (!s.remote) {
            local.push_back(&s);
        }
    }
    const int n = (int)local.size();
    if (g_custom && n == g_launchCount) {
        bool all = true;
        for (auto* s : local) {
            all = all && s->hasRect;
        }
        if (all) {
            return; // the launcher's own arrangement
        }
    }
    // same defaults as the launcher: 1 full, 2 top/bottom, 3 = two on top + one wide, 4 quadrants
    static const float L[5][4][4] = {
        {},
        { { 0, 0, 1, 1 } },
        { { 0, 0, 1, .5f }, { 0, .5f, 1, .5f } },
        { { 0, 0, .5f, .5f }, { .5f, 0, .5f, .5f }, { 0, .5f, 1, .5f } },
        { { 0, 0, .5f, .5f }, { .5f, 0, .5f, .5f }, { 0, .5f, .5f, .5f }, { .5f, .5f, .5f, .5f } },
    };
    for (int i = 0; i < n && i < kMaxLocal; i++) {
        local[i]->rx = L[n][i][0];
        local[i]->ry = L[n][i][1];
        local[i]->rw = L[n][i][2];
        local[i]->rh = L[n][i][3];
        if (n == 2 && g_sideBySide) { // the launcher's "Split: side by side"
            local[i]->rx = i * .5f, local[i]->ry = 0, local[i]->rw = .5f, local[i]->rh = 1;
        }
    }
}

static std::string lower(std::string s)
{
    for (auto& c : s) {
        c = (char)tolower((unsigned char)c);
    }
    return s;
}

// ------------------------------------------------------------------ config
void LocalInit()
{
    g_slots.clear();
    const int n = std::min(kMaxLocal, atoi(IniString("Local", "Count", "0").c_str()));
    g_custom = atoi(IniString("Local", "Custom", "0").c_str()) != 0;
    g_sideBySide = lower(IniString("Local", "Split2", "topbottom")) == "sidebyside";
    for (int i = 0; i < n; i++) {
        const std::string k = "P" + std::to_string(i + 1);
        Slot s;
        s.padId = IniString("Local", k);
        if (s.padId.empty()) {
            continue;
        }
        // player 1 is always the Character tab (LocalCharacterOf); older launchers wrote
        // P1Char=random for it, which turned a chosen wolf back into a random rabbit
        s.ch = g_slots.empty() ? std::string("default") : lower(IniString("Local", k + "Char", "default"));
        float x, y, w, h;
        if (sscanf(IniString("Local", k + "Rect").c_str(), "%f,%f,%f,%f", &x, &y, &w, &h) == 4 && w > 0.05f && h > 0.05f) {
            s.rx = x, s.ry = y, s.rw = w, s.rh = h;
            s.hasRect = true;
        }
        g_slots.push_back(s);
    }
    g_launchCount = (int)g_slots.size();
    if (g_slots.empty()) {
        g_slots.push_back(Slot()); // player 1, keyboard (a controller attaches when it shows up)
    }
    autoLayout();
    printf("LugaruEX: local players: %d\n", (int)g_slots.size());
}

std::string LocalPadOfP1()
{
    return g_slots.empty() ? std::string() : g_slots[0].padId;
}

// ------------------------------------------------------------------ characters
static void parseChar(const std::string& spec, std::string& model, std::string& skin)
{
    const size_t c = spec.find(':');
    if (c == std::string::npos) {
        model = spec;
        skin.clear();
    } else {
        model = spec.substr(0, c);
        skin = spec.substr(c + 1);
    }
}

// the look the level gives its player, noted as it loads (before anyone's launcher choice is
// applied): "Default" for any player - player 1, local players 2+, online guests - is this
static int g_levelCreature = -1, g_levelSkin = 0;
static float g_levelScale = .2f, g_levelTolerance = 200;
static std::vector<std::string> g_levelClothes;
static std::vector<float> g_levelTintR, g_levelTintG, g_levelTintB;

void NoteLevelLook(const Person& p1)
{
    g_levelCreature = p1.creature;
    g_levelSkin = p1.whichskin;
    g_levelScale = p1.scale;
    g_levelTolerance = p1.damagetolerance;
    g_levelClothes = p1.clothes;
    g_levelTintR = p1.clothestintr;
    g_levelTintG = p1.clothestintg;
    g_levelTintB = p1.clothestintb;
}

void ApplyCharacter(Person& p, const std::string& model, const std::string& skinIn, bool tutorial)
{
    // model: "rabbit", "wolf" or a custom model "rabbit/Name" / "wolf/Name" (shared/looks.h)
    const bool wolf = (Looks_Base(model) == "wolf") && !tutorial; // tutorial steps need a rabbit
    const person_type want = wolf ? wolftype : rabbittype;
    std::string dir = tutorial ? std::string() : Looks_ModelDir(model);
    if (!dir.empty() && Looks_Base(model) != (wolf ? "wolf" : "rabbit")) {
        dir.clear();
    }
    auto exists = [](const std::string& rel) {
        FILE* f = fopen(Folders::getResourcePath(rel).c_str(), "rb");
        if (f) {
            fclose(f);
        }
        return f != nullptr;
    };
    std::string skin = skinIn;
    if (skin == "default" && !dir.empty()) {
        // a custom model's own skin, when it brings one
        for (const char* ext : { "png", "jpg", "jpeg" }) {
            if (skin == "default" && exists(dir + "Skin." + ext)) {
                skin = dir + "Skin." + ext;
            }
        }
    }
    if (skin == "default") {
        // the level's own skin for its player; on the other creature, that creature's first
        // (players 2+ start as copies of player 1, so name the skin rather than keeping theirs)
        const std::vector<std::string>& list = PersonType::types[want].skins;
        const int k = (g_levelCreature == want && g_levelSkin >= 0 && g_levelSkin < (int)list.size()) ? g_levelSkin : 0;
        skin = list.empty() ? std::string() : list[k];
    }
    if (!dir.empty()) {
        // (a model from a mod that isn't here: online, before the files came, or switched off)
        bool any = false;
        for (const std::string& f : { std::string("Body.solid"), std::string("Wolf.solid"), std::string("BasicFigure"), std::string("BasicFigureWolf") }) {
            any = any || exists(dir + f);
        }
        if (!any) {
            printf("LugaruEX: model %s not found here, using the creature's own\n", model.c_str());
            dir.clear();
        }
    }

    if (wolf) {
        // a player wolf keeps the level's weapons put away (they draw them like rabbits; they
        // used to be dropped at the feet with no speed and stayed floating there)
        p.weaponactive = -1;
        p.clothes.clear();
        p.clothestintr.clear();
        p.clothestintg.clear();
        p.clothestintb.clear();
    }
    p.exPoseReady = false;
    if (p.creature == want && p.exModelDir != dir) {
        p.exModelDir = dir; // another model of the same creature: its files, same animations
        p.skeletonLoad();
    }
    p.exModelDir = dir;
    if (p.creature != want) {
        p.changeCreatureType(want);
        if (wolf) {
            p.setProportions(1, 1, 1, 1);
        }
        // the level started this person on the other creature's animations (a wolf kept
        // playing the rabbit idle): start over on this creature's own idle
        p.animCurrent = p.animTarget = p.getIdle();
        p.frameCurrent = 0;
        p.frameTarget = 1;
        p.target = 0;
    }

    if (skin.empty()) {
        return;
    }
    if (skin.find('/') == std::string::npos) {
        skin = "Textures/" + skin;
    }
    // a skin made on another PC (online) may not exist here: keep the creature's own then
    if (FILE* f = fopen(Folders::getResourcePath(skin).c_str(), "rb")) {
        fclose(f);
    } else {
        printf("LugaruEX: skin %s not found here, using the default\n", skin.c_str());
        return;
    }
    const std::vector<std::string>& list = PersonType::types[want].skins;
    for (unsigned i = 0; i < list.size(); i++) {
        if (SDL_strcasecmp(list[i].c_str(), skin.c_str()) == 0) {
            p.whichskin = i;
        }
    }
    p.exSkinPath = skin;
    p.skeleton.drawmodel.textureptr.load(skin, 1, &p.skeleton.skinText[0], &p.skeleton.skinsize);
    p.addClothes(); // repaint clothes (and re-upload) on the new skin
    p.exPoseReady = false; // a reloaded skeleton needs its rotations again (online guests)
}


// ------------------------------------------------------------------ outfits
// The launcher's "Clothes" choice: made of the game's own clothing textures (shared/outfits.h,
// the same list the launchers show).
typedef OutfitDef Outfit;
static const std::vector<Outfit>& outfits()
{
    return Outfits();
}
// put an outfit on (key "" / "default" = keep the level's own clothes). Wolves wear none.
void ApplyOutfit(Person& p, const std::string& key)
{
    if (key.empty() || key == "default" || p.creature == wolftype) {
        return;
    }
    // "random": one of the outfits (a different one each level); "custom:<path>": the
    // player's own image ("New clothes..." in the launcher), painted over the skin as is
    Outfit custom;
    const Outfit* o = nullptr;
    std::string chosen = key;
    if (key == "random") {
        const std::vector<Outfit>& list = outfits();
        o = &list[1 + rand() % (list.size() - 1)]; // not "none"
        chosen = o->key;
    } else if (key.compare(0, 7, "custom:") == 0) {
        custom.key = "custom";
        custom.pieces.push_back({ nullptr, 1, 1, 1 });
        o = &custom;
    } else {
        for (const Outfit& x : outfits()) {
            if (key == x.key) {
                o = &x;
            }
        }
    }
    if (!o) {
        return;
    }
    p.clothes.clear();
    p.clothestintr.clear();
    p.clothestintg.clear();
    p.clothestintb.clear();
    for (const OutfitPiece& c : o->pieces) {
        p.clothes.push_back(c.tex ? std::string(c.tex) : key.substr(7)); // custom: the image's path
        p.clothestintr.push_back(c.r);
        p.clothestintg.push_back(c.g);
        p.clothestintb.push_back(c.b);
    }
    // a clean skin, then the new clothes painted on it
    const std::vector<std::string>& skins = PersonType::types[p.creature].skins;
    std::string skin = p.exSkinPath;
    if (skin.empty() && p.whichskin >= 0 && p.whichskin < (int)skins.size()) {
        skin = skins[p.whichskin];
    }
    if (!skin.empty()) {
        p.skeleton.drawmodel.textureptr.load(skin, 1, &p.skeleton.skinText[0], &p.skeleton.skinsize);
    }
    p.addClothes();
    p.exOutfit = chosen; // a random pick is sent on as the outfit it became
}

// "model:skin|outfit" -> the look and the outfit
static void splitOutfit(const std::string& spec, std::string& look, std::string& outfit)
{
    const size_t bar = spec.find('|');
    look = bar == std::string::npos ? spec : spec.substr(0, bar);
    outfit = bar == std::string::npos ? std::string() : spec.substr(bar + 1);
}
// a skin of this creature nobody in the level is wearing (kept while the same level is replayed)
static std::string randomSkin(Slot& s, int creature = rabbittype)
{
    const std::vector<std::string>& list = PersonType::types[creature].skins;
    if (list.empty()) {
        return std::string();
    }
    if (s.rndLevel == g_level && s.rndCreature == creature && s.rndSkin >= 0 && s.rndSkin < (int)list.size()) {
        return list[s.rndSkin];
    }
    std::vector<bool> used(list.size(), false);
    for (auto& p : Person::players) {
        if (p->creature == creature && p->whichskin >= 0 && p->whichskin < (int)list.size()) {
            used[p->whichskin] = true;
        }
    }
    for (auto& o : g_slots) {
        if (&o != &s && o.rndLevel == g_level && o.rndCreature == creature && o.rndSkin >= 0 && o.rndSkin < (int)list.size()) {
            used[o.rndSkin] = true;
        }
    }
    std::vector<int> free;
    for (size_t i = 0; i < list.size(); i++) {
        if (!used[i]) {
            free.push_back((int)i);
        }
    }
    if (free.empty()) {
        for (size_t i = 0; i < list.size(); i++) {
            free.push_back((int)i);
        }
    }
    s.rndSkin = free[rand() % free.size()];
    s.rndLevel = g_level;
    s.rndCreature = creature;
    return list[s.rndSkin];
}

static void applySlotCharacter(Slot& s, Person& p, bool tutorial)
{
    std::string look, outfit; // "model:skin|outfit"
    splitOutfit(s.ch, look, outfit);
    std::string model, skin;
    if (look == "random") {
        model = "rabbit";
        skin = randomSkin(s);
    } else if (look.empty() || look == "default") {
        ApplyOutfit(p, outfit); // the level's own look, maybe other clothes
        return;
    } else {
        parseChar(look, model, skin);
        if (skin == "random") { // "wolf:random", "rabbit:random"
            skin = randomSkin(s, (Looks_Base(model) == "wolf" && !tutorial) ? wolftype : rabbittype);
        }
    }
    ApplyCharacter(p, model, skin, tutorial);
    ApplyOutfit(p, outfit);
}

// player 1's look from the launcher's Character tab when its skin is "random"
void ApplyRandomLook(Person& p, const std::string& model, bool tutorial)
{
    if (g_slots.empty()) {
        return;
    }
    const std::string skin = randomSkin(g_slots[0], (Looks_Base(model) == "wolf" && !tutorial) ? wolftype : rabbittype);
    ApplyCharacter(p, model, skin, tutorial);
}

// ------------------------------------------------------------------ spawning
static void resetCamera(Slot& s, const Person& p)
{
    s.cam = Camera();
    s.cam.cameraloc = p.coords;
    s.cam.cameraloc.y += 5;
    s.cam.viewer = s.cam.cameraloc;
    s.cam.yaw = Person::players.empty() ? p.yaw : Game::yaw;
    s.cam.pitch = 0;
    s.pendYaw = s.pendPitch = 0;
    s.viewValid = false;
}

// can a new player stand at 'to'? Nothing solid between player 1's chest and there,
// and nothing over it up to a head and a half (not inside or under a box or platform)
static bool clearSpot(const XYZ& from, const XYZ& to, float h)
{
    for (const auto& o : Object::objects) {
        if (o->type == treeleavestype || o->type == bushtype || o->type == firetype) {
            continue; // nothing to bump into
        }
        XYZ a = from, b = to, hit;
        a.y += h;
        b.y += h;
        if (o->model.LineCheck(&a, &b, &hit, &o->position, &o->yaw) != -1) {
            return false;
        }
        a = to;
        b = to;
        a.y += 0.05f;
        b.y += h * 2.5f;
        if (o->model.LineCheck(&a, &b, &hit, &o->position, &o->yaw) != -1) {
            return false;
        }
        // and from high above down to the feet: a line from inside a rock or wall out of its
        // top isn't seen by the check above (faces only count from their outside), and a
        // wall taller than that line hid it completely
        b.y = to.y + 30.f;
        if (o->model.LineCheck(&b, &a, &hit, &o->position, &o->yaw) != -1) {
            return false;
        }
    }
    return true;
}

// something to stand on: the ground, or an object right under the feet (a platform)
static bool supportedSpot(const XYZ& at)
{
    if (at.y - terrain.getHeight(at.x, at.z) < .6f) {
        return true;
    }
    for (const auto& o : Object::objects) {
        if (o->type == treeleavestype || o->type == bushtype || o->type == firetype) {
            continue;
        }
        XYZ a = at, b = at, hit;
        a.y += .5f;
        b.y -= 1.2f;
        if (o->model.LineCheck(&a, &b, &hit, &o->position, &o->yaw) != -1) {
            return true;
        }
    }
    return false;
}

// a free spot near player 1 for another player: the slot's own place beside them first,
// then rings further out (on player 1's level, else on the ground); none at all: player 1's
// own spot. Never inside or under an object, never off the edge of a platform.
static XYZ spotNear(Person& p1, int slotIndex, bool log)
{
    static const float side[4][2] = { { 1.1f, 0 }, { -1.1f, 0 }, { 0, -1.3f }, { 1.1f, -1.3f } };
    const float h = std::max(.5f, p1.scale * 4); // about chest height
    std::vector<XYZ> tries;
    {
        XYZ o;
        o.x = side[(slotIndex + 3) & 3][0];
        o.y = 0;
        o.z = side[(slotIndex + 3) & 3][1];
        tries.push_back(DoRotation(o, 0, p1.yaw, 0));
    }
    for (float r : { 1.1f, 1.8f, 2.6f, 3.6f, 5.f, 7.f }) {
        for (int k = 0; k < 8; k++) {
            XYZ o;
            o.x = r * cosf((k * 45.f + slotIndex * 20.f) * 3.14159265f / 180.f);
            o.y = 0;
            o.z = r * sinf((k * 45.f + slotIndex * 20.f) * 3.14159265f / 180.f);
            tries.push_back(o);
        }
    }
    for (size_t k = 0; k < tries.size(); k++) {
        XYZ at = p1.coords + tries[k];
        const float ground = terrain.getHeight(at.x, at.z);
        // player 1 a little above the ground has not landed yet (levels start them in the
        // air): the others go on the ground; only well above it (a roof, a platform) on
        // player 1's level
        const bool p1High = p1.coords.y - terrain.getHeight(p1.coords.x, p1.coords.z) > 2.f;
        for (int level = p1High ? 0 : 1; level < 2; level++) {
            XYZ spot = at;
            if (level == 1 && !p1High) {
                spot.y = ground;
            } else if (level == 1) {
                if (spot.y - ground < .6f || spot.y - ground > 3.f) {
                    break; // (already on the ground, or player 1 is far above it)
                }
                spot.y = ground; // beside a low step player 1 stands on
            }
            spot.y = std::max(spot.y, ground);
            // nobody already standing there (a level's own character on the spot knocked the
            // new player over at once)
            bool crowded = false;
            for (const auto& o : Person::players) {
                const float dx = o->coords.x - spot.x, dz = o->coords.z - spot.z, dy = o->coords.y - spot.y;
                crowded = crowded || (o.get() != &p1 && dx * dx + dz * dz < (o->isPlayerControlled() ? 1.2f * 1.2f : 1.5f * 1.5f) && fabsf(dy) < 2.5f); // (a character sitting or lying takes more room)
            }
            if (!crowded && supportedSpot(spot) && clearSpot(p1.coords, spot, h)) {
                if (log && k > 0) {
                    printf("LugaruEX: player %d's usual spot is blocked - starts %.1f from player 1\n", slotIndex + 1, std::sqrt(tries[k].x * tries[k].x + tries[k].z * tries[k].z));
                }
                return spot;
            }
        }
    }
    if (log) {
        printf("LugaruEX: no free spot near player 1 - player %d starts on the same spot\n", slotIndex + 1);
    }
    return p1.coords;
}

// player 1 as last seen by LocalFrame (also set when players are put next to it: a scene
// that moves player 1 in the first step of a level is seen as a move)
static XYZ g_p1Last;
static const Person* g_p1Was = nullptr;

static void spawn(Slot& s, int slotIndex, bool tutorial)
{
    if (Person::players.empty()) {
        return;
    }
    Person& p1 = *Person::players[0];
    std::shared_ptr<Person> np(new Person());
    Person& p = *np;
    p.id = Person::players.size();
    // starts with the look the level gives its player (the "Default" look; the slot's own
    // choice goes on top in applySlotCharacter) - not player 1's, which may be player 1's
    // own launcher choice
    const bool lvl = g_levelCreature >= 0;
    p.creature = lvl ? (person_type)g_levelCreature : p1.creature;
    p.whichskin = lvl ? g_levelSkin : p1.whichskin;
    p.scale = lvl ? g_levelScale : p1.scale;

    // next to player 1 (spotNear: free, with something to stand on)
    p.coords = spotNear(p1, slotIndex, true);
    s.spawnAt = p.coords;
    s.following = true; // (kept beside player 1 until this player moves: see LocalFrame)
    g_p1Was = &p1;
    g_p1Last = p1.coords;
    p.oldcoords = p.realoldcoords = p.coords;
    p.yaw = p.targetyaw = p1.yaw;

    Person::players.push_back(np);
    p.skeletonLoad();
    if (p.creature != wolftype) {
        p.clothes = lvl ? g_levelClothes : p1.clothes;
        p.clothestintr = lvl ? g_levelTintR : p1.clothestintr;
        p.clothestintg = lvl ? g_levelTintG : p1.clothestintg;
        p.clothestintb = lvl ? g_levelTintB : p1.clothestintb;
    }
    p.addClothes();

    // same start state the level loader gives player 1
    p.velocity = 0;
    p.updatedelay = 0;
    p.normalsupdatedelay = 0;
    p.setProportions(1, 1, 1, 1);
    p.headless = 0;
    p.currentoffset = 0;
    p.targetoffset = 0;
    p.damagetolerance = lvl ? g_levelTolerance : p1.damagetolerance; // (the level's player, as for its look)
    p.power = p1.power;
    p.speedmult = p1.speedmult;
    p.speed = p1.speed;
    p.protectionhead = p1.protectionhead;
    p.protectionhigh = p1.protectionhigh;
    p.protectionlow = p1.protectionlow;
    p.armorhead = p1.armorhead;
    p.armorhigh = p1.armorhigh;
    p.armorlow = p1.armorlow;
    p.metalhead = p1.metalhead;
    p.metalhigh = p1.metalhigh;
    p.metallow = p1.metallow;
    p.tempanimation = Animation("Tempanim", lowheight, neutral);
    p.headmorphness = 0;
    p.targetheadmorphness = 1;
    p.headmorphstart = 0;
    p.headmorphend = 0;
    p.pausetime = 0;
    p.dead = 0;
    p.jumppower = 5;
    p.damage = 0;
    p.permanentdamage = 0;
    p.superpermanentdamage = 0;
    p.collided = -10;
    p.bloodloss = 0;
    p.weaponactive = -1;
    p.weaponstuck = -1;
    p.num_weapons = 0;
    p.bleeding = 0;
    p.deathbleeding = 0;
    p.stunned = 0;
    p.hasvictim = 0;
    p.wentforweapon = 0;
    p.immobile = 0;
    p.howactive = typeactive;
    p.aitype = playercontrolled;
    p.animCurrent = p.animTarget = bounceidleanim;
    p.frameCurrent = 0;
    p.frameTarget = 1;
    p.target = 0;
    p.loaded = true;

    applySlotCharacter(s, p, tutorial);
    if (p.creature == wolftype) {
        p.animCurrent = p.animTarget = p.getIdle();
    }

    s.person = (int)p.id;
    s.who = &p;
    resetCamera(s, p);
    printf("LugaruEX: local player %d joined as person %d\n", slotIndex + 1, s.person);
}

void LocalLevelLoaded(const std::string& levelName, bool tutorial)
{
    g_level = levelName;
    for (auto& s : g_slots) {
        s.person = -1;
        s.who = nullptr;
        s.spec = -1;
    }
    if (Person::players.empty()) {
        return;
    }
    g_slots[0].person = 0;
    g_slots[0].who = Person::players[0].get();
    if (NetIsGuest()) {
        // online guest: the host says who is who (LocalGuestEnter) and runs everyone
        LocalInvalidateViews();
        return;
    }
    if (g_slots[0].ch != "default" && !g_slots[0].ch.empty()) {
        applySlotCharacter(g_slots[0], *Person::players[0], tutorial);
    }
    { // every player is in the level, the tutorial too (each one does every step)
        for (size_t i = 1; i < g_slots.size(); i++) {
            if (!g_slots[i].removed) { // (kicked players don't come back)
                spawn(g_slots[i], (int)i, tutorial);
            }
        }
    }
    for (auto& p : Person::players) {
        p->exTarget = 0;
        p->exOldAttack = p->exRealAttack = false;
    }
    LocalInvalidateViews();
}

// ------------------------------------------------------------------ per frame
void LocalFrame(double dt)
{
    // drop-in: every connected controller is a player
    bool changed = false;
    for (int i = 0; i < PadCount(); i++) {
        const std::string id = PadIdAt(i);
        bool known = false;
        for (auto& s : g_slots) {
            known = known || s.padId == id;
        }
        if (known) {
            continue;
        }
        if (g_slots[0].padId.empty()) {
            g_slots[0].padId = id; // player 1 picks up the first controller
            continue;
        }
        if (localSlotCount() >= kMaxLocal) {
            continue;
        }
        Slot s;
        s.padId = id;
        s.ch = "random";
        g_slots.push_back(s);
        changed = true;
        printf("LugaruEX: controller %s dropped in as player %d\n", id.c_str(), (int)g_slots.size());
        if (Game::gameon && !Person::players.empty() && personValid(g_slots[0]) && !Game::loading && !NetIsGuest()) { // a guest's new player is made by the host
            spawn(g_slots.back(), (int)g_slots.size() - 1, Tutorial::active);
        }
    }
    if (changed) {
        autoLayout();
    }

    // Players who haven't moved yet stay with player 1 when the game puts player 1 somewhere
    // else (a level's opening scene moves player 1 to it: the others were left at the start,
    // often among the enemies of that scene). Once they move they are on their own.
    if (!NetIsGuest() && Game::gameon && !Game::loading && !Person::players.empty() && personValid(g_slots[0])) {
        Person& p1 = *Person::players[0];
        const bool jumped = g_p1Was == &p1 && distsq(&p1.coords, &g_p1Last) > 3.f * 3.f; // (not walking: put there)
        g_p1Was = &p1;
        g_p1Last = p1.coords;
        for (size_t i = 1; i < g_slots.size(); i++) {
            Slot& s = g_slots[i];
            if (!s.following || !personValid(s)) {
                continue;
            }
            Person& p = *Person::players[s.person];
            if (p.dead || p.skeleton.free || distsq(&p.coords, &s.spawnAt) > 1.5f * 1.5f) {
                s.following = false; // (moved, hit or fallen: playing now)
                printf("LugaruEX: player %d plays on from %.1f away from where it started (%s)\n", (int)i + 1, std::sqrt(distsq(&p.coords, &s.spawnAt)), p.dead ? "dead" : p.skeleton.free ? "knocked down" : "moved");
                continue;
            }
            if (jumped) {
                p.coords = p.oldcoords = p.realoldcoords = s.spawnAt = spotNear(p1, (int)i, false);
                p.yaw = p.targetyaw = p1.yaw;
                p.velocity = 0;
                s.viewValid = false;
                printf("LugaruEX: player %d moved along with player 1\n", (int)i + 1);
            }
        }
    }

    // right stick look for players 2..4 (player 1 goes through the mouse path)
    if (mainmenu || (Dialog::inDialog() && !Dialog::directing)) {
        return;
    }
    const float speed = PadLookSpeed();
    for (size_t i = 1; i < g_slots.size(); i++) {
        Slot& s = g_slots[i];
        PadInput in;
        if (s.remote || !personValid(s) || !PadRead(s.padId, in)) {
            continue;
        }
        s.pendYaw += in.lookX * 360.f * speed * (float)dt;
        s.pendPitch += in.lookY * 252.f * speed * (float)dt;
    }
}

// Dead players can watch the others: each fresh press of attack moves the view
// to the next player (living, or a body) and finally back to yourself. Your own
// look controls turn the camera; the player being watched isn't affected.
void SpectateNext(int slot);

// an online guest: every player's character, as the host's roster lists them
static std::vector<int> g_guestHumans;

// a human player: steered by a player, a player's seat here (split screen, an online
// guest on the host), or in the host's player list (on a guest)
static bool isHuman(int i)
{
    if (i < 0 || i >= (int)Person::players.size()) {
        return false;
    }
    return Person::players[i]->isPlayerControlled() || slotOfPerson(i) ||
           std::find(g_guestHumans.begin(), g_guestHumans.end(), i) != g_guestHumans.end();
}

// player 1's seat must always know its character (person 0; an online guest's own one):
// if it ever lost it, spectating and the camera quietly stuck to that body
static void relinkPlayerOne()
{
    if (g_slots.empty() || personValid(g_slots[0])) {
        return;
    }
    Slot& s = g_slots[0];
    const int want = NetIsGuest() ? s.person : 0;
    if (want >= 0 && want < (int)Person::players.size()) {
        s.person = want;
        s.who = Person::players[want].get();
        s.spec = -1;
        printf("LugaruEX: player 1's seat found its character again (person %d)\n", want);
    }
}

static void updateSpectate()
{
    relinkPlayerOne();
    for (size_t i = 0; i < g_slots.size(); i++) {
        Slot& s = g_slots[i];
        if (!personValid(s) || s.remote) { // online guests spectate on their own PC
            continue;
        }
        const Person& me = *Person::players[s.person];
        const bool btn = AttackKeyDown(s.person);
        const bool press = btn && !s.specBtn;
        s.specBtn = btn;
        if (!me.dead) {
            s.spec = -1;
            continue;
        }
        if (s.spec >= 0 && (s.spec >= (int)Person::players.size() || !isHuman(s.spec))) {
            s.spec = -1;
        }
        if (press) {
            SpectateNext((int)i);
        }
    }
}

void SpectateNext(int slot)
{
    if (slot < 0 || slot >= (int)g_slots.size() || !personValid(g_slots[slot])) {
        return;
    }
    Slot& s = g_slots[slot];
    // every player in the game (this PC's, split screen and online - an online guest
    // only has its own players in its slots), starting after whoever is watched now
    std::vector<int> order;
    for (size_t i = 0; i < Person::players.size(); i++) {
        if (isHuman((int)i) || (int)i == s.person) {
            order.push_back((int)i);
        }
    }
    if (order.size() < 2 && Person::players.size() > 1) {
        // nobody else to watch: say why (who counts as a player here), for bug reports
        std::string why;
        for (size_t i = 0; i < Person::players.size(); i++) {
            const Person& p = *Person::players[i];
            if (p.aitype == playercontrolled || slotOfPerson((int)i) || std::find(g_guestHumans.begin(), g_guestHumans.end(), (int)i) != g_guestHumans.end()) {
                why += " " + std::to_string(i) + (p.dead ? "(dead)" : "");
            }
        }
        printf("LugaruEX: player %d has nobody else to watch (players:%s; slot person %d)\n", slot + 1, why.c_str(), s.person);
    }
    const int cur = s.spec >= 0 ? s.spec : s.person;
    const auto it = std::find(order.begin(), order.end(), cur);
    const size_t at = it == order.end() ? 0 : (size_t)(it - order.begin());
    const int next = order.empty() ? s.person : order[(at + 1) % order.size()];
    s.spec = (next == s.person) ? -1 : next;
    printf("LugaruEX: player %d now watching person %d (%d players to watch)\n", slot + 1, s.spec >= 0 ? s.spec : s.person, (int)order.size());
}

void LocalSimLook()
{
    updateSpectate();
    for (size_t i = 1; i < g_slots.size(); i++) {
        Slot& s = g_slots[i];
        if (s.remote) {
            continue; // an online guest's camera angles arrive with their input
        }
        s.cam.yaw += s.pendYaw;
        s.cam.pitch += s.pendPitch;
        s.pendYaw = s.pendPitch = 0;
        s.cam.pitch = std::max(-70.f, std::min(90.f, s.cam.pitch));
    }
}

void LocalTickInput()
{
    const bool talking = Dialog::inDialog();
    for (size_t i = 1; i < g_slots.size(); i++) {
        Slot& s = g_slots[i];
        if (!personValid(s)) {
            continue;
        }
        Person& p = *Person::players[s.person];
        PadInput in;
        if (s.remote) {
            if (s.netConnected) {
                in = s.net; // online guest: their buttons, as last received
            }
        } else {
            PadRead(s.padId, in); // all released while the controller is unplugged
        }
        if (talking) {
            in = PadInput();
        }
        p.forwardkeydown = in.fwd;
        p.backkeydown = in.back;
        p.leftkeydown = in.left;
        p.rightkeydown = in.right;
        p.jumpkeydown = in.jump;
        p.crouchkeydown = in.crouch;
        p.drawkeydown = in.draw;
        p.throwkeydown = in.thrw;
        p.exWalk = in.walk;
        if (!p.jumpkeydown) {
            p.jumpclimb = 0;
            p.jumptogglekeydown = 0;
        }
        if (p.jumpkeydown && p.animTarget != jumpupanim && p.animTarget != jumpdownanim && !p.isFlip()) {
            p.jumptogglekeydown = 1;
        }
    }
}

// ------------------------------------------------------------------ cameras
static void swapIn(const Camera& c)
{
    viewer = c.viewer;
    viewerfacing = c.viewerfacing;
    cameraloc = c.cameraloc;
    cameradist = c.cameradist;
    Game::yaw = c.yaw;
    Game::pitch = c.pitch;
    camerashake = c.shake;
    woozy = c.woozy;
    blackout = c.blackout;
}

static void swapOut(Camera& c)
{
    c.viewer = viewer;
    c.viewerfacing = viewerfacing;
    c.cameraloc = cameraloc;
    c.cameradist = cameradist;
    c.yaw = Game::yaw;
    c.pitch = Game::pitch;
    c.shake = camerashake;
    c.woozy = woozy;
    c.blackout = blackout;
}

void LocalCameras(void (*camera)())
{
    if (g_slots.size() < 2) {
        return;
    }
    Camera p1;
    swapOut(p1);
    for (size_t i = 1; i < g_slots.size(); i++) {
        Slot& s = g_slots[i];
        if (!personValid(s) || s.remote) { // online guests run their camera on their own PC
            continue;
        }
        if (Dialog::inDialog() && !Dialog::directing) {
            s.cam = p1; // everyone watches the cutscene camera
            continue;
        }
        swapIn(s.cam);
        g_view = (int)i;
        camera();
        swapOut(s.cam);
    }
    g_view = 0;
    swapIn(p1);
}

// Split screen audio: one "ear" per player camera; every positional sound is
// heard from the nearest one (see OPENAL_SetEars). Single player: normal listener.
void LocalAudioEars()
{
    float pos[kMaxLocal][3], ori[kMaxLocal][6];
    int n = 0;
    if (LocalSplit()) {
        for (size_t i = 0; i < g_slots.size() && n < kMaxLocal; i++) {
            const Slot& s = g_slots[i];
            if (!personValid(s) || s.remote) {
                continue;
            }
            // player 1's camera lives in the globals; the others in their slot
            const XYZ v = (i == 0) ? viewer : s.cam.viewer;
            const float yw = (i == 0) ? Game::yaw : s.cam.yaw, pt = (i == 0) ? Game::pitch : s.cam.pitch;
            XYZ f, u;
            f = 0;
            f.z = -1;
            u = f;
            f = DoRotation(DoRotation(f, -pt, 0, 0), 0, -yw, 0);
            u = DoRotation(DoRotation(u, -pt + 90, 0, 0), 0, -yw, 0);
            // the same conventions the game uses for its listener
            pos[n][0] = v.x, pos[n][1] = v.y, pos[n][2] = v.z;
            ori[n][0] = -f.x, ori[n][1] = f.y, ori[n][2] = -f.z;
            ori[n][3] = -u.x, ori[n][4] = u.y, ori[n][5] = -u.z;
            n++;
        }
    }
    OPENAL_SetEars(n, pos, ori);
}

void LocalCaptureViews()
{
    for (auto& s : g_slots) {
        if (!s.viewValid) {
            s.prevViewer = s.curViewer = s.cam.viewer;
            s.viewValid = true;
        } else {
            s.prevViewer = s.curViewer;
            s.curViewer = s.cam.viewer;
        }
    }
}

void LocalInvalidateViews()
{
    for (auto& s : g_slots) {
        s.viewValid = false;
    }
}

int ViewPerson()
{
    if (g_view < 0 || g_view >= (int)g_slots.size() || !personValid(g_slots[g_view])) {
        return 0;
    }
    const Slot& s = g_slots[g_view];
    if (s.spec >= 0 && s.spec < (int)Person::players.size()) {
        return s.spec; // a dead player watching someone else
    }
    return s.person;
}

int ViewSlot()
{
    return g_view;
}

int ViewX()
{
    return g_vx;
}

int ViewY()
{
    return g_vy;
}

bool LocalSplit()
{
    return activeCount() > 1;
}

// ------------------------------------------------------------------ drawing
static bool g_drawingViews = false;
static float g_viewArea = 1;   // share of the window the current view covers
static int g_biggestView = 0;  // slot that shows the shared score
static int g_bottomLeftView = 0; // slot that shows the lighting look name
static float g_fullH = 0;      // window height while views are drawn

bool DrawingViews()
{
    return g_drawingViews;
}

float SharedHudHeight()
{
    return (g_drawingViews && g_fullH > 0) ? g_fullH : screenheight;
}

bool DrawSharedHud()
{
    return !g_drawingViews || g_view == g_biggestView;
}

bool DrawBottomLeftHud()
{
    return !g_drawingViews || g_view == g_bottomLeftView;
}

float ViewFovScale()
{
    if (!g_drawingViews) {
        return 1.f;
    }
    // smaller views zoom in a little; very wide strips trim some height so
    // they don't turn into a fisheye (they stay wide)
    float k = 1.f - 0.16f * (1.f - sqrtf(std::max(0.f, std::min(1.f, g_viewArea))));
    const float ar = screenwidth / std::max(1.f, screenheight);
    if (ar > 16.f / 9.f) {
        k *= powf((16.f / 9.f) / ar, 0.6f);
    }
    return k;
}

void DrawViews(float alpha, void (*draw)())
{
    if (!LocalSplit() || mainmenu || !Game::gameon) {
        draw();
        return;
    }
    const float fullW = screenwidth, fullH = screenheight;
    g_fullH = fullH;
    const float savedMult = multiplier;
    Camera p1;
    swapOut(p1); // player 1's camera, already blended by the interpolation

    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    // the shared score always goes in the top-left view
    float best = 1e9f;
    for (size_t i = 0; i < g_slots.size(); i++) {
        const float k = g_slots[i].ry * 10.f + g_slots[i].rx;
        if (personValid(g_slots[i]) && !g_slots[i].remote && k < best - 1e-4f) {
            best = k;
            g_biggestView = (int)i;
        }
    }
    // the lighting look's name goes in the bottom-left view (the window's bottom-left corner)
    best = 1e9f;
    for (size_t i = 0; i < g_slots.size(); i++) {
        const float k = -(g_slots[i].ry + g_slots[i].rh) * 10.f + g_slots[i].rx;
        if (personValid(g_slots[i]) && !g_slots[i].remote && k < best - 1e-4f) {
            best = k;
            g_bottomLeftView = (int)i;
        }
    }

    g_drawingViews = true;
    bool first = true;
    for (size_t i = 0; i < g_slots.size(); i++) {
        Slot& s = g_slots[i];
        if (!personValid(s) || s.remote) { // online guests have their own screen
            continue;
        }
        // pixel rectangle; views share their edges (no seams)
        const int x0 = (int)lroundf(s.rx * fullW), x1 = (int)lroundf((s.rx + s.rw) * fullW);
        const int t0 = (int)lroundf(s.ry * fullH), t1 = (int)lroundf((s.ry + s.rh) * fullH);
        if (x1 - x0 < 16 || t1 - t0 < 16) {
            continue;
        }
        g_viewArea = (float)(x1 - x0) * (t1 - t0) / std::max(1.f, fullW * fullH);
        g_vx = x0;
        g_vy = (int)fullH - t1; // GL origin is bottom-left
        screenwidth = (float)(x1 - x0);
        screenheight = (float)(t1 - t0);
        glEnable(GL_SCISSOR_TEST);
        glScissor(g_vx, g_vy, x1 - x0, t1 - t0);

        g_view = (int)i;
        if (i == 0 || (Dialog::inDialog() && !Dialog::directing)) {
            swapIn(p1);
        } else {
            Camera c = s.cam;
            if (s.viewValid) {
                const XYZ& a = s.prevViewer;
                const XYZ& b = s.curViewer;
                const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
                if (dx * dx + dy * dy + dz * dz < 25 * 25) {
                    c.viewer.x = a.x + dx * alpha;
                    c.viewer.y = a.y + dy * alpha;
                    c.viewer.z = a.z + dz * alpha;
                }
            }
            c.yaw += s.pendYaw;
            c.pitch = std::max(-70.f, std::min(90.f, c.pitch + s.pendPitch));
            swapIn(c);
        }
        // effects that advance while drawing (particles, flashes) must only advance once per frame
        multiplier = first ? savedMult : 0;
        first = false;
        draw();
        // the death / blood dim fades in while drawing: keep it for this view
        if (i == 0) {
            p1.blackout = blackout;
        } else {
            // later views draw with the frame time at 0 (particles must not advance
            // twice), so advance their fade here the way the HUD code does
            const Person& watched = *Person::players[ViewPerson()];
            float b = blackout;
            if (watched.dead) {
                b += savedMult * 3;
                if (watched.dead == 1) {
                    b = .4f;
                }
                if (watched.dead == 2 && b > .6f) {
                    b = .6f;
                }
            }
            s.cam.blackout = b;
        }
    }

    g_drawingViews = false;
    glDisable(GL_SCISSOR_TEST);
    screenwidth = fullW;
    screenheight = fullH;
    g_vx = g_vy = 0;
    g_view = 0;
    multiplier = savedMult;
    swapIn(p1);
    glViewport(0, 0, (int)fullW, (int)fullH);
    swap_gl_buffers(); // one present for the whole split-screen frame
}

// ------------------------------------------------------------------ rules
float YawOf(int person)
{
    if (person == 0) {
        return Game::yaw;
    }
    Slot* s = slotOfPerson(person);
    return s ? s->cam.yaw : 0.f;
}

float PitchOf(int person)
{
    if (person == 0) {
        return Game::pitch;
    }
    Slot* s = slotOfPerson(person);
    return s ? s->cam.pitch : 0.f;
}

bool PlayerOneHere(int person)
{
    if (NetIsGuest()) {
        return !g_slots.empty() && personValid(g_slots[0]) && g_slots[0].person == person;
    }
    return person == 0;
}

bool AttackKeyDown(int person)
{
    if (person == 0 || (!g_slots.empty() && person == g_slots[0].person)) {
        // test hook LUGARUEX_TEST_SPECBTN: a dead player 1 taps attack every 1.5 s
        static const bool specBtn = getenv("LUGARUEX_TEST_SPECBTN") != nullptr;
        if (specBtn && person >= 0 && person < (int)Person::players.size() && Person::players[person]->dead) {
            return (SDL_GetTicks() / 750) % 2 == 0;
        }
        // player 1 on this PC (on an online guest that is their own character);
        // in online play the game runs on behind a menu, so menu clicks don't count
        return !mainmenu && Input::isKeyDown(Game::attackkey);
    }
    Slot* s = slotOfPerson(person);
    if (!s) {
        return false;
    }
    if (s->remote) {
        return s->netConnected && s->net.attack;
    }
    PadInput in;
    PadRead(s->padId, in);
    return in.attack;
}

// ------------------------------------------------------------------ dialogues
static std::vector<char> g_talkPrev, g_talkPress;
static int g_dlgOwner = 0;

void DialogInputStep()
{
    const size_t n = Person::players.size();
    g_talkPrev.resize(n, 0);
    g_talkPress.assign(n, 0);
    for (size_t i = 0; i < n; i++) {
        const bool down = Person::players[i]->isPlayerControlled() && AttackKeyDown((int)i);
        g_talkPress[i] = down && !g_talkPrev[i];
        g_talkPrev[i] = down;
    }
}

bool TalkPressed(int person)
{
    return person >= 0 && person < (int)g_talkPress.size() && g_talkPress[person];
}

void DialogStarted(int owner)
{
    g_dlgOwner = owner;
    printf("LugaruEX: dialogue %d starts, person %d clicks through it\n", Dialog::whichdialogue, DialogOwner());
}

int DialogOwner()
{
    const int o = g_dlgOwner;
    if (o <= 0 || o >= (int)Person::players.size() || !isHuman(o)) {
        return 0;
    }
    const Slot* s = slotOfPerson(o);
    if (!s || (s->remote && !s->netConnected)) {
        return 0; // they left: back to player 1 / the host
    }
    return o;
}

bool DialogAdvancePressed()
{
    return TalkPressed(DialogOwner());
}

bool HiddenInScene(int person)
{
    if (person <= 0 || person >= (int)Person::players.size() || !Dialog::inDialog() || Dialog::directing ||
        !Person::players[person]->isPlayerControlled()) {
        return false; // player 1 is in every scene (as in the original); characters of the level stay
    }
    for (const DialogScene& s : Dialog::currentDialog().scenes) {
        if (s.participantfocus == person) {
            return false; // the scene does use this one
        }
    }
    return true;
}

int AITarget(Person& self)
{
    const int n = (int)Person::players.size();
    int cur = self.exTarget;
    if (cur < 0 || cur >= n || !Person::players[cur]->isPlayerControlled()) {
        cur = 0;
    }
    if (humanCount() < 2) {
        self.exTarget = 0;
        return 0;
    }
    auto d2 = [&](int h) {
        const XYZ& a = self.coords;
        const XYZ& b = Person::players[h]->coords;
        return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
    };
    if (Tutorial::active) {
        // the sparring partner goes after whoever still has to do this step
        int pick = -1;
        float pickD = 1e30f;
        for (auto& s : g_slots) {
            if (!personValid(s) || Person::players[s.person]->dead || Tutorial::DoneBy(s.person)) {
                continue;
            }
            const float d = d2(s.person);
            if (d < pickD) {
                pick = s.person;
                pickD = d;
            }
        }
        if (pick >= 0) {
            self.exTarget = pick;
            return pick;
        }
    }
    int best = cur;
    float bestD = Person::players[cur]->dead ? 1e30f : d2(cur);
    for (auto& s : g_slots) {
        if (!personValid(s) || s.person == cur) {
            continue;
        }
        const Person& h = *Person::players[s.person];
        if (h.dead) {
            continue;
        }
        const float d = d2(s.person);
        if (d < bestD * 0.6f) { // only switch to someone clearly closer
            best = s.person;
            bestD = d;
        }
    }
    self.exTarget = best;
    return best;
}

bool AllHumansDead()
{
    if (Person::players.empty()) {
        return false;
    }
    for (auto& p : Person::players) {
        if (p->isPlayerControlled() && !p->dead) {
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------------------ online play
int LocalAddRemote(unsigned long long token, const std::string& character)
{
    for (size_t i = 0; i < g_slots.size(); i++) {
        if (g_slots[i].remote && g_slots[i].token == token) {
            g_slots[i].netConnected = true; // back after a drop-out: same seat, same character
            return (int)i;
        }
    }
    int remotes = 0;
    for (auto& s : g_slots) {
        remotes += s.remote ? 1 : 0;
    }
    // no player cap: only the network's one-byte person index (255 characters with the
    // level's own) limits how many can join
    if (remotes >= 190 || Person::players.size() + 1 >= 250) {
        return -1;
    }
    Slot s;
    s.remote = true;
    s.token = token;
    s.ch = character.empty() ? "default" : lower(character);
    s.netConnected = true;
    g_slots.push_back(s);
    const int idx = (int)g_slots.size() - 1;
    if (Game::gameon && !Person::players.empty() && personValid(g_slots[0]) && !Game::loading) {
        spawn(g_slots[idx], idx, Tutorial::active); // joins the level that is running
    }
    return idx;
}

void LocalRemoteInput(int slot, const PadInput& in, float yaw, float pitch, bool connected)
{
    if (slot < 0 || slot >= (int)g_slots.size() || !g_slots[slot].remote) {
        return;
    }
    Slot& s = g_slots[slot];
    s.net = in;
    s.netConnected = connected;
    if (connected) {
        s.cam.yaw = yaw;
        s.cam.pitch = std::max(-70.f, std::min(90.f, pitch));
    }
}

bool LocalRemoteToEnemy(int slot)
{
    if (slot < 0 || slot >= (int)g_slots.size() || !g_slots[slot].remote || g_slots[slot].removed) {
        return false;
    }
    Slot& s = g_slots[slot];
    if (personValid(s)) {
        // the kicked player's character turns on everyone: the game's own AI takes it over
        Person& p = *Person::players[s.person];
        p.aitype = attacktypecutoff;
        p.exTarget = 0;
        printf("LugaruEX: person %d (a kicked player) is an enemy now\n", s.person);
    }
    s.person = -1;
    s.who = nullptr;
    s.removed = true;
    s.token = 0; // (a later join gets a new seat)
    s.netConnected = false;
    s.net = PadInput();
    return true;
}

int LocalSlotPerson(int slot)
{
    if (slot < 0 || slot >= (int)g_slots.size() || !personValid(g_slots[slot])) {
        return -1;
    }
    return g_slots[slot].person;
}

bool RemotePerson(int person)
{
    for (auto& s : g_slots) {
        if (s.remote && personValid(s) && s.person == person) {
            return true;
        }
    }
    return false;
}

void LocalRoster(std::vector<NetLook>& out)
{
    out.clear();
    for (size_t i = 0; i < Person::players.size(); i++) {
        const Person& p = *Person::players[i];
        if (p.aitype == playercontrolled) {
            NetLook l{ (int)i, (int)p.creature, p.whichskin, p.exSkinPath, p.exOutfit, std::string() };
            if (p.exModelDir.compare(0, 14, "Models/Custom/") == 0 && p.exModelDir.size() > 15) {
                l.model = p.exModelDir.substr(14, p.exModelDir.size() - 15); // "rabbit/Name"
            }
            out.push_back(l);
        }
    }
}

void LocalGuestEnter(const std::vector<int>& mine, const std::vector<NetLook>& roster, bool force)
{
    g_guestHumans.clear(); // every player in the game, as the host lists them
    for (const NetLook& l : roster) {
        g_guestHumans.push_back(l.person);
    }
    for (const NetLook& l : roster) {
        if (l.person > (int)Person::players.size()) {
            break; // the host's characters always follow the level's own ones
        }
        if (l.person == (int)Person::players.size()) {
            Slot tmp; // a stand-in for another player; the host moves it from now on
            spawn(tmp, l.person & 3, Tutorial::active);
        }
        Person& p = *Person::players[l.person];
        const int cr = (l.creature == wolftype) ? wolftype : rabbittype;
        const std::vector<std::string>& skins = PersonType::types[cr].skins;
        std::string skin = l.skin;
        if (skin.empty() && l.whichskin >= 0 && l.whichskin < (int)skins.size()) {
            skin = skins[l.whichskin];
        }
        const std::string model = l.model.empty() ? std::string(cr == wolftype ? "wolf" : "rabbit") : l.model;
        const std::string wantDir = Tutorial::active ? std::string() : Looks_ModelDir(l.model);
        if (force || p.creature != cr || p.whichskin != l.whichskin || p.exSkinPath != l.skin || p.exModelDir != wantDir) {
            ApplyCharacter(p, model, skin, Tutorial::active); // tutorial: rabbits only
        }
        if (!l.outfit.empty() && (force || p.exOutfit != l.outfit)) {
            ApplyOutfit(p, l.outfit); // that player's Clothes choice
        }
        p.aitype = playercontrolled;
    }
    // this PC's players, in order, get the characters the host made for them
    for (size_t k = 0; k < g_slots.size(); k++) {
        Slot& s = g_slots[k];
        const int me = k < mine.size() ? mine[k] : -1;
        if (me >= 0 && me < (int)Person::players.size()) {
            if (s.person != me || s.who != Person::players[me].get()) {
                s.spec = -1;
                if (k > 0) {
                    resetCamera(s, *Person::players[me]); // start the camera at the character
                }
            }
            s.person = me;
            s.who = Person::players[me].get();
        } else if (k > 0) {
            s.person = -1; // the host hasn't added this player yet
            s.who = nullptr;
        }
    }
    autoLayout();
    LocalInvalidateViews();
}

int LocalPlayersHere()
{
    return localSlotCount();
}

std::string LocalCharacterOf(int k)
{
    if (k < 0 || k >= (int)g_slots.size()) {
        return "random";
    }
    const Slot& s = g_slots[k];
    if (k == 0 && (s.ch.empty() || s.ch == "default")) {
        // player 1: the launcher's Character tab. Default = the level's own look (the
        // launcher now starts on Random instead); "random" skin = one nobody wears
        if (cfg.playerDefault) {
            return "default";
        }
        const std::string outfit = (cfg.playerClothes.empty() || cfg.playerClothes == "default") ? "" : "|" + cfg.playerClothes;
        return cfg.playerModel + ":" + cfg.playerSkin + outfit;
    }
    return s.ch.empty() ? std::string("default") : s.ch;
}

void LocalInputOf(int k, PadInput& in, float& yaw, float& pitch)
{
    in = PadInput();
    yaw = pitch = 0;
    if (k < 0 || k >= (int)g_slots.size()) {
        return;
    }
    const Slot& s = g_slots[k];
    if (k == 0) {
        yaw = Game::yaw;
        pitch = Game::pitch;
        if (!mainmenu) { // keyboard, mouse and player 1's controller
            in.fwd = Input::isKeyDown(Game::forwardkey);
            in.back = Input::isKeyDown(Game::backkey);
            in.left = Input::isKeyDown(Game::leftkey);
            in.right = Input::isKeyDown(Game::rightkey);
            in.jump = Input::isKeyDown(Game::jumpkey);
            in.crouch = Input::isKeyDown(Game::crouchkey);
            in.attack = Input::isKeyDown(Game::attackkey);
            in.draw = Input::isKeyDown(Game::drawkey);
            in.thrw = Input::isKeyDown(Game::throwkey);
            in.walk = P1Walk();
        }
        return;
    }
    yaw = s.cam.yaw;
    pitch = s.cam.pitch;
    if (!mainmenu) {
        PadRead(s.padId, in);
    }
}
}

namespace EX
{
// vibration for whoever plays this person: a controller here, or an online guest's
void Rumble(int person, float strength, int ms)
{
    if (person < 0 || strength <= 0) {
        return;
    }
    static const bool log = getenv("LUGARUEX_TEST_RUMBLELOG") != nullptr;
    if (log) {
        printf("RUMBLE person %d strength %.2f %d ms\n", person, strength, ms);
    }
    for (size_t i = 0; i < g_slots.size(); i++) {
        Slot& s = g_slots[i];
        const bool mine = personValid(s) ? s.person == person : (i == 0 && person == 0 && !NetIsGuest());
        if (!mine) {
            continue;
        }
        if (s.remote) {
            NetRumbleEvent(person, strength, ms);
        } else {
            PadRumble(s.padId, strength, ms);
        }
        return;
    }
}
}

namespace EX
{
// tutorial "look around" step for players 2+: has their camera turned since the last check
bool LocalLooking(int person)
{
    for (auto& s : g_slots) {
        if (!personValid(s) || s.person != person) {
            continue;
        }
        const bool moved = fabsf(s.cam.yaw - s.lookYaw) > 0.05f || fabsf(s.cam.pitch - s.lookPitch) > 0.05f;
        s.lookYaw = s.cam.yaw;
        s.lookPitch = s.cam.pitch;
        return moved;
    }
    return false;
}
}

namespace EX
{
void LocalReapplyRemoteLooks()
{
    for (Slot& s : g_slots) {
        if (s.remote && !s.removed && personValid(s)) {
            applySlotCharacter(s, *Person::players[s.person], Tutorial::active);
        }
    }
}
} // namespace EX
