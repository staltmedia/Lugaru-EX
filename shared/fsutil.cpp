// LugaruEX - small portable file helpers (GPLv2+)
#include "fsutil.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fsu
{
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
static std::string U(const wchar_t* w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    std::string s(n > 0 ? n - 1 : 0, 0);
    if (n > 1) {
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, NULL, NULL);
    }
    return s;
}
#endif

FILE* Open(const std::string& path, const char* mode)
{
#ifdef _WIN32
    return _wfopen(W(path).c_str(), W(mode).c_str());
#else
    return fopen(path.c_str(), mode);
#endif
}

bool IsFile(const std::string& p)
{
#ifdef _WIN32
    const DWORD a = GetFileAttributesW(W(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool IsDir(const std::string& p)
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
            if (part.empty() || part.back() == ':' || IsDir(part)) {
                continue;
            }
#ifdef _WIN32
            CreateDirectoryW(W(part).c_str(), NULL);
#else
            mkdir(part.c_str(), 0755);
#endif
        }
    }
    return IsDir(p);
}

std::vector<std::string> List(const std::string& dir, bool dirs)
{
    std::vector<std::string> out;
#ifdef _WIN32
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(W(dir + "/*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::string n = U(fd.cFileName);
            if (n == "." || n == "..") {
                continue;
            }
            if (((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == dirs) {
                out.push_back(n);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
#else
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n == "." || n == "..") {
                continue;
            }
            if (IsDir(dir + "/" + n) == dirs) {
                out.push_back(n);
            }
        }
        closedir(d);
    }
#endif
    std::sort(out.begin(), out.end());
    return out;
}

bool RemoveTree(const std::string& p)
{
    if (IsFile(p)) {
#ifdef _WIN32
        SetFileAttributesW(W(p).c_str(), FILE_ATTRIBUTE_NORMAL);
        return DeleteFileW(W(p).c_str()) != 0;
#else
        return unlink(p.c_str()) == 0;
#endif
    }
    if (!IsDir(p)) {
        return true;
    }
    for (const std::string& n : List(p, true)) {
        RemoveTree(p + "/" + n);
    }
    for (const std::string& n : List(p, false)) {
        RemoveTree(p + "/" + n);
    }
#ifdef _WIN32
    return RemoveDirectoryW(W(p).c_str()) != 0;
#else
    return rmdir(p.c_str()) == 0;
#endif
}

bool Rename(const std::string& from, const std::string& to)
{
#ifdef _WIN32
    return MoveFileExW(W(from).c_str(), W(to).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(from.c_str(), to.c_str()) == 0;
#endif
}

bool ReadAll(const std::string& path, std::string& out)
{
    FILE* f = Open(path, "rb");
    if (!f) {
        return false;
    }
    out.clear();
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    fclose(f);
    return true;
}

bool WriteAll(const std::string& path, const std::string& data)
{
    FILE* f = Open(path, "wb");
    if (!f) {
        return false;
    }
    const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    return (fclose(f) == 0) && ok;
}

bool Copy(const std::string& from, const std::string& to)
{
    std::string d;
    return ReadAll(from, d) && WriteAll(to, d);
}

uint64_t Size(const std::string& path)
{
    FILE* f = Open(path, "rb");
    if (!f) {
        return 0;
    }
    fseek(f, 0, SEEK_END);
    const long long n = ftell(f);
    fclose(f);
    return n > 0 ? (uint64_t)n : 0;
}

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
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::string Join(const std::string& dir, const std::string& name)
{
    if (dir.empty()) {
        return name;
    }
    const char last = dir.back();
    return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
}

std::string Parent(const std::string& path)
{
    std::string p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) {
        p.pop_back();
    }
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string() : p.substr(0, s);
}

std::string BaseName(const std::string& path)
{
    std::string p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) {
        p.pop_back();
    }
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}
} // namespace fsu
