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

// What a file in a mod zip is, worked out from its path and its contents, so a mod can come
// in any layout: a Data folder, the game's folders at the top, folders named for where
// their files go ("This goes into Maps", "Textures folder"), or loose files (the readme
// said where each one goes). Mac and Windows leftovers are dropped.

// the game folder a folder name stands for: exactly ("Textures"), or as a word in a phrase
// ("This goes into Maps", "put in sound folder"); "" if none (or more than one)
std::string canonFolder(const std::string& lowName)
{
    static const std::map<std::string, std::string> exact = {
        { "textures", "Textures" }, { "maps", "Maps" }, { "sounds", "Sounds" }, { "models", "Models" }, { "campaigns", "Campaigns" },
        { "animations", "Animations" }, { "skeleton", "Skeleton" }, { "dialogues", "Dialogues" }, { "music", "Music" }, { "fonts", "Fonts" },
    };
    auto it = exact.find(lowName);
    return it == exact.end() ? std::string() : it->second;
}
std::string phraseFolder(const std::string& lowName)
{
    static const std::map<std::string, std::string> words = {
        { "texture", "Textures" }, { "textures", "Textures" }, { "map", "Maps" }, { "maps", "Maps" }, { "level", "Maps" },
        { "levels", "Maps" }, { "sound", "Sounds" }, { "sounds", "Sounds" }, { "model", "Models" }, { "models", "Models" },
        { "campaign", "Campaigns" }, { "campaigns", "Campaigns" }, { "animation", "Animations" }, { "animations", "Animations" },
        { "skeleton", "Skeleton" }, { "skeletons", "Skeleton" }, { "music", "Music" },
    };
    // only a folder that tells you where to put it ("This goes into Maps", "Textures folder",
    // "put in sounds") or is just the word ("Map"): a mod's own name like "Sven Campaign" isn't
    static const std::set<std::string> say = { "goes", "go", "into", "in", "put", "copy", "folder", "dir", "directory", "place", "add", "to" };
    std::string found, word;
    bool clash = false, told = false;
    int count = 0;
    for (char c : lowName + " ") {
        if (isalnum((unsigned char)c)) {
            word += c;
            continue;
        }
        if (!word.empty()) {
            count++;
            told |= say.count(word) != 0;
            auto it = words.find(word);
            if (it != words.end()) {
                clash |= !found.empty() && found != it->second;
                found = it->second;
            }
        }
        word.clear();
    }
    return clash || (!told && count != 1) ? std::string() : found;
}

// does a file fit the game folder its path names (else it is sorted by its contents)
bool fitsFolder(const std::string& folder, const std::string& name, Zip& zip, mz_uint index)
{
    const std::string low = fsu::Lower(name);
    const size_t dot = low.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : low.substr(dot + 1);
    const bool image = ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "bmp";
    const bool sound = ext == "ogg" || ext == "wav" || ext == "mp3";
    if (folder == "Textures" || folder == "Fonts") {
        return image;
    }
    if (folder == "Sounds" || folder == "Music") {
        return sound;
    }
    if (folder == "Models") {
        return ext == "solid" || ext.empty() || image; // (custom models: skeleton files and Skin.png too)
    }
    if (folder == "Campaigns") {
        return ext == "txt" && fsu::Lower(peek(zip, index, 256)).find("campaign levels:") != std::string::npos;
    }
    if (folder == "Maps") {
        return ext.empty() || ext == "json";
    }
    if (folder == "Skeleton" || folder == "Animations") {
        return ext.empty();
    }
    return isDataFile(name); // (Dialogues and the rest)
}

bool isJunk(const std::vector<std::string>& parts)
{
    for (const std::string& p : parts) {
        const std::string l = fsu::Lower(p);
        if (l == "__macosx" || p[0] == '.') {
            return true; // Mac resource forks (._name), .DS_Store, hidden folders
        }
    }
    const std::string l = fsu::Lower(parts.back());
    return l == "thumbs.db" || l == "desktop.ini" || l == "icon\r";
}

