// lugaruex-steam: Steam lobbies and Valve's relay network for LugaruEX.
// MIT licence (steam/LICENSE.txt). A separate program from the GPL game: the game starts
// it and they only exchange UDP packets on 127.0.0.1.
//
//   lugaruex-steam host <gamePort> <listed 0|1>   make a lobby, carry guests' packets to the game
//   lugaruex-steam join <code> <gamePort>         find the lobby's host, carry the game's packets to it
//   lugaruex-steam list                           print the listed lobbies (for the launcher), then exit
//
// Everything goes through Valve's relays (ICE off): no player ever learns another's IP
// address. The game's own protocol is unchanged: each Steam guest looks to the host game
// like a player at 127.0.0.1:<a port of this helper>.
//
// Control packets between the game and this helper start with "LGXS" + one type byte:
//   helper -> game: 'H' hello (from the control socket), 'C' lobby code, 'E' error text,
//                   'A' (join: sent from the socket the game should treat as the host)
//   game -> helper: 'K' keep alive (every second; quiet 10 s = the game is gone), 'Q' quit
#include <cstdlib>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
static void setEnv(const char* k, const char* v) { _putenv_s(k, v); }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define closesocket close
static void setEnv(const char* k, const char* v) { setenv(k, v, 1); }
#endif

#include "steam/steam_api.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
const uint32_t kCtlMagic = 0x5358474C;  // "LGXS"
const uint32_t kGameMagic = 0x3958474C; // "LGX9" (the game's protocol; only for asking it who it is)
const char* const kProto = "9";         // lobbies of this protocol only

double now()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ---- lobby codes: the lobby's 64-bit id in Crockford base32, "XXXX-XXXX-XXXXX"
const char* const kB32 = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
std::string encodeCode(uint64_t v)
{
    char c[13];
    for (int i = 12; i >= 0; i--) {
        c[i] = kB32[v & 31];
        v >>= 5;
    }
    std::string s(c, 13);
    return s.substr(0, 4) + "-" + s.substr(4, 4) + "-" + s.substr(8);
}
bool decodeCode(const std::string& in, uint64_t& out)
{
    std::string s;
    for (char ch : in) {
        if (ch == '-' || ch == ' ') {
            continue;
        }
        s += (char)toupper((unsigned char)ch);
    }
    if (!s.empty() && s.find_first_not_of("0123456789") == std::string::npos && s.size() >= 15) {
        out = strtoull(s.c_str(), nullptr, 10); // a lobby id pasted as a plain number
        return out != 0;
    }
    if (s.size() != 13) {
        return false;
    }
    uint64_t v = 0;
    for (char ch : s) {
        if (ch == 'O') {
            ch = '0';
        }
        if (ch == 'I' || ch == 'L') {
            ch = '1';
        }
        const char* p = strchr(kB32, ch);
        if (!p || !*p) {
            return false;
        }
        v = (v << 5) | (uint64_t)(p - kB32);
    }
    out = v;
    return v != 0;
}

std::string hex(const uint8_t* d, int n)
{
    static const char* h = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < n; i++) {
        s += h[d[i] >> 4];
        s += h[d[i] & 15];
    }
    return s;
}

SOCKET loopbackSocket()
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        return s;
    }
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // this PC only
    a.sin_port = 0;
    if (bind(s, (sockaddr*)&a, sizeof(a)) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    int buf = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&buf, sizeof(buf));
    return s;
}

class Helper
{
public:
    enum Mode
    {
        HOST,
        JOIN,
        LIST
    } mode = HOST;
    sockaddr_in game = {};
    bool listed = true;
    bool done = false;
    int exitCode = 0;

    struct Peer
    {
        CSteamID id;
        SOCKET s = INVALID_SOCKET;
        double heard = 0;
    };
    std::vector<Peer> peers;
    SOCKET ctl = INVALID_SOCKET;
    CSteamID lobby, host;
    double lastKeep = 0, lastInfoAsk = -10;
    std::string lastInfo;

    CCallResult<Helper, LobbyCreated_t> crCreate;
    CCallResult<Helper, LobbyEnter_t> crEnter;
    CCallResult<Helper, LobbyMatchList_t> crList;
    STEAM_CALLBACK(Helper, onSession, SteamNetworkingMessagesSessionRequest_t);

    void ctlSend(char type, const std::string& text, SOCKET from = INVALID_SOCKET)
    {
        std::string p(4, '\0');
        for (int k = 0; k < 4; k++) {
            p[k] = (char)(kCtlMagic >> (8 * k));
        }
        p += type;
        p += text;
        sendto(from == INVALID_SOCKET ? ctl : from, p.data(), (int)p.size(), 0, (sockaddr*)&game, sizeof(game));
    }
    void fail(const std::string& why)
    {
        if (mode == LIST) {
            printf("ERR %s\n", why.c_str());
            fflush(stdout);
        } else {
            ctlSend('E', why);
        }
        exitCode = 1;
        done = true;
    }

