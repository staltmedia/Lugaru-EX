// LugaruEX - mods: installing zips, the mod list, packing for online sharing (GPLv2+)
#include "modpack.h"

#include "fsutil.h"
#include "miniz/miniz.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>

namespace
{
const uint64_t kMaxModBytes = 1024ull * 1024 * 1024; // unpacked size of one mod
const int kMaxModFiles = 50000;

// the folders of the game's Data folder: a mod is found by these
const char* kDataFolders[] = { "textures", "maps", "sounds", "models", "campaigns", "animations", "skeleton", "dialogues", "music", "fonts" };

bool isDataFolder(const std::string& n)
{
    const std::string l = fsu::Lower(n);
    for (const char* d : kDataFolders) {
        if (l == d) {
            return true;
        }
    }
    return false;
}

// game data only: images, sounds, text, models and the extension-less files the game
// uses (maps, skeletons, animations); never programs or scripts
bool isDataFile(const std::string& name)
{
    const size_t dot = name.rfind('.');
    if (dot == std::string::npos) {
        return true;
    }
    static const std::set<std::string> ok = { "png", "jpg", "jpeg", "tga", "bmp", "ogg", "wav", "mp3", "txt", "json", "solid", "xml" };
    return ok.count(fsu::Lower(name.substr(dot + 1))) != 0;
}

// split "a/b\c" into parts; false for anything that could leave the mod folder
bool safeParts(const std::string& path, std::vector<std::string>& parts)
{
    parts.clear();
    if (path.empty() || path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos) {
        return false;
    }
    std::string cur;
    for (char c : path + "/") {
        if (c == '/' || c == '\\') {
            if (cur == "..") {
                return false;
            }
            if (!cur.empty() && cur != ".") {
                parts.push_back(cur);
            }
            cur.clear();
        } else {
            if ((unsigned char)c < 32) {
                return false;
            }
            cur += c;
        }
    }
    return !parts.empty();
}

std::map<std::string, std::string> readModTxt(const std::string& path)
{
    std::map<std::string, std::string> kv;
    std::string t;
    if (!fsu::ReadAll(path, t)) {
        return kv;
    }
    size_t a = 0;
    while (a < t.size()) {
        size_t b = t.find('\n', a);
        if (b == std::string::npos) {
            b = t.size();
        }
        const std::string line = fsu::Trim(t.substr(a, b - a));
        a = b + 1;
        const size_t eq = line.find('=');
        if (eq != std::string::npos && line[0] != '#' && line[0] != ';') {
            kv[fsu::Lower(fsu::Trim(line.substr(0, eq)))] = fsu::Trim(line.substr(eq + 1));
        }
    }
    return kv;
}

size_t writeToFile(void* opaque, mz_uint64 ofs, const void* buf, size_t n)
{
    FILE* f = (FILE*)opaque;
    if (fseek(f, (long)ofs, SEEK_SET) != 0) {
        return 0;
    }
    return fwrite(buf, 1, n, f);
}

struct Zip
{
    mz_zip_archive z;
    FILE* f = nullptr;
    bool ok = false;
    explicit Zip(const std::string& path)
    {
        memset(&z, 0, sizeof(z));
        f = fsu::Open(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            const long long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            ok = size > 0 && mz_zip_reader_init_cfile(&z, f, (mz_uint64)size, 0);
        }
    }
    ~Zip()
    {
        if (ok) {
            mz_zip_reader_end(&z);
        }
        if (f) {
            fclose(f);
        }
    }
};

struct Item
{
    mz_uint index;
    std::string rel; // path inside the mod
    uint64_t size;
};

// the first bytes of an entry (to tell what a loose file is)
std::string peek(Zip& zip, mz_uint index, size_t want)
{
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip.z, index, &st) || st.m_uncomp_size > (64u << 20)) {
        return "";
    }
    size_t n = 0;
    void* p = mz_zip_reader_extract_to_heap(&zip.z, index, &n, 0);
    if (!p) {
        return "";
    }
    std::string s((const char*)p, std::min(n, want));
    mz_free(p);
    return s;
}

