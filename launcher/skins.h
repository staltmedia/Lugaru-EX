// LugaruEX launcher - character skin table (shared by the Character and Local tabs)
#pragma once

// Character skins: engine (OSS) path, name shown, original HD file name
struct SkinInfo
{
    const char* model;
    const char* path;
    const wchar_t* label;
    const wchar_t* hdFile;
};
static const SkinInfo kSkins[] = {
    { "rabbit", "Textures/FurBrown.jpg", L"Brown", L"Fur3.jpg" },
    { "rabbit", "Textures/FurWhite.jpg", L"White", L"Fur.jpg" },
    { "rabbit", "Textures/FurBlack.jpg", L"Black", L"Fur2.jpg" },
    { "rabbit", "Textures/FurBrownWhite.jpg", L"Brown and white", L"WB2.jpg" },
    { "rabbit", "Textures/FurBlackWhite.jpg", L"Black and white", L"BW2.jpg" },
    { "rabbit", "Textures/FurLynx.jpg", L"Lynx", L"Lynx.jpg" },
    { "rabbit", "Textures/FurOtter.jpg", L"Otter", L"Otter.jpg" },
    { "rabbit", "Textures/FurOpal.jpg", L"Opal", L"Opal.jpg" },
    { "rabbit", "Textures/FurSable.jpg", L"Sable", L"Sable.jpg" },
    { "rabbit", "Textures/FurChocolate.jpg", L"Chocolate", L"Chocolate.jpg" },
    { "rabbit", "Textures/FurDavid.jpg", L"David", L"David.jpg" },
    { "rabbit", "Textures/FurDarko.jpg", L"Darko", L"Darko.jpg" },
    { "wolf", "Textures/FurWolfGrey.jpg", L"Grey", L"Wolf.jpg" },
    { "wolf", "Textures/FurWolfDark.jpg", L"Dark", L"DarkWolf.jpg" },
    { "wolf", "Textures/FurWolfSnow.jpg", L"Snow", L"SnowWolf.jpg" },
    { "wolf", "Textures/FurWolfLizard.jpg", L"Lizard", L"LizardWolf.jpg" },
};