    Peer* peerOf(const CSteamID& id, bool make)
    {
        for (Peer& p : peers) {
            if (p.id == id) {
                return &p;
            }
        }
        if (!make || peers.size() >= 64) {
            return nullptr;
        }
        Peer p;
        p.id = id;
        p.s = loopbackSocket();
        if (p.s == INVALID_SOCKET) {
            return nullptr;
        }
        p.heard = now();
        peers.push_back(p);
        if (mode == HOST) {
            // tell the game which Steam account this port carries (only for its ban list)
            sockaddr_in a = {};
            socklen_t l = sizeof(a);
            getsockname(p.s, (sockaddr*)&a, &l);
            ctlSend('P', std::to_string(ntohs(a.sin_port)) + " " + std::to_string((unsigned long long)id.ConvertToUint64()));
        }
        return &peers.back();
    }

    // ---- host
    void startHost()
    {
        SteamAPICall_t c = SteamMatchmaking()->CreateLobby(listed ? k_ELobbyTypePublic : k_ELobbyTypeInvisible, 250);
        crCreate.Set(c, this, &Helper::onCreated);
    }
    void onCreated(LobbyCreated_t* r, bool ioFailure)
    {
        if (ioFailure || r->m_eResult != k_EResultOK) {
            fail("Steam couldn't make a lobby (try again in a moment)");
            return;
        }
        lobby = CSteamID(r->m_ulSteamIDLobby);
        SteamMatchmaking()->SetLobbyData(lobby, "lgx", kProto);
        SteamMatchmaking()->SetLobbyJoinable(lobby, true);
        ctlSend('C', encodeCode(lobby.ConvertToUint64()));
        lastInfoAsk = -10;
    }
    // ask the game what it is (name, players...) for the lobby list
    void askInfo()
    {
        uint8_t q[64] = {};
        for (int k = 0; k < 4; k++) {
            q[k] = (uint8_t)(kGameMagic >> (8 * k));
        }
        q[4] = 1; // DISCOVER
        sendto(ctl, (const char*)q, sizeof(q), 0, (sockaddr*)&game, sizeof(game));
    }
    void gotInfo(const uint8_t* d, int n)
    {
        if (!lobby.IsValid()) {
            return;
        }
        const std::string h = hex(d, n);
        if (h != lastInfo) {
            lastInfo = h;
            SteamMatchmaking()->SetLobbyData(lobby, "info", h.c_str());
        }
        SteamNetworkPingLocation_t loc;
        if (SteamNetworkingUtils()->GetLocalPingLocation(loc) >= 0) {
            char buf[k_cchMaxSteamNetworkingPingLocationString];
            SteamNetworkingUtils()->ConvertPingLocationToString(loc, buf, sizeof(buf));
            SteamMatchmaking()->SetLobbyData(lobby, "loc", buf);
        }
    }

    // ---- join
    void startJoin(uint64_t code)
    {
        SteamAPICall_t c = SteamMatchmaking()->JoinLobby(CSteamID((uint64)code)); // (Steam's own 64-bit type: unsigned long long everywhere)
        crEnter.Set(c, this, &Helper::onEntered);
    }
    void onEntered(LobbyEnter_t* r, bool ioFailure)
    {
        if (ioFailure || r->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
            fail("No game with that code - it closed, or the code is mistyped");
            return;
        }
        lobby = CSteamID(r->m_ulSteamIDLobby);
        const char* proto = SteamMatchmaking()->GetLobbyData(lobby, "lgx");
        host = SteamMatchmaking()->GetLobbyOwner(lobby);
        // (out again straight away: other guests never see this player in the lobby)
        SteamMatchmaking()->LeaveLobby(lobby);
        if (!proto || strcmp(proto, kProto) != 0) {
            fail("That code is for another version of LugaruEX (or another game)");
            return;
        }
        if (!host.IsValid()) {
            fail("Steam didn't say who hosts that game - try again");
            return;
        }
        Peer* p = peerOf(host, true);
        if (!p) {
            fail("can't open a local port");
            return;
        }
        ctlSend('A', "", p->s); // the game sends to this socket from now on
    }

