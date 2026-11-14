// LugaruEX - custom character looks: skins, clothes and models the player made or a mod
// brings (used by both launchers) (GPLv2+)
//
// Where they live (in <game>/LugaruEX/Data/ and in every mod that is switched on):
//   Textures/Custom/rabbit/<name>.png|jpg   rabbit skins
//   Textures/Custom/wolf/<name>.png|jpg     wolf skins
//   Textures/Custom/clothes/<name>.png|jpg  clothes (painted over the skin as they are)
//   Models/Custom/rabbit/<Name>/            a model: any of the rabbit's own files by their
//   Models/Custom/wolf/<Name>/              names (Body.solid .. Body7.solid, BodyLow.solid,
//                                           Belt.solid, Skeleton files BasicFigure,
//                                           BasicFigureLow, RabbitBelt; Wolf*.solid and
//                                           BasicFigureWolf* for wolves) plus an optional
//                                           Skin.png|jpg (its default skin). Missing files
//                                           come from the creature's own.
// The game finds them by the same paths (mods first), so a mod that is switched off takes
// its looks with it.
#pragma once
#include <string>
#include <vector>

struct LookImage
{
    std::string model;      // "rabbit" | "wolf" | "clothes"
    std::string enginePath; // Textures/Custom/rabbit/Name.png
    std::string label;      // Name, or "Name (Mod)"
    std::string file;       // full path on disk (UTF-8)
};

struct LookModel
{
    std::string base;  // "rabbit" | "wolf": its animations, moves and sounds
    std::string key;   // "rabbit/Name": the Character tab's model value
    std::string label; // "Name (Mod)"
    std::string dir;   // full path of its folder, ends with '/'
    std::string skin;  // full path of its Skin.png|jpg, or empty
};

// exDir = <game>/LugaruEX/ (ends with a slash); mods = folders in exDir/Mods that are on
std::vector<LookImage> Looks_Images(const std::string& exDir, const std::vector<std::string>& mods);
std::vector<LookModel> Looks_Models(const std::string& exDir, const std::vector<std::string>& mods);
// the mods that are on, from the [Mods] List value (installed ones only, in order)
std::vector<std::string> Looks_EnabledMods(const std::string& exDir, const std::string& listValue);
// "rabbit/Name" -> "Models/Custom/rabbit/Name/" ("" for plain "rabbit" / "wolf")
std::string Looks_ModelDir(const std::string& key);
// the creature a model value plays as: "wolf" for "wolf" and "wolf/..." , else "rabbit"
std::string Looks_Base(const std::string& key);
// a model value that is safe to use as a path (letters, digits, space . - _ in the name)
bool Looks_SafeModelKey(const std::string& key);
