// LugaruEX - installing: finding the game, copying LugaruEX in, shortcuts, the free
// game data, and putting updates in place (GPLv2+). Shared by both launchers.
//
// A LugaruEX package is a folder holding LugaruEX/ (the game, its files) and, on Windows,
// Lugaru.exe (the launcher). The launcher is installed as <game>/LugaruEX/LugaruEX
// Launcher.exe (Linux: lugaruex-launcher) and, for Steam's Lugaru HD on Windows, also as
// <game>/Lugaru.exe so Steam's Play button opens it (the original is kept in
// <game>/LugaruEX/backup/).
#pragma once
#include <string>
#include <vector>

struct GameFolder
{
    std::string path;     // the folder with Data/ in it
    std::string kind;     // "Lugaru HD (Steam)", "Lugaru (open source)", ...
    bool steam = false;   // Steam's copy (its Play button can open LugaruEX)
    bool writable = true; // false: a system folder (Linux) or Program Files without admin rights
    bool hasLugaruEX = false;
    std::string exVersion; // the LugaruEX version installed there ("" = none, "?" = unknown)
    bool ownCopy = false;  // a free game copy LugaruEX downloaded: the whole folder may be deleted
};

bool Setup_IsGameFolder(const std::string& dir);            // has Data/Maps
std::vector<GameFolder> Setup_FindGames();                   // Steam libraries, open-source installs, common folders
GameFolder Setup_Describe(const std::string& dir);           // a folder the player picked
std::string Setup_FreeGameFolder();                         // where the free data goes: user's own folder
const char* Setup_FreeDataUrl();                            // OSS Lugaru's data (CC-BY-SA), as a zip

struct SetupOptions
{
    bool steamPlayButton = true; // Windows, Steam: the Play button opens LugaruEX
    bool shortcut = true;        // desktop / app menu entry
};

// copy the package into the game folder (an update: the same, over an installation).
// Settings, mods and saves are never touched. launcher = the installed launcher's path.
bool Setup_Install(const std::string& packageDir, const GameFolder& game, const SetupOptions& opt, std::string& launcher, std::string& error);
// unpack a release package (the .zip / .tar.gz) into dir
bool Setup_Unpack(const std::string& package, const std::string& dir, std::string& error);
// the free data zip (from Setup_FreeDataUrl) -> dir/Data
bool Setup_UnpackFreeData(const std::string& zip, const std::string& dir, std::string& error);
// Linux: a game folder for read-only (system) game data: dir/Data -> the system's data
bool Setup_LinkSystemData(const std::string& systemData, const std::string& dir, std::string& error);

// a free game copy LugaruEX downloaded (marks the folder, so it can be deleted later)
void Setup_MarkOwnCopy(const std::string& dir);
// remove LugaruEX from a game folder (<game>/LugaruEX: its settings, mods and screenshots go
// too; on Windows Steam's Lugaru.exe comes back), or with wholeFolder a free copy entirely
bool Setup_Remove(const GameFolder& game, bool wholeFolder, std::string& error);
// compare "0.4.12" style versions: <0, 0, >0
int Setup_CompareVersions(const std::string& a, const std::string& b);

// Steam: the launcher as a non-Steam game in every Steam account on this PC.
// 0 added, 1 already there, 2 Steam is running (it would overwrite the list: close it first), -1 error
int Setup_AddToSteam(const std::string& launcher, const std::string& gameDir, std::string& error);
bool Setup_InSteam(const std::string& launcher); // a Steam account here has it as a non-Steam game
bool Setup_SteamRunning();
bool Setup_CloseSteam(); // asks Steam to exit and waits (up to 30 s)
void Setup_StartSteam();

void Setup_CleanOld(const std::string& exePath); // removes <exe>.old left by an update
std::string Setup_SelfPath();                    // this program's full path
std::string Setup_LauncherName();                // "LugaruEX Launcher.exe" / "lugaruex-launcher"
