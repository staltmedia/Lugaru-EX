// LugaruEX - mods: installing zips, the mod list, packing for online sharing (GPLv2+)
// Shared by the game and both launchers.
//
// A mod is a folder in <game>/LugaruEX/Mods/<name>/ laid out like the game's Data folder
// (Textures/, Maps/, Sounds/, Models/, Campaigns/, ...), plus an optional mod.txt
// (name=, author=, version=, description=). Only data files are ever unpacked.
// Which mods are on, and their order (the first one wins when two change the same
// file), is the ini's [Mods] List=+First Mod|-Switched Off|+Another.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ModInfo
{
    std::string folder;       // folder name in Mods/ (also its id in the list)
    std::string name;         // from mod.txt, else the folder name
    std::string author, version, description;
    uint64_t bytes = 0;
    int files = 0;
};

struct ModEntry // one item of the [Mods] List
{
    std::string folder;
    bool on = true;
};

// unpack a mod zip into modsDir/<name>/ (a new folder: name from mod.txt or the zip's file name).
// Rejects zips with no game data in them and anything outside the mod's folder; skips
// files that are not game data (programs, scripts). folderOut = the new folder's name.
bool Mod_InstallZip(const std::string& zipPath, const std::string& modsDir, std::string& folderOut, std::string& error);
// unpack a zip made by Mod_PackFolder (online sharing) into destDir, with the same checks
// (maxBytes: a smaller limit on the unpacked size than a mod's, e.g. for players' looks; 0 = the mod limit)
bool Mod_ExtractTo(const std::string& zipPath, const std::string& destDir, std::string& error, uint64_t maxBytes = 0);
// zip a mod folder for sending (files in name order, no dates: same folder = same bytes)
bool Mod_PackFolder(const std::string& folder, const std::string& zipPath);
uint64_t Mod_HashFile(const std::string& path); // FNV-1a 64 of the bytes, 0 if unreadable
uint64_t Mod_Fingerprint(const std::string& folder); // the mod's game files (names + bytes): same mod, same number

std::vector<ModInfo> Mod_List(const std::string& modsDir); // installed mods (folders not starting with '.')
bool Mod_Delete(const std::string& modsDir, const std::string& folder);

// [Mods] List value <-> entries; Mod_Order keeps the saved order, adds new mods at the
// top (switched on) and drops ones that are gone
std::vector<ModEntry> Mod_ParseList(const std::string& value);
std::string Mod_FormatList(const std::vector<ModEntry>& list);
std::vector<ModEntry> Mod_Order(const std::vector<ModEntry>& saved, const std::vector<ModInfo>& installed);

std::string Mod_SafeName(const std::string& name); // ASCII letters, digits, space, - and _
