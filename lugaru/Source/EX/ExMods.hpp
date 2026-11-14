/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef _EX_MODS_HPP_
#define _EX_MODS_HPP_

#include <string>
#include <vector>

// Mods: folders in LugaruEX/Mods/ laid out like Data/, switched on and ordered in the
// launcher's Mods tab ([Mods] List). Their files win over the game's own (and over
// texture overrides); the first mod in the list wins over the ones below it.
namespace EX
{
void ModsLoad(); // reads the list (once at start)
// the folders searched before Data, first = highest priority. Online, a guest uses the
// host's mods (downloaded) instead of its own, so both play the same game.
const std::vector<std::string>& ModRoots();
void ModsSetSession(const std::vector<std::string>& roots, bool active); // guest: the host's mods
std::vector<std::string> ModsLocalEnabled(); // this PC's mod folders that are on, in order
std::string ModsFolder();                   // LugaruEX/Mods/
// online guest: a folder of the host's own copies of levels (and terrain) that differ from
// this PC's, searched before everything else; "" when not playing as a guest
void ModsSetSyncRoot(const std::string& dir);
void ModsRescan(); // files were added to a root: look again
}

#endif
