// LugaruEX - updates from GitHub releases, downloads, SHA-256 (GPLv2+)
// Shared by both launchers (and their installers).
#pragma once
#include <atomic>
#include <cstdint>
#include <string>

// the GitHub repository whose releases the launchers update from ([Launcher] UpdateRepo)
extern const char* const kUpdateRepo;

struct UpdateInfo
{
    std::string version;  // "0.4.7" (the release tag without the v)
    std::string asset;    // file name
    std::string url;      // download link
    uint64_t size = 0;
    std::string sha256;   // hex, when GitHub lists it
};

// HTTPS GET into a string (small replies) or a file; progress counts bytes as they arrive
bool Http_GetString(const std::string& url, std::string& out, std::string& error);
bool Http_GetFile(const std::string& url, const std::string& path, std::atomic<uint64_t>* progress, std::string& error);

// the newest release of repo ("owner/name") and its package for this computer
// (platform "win64": the .zip, "linux-x64": the .tar.gz)
bool Update_Latest(const std::string& repo, const char* platform, UpdateInfo& out, std::string& error);
bool Update_IsNewer(const std::string& version, const std::string& current); // 0.4.10 > 0.4.9
// download to path and check the size and the SHA-256
bool Update_Download(const UpdateInfo& info, const std::string& path, std::atomic<uint64_t>* progress, std::string& error);

std::string Sha256File(const std::string& path); // hex, "" if unreadable
const char* Update_Platform();                    // "win64" or "linux-x64"
