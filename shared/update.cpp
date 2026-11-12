// LugaruEX - updates from GitHub releases, downloads, SHA-256 (GPLv2+)
#include "update.h"

#include "fsutil.h"

#include <cstring>
#include <map>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <cstdio>
#include <cstdlib>
#include <sys/wait.h>
#endif

const char* const kUpdateRepo = "staltmedia/Lugaru-EX"; // public releases (and source)

const char* Update_Platform()
{
#ifdef _WIN32
    return "win64";
#else
    return "linux-x64";
#endif
}

// ---------------------------------------------------------------- HTTP
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

// one GET; sink gets the body in pieces (false = stop)
template <typename Sink>
static bool httpGet(const std::string& url, Sink sink, std::string& error)
{
    const std::wstring wurl = W(url);
    URL_COMPONENTS u = { sizeof(u) };
    wchar_t host[256] = {}, path[4096] = {};
    u.lpszHostName = host;
    u.dwHostNameLength = 256;
    u.lpszUrlPath = path;
    u.dwUrlPathLength = 4096;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &u) || u.nScheme != INTERNET_SCHEME_HTTPS) {
        error = "bad address";
        return false;
    }
    std::wstring full = path;
    HINTERNET s = WinHttpOpen(L"LugaruEX-updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { // (Windows before 8.1)
        s = WinHttpOpen(L"LugaruEX-updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    HINTERNET c = s ? WinHttpConnect(s, host, u.nPort, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", full.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
    bool ok = false;
    if (r) {
        WinHttpSetTimeouts(r, 10000, 10000, 15000, 30000);
        const wchar_t* hdr = L"Accept: application/vnd.github+json, application/octet-stream, */*\r\n";
        if (WinHttpSendRequest(r, hdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
            DWORD status = 0, len = sizeof(status);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
            if (status == 200) {
                ok = true;
                std::vector<char> buf(65536);
                for (;;) {
                    DWORD got = 0;
                    if (!WinHttpReadData(r, buf.data(), (DWORD)buf.size(), &got)) {
                        ok = false;
                        error = "the download broke off";
                        break;
                    }
                    if (got == 0) {
                        break;
                    }
                    if (!sink(buf.data(), (size_t)got)) {
                        ok = false;
                        error = "can't save the download";
                        break;
                    }
                }
            } else {
                error = "the server answered " + std::to_string(status);
            }
        } else {
            error = "no connection";
        }
    } else {
        error = "no connection";
    }
    for (HINTERNET h : { r, c, s }) {
        if (h) {
            WinHttpCloseHandle(h);
        }
    }
    return ok;
}

bool Http_GetString(const std::string& url, std::string& out, std::string& error)
{
    out.clear();
    return httpGet(url, [&](const char* d, size_t n) { out.append(d, n); return out.size() < (16u << 20); }, error);
}

bool Http_GetFile(const std::string& url, const std::string& path, std::atomic<uint64_t>* progress, std::string& error)
{
    FILE* f = fsu::Open(path, "wb");
    if (!f) {
        error = "can't write " + path;
        return false;
    }
    uint64_t total = 0;
    const bool ok = httpGet(url, [&](const char* d, size_t n) {
        total += n;
        if (progress) {
            *progress = total;
        }
        return fwrite(d, 1, n, f) == n;
    }, error);
    fclose(f);
    return ok;
}
#else
// curl (or wget) does the HTTPS: every Linux desktop and SteamOS has one of them
static std::string quote(const std::string& s)
{
    std::string r = "'";
    for (char c : s) {
        r += (c == '\'') ? std::string("'\\''") : std::string(1, c);
    }
    return r + "'";
}

static bool have(const char* tool)
{
    return system((std::string("command -v ") + tool + " >/dev/null 2>&1").c_str()) == 0;
}

bool Http_GetString(const std::string& url, std::string& out, std::string& error)
{
    out.clear();
    std::string cmd;
    if (have("curl")) {
        cmd = "curl -fsSL --max-time 20 -A LugaruEX-updater -H 'Accept: application/vnd.github+json' " + quote(url);
    } else if (have("wget")) {
        cmd = "wget -q -T 20 -U LugaruEX-updater -O - " + quote(url);
    } else {
        error = "needs curl or wget";
        return false;
    }
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) {
        error = "can't run curl";
        return false;
    }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0 && out.size() < (16u << 20)) {
        out.append(buf, n);
    }
    const int rc = pclose(p);
    if (rc != 0 || out.empty()) {
        error = "no connection (or the server said no)";
        return false;
    }
    return true;
}