// what a file is by its contents (no folder said): "" = not game data
std::string sniff(Zip& zip, mz_uint index, const std::string& name, bool strongOnly)
{
    const std::string low = fsu::Lower(name);
    const size_t dot = low.rfind('.');
    const std::string ext = dot == std::string::npos ? "" : low.substr(dot + 1);
    if (ext.empty()) {
        const std::string head = peek(zip, index, 4);
        const uint32_t v = head.size() == 4 ? ((uint32_t)(uint8_t)head[0] << 24 | (uint32_t)(uint8_t)head[1] << 16 | (uint32_t)(uint8_t)head[2] << 8 |
                                               (uint8_t)head[3])
                                            : 0;
        return v >= 1 && v <= 64 ? "Maps" : ""; // (a Lugaru map starts with its version)
    }
    if (ext == "txt") {
        return fsu::Lower(peek(zip, index, 256)).find("campaign levels:") != std::string::npos ? "Campaigns" : "";
    }
    if (ext == "json") {
        return peek(zip, index, 4096).find("\"map\"") != std::string::npos ? "Maps" : ""; // OSS Lugaru's json maps
    }
    if (strongOnly) {
        return ""; // (a mod laid out in folders: loose pictures next to them are its readme's)
    }
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "bmp") {
        return "Textures";
    }
    if (ext == "ogg" || ext == "wav" || ext == "mp3") {
        return "Sounds";
    }
    if (ext == "solid") {
        return "Models";
    }
    return "";
}

