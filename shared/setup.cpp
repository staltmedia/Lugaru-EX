// LugaruEX - installing and updating (GPLv2+)
#include "setup.h"

#include "fsutil.h"
#include "update.h" // (Http_CleanEnv)
#include "miniz/miniz.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#else
#include <glob.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
std::wstring W(const std::string& s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring w(n > 0 ? n - 1 : 0, 0);
    if (n > 1) {
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    }
    return w;
}
std::string U(const std::wstring& w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(n > 0 ? n - 1 : 0, 0);
    if (n > 1) {
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, NULL, NULL);
    }
    return s;
}
std::string regString(HKEY root, const wchar_t* key, const wchar_t* value, REGSAM view = 0)
{
    HKEY k;
    if (RegOpenKeyExW(root, key, 0, KEY_READ | view, &k) != ERROR_SUCCESS) {
        return "";
    }
    wchar_t buf[1024] = {};
    DWORD size = sizeof(buf) - sizeof(wchar_t), type = 0;
    std::string r;
    if (RegQueryValueExW(k, value, NULL, &type, (BYTE*)buf, &size) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        r = U(buf);
    }
    RegCloseKey(k);
    return r;
}
std::string env(const char* name)
{
    wchar_t buf[1024] = {};
    std::wstring n(name, name + strlen(name));
    return GetEnvironmentVariableW(n.c_str(), buf, 1024) ? U(buf) : "";
}
#else
std::string env(const char* name)
{
    const char* v = getenv(name);
    return v ? v : "";
}
#endif

std::string norm(std::string p)
{
#ifdef _WIN32
    std::replace(p.begin(), p.end(), '/', '\\');
#endif
    while (p.size() > 3 && (p.back() == '/' || p.back() == '\\')) {
        p.pop_back();
    }
    return p;
}

bool writable(const std::string& dir)
{
    const std::string t = fsu::Join(dir, ".lugaruex-write-test");
    if (!fsu::WriteAll(t, "x")) {
        return false;
    }
    fsu::RemoveTree(t);
    return true;
}

// "path" entries of Steam's libraryfolders.vdf
std::vector<std::string> steamLibraries(const std::string& steamRoot)
{
    std::vector<std::string> libs = { steamRoot };
    std::string vdf;
    if (fsu::ReadAll(fsu::Join(steamRoot, "steamapps/libraryfolders.vdf"), vdf)) {
        size_t a = 0;
        while ((a = vdf.find("\"path\"", a)) != std::string::npos) {
            a += 6;
            const size_t q1 = vdf.find('"', a);
            const size_t q2 = q1 == std::string::npos ? q1 : vdf.find('"', q1 + 1);
            if (q2 == std::string::npos) {
                break;
            }
            std::string p = vdf.substr(q1 + 1, q2 - q1 - 1), un;
            for (size_t i = 0; i < p.size(); i++) {
                if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\') {
                    i++;
                }
                un += p[i];
            }
            libs.push_back(un);
            a = q2 + 1;
        }
    }
    return libs;
}

void add(std::vector<GameFolder>& out, std::set<std::string>& seen, const std::string& dir, const std::string& kind, bool steam)
{
    const std::string p = norm(dir);
    if (p.empty() || seen.count(fsu::Lower(p))) {
        return;
    }
    GameFolder g = Setup_Describe(p);
    if (g.path.empty()) {
        return;
    }
    seen.insert(fsu::Lower(p));
    if (!kind.empty()) {
        g.kind = kind;
    }
    g.steam = g.steam || steam;
    out.push_back(g);
}

bool safeRel(const std::string& rel)
{
    if (rel.empty() || rel[0] == '/' || rel[0] == '\\' || rel.find(':') != std::string::npos) {
        return false;
    }
    std::string cur;
    for (char c : rel + "/") {
        if (c == '/' || c == '\\') {
            if (cur == "..") {
                return false;
            }
            cur.clear();
        } else {
            cur += c;
        }
    }
    return true;
}

// this user's LugaruEX folder (saves, logs; here: the list of installs)
std::string userDir()
{
#ifdef _WIN32
    return env("APPDATA") + "\\LugaruEX";
#else
    return (env("XDG_DATA_HOME").empty() ? env("HOME") + "/.local/share" : env("XDG_DATA_HOME")) + "/LugaruEX";
#endif
}

