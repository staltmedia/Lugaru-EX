// LugaruEX - custom character looks (see looks.h) (GPLv2+)
#include "looks.h"

#include "fsutil.h"
#include "modpack.h"

#include <algorithm>

namespace
{
bool isImage(const std::string& name)
{
    const std::string l = fsu::Lower(name);
    const size_t dot = l.rfind('.');
    if (dot == std::string::npos) {
        return false;
    }
    const std::string ext = l.substr(dot + 1);
    return ext == "png" || ext == "jpg" || ext == "jpeg";
}

std::string stem(const std::string& name)
{
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// the places looked in: LugaruEX/Data first, then the mods in order (first one wins)
struct Root
{
    std::string dir; // ends with '/'
    std::string mod; // "" for LugaruEX/Data
};
std::vector<Root> roots(const std::string& exDir, const std::vector<std::string>& mods)
{
    std::vector<Root> r;
    r.push_back({ exDir + "Data/", "" });
    for (const std::string& m : mods) {
        r.push_back({ exDir + "Mods/" + m + "/", m });
    }
    return r;
}

std::string modName(const std::string& exDir, const std::string& folder)
{
    // mod.txt's name when it has one (Mod_List reads it), else the folder
    for (const ModInfo& i : Mod_List(exDir + "Mods")) {
        if (i.folder == folder) {
            return i.name.empty() ? folder : i.name;
        }
    }
    return folder;
}

std::string labelOf(const std::string& exDir, const std::string& name, const std::string& mod)
{
    return mod.empty() ? name : name + " (" + modName(exDir, mod) + ")";
}

bool safeName(const std::string& n)
{
    if (n.empty() || n.size() > 64 || n[0] == '.' || n[0] == ' ') {
        return false;
    }
    for (char c : n) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == '-' || c == '_')) {
            return false;
        }
    }
    return n.find("..") == std::string::npos;
}
} // namespace

std::vector<LookImage> Looks_Images(const std::string& exDir, const std::vector<std::string>& mods)
{
    std::vector<LookImage> v;
    for (const char* model : { "rabbit", "wolf", "clothes" }) {
        std::vector<std::string> seen; // (the same file name from two places: the first one wins)
        for (const Root& r : roots(exDir, mods)) {
            const std::string dir = r.dir + "Textures/Custom/" + model;
            std::vector<std::string> names = fsu::List(dir, false);
            std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return fsu::Lower(a) < fsu::Lower(b); });
            for (const std::string& n : names) {
                if (!isImage(n) || std::find(seen.begin(), seen.end(), fsu::Lower(n)) != seen.end()) {
                    continue;
                }
                seen.push_back(fsu::Lower(n));
                LookImage s;
                s.model = model;
                s.file = dir + "/" + n;
                s.label = labelOf(exDir, stem(n), r.mod);
                s.enginePath = std::string("Textures/Custom/") + model + "/" + n;
                v.push_back(s);
            }
        }
    }
    return v;
}

std::vector<LookModel> Looks_Models(const std::string& exDir, const std::vector<std::string>& mods)
{
    std::vector<LookModel> v;
    for (const char* base : { "rabbit", "wolf" }) {
        std::vector<std::string> seen;
        for (const Root& r : roots(exDir, mods)) {
            const std::string dir = r.dir + "Models/Custom/" + base;
            std::vector<std::string> names = fsu::List(dir, true);
            std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return fsu::Lower(a) < fsu::Lower(b); });
            for (const std::string& n : names) {
                if (!safeName(n) || std::find(seen.begin(), seen.end(), fsu::Lower(n)) != seen.end()) {
                    continue;
                }
                seen.push_back(fsu::Lower(n));
                LookModel m;
                m.base = base;
                m.key = std::string(base) + "/" + n;
                m.label = labelOf(exDir, n, r.mod);
                m.dir = dir + "/" + n + "/";
                for (const std::string& f : fsu::List(m.dir, false)) {
                    if (isImage(f) && fsu::Lower(stem(f)) == "skin") {
                        m.skin = m.dir + f;
                    }
                }
                v.push_back(m);
            }
        }
    }
    return v;
}

std::vector<std::string> Looks_EnabledMods(const std::string& exDir, const std::string& listValue)
{
    std::vector<std::string> out;
    for (const ModEntry& e : Mod_Order(Mod_ParseList(listValue), Mod_List(exDir + "Mods"))) {
        if (e.on) {
            out.push_back(e.folder);
        }
    }
    return out;
}

std::string Looks_ModelDir(const std::string& key)
{
    const size_t slash = key.find('/');
    if (slash == std::string::npos || !Looks_SafeModelKey(key)) {
        return std::string();
    }
    return "Models/Custom/" + key + "/";
}

std::string Looks_Base(const std::string& key)
{
    return (key == "wolf" || key.compare(0, 5, "wolf/") == 0) ? "wolf" : "rabbit";
}

bool Looks_SafeModelKey(const std::string& key)
{
    const size_t slash = key.find('/');
    if (slash == std::string::npos) {
        return key == "rabbit" || key == "wolf";
    }
    const std::string base = key.substr(0, slash);
    return (base == "rabbit" || base == "wolf") && safeName(key.substr(slash + 1));
}