// Older Lugaru mods are loose files with a readme saying where each goes ("put TCT in
// Maps, TempleChallenge.txt in Campaigns"): sort them by what they are. Extension-less
// files that start like a Lugaru map (a big-endian version number) are maps; .txt files
// that start with "Campaign Levels:" are campaigns, other .txt files are readmes;
// images, sounds and models go to Textures, Sounds and Models.
bool collectLoose(Zip& zip, const std::vector<std::pair<mz_uint, std::vector<std::string>>>& files, std::vector<Item>& items, int& modTxtIndex,
                  std::string& error, int& skipped)
{
    // everything in one top folder (the zip of a folder): that folder is the mod
    std::string top = files.empty() || files[0].second.size() < 2 ? "" : files[0].second[0];
    for (auto& f : files) {
        if (f.second.size() < 2 || f.second[0] != top) {
            top.clear();
        }
    }
    uint64_t total = 0;
    for (auto& f : files) {
        const std::vector<std::string>& parts = f.second;
        if (parts.size() != (top.empty() ? 1u : 2u)) {
            skipped++; // deeper folders that aren't game folders
            continue;
        }
        const std::string name = parts.back(), low = fsu::Lower(name);
        const size_t dot = low.rfind('.');
        const std::string ext = dot == std::string::npos ? "" : low.substr(dot + 1);
        std::string folder;
        if (low == "mod.txt") {
            modTxtIndex = (int)f.first;
            continue;
        }
        if (ext.empty()) {
            const std::string head = peek(zip, f.first, 4);
            const uint32_t v = head.size() == 4 ? ((uint32_t)(uint8_t)head[0] << 24 | (uint32_t)(uint8_t)head[1] << 16 | (uint32_t)(uint8_t)head[2] << 8 |
                                                   (uint8_t)head[3])
                                                : 0;
            if (v >= 1 && v <= 64) {
                folder = "Maps";
            }
        } else if (ext == "txt") {
            if (fsu::Lower(peek(zip, f.first, 256)).find("campaign levels:") != std::string::npos) {
                folder = "Campaigns";
            }
        } else if (ext == "json") {
            if (peek(zip, f.first, 4096).find("\"map\"") != std::string::npos) {
                folder = "Maps"; // OSS Lugaru's json maps
            }
        } else if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "bmp") {
            folder = "Textures";
        } else if (ext == "ogg" || ext == "wav" || ext == "mp3") {
            folder = "Sounds";
        } else if (ext == "solid") {
            folder = "Models";
        }
        if (folder.empty()) {
            skipped++; // readmes, programs, anything unknown
            continue;
        }
        mz_zip_archive_file_stat st;
        mz_zip_reader_file_stat(&zip.z, f.first, &st);
        total += st.m_uncomp_size;
        items.push_back({ f.first, folder + "/" + name, st.m_uncomp_size });
    }
    if (items.empty()) {
        error = "no game files in it (no maps, campaigns, textures, sounds or models)";
        return false;
    }
    if (total > kMaxModBytes || (int)items.size() > kMaxModFiles) {
        error = "it is too big (over 1 GB or 50000 files)";
        return false;
    }
    return true;
}

// the entries that make up the mod: below the most common data root (a "Data" folder,
// or the folder that holds Textures/, Maps/...), data files only
bool collect(Zip& zip, std::vector<Item>& items, int& modTxtIndex, std::string& error, int& skipped)
{
    std::string modTxtRel;
    modTxtIndex = -1;
    const mz_uint n = mz_zip_reader_get_num_files(&zip.z);
    std::map<std::string, int> votes; // root prefix -> entries under it
    std::vector<std::pair<mz_uint, std::vector<std::string>>> files;
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip.z, i, &st) || st.m_is_directory) {
            continue;
        }
        std::vector<std::string> parts;
        if (!safeParts(st.m_filename, parts)) {
            error = std::string("unsafe path in the zip: ") + st.m_filename;
            return false;
        }
        files.push_back({ i, parts });
        for (size_t k = 0; k < parts.size(); k++) {
            std::string root;
            for (size_t j = 0; j < k; j++) {
                root += parts[j] + "/";
            }
            if (fsu::Lower(parts[k]) == "data" && k + 1 < parts.size()) {
                votes[root + parts[k] + "/"]++;
                break;
            }
            if (isDataFolder(parts[k]) && k + 1 < parts.size()) {
                votes[root]++;
                break;
            }
        }
    }
    if (votes.empty()) {
        return collectLoose(zip, files, items, modTxtIndex, error, skipped);
    }
    std::string root;
    int best = -1;
    for (auto& v : votes) {
        if (v.second > best || (v.second == best && v.first.size() < root.size())) {
            best = v.second;
            root = v.first;
        }
    }
    uint64_t total = 0;
    for (auto& f : files) {
        std::string path;
        for (size_t j = 0; j < f.second.size(); j++) {
            path += (j ? "/" : "") + f.second[j];
        }
        mz_zip_archive_file_stat st;
        mz_zip_reader_file_stat(&zip.z, f.first, &st);
        if (fsu::Lower(f.second.back()) == "mod.txt") {
            // the mod's description: the one nearest the top of the zip
            if (modTxtRel.empty() || std::count(path.begin(), path.end(), '/') < std::count(modTxtRel.begin(), modTxtRel.end(), '/')) {
                modTxtRel = path;
                modTxtIndex = (int)f.first;
            }
            continue;
        }
        if (path.compare(0, root.size(), root) != 0) {
            continue; // outside the mod's data (readme files and such)
        }
        const std::string rel = path.substr(root.size());
        if (rel.find('/') == std::string::npos || !isDataFile(f.second.back())) {
            skipped++; // loose files next to the data folders, programs, scripts
            continue;
        }
        total += st.m_uncomp_size;
        items.push_back({ f.first, rel, st.m_uncomp_size });
    }
    if (items.empty()) {
        error = "no game files in it";
        return false;
    }
    if (total > kMaxModBytes || (int)items.size() > kMaxModFiles) {
        error = "it is too big (over 1 GB or 50000 files)";
        return false;
    }
    return true;
}