bool Http_GetFile(const std::string& url, const std::string& path, std::atomic<uint64_t>* progress, std::string& error)
{
    std::string cmd;
    if (have("curl")) {
        cmd = "curl -fsSL --retry 2 -A LugaruEX-updater -o " + quote(path) + " " + quote(url);
    } else if (have("wget")) {
        cmd = "wget -q -U LugaruEX-updater -O " + quote(path) + " " + quote(url);
    } else {
        error = "needs curl or wget";
        return false;
    }
    (void)progress; // the caller watches the file grow
    if (system(cmd.c_str()) != 0) {
        error = "the download failed";
        return false;
    }
    return true;
}
#endif

// ---------------------------------------------------------------- tiny JSON
namespace
{
struct J
{
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    std::string s;
    double n = 0;
    std::vector<J> a;
    std::map<std::string, J> o;
    const J& operator[](const char* k) const
    {
        static const J none;
        auto it = o.find(k);
        return it == o.end() ? none : it->second;
    }
};

struct P
{
    const char* p;
    const char* e;
    int depth = 0;
    void ws()
    {
        while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
            p++;
        }
    }
    bool str(std::string& out)
    {
        if (p >= e || *p != '"') {
            return false;
        }
        p++;
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                p++;
                switch (*p) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'u':
                        if (p + 4 < e) {
                            const unsigned cp = (unsigned)strtoul(std::string(p + 1, 4).c_str(), nullptr, 16);
                            if (cp < 0x80) {
                                out += (char)cp;
                            } else if (cp < 0x800) {
                                out += (char)(0xC0 | (cp >> 6));
                                out += (char)(0x80 | (cp & 63));
                            } else {
                                out += (char)(0xE0 | (cp >> 12));
                                out += (char)(0x80 | ((cp >> 6) & 63));
                                out += (char)(0x80 | (cp & 63));
                            }
                            p += 4;
                        }
                        break;
                    default: out += *p;
                }
            } else {
                out += *p;
            }
            p++;
        }
        if (p >= e) {
            return false;
        }
        p++;
        return true;
    }
    bool value(J& v)
    {
        ws();
        if (p >= e || ++depth > 64) {
            return false;
        }
        bool ok = true;
        if (*p == '{') {
            v.t = J::OBJ;
            p++;
            ws();
            if (p < e && *p == '}') {
                p++;
            } else {
                for (;;) {
                    ws();
                    std::string k;
                    if (!str(k)) {
                        ok = false;
                        break;
                    }
                    ws();
                    if (p >= e || *p != ':') {
                        ok = false;
                        break;
                    }
                    p++;
                    if (!value(v.o[k])) {
                        ok = false;
                        break;
                    }
                    ws();
                    if (p < e && *p == ',') {
                        p++;
                        continue;
                    }
                    if (p < e && *p == '}') {
                        p++;
                        break;
                    }
                    ok = false;
                    break;
                }
            }
        } else if (*p == '[') {
            v.t = J::ARR;
            p++;
            ws();
            if (p < e && *p == ']') {
                p++;
            } else {
                for (;;) {
                    v.a.emplace_back();
                    if (!value(v.a.back())) {
                        ok = false;
                        break;
                    }
                    ws();
                    if (p < e && *p == ',') {
                        p++;
                        continue;
                    }
                    if (p < e && *p == ']') {
                        p++;
                        break;
                    }
                    ok = false;
                    break;
                }
            }
        } else if (*p == '"') {
            v.t = J::STR;
            ok = str(v.s);
        } else if (!strncmp(p, "true", 4) || !strncmp(p, "false", 5)) {
            v.t = J::BOOL;
            v.n = *p == 't';
            p += *p == 't' ? 4 : 5;
        } else if (!strncmp(p, "null", 4)) {
            p += 4;
        } else {
            char* end = nullptr;
            v.t = J::NUM;
            v.n = strtod(p, &end);
            ok = end && end > p;
            p = end ? end : e;
        }
        depth--;
        return ok;
    }
};
} // namespace