// game folders LugaruEX was installed into (so a later run finds them, wherever they are)
std::vector<std::string> knownInstalls()
{
    std::vector<std::string> out;
    std::string t;
    if (fsu::ReadAll(fsu::Join(userDir(), "installs.txt"), t)) {
        size_t a = 0;
        while (a < t.size()) {
            size_t b = t.find('\n', a);
            if (b == std::string::npos) {
                b = t.size();
            }
            std::string l = t.substr(a, b - a);
            while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) {
                l.pop_back();
            }
            if (!l.empty()) {
                out.push_back(l);
            }
            a = b + 1;
        }
    }
    return out;
}

void writeInstalls(const std::vector<std::string>& list)
{
    std::string t;
    for (const std::string& p : list) {
        t += p + "\n";
    }
    fsu::MakeDirs(userDir());
    fsu::WriteAll(fsu::Join(userDir(), "installs.txt"), t);
}

void rememberInstall(const std::string& dir)
{
    std::vector<std::string> l = knownInstalls();
    for (const std::string& p : l) {
        if (fsu::Lower(norm(p)) == fsu::Lower(norm(dir))) {
            return;
        }
    }
    l.push_back(norm(dir));
    writeInstalls(l);
}

void forgetInstall(const std::string& dir)
{
    std::vector<std::string> l = knownInstalls(), keep;
    for (const std::string& p : l) {
        if (fsu::Lower(norm(p)) != fsu::Lower(norm(dir))) {
            keep.push_back(p);
        }
    }
    writeInstalls(keep);
}

const char* const kOwnCopyMark = ".lugaruex-free-game";

// the LugaruEX version in a game folder: LugaruEX/version.txt, else the player README's
// first line ("LugaruEX 0.4.12 - ..."); "?" when LugaruEX is there but says no version
std::string installedVersion(const std::string& gameDir)
{
    const std::string ex = fsu::Join(gameDir, "LugaruEX");
#ifdef _WIN32
    const bool engine = fsu::IsFile(fsu::Join(ex, "LugaruEX.exe"));
#else
    const bool engine = fsu::IsFile(fsu::Join(ex, "lugaruex"));
#endif
    if (!engine) {
        return "";
    }
    std::string t;
    if (fsu::ReadAll(fsu::Join(ex, "version.txt"), t) || fsu::ReadAll(fsu::Join(ex, "README.txt"), t)) {
        size_t a = t.find_first_of("0123456789");
        if (a != std::string::npos && a < 40) {
            size_t b = a;
            while (b < t.size() && (isdigit((unsigned char)t[b]) || t[b] == '.')) {
                b++;
            }
            return t.substr(a, b - a);
        }
    }
    return "?";
}

// write src over dst; a file in use (the running launcher) is moved aside to dst.old first
bool replaceFile(const std::string& src, const std::string& dst)
{
    const std::string tmp = dst + ".new";
    if (!fsu::Copy(src, tmp)) {
        return false;
    }
    if (fsu::Rename(tmp, dst)) {
        return true;
    }
    fsu::RemoveTree(dst + ".old");
    if (fsu::Rename(dst, dst + ".old") && fsu::Rename(tmp, dst)) {
        return true;
    }
    fsu::RemoveTree(tmp);
    return false;
}

bool copyTree(const std::string& src, const std::string& dst, std::string& error)
{
    fsu::MakeDirs(dst);
    for (const std::string& f : fsu::List(src, false)) {
        if (fsu::Lower(f) == "lugaruex.ini") {
            continue; // never the player's settings
        }
        if (!replaceFile(fsu::Join(src, f), fsu::Join(dst, f))) {
            error = "can't write " + fsu::Join(dst, f) + " (is the game running?)";
            return false;
        }
#ifndef _WIN32
        struct stat st;
        if (stat(fsu::Join(src, f).c_str(), &st) == 0) {
            chmod(fsu::Join(dst, f).c_str(), st.st_mode & 0777);
        }
#endif
    }
    for (const std::string& d : fsu::List(src, true)) {
        if (!copyTree(fsu::Join(src, d), fsu::Join(dst, d), error)) {
            return false;
        }
    }
    return true;
}

void shortcut(const std::string& launcher, const std::string& gameDir)
{
#ifdef _WIN32
    IShellLinkW* link = NULL;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&link))) {
        return;
    }
    link->SetPath(W(launcher).c_str());
    link->SetWorkingDirectory(W(gameDir).c_str());
    link->SetDescription(L"Lugaru HD with the LugaruEX engine");
    IPersistFile* pf = NULL;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, (void**)&pf))) {
        for (int where : { CSIDL_DESKTOPDIRECTORY, CSIDL_PROGRAMS }) {
            wchar_t dir[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(NULL, where, NULL, 0, dir))) {
                pf->Save((std::wstring(dir) + L"\\LugaruEX.lnk").c_str(), TRUE);
            }
        }
        pf->Release();
    }
    link->Release();