bool extractItems(Zip& zip, const std::vector<Item>& items, const std::string& dest, std::string& error)
{
    for (const Item& it : items) {
        const std::string out = fsu::Join(dest, it.rel);
        fsu::MakeDirs(fsu::Parent(out));
        FILE* f = fsu::Open(out, "wb");
        if (!f) {
            error = "can't write " + out;
            return false;
        }
        const bool ok = mz_zip_reader_extract_to_callback(&zip.z, it.index, writeToFile, f, 0);
        fclose(f);
        if (!ok) {
            error = "damaged zip (" + it.rel + ")";
            return false;
        }
    }
    return true;
}

void listFiles(const std::string& base, const std::string& rel, std::vector<std::string>& out)
{
    const std::string dir = rel.empty() ? base : fsu::Join(base, rel);
    for (const std::string& n : fsu::List(dir, false)) {
        out.push_back(rel.empty() ? n : rel + "/" + n);
    }
    for (const std::string& n : fsu::List(dir, true)) {
        listFiles(base, rel.empty() ? n : rel + "/" + n, out);
    }
}
} // namespace

// natural order: map2 before map10
static bool naturalLess(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (isdigit((unsigned char)a[i]) && isdigit((unsigned char)b[j])) {
            size_t i2 = i, j2 = j;
            while (i2 < a.size() && isdigit((unsigned char)a[i2])) {
                i2++;
            }
            while (j2 < b.size() && isdigit((unsigned char)b[j2])) {
                j2++;
            }
            const std::string na = a.substr(i, i2 - i), nb = b.substr(j, j2 - j);
            if (na.size() != nb.size()) {
                return na.size() < nb.size();
            }
            if (na != nb) {
                return na < nb;
            }
            i = i2, j = j2;
            continue;
        }
        const char ca = (char)tolower((unsigned char)a[i]), cb = (char)tolower((unsigned char)b[j]);
        if (ca != cb) {
            return ca < cb;
        }
        i++, j++;
    }
    return a.size() - i < b.size() - j;
}

// A level pack (maps, but no campaign file) is playable only from the developer console:
// give it a campaign of its own, its levels in name order, each one opening the next.
static void addLevelPackCampaign(const std::string& modDir, const std::string& modName)
{
    if (!fsu::List(fsu::Join(modDir, "Campaigns"), false).empty()) {
        return;
    }
    std::vector<std::string> maps;
    for (const std::string& f : fsu::List(fsu::Join(modDir, "Maps"), false)) {
        std::string n = f;
        if (n.size() > 5 && fsu::Lower(n.substr(n.size() - 5)) == ".json") {
            n.resize(n.size() - 5); // (the game finds Maps/<name>.json by the name)
        }
        if (!n.empty() && n.find_first_of(" \t") == std::string::npos && n.find('.') == std::string::npos &&
            std::find(maps.begin(), maps.end(), n) == maps.end()) {
            maps.push_back(n); // (the campaign file can't hold a name with a space)
        }
    }
    if (maps.empty()) {
        return;
    }
    std::sort(maps.begin(), maps.end(), naturalLess);
    if (maps.size() > 64) {
        maps.resize(64);
    }
    std::string camp; // the campaign's name: the mod's, without spaces
    for (char c : modName) {
        if (c != ' ') {
            camp += c;
        }
    }
    if (camp.empty()) {
        camp = "LevelPack";
    }
    const int n = (int)maps.size();
    std::string t = "Campaign Levels: " + std::to_string(n) + "\n";
    for (int i = 0; i < n; i++) {
        // markers spread across the world map, going left to right in a gentle wave
        const int x = n == 1 ? 320 : 110 + 420 * i / (n - 1), y = 250 + ((i % 2) ? 45 : -45);
        t += "\nLevel " + std::to_string(i + 1) + ":\nName: " + maps[i] + "\nDescription: " + maps[i] + "\nChooseNext: 1\n";
        t += i + 1 < n ? "NumNext: 1\nNextLevel: " + std::to_string(i + 2) + "\n" : std::string("NumNext: 0\n");
        t += "LocationX: " + std::to_string(x) + "\nLocationY: " + std::to_string(y) + "\n";
    }
    t += "\nEndText:" + camp + " complete!\n\n\n";
    fsu::MakeDirs(fsu::Join(modDir, "Campaigns"));
    fsu::WriteAll(fsu::Join(modDir, "Campaigns/" + camp + ".txt"), t);
}