    // ---- list
    void startList()
    {
        SteamMatchmaking()->AddRequestLobbyListStringFilter("lgx", kProto, k_ELobbyComparisonEqual);
        SteamMatchmaking()->AddRequestLobbyListDistanceFilter(k_ELobbyDistanceFilterWorldwide);
        SteamMatchmaking()->AddRequestLobbyListResultCountFilter(200);
        crList.Set(SteamMatchmaking()->RequestLobbyList(), this, &Helper::onList);
    }
    void onList(LobbyMatchList_t* r, bool ioFailure)
    {
        if (ioFailure) {
            fail("Steam didn't answer");
            return;
        }
        // ping estimates need this PC's place on Valve's network (a few seconds at most)
        SteamNetworkPingLocation_t mine;
        for (int i = 0; i < 40 && SteamNetworkingUtils()->GetLocalPingLocation(mine) < 0; i++) {
            SteamAPI_RunCallbacks();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        for (uint32 i = 0; i < r->m_nLobbiesMatching; i++) {
            const CSteamID l = SteamMatchmaking()->GetLobbyByIndex((int)i);
            const char* info = SteamMatchmaking()->GetLobbyData(l, "info");
            const char* loc = SteamMatchmaking()->GetLobbyData(l, "loc");
            if (!info || !*info || strlen(info) > 4000 || strspn(info, "0123456789abcdef") != strlen(info)) {
                continue; // (not set up yet, or not ours)
            }
            int ping = -1;
            SteamNetworkPingLocation_t there;
            if (loc && *loc && SteamNetworkingUtils()->ParsePingLocationString(loc, there)) {
                ping = SteamNetworkingUtils()->EstimatePingTimeFromLocalHost(there);
            }
            printf("L %s %d %s\n", encodeCode(l.ConvertToUint64()).c_str(), ping, info);
        }
        printf("END\n");
        fflush(stdout);
        done = true;
    }

    // ---- packets
    void pump()
    {
        // from Steam: to the game, from that player's own local port
        SteamNetworkingMessage_t* msgs[64];
        for (int round = 0; round < 16; round++) {
            const int n = SteamNetworkingMessages()->ReceiveMessagesOnChannel(0, msgs, 64);
            if (n <= 0) {
                break;
            }
            for (int i = 0; i < n; i++) {
                const CSteamID from = msgs[i]->m_identityPeer.GetSteamID();
                const bool allowed = mode == HOST || from == host; // (a guest only hears its host)
                Peer* p = allowed ? peerOf(from, mode == HOST) : nullptr;
                if (p && msgs[i]->m_cbSize > 0 && msgs[i]->m_cbSize <= 2048) {
                    p->heard = now();
                    sendto(p->s, (const char*)msgs[i]->m_pData, (int)msgs[i]->m_cbSize, 0, (sockaddr*)&game, sizeof(game));
                }
                msgs[i]->Release();
            }
        }
        // from the game: to the player whose port it was sent to
        char buf[2048];
        for (Peer& p : peers) {
            for (int k = 0; k < 256; k++) {
                sockaddr_in from;
                socklen_t fl = sizeof(from);
                const int n = (int)recvfrom(p.s, buf, sizeof(buf), 0, (sockaddr*)&from, &fl);
                if (n <= 0) {
                    break;
                }
                if (from.sin_addr.s_addr != htonl(INADDR_LOOPBACK) || from.sin_port != game.sin_port) {
                    continue; // only the game on this PC
                }
                SteamNetworkingIdentity to;
                to.SetSteamID(p.id);
                SteamNetworkingMessages()->SendMessageToUser(to, buf, (uint32)n,
                                                             k_nSteamNetworkingSend_Unreliable | k_nSteamNetworkingSend_NoNagle |
                                                                 k_nSteamNetworkingSend_AutoRestartBrokenSession,
                                                             0);
            }
        }
        // the control socket: keep-alives, quit, and the game's answer about itself
        for (int k = 0; k < 64; k++) {
            sockaddr_in from;
            socklen_t fl = sizeof(from);
            const int n = (int)recvfrom(ctl, buf, sizeof(buf), 0, (sockaddr*)&from, &fl);
            if (n <= 0) {
                break;
            }
            if (from.sin_addr.s_addr != htonl(INADDR_LOOPBACK) || from.sin_port != game.sin_port || n < 5) {
                continue;
            }
            uint32_t magic = 0;
            for (int i = 0; i < 4; i++) {
                magic |= (uint32_t)(uint8_t)buf[i] << (8 * i);
            }
            if (magic == kCtlMagic && buf[4] == 'K') {
                lastKeep = now();
            } else if (magic == kCtlMagic && buf[4] == 'Q') {
                done = true;
            } else if (magic == kGameMagic && buf[4] == 2) { // HERE: the game's own description
                gotInfo((const uint8_t*)buf + 5, n - 5);
            }
        }
    }
};

void Helper::onSession(SteamNetworkingMessagesSessionRequest_t* r)
{
    // host: anyone who has the code (or found the lobby) may talk; the game itself checks
    // passwords, versions and who may play. A guest only takes its host.
    const CSteamID from = r->m_identityRemote.GetSteamID();
    if (mode == HOST || from == host) {
        SteamNetworkingMessages()->AcceptSessionWithUser(r->m_identityRemote);
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: lugaruex-steam host <gamePort> <listed 0|1> | join <code> <gamePort> | list\n");
        return 2;
    }
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    const std::string mode = argv[1];
    // Valve's public test app: anyone with Steam can use it (the game isn't a Steam app)
    setEnv("SteamAppId", "480");
    setEnv("SteamGameId", "480");

    int gamePort = 0;
    uint64_t code = 0;
    bool listed = true;
    if (mode == "host" && argc >= 3) {
        gamePort = atoi(argv[2]);
        listed = argc < 4 || atoi(argv[3]) != 0;
    } else if (mode == "join" && argc >= 4) {
        decodeCode(argv[2], code); // (0 = not a code: said below)
        gamePort = atoi(argv[3]);
    } else if (mode != "list") {
        fprintf(stderr, "lugaruex-steam: unknown mode\n");
        return 2;
    }
    if ((mode == "host" || mode == "join") && (gamePort <= 0 || gamePort > 65535)) {
        fprintf(stderr, "lugaruex-steam: bad game port\n");
        return 2;
    }

    SOCKET ctl = loopbackSocket();
    sockaddr_in game = {};
    game.sin_family = AF_INET;
    game.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    game.sin_port = htons((uint16_t)gamePort);
    auto sayError = [&](const std::string& why) {
        if (mode == "list") {
            printf("ERR %s\n", why.c_str());
            fflush(stdout);
            return;
        }
        std::string p(4, '\0');
        for (int k = 0; k < 4; k++) {
            p[k] = (char)(kCtlMagic >> (8 * k));
        }
        p += 'E';
        p += why;
        sendto(ctl, p.data(), (int)p.size(), 0, (sockaddr*)&game, sizeof(game));
    };
    if (mode == "join" && code == 0) {
        sayError("That code doesn't look right - it is like ABCD-EFGH-JKLMN");
        return 1;
    }

    SteamErrMsg err = {};
    if (SteamAPI_InitEx(&err) != k_ESteamAPIInitResult_OK) {
        sayError("Steam isn't running - start Steam and log in (or tick Direct connection under Advanced)");
        fprintf(stderr, "lugaruex-steam: %s\n", err);
        return 1;
    }
    // never share IP addresses with other players: everything through Valve's relays
    SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable,
                                                      k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Disable);
    SteamNetworkingUtils()->InitRelayNetworkAccess();

