/*
Copyright (C) 2026 - LugaruEX contributors

This file is part of LugaruEX, a modified version of Lugaru.

Lugaru is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// sockets first (winsock must come before anything that pulls in windows.h)
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define closesocket close
#endif

#include "EX/ExNet.hpp"

#include "Audio/Sounds.hpp"
#include "EX/ExConfig.hpp"
#include "EX/ExInterp.hpp"
#include "EX/ExLocal.hpp"
#include "EX/ExMods.hpp"
#include "fsutil.h"
#include "modpack.h"
#include "Tutorial.hpp"
#include "EX/ExPad.hpp"
#include "Game.hpp"
#include "Level/Awards.hpp"
#include "Level/Dialog.hpp"
#include "Menu/Menu.hpp"
#include "Objects/Object.hpp"
#include "Objects/Person.hpp"
#include "Objects/Weapons.hpp"
#include "Graphic/gamegl.hpp"
#include "Utils/Input.hpp"
#include "Utils/Folders.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <cstdio>
#include <cstring>
#include <deque>
#include <set>
#include <unordered_set>
#include <random>
#include <vector>

extern int mainmenu;
extern bool winfreeze;
extern XYZ viewer;

extern int slomo;
extern float slomodelay;
extern float screenwidth, screenheight;
extern Terrain terrain;
extern float multiplier;
extern float damagedealt; // (damagetaken comes from Level/Awards.hpp)

namespace EX
{
// ------------------------------------------------------------------ protocol
static const uint32_t kMagic = 0x3858474C; // "LGX8" (0.4.7: mod sharing)
enum : uint8_t
{
    T_DISCOVER = 1, // guest -> broadcast: any host here?
    T_HERE,         // host -> guest
    T_HELLO,        // guest -> host: token, character
    T_WELCOME,      // host -> guest
    T_LEVEL,        // host -> guest: level + who is who (resent until acknowledged)
    T_INPUT,        // guest -> host: buttons + camera, acks level/roster
    T_SNAP,         // host -> guest: characters, weapons, events
    T_IDLE,         // host -> guest: connected, but no level running (menu)
    T_BYE,
    T_PING, // guest -> host every second: its clock + how the connection looks from there
    T_PONG, // host -> guest: the clock back (round trip time) + the host's frame rate
    T_INFO, // both ways once: graphics card, screen, refresh and simulation rate (for the log)
    T_MODLIST,  // host -> guest: the mods to play with (resent until the guest is ready)
    T_MODGET,   // guest -> host: a piece of a mod it is downloading
    T_MODDATA,  // host -> guest: that piece
    T_MODREADY, // guest -> host: has every mod (or plays without them)
};
static const int kMaxPacket = 1200;    // stays under every MTU, no IP fragmentation
static const double kSendRate = 60;    // snapshots and inputs per second
static const double kTimeout = 6;      // seconds of silence = gone

struct Writer
{
    uint8_t b[kMaxPacket + 1024]; // slack: room() is checked before each entry, never mid-entry
    int n = 0;
    bool room(int k) const { return n + k <= kMaxPacket; }
    void u8(int v) { b[n++] = (uint8_t)v; }
    void i8(int v) { b[n++] = (uint8_t)(int8_t)std::max(-128, std::min(127, v)); }
    void u16(int v)
    {
        v = std::max(0, std::min(65535, v));
        b[n++] = (uint8_t)v;
        b[n++] = (uint8_t)(v >> 8);
    }
    void i16(int v) { u16((uint16_t)(int16_t)std::max(-32768, std::min(32767, v)) & 0xFFFF); }
    void u32(uint32_t v)
    {
        for (int k = 0; k < 4; k++) {
            b[n++] = (uint8_t)(v >> (8 * k));
        }
    }
    void f32(float f)
    {
        uint32_t v;
        memcpy(&v, &f, 4);
        u32(v);
    }
    void u64(uint64_t v)
    {
        u32((uint32_t)v);
        u32((uint32_t)(v >> 32));
    }
    void str(const std::string& s)
    {
        const int l = std::min(255, (int)s.size());
        u8(l);
        memcpy(b + n, s.data(), l);
        n += l;
    }
    void bytes(const void* d, int l)
    {
        memcpy(b + n, d, l);
        n += l;
    }
    void head(uint8_t type)
    {
        n = 0;
        u32(kMagic);
        u8(type);
    }
};

struct Reader
{
    const uint8_t* b;
    int n, p = 0;
    bool ok = true;
    Reader(const uint8_t* d, int len) : b(d), n(len) {}
    bool need(int k)
    {
        if (p + k > n) {
            ok = false;
        }
        return ok;
    }
    int u8() { return need(1) ? b[p++] : 0; }
    int i8() { return need(1) ? (int8_t)b[p++] : 0; }
    int u16()
    {
        if (!need(2)) {
            return 0;
        }
        const int v = b[p] | (b[p + 1] << 8);
        p += 2;
        return v;
    }
    int i16() { return (int16_t)(uint16_t)u16(); }
    uint32_t u32()
    {
        if (!need(4)) {
            return 0;
        }
        uint32_t v = 0;
        for (int k = 0; k < 4; k++) {
            v |= (uint32_t)b[p + k] << (8 * k);
        }
        p += 4;
        return v;
    }
    float f32()
    {
        const uint32_t v = u32();
        float f;
        memcpy(&f, &v, 4);
        return std::isfinite(f) ? f : 0.f;
    }
    uint64_t u64()
    {
        const uint64_t lo = u32();
        return lo | ((uint64_t)u32() << 32);
    }
    std::string str()
    {
        const int l = u8();
        if (!need(l)) {
            return std::string();
        }
        std::string s((const char*)b + p, l);
        p += l;
        return s;
    }
    bool bytes(void* d, int l)
    {
        if (l < 0 || !need(l)) {
            return false;
        }
        memcpy(d, b + p, l);
        p += l;
        return true;
    }
};

static inline int angle100(float a)
{
    a = fmodf(a, 360.f);
    if (a >= 180) {
        a -= 360;
    }
    if (a < -180) {
        a += 360;
    }
    return (int)lroundf(a * 100);
}

// ------------------------------------------------------------------ socket
enum Role
{
    R_NONE,
    R_HOST,
    R_GUEST
};
static Role g_role = R_NONE;
static SOCKET g_sock = INVALID_SOCKET;
static int g_port = 27015;
static bool g_lan = false;
static double g_t0 = 0;
static double g_bytesOut = 0, g_packetsOut = 0; // for the bandwidth line at exit

static double now()
{
    return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency() - g_t0;
}

static bool sameAddr(const sockaddr_in& a, const sockaddr_in& b)
{
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

static void sendTo(const Writer& w, const sockaddr_in& to)
{
    if (g_sock != INVALID_SOCKET) {
        sendto(g_sock, (const char*)w.b, w.n, 0, (const sockaddr*)&to, sizeof(to));
        g_bytesOut += w.n + 28; // + IP/UDP headers
        g_packetsOut++;
    }
}

static bool openSocket(int port, bool broadcast)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false;
    }
#endif
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == INVALID_SOCKET) {
        return false;
    }
    int yes = 1;
    if (broadcast) {
        setsockopt(g_sock, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
    }
    int buf = 1 << 20; // room for bursts of snapshots
    setsockopt(g_sock, SOL_SOCKET, SO_RCVBUF, (const char*)&buf, sizeof(buf));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)port);
    if (bind(g_sock, (const sockaddr*)&a, sizeof(a)) != 0) {
        fprintf(stderr, "LugaruEX net: can't use UDP port %d (another program on it?)\n", port);
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
        return false;
    }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(g_sock, FIONBIO, &nb);
#else
    fcntl(g_sock, F_SETFL, fcntl(g_sock, F_GETFL, 0) | O_NONBLOCK);
#endif
    return true;
}

// ------------------------------------------------------------------ host state
struct Client
{
    sockaddr_in addr;
    uint64_t token = 0;
    std::vector<int> slots; // one ExLocal remote slot per player on that PC (split screen there)
    double lastHeard = 0, lastLevelSent = -10;
    uint32_t ackLevel = 0, ackRoster = 0;
    bool connected = false;
    // connection as the guest measures it (sent in its pings), and inputs heard here
    int pingMs = -1, lossPermille = 0, jitterMs = 0, delayMs = 0, fps = 0;
    long inputs = 0;
    bool infoSent = false;
    bool modsReady = false; // the guest has the host's mods (or plays without them)
    double lastModList = -10;
};
static std::vector<Client> g_clients;
static uint32_t g_levelSeq = 0, g_rosterVer = 0, g_snapSeq = 0;
static std::string g_levelName, g_rosterSig;
static bool g_levelTutorial = false;
static double g_sendAcc = 0, g_lastIdle = 0;
static double g_simClock = 0; // host: seconds simulated (snapshot timestamps)

struct Event
{
    uint32_t id;
    uint8_t kind; // 'S' sound, 'B' bonus, 'D' blood, 'R' rumble, 'F' sprite, 'G' ground decal
    int a, b, c;  // sound: id, positional, volume / bonus: person, bonus, value / F, G: type, size, opacity (x1000)
    float x, y, z;
    double t;
    // F / G only: velocity (G: vx = rotation), colour (x127.5), special, speed, alive time (x100)
    float vx, vy, vz;
    uint8_t r, g, bl, special, speed, alive;
};
static std::deque<Event> g_events;
static uint32_t g_eventId = 0;
static const double kEventRepeat = 0.25; // each event rides along this long (packet loss)
static const double kFxRepeat = 0.07;    // effects (F, G): many and short-lived, a lost one is no loss
static int fxClamp(float v, int lo, int hi)
{
    return std::max(lo, std::min(hi, (int)lroundf(v)));
}

// ------------------------------------------------------------------ guest state
struct NetPerson
{
    double t = -1;
    XYZ coords, curoff, tgtoff;
    float yaw = 0, tilt = 0, tilt2 = 0, headyaw = 0, headpitch = 0, target = 0;
    int animCur = 0, animTgt = 0, frameCur = 0, frameTgt = 0;
    int dead = 0, free = 0, aitype = 0, flags = 0;
    float damage = 0, bloodloss = 0, perm = 0, superperm = 0, tol = 200, dim = 0;
    int numw = 0, wactive = -1, wstuck = -1, wstuckwhere = 0, w[4] = { -1, -1, -1, -1 };
    std::vector<XYZ> joints; // ragdoll only
    bool hasJoints = false;
};
struct NetWeapon
{
    double t = -1;
    int owner = -1, bloody = 0;
    float drip = 0; // blood still dripping off it (0..5)
    bool onfire = false;
    XYZ pos;
    float rot[8] = {};
};
static sockaddr_in g_hostAddr;
static bool g_haveHost = false, g_welcomed = false, g_inLevel = false;
static uint64_t g_token = 0;
static double g_lastHeard = 0, g_lastHello = -10, g_lastDiscover = -10, g_inputAcc = 0;
static uint32_t g_gLevel = 0, g_gRoster = 0;
static int g_myPerson = -1;
static std::vector<std::deque<NetPerson>> g_hist;
static std::vector<std::deque<NetWeapon>> g_whist;

// a weapon record's flag byte: bits 0-1 bloody (0-2), 2-5 blood still dripping off it
// (blooddrip x3, 0-15), 7 burning
static int wpFlags(const Weapon& wp)
{
    const int drip = std::max(0, std::min(15, (int)(wp.blooddrip * 3 + .5f)));
    return (wp.bloody & 3) | (drip << 2) | (const_cast<Weapon&>(wp).exOnFire() ? 0x80 : 0);
}


// ------------------------------------------------------------------ PC info and level fingerprints
// Each PC tells the other what it runs on (written to both logs), and the host
// sends a fingerprint of the level file and the terrain, so a guest whose copy of
// the game differs is told so (characters would float / sink on its screen).
static uint32_t g_mapPrint = 0, g_terrainPrint = 0;
static std::string g_warn;
static double g_warnUntil = 0;
static float g_hostFps = -1;
static bool g_infoSent = false;

static uint32_t filePrint(const std::string& rel)
{
    FILE* f = fopen(Folders::getResourcePath(rel).c_str(), "rb");
    if (!f) {
        return 0;
    }
    uint32_t h = 2166136261u; // FNV-1a
    unsigned char buf[8192];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < got; i++) {
            h = (h ^ buf[i]) * 16777619u;
        }
    }
    fclose(f);
    return h ? h : 1;
}


// fingerprint of the ground as built (heights after the environment's own changes)
static uint32_t groundPrint()
{
    uint32_t h = 2166136261u;
    const int n = std::max(0, std::min((int)terrain.size, (int)max_terrain_size));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            const int32_t q = (int32_t)(terrain.heightmap[i][j] * 1000.f); // a millimetre is plenty
            for (int k = 0; k < 4; k++) {
                h = (h ^ ((q >> (8 * k)) & 0xff)) * 16777619u;
            }
        }
    }
    return h ? h : 1;
}
static void writeInfo(Writer& w)
{
    w.head(T_INFO);
    const char* gpu = (const char*)glGetString(GL_RENDERER);
    w.str(gpu ? gpu : "unknown graphics");
    w.u16(RefreshRate());
    w.u16(SimRate());
    w.u16((int)screenwidth);
    w.u16((int)screenheight);
    w.u8(cfg.vsync);
    w.u8(cfg.msaa);
    w.u8(cfg.shaders ? 1 : 0);
}

static void logInfo(Reader& r, const char* who)
{
    const std::string gpu = r.str();
    const int hz = r.u16(), sim = r.u16(), w = r.u16(), h = r.u16(), vs = r.u8(), msaa = r.u8(), sh = r.u8();
    if (r.ok) {
        printf("LugaruEX net: %s PC: %s, %dx%d, %d Hz display, %d Hz simulation, vsync %d, MSAA %dx, shaders %s\n", who, gpu.c_str(), w, h, hz, sim,
               vs, msaa, sh ? "on" : "off");
    }
}
// ------------------------------------------------------------------ connection stats
// Guest side: round trip time (pings), lost snapshots, jitter and the view delay the
// jitter buffer chose, frame rate. Shown with the console key (~: console, then this
// display, then off), written to the log every 10 s, and
// sent to the host in each ping so the host's log has them too.
static bool g_showStats = false;
static float g_pingMs = -1, g_lossPct = 0, g_jitterMs = 0, g_delayMs = 0, g_fps = 0;
static std::set<uint32_t> g_snapSeen; // snapshot numbers that arrived this stats window
static double g_statWinStart = 0, g_lastPing = 0, g_lastStatLog = 0;
static long g_frameCount = 0;
static bool g_statWindowDone = false; // a 2 s stats window just closed (set in NetFrame)
// Guest clock: the world is shown at local time + g_offset (in host seconds).
// g_offset follows the least-delayed recent snapshot minus a delay sized to the
// measured jitter, and only ever glides (never jumps) so motion stays smooth on
// a jittery internet / VPN link.
static double g_offset = 0, g_targetOffset = 0, g_lastRt = -1e9, g_lastStepT = 0;
static bool g_offsetValid = false;
static std::deque<std::pair<double, double>> g_offSamples; // (local time, host time - local time)
static int g_testJitter = 0; // LUGARUEX_TEST_JITTER (ms)
static long g_stSteps = 0, g_stStarved = 0;
static long g_bloodReplays = 0, g_rumbles = 0;
static long g_fxSent = 0, g_fxPlayed = 0; // effects (sprites, ground decals) sent / shown
static float g_jerkMax = 0; // guest: biggest sudden change in its own character's drawn movement
static long g_jerkBig = 0;
static double g_stDelaySum = 0;
struct Delayed
{
    double at;
    std::vector<uint8_t> data;
    sockaddr_in from;
};
static std::vector<Delayed> g_delayed;
static void clockSample(double ht, double t)
{
    g_offSamples.push_back({ t, ht - t });
    while (!g_offSamples.empty() && t - g_offSamples.front().first > 2.0) {
        g_offSamples.pop_front();
    }
    std::vector<double> s;
    s.reserve(g_offSamples.size());
    for (auto& e : g_offSamples) {
        s.push_back(e.second);
    }
    std::sort(s.begin(), s.end());
    const double best = s.back();                       // least network delay
    const double slow = s[(size_t)(s.size() * 0.03)];   // ignore the rare worst few
    const double jitter = std::min(0.3, std::max(0.0, best - slow));
    const double delay = 0.035 + jitter * 1.1;          // two snapshot intervals + jitter
    g_jitterMs = (float)(jitter * 1000);
    g_delayMs = (float)(delay * 1000);
    g_targetOffset = best - delay;
    if (!g_offsetValid || fabs(g_targetOffset - g_offset) > 0.5) {
        g_offset = g_targetOffset;
        g_offsetValid = true;
        g_lastRt = -1e9;
    }
}
static double guestRenderTime()
{
    const double t = now();
    const double dt = std::min(0.1, std::max(0.0, t - g_lastStepT));
    g_lastStepT = t;
    const double diff = g_targetOffset - g_offset;
    // more delay is needed quickly (else the picture starves), less can wait
    g_offset += std::max(-0.25 * dt, std::min(0.05 * dt, diff));
    double rt = t + g_offset;
    if (rt < g_lastRt) {
        rt = g_lastRt; // never back in time
    }
    g_lastRt = rt;
    return rt;
}
static std::deque<uint32_t> g_seenEvents;
static std::unordered_set<uint32_t> g_seenSet;
static int g_score = 0, g_slomo = 0, g_winfreeze = 0;
static std::string g_status;

// ------------------------------------------------------------------ init
static void hostPrepareMods(); // (mod sharing, below)
static void guestModsForget();

void NetInit()
{
    g_t0 = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
    g_showStats = getenv("LUGARUEX_TEST_STATS") != nullptr; // test hook: stats display on from the start
    if (const char* j = getenv("LUGARUEX_TEST_JITTER")) {
        g_testJitter = std::max(0, atoi(j));
    }
    g_port = cfg.mpPort > 0 && cfg.mpPort < 65536 ? cfg.mpPort : 27015;
    g_lan = cfg.mpLan != 0;
    if (cfg.mpMode == "host") {
        if (openSocket(g_port, true)) {
            g_role = R_HOST;
            printf("LugaruEX net: hosting on UDP port %d%s\n", g_port, g_lan ? " (LAN: answering searches)" : "");
            hostPrepareMods(); // pack the mods that are on, for guests to download
        }
    } else if (cfg.mpMode == "join") {
        if (openSocket(0, true)) {
            g_role = R_GUEST;
            guestModsForget(); // with sharing on, wait for the host's mod list
            std::random_device rd;
            g_token = ((uint64_t)rd() << 32) ^ rd() ^ (uint64_t)SDL_GetPerformanceCounter();
            if (!g_lan) {
                std::string a = cfg.mpAddress;
                int port = g_port;
                const size_t c = a.rfind(':');
                if (c != std::string::npos) {
                    port = atoi(a.substr(c + 1).c_str());
                    a = a.substr(0, c);
                }
                addrinfo hints = {}, *res = nullptr;
                hints.ai_family = AF_INET;
                hints.ai_socktype = SOCK_DGRAM;
                if (getaddrinfo(a.c_str(), nullptr, &hints, &res) == 0 && res) {
                    g_hostAddr = *(sockaddr_in*)res->ai_addr;
                    g_hostAddr.sin_port = htons((uint16_t)port);
                    g_haveHost = true;
                    freeaddrinfo(res);
                } else {
                    fprintf(stderr, "LugaruEX net: can't find host '%s'\n", a.c_str());
                }
            }
            printf("LugaruEX net: joining %s\n", g_lan ? "(searching the LAN)" : cfg.mpAddress.c_str());
        }
    }
}

void NetShutdown()
{
    if (g_sock == INVALID_SOCKET) {
        return;
    }
    if (g_role == R_GUEST && g_stSteps > 0) {
        printf("LugaruEX net: guest view %.0f ms behind the newest snapshot on average, ran past it in %.1f%% of steps\n",
               1000.0 * g_stDelaySum / g_stSteps, 100.0 * g_stStarved / g_stSteps);
        printf("LugaruEX net: guest replayed %ld blood effects, %ld rumbles\n", g_bloodReplays, g_rumbles);
        printf("LugaruEX net: own character jerk: max %.3f, %ld sudden steps\n", g_jerkMax, g_jerkBig);
    }
    if (g_role == R_GUEST) {
        printf("LugaruEX net: guest showed %ld host effects\n", g_fxPlayed);
    } else if (g_role == R_HOST) {
        printf("LugaruEX net: host sent %ld effects\n", g_fxSent);
    }
    Writer w;
    w.head(T_BYE);
    w.u64(g_token);
    if (g_role == R_GUEST && g_haveHost) {
        sendTo(w, g_hostAddr);
    }
    if (g_role == R_HOST) {
        for (auto& c : g_clients) {
            sendTo(w, c.addr);
        }
    }
    const double secs = std::max(1.0, now());
    printf("LugaruEX net: sent %.0f KB in %.0f s (%.1f KB/s, %.0f packets/s)%s\n", g_bytesOut / 1024, secs, g_bytesOut / 1024 / secs,
           g_packetsOut / secs, g_role == R_HOST ? " to all guests" : "");
    if (g_role == R_HOST) {
        printf("LugaruEX net: %ld rumble events sent to guests\n", g_rumbles);
    }
    closesocket(g_sock);
    g_sock = INVALID_SOCKET;
#ifdef _WIN32
    WSACleanup();
#endif
}

bool NetIsHost()
{
    return g_role == R_HOST;
}

bool NetIsGuest()
{
    return g_role == R_GUEST;
}

static std::vector<int> g_awards; // guest: the host's awards when the level was won, in its order
static int g_dlgWhich = -1, g_dlgScene = 0, g_dlgOwner = 0; // guest: the host's dialogue scene (-1 = none)

int NetGuestAwards(int* awards)
{
    if (g_role != R_GUEST) {
        return -1;
    }
    int n = 0;
    for (int a : g_awards) {
        if (a >= 0 && a < award_count && n < award_count) {
            awards[n++] = a;
        }
    }
    return n;
}

// ------------------------------------------------------------------ host: out
static bool hostInLevel()
{
    return Game::gameon && !Game::loading && !Person::players.empty();
}

static void hostSendLevel(Client& c)
{
    std::vector<NetLook> roster;
    LocalRoster(roster);
    Writer w;
    w.head(T_LEVEL);
    w.u32(g_levelSeq);
    w.u32(g_rosterVer);
    w.u8(g_levelTutorial ? 1 : 0);
    w.str(g_levelName);
    w.u8((int)c.slots.size()); // which character is each of that PC's players
    for (int s : c.slots) {
        w.i16(LocalSlotPerson(s));
    }
    // the looks that fit in one packet (any more play with the level's look there); the count
    // is written after them so it always matches, and room is kept for the fingerprints
    const int countAt = w.n;
    w.u8(0);
    int count = 0;
    for (size_t i = 0; i < roster.size() && count < 255 && w.room(8 + 6 + (int)roster[i].skin.size() + (int)roster[i].outfit.size()); i++) {
        w.u8(roster[i].person);
        w.u8(roster[i].creature);
        w.u8(roster[i].whichskin);
        w.str(roster[i].skin);
        w.str(roster[i].outfit);
        count++;
    }
    w.b[countAt] = (uint8_t)count;
    w.u32(g_mapPrint); // fingerprints: the guest checks its own copies against these
    w.u32(g_terrainPrint);
    sendTo(w, c.addr);
    c.lastLevelSent = now();
}

static void writePerson(Writer& w, int idx, const Person& p, bool joints)
{
    w.u8('P');
    w.u8(idx);
    w.f32(p.coords.x);
    w.f32(p.coords.y);
    w.f32(p.coords.z);
    w.i16(angle100(p.yaw));
    w.i16(angle100(p.tilt));
    w.i16(angle100(p.tilt2));
    w.i16(angle100(p.targetheadyaw));
    w.i16(angle100(p.targetheadpitch));
    w.u8(p.animCurrent);
    w.u8(p.animTarget);
    w.u8(p.frameCurrent);
    w.u8(p.frameTarget);
    w.u16((int)lroundf(std::max(0.f, std::min(1.f, p.target)) * 65535));
    w.u8(p.dead);
    w.u8(p.skeleton.free);
    w.u8(p.aitype);
    // onterrain: the guest plants feet on the ground (IK) like the host; stunned and howactive
    // (awake, asleep, knocked out...): the face (stunned, eyes shut, open mouth)
    w.u8((p.onfire ? 1 : 0) | (p.headless ? 2 : 0) | (p.onterrain ? 4 : 0) | (p.stunned > 0 ? 8 : 0) | ((p.howactive & 7) << 4));
    w.u16((int)p.damage);
    w.u16((int)p.bloodloss);
    w.u16((int)p.permanentdamage);
    w.u16((int)p.superpermanentdamage);
    w.u16((int)p.damagetolerance);
    w.u8((int)(std::max(0.f, std::min(1.f, p.blooddimamount)) * 255));
    w.u8(p.num_weapons);
    w.i8(p.weaponactive);
    w.i8(p.weaponstuck);
    w.u8(p.weaponstuckwhere);
    for (int k = 0; k < 4; k++) {
        w.i8(k < p.num_weapons ? p.weaponids[k] : -1);
    }
    const XYZ* offs[2] = { &p.currentoffset, &p.targetoffset };
    for (const XYZ* o : offs) {
        w.i16((int)lroundf(o->x * 1024));
        w.i16((int)lroundf(o->y * 1024));
        w.i16((int)lroundf(o->z * 1024));
    }
    const int nj = joints ? (int)std::min<size_t>(p.skeleton.joints.size(), 60) : 0;
    w.u8(nj);
    for (int j = 0; j < nj; j++) {
        const XYZ& q = p.skeleton.joints[j].position;
        w.i16((int)lroundf(q.x * 1024));
        w.i16((int)lroundf(q.y * 1024));
        w.i16((int)lroundf(q.z * 1024));
    }
}

static int personSize(const Person& p, bool joints)
{
    return 66 + (joints ? 6 * (int)std::min<size_t>(p.skeleton.joints.size(), 60) : 0); // = what writePerson writes
}

static void hostSnapshots()
{
    // the roster changes when someone joins: guests add their character
    std::vector<NetLook> roster;
    LocalRoster(roster);
    std::string sig;
    for (auto& l : roster) {
        sig += std::to_string(l.person) + "," + std::to_string(l.creature) + "," + std::to_string(l.whichskin) + "," + l.skin + ";";
    }
    if (sig != g_rosterSig) {
        g_rosterSig = sig;
        g_rosterVer++;
    }
    const double t = now();
    while (!g_events.empty() && t - g_events.front().t > kEventRepeat) {
        g_events.pop_front();
    }
    g_snapSeq++;
    // settled bodies don't change: their joints only go out twice a second
    const bool slowTick = (g_snapSeq % 30) == 0;

    std::vector<Writer> packets(1);
    auto startPacket = [&](Writer& w) {
        w.head(T_SNAP);
        w.u32(g_levelSeq);
        w.u32(g_snapSeq);
        w.f32((float)g_simClock); // simulation time: even if this PC runs several steps in one
                                  // frame, guests see them spread out as they were simulated
        w.u8((slomo ? 1 : 0) | (winfreeze ? 2 : 0));
        w.u32((uint32_t)bonustotal);
        w.f32(Game::leveltime);
        // the end-of-level screen: level score (score - score at the start), time and awards
        w.u32((uint32_t)startbonustotal);
        w.f32(Game::wonleveltime);
        {
            int awards[award_count];
            const int n = winfreeze ? std::min(award_awards(awards), 15) : 0;
            w.u8(n);
            for (int i = 0; i < n; i++) {
                w.u8(awards[i]);
            }
        }
        // bushes and trees set on fire by a burning character (campfires always burn): they
        // burn until the level ends, so a guest that joins later sees them burning too
        {
            std::vector<int> burning;
            for (size_t i = 0; i < Object::objects.size() && burning.size() < 40; i++) {
                if (Object::objects[i]->onfire && Object::objects[i]->type != firetype && i < 65535) {
                    burning.push_back((int)i);
                }
            }
            w.u8((int)burning.size());
            for (int i : burning) {
                w.u16(i);
            }
        }
        w.u8((int)std::min<size_t>(255, weapons.size())); // the tutorial adds and removes weapons
        // tutorial: the step, its timer and who has done it (guests don't run the tutorial)
        w.u8(Tutorial::active ? 1 : 0);
        if (Tutorial::active) {
            w.u8(Tutorial::stage);
            w.f32(Tutorial::stagetime);
            w.f32(Tutorial::maxtime);
            w.u8(Tutorial::PlayersDone());
            w.u8(Tutorial::PlayersIn());
            w.u32(Tutorial::DoneMask());
            w.f32(damagedealt); // the sparring step shows the team's totals
            w.f32(damagetaken);
        }
        // dialogue scene: guests show it too (they loaded the level's dialogues); 255 = none
        const bool dlg = Dialog::inDialog() && !Dialog::directing;
        w.u8(dlg ? std::min(254, Dialog::whichdialogue) : 255);
        w.u8(dlg ? std::min(255, Dialog::indialogue) : 0);
        w.u8(dlg ? std::min(255, DialogOwner()) : 0);
    };
    startPacket(packets[0]);
    auto room = [&](int k) -> Writer& {
        if (!packets.back().room(k)) {
            packets.emplace_back();
            startPacket(packets.back());
        }
        return packets.back();
    };
    for (auto& e : g_events) { // events first, so they survive in the first packet
        const bool fx = e.kind == 'F' || e.kind == 'G';
        if (fx && t - e.t > kFxRepeat) {
            continue; // effects go out a few times only
        }
        Writer& w = room(fx ? 35 : 23);
        w.u8(e.kind);
        w.u32(e.id);
        w.i16(e.a);
        w.i16(e.b);
        w.i16(e.c);
        w.f32(e.x);
        w.f32(e.y);
        w.f32(e.z);
        if (fx) {
            w.i16(fxClamp(e.vx * 50, -32767, 32767));
            w.i16(fxClamp(e.vy * 50, -32767, 32767));
            w.i16(fxClamp(e.vz * 50, -32767, 32767));
            w.u8(e.r);
            w.u8(e.g);
            w.u8(e.bl);
            w.u8(e.special);
            w.u8(e.speed);
            w.u8(e.alive);
        }
    }
    for (size_t i = 0; i < Person::players.size() && i < 255; i++) {
        const Person& p = *Person::players[i];
        const bool joints = p.skeleton.free && (p.skeleton.free != 2 || slowTick);
        writePerson(room(personSize(p, joints)), (int)i, p, joints);
    }
    for (size_t i = 0; i < weapons.size() && i < 255; i++) {
        Weapon& wp = weapons[i]; // (getType is not const)
        if (wp.owner != -1) {
            Writer& w = room(5);
            w.u8('W');
            w.u8((int)i);
            w.i8(wp.owner);
            w.u8(wpFlags(wp)); // bloody, dripping, burning
            w.u8(wp.getType()); // the tutorial turns weapons into others
        } else {
            Writer& w = room(33);
            w.u8('W');
            w.u8((int)i);
            w.i8(-1);
            w.u8(wpFlags(wp)); // bloody, dripping, burning
            w.u8(wp.getType()); // the tutorial turns weapons into others
            w.f32(wp.position.x);
            w.f32(wp.position.y);
            w.f32(wp.position.z);
            const float r[8] = { wp.rotation1, wp.rotation2, wp.rotation3, wp.bigrotation, wp.bigtilt, wp.bigtilt2, wp.smallrotation, wp.smallrotation2 };
            for (float a : r) {
                w.i16(angle100(a));
            }
        }
    }
    for (auto& c : g_clients) {
        if (!c.connected) {
            continue;
        }
        if (c.ackLevel != g_levelSeq || c.ackRoster != g_rosterVer) {
            if (t - c.lastLevelSent > 0.25) {
                hostSendLevel(c);
            }
            if (c.ackLevel != g_levelSeq) {
                continue; // still loading the level: snapshots would be for the wrong map
            }
        }
        for (auto& pk : packets) {
            sendTo(pk, c.addr);
        }
    }
}

void NetAfterSimStep(double dt)
{
    if (g_role != R_HOST) {
        return;
    }
    g_simClock += dt;
    g_sendAcc += dt;
    if (g_sendAcc < 1.0 / kSendRate) {
        return;
    }
    g_sendAcc = fmod(g_sendAcc, 1.0 / kSendRate);
    if (hostInLevel()) {
        hostSnapshots();
    } else if (now() - g_lastIdle > 0.5) {
        g_lastIdle = now();
        Writer w;
        w.head(T_IDLE);
        for (auto& c : g_clients) {
            if (c.connected) {
                sendTo(w, c.addr);
            }
        }
    }
}

void NetLevelLoaded(const std::string& name, bool tutorial)
{
    if (g_role != R_HOST) {
        return;
    }
    g_levelSeq++;
    g_rosterVer++;
    g_rosterSig.clear();
    g_levelName = name;
    g_levelTutorial = tutorial;
    g_mapPrint = filePrint("Maps/" + name);
    {
        g_terrainPrint = groundPrint(); // the ground as built (snow / desert change it)
    }
    g_events.clear();
    for (auto& c : g_clients) {
        c.lastLevelSent = -10; // tell everyone straight away
    }
}

void NetSoundEvent(int sound, const XYZ* pos, float volume)
{
    if (g_role != R_HOST || g_clients.empty() || !hostInLevel() || mainmenu) { // not the host's own menu clicks
        return;
    }
    Event e = {};
    e.id = ++g_eventId;
    e.kind = 'S';
    e.a = sound;
    e.b = pos ? 1 : 0;
    e.c = (int)volume;
    if (pos) {
        e.x = pos->x, e.y = pos->y, e.z = pos->z;
    }
    e.t = now();
    g_events.push_back(e);
}

void NetBonusEvent(int person, int bonus, int value)
{
    if (g_role != R_HOST || g_clients.empty()) {
        return;
    }
    Event e = {};
    e.id = ++g_eventId;
    e.kind = 'B';
    e.a = person, e.b = bonus, e.c = value;
    e.t = now();
    g_events.push_back(e);
}


static bool g_muted = false;
bool NetMuted()
{
    return g_muted;
}

void NetBloodEvent(int person, int kind, float howmuch, int which, const XYZ* where, bool spurt)
{
    // a plain DoBlood only moves the bleeding spot (the guest does not simulate drips): send the sprays only
    if (g_role != R_HOST || g_clients.empty() || !hostInLevel() || mainmenu || (kind == 0 && !spurt)) {
        return;
    }
    Event e = {};
    e.id = ++g_eventId;
    e.kind = 'D';
    e.a = person;
    e.b = (kind & 3) | (spurt ? 4 : 0);
    e.c = (which & 0xff) | (std::max(0, std::min(127, (int)(howmuch * 16 + .5f))) << 8);
    if (where) {
        e.x = where->x, e.y = where->y, e.z = where->z;
    }
    e.t = now();
    g_events.push_back(e);
}

void NetRumbleEvent(int person, float strength, int ms)
{
    if (g_role != R_HOST || g_clients.empty()) {
        return;
    }
    Event e = {};
    e.id = ++g_eventId;
    e.kind = 'R';
    g_rumbles++;
    e.a = person;
    e.b = (int)(std::max(0.f, std::min(1.f, strength)) * 1000);
    e.c = std::max(0, std::min(2000, ms));
    e.t = now();
    g_events.push_back(e);
}

// ------------------------------------------------------------------ shared effects
// Guests don't simulate, so the sprites and ground decals the host's simulation makes
// (hit puffs, blood sprays, dust, splinters, breath, footprints, blood pools) ride
// along as events. Effects are many and short-lived: each goes out with ~4 snapshots
// (a lost one is no loss), and a burst is capped.
static int g_fxShare = 0, g_fxLocal = 0;
static int g_fxLastEvent = -1; // g_events index of the sprite just recorded (setLastSprite*)


NetFxShare::NetFxShare()
{
    g_fxShare++;
}
NetFxShare::~NetFxShare()
{
    g_fxShare--;
}
NetFxLocal::NetFxLocal()
{
    g_fxLocal++;
}
NetFxLocal::~NetFxLocal()
{
    g_fxLocal--;
}

static bool fxSharing()
{
    return g_fxShare > 0 && g_fxLocal == 0 && g_role == R_HOST && !g_clients.empty() && hostInLevel() && g_events.size() < 1500;
}

bool NetSpriteHook(int type, const XYZ& pos, const XYZ& vel, float r, float g, float b, float size, float opacity)
{
    g_fxLastEvent = -1;
    if (g_role == R_GUEST) {
        return g_muted; // a blood replay: its sprays come from the host, as they were made there
    }
    if (!fxSharing()) {
        return false;
    }
    Event e = {};
    e.id = ++g_eventId;
    e.kind = 'F';
    e.a = type;
    e.b = fxClamp(size * 1000, 0, 32767);
    e.c = fxClamp(opacity * 1000, 0, 32767);
    e.x = pos.x, e.y = pos.y, e.z = pos.z;
    e.vx = vel.x, e.vy = vel.y, e.vz = vel.z;
    e.r = (uint8_t)fxClamp(r * 127.5f, 0, 255);
    e.g = (uint8_t)fxClamp(g * 127.5f, 0, 255);
    e.bl = (uint8_t)fxClamp(b * 127.5f, 0, 255);
    e.t = now();
    g_events.push_back(e);
    g_fxLastEvent = (int)g_events.size() - 1;
    g_fxSent++;
    return false;
}

void NetSpriteLast(int what, float value)
{
    if (g_fxLastEvent < 0 || g_fxLastEvent >= (int)g_events.size() || g_events[g_fxLastEvent].kind != 'F') {
        return;
    }
    Event& e = g_events[g_fxLastEvent];
    if (what == 0) {
        e.special = (uint8_t)fxClamp(value, 0, 255);
    } else if (what == 1) {
        e.speed = (uint8_t)fxClamp(value, 1, 255);
    } else {
        e.alive = (uint8_t)fxClamp(value * 100, 1, 255);
    }
}

void NetDecalHook(int type, const XYZ& pos, float size, float opacity, float rotation)
{
    if (!fxSharing()) {
        return;
    }
    Event e = {};
    e.id = ++g_eventId;
    e.kind = 'G';
    e.a = type;
    e.b = fxClamp(size * 1000, 0, 32767);
    e.c = fxClamp(opacity * 1000, 0, 32767);
    e.x = pos.x, e.y = pos.y, e.z = pos.z;
    e.vx = fmodf(rotation, 360.f);
    e.t = now();
    g_events.push_back(e);
    g_fxSent++;
}

// ------------------------------------------------------------------ host: in
static Client* clientByAddr(const sockaddr_in& a)
{
    for (auto& c : g_clients) {
        if (sameAddr(c.addr, a)) {
            return &c;
        }
    }
    return nullptr;
}

// ------------------------------------------------------------------ mod sharing
// The host packs each mod it has on into LugaruEX/Mods/.cache/<mod>.zip and offers the
// list (hash, size, name). A guest downloads what it lacks in 1 KB pieces over this same
// connection into LugaruEX/Mods/.net/<hash>/, checks the hash, unpacks it (game data
// only) and plays with exactly the host's mods. Until then it ignores the host's LEVEL
// (the host resends it), so it never joins a level without them.
static const int kModChunk = 1024;
static const int kModWindow = 160;                         // pieces in flight (160 KB per round trip)
static const uint64_t kModMaxTotal = 2048ull * 1024 * 1024; // what a guest takes at most
static const int kModMaxList = 14;                          // mods in one list packet

struct ShareMod // host
{
    std::string folder, zip;
    uint64_t hash = 0;
    uint32_t size = 0;
    FILE* f = nullptr;
};
static std::vector<ShareMod> g_share;
static bool g_shareOn = false;

struct GetMod // guest
{
    std::string name, part, dir;
    uint64_t hash = 0;
    uint32_t size = 0, got = 0;
    std::vector<uint8_t> have;
    std::map<uint32_t, double> pending; // piece -> when it was asked for
    FILE* f = nullptr;
    bool done = false;
    int tries = 0;
};
static std::vector<GetMod> g_get;
static bool g_modsReady = false;   // guest: may join the host's levels
static uint64_t g_modListSig = 0;  // guest: the list being worked on
static std::string g_modProblem;   // guest: shown while waiting
static double g_lastModReady = -10;

static std::string hex64(uint64_t v)
{
    char b[20];
    snprintf(b, sizeof(b), "%016llx", (unsigned long long)v);
    return b;
}

static void hostPrepareMods()
{
    g_shareOn = cfg.mpShareMods != 0;
    if (!g_shareOn) {
        return;
    }
    const std::string cache = ModsFolder() + ".cache";
    fsu::MakeDirs(cache);
    for (const std::string& folder : ModsLocalEnabled()) {
        if ((int)g_share.size() >= kModMaxList) {
            printf("LugaruEX net: only the first %d mods are shared\n", kModMaxList);
            break;
        }
        ShareMod m;
        m.folder = folder;
        m.zip = cache + "/" + folder + ".zip";
        if (!Mod_PackFolder(ModsFolder() + folder, m.zip)) {
            printf("LugaruEX net: could not pack mod '%s' for sharing\n", folder.c_str());
            continue;
        }
        m.hash = Mod_HashFile(m.zip);
        const uint64_t size = fsu::Size(m.zip);
        m.f = fsu::Open(m.zip, "rb");
        if (!m.f || size == 0 || size > 0xFFFFFFF0ull) {
            if (m.f) {
                fclose(m.f);
            }
            continue;
        }
        m.size = (uint32_t)size;
        printf("LugaruEX net: sharing mod '%s' (%.1f MB)\n", folder.c_str(), m.size / 1048576.0);
        g_share.push_back(m);
    }
}

static void hostSendModList(Client& c)
{
    Writer w;
    w.head(T_MODLIST);
    w.u8(g_shareOn ? 1 : 0);
    w.u8((int)g_share.size());
    for (const ShareMod& m : g_share) {
        w.u64(m.hash);
        w.u32(m.size);
        w.str(m.folder);
    }
    sendTo(w, c.addr);
    c.lastModList = now();
}

static void hostModGet(Client& c, Reader& r)
{
    const uint64_t hash = r.u64();
    const uint32_t offset = r.u32();
    if (!r.ok) {
        return;
    }
    for (ShareMod& m : g_share) {
        if (m.hash != hash || offset >= m.size || offset % kModChunk) {
            continue;
        }
        uint8_t buf[kModChunk];
        const int len = (int)std::min<uint32_t>(kModChunk, m.size - offset);
        if (fseek(m.f, (long)offset, SEEK_SET) != 0 || (int)fread(buf, 1, len, m.f) != len) {
            return;
        }
        Writer w;
        w.head(T_MODDATA);
        w.u64(hash);
        w.u32(offset);
        w.u16(len);
        w.bytes(buf, len);
        sendTo(w, c.addr);
        return;
    }
}

static void guestSendModReady()
{
    Writer w;
    w.head(T_MODREADY);
    sendTo(w, g_hostAddr);
    g_lastModReady = now();
}

static void guestModsReady(bool useHosts)
{
    for (GetMod& m : g_get) {
        if (m.f) {
            fclose(m.f);
            m.f = nullptr;
        }
    }
    std::vector<std::string> roots;
    for (const GetMod& m : g_get) {
        roots.push_back(m.dir);
    }
    ModsSetSession(roots, useHosts);
    g_modsReady = true;
    g_modProblem.clear();
    guestSendModReady();
}

static void guestModList(Reader& r)
{
    const bool sharing = r.u8() != 0;
    const int count = r.u8();
    std::vector<GetMod> list;
    uint64_t sig = sharing ? 0x9e3779b97f4a7c15ull : 1;
    uint64_t total = 0;
    for (int k = 0; k < count && r.ok; k++) {
        GetMod m;
        m.hash = r.u64();
        m.size = r.u32();
        m.name = r.str();
        sig = (sig ^ m.hash) * 1099511628211ull;
        total += m.size;
        list.push_back(m);
    }
    if (!r.ok) {
        return;
    }
    if (sig == g_modListSig) {
        if (g_modsReady) {
            guestSendModReady(); // the host didn't hear it yet
        }
        return;
    }
    g_modListSig = sig;
    g_get.clear();
    if (!sharing || cfg.mpShareMods == 0) {
        // the host keeps its mods to itself, or this PC said no: play with this PC's own
        printf("LugaruEX net: %s\n", !sharing ? "the host doesn't share mods" : "not downloading the host's mods (Mod share is off)");
        guestModsReady(false);
        return;
    }
    if (total > kModMaxTotal) {
        g_modProblem = "The host's mods are too big to download - playing without them";
        printf("LugaruEX net: the host's mods are %.0f MB - too big, playing without them\n", total / 1048576.0);
        guestModsReady(false);
        return;
    }
    const std::string net = ModsFolder() + ".net";
    fsu::MakeDirs(net);
    for (GetMod& m : list) {
        m.dir = net + "/" + hex64(m.hash);
        m.part = m.dir + ".part";
        m.done = fsu::IsFile(m.dir + "/.done");
        if (!m.done) {
            m.have.assign((m.size + kModChunk - 1) / kModChunk, 0);
            fsu::RemoveTree(m.dir);
            m.f = fsu::Open(m.part, "w+b");
            if (!m.f) {
                g_modProblem = "Can't save the host's mods - playing without them";
                g_get.clear();
                guestModsReady(false);
                return;
            }
            printf("LugaruEX net: downloading the host's mod '%s' (%.1f MB)\n", m.name.c_str(), m.size / 1048576.0);
        }
        g_get.push_back(m);
    }
    g_modsReady = false;
    bool all = true;
    for (const GetMod& m : g_get) {
        all &= m.done;
    }
    if (all) {
        printf("LugaruEX net: the host's %d mod%s already here\n", (int)g_get.size(), g_get.size() == 1 ? " is" : "s are");
        guestModsReady(true);
    }
}

static void guestModFinished(GetMod& m)
{
    fclose(m.f);
    m.f = nullptr;
    std::string err;
    if (Mod_HashFile(m.part) == m.hash && Mod_ExtractTo(m.part, m.dir, err)) {
        fsu::WriteAll(m.dir + "/.done", m.name + "\n");
        fsu::RemoveTree(m.part);
        m.done = true;
        printf("LugaruEX net: got the host's mod '%s'\n", m.name.c_str());
    } else if (++m.tries < 2) {
        printf("LugaruEX net: mod '%s' arrived damaged (%s) - downloading it again\n", m.name.c_str(), err.c_str());
        fsu::RemoveTree(m.dir);
        std::fill(m.have.begin(), m.have.end(), 0);
        m.got = 0;
        m.pending.clear();
        m.f = fsu::Open(m.part, "w+b");
    } else {
        g_modProblem = "A mod from the host could not be used - playing without the host's mods";
        printf("LugaruEX net: mod '%s' failed twice - playing without the host's mods\n", m.name.c_str());
        g_get.clear();
        guestModsReady(false);
        return;
    }
    bool all = true;
    for (const GetMod& o : g_get) {
        all &= o.done;
    }
    if (all) {
        guestModsReady(true);
    }
}

static void guestModData(Reader& r)
{
    const uint64_t hash = r.u64();
    const uint32_t offset = r.u32();
    const int len = r.u16();
    uint8_t buf[kModChunk];
    if (!r.ok || len <= 0 || len > kModChunk || !r.bytes(buf, len)) {
        return;
    }
    for (GetMod& m : g_get) {
        if (m.hash != hash || m.done || !m.f || offset % kModChunk || offset >= m.size) {
            continue;
        }
        const uint32_t idx = offset / kModChunk;
        const int want = (int)std::min<uint32_t>(kModChunk, m.size - offset);
        m.pending.erase(idx);
        if (m.have[idx] || len != want) {
            return;
        }
        if (fseek(m.f, (long)offset, SEEK_SET) != 0 || (int)fwrite(buf, 1, len, m.f) != len) {
            return;
        }
        m.have[idx] = 1;
        m.got += len;
        if (m.got >= m.size) {
            guestModFinished(m);
        }
        return;
    }
}

// guest, every frame: keep up to kModWindow pieces in flight; ask again for lost ones
static void guestModPump(double t)
{
    if (g_modsReady) {
        if (t - g_lastModReady > 1.0 && g_modListSig) {
            guestSendModReady(); // until the host stops sending the list (cheap)
        }
        return;
    }
    int inFlight = 0;
    uint64_t got = 0, total = 0;
    for (GetMod& m : g_get) {
        total += m.size;
        got += m.done ? m.size : m.got;
        for (auto it = m.pending.begin(); it != m.pending.end();) {
            if (t - it->second > 0.8) {
                it = m.pending.erase(it); // lost: ask again
            } else {
                ++it;
                inFlight++;
            }
        }
    }
    for (GetMod& m : g_get) {
        if (m.done || !m.f) {
            continue;
        }
        for (uint32_t i = 0; i < m.have.size() && inFlight < kModWindow; i++) {
            if (m.have[i] || m.pending.count(i)) {
                continue;
            }
            Writer w;
            w.head(T_MODGET);
            w.u64(m.hash);
            w.u32(i * (uint32_t)kModChunk);
            sendTo(w, g_hostAddr);
            m.pending[i] = t;
            inFlight++;
        }
    }
    if (total > 0) {
        char b[128];
        snprintf(b, sizeof(b), "Downloading the host's mods: %.1f / %.1f MB", got / 1048576.0, total / 1048576.0);
        g_status = b;
    } else if (!g_modListSig) {
        g_status = "Getting the host's mod list...";
    }
}

static void guestModsForget()
{
    for (GetMod& m : g_get) {
        if (m.f) {
            fclose(m.f);
        }
    }
    g_get.clear();
    g_modListSig = 0;
    g_modsReady = cfg.mpShareMods == 0; // without sharing, a guest never waits for a list
    ModsSetSession({}, false);
}

static void hostReceive(const uint8_t* d, int n, const sockaddr_in& from)
{
    Reader r(d, n);
    if (r.u32() != kMagic) {
        return;
    }
    const int type = r.u8();
    const double t = now();
    if (type == T_DISCOVER) {
        if (g_lan) {
            Writer w;
            w.head(T_HERE);
            sendTo(w, from);
        }
        return;
    }
    if (type == T_HELLO) {
        const uint64_t token = r.u64();
        const int count = std::max(1, std::min(4, r.u8())); // players on that PC (split screen there)
        std::vector<std::string> chars;
        for (int k = 0; k < count; k++) {
            chars.push_back(r.str());
        }
        if (!r.ok) {
            return;
        }
        Client* c = nullptr;
        for (auto& o : g_clients) {
            if (o.token == token) {
                c = &o;
            }
        }
        if (!c) {
            g_clients.push_back(Client());
            c = &g_clients.back();
            c->token = token;
        }
        c->addr = from;
        c->lastHeard = t;
        const bool wasConnected = c->connected;
        const size_t before = c->slots.size();
        // one seat per player there; the same token (+ player number) gets the same seat back
        for (int k = 0; k < count; k++) {
            const int slot = LocalAddRemote(token + (uint64_t)k, chars[k]);
            if (k < (int)c->slots.size()) {
                c->slots[k] = slot;
            } else if (slot >= 0) {
                c->slots.push_back(slot);
            }
        }
        c->connected = !c->slots.empty() && c->slots[0] >= 0;
        if (!wasConnected || c->slots.size() != before) {
            c->ackLevel = c->ackRoster = 0;
            c->lastLevelSent = -10;
            c->modsReady = false;
            c->lastModList = -10;
            char ip[64];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            printf("LugaruEX net: %s joined with %d player%s (%s)\n", ip, (int)c->slots.size(), c->slots.size() == 1 ? "" : "s",
                   c->connected ? "playing" : "full");
        }
        Writer w;
        w.head(T_WELCOME);
        w.u8(c->connected ? 1 : 0);
        sendTo(w, from);
        return;
    }
    Client* c = clientByAddr(from);
    if (!c) {
        return;
    }
    c->lastHeard = t;
    if (type == T_MODGET) {
        hostModGet(*c, r);
        return;
    }
    if (type == T_MODREADY) {
        c->modsReady = true;
        return;
    }
    if (type == T_PING) {
        const uint32_t clock = r.u32();
        const int ping = r.u16(), loss = r.u16(), jit = r.u16(), del = r.u16(), fps = r.u16();
        if (!r.ok) {
            return;
        }
        c->pingMs = ping == 0xffff ? -1 : ping;
        c->lossPermille = loss, c->jitterMs = jit, c->delayMs = del, c->fps = fps;
        Writer w;
        w.head(T_PONG);
        w.u32(clock);
        w.u16((int)g_fps); // the guest shows the host's frame rate too
        sendTo(w, from);
        return;
    }
    if (type == T_INFO) {
        logInfo(r, "guest's");
        if (!c->infoSent) {
            c->infoSent = true;
            Writer w;
            writeInfo(w);
            sendTo(w, from);
        }
        return;
    }
    if (type == T_INPUT) {
        c->inputs++;
        const uint32_t lvl = r.u32(), ros = r.u32();
        const int count = r.u8();
        if (!r.ok) {
            return;
        }
        c->ackLevel = lvl;
        c->ackRoster = ros;
        for (int k = 0; k < count && k < 4; k++) {
            const int btn = r.u16();
            const float yaw = r.f32(), pitch = r.f32();
            if (!r.ok) {
                return;
            }
            if (k < (int)c->slots.size()) {
                PadInput in;
                in.fwd = btn & 1, in.back = btn & 2, in.left = btn & 4, in.right = btn & 8;
                in.jump = btn & 16, in.crouch = btn & 32, in.attack = btn & 64, in.draw = btn & 128, in.thrw = btn & 256;
                in.walk = btn & 512; // (older guests never set it: they run)
                LocalRemoteInput(c->slots[k], in, yaw, pitch, c->connected);
            }
        }
    } else if (type == T_BYE) {
        c->connected = false;
        for (int s : c->slots) {
            LocalRemoteInput(s, PadInput(), 0, 0, false);
        }
        printf("LugaruEX net: a guest left\n");
    }
}

// ------------------------------------------------------------------ guest: in
static void guestLeaveLevel(const char* why)
{
    if (g_inLevel) {
        printf("LugaruEX net: back to waiting (%s)\n", why);
    }
    g_inLevel = false;
    g_gLevel = 0;
    g_gRoster = 0;
    if (Game::gameon) {
        Game::gameon = 0;
        mainmenu = 1;
        winfreeze = false;
        Menu::Load();
    }
}

static void guestLevel(Reader& r)
{
    const uint32_t lvl = r.u32(), ros = r.u32();
    const bool tutorial = r.u8() != 0;
    const std::string name = r.str();
    std::vector<int> mine(r.u8()); // the character of each player on this PC
    for (int& m : mine) {
        m = r.i16();
    }
    const int count = r.u8();
    std::vector<NetLook> roster;
    for (int i = 0; i < count && r.ok; i++) {
        NetLook l;
        l.person = r.u8();
        l.creature = r.u8();
        l.whichskin = r.u8();
        l.skin = r.str();
        l.outfit = r.str();
        roster.push_back(l);
    }
    if (!r.ok || name.empty()) {
        return;
    }
    uint32_t hostMap = 0, hostTerrain = 0; // (hosts before 0.3.6 don't send them)
    if (r.p + 8 <= r.n) {
        hostMap = r.u32();
        hostTerrain = r.u32();
    }
    if (lvl != g_gLevel) {
        printf("LugaruEX net: host loaded '%s'\n", name.c_str());
        Menu::startLevelByName(name, tutorial); // the same map from this PC's own Data folder
        const uint32_t myMap = filePrint("Maps/" + name), myTerrain = groundPrint();
        if ((hostMap && myMap != hostMap) || (hostTerrain && myTerrain != hostTerrain)) {
            printf("LugaruEX net: WARNING - this PC's %s differs from the host's (map %08x vs %08x, terrain %08x vs %08x)\n",
                   (hostMap && myMap != hostMap) ? "level file" : "terrain", myMap, hostMap, myTerrain, hostTerrain);
            g_warn = (hostMap && myMap != hostMap) ? "Your copy of this level differs from the host's - characters may float or sink"
                                                     : "Your ground differs from the host's - characters may float or sink";
            g_warnUntil = now() + 12;
        }
        Game::loading = 0;
        g_hist.assign(Person::players.size() + 8, std::deque<NetPerson>());
        g_whist.assign(weapons.size(), std::deque<NetWeapon>());
        // the host's clock stood still while it loaded: sync to it afresh
        g_offsetValid = false;
        g_offSamples.clear();
        g_lastRt = -1e9;
        g_gLevel = lvl;
        g_gRoster = 0;
        g_dlgWhich = -1;
        InterpInvalidate();
    }
    if (ros != g_gRoster) {
        LocalGuestEnter(mine, roster);
        g_myPerson = mine.empty() ? -1 : mine[0];
        g_gRoster = ros;
        if (g_hist.size() < Person::players.size()) {
            g_hist.resize(Person::players.size());
        }
    }
    g_inLevel = true;
}

static bool seenEvent(uint32_t id)
{
    // events come several times (packet loss); effects come in bursts, so remember plenty
    if (g_seenSet.count(id)) {
        return true;
    }
    g_seenEvents.push_back(id);
    g_seenSet.insert(id);
    if (g_seenEvents.size() > 4096) {
        g_seenSet.erase(g_seenEvents.front());
        g_seenEvents.pop_front();
    }
    return false;
}

static void guestSnap(Reader& r)
{
    const uint32_t lvl = r.u32();
    const uint32_t snapNo = r.u32();
    const double ht = r.f32();
    const int flags = r.u8();
    const int score = (int)r.u32();
    const float hostLevelTime = r.f32();
    const int startScore = (int)r.u32();
    const float wonTime = r.f32();
    std::vector<int> awardList(r.u8());
    for (auto& a : awardList) {
        a = r.u8();
    }
    std::vector<int> burning(r.u8());
    for (auto& o : burning) {
        o = r.u16();
    }
    const int weaponCount = r.u8();
    const bool tut = r.u8() != 0;
    int tStage = 0, tDone = 0, tTotal = 0;
    float tTime = 0, tMax = 0;
    uint32_t tMask = 0;
    float tDealt = 0, tTaken = 0;
    if (tut) {
        tStage = r.u8();
        tTime = r.f32();
        tMax = r.f32();
        tDone = r.u8();
        tTotal = r.u8();
        tMask = r.u32();
        tDealt = r.f32();
        tTaken = r.f32();
    }
    const int dWhich = r.u8(), dScene = r.u8(), dOwner = r.u8();
    if (!r.ok || lvl != g_gLevel || !g_inLevel) {
        return;
    }
    clockSample(ht, now());
    // lost snapshots: distinct numbers that arrived vs the span they cover this window
    g_snapSeen.insert(snapNo);
    // level-wide state only from the newest snapshot (a late one must not roll it back)
    static uint32_t newestApplied = 0;
    if (snapNo >= newestApplied || newestApplied - snapNo > 100000) {
        newestApplied = snapNo;
        g_slomo = flags & 1;
        g_winfreeze = (flags & 2) ? 1 : 0;
        g_score = score;
        Game::leveltime = hostLevelTime;
        startbonustotal = startScore;
        Game::wonleveltime = wonTime;
        g_awards = awardList;
        g_dlgWhich = dWhich == 255 ? -1 : dWhich;
        g_dlgScene = dScene;
        g_dlgOwner = dOwner;
        for (int o : burning) {
            if (o >= 0 && o < (int)Object::objects.size() && !Object::objects[o]->onfire) {
                Object::objects[o]->onfire = 1; // the flames come from Object::DoStuff (exGuestAmbient)
            }
        }
        // the host's weapons: the tutorial hands out and takes away knives and swords
        if ((int)weapons.size() != weaponCount) {
            while ((int)weapons.size() < weaponCount) {
                weapons.push_back(Weapon(knife, -1));
            }
            if ((int)weapons.size() > weaponCount) {
                weapons.erase(weapons.begin() + weaponCount, weapons.end());
            }
            g_whist.resize(weapons.size());
        }
        if (tut && Tutorial::active) {
            Tutorial::stage = tStage;
            Tutorial::stagetime = tTime;
            Tutorial::maxtime = tMax;
            Tutorial::SetRemoteProgress(tDone, tTotal, tMask);
            damagedealt = tDealt;
            damagetaken = tTaken;
        }
    }
    while (r.ok && r.p < r.n) {
        const int kind = r.u8();
        if (kind == 'S' || kind == 'B' || kind == 'D' || kind == 'R') {
            const uint32_t id = r.u32();
            const int a = r.i16(), b = r.i16(), c = r.i16();
            XYZ pos;
            pos.x = r.f32();
            pos.y = r.f32();
            pos.z = r.f32();
            if (!r.ok || seenEvent(id)) {
                continue;
            }
            if (kind == 'S' && a >= 0 && a < sounds_count) {
                if (b) {
                    emit_sound_at(a, pos, (float)c);
                } else {
                    emit_sound_np(a, (float)c);
                }
            } else if (kind == 'B' && a >= 0 && a < (int)Person::players.size()) {
                Person& p = *Person::players[a];
                p.exPopBonus = b;
                p.exPopValue = c;
                p.exPopTime = 0;
            } else if (kind == 'D' && a >= 0 && a < (int)Person::players.size()) {
                // replay the host's blood here: particles, and the wounds painted on the skin
                Person& p = *Person::players[a];
                const float howmuch = (c >> 8) / 16.f;
                const int which = c & 0xff;
                g_muted = true; // its sounds come from the host already
                g_bloodReplays++;
                if (b & 4) {
                    p.spurt = 1;
                    p.bleeding = 0;
                }
                if ((b & 3) == 0) {
                    p.DoBlood(howmuch, which);
                } else if ((b & 3) == 1) {
                    p.DoBloodBig(howmuch, which);
                } else {
                    p.DoBloodBigWhere(howmuch, which, pos);
                }
                g_muted = false;
            } else if (kind == 'R') {
                Rumble(a, b / 1000.f, c);
                g_rumbles++;
            }
        } else if (kind == 'F' || kind == 'G') {
            // a sprite or ground decal the host's simulation made
            const uint32_t id = r.u32();
            const int a = r.i16(), b = r.i16(), c = r.i16();
            XYZ pos, vel;
            pos.x = r.f32();
            pos.y = r.f32();
            pos.z = r.f32();
            vel.x = r.i16() / 50.f;
            vel.y = r.i16() / 50.f;
            vel.z = r.i16() / 50.f;
            const int cr = r.u8(), cg = r.u8(), cb = r.u8(), special = r.u8(), speed = r.u8(), alive = r.u8();
            if (!r.ok || seenEvent(id)) {
                continue;
            }
            g_fxPlayed++;
            if (kind == 'F' && a >= 0 && a < spritenumber) {
                Sprite::MakeSprite(a, pos, vel, cr / 127.5f, cg / 127.5f, cb / 127.5f, b / 1000.f, c / 1000.f);
                if (special) {
                    Sprite::setLastSpriteSpecial(special);
                }
                if (speed) {
                    Sprite::setLastSpriteSpeed(speed);
                }
                if (alive) {
                    Sprite::setLastSpriteAlivetime(alive / 100.f);
                }
            } else if (kind == 'G' && a >= 0 && a <= bodyprintdecal) {
                terrain.MakeDecal((decal_type)a, pos, b / 1000.f, c / 1000.f, vel.x);
            }
        } else if (kind == 'P') {
            NetPerson s;
            s.t = ht;
            const int idx = r.u8();
            s.coords.x = r.f32();
            s.coords.y = r.f32();
            s.coords.z = r.f32();
            s.yaw = r.i16() / 100.f;
            s.tilt = r.i16() / 100.f;
            s.tilt2 = r.i16() / 100.f;
            s.headyaw = r.i16() / 100.f;
            s.headpitch = r.i16() / 100.f;
            s.animCur = r.u8();
            s.animTgt = r.u8();
            s.frameCur = r.u8();
            s.frameTgt = r.u8();
            s.target = r.u16() / 65535.f;
            s.dead = r.u8();
            s.free = r.u8();
            s.aitype = r.u8();
            s.flags = r.u8();
            s.damage = (float)r.u16();
            s.bloodloss = (float)r.u16();
            s.perm = (float)r.u16();
            s.superperm = (float)r.u16();
            s.tol = (float)r.u16();
            s.dim = r.u8() / 255.f;
            s.numw = r.u8();
            s.wactive = r.i8();
            s.wstuck = r.i8();
            s.wstuckwhere = r.u8();
            for (int k = 0; k < 4; k++) {
                s.w[k] = r.i8();
            }
            XYZ* offs[2] = { &s.curoff, &s.tgtoff };
            for (XYZ* o : offs) {
                o->x = r.i16() / 1024.f;
                o->y = r.i16() / 1024.f;
                o->z = r.i16() / 1024.f;
            }
            const int nj = r.u8();
            s.hasJoints = nj > 0;
            s.joints.resize(nj);
            for (int j = 0; j < nj; j++) {
                s.joints[j].x = r.i16() / 1024.f;
                s.joints[j].y = r.i16() / 1024.f;
                s.joints[j].z = r.i16() / 1024.f;
            }
            if (!r.ok) {
                break;
            }
            if (idx >= (int)g_hist.size()) {
                g_hist.resize(idx + 1);
            }
            auto& h = g_hist[idx];
            // a snapshot that overtook this one on the way (VPNs reorder): slot it in by time
            // instead of throwing it away; duplicates and ones older than the history are dropped
            auto at = h.end();
            while (at != h.begin() && (at - 1)->t > ht) {
                --at;
            }
            if ((at != h.begin() && (at - 1)->t == ht) || (at == h.begin() && !h.empty())) {
                continue;
            }
            if (s.free && !s.hasJoints && at != h.begin() && (at - 1)->hasJoints) {
                s.joints = (at - 1)->joints; // a settled body: same pose as last time
                s.hasJoints = true;
            }
            h.insert(at, s);
            while (h.size() > 64 || (h.size() > 2 && h.back().t - h.front().t > 0.8)) {
                h.pop_front(); // enough history for the largest jitter delay
            }
        } else if (kind == 'W') {
            NetWeapon s;
            s.t = ht;
            const int idx = r.u8();
            s.owner = r.i8();
            const int bloodyFire = r.u8();
            s.bloody = bloodyFire & 3;
            s.drip = ((bloodyFire >> 2) & 15) / 3.f;
            s.onfire = (bloodyFire & 0x80) != 0;
            const int wtype = r.u8();
            if (r.ok && idx < (int)weapons.size() && weapons[idx].getType() != wtype && (wtype == knife || wtype == sword || wtype == staff)) {
                weapons[idx].setType(wtype);
            }
            if (s.owner == -1) {
                s.pos.x = r.f32();
                s.pos.y = r.f32();
                s.pos.z = r.f32();
                for (float& a : s.rot) {
                    a = r.i16() / 100.f;
                }
            }
            if (!r.ok) {
                break;
            }
            if (idx < (int)g_whist.size()) {
                auto& h = g_whist[idx];
                auto at = h.end();
                while (at != h.begin() && (at - 1)->t > ht) {
                    --at;
                }
                const bool dup = at != h.begin() && (at - 1)->t == ht;
                if (!dup && (at != h.begin() || h.empty())) {
                    h.insert(at, s);
                    while (h.size() > 64 || (h.size() > 2 && h.back().t - h.front().t > 0.8)) {
                        h.pop_front();
                    }
                }
            }
        } else {
            break;
        }
    }
}

static void guestReceive(const uint8_t* d, int n, const sockaddr_in& from)
{
    Reader r(d, n);
    if (r.u32() != kMagic) {
        return;
    }
    const int type = r.u8();
    if (type == T_HERE) {
        if (!g_haveHost) {
            g_hostAddr = from;
            g_haveHost = true;
            char ip[64];
            inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            printf("LugaruEX net: found a host on the LAN at %s\n", ip);
        }
        return;
    }
    if (!g_haveHost || !sameAddr(from, g_hostAddr)) {
        return;
    }
    g_lastHeard = now();
    switch (type) {
        case T_WELCOME:
            if (!g_welcomed) {
                g_welcomed = true;
                printf("LugaruEX net: connected (%s)\n", r.u8() ? "playing" : "the host is full - watching");
            }
            break;
        case T_LEVEL:
            g_welcomed = true;
            if (g_modsReady) { // (not before the host's mods are here: the host sends it again)
                guestLevel(r);
            }
            break;
        case T_MODLIST:
            guestModList(r);
            break;
        case T_MODDATA:
            guestModData(r);
            break;
        case T_SNAP:
            guestSnap(r);
            break;
        case T_PONG: {
            const uint32_t sent = r.u32();
            if (r.ok) {
                const float rtt = (float)(SDL_GetTicks() - sent);
                g_pingMs = g_pingMs < 0 ? rtt : g_pingMs * 0.7f + rtt * 0.3f;
            }
            if (r.p + 2 <= r.n) {
                g_hostFps = (float)r.u16();
            }
            break;
        }
        case T_INFO:
            logInfo(r, "host's");
            break;
        case T_IDLE:
            g_welcomed = true;
            if (g_inLevel) {
                guestLeaveLevel("the host is in the menu");
            }
            break;
        case T_BYE:
            g_welcomed = false;
            guestLeaveLevel("the host closed the game");
            guestModsForget();
            break;
    }
}

// ------------------------------------------------------------------ per frame
void NetFrame(double dt)
{
    // frame rate for the stats display (also offline)
    {
        const double t = now();
        g_frameCount++;
        if (t - g_statWinStart >= 2.0) {
            g_fps = (float)(g_frameCount / std::max(0.001, t - g_statWinStart));
            g_frameCount = 0;
            g_statWinStart = t;
            g_statWindowDone = true;
        }
    }
    if (g_role == R_NONE || g_sock == INVALID_SOCKET) {
        return;
    }
    uint8_t buf[2048];
    for (int k = 0; k < 4096; k++) {
        sockaddr_in from;
        socklen_t fl = sizeof(from);
        const int n = (int)recvfrom(g_sock, (char*)buf, sizeof(buf), 0, (sockaddr*)&from, &fl);
        if (n <= 0) {
            break;
        }
        if (g_role == R_HOST) {
            hostReceive(buf, n, from);
        } else if (g_testJitter > 0) {
            // test hook LUGARUEX_TEST_JITTER=ms: hold packets a random time, like a bad link
            g_delayed.push_back({ now() + 0.03 + (rand() % g_testJitter) / 1000.0, std::vector<uint8_t>(buf, buf + n), from });
        } else {
            guestReceive(buf, n, from);
        }
    }
    for (size_t i = 0; i < g_delayed.size();) {
        if (g_delayed[i].at <= now()) {
            Delayed d = g_delayed[i];
            g_delayed.erase(g_delayed.begin() + i);
            guestReceive(d.data.data(), (int)d.data.size(), d.from);
        } else {
            i++;
        }
    }
    const double t = now();
    const bool statWindow = g_statWindowDone;
    g_statWindowDone = false;
    const bool logNow = t - g_lastStatLog >= 10.0;
    if (logNow) {
        g_lastStatLog = t;
    }
    if (g_role == R_HOST) {
        for (auto& c : g_clients) {
            if (c.connected && !c.modsReady && t - c.lastModList > 1.0) {
                hostSendModList(c); // until the guest says it has them
            }
            if (c.connected && t - c.lastHeard > kTimeout) {
                c.connected = false; // their character stays and stands still, like an unplugged pad
                for (int s : c.slots) {
                    LocalRemoteInput(s, PadInput(), 0, 0, false);
                }
                printf("LugaruEX net: a guest stopped responding\n");
            }
            if (logNow && c.connected && hostInLevel()) {
                char ip[64];
                inet_ntop(AF_INET, &c.addr.sin_addr, ip, sizeof(ip));
                printf("LugaruEX net: guest %s: ping %d ms, snapshots lost %.1f%%, jitter %d ms, view %d ms behind, %d fps there, %ld inputs/s here\n",
                       ip, c.pingMs, c.lossPermille / 10.f, c.jitterMs, c.delayMs, c.fps, c.inputs / 10);
            }
            if (logNow) {
                c.inputs = 0;
            }
        }
        return;
    }
    if (statWindow) {
        // snapshots this window: the ones that arrived vs the numbers they span
        const double expected = g_snapSeen.empty() ? 0 : (double)(*g_snapSeen.rbegin() - *g_snapSeen.begin() + 1);
        g_lossPct = expected > 1 ? (float)std::max(0.0, 100.0 * (1.0 - g_snapSeen.size() / expected)) : 0.f;
        g_snapSeen.clear();
    }
    if (g_haveHost && g_welcomed && !g_infoSent) {
        g_infoSent = true; // tell the host what this PC runs on (it answers with its own)
        Writer w;
        writeInfo(w);
        sendTo(w, g_hostAddr);
    }
    if (g_haveHost && g_welcomed && t - g_lastPing >= 1.0) {
        g_lastPing = t;
        Writer w;
        w.head(T_PING);
        w.u32(SDL_GetTicks());
        w.u16(g_pingMs < 0 ? 0xffff : std::min(65000, (int)g_pingMs));
        w.u16((int)(g_lossPct * 10));
        w.u16((int)g_jitterMs);
        w.u16((int)g_delayMs);
        w.u16((int)g_fps);
        sendTo(w, g_hostAddr);
    }
    if (logNow && g_inLevel) {
        printf("LugaruEX net: ping %.0f ms, snapshots lost %.1f%%, jitter %.0f ms, view %.0f ms behind, %.0f fps\n",
               g_pingMs, g_lossPct, g_jitterMs, g_delayMs, g_fps);
    }

    // guest
    if (!g_haveHost) {
        g_status = g_lan ? "Looking for a host on your network..." : "Can't find the host - check the address in the launcher";
        if (g_lan && t - g_lastDiscover > 1) {
            g_lastDiscover = t;
            Writer w;
            w.head(T_DISCOVER);
            sockaddr_in b = {};
            b.sin_family = AF_INET;
            b.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            b.sin_port = htons((uint16_t)g_port);
            sendTo(w, b);
        }
        return;
    }
    if (g_welcomed && t - g_lastHeard > kTimeout) {
        g_welcomed = false;
        guestLeaveLevel("lost the host");
        guestModsForget();
    }
    // every player on this PC (split screen here) and how each looks; sent again
    // when a controller drops in, so the host adds that player too
    const int here = std::max(1, std::min(4, LocalPlayersHere()));
    static int announced = 0;
    if (!g_welcomed || here != announced) {
        if (!g_welcomed) {
            g_status = "Connecting to the host...";
        }
        if (t - g_lastHello > 0.5) {
            g_lastHello = t;
            Writer w;
            w.head(T_HELLO);
            w.u64(g_token);
            w.u8(here);
            for (int k = 0; k < here; k++) {
                w.str(LocalCharacterOf(k));
            }
            sendTo(w, g_hostAddr);
            if (g_welcomed) {
                announced = here;
            }
        }
        if (!g_welcomed) {
            return;
        }
    }
    g_status = g_inLevel ? "" : "Waiting for host...";
    guestModPump(t);
    g_inputAcc += dt;
    if (g_inputAcc >= 1.0 / kSendRate) {
        g_inputAcc = fmod(g_inputAcc, 1.0 / kSendRate);
        // test hook: LUGARUEX_TEST_GUESTRUN makes this guest's player 1 run, turn and attack on its own
        static const bool testRun = getenv("LUGARUEX_TEST_GUESTRUN") != nullptr;
        if (testRun && g_inLevel) {
            Game::yaw += 0.6f;
        }
        Writer w;
        w.head(T_INPUT);
        w.u32(g_gLevel);
        w.u32(g_gRoster);
        w.u8(here);
        for (int k = 0; k < here; k++) {
            PadInput in;
            float yaw, pitch;
            LocalInputOf(k, in, yaw, pitch);
            if (!g_inLevel) {
                in = PadInput();
            }
            if (k == 0 && testRun && g_inLevel) {
                static const bool testWalk = getenv("LUGARUEX_TEST_GUESTRUN") && strcmp(getenv("LUGARUEX_TEST_GUESTRUN"), "walk") == 0;
                in.fwd = true;
                in.walk = testWalk; // LUGARUEX_TEST_GUESTRUN=walk: walk instead
                in.attack = !testWalk && ((int)(now() * 2)) % 5 == 0;
            }
            const int btn = (in.fwd ? 1 : 0) | (in.back ? 2 : 0) | (in.left ? 4 : 0) | (in.right ? 8 : 0) | (in.jump ? 16 : 0) |
                            (in.crouch ? 32 : 0) | (in.attack ? 64 : 0) | (in.draw ? 128 : 0) | (in.thrw ? 256 : 0) | (in.walk ? 512 : 0);
            w.u16(btn);
            w.f32(yaw);
            w.f32(pitch);
        }
        sendTo(w, g_hostAddr);
    }
}

// ------------------------------------------------------------------ guest: show
static XYZ lerpv(XYZ a, XYZ b, float t)
{
    XYZ r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.z = a.z + (b.z - a.z) * t;
    return r;
}

static float lerpAngle(float a, float b, float t)
{
    float d = fmodf(b - a, 360.f);
    if (d > 180) {
        d -= 360;
    }
    if (d < -180) {
        d += 360;
    }
    return a + d * t;
}

// guest: the host's dialogue scene - same text box and scene camera as the host
// (the guest loaded the level's dialogues); its owner clicks on with attack, which
// reaches the host with the rest of their input
static void guestDialog()
{
    const bool was = Dialog::inDialog();
    static int lastWhich = -1, lastScene = -1;
    Dialog::directing = false;
    if (g_dlgWhich >= 0 && g_dlgWhich < (int)Dialog::dialogs.size() && g_dlgScene < (int)Dialog::dialogs[g_dlgWhich].scenes.size()) {
        Dialog::whichdialogue = g_dlgWhich;
        Dialog::indialogue = g_dlgScene;
    } else {
        if (g_dlgWhich >= 0 && lastWhich != g_dlgWhich) {
            printf("LugaruEX net: host shows dialogue %d scene %d, not in this PC's level\n", g_dlgWhich, g_dlgScene);
            lastWhich = g_dlgWhich;
        }
        Dialog::indialogue = -1;
    }
    if (Dialog::inDialog()) {
        if (!was || lastWhich != Dialog::whichdialogue || lastScene != Dialog::indialogue) {
            printf("LugaruEX net: dialogue %d scene %d (person %d clicks on%s)\n", Dialog::whichdialogue, Dialog::indialogue, g_dlgOwner,
                   g_dlgOwner == g_myPerson ? " - you" : "");
            lastWhich = Dialog::whichdialogue;
            lastScene = Dialog::indialogue;
            InterpInvalidate(); // the scene camera cuts
        }
        Game::cameramode = 1;
        pause_sound(whooshsound);
        viewer = Dialog::currentScene().camera;
        viewer.y = std::max((double)viewer.y, terrain.getHeight(viewer.x, viewer.z) + .1);
        Game::yaw = Dialog::currentScene().camerayaw;
        Game::pitch = Dialog::currentScene().camerapitch;
    } else if (was) {
        Game::cameramode = 0;
        lastWhich = lastScene = -1;
        InterpInvalidate();
    }
}

// guest: bushes sway when someone moves through them (Person::DoStuff does it on the
// host; the leaves and the rustle come from the host as effects and sounds)
static void exGuestBushes(Person& p)
{
    if ((p.aitype == passivetype && p.skeleton.free != 1) || findLengthfast(&p.velocity) <= .1f) {
        return;
    }
    for (auto& o : Object::objects) {
        if (o->type != bushtype) {
            continue;
        }
        const float s2 = o->scale * o->scale;
        if (distsqflat(&p.coords, &o->position) < s2 * 12 && distsq(&p.coords, &o->position) < s2 * 49) {
            o->rotx += p.velocity.x * multiplier * 6;
            o->roty += p.velocity.z * multiplier * 6;
        }
    }
}

void NetGuestStep()
{
    if (g_role != R_GUEST || !g_inLevel || !Game::gameon) {
        return;
    }
    Game::exGuestAmbient(); // hawk, falling snow: what the host's tick would do around you
    guestDialog();
    bonustotal = g_score;
    slomo = g_slomo;
    if (slomo) {
        slomodelay = 1; // the host decides when it ends
    }
    winfreeze = g_winfreeze != 0;
    // show the world a little in the past (sized to the network's jitter), blending between two snapshots
    const double rt = guestRenderTime();
    if (!g_hist.empty() && !g_hist[0].empty()) {
        g_stSteps++;
        g_stStarved += rt > g_hist[0].back().t;
        g_stDelaySum += g_hist[0].back().t - rt;
    }
    static bool fireLoop = false;
    bool anyOnFire = false;
    for (size_t i = 0; i < Person::players.size() && i < g_hist.size(); i++) {
        auto& h = g_hist[i];
        if (h.empty()) {
            continue;
        }
        const NetPerson* a = &h.front();
        const NetPerson* b = a;
        for (size_t k = 0; k < h.size(); k++) {
            if (h[k].t <= rt) {
                a = &h[k];
                b = (k + 1 < h.size()) ? &h[k + 1] : &h[k];
            }
        }
        const float f = (b->t > a->t) ? (float)std::max(0.0, std::min(1.0, (rt - a->t) / (b->t - a->t))) : 1.f;
        const NetPerson& s = (f < 0.5f) ? *a : *b; // discrete state from the nearer one
        Person& p = *Person::players[i];
        const bool wasFree = p.skeleton.free != 0;
        const XYZ stepStart = p.coords;
        // last step's position, as the host's simulation keeps it: the camera aims at it
        // during reversals and tackles (stale, it swung the camera off to an old spot)
        p.oldcoords = p.coords;
        p.realoldcoords = p.coords;
        p.coords = lerpv(a->coords, b->coords, f);
        if (a == b && h.size() >= 2 && rt > a->t && !a->free) {
            // a late snapshot: keep moving briefly instead of freezing
            const NetPerson& p0 = h[h.size() - 2];
            const double span = a->t - p0.t;
            if (span > 0.001 && span < 0.2) {
                const float k = (float)(std::min(rt - a->t, 0.1) / span);
                XYZ c1 = a->coords, c0 = p0.coords;
                XYZ d = c1 - c0;
                p.coords = c1 + d * k;
            }
        }
        p.yaw = lerpAngle(a->yaw, b->yaw, f);
        p.targetyaw = p.yaw;
        p.tilt = lerpAngle(a->tilt, b->tilt, f);
        p.tilt2 = lerpAngle(a->tilt2, b->tilt2, f);
        p.targetheadyaw = lerpAngle(a->headyaw, b->headyaw, f);
        p.targetheadpitch = lerpAngle(a->headpitch, b->headpitch, f);
        const int nAnims = (int)Animation::animations.size();
        if (s.animCur < nAnims && s.animTgt < nAnims) { // a newer or different game's animation: keep the last one
            p.animCurrent = s.animCur;
            p.animTarget = s.animTgt;
            // and frames this PC's copy of the animation has (a different Data folder could have fewer)
            p.frameCurrent = std::max(0, std::min((int)Animation::animations[p.animCurrent].frames.size() - 1, s.frameCur));
            p.frameTarget = std::max(0, std::min((int)Animation::animations[p.animTarget].frames.size() - 1, s.frameTgt));
        }
        p.target = (a->animCur == b->animCur && a->frameCur == b->frameCur && a->frameTgt == b->frameTgt) ? a->target + (b->target - a->target) * f : s.target;
        p.currentoffset = s.curoff;
        p.targetoffset = s.tgtoff;
        p.dead = s.dead;
        p.aitype = s.aitype;
        const bool wasOnFire = p.onfire;
        p.onfire = (s.flags & 1) != 0;
        p.headless = (s.flags & 2) != 0;
        p.onterrain = (s.flags & 4) != 0;
        p.stunned = (s.flags & 8) ? 1.f : 0.f;
        p.howactive = (s.flags >> 4) & 7;
        if (p.onfire && !wasOnFire) {
            p.exFireStart(); // catching fire: the burst of flames and the fire loop
        }
        anyOnFire = anyOnFire || p.onfire;
        p.exFireVisual(multiplier); // flames and burn marks (the host's DoStuff draws them there)
        p.damage = s.damage;
        p.bloodloss = s.bloodloss;
        p.permanentdamage = s.perm;
        p.superpermanentdamage = s.superperm;
        p.damagetolerance = std::max(1.f, s.tol);
        p.blooddimamount = s.dim;
        p.num_weapons = std::max(0, std::min(4, s.numw));
        p.weaponactive = s.wactive;
        p.weaponstuck = s.wstuck;
        p.weaponstuckwhere = s.wstuckwhere;
        for (int k = 0; k < 4; k++) {
            p.weaponids[k] = s.w[k];
        }
        const int prevFree = p.skeleton.free;
        p.skeleton.free = s.free;
        if (s.free) {
            if (s.hasJoints && s.joints.size() == p.skeleton.joints.size()) {
                const bool blend = a->hasJoints && b->hasJoints && a->joints.size() == b->joints.size() && a->joints.size() == s.joints.size();
                for (size_t j = 0; j < p.skeleton.joints.size(); j++) {
                    p.skeleton.joints[j].position = blend ? lerpv(a->joints[j], b->joints[j], f) : s.joints[j];
                }
            }
            if (s.free == 2 && prevFree != 2 && !p.skeleton.joints.empty()) {
                // a body comes to rest: build both detail levels and their lighting once at the
                // final pose, as the host does (a settled body is not rebuilt after that)
                p.skeleton.free = 3;
                p.DrawSkeleton();
                p.skeleton.free = 2;
            }
        } else {
            p.exNetPose(wasFree && (p.animCurrent == tempanim || p.animTarget == tempanim));
            // Attacks and reversals move a character with its position AND an animation
            // offset, and the host trades one for the other within a single step. Blending
            // the position while the offset switches all at once made the body (and the
            // camera on it) jump and slide back - so blend what is actually seen, the
            // position plus the offset, and work the position out from that.
            if (!a->free && !b->free) {
                auto seen = [&](const NetPerson& n) {
                    const float t = std::max(0.f, std::min(1.f, n.target));
                    XYZ c = n.coords, co = n.curoff, to = n.tgtoff;
                    XYZ o = co * (1 - t) + to * t;
                    return c + o * p.scale;
                };
                XYZ v = lerpv(seen(*a), seen(*b), f);
                if (a == b && h.size() >= 2 && rt > a->t && !h[h.size() - 2].free) {
                    const NetPerson& p0 = h[h.size() - 2];
                    const double span = a->t - p0.t;
                    if (span > 0.001 && span < 0.2) {
                        const float k = (float)(std::min(rt - a->t, 0.1) / span);
                        XYZ v1 = seen(*a), v0 = seen(p0);
                        XYZ d = v1 - v0;
                        v = v1 + d * k;
                    }
                }
                XYZ off = p.offset;
                p.coords = v - off * p.scale;
                // the camera aims at oldcoords + offset during reversals and tackles: with the
                // drawn position already smooth here, the current position is the steady aim
                // (last step's position plus this step's offset still jumped)
                p.oldcoords = p.coords;
            }
        }
        // the host's velocity isn't sent: work it out from the movement (the camera speeds up
        // in fast falls, speed blur, bushes shake when walked through); jumps (teleports,
        // level restarts) count as standing still
        {
            XYZ v;
            v = 0;
            if (multiplier > 0) {
                v = (p.coords - stepStart) / multiplier;
            }
            if (findLengthfast(&v) > 60 * 60) {
                v = 0;
            }
            p.velocity = p.velocity * .5f + v * .5f;
        }
        exGuestBushes(p);
        // faces, hands and tails: breathing, blinking, fists, yells, a hand gripping its
        // weapon, shut eyes when knocked out, open mouth when dead (DoStuff on the host)
        p.whichpatchx = std::max(0, std::min(subdivision - 1, (int)(p.coords.x / (terrain.size / subdivision * terrain.scale))));
        p.whichpatchz = std::max(0, std::min(subdivision - 1, (int)(p.coords.z / (terrain.size / subdivision * terrain.scale))));
        p.exMorphs();
        if (!p.skeleton.free) {
            p.exAnimMorphs();
        }
    }
    if (anyOnFire) {
        fireLoop = true;
    } else if (fireLoop) {
        fireLoop = false;
        pause_sound(stream_firesound); // nobody burns any more (rolled it out)
    }
    // smoothness of this PC's own character as drawn (test numbers, printed at exit)
    if (g_myPerson >= 0 && g_myPerson < (int)Person::players.size()) {
        Person& me = *Person::players[g_myPerson];
        XYZ off = me.offset;
        XYZ v = me.coords + (me.skeleton.free ? off * 0 : off * me.scale);
        static XYZ lastV, lastD;
        static bool haveV = false, haveD = false;
        if (haveV) {
            XYZ d = v - lastV;
            if (haveD && !me.skeleton.free) {
                XYZ dd = d - lastD;
                const float j = sqrtf(dd.x * dd.x + dd.y * dd.y + dd.z * dd.z);
                g_jerkMax = std::max(g_jerkMax, j);
                g_jerkBig += j > 0.05f;
            }
            lastD = d;
            haveD = true;
        }
        lastV = v;
        haveV = true;
    }
    for (size_t i = 0; i < weapons.size() && i < g_whist.size(); i++) {
        auto& h = g_whist[i];
        if (h.empty()) {
            continue;
        }
        const NetWeapon* a = &h.front();
        const NetWeapon* b = a;
        for (size_t k = 0; k < h.size(); k++) {
            if (h[k].t <= rt) {
                a = &h[k];
                b = (k + 1 < h.size()) ? &h[k + 1] : &h[k];
            }
        }
        const float f = (b->t > a->t) ? (float)std::max(0.0, std::min(1.0, (rt - a->t) / (b->t - a->t))) : 1.f;
        Weapon& w = weapons[i];
        const NetWeapon& s = (f < 0.5f) ? *a : *b;
        w.owner = (s.owner >= 0 && s.owner < (int)Person::players.size()) ? s.owner : -1;
        w.bloody = s.bloody;
        w.blooddrip = s.drip;
        w.exSetOnFire(s.onfire);
        if (w.owner == -1) {
            const bool blend = a->owner == -1 && b->owner == -1;
            const XYZ was = w.position;
            w.position = blend ? lerpv(a->pos, b->pos, f) : s.pos;
            float* r[8] = { &w.rotation1, &w.rotation2, &w.rotation3, &w.bigrotation, &w.bigtilt, &w.bigtilt2, &w.smallrotation, &w.smallrotation2 };
            for (int k = 0; k < 8; k++) {
                *r[k] = blend ? lerpAngle(a->rot[k], b->rot[k], f) : s.rot[k];
            }
            w.exGuestTip();
            // a thrown weapon in flight draws with its motion trail (Weapon::draw: velocity set,
            // not tumbling); dropped / bouncing ones fall far slower than a throw
            XYZ v;
            v = 0;
            if (blend && multiplier > 0) {
                v = (w.position - was) / multiplier;
            }
            const bool flying = v.x * v.x + v.y * v.y + v.z * v.z > 15 * 15;
            w.velocity = flying ? v : v * 0;
            w.physics = !flying;
        }
        w.exEffects(); // blood drips, flames, the glint of a weapon lying around
    }
}

bool NetStatsShown()
{
    return g_showStats;
}

void NetShowStats(bool on)
{
    g_showStats = on;
}

static void drawStats()
{
    std::vector<std::string> lines;
    char b[200];
    if (g_role == R_GUEST) {
        if (g_pingMs < 0) {
            lines.push_back("Ping: measuring...");
        } else {
            snprintf(b, sizeof(b), "Ping %.0f ms   Lost %.1f%%   Jitter %.0f ms", g_pingMs, g_lossPct, g_jitterMs);
            lines.push_back(b);
        }
        if (g_hostFps >= 0) {
            snprintf(b, sizeof(b), "View %.0f ms behind host   %.0f fps here   host %.0f fps", g_delayMs, g_fps, g_hostFps);
        } else {
            snprintf(b, sizeof(b), "View %.0f ms behind host   %.0f fps", g_delayMs, g_fps);
        }
        lines.push_back(b);
    } else if (g_role == R_NONE) {
        snprintf(b, sizeof(b), "%.0f fps", g_fps);
        lines.push_back(b);
    } else {
        snprintf(b, sizeof(b), "Hosting   %.0f fps", g_fps);
        lines.push_back(b);
        for (auto& c : g_clients) {
            if (!c.connected) {
                continue;
            }
            snprintf(b, sizeof(b), "Guest: ping %d ms   lost %.1f%%   jitter %d ms   %d fps", c.pingMs, c.lossPermille / 10.f, c.jitterMs, c.fps);
            lines.push_back(b);
        }
    }
    const float fullH = SharedHudHeight();
    const float size = 1.1f * fullH / 768.f;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    float y = 60 * fullH / 768.f; // above the lighting look's name
    for (int i = (int)lines.size() - 1; i >= 0; i--) {
        Game::text->glPrintOutlined(.8f, 1, .8f, 1, 20 * fullH / 768.f, y, lines[i], 1, size, screenwidth, screenheight);
        y += 22 * fullH / 768.f;
    }
    glPopAttrib();
}

void NetDrawOverlay()
{
    if (g_showStats && !mainmenu && DrawBottomLeftHud()) {
        drawStats();
    }
    if (!g_warn.empty() && now() < g_warnUntil && !mainmenu && DrawSharedHud()) {
        // this PC's copy of the level differs from the host's
        const float fullH = SharedHudHeight();
        const float size = 1.2f * fullH / 768.f;
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_CULL_FACE);
        glDisable(GL_FOG);
        const float x = screenwidth / 2 - g_warn.size() * 10.f * size / 2;
        Game::text->glPrintOutlined(1, .8f, .3f, 1, x, screenheight * 0.7f, g_warn, 1, size, screenwidth, screenheight);
        glPopAttrib();
    }
    if (g_role != R_GUEST || g_status.empty() || !mainmenu) {
        return;
    }
    const float size = 1.6f * screenheight / 768.f;
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    const float pulse = 0.7f + 0.3f * sinf(SDL_GetTicks() / 400.f);
    const float x = screenwidth / 2 - g_status.size() * 10.f * size / 2;
    Game::text->glPrintOutlined(1, .85f, .7f, pulse, x, screenheight * 0.12f, g_status, 1, size, screenwidth, screenheight);
    glPopAttrib();
}
}