std::string Mod_SafeName(const std::string& name)
{
    std::string r;
    for (unsigned char c : name) {
        r += (c < 128 && (isalnum(c) || c == ' ' || c == '-' || c == '_')) ? (char)c : '_';
    }
    r = fsu::Trim(r);
    while (!r.empty() && (r[0] == '.' || r[0] == '_')) {
        r.erase(0, 1);
    }
    if (r.size() > 60) {
        r.resize(60);
    }
    return r.empty() ? std::string("Mod") : r;
}

bool Mod_InstallZip(const std::string& zipPath, const std::string& modsDir, std::string& folderOut, std::string& error)
{
    Zip zip(zipPath);
    if (!zip.ok) {
        error = "not a zip file (or it is damaged)";
        return false;
    }
    std::vector<Item> items;
    int modTxt = -1;
    int skipped = 0;
    if (!collect(zip, items, modTxt, error, skipped)) {
        return false;
    }
    // the mod's name: mod.txt's name=, else the zip's file name
    std::string name = fsu::BaseName(zipPath);
    name = name.substr(0, name.rfind('.'));
    std::string modTxtData;
    if (modTxt >= 0) {
        size_t n = 0;
        if (void* p = mz_zip_reader_extract_to_heap(&zip.z, (mz_uint)modTxt, &n, 0)) {
            modTxtData.assign((const char*)p, n);
            mz_free(p);
            const size_t k = fsu::Lower(modTxtData).find("name=");
            if (k != std::string::npos) {
                const size_t e = modTxtData.find('\n', k);
                const std::string v = fsu::Trim(modTxtData.substr(k + 5, e == std::string::npos ? std::string::npos : e - k - 5));
                if (!v.empty()) {
                    name = v;
                }
            }
        }
    }
    std::string folder = Mod_SafeName(name);
    for (int k = 2; fsu::IsDir(fsu::Join(modsDir, folder)) || fsu::IsFile(fsu::Join(modsDir, folder)); k++) {
        folder = Mod_SafeName(name) + " " + std::to_string(k);
    }
    // unpack into a hidden folder first, so a failed install leaves nothing behind
    const std::string tmp = fsu::Join(modsDir, ".installing");
    fsu::RemoveTree(tmp);
    fsu::MakeDirs(tmp);
    if (!extractItems(zip, items, tmp, error)) {
        fsu::RemoveTree(tmp);
        return false;
    }
    addLevelPackCampaign(tmp, folder);
    if (modTxtData.empty()) {
        modTxtData = "name=" + folder + "\n"; // (a second copy shows as "Name 2")
    }
    fsu::WriteAll(fsu::Join(tmp, "mod.txt"), modTxtData);
    if (!fsu::Rename(tmp, fsu::Join(modsDir, folder))) {
        fsu::RemoveTree(tmp);
        error = "can't create the mod's folder in " + modsDir;
        return false;
    }
    folderOut = folder;
    return true;
}

bool Mod_ExtractTo(const std::string& zipPath, const std::string& destDir, std::string& error)
{
    Zip zip(zipPath);
    if (!zip.ok) {
        error = "damaged download";
        return false;
    }
    std::vector<Item> items;
    int modTxt = -1;
    int skipped = 0;
    if (!collect(zip, items, modTxt, error, skipped)) {
        return false;
    }
    fsu::MakeDirs(destDir);
    return extractItems(zip, items, destDir, error);
}

