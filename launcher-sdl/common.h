// LugaruEX launcher for Linux (SDL2 + Dear ImGui) - shared helpers (GPLv2+)
#pragma once
#include <map>
#include <string>
#include <vector>

// ---- files (UTF-8 paths, forward slashes)
bool FileExists(const std::string& p);
bool DirExists(const std::string& p);
bool MakeDirs(const std::string& p);  // like mkdir -p
std::vector<std::string> ListDir(const std::string& dir, const std::string& ext); // file names ending in ext (any case)
// dir + name, with the name matched without regard to case (Linux file names are case-sensitive,
// the game's are not); "" if not there
std::string FindFileCI(const std::string& dir, const std::string& name);
std::string Lower(std::string s);
std::string Trim(const std::string& s);

// ---- ini: section.key (lower case) -> value; ReadIni keeps every section in file order
struct Ini
{
    std::map<std::string, std::string> kv;
    std::vector<std::pair<std::string, std::vector<std::string>>> sections; // name as written, raw lines
    std::string Get(const std::string& sec, const std::string& key, const std::string& def = "") const;
    int GetInt(const std::string& sec, const std::string& key, int def) const;
};
Ini ReadIni(const std::string& path);

// ---- images: RGBA, top-down
struct Image
{
    int w = 0, h = 0;
    std::vector<unsigned char> px;
};
bool ReadImage(const std::string& path, Image& img);
bool SavePNG(const std::string& path, const Image& img);
