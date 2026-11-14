// LugaruEX - online play: server info, names, colour codes, password proofs (GPLv2+)
#include "netinfo.h"
#include "looks.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace netinfo
{
namespace
{
void putStr(std::vector<uint8_t>& o, const std::string& s, size_t max)
{
    const size_t l = std::min<size_t>(std::min<size_t>(s.size(), max), 255);
    o.push_back((uint8_t)l);
    o.insert(o.end(), s.begin(), s.begin() + l);
}

struct In
{
    const uint8_t* d;
    int n, p = 0;
    bool ok = true;
    int u8()
    {
        if (p + 1 > n) {
            ok = false;
            return 0;
        }
        return d[p++];
    }
    int u16()
    {
        const int a = u8(), b = u8();
        return a | (b << 8);
    }
    std::string str()
    {
        const int l = u8();
        if (!ok || p + l > n) {
            ok = false;
            return std::string();
        }
        std::string s((const char*)d + p, l);
        p += l;
        return s;
    }
};

// ---- SHA-256 (FIPS 180-4)
struct Sha
{
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    uint8_t buf[64];
    size_t used = 0;
    uint64_t bits = 0;
    static uint32_t ror(uint32_t x, int k) { return (x >> k) | (x << (32 - k)); }
    void block(const uint8_t* p)
    {
        static const uint32_t K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
            0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
            0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
            0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
            0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
            0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
        }
        for (int i = 16; i < 64; i++) {
            const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            const uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            const uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
    }
    void add(const void* data, size_t len)
    {
        const uint8_t* p = (const uint8_t*)data;
        bits += (uint64_t)len * 8;
        while (len--) {
            buf[used++] = *p++;
            if (used == 64) {
                block(buf);
                used = 0;
            }
        }
    }
    std::vector<uint8_t> done()
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
        std::vector<uint8_t> out(32);
        for (int i = 0; i < 8; i++) {
            for (int k = 0; k < 4; k++) {
                out[4 * i + k] = (uint8_t)(h[i] >> (24 - 8 * k));
            }
        }
        return out;
    }
};
} // namespace

std::vector<uint8_t> EncodeInfo(const ServerInfo& info)
{
    std::vector<uint8_t> o;
    o.push_back(1); // layout version
    for (int k = 0; k < 4; k++) {
        o.push_back((uint8_t)(info.id >> (8 * k)));
    }
    putStr(o, info.version, 16);
    putStr(o, info.name, 120);
    o.push_back((uint8_t)((info.password ? 1 : 0) | (info.shareMods ? 2 : 0) | (info.inGame ? 4 : 0)));
    o.push_back((uint8_t)std::min(255, std::max(0, info.mods)));
    o.push_back((uint8_t)std::min(255, std::max(0, info.players)));
    o.push_back((uint8_t)std::min(255, std::max(0, info.maxPlayers)));
    // the player list, as many as fit one packet (names are short)
    std::vector<uint8_t> list;
    int n = 0;
    if (!info.password) {
        for (const Player& p : info.list) {
            if (o.size() + list.size() + kMaxName + 3 > 1000 || n >= 64) {
                break;
            }
            putStr(list, p.name, kMaxName);
            list.push_back(p.state);
            n++;
        }
    }
    o.push_back((uint8_t)n);
    o.insert(o.end(), list.begin(), list.end());
    return o;
}

bool DecodeInfo(const uint8_t* d, int n, ServerInfo& out)
{
    In r{ d, n };
    if (r.u8() != 1) {
        return false;
    }
    out = ServerInfo();
    for (int k = 0; k < 4; k++) {
        out.id |= (uint32_t)r.u8() << (8 * k);
    }
    out.version = CleanText(r.str(), 16, false);
    out.name = CleanText(r.str(), kMaxServerName, true);
    const int flags = r.u8();
    out.password = flags & 1;
    out.shareMods = (flags & 2) != 0;
    out.inGame = (flags & 4) != 0;
    out.mods = r.u8();
    out.players = r.u8();
    out.maxPlayers = r.u8();
    const int count = r.u8();
    for (int i = 0; i < count && r.ok; i++) {
        Player p;
        p.name = CleanText(r.str(), kMaxName, false);
        p.state = (uint8_t)(r.u8() == PS_INGAME ? PS_INGAME : PS_WAITING);
        if (r.ok) {
            out.list.push_back(p);
        }
    }
    if (out.name.empty()) {
        out.name = "LugaruEX server";
    }
    return r.ok;
}

std::string CleanText(const std::string& s, size_t maxLen, bool allowColours)
{
    std::string o;
    size_t visible = 0;
    for (size_t i = 0; i < s.size() && visible < maxLen; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '^' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9') {
            if (allowColours) {
                o += s.substr(i, 2);
            }
            i++;
            continue;
        }
        if (c < 32 || c > 126) {
            continue; // control characters, other alphabets: never shown
        }
        o += (char)c;
        visible++;
    }
    // trim spaces (and a dangling colour code at the end)
    size_t a = 0;
    while (a < o.size() && o[a] == ' ') {
        a++;
    }
    o = o.substr(a);
    while (!o.empty() && o.back() == ' ') {
        o.pop_back();
    }
    while (o.size() >= 2 && o[o.size() - 2] == '^' && o.back() >= '0' && o.back() <= '9') {
        o.resize(o.size() - 2);
    }
    return StripColours(o).empty() ? std::string() : o;
}

