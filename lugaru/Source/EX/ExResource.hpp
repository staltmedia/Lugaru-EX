/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#ifndef _EX_RESOURCE_HPP_
#define _EX_RESOURCE_HPP_

#include <string>

namespace EX
{
/**
 * Map an engine resource path ("Textures/FurBrown.jpg") to a real file.
 * Lookup order:
 *   1. Overrides/<path> (any image extension for textures)  [if enabled]
 *   2. Data/<path>
 *   3. Known OSS -> original HD renames (e.g. FurBrown.jpg -> Fur3.jpg)
 *   4. Fuzzy match in the same Data folder (case, spaces, underscores;
 *      .ogg also matches .mp3/.wav)
 *   5. LugaruEX/Data/<path> (small set of free OSS files shipped with the mod)
 * Returns Data/<path> if nothing matched (callers report it as missing).
 */
std::string ResolveResource(const std::string& dataDir, const std::string& path);

/** True if the resolved file came from the Overrides folder. */
bool IsOverridePath(const std::string& resolved);
// forget every lookup (the mods in use changed)
void ResourceCacheClear();
}

#endif