#else
    const std::string apps = (env("XDG_DATA_HOME").empty() ? env("HOME") + "/.local/share" : env("XDG_DATA_HOME")) + "/applications";
    fsu::MakeDirs(apps);
    const std::string ex = fsu::Parent(launcher);
    fsu::WriteAll(apps + "/lugaruex.desktop", "[Desktop Entry]\nType=Application\nName=LugaruEX\nComment=Lugaru with the LugaruEX engine\n"
                                              "Exec=\"" + launcher + "\"\nPath=" + ex + "\nIcon=" + ex + "/lugaruex.png\nTerminal=false\n"
                                              "Categories=Game;ActionGame;\n");
#endif
}

size_t writeOut(void* opaque, mz_uint64 ofs, const void* buf, size_t n)
{
    FILE* f = (FILE*)opaque;
    return fseek(f, (long)ofs, SEEK_SET) == 0 ? fwrite(buf, 1, n, f) : 0;
}

bool unzipTo(const std::string& zipPath, const std::string& dir, const std::string& stripTo, std::string& error)
{
    FILE* f = fsu::Open(zipPath, "rb");
    if (!f) {
        error = "can't open " + zipPath;
        return false;
    }
    fseek(f, 0, SEEK_END);
    const long long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    mz_zip_archive z;
    memset(&z, 0, sizeof(z));
    if (size <= 0 || !mz_zip_reader_init_cfile(&z, f, (mz_uint64)size, 0)) {
        fclose(f);
        error = "damaged zip";
        return false;
    }
    bool ok = true;
    int written = 0;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z) && ok; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st) || st.m_is_directory) {
            continue;
        }
        std::string name = st.m_filename;
        std::replace(name.begin(), name.end(), '\\', '/');
        if (!stripTo.empty()) { // keep only what is below a ".../<stripTo>/" folder
            const size_t k = ("/" + name).find("/" + stripTo + "/");
            if (k == std::string::npos) {
                continue;
            }
            name = stripTo + "/" + name.substr(k + stripTo.size() + 1);
        }
        if (!safeRel(name)) {
            error = "unsafe path in the zip";
            ok = false;
            break;
        }
        const std::string out = fsu::Join(dir, name);
        fsu::MakeDirs(fsu::Parent(out));
        FILE* o = fsu::Open(out + ".tmp", "wb");
        ok = o && mz_zip_reader_extract_to_callback(&z, i, writeOut, o, 0);
        if (o) {
            fclose(o);
        }
        ok = ok && fsu::Rename(out + ".tmp", out);
        if (!ok) {
            error = "can't unpack " + name;
        }
        written++;
    }
    mz_zip_reader_end(&z);
    fclose(f);
    if (ok && written == 0) {
        error = "nothing to unpack";
        ok = false;
    }
    return ok;
}
} // namespace

bool Setup_IsGameFolder(const std::string& dir)
{
    return !dir.empty() && fsu::IsDir(fsu::Join(dir, "Data/Maps"));
}

GameFolder Setup_Describe(const std::string& dirIn)
{
    GameFolder g;
    const std::string dir = norm(dirIn);
    if (!Setup_IsGameFolder(dir)) {
#ifndef _WIN32
        if (fsu::IsDir(fsu::Join(dir, "Maps")) && fsu::IsDir(fsu::Join(dir, "Textures"))) {
            g.path = dir; // a system install's data folder (no Data/ around it)
            g.kind = "Lugaru (open source, installed for the system)";
            g.writable = false;
        }
#endif
        return g;
    }
    g.path = dir;
    const std::string low = fsu::Lower(dir);
    g.steam = low.find("steamapps") != std::string::npos;
    const bool hd = fsu::IsFile(fsu::Join(dir, "Data/Textures/Lugaru.png")) && (fsu::IsFile(fsu::Join(dir, "Lugaru.exe")) || g.steam);
    g.kind = g.steam ? "Lugaru HD (Steam)" : (hd ? "Lugaru HD" : "Lugaru (open source)");
    if (fsu::Lower(norm(dir)) == fsu::Lower(norm(Setup_FreeGameFolder()))) {
        g.kind = "Lugaru (free open-source data)";
    }
    g.writable = writable(dir);
    g.exVersion = installedVersion(dir);
    g.hasLugaruEX = !g.exVersion.empty();
    g.ownCopy = !g.steam && (fsu::IsFile(fsu::Join(dir, kOwnCopyMark)) || fsu::Lower(norm(dir)) == fsu::Lower(norm(Setup_FreeGameFolder())));
    return g;
}

