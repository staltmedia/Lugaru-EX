// LugaruEX launcher for Linux - the banner, composed from the user's own game files (GPLv2+)
#pragma once
#include "common.h"

// W x H banner (RGBA) from <game>/Data/Textures/Eyelid.png and Lugaru.png: the logo's "HD"
// is replaced by "EX" in the LUGARU letters' own colours, with an ember glow and a vignette.
// Also writes <exDir>Data/Textures/LugaruEX-Logo.png, the same edit for the game's main menu.
// Same look as the Windows launcher.
void BuildBanner(const std::string& gameDir, const std::string& exDir, int W, int H, Image& out);

// a font file for the UI ("sans") or the EX lettering ("serif"); "" if none was found
std::string FindFontFile(bool serif);
