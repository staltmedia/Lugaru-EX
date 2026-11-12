/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include "EX/ExResource.hpp"

#include "EX/ExConfig.hpp"
#include "EX/ExMods.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

namespace EX
{
static std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static std::string normName(const std::string& s)
{
    std::string r;
    for (unsigned char c : s) {
        if (c == ' ' || c == '_' || c == '-') {
            continue;
        }
        r += (char)std::tolower(c);
    }
    return r;
}

static bool fileExists(const std::string& p)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static void splitPath(const std::string& p, std::string& dir, std::string& base, std::string& ext)
{
    size_t s = p.find_last_of('/');
    dir = (s == std::string::npos) ? "" : p.substr(0, s);
    std::string file = (s == std::string::npos) ? p : p.substr(s + 1);
    size_t d = file.find_last_of('.');
    if (d == std::string::npos) {
        base = file;
        ext = "";
    } else {
        base = file.substr(0, d);
        ext = file.substr(d); // includes dot
    }
}

// OSS renamed some original files to clearer names. Map back to the names
// shipped with the original Lugaru HD (keys are lower-case OSS paths).
static const std::map<std::string, std::string>& renames()
{
    static const std::map<std::string, std::string> m = {
        { "textures/furbrown.jpg", "Textures/Fur3.jpg" },
        { "textures/furwhite.jpg", "Textures/Fur.jpg" },
        { "textures/furblack.jpg", "Textures/Fur2.jpg" },
        { "textures/furblackwhite.jpg", "Textures/BW2.jpg" },
        { "textures/furbrownwhite.jpg", "Textures/WB2.jpg" },
        { "textures/furchocolate.jpg", "Textures/Chocolate.jpg" },
        { "textures/furdavid.jpg", "Textures/David.jpg" },
        { "textures/furlynx.jpg", "Textures/Lynx.jpg" },
        { "textures/furopal.jpg", "Textures/Opal.jpg" },
        { "textures/furotter.jpg", "Textures/Otter.jpg" },
        { "textures/fursable.jpg", "Textures/Sable.jpg" },
        { "textures/furwolfdark.jpg", "Textures/DarkWolf.jpg" },
        { "textures/furwolfgrey.jpg", "Textures/Wolf.jpg" },
        { "textures/furwolflizard.jpg", "Textures/LizardWolf.jpg" },
        { "textures/furwolfsnow.jpg", "Textures/SnowWolf.jpg" },
        { "textures/bloodknifelight.png", "Textures/Lightbloodknife.png" },
    };
    return m;
}

// Per-directory listing cache: normalized name -> real file name
struct DirIndex
{
    bool scanned = false;
    std::unordered_map<std::string, std::string> byName; // norm(file) -> file
    std::unordered_map<std::string, std::string> byBase; // norm(base)+"|"+lower(ext) -> file
};

static std::unordered_map<std::string, DirIndex> dirCache;
static std::unordered_map<std::string, std::string> resolveCache;
static std::mutex mtx;

static DirIndex& indexDir(const std::string& dir)
{
    DirIndex& di = dirCache[lower(dir)];
    if (di.scanned) {
        return di;
    }
    di.scanned = true;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "/*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return di;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        std::string name = fd.cFileName;
        std::string d, b, e;
        splitPath(name, d, b, e);
        di.byName.emplace(normName(name), name);
        di.byBase.emplace(normName(b) + "|" + lower(e), name);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            if (name == "." || name == ".." || !fileExists(dir + "/" + name)) {
                continue;
            }
            std::string dd, b, x;
            splitPath(name, dd, b, x);
            di.byName.emplace(normName(name), name);
            di.byBase.emplace(normName(b) + "|" + lower(x), name);
        }
        closedir(d);
    }
#endif
    return di;
}

// root + "/" + dir with each folder name as it is spelled on disk (Linux file names are
// case-sensitive; Windows finds any spelling)
static std::string fixDirCase(const std::string& root, const std::string& dir)
{
    std::string out = root;
    if (dir.empty()) {
        return out;
    }
#ifdef _WIN32
    return out + "/" + dir;
#else
    size_t a = 0;
    while (a <= dir.size()) {
        const size_t b = std::min(dir.find('/', a), dir.size());
        const std::string part = dir.substr(a, b - a);
        a = b + 1;
        if (part.empty()) {
            continue;
        }
        std::string next = out + "/" + part;
        struct stat st;
        if (stat(next.c_str(), &st) != 0) {
            if (DIR* d = opendir(out.c_str())) {
                while (dirent* e = readdir(d)) {
                    if (lower(e->d_name) == lower(part)) {
                        next = out + "/" + e->d_name;
                        break;
                    }
                }
                closedir(d);
            }
        }
        out = next;
    }
    return out;
#endif
}