static std::vector<int> versionParts(std::string v)
{
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) {
        v.erase(0, 1);
    }
    std::vector<int> parts;
    int cur = 0;
    bool any = false;
    for (char c : v) {
        if (c >= '0' && c <= '9') {
            cur = cur * 10 + (c - '0');
            any = true;
        } else if (c == '.') {
            parts.push_back(cur);
            cur = 0;
        } else {
            break; // "0.4.7-beta": the numbers before the dash
        }
    }
    if (any) {
        parts.push_back(cur);
    }
    return parts;
}

bool Update_IsNewer(const std::string& version, const std::string& current)
{
    std::vector<int> a = versionParts(version), b = versionParts(current);
    if (a.empty()) {
        return false;
    }
    while (a.size() < b.size()) {
        a.push_back(0);
    }
    while (b.size() < a.size()) {
        b.push_back(0);
    }
    return a > b;
}

bool Update_Latest(const std::string& repo, const char* platform, UpdateInfo& out, std::string& error)
{
    std::string body;
    if (!Http_GetString("https://api.github.com/repos/" + repo + "/releases/latest", body, error)) {
        return false;
    }
    J root;
    P p{ body.data(), body.data() + body.size() };
    if (!p.value(root) || root.t != J::OBJ) {
        error = "unexpected reply from GitHub";
        return false;
    }
    std::string tag = root["tag_name"].s;
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) {
        tag.erase(0, 1);
    }
    const bool win = strcmp(platform, "win64") == 0;
    for (const J& a : root["assets"].a) {
        const std::string name = a["name"].s, lname = fsu::Lower(name);
        const bool match = win ? (lname.find("win64") != std::string::npos && lname.size() > 4 && lname.compare(lname.size() - 4, 4, ".zip") == 0)
                               : (lname.find("linux-x64") != std::string::npos && lname.find("debug") == std::string::npos &&
                                  lname.size() > 7 && lname.compare(lname.size() - 7, 7, ".tar.gz") == 0);
        if (!match) {
            continue;
        }
        out.version = tag;
        out.asset = name;
        out.url = a["browser_download_url"].s;
        out.size = (uint64_t)a["size"].n;
        const std::string d = a["digest"].s;
        out.sha256 = d.compare(0, 7, "sha256:") == 0 ? fsu::Lower(d.substr(7)) : "";
        return !out.url.empty() && out.url.compare(0, 8, "https://") == 0;
    }
    error = "the newest release has no package for this computer";
    return false;
}

// ---------------------------------------------------------------- SHA-256
namespace
{
struct Sha256
{
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    uint8_t buf[64];
    size_t used = 0;
    uint64_t bits = 0;
    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void block(const uint8_t* p)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
            0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
            0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
            0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
            0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        }
        for (int i = 16; i < 64; i++) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
    }
    void add(const uint8_t* d, size_t n)
    {
        bits += (uint64_t)n * 8;
        while (n--) {
            buf[used++] = *d++;
            if (used == 64) {
                block(buf);
                used = 0;
            }
        }
    }
    std::string hex()
    {
        const uint64_t total = bits;
        const uint8_t one = 0x80, zero = 0;
        add(&one, 1);
        while (used != 56) {
            add(&zero, 1);
        }
        uint8_t len[8];
        for (int i = 0; i < 8; i++) {
            len[i] = (uint8_t)(total >> (56 - 8 * i));
        }
        add(len, 8);
        char out[65];
        for (int i = 0; i < 8; i++) {
            snprintf(out + i * 8, 9, "%08x", h[i]);
        }
        return out;
    }
};
} // namespace

std::string Sha256File(const std::string& path)
{
    FILE* f = fsu::Open(path, "rb");
    if (!f) {
        return "";
    }
    Sha256 s;
    std::vector<uint8_t> buf(1 << 16);
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) {
        s.add(buf.data(), n);
    }
    fclose(f);
    return s.hex();
}

bool Update_Download(const UpdateInfo& info, const std::string& path, std::atomic<uint64_t>* progress, std::string& error)
{
    const std::string part = path + ".part";
    if (!Http_GetFile(info.url, part, progress, error)) {
        fsu::RemoveTree(part);
        return false;
    }
    if (info.size && fsu::Size(part) != info.size) {
        error = "the download is incomplete";
        fsu::RemoveTree(part);
        return false;
    }
    if (!info.sha256.empty() && Sha256File(part) != info.sha256) {
        error = "the download doesn't match the release (SHA-256)";
        fsu::RemoveTree(part);
        return false;
    }
    if (!fsu::Rename(part, path)) {
        error = "can't save " + path;
        return false;
    }
    return true;
}