void Setup_MarkOwnCopy(const std::string& dir)
{
    fsu::WriteAll(fsu::Join(dir, kOwnCopyMark), "A copy of the free Lugaru game data downloaded by LugaruEX (its installer may delete this folder).\n");
    rememberInstall(dir);
}

int Setup_CompareVersions(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = 0, y = 0;
        while (i < a.size() && a[i] != '.') {
            x = x * 10 + (isdigit((unsigned char)a[i]) ? a[i] - '0' : 0);
            i++;
        }
        while (j < b.size() && b[j] != '.') {
            y = y * 10 + (isdigit((unsigned char)b[j]) ? b[j] - '0' : 0);
            j++;
        }
        if (x != y) {
            return x < y ? -1 : 1;
        }
        i++;
        j++;
    }
    return 0;
}

bool Setup_Remove(const GameFolder& game, bool wholeFolder, std::string& error)
{
    const std::string dir = norm(game.path);
    if (dir.empty() || !Setup_IsGameFolder(dir)) {
        error = "not a game folder";
        return false;
    }
    if (wholeFolder) {
        // only a free copy LugaruEX itself downloaded, never Steam's or anyone else's game
        const GameFolder g = Setup_Describe(dir);
        if (!g.ownCopy || g.steam) {
            error = "only a copy LugaruEX downloaded can be deleted as a whole";
            return false;
        }
        if (!fsu::RemoveTree(dir)) {
            error = "can't delete " + dir + " (is the game running?)";
            return false;
        }
        forgetInstall(dir);
        return true;
    }
    const std::string ex = fsu::Join(dir, "LugaruEX");
    if (!fsu::IsDir(ex)) {
        error = "LugaruEX is not in " + dir;
        return false;
    }
#ifdef _WIN32
    // Steam's Play button: the game's own Lugaru.exe back
    const std::string backup = fsu::Join(ex, "backup\\Lugaru.exe"), gameExe = fsu::Join(dir, "Lugaru.exe");
    if (fsu::IsFile(backup) && !replaceFile(backup, gameExe)) {
        error = "can't put the game's Lugaru.exe back (is it running?)";
        return false;
    }
#else
    // its app menu entry, when it points here
    const std::string desk = (env("XDG_DATA_HOME").empty() ? env("HOME") + "/.local/share" : env("XDG_DATA_HOME")) + "/applications/lugaruex.desktop";
    std::string d;
    if (fsu::ReadAll(desk, d) && d.find(ex + "/") != std::string::npos) {
        fsu::RemoveTree(desk);
    }
#endif
    if (!fsu::RemoveTree(ex)) {
        error = "can't delete " + ex + " (is the game running?)";
        return false;
    }
    forgetInstall(dir);
    return true;
}

