// LugaruEX - installing and updating (GPLv2+)
#include "setup.h"

#include "fsutil.h"
#include "miniz/miniz.h"

#include <algorithm>
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
    g.hasLugaruEX = fsu::IsDir(fsu::Join(dir, "LugaruEX"));
    return g;
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
    return n > 0 ? std::string(buf, (size_t)n) : std::string();
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
    const std::string cmd = "tar -xzf '" + package + "' -C '" + dir + "' 2>/dev/null";
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