bool Mod_PackFolder(const std::string& folder, const std::string& zipPath)
{
    std::vector<std::string> files;
    listFiles(folder, "", files);
    std::sort(files.begin(), files.end());
    FILE* f = fsu::Open(zipPath, "wb");
    if (!f) {
        return false;
    }
    mz_zip_archive z;
    memset(&z, 0, sizeof(z));
    bool ok = mz_zip_writer_init_cfile(&z, f, 0);
    MZ_TIME_T fixed = 315532800; // 1980-01-01: no dates, so the same mod packs to the same bytes
    for (const std::string& rel : files) {
        if (!ok) {
            break;
        }
        if (rel.find('/') == std::string::npos || !isDataFile(fsu::BaseName(rel))) {
            continue; // mod.txt and anything that isn't game data stay home
        }
        std::string data;
        if (!fsu::ReadAll(fsu::Join(folder, rel), data)) {
            continue;
        }
        ok = mz_zip_writer_add_mem_ex_v2(&z, rel.c_str(), data.data(), data.size(), nullptr, 0, MZ_BEST_SPEED, 0, 0, &fixed, nullptr, 0, nullptr, 0);
    }
    ok = ok && mz_zip_writer_finalize_archive(&z);
    mz_zip_writer_end(&z);
    fclose(f);
    if (!ok) {
        fsu::RemoveTree(zipPath);
    }
    return ok;
}

uint64_t Mod_HashFile(const std::string& path)
{
    FILE* f = fsu::Open(path, "rb");
    if (!f) {
        return 0;
    }
    uint64_t h = 1469598103934665603ull;
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            h = (h ^ buf[i]) * 1099511628211ull;
        }
    }
    fclose(f);
    return h ? h : 1;
}

std::vector<ModInfo> Mod_List(const std::string& modsDir)
{
    std::vector<ModInfo> v;
    for (const std::string& d : fsu::List(modsDir, true)) {
        if (d.empty() || d[0] == '.') {
            continue;
        }
        ModInfo m;
        m.folder = d;
        const std::string dir = fsu::Join(modsDir, d);
        auto kv = readModTxt(fsu::Join(dir, "mod.txt"));
        m.name = kv.count("name") && !kv["name"].empty() ? kv["name"] : d;
        m.author = kv["author"];
        m.version = kv["version"];
        m.description = kv["description"];
        std::vector<std::string> files;
        listFiles(dir, "", files);
        for (const std::string& f : files) {
            m.bytes += fsu::Size(fsu::Join(dir, f));
        }
        m.files = (int)files.size();
        v.push_back(m);
    }
    return v;
}

bool Mod_Delete(const std::string& modsDir, const std::string& folder)
{
    std::vector<std::string> parts;
    if (folder.empty() || folder[0] == '.' || !safeParts(folder, parts) || parts.size() != 1) {
        return false;
    }
    return fsu::RemoveTree(fsu::Join(modsDir, folder));
}

std::vector<ModEntry> Mod_ParseList(const std::string& value)
{
    std::vector<ModEntry> v;
    size_t a = 0;
    while (a <= value.size()) {
        size_t b = value.find('|', a);
        if (b == std::string::npos) {
            b = value.size();
        }
        std::string item = fsu::Trim(value.substr(a, b - a));
        a = b + 1;
        if (item.empty()) {
            continue;
        }
        ModEntry e;
        e.on = item[0] != '-';
        if (item[0] == '-' || item[0] == '+') {
            item.erase(0, 1);
        }
        e.folder = fsu::Trim(item);
        if (!e.folder.empty()) {
            v.push_back(e);
        }
    }
    return v;
}

std::string Mod_FormatList(const std::vector<ModEntry>& list)
{
    std::string s;
    for (size_t i = 0; i < list.size(); i++) {
        s += (i ? "|" : "") + std::string(list[i].on ? "+" : "-") + list[i].folder;
    }
    return s;
}

std::vector<ModEntry> Mod_Order(const std::vector<ModEntry>& saved, const std::vector<ModInfo>& installed)
{
    std::set<std::string> have, listed;
    for (auto& m : installed) {
        have.insert(m.folder);
    }
    std::vector<ModEntry> out;
    for (auto& m : installed) { // new mods: on, and first
        bool known = false;
        for (auto& s : saved) {
            known |= s.folder == m.folder;
        }
        if (!known) {
            out.push_back({ m.folder, true });
            listed.insert(m.folder);
        }
    }
    for (auto& s : saved) {
        if (have.count(s.folder) && !listed.count(s.folder)) {
            out.push_back(s);
            listed.insert(s.folder);
        }
    }
    return out;
}