// ---- Steam: non-Steam game shortcut (config/shortcuts.vdf, Valve's binary key-value format)
namespace
{
struct VNode
{
    uint8_t type = 0; // 0 map, 1 string, 2 int32, 7 uint64, others kept as raw bytes
    std::string name, str;
    std::vector<uint8_t> raw;
    std::vector<VNode> kids;
};

bool vRead(const std::string& d, size_t& p, std::vector<VNode>& out, int depth)
{
    if (depth > 16) {
        return false;
    }
    while (p < d.size()) {
        const uint8_t t = (uint8_t)d[p++];
        if (t == 8) {
            return true;
        }
        VNode n;
        n.type = t;
        const size_t z = d.find('\0', p);
        if (z == std::string::npos) {
            return false;
        }
        n.name = d.substr(p, z - p);
        p = z + 1;
        if (t == 0) {
            if (!vRead(d, p, n.kids, depth + 1)) {
                return false;
            }
        } else if (t == 1) {
            const size_t e = d.find('\0', p);
            if (e == std::string::npos) {
                return false;
            }
            n.str = d.substr(p, e - p);
            p = e + 1;
        } else if (t == 2 || t == 3 || t == 4 || t == 6) {
            if (p + 4 > d.size()) {
                return false;
            }
            n.raw.assign(d.begin() + p, d.begin() + p + 4);
            p += 4;
        } else if (t == 7) {
            if (p + 8 > d.size()) {
                return false;
            }
            n.raw.assign(d.begin() + p, d.begin() + p + 8);
            p += 8;
        } else {
            return false; // (wide strings: never in shortcuts.vdf)
        }
        out.push_back(n);
    }
    return false;
}

void vWrite(std::string& o, const std::vector<VNode>& list)
{
    for (const VNode& n : list) {
        o += (char)n.type;
        o += n.name;
        o += '\0';
        if (n.type == 0) {
            vWrite(o, n.kids);
        } else if (n.type == 1) {
            o += n.str;
            o += '\0';
        } else {
            o.append(n.raw.begin(), n.raw.end());
        }
    }
    o += (char)8;
}

VNode vStr(const std::string& k, const std::string& v)
{
    VNode n;
    n.type = 1;
    n.name = k;
    n.str = v;
    return n;
}

VNode vInt(const std::string& k, uint32_t v)
{
    VNode n;
    n.type = 2;
    n.name = k;
    for (int i = 0; i < 4; i++) {
        n.raw.push_back((uint8_t)(v >> (8 * i)));
    }
    return n;
}

uint32_t crc32(const std::string& s)
{
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char ch : s) {
        c ^= ch;
        for (int k = 0; k < 8; k++) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        }
    }
    return ~c;
}

std::vector<std::string> steamRootsHere()
{
    std::vector<std::string> roots;
#ifdef _WIN32
    roots.push_back(regString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"));
#else
    const std::string home = env("HOME");
    std::set<std::string> seen;
    for (const char* r : { "/.steam/steam", "/.local/share/Steam", "/.var/app/com.valvesoftware.Steam/.local/share/Steam" }) {
        char real[4096];
        const std::string p = home + r;
        if (realpath(p.c_str(), real) && seen.insert(real).second) {
            roots.push_back(real);
        }
    }
#endif
    return roots;
}
} // namespace

