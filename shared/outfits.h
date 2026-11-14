// LugaruEX - the launcher's Clothes choices: outfits made of the game's own clothing
// images, painted over the skin in this order with these tints (GPLv2+).
// Shared by the engine (EX/ExLocal.cpp), the 3D preview and both launchers.
// Every clothing image the game's levels use is in at least one of them; the mixes and
// tints are those of characters in the levels.
#pragma once
#include <vector>

struct OutfitPiece
{
    const char* tex; // game-relative path
    float r, g, b;
};
struct OutfitDef
{
    const char* key;   // what LugaruEX.ini stores ([Player] Clothes=)
    const char* label; // what the launcher shows
    std::vector<OutfitPiece> pieces;
};

// [0] is "none"; Random picks from the rest
inline const std::vector<OutfitDef>& Outfits()
{
    static const std::vector<OutfitDef> list = {
        { "none", "None", {} },
        { "shirt", "Shirt and pants", { { "Textures/Shirt.png", .9f, .88f, .82f }, { "Textures/Pants.png", .5f, .4f, .3f }, { "Textures/Belt.png", 1, 1, 1 } } },
        { "blackshirt", "Black shirt", { { "Textures/Shirt.png", 0, 0, 0 }, { "Textures/Pants.png", .5f, .5f, .5f } } },
        { "longsleeve", "Long sleeves", { { "Textures/LongSleeve.png", 0, 0, 0 }, { "Textures/Pants.png", .5f, .5f, .5f } } },
        { "bluepants", "Blue trousers", { { "Textures/FancyPants.png", 0, 0, .3f } } },
        { "darkpants", "Dark trousers", { { "Textures/FancyPants.png", .2f, .2f, .2f } } },
        { "female", "Top and trousers", { { "Textures/FancyPants.png", .1f, .1f, .4f }, { "Textures/ShirtFemale.png", 1, .8f, .8f } } },
        { "camo", "Camo", { { "Textures/FancyPantsCamo.png", 1, 1, 1 }, { "Textures/Belt.png", 1, 1, 1 } } },
        { "commando", "Camo and bandanas", { { "Textures/HeadBand.png", .5f, .5f, .3f }, { "Textures/FancyPantsCamo.png", 1, 1, 1 }, { "Textures/Shirt.png", 0, 0, 0 }, { "Textures/HeadBand.png", .3f, 0, 0 } } },
        { "snowcamo", "Snow camo", { { "Textures/FancyPantsCamoArctic.png", 1, 1, 1 }, { "Textures/Belt.png", 1, 1, 1 } } },
        { "arctic", "Arctic armor", { { "Textures/FancyPantsCamoArctic.png", 1, 1, 1 }, { "Textures/HeadBand.png", 1, 1, 1 }, { "Textures/LeatherArmor.png", .5f, .5f, .5f } } },
        { "desertcamo", "Desert camo", { { "Textures/FancyPantsCamoDesert.png", 1, 1, 1 }, { "Textures/Belt.png", 1, 1, 1 } } },
        { "leather", "Leather armor", { { "Textures/LeatherArmor.png", 1, 1, 1 }, { "Textures/LeatherPants.png", 1, 1, 1 }, { "Textures/LeatherWristLeft.png", 1, 1, 1 }, { "Textures/LeatherWristRight.png", 1, 1, 1 } } },
        { "leatherpants", "Leather pants and bracers", { { "Textures/LeatherPants.png", 1, 1, 1 }, { "Textures/LeatherWristLeft.png", 1, 1, 1 }, { "Textures/LeatherWristRight.png", 1, 1, 1 } } },
        { "raider", "Red leather", { { "Textures/LeatherArmor.png", .4f, 0, 0 }, { "Textures/LeatherWristRight.png", .5f, 0, 0 }, { "Textures/FootWrap.png", .1f, 0, 0 }, { "Textures/Pants.png", 0, 0, 0 } } },
        { "wanderer", "Wanderer", { { "Textures/EarWrap.png", 1, 1, 0 }, { "Textures/LongSleeve.png", 1, 1, 0 }, { "Textures/Pants.png", 1, .8f, 0 }, { "Textures/LeatherArmor.png", 1, 1, 1 } } },
        { "gi", "Gi", { { "Textures/Gi.png", 1, 1, 1 }, { "Textures/HeadBand.png", .8f, .1f, .1f } } },
        { "bluegi", "Blue gi", { { "Textures/Gi.png", 0, 0, .3f }, { "Textures/HeadBand.png", 0, 0, .3f } } },
        { "torngi", "Torn gi", { { "Textures/Pants.png", 1, .7f, 0 }, { "Textures/GiTeared.png", .8f, .8f, .8f } } },
        { "ninja", "Ninja", { { "Textures/GiBottom.png", 0, 0, 0 }, { "Textures/GiTop.png", 0, 0, 0 }, { "Textures/Mask.png", 0, 0, 0 }, { "Textures/EarWrap.png", 0, 0, 0 } } },
        { "feng", "Feng (wind)", { { "Textures/GiTop.png", 1, 1, 1 }, { "Textures/ClFeng.png", 1, 1, 1 }, { "Textures/GiBottom.png", .05f, .05f, .05f } } },
        { "shui", "Shui (water)", { { "Textures/GiBottom.png", 1, 1, 1 }, { "Textures/GiTop.png", 1, 1, 1 }, { "Textures/ClShui.png", 1, 1, 1 } } },
        { "huo", "Huo (fire)", { { "Textures/FancyPants.png", .8f, .15f, .15f }, { "Textures/GiTeared.png", .8f, .15f, .15f }, { "Textures/ClHuo.png", .8f, .15f, .15f }, { "Textures/LeatherWristLeft.png", 1, 0, 0 }, { "Textures/LeatherWristRight.png", 1, 0, 0 } } },
        { "shan", "Shan (mountain)", { { "Textures/ClShan.png", .6f, .5f, .45f }, { "Textures/Pants.png", .4f, .2f, .2f }, { "Textures/LeatherWristLeft.png", .4f, .2f, .2f } } },
        { "desert", "Desert robe", { { "Textures/DesertCl.png", 1, .9f, .6f } } },
        { "nomad", "Desert wraps", { { "Textures/DesertCl.png", .9f, .8f, .5f }, { "Textures/HeadBand.png", .9f, .8f, .5f }, { "Textures/EarWrap.png", .9f, .8f, .5f }, { "Textures/FootWrap.png", .9f, .8f, .5f } } },
    };
    return list;
}