    {
        Helper h;
        h.ctl = ctl;
        h.game = game;
        h.listed = listed;
        h.lastKeep = now();
        if (mode == "host") {
            h.mode = Helper::HOST;
            h.ctlSend('H', "");
            h.startHost();
        } else if (mode == "join") {
            h.mode = Helper::JOIN;
            h.ctlSend('H', "");
            h.startJoin(code);
        } else {
            h.mode = Helper::LIST;
            h.startList();
        }
        const double started = now();
        while (!h.done) {
            SteamAPI_RunCallbacks();
            if (h.mode == Helper::LIST) {
                if (now() - started > 20) {
                    h.fail("Steam didn't answer in time");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            h.pump();
            if (now() - h.lastKeep > 10) {
                h.done = true; // the game is gone
            }
            if (h.mode == Helper::HOST && now() - h.lastInfoAsk > 3) {
                h.lastInfoAsk = now();
                h.askInfo();
            }
            // wait for traffic (at most a millisecond: packets go on quickly)
            fd_set rs;
            FD_ZERO(&rs);
            SOCKET top = h.ctl;
            FD_SET(h.ctl, &rs);
            for (auto& p : h.peers) {
                FD_SET(p.s, &rs);
                top = p.s > top ? p.s : top;
            }
            timeval tv = { 0, 1000 };
            select((int)top + 1, &rs, nullptr, nullptr, &tv);
        }
        if (h.lobby.IsValid() && h.mode == Helper::HOST) {
            SteamMatchmaking()->LeaveLobby(h.lobby); // (the lobby closes with its last member)
        }
        for (auto& p : h.peers) {
            closesocket(p.s);
        }
        const int rc = h.exitCode;
        SteamAPI_RunCallbacks();
        SteamAPI_Shutdown();
        closesocket(ctl);
        return rc;
    }
}