// the entries that make up the mod, each with its place in the game's Data layout
bool collect(Zip& zip, std::vector<Item>& items, int& modTxtIndex, std::string& error, int& skipped)
{
    modTxtIndex = -1;
    size_t modTxtDepth = 0;
    const mz_uint n = mz_zip_reader_get_num_files(&zip.z);
    struct File
    {
        mz_uint index;
        std::vector<std::string> parts;
        std::string folder; // the game folder its path names ("" = none)
        size_t restFrom = 0; // the parts after that folder
        uint64_t size = 0;
    };
    std::vector<File> files;
    bool anyHint = false;
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip.z, i, &st) || st.m_is_directory) {
            continue;
        }
        File f;
        f.index = i;
        f.size = st.m_uncomp_size;
        if (!safeParts(st.m_filename, f.parts)) {
            error = std::string("unsafe path in the zip: ") + st.m_filename;
            return false;
        }
        if (isJunk(f.parts)) {
            continue;
        }
        if (fsu::Lower(f.parts.back()) == "mod.txt") {
            // the mod's description: the one nearest the top of the zip
            if (modTxtIndex < 0 || f.parts.size() < modTxtDepth) {
                modTxtIndex = (int)i;
                modTxtDepth = f.parts.size();
            }
            continue;
        }
        // folders to read: those after a "Data" folder if there is one
        size_t from = 0;
        for (size_t k = 0; k + 1 < f.parts.size(); k++) {
            if (fsu::Lower(f.parts[k]) == "data") {
                from = k + 1;
            }
        }
        // a folder named exactly like a game folder (the nearest the top), else one whose
        // name says it ("This goes into Maps"; the nearest the file)
        for (size_t k = from; k + 1 < f.parts.size() && f.folder.empty(); k++) {
            f.folder = canonFolder(fsu::Lower(f.parts[k]));
            f.restFrom = k + 1;
        }
        for (size_t k = f.parts.size() - 1; f.folder.empty() && k-- > from;) {
            f.folder = phraseFolder(fsu::Lower(f.parts[k]));
            f.restFrom = k + 1;
        }
        anyHint |= !f.folder.empty();
        files.push_back(f);
    }
    std::set<std::string> taken;
    uint64_t total = 0;
    for (File& f : files) {
        std::string rel;
        if (!f.folder.empty() && !fitsFolder(f.folder, f.parts.back(), zip, f.index)) {
            f.folder.clear(); // (a readme in "This goes into Maps", a picture in Campaigns...)
        }
        if (!f.folder.empty()) {
            rel = f.folder;
            for (size_t k = f.restFrom; k < f.parts.size(); k++) {
                rel += "/" + f.parts[k];
            }
        } else {
            const std::string folder = sniff(zip, f.index, f.parts.back(), anyHint);
            if (folder.empty()) {
                skipped++; // readmes, previews, anything unknown
                continue;
            }
            rel = folder + "/" + f.parts.back();
        }
        if (!taken.insert(fsu::Lower(rel)).second) {
            skipped++; // (the same file twice: the first one counts)
            continue;
        }
        total += f.size;
        items.push_back({ f.index, rel, f.size });
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

bool Mod_InstallZip(const std::string& zipPath, const std::string& modsDir, std::string& folderOut, std::string& error, int* skippedOut)
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
    if (skippedOut) {
        *skippedOut = skipped;
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
    // A campaign called "main" was made to replace the game's own (the old way to install
    // one): it becomes this mod's campaign instead, next to the main one. With a single
    // campaign, its world map (Textures/World.png) becomes that campaign's map only.
    {
        std::string camp; // (campaign names can't hold spaces)
        for (char c : folder) {
            if (c != ' ') {
                camp += c;
            }
        }
        std::vector<Item*> campaigns;
        Item* world = nullptr;
        for (Item& it : items) {
            const std::string l = fsu::Lower(it.rel);
            if (l.compare(0, 10, "campaigns/") == 0 && l.find('/', 10) == std::string::npos && l.size() > 14 && l.compare(l.size() - 4, 4, ".txt") == 0) {
                campaigns.push_back(&it);
            }
            if (l == "textures/world.png" || l == "textures/world.jpg") {
                world = &it;
            }
        }
        for (Item* c : campaigns) {
            if (fsu::Lower(c->rel) == "campaigns/main.txt") {
                c->rel = "Campaigns/" + camp + ".txt";
            }
        }
        if (campaigns.size() == 1 && world) {
            const std::string stem = campaigns[0]->rel.substr(10, campaigns[0]->rel.size() - 14);
            world->rel = "Textures/" + stem + "/World" + world->rel.substr(world->rel.rfind('.'));
        }
    }
    // no mod.txt: describe the mod with the start of its readme
    std::string description;
    if (modTxt < 0) {
        mz_uint best = 0;
        size_t bestDepth = 99;
        const mz_uint n = mz_zip_reader_get_num_files(&zip.z);
        for (mz_uint i = 0; i < n; i++) {
            mz_zip_archive_file_stat st;
            std::vector<std::string> parts;
            if (!mz_zip_reader_file_stat(&zip.z, i, &st) || st.m_is_directory || !safeParts(st.m_filename, parts) || isJunk(parts)) {
                continue;
            }
            std::string l = fsu::Lower(parts.back());
            l.erase(std::remove(l.begin(), l.end(), ' '), l.end());
            const bool readme = l == "readme" || l.compare(0, 7, "readme.") == 0 || l == "readme.txt";
            if (readme && parts.size() < bestDepth && st.m_uncomp_size < (1u << 20)) {
                best = i;
                bestDepth = parts.size();
            }
        }
        if (bestDepth < 99) {
            std::string t = peek(zip, best, 2000), para;
            for (char& c : t) {
                if (c == '\r' || c == '\t') {
                    c = c == '\t' ? ' ' : '\n';
                }
            }
            size_t a = 0;
            while (a < t.size() && para.empty()) { // the first paragraph that says something
                size_t b = t.find("\n\n", a);
                para = fsu::Trim(t.substr(a, b == std::string::npos ? std::string::npos : b - a));
                a = b == std::string::npos ? t.size() : b + 2;
            }
            for (char& c : para) {
                if (c == '\n' || (unsigned char)c < 32 || (unsigned char)c > 126) {
                    c = ' ';
                }
            }
            if (para.size() > 200) {
                para = para.substr(0, 197) + "...";
            }
            description = para;
        }
    }
    // unpack into a hidden folder first, so a failed install leaves nothing behind
    const std::string tmp = fsu::Join(modsDir, ".installing");
    fsu::RemoveTree(tmp);
    fsu::MakeDirs(tmp);
    if (!extractItems(zip, items, tmp, error)) {
        fsu::RemoveTree(tmp);
        return false;
    }
    // campaign files written on old Macs end their lines with CR only: the game's end text
    // needs them as lines
    for (const std::string& c : fsu::List(fsu::Join(tmp, "Campaigns"), false)) {
        const std::string p = fsu::Join(fsu::Join(tmp, "Campaigns"), c);
        std::string t, o;
        if (fsu::ReadAll(p, t) && t.find('\r') != std::string::npos) {
            for (size_t i = 0; i < t.size(); i++) {
                if (t[i] == '\r') {
                    o += '\n';
                    if (i + 1 < t.size() && t[i + 1] == '\n') {
                        i++;
                    }
                } else {
                    o += t[i];
                }
            }
            fsu::WriteAll(p, o);
        }
    }
    addLevelPackCampaign(tmp, folder);
    if (modTxtData.empty()) {
        modTxtData = "name=" + folder + "\n"; // (a second copy shows as "Name 2")
        if (!description.empty()) {
            modTxtData += "description=" + description + "\n";
        }
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

namespace
{
std::string extOf(const std::string& path)
{
    const std::string b = fsu::BaseName(path);
    const size_t dot = b.rfind('.');
    return dot == std::string::npos ? std::string() : fsu::Lower(b.substr(dot + 1));
}

bool isOtherArchive(const std::string& ext)
{
    static const std::set<std::string> a = { "rar", "7z", "tar", "gz", "tgz", "xz", "bz2", "zst", "cab", "lzh", "arj", "sit", "sitx", "dmg" };
    return a.count(ext) != 0;
}

bool isProgram(const std::string& ext)
{
    static const std::set<std::string> p = { "exe", "dll", "bat", "cmd", "com", "scr", "msi", "ps1", "vbs", "js", "jar", "app", "dylib", "so", "sh", "py" };
    return p.count(ext) != 0;
}

// pictures and sounds in formats the game can't read (it reads .png/.jpg and .ogg/.mp3/.wav)
bool isUnreadableAsset(const std::string& ext)
{
    static const std::set<std::string> u = { "tga", "bmp", "dds", "tif", "tiff", "gif", "psd", "webp", "flac", "m4a", "aac", "wma" };
    return u.count(ext) != 0;
}

// what to tell the player about files that were left out or won't work ("" = nothing worth
// saying: readmes and the like are left out quietly)
std::string leftOutNote(int programs, const std::string& firstProgram, int unreadable, const std::string& firstUnreadable)
{
    std::string t;
    if (programs > 0) {
        t = "Left out " + firstProgram + (programs > 1 ? " and " + std::to_string(programs - 1) + " other program file(s)" : std::string()) +
            ": only game data is installed (maps, textures, sounds, models...). A mod that needs its own Lugaru.exe only works in part.";
    }
    if (unreadable > 0) {
        t += (t.empty() ? "" : " ") + firstUnreadable + (unreadable > 1 ? " and " + std::to_string(unreadable - 1) + " other file(s)" : std::string()) +
             " can't be used: the game reads pictures as .png or .jpg and sounds as .ogg, .mp3 or .wav.";
    }
    return t;
}

// zip an unpacked mod folder (files that could be game data, mod.txt and readmes) so it goes
// through the same installer as a downloaded zip
bool zipFolder(const std::string& folder, const std::string& zipPath, int& programs, std::string& firstProgram, int& unreadable, std::string& firstUnreadable)
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
    uint64_t total = 0;
    int added = 0;
    for (const std::string& rel : files) {
        if (!ok) {
            break;
        }
        const std::string base = fsu::BaseName(rel);
        if (isProgram(extOf(base))) {
            if (programs++ == 0) {
                firstProgram = base;
            }
            continue;
        }
        if (isUnreadableAsset(extOf(base)) && unreadable++ == 0) {
            firstUnreadable = base;
        }
        if (!isDataFile(base) || ++added > kMaxModFiles) {
            continue;
        }
        std::string data;
        if (!fsu::ReadAll(fsu::Join(folder, rel), data)) {
            continue;
        }
        total += data.size();
        if (total > kMaxModBytes) {
            ok = false;
            break;
        }
        ok = mz_zip_writer_add_mem(&z, rel.c_str(), data.data(), data.size(), MZ_NO_COMPRESSION);
    }
    ok = ok && mz_zip_writer_finalize_archive(&z);
    mz_zip_writer_end(&z);
    fclose(f);
    return ok;
}
} // namespace

bool Mod_Install(const std::string& rawPath, const std::string& modsDir, std::string& folderOut, std::string& error, std::string& note)
{
    note.clear();
    std::string path = rawPath;
    while (path.size() > 3 && (path.back() == '/' || path.back() == '\\')) {
        path.pop_back();
    }
    const std::string tmp = fsu::Join(modsDir, ".adding");
    if (fsu::IsDir(path)) {
        // an unpacked mod; named after its folder (the one above when it is the "Data" folder itself)
        std::string name = fsu::BaseName(path);
        if (fsu::Lower(name) == "data" && !fsu::BaseName(fsu::Parent(path)).empty()) {
            name = fsu::BaseName(fsu::Parent(path));
        }
        fsu::RemoveTree(tmp);
        fsu::MakeDirs(tmp);
        const std::string zip = fsu::Join(tmp, Mod_SafeName(name) + ".zip");
        int programs = 0, unreadable = 0;
        std::string firstProgram, firstUnreadable;
        if (!zipFolder(path, zip, programs, firstProgram, unreadable, firstUnreadable)) {
            fsu::RemoveTree(tmp);
            error = "couldn't read the folder (or it is over 1 GB)";
            return false;
        }
        const bool ok = Mod_InstallZip(zip, modsDir, folderOut, error);
        fsu::RemoveTree(tmp);
        if (!ok && programs > 0 && error.compare(0, 13, "no game files") == 0) {
            error = "it only has programs (" + firstProgram + "): LugaruEX mods are game data (maps, textures, sounds, models...)";
        }
        if (ok) {
            note = leftOutNote(programs, firstProgram, unreadable, firstUnreadable);
        }
        return ok;
    }
    const std::string ext = extOf(path);
    if (isOtherArchive(ext)) {
        error = "it is a ." + ext + " archive: unpack it first (Windows: right-click > Extract All, or 7-Zip), then add the unpacked folder";
        return false;
    }
    if (!fsu::IsFile(path)) {
        error = "not found";
        return false;
    }
    Zip zip(path);
    if (!zip.ok) {
        error = ext == "zip" ? std::string("the zip is damaged (download it again)") : "not a mod: add a mod's .zip file or its unpacked folder";
        return false;
    }
    // programs in it (left out), and archives inside it (a mod zipped twice)
    int programs = 0;
    std::string firstProgram, firstUnreadable;
    int unreadable = 0;
    std::vector<mz_uint> innerZips;
    std::string innerOther;
    const mz_uint n = mz_zip_reader_get_num_files(&zip.z);
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        std::vector<std::string> parts;
        if (!mz_zip_reader_file_stat(&zip.z, i, &st) || st.m_is_directory || !safeParts(st.m_filename, parts) || isJunk(parts)) {
            continue;
        }
        const std::string e = extOf(parts.back());
        if (isUnreadableAsset(e) && unreadable++ == 0) {
            firstUnreadable = parts.back();
        }
        if (isProgram(e) && programs++ == 0) {
            firstProgram = parts.back();
        }
        if (e == "zip") {
            innerZips.push_back(i);
        } else if (isOtherArchive(e) && innerOther.empty()) {
            innerOther = e;
        }
    }
    if (Mod_InstallZip(path, modsDir, folderOut, error)) {
        note = leftOutNote(programs, firstProgram, unreadable, firstUnreadable);
        return true;
    }
    if (error.compare(0, 13, "no game files") != 0) {
        return false;
    }
    static int unwrapping = 0; // (one zip inside another; not deeper)
    if (innerZips.size() == 1 && unwrapping == 0) {
        // the mod's own zip inside this one: install that, under this zip's name
        std::string name = fsu::BaseName(path);
        name = name.substr(0, name.rfind('.'));
        const std::string tmpInner = fsu::Join(modsDir, ".adding-inner"); // (not .adding: installing it uses that)
        fsu::RemoveTree(tmpInner);
        fsu::MakeDirs(tmpInner);
        const std::string inner = fsu::Join(tmpInner, Mod_SafeName(name) + ".zip");
        mz_zip_archive_file_stat st;
        bool ok = mz_zip_reader_file_stat(&zip.z, innerZips[0], &st) && st.m_uncomp_size <= kMaxModBytes;
        if (ok) {
            FILE* f = fsu::Open(inner, "wb");
            ok = f && mz_zip_reader_extract_to_callback(&zip.z, innerZips[0], writeToFile, f, 0);
            if (f) {
                fclose(f);
            }
        }
        if (!ok) {
            error = "the zip is damaged (download it again)";
        } else {
            unwrapping++;
            ok = Mod_Install(inner, modsDir, folderOut, error, note);
            unwrapping--;
        }
        fsu::RemoveTree(tmpInner);
        return ok;
    }
    if (innerZips.size() > 1) {
        error = "it holds several zips: add each of them (unpack this one first)";
    } else if (!innerOther.empty()) {
        error = "the mod inside is a ." + innerOther + " archive: unpack it (Windows: right-click > Extract All, or 7-Zip), then add the unpacked folder";
    } else if (programs > 0) {
        error = "it only has programs (" + firstProgram + "): LugaruEX mods are game data (maps, textures, sounds, models...)";
    } else if (unreadable > 0) {
        error = "its files (" + firstUnreadable + ") are in formats the game can't read: pictures need to be .png or .jpg, sounds .ogg, .mp3 or .wav";
    }
    return false;
}

bool Mod_ExtractTo(const std::string& zipPath, const std::string& destDir, std::string& error, uint64_t maxBytes)
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
    if (maxBytes) {
        uint64_t total = 0;
        for (const Item& it : items) {
            total += it.size;
        }
        if (total > maxBytes) {
            error = "it unpacks to more than it should";
            return false;
        }
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

uint64_t Mod_Fingerprint(const std::string& folder)
{
    // the files sharing would send (Mod_PackFolder's), names and bytes: the same mod gives
    // the same number on every PC, any change another
    std::vector<std::string> files;
    listFiles(folder, "", files);
    std::sort(files.begin(), files.end());
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* d, size_t n) {
        const unsigned char* p = (const unsigned char*)d;
        for (size_t i = 0; i < n; i++) {
            h = (h ^ p[i]) * 1099511628211ull;
        }
    };
    for (const std::string& rel : files) {
        if (rel.find('/') == std::string::npos || !isDataFile(fsu::BaseName(rel))) {
            continue;
        }
        std::string data;
        if (!fsu::ReadAll(fsu::Join(folder, rel), data)) {
            continue;
        }
        const std::string name = fsu::Lower(rel);
        mix(name.data(), name.size() + 1);
        mix(data.data(), data.size());
    }
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
