/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "EX/ExMods.hpp"

#include "EX/ExConfig.hpp"
#include "EX/ExResource.hpp"

#include "fsutil.h"
#include "modpack.h"

#include <cstdio>

namespace EX
{
static std::vector<std::string> g_local, g_session, g_roots;
static bool g_sessionActive = false;

std::string ModsFolder()
{
    return ExFolder() + "Mods/";
}

static void rebuild()
{
    g_roots = g_sessionActive ? g_session : g_local;
    ResourceCacheClear();
}

void ModsLoad()
{
    g_local.clear();
    const std::vector<ModEntry> list = Mod_ParseList(IniString("Mods", "List", ""));
    for (const ModEntry& e : list) {
        const std::string dir = ModsFolder() + e.folder;
        if (e.on && fsu::IsDir(dir)) {
            g_local.push_back(dir);
            printf("LugaruEX: mod on: %s\n", e.folder.c_str());
            for (const std::string& c : fsu::List(dir + "/Campaigns", false)) {
                printf("LugaruEX: mod campaign: %s\n", c.c_str());
            }
        }
    }
    rebuild();
}

const std::vector<std::string>& ModRoots()
{
    return g_roots;
}

std::vector<std::string> ModsLocalEnabled()
{
    std::vector<std::string> v;
    for (const std::string& r : g_local) {
        v.push_back(fsu::BaseName(r));
    }
    return v;
}

void ModsSetSession(const std::vector<std::string>& roots, bool active)
{
    if (active == g_sessionActive && roots == g_session) {
        return;
    }
    g_session = roots;
    g_sessionActive = active;
    printf("LugaruEX: %s\n", active ? (roots.empty() ? "playing the host's game (no mods)" : "playing with the host's mods") : "back to this PC's own mods");
    rebuild();
}
}