static bool isImageExt(const std::string& e)
{
    std::string l = lower(e);
    return l == ".png" || l == ".jpg" || l == ".jpeg";
}

static bool isSoundExt(const std::string& e)
{
    std::string l = lower(e);
    return l == ".ogg" || l == ".mp3" || l == ".wav";
}

// Look for path inside root, exact then fuzzy. altExts: other extensions accepted.
static bool findIn(const std::string& root, const std::string& path, bool fuzzy, const std::vector<std::string>& altExts, std::string& out)
{
    std::string full = root + "/" + path;
    if (fileExists(full)) {
        out = full;
        return true;
    }
    std::string dir, base, ext;
    splitPath(path, dir, base, ext);
    const std::string absDir = fixDirCase(root, dir);
    for (const std::string& e : altExts) {
        std::string p = absDir + "/" + base + e;
        if (fileExists(p)) {
            out = p;
            return true;
        }
    }
    if (!fuzzy) {
        return false;
    }
    DirIndex& di = indexDir(absDir);
    auto it = di.byName.find(normName(base + ext));
    if (it != di.byName.end()) {
        out = absDir + "/" + it->second;
        return true;
    }
    for (const std::string& e : altExts) {
        auto jt = di.byBase.find(normName(base) + "|" + lower(e));
        if (jt != di.byBase.end()) {
            out = absDir + "/" + jt->second;
            return true;
        }
    }
    return false;
}

std::string ResolveResource(const std::string& dataDir, const std::string& rawPath)
{
    std::string path = rawPath;
    std::replace(path.begin(), path.end(), '\\', '/');
    // Original (Mac era) binary maps store clothes as ":Data:Textures:shirt.png"
    if (path.find(':') != std::string::npos) {
        std::replace(path.begin(), path.end(), ':', '/');
        while (!path.empty() && path[0] == '/') {
            path.erase(0, 1);
        }
        if (lower(path.substr(0, 5)) == "data/") {
            path.erase(0, 5);
        }
    }

    std::lock_guard<std::mutex> lock(mtx);
    auto cached = resolveCache.find(path);
    if (cached != resolveCache.end()) {
        return cached->second;
    }

    std::string dir, base, ext;
    splitPath(path, dir, base, ext);
    std::vector<std::string> alt;
    if (isImageExt(ext)) {
        alt = { ".png", ".jpg", ".jpeg" };
    } else if (isSoundExt(ext)) {
        alt = { ".ogg", ".mp3", ".wav" };
    }

    std::string out;
    const std::string exe = GameDir();
    bool found = false;

    // 0. Mods (switched on in the launcher; online, the host's), first one first
    //    (mods made for the original game use its file names: the known renames too)
    for (const std::string& root : ModRoots()) {
        found = findIn(root, path, true, alt, out);
        if (!found) {
            auto r = renames().find(lower(path));
            found = r != renames().end() && findIn(root, r->second, true, alt, out);
        }
        if (found) {
            break;
        }
    }
    // 1. Overrides (also under original HD names)
    if (!found && cfg.textureOverrides) {
        // LugaruEX/Overrides (older versions: Overrides in the game folder)
        static const std::string ovr = [] {
            const std::string n = ExFolder() + "Overrides";
#ifdef _WIN32
            WIN32_FILE_ATTRIBUTE_DATA d;
            return GetFileAttributesExA(n.c_str(), GetFileExInfoStandard, &d) ? n : GameDir() + "Overrides";
#else
            return n;
#endif
        }();
        found = findIn(ovr, path, true, alt, out);
        if (!found) {
            auto r = renames().find(lower(path));
            if (r != renames().end()) {
                found = findIn(ovr, r->second, true, alt, out);
            }
        }
    }
    // 2. Data exact (no alternate extensions: keep original file preference)
    if (!found) {
        found = findIn(dataDir, path, false, {}, out);
    }
    // 3. Known renames
    if (!found) {
        auto r = renames().find(lower(path));
        if (r != renames().end()) {
            found = findIn(dataDir, r->second, true, alt, out);
        }
    }
    // 4. Fuzzy + alternate extensions in Data
    if (!found) {
        found = findIn(dataDir, path, true, alt, out);
    }
    // 5. Files shipped with LugaruEX
    if (!found) {
        found = findIn(exe + "LugaruEX/Data", path, true, alt, out);
    }
    if (!found) {
        out = dataDir + "/" + path;
    } else if (out != dataDir + "/" + path) {
        std::cerr << "LugaruEX: " << path << " -> " << out << std::endl;
    }
    resolveCache[path] = out;
    return out;
}

bool IsOverridePath(const std::string& resolved)
{
    return resolved.find("Overrides/") != std::string::npos;
}

void ResourceCacheClear()
{
    std::lock_guard<std::mutex> lock(mtx);
    resolveCache.clear();
    dirCache.clear();
}
}
