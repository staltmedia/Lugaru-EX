// LugaruEX launcher for Linux - files, ini, images (GPLv2+)
#include "common.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

std::string Lower(std::string s)
{
    for (auto& c : s) {
        c = (char)tolower((unsigned char)c);
    }
    return s;
}

std::string Trim(const std::string& s)
{
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) {
        return "";
    }
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

#ifdef _WIN32
static std::wstring W(const std::string& s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::wstring w(n > 0 ? n - 1 : 0, 0);
    if (n > 1) {
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    }
    return w;
}
static std::string U(const std::wstring& w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, NULL, 0, NULL, NULL);
    std::string s(n > 0 ? n - 1 : 0, 0);
    if (n > 1) {
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, NULL, NULL);
    }
    return s;
}
#endif

static FILE* openU8(const std::string& p, const char* mode)
{
#ifdef _WIN32
    return _wfopen(W(p).c_str(), W(mode).c_str());
#else
    return fopen(p.c_str(), mode);
#endif
}

bool FileExists(const std::string& p)
{
#ifdef _WIN32
    const DWORD a = GetFileAttributesW(W(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool DirExists(const std::string& p)
{
#ifdef _WIN32
    const DWORD a = GetFileAttributesW(W(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool MakeDirs(const std::string& p)
{
    for (size_t i = 1; i <= p.size(); i++) {
        if (i == p.size() || p[i] == '/' || p[i] == '\\') {
            const std::string part = p.substr(0, i);
            if (!part.empty() && !DirExists(part)) {
#ifdef _WIN32
                CreateDirectoryW(W(part).c_str(), NULL);
#else
                mkdir(part.c_str(), 0755);
#endif
            }
        }
    }
    return DirExists(p);
}

std::vector<std::string> ListDir(const std::string& dir, const std::string& ext)
{
    std::vector<std::string> out;
    const std::string le = Lower(ext);
    auto take = [&](const std::string& n) {
        if (n.size() > le.size() && Lower(n.substr(n.size() - le.size())) == le && FileExists(dir + "/" + n)) {
            out.push_back(n);
        }
    };
#ifdef _WIN32
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(W(dir + "/*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            take(U(fd.cFileName));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
#else
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            take(e->d_name);
        }
        closedir(d);
    }
#endif
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });
    return out;
}

std::string FindFileCI(const std::string& dir, const std::string& name)
{
    if (FileExists(dir + "/" + name)) {
        return dir + "/" + name;
    }
#ifndef _WIN32
    const std::string want = Lower(name);
    if (DIR* d = opendir(dir.c_str())) {
        std::string found;
        while (dirent* e = readdir(d)) {
            if (found.empty() && Lower(e->d_name) == want) {
                found = dir + "/" + e->d_name;
            }
        }
        closedir(d);
        return found;
    }
#endif
    return "";
}

std::string Ini::Get(const std::string& sec, const std::string& key, const std::string& def) const
{
    auto it = kv.find(Lower(sec) + "." + Lower(key));
    return it == kv.end() ? def : it->second;
}

int Ini::GetInt(const std::string& sec, const std::string& key, int def) const
{
    const std::string v = Get(sec, key);
    return v.empty() ? def : atoi(v.c_str());
}

Ini ReadIni(const std::string& path)
{
    Ini ini;
    FILE* f = openU8(path, "rb");
    if (!f) {
        return ini;
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        text.append(buf, n);
    }
    fclose(f);
    std::string sec;
    size_t a = 0;
    while (a < text.size()) {
        size_t b = text.find('\n', a);
        if (b == std::string::npos) {
            b = text.size();
        }
        std::string raw = text.substr(a, b - a);
        a = b + 1;
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();
        }
        const std::string line = Trim(raw);
        if (!line.empty() && line[0] == '[') {
            const size_t e = line.find(']');
            sec = Trim(line.substr(1, e == std::string::npos ? std::string::npos : e - 1));
            ini.sections.push_back({ sec, {} });
            continue;
        }
        if (ini.sections.empty()) {
            ini.sections.push_back({ "", {} });
        }
        ini.sections.back().second.push_back(raw);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq != std::string::npos) {
            ini.kv[Lower(sec) + "." + Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
        }
    }
    return ini;
}

bool ReadImage(const std::string& path, Image& img)
{
    FILE* f = openU8(path, "rb");
    if (!f) {
        return false;
    }
    int w = 0, h = 0, c = 0;
    unsigned char* d = stbi_load_from_file(f, &w, &h, &c, 4);
    fclose(f);
    if (!d) {
        return false;
    }
    img.w = w;
    img.h = h;
    img.px.assign(d, d + (size_t)w * h * 4);
    stbi_image_free(d);
    return true;
}

static void writeToFile(void* ctx, void* data, int size)
{
    fwrite(data, 1, (size_t)size, (FILE*)ctx);
}

bool SavePNG(const std::string& path, const Image& img)
{
    FILE* f = openU8(path, "wb");
    if (!f) {
        return false;
    }
    const int ok = stbi_write_png_to_func(writeToFile, f, img.w, img.h, 4, img.px.data(), img.w * 4);
    fclose(f);
    return ok != 0;
}
