// LugaruEX - small portable file helpers (UTF-8 paths, '/' or '\' separators) (GPLv2+)
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace fsu
{
FILE* Open(const std::string& path, const char* mode);
bool IsFile(const std::string& path);
bool IsDir(const std::string& path);
bool MakeDirs(const std::string& path);                    // like mkdir -p
bool RemoveTree(const std::string& path);                  // a file, or a folder and everything in it
bool Rename(const std::string& from, const std::string& to); // replaces 'to' if it is a file
bool Copy(const std::string& from, const std::string& to);
// names in a folder (not "." / ".."); dirs: folders only, else files only
std::vector<std::string> List(const std::string& dir, bool dirs);
bool ReadAll(const std::string& path, std::string& out);
bool WriteAll(const std::string& path, const std::string& data);
uint64_t Size(const std::string& path); // 0 if missing
std::string Lower(std::string s);
std::string Trim(const std::string& s);
std::string Join(const std::string& dir, const std::string& name); // dir + '/' + name
std::string Parent(const std::string& path);                       // without the last part
std::string BaseName(const std::string& path);                     // the last part
} // namespace fsu