std::string CleanName(const std::string& s)
{
    const std::string n = CleanText(s, kMaxName, false);
    return n.empty() ? std::string("User") : n;
}

std::string StripColours(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '^' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9') {
            i++;
            continue;
        }
        o += s[i];
    }
    return o;
}

std::vector<Segment> ColourSegments(const std::string& s)
{
    std::vector<Segment> out;
    Segment cur{ -1, std::string() };
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '^' && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '9') {
            if (!cur.text.empty()) {
                out.push_back(cur);
            }
            cur = Segment{ s[i + 1] - '0', std::string() };
            i++;
            continue;
        }
        cur.text += s[i];
    }
    if (!cur.text.empty()) {
        out.push_back(cur);
    }
    return out;
}

void ColourRGB(int colour, unsigned char rgb[3])
{
    static const unsigned char pal[10][3] = {
        { 160, 160, 160 }, // ^0 grey
        { 255, 80, 70 },   // ^1 red
        { 110, 220, 90 },  // ^2 green
        { 250, 220, 80 },  // ^3 yellow
        { 100, 150, 255 }, // ^4 blue
        { 90, 220, 230 },  // ^5 cyan
        { 230, 110, 230 }, // ^6 magenta
        { 245, 245, 245 }, // ^7 white
        { 255, 160, 60 },  // ^8 orange
        { 190, 140, 255 }, // ^9 purple
    };
    const unsigned char def[3] = { 235, 230, 220 };
    const unsigned char* c = (colour >= 0 && colour <= 9) ? pal[colour] : def;
    rgb[0] = c[0], rgb[1] = c[1], rgb[2] = c[2];
}

namespace
{
bool plainChars(const std::string& s, const char* extra)
{
    for (unsigned char c : s) {
        if (!(isalnum(c) || strchr(extra, c))) {
            return false;
        }
    }
    return true;
}
} // namespace

bool SafeDataPath(const std::string& p)
{
    if (p.empty() || p.size() > 200 || p[0] == '/' || p.find("..") != std::string::npos || p.find("//") != std::string::npos) {
        return false;
    }
    std::string l = p; // (paths may come in any case: the game finds files in any case)
    for (char& c : l) {
        c = (char)tolower((unsigned char)c);
    }
    if (l.compare(0, 9, "textures/") != 0) {
        // looks are textures, nothing else - or a custom model's own skin image
        const bool image = l.size() > 4 && (l.compare(l.size() - 4, 4, ".png") == 0 || l.compare(l.size() - 4, 4, ".jpg") == 0 ||
                                            (l.size() > 5 && l.compare(l.size() - 5, 5, ".jpeg") == 0));
        if (l.compare(0, 14, "models/custom/") != 0 || !image) {
            return false;
        }
    }
    return plainChars(p, " _-./()");
}

bool SafeLevelName(const std::string& n)
{
    return !n.empty() && n.size() <= 64 && n[0] != '.' && n.find("..") == std::string::npos && plainChars(n, " _-.()");
}

std::string SafeLook(const std::string& spec)
{
    if (spec.size() > 300) {
        return "random";
    }
    std::string look = spec, outfit;
    const size_t bar = spec.find('|');
    if (bar != std::string::npos) {
        look = spec.substr(0, bar);
        outfit = spec.substr(bar + 1);
    }
    std::string model = look, skin;
    const size_t colon = look.find(':');
    if (colon != std::string::npos) {
        model = look.substr(0, colon);
        skin = look.substr(colon + 1);
    }
    if (model != "random" && model != "default" && !Looks_SafeModelKey(model)) { // (rabbit, wolf, rabbit/Name...)
        return "random";
    }
    if (!skin.empty() && skin != "random" && skin != "default" && !SafeDataPath(skin)) {
        return "random";
    }
    if (!outfit.empty()) {
        const bool key = outfit.size() <= 32 && plainChars(outfit, "_-");
        const bool custom = outfit.compare(0, 7, "custom:") == 0 && SafeDataPath(outfit.substr(7));
        if (!key && !custom) {
            outfit.clear();
        }
    }
    return model + (skin.empty() ? "" : ":" + skin) + (outfit.empty() ? "" : "|" + outfit);
}

std::vector<uint8_t> PasswordProof(uint64_t nonce, const std::string& password)
{
    Sha s;
    uint8_t n[8];
    for (int i = 0; i < 8; i++) {
        n[i] = (uint8_t)(nonce >> (8 * i));
    }
    static const char salt[] = "LugaruEX password";
    s.add(salt, sizeof(salt) - 1);
    s.add(n, 8);
    s.add(password.data(), password.size());
    return s.done();
}

const char* StateName(int state)
{
    return state == PS_INGAME ? "In game" : "Waiting";
}
} // namespace netinfo