bool Setup_InSteam(const std::string& launcher)
{
    const std::string exe = "\"" + launcher + "\"";
    for (const std::string& root : steamRootsHere()) {
        const std::string ud = fsu::Join(root, "userdata");
        for (const std::string& id : fsu::List(ud, true)) {
            std::string d;
            if (!fsu::ReadAll(fsu::Join(fsu::Join(fsu::Join(ud, id), "config"), "shortcuts.vdf"), d)) {
                continue;
            }
            size_t p = 0;
            std::vector<VNode> top;
            if (!vRead(d, p, top, 0) || top.empty()) {
                continue;
            }
            for (const VNode& e : top[0].kids) {
                for (const VNode& f : e.kids) {
                    if (f.type == 1 && fsu::Lower(f.name) == "exe" && f.str == exe) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool Setup_SteamRunning()
{
#ifdef _WIN32
    return FindWindowW(L"vguiPopupWindow", NULL) != NULL || FindWindowW(NULL, L"Steam") != NULL;
#else
    // a process named steam (or its web helper) - read from /proc, not a shell command
    // (pgrep -f matched the shell that ran it)
    for (const std::string& pid : fsu::List("/proc", true)) {
        if (pid.empty() || pid.find_first_not_of("0123456789") != std::string::npos) {
            continue;
        }
        std::string comm;
        if (fsu::ReadAll("/proc/" + pid + "/comm", comm)) {
            while (!comm.empty() && (comm.back() == '\n' || comm.back() == ' ')) {
                comm.pop_back();
            }
            if (comm == "steam" || comm == "steamwebhelper") {
                return true;
            }
        }
    }
    return false;
#endif
}

bool Setup_CloseSteam()
{
#ifndef _WIN32
    if (system("command -v steam >/dev/null 2>&1") == 0) {
        system("steam -shutdown >/dev/null 2>&1 &");
    } else {
        system("flatpak run com.valvesoftware.Steam -shutdown >/dev/null 2>&1 &");
    }
    for (int i = 0; i < 60 && Setup_SteamRunning(); i++) {
        usleep(500000);
    }
    return !Setup_SteamRunning();
#else
    return false;
#endif
}

void Setup_StartSteam()
{
#ifndef _WIN32
    if (system("command -v steam >/dev/null 2>&1") == 0) {
        system("setsid steam >/dev/null 2>&1 &");
    } else {
        system("setsid flatpak run com.valvesoftware.Steam >/dev/null 2>&1 &");
    }
#endif
}

int Setup_AddToSteam(const std::string& launcher, const std::string& gameDir, std::string& error)
{
    if (Setup_SteamRunning()) {
        return 2;
    }
    const std::string appName = "Lugaru EX";
    const std::string exe = "\"" + launcher + "\"", start = "\"" + fsu::Parent(launcher) + "\"";
    int added = 0, already = 0, users = 0;
    for (const std::string& root : steamRootsHere()) {
        const std::string ud = fsu::Join(root, "userdata");
        for (const std::string& id : fsu::List(ud, true)) {
            if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos || id == "0") {
                continue; // (only real account folders)
            }
            users++;
            const std::string cfg = fsu::Join(fsu::Join(ud, id), "config"), file = fsu::Join(cfg, "shortcuts.vdf");
            std::string d;
            std::vector<VNode> top;
            if (fsu::ReadAll(file, d) && !d.empty()) {
                size_t p = 0;
                if (!vRead(d, p, top, 0) || top.empty() || top[0].type != 0) {
                    error = "Steam's list of non-Steam games in " + cfg + " could not be read; it was left as it is";
                    continue;
                }
            } else {
                VNode root;
                root.type = 0;
                root.name = "shortcuts";
                top.push_back(root);
            }
            std::vector<VNode>& list = top[0].kids;
            bool have = false;
            for (const VNode& e : list) {
                for (const VNode& f : e.kids) {
                    if (f.type == 1 && fsu::Lower(f.name) == "exe" && f.str == exe) {
                        have = true;
                    }
                }
            }
            if (have) {
                already++;
                continue;
            }
            VNode e;
            e.type = 0;
            e.name = std::to_string(list.size());
            e.kids.push_back(vInt("appid", crc32(exe + appName) | 0x80000000u));
            e.kids.push_back(vStr("AppName", appName));
            e.kids.push_back(vStr("Exe", exe));
            e.kids.push_back(vStr("StartDir", start));
            e.kids.push_back(vStr("icon", fsu::Join(fsu::Parent(launcher), "lugaruex.png")));
            e.kids.push_back(vStr("ShortcutPath", ""));
            e.kids.push_back(vStr("LaunchOptions", ""));
            e.kids.push_back(vInt("IsHidden", 0));
            e.kids.push_back(vInt("AllowDesktopConfig", 1));
            e.kids.push_back(vInt("AllowOverlay", 1));
            e.kids.push_back(vInt("OpenVR", 0));
            e.kids.push_back(vInt("Devkit", 0));
            e.kids.push_back(vStr("DevkitGameID", ""));
            e.kids.push_back(vInt("DevkitOverrideAppID", 0));
            e.kids.push_back(vInt("LastPlayTime", 0));
            e.kids.push_back(vStr("FlatpakAppID", ""));
            VNode tags;
            tags.type = 0;
            tags.name = "tags";
            e.kids.push_back(tags);
            list.push_back(e);
            std::string o;
            vWrite(o, top);
            fsu::MakeDirs(cfg);
            if (fsu::IsFile(file)) {
                fsu::Copy(file, file + ".lugaruex-backup"); // (Steam's own list as it was)
            }
            if (!fsu::WriteAll(file + ".tmp", o) || !fsu::Rename(file + ".tmp", file)) {
                error = "can't write " + file;
                continue;
            }
            added++;
        }
    }
    (void)gameDir;
    if (added > 0) {
        return 0;
    }
    if (already > 0) {
        return 1;
    }
    if (users == 0 && error.empty()) {
        error = "no Steam account found on this computer (start Steam and log in once)";
    }
    return -1;
}

std::string Setup_FreeGameFolder()
{
#ifdef _WIN32
    return env("LOCALAPPDATA") + "\\LugaruEX Game";
#else
    return (env("XDG_DATA_HOME").empty() ? env("HOME") + "/.local/share" : env("XDG_DATA_HOME")) + "/LugaruEX/game";
#endif
}

const char* Setup_FreeDataUrl()
{
    // OSS Lugaru's data (CC-BY-SA) at the version LugaruEX is built from
    return "https://gitlab.com/osslugaru/lugaru/-/archive/42f148d033358925d1e31cec68ad02ee03ec64c2/"
           "lugaru-42f148d033358925d1e31cec68ad02ee03ec64c2.zip?path=Data";
}

std::vector<GameFolder> Setup_FindGames()
{
    std::vector<GameFolder> out;
    std::set<std::string> seen;
    std::vector<std::string> steamRoots;
#ifdef _WIN32
    steamRoots.push_back(regString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"));
    steamRoots.push_back(regString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"));
    steamRoots.push_back(regString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath"));
#else
    const std::string home = env("HOME");
    for (const char* r : { "/.steam/steam", "/.steam/root", "/.local/share/Steam", "/.var/app/com.valvesoftware.Steam/.local/share/Steam",
                           "/snap/steam/common/.local/share/Steam" }) {
        steamRoots.push_back(home + r);
    }
#endif
    for (const std::string& root : steamRoots) {
        if (root.empty() || !fsu::IsDir(root)) {
            continue;
        }
        for (const std::string& lib : steamLibraries(root)) {
            for (const char* name : { "Lugaru HD", "Lugaru", "lugaru" }) {
                add(out, seen, fsu::Join(lib, std::string("steamapps/common/") + name), "", true);
            }
        }
    }
#ifdef _WIN32
    // installed copies (their uninstall entries know where), then the usual folders
    for (HKEY root : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        for (REGSAM view : { (REGSAM)KEY_WOW64_64KEY, (REGSAM)KEY_WOW64_32KEY }) {
            HKEY k;
            if (RegOpenKeyExW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | view, &k) != ERROR_SUCCESS) {
                continue;
            }
            wchar_t sub[256];
            for (DWORD i = 0;; i++) {
                DWORD n = 256;
                if (RegEnumKeyExW(k, i, sub, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) {
                    break;
                }
                const std::wstring key = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + sub;
                const std::string name = fsu::Lower(regString(root, key.c_str(), L"DisplayName", view));
                if (name.find("lugaru") != std::string::npos && name.find("lugaruex") == std::string::npos) {
                    add(out, seen, regString(root, key.c_str(), L"InstallLocation", view), "", false);
                }
            }
            RegCloseKey(k);
        }
    }
    for (const std::string& base : { env("ProgramFiles"), env("ProgramFiles(x86)"), std::string("C:\\Games"), env("USERPROFILE") + "\\Games" }) {
        for (const char* name : { "Lugaru HD", "Lugaru", "Wolfire Games\\Lugaru HD", "GOG Games\\Lugaru HD", "Humble\\Lugaru HD" }) {
            if (!base.empty()) {
                add(out, seen, fsu::Join(base, name), "", false);
            }
        }
    }
#else
    // open-source Lugaru from the system's packages (Flatpak too)
    for (const char* d : { "/usr/share/games/lugaru", "/usr/share/lugaru", "/usr/local/share/lugaru", "/usr/local/share/games/lugaru" }) {
        add(out, seen, d, "", false);
    }
    for (const std::string& pat : { std::string("/var/lib/flatpak/app/*ugaru*/current/active/files/share/*ugaru*"),
                                    home + "/.local/share/flatpak/app/*ugaru*/current/active/files/share/*ugaru*" }) {
        glob_t g;
        if (glob(pat.c_str(), 0, nullptr, &g) == 0) {
            for (size_t i = 0; i < g.gl_pathc; i++) {
                add(out, seen, g.gl_pathv[i], "Lugaru (open source, Flatpak)", false);
            }
        }
        globfree(&g);
    }
    for (const std::string& d : { home + "/Games/Lugaru HD", home + "/Games/Lugaru", home + "/GOG Games/Lugaru HD", home + "/lugaru" }) {
        add(out, seen, d, "", false);
    }
#endif
    add(out, seen, Setup_FreeGameFolder(), "", false);
    // every folder LugaruEX was installed into before (and free copies it downloaded)
    for (const std::string& d : knownInstalls()) {
        add(out, seen, d, "", false);
    }
    // Steam's copy first: it's what most players have
    std::stable_sort(out.begin(), out.end(), [](const GameFolder& a, const GameFolder& b) { return a.steam > b.steam; });
    return out;
}

std::string Setup_LauncherName()
{
#ifdef _WIN32
    return "LugaruEX Launcher.exe";
#else
    return "lugaruex-launcher";
#endif
}

std::string Setup_SelfPath()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH * 2);
    return U(std::wstring(buf, n));
#else
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    std::string p = n > 0 ? std::string(buf, (size_t)n) : std::string();
    // once an update has replaced the running file, the link reads "<path> (deleted)"
    const std::string del = " (deleted)";
    if (p.size() > del.size() && p.compare(p.size() - del.size(), del.size(), del) == 0) {
        p.erase(p.size() - del.size());
    }
    return p;
#endif
}

void Setup_CleanOld(const std::string& exePath)
{
    // (the old launcher may still be closing: try for a moment)
    for (int i = 0; i < 20 && fsu::IsFile(exePath + ".old") && !fsu::RemoveTree(exePath + ".old"); i++) {
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000);
#endif
    }
}

bool Setup_Install(const std::string& packageDir, const GameFolder& game, const SetupOptions& opt, std::string& launcher, std::string& error)
{
    const std::string src = fsu::Join(packageDir, "LugaruEX"), ex = fsu::Join(game.path, "LugaruEX");
    if (!fsu::IsDir(src)) {
        error = "the LugaruEX files are missing next to the installer";
        return false;
    }
    if (!game.writable || !writable(game.path)) {
        error = "can't write to " + game.path;
        return false;
    }
    // the LugaruEX folder (an update writes over the old files; settings, mods, presets,
    // texture overrides and custom skins stay)
    if (fsu::Lower(norm(src)) != fsu::Lower(norm(ex)) && !copyTree(src, ex, error)) {
        return false;
    }
#ifdef _WIN32
    const std::string exe = fsu::Join(packageDir, "Lugaru.exe");
    launcher = fsu::Join(ex, Setup_LauncherName());
    if (fsu::IsFile(exe) && fsu::Lower(norm(exe)) != fsu::Lower(norm(launcher)) && !replaceFile(exe, launcher)) {
        error = "can't write " + launcher;
        return false;
    }
    // Steam: its Play button starts <game>/Lugaru.exe - make that the launcher (once backed up)
    const std::string gameExe = fsu::Join(game.path, "Lugaru.exe"), backup = fsu::Join(ex, "backup/Lugaru.exe");
    const bool ours = fsu::IsFile(backup); // installed before: Lugaru.exe is the launcher
    if (fsu::IsFile(exe) && (ours || (game.steam && opt.steamPlayButton))) {
        if (!ours && fsu::IsFile(gameExe)) {
            fsu::MakeDirs(fsu::Parent(backup));
            if (!fsu::Copy(gameExe, backup)) {
                error = "can't back up the game's Lugaru.exe";
                return false;
            }
        }
        if (fsu::Lower(norm(exe)) != fsu::Lower(norm(gameExe)) && !replaceFile(exe, gameExe)) {
            error = "can't replace Lugaru.exe (is the game running?)";
            return false;
        }
    }
#else
    launcher = fsu::Join(ex, Setup_LauncherName());
    for (const char* f : { "lugaruex", "lugaruex-launcher", "install.sh", "uninstall.sh" }) {
        chmod(fsu::Join(ex, f).c_str(), 0755);
    }
#endif
    if (opt.shortcut) {
        shortcut(launcher, game.path);
    }
    rememberInstall(game.path); // (found again next time, wherever it is)
    return true;
}

bool Setup_Unpack(const std::string& package, const std::string& dir, std::string& error)
{
    fsu::RemoveTree(dir);
    fsu::MakeDirs(dir);
    const std::string low = fsu::Lower(package);
    if (low.size() > 4 && low.compare(low.size() - 4, 4, ".zip") == 0) {
        return unzipTo(package, dir, "", error);
    }
#ifndef _WIN32
    if (package.find('\'') != std::string::npos || dir.find('\'') != std::string::npos) {
        error = "unusual characters in the path"; // (never into a shell command)
        return false;
    }
    const std::string cmd = Http_CleanEnv() + "tar -xzf '" + package + "' -C '" + dir + "' 2>/dev/null"; // (system libraries, not Steam's)
    if (system(cmd.c_str()) == 0) {
        return true;
    }
#endif
    error = "can't unpack " + package;
    return false;
}

bool Setup_UnpackFreeData(const std::string& zip, const std::string& dir, std::string& error)
{
    fsu::MakeDirs(dir);
    return unzipTo(zip, dir, "Data", error) && Setup_IsGameFolder(dir);
}

bool Setup_LinkSystemData(const std::string& systemData, const std::string& dir, std::string& error)
{
#ifdef _WIN32
    (void)systemData;
    (void)dir;
    error = "not on Windows";
    return false;
#else
    fsu::MakeDirs(dir);
    const std::string link = dir + "/Data";
    unlink(link.c_str());
    if (symlink(systemData.c_str(), link.c_str()) != 0) {
        error = "can't link " + link;
        return false;
    }
    return true;
#endif
}
