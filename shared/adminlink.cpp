// LugaruEX - the launchers' link to the game they started (GPLv2+)
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
#endif

#include "adminlink.h"
#include "netinfo.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>

static const uint32_t kAdminMagic = 0x4158474C; // "LGXA"

typedef decltype(socket(0, 0, 0)) Sock;

AdminLink::~AdminLink()
{
    if (m_sock != -1) {
        closesocket((Sock)m_sock);
    }
}

bool AdminLink::Open()
{
    if (m_sock != -1) {
        return true;
    }
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false;
    }
#endif
    Sock s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) {
        return false;
    }
#else
    if (s < 0) {
        return false;
    }
#endif
    sockaddr_in me = {};
    me.sin_family = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(me);
    if (bind(s, (const sockaddr*)&me, sizeof(me)) != 0 || getsockname(s, (sockaddr*)&me, &len) != 0) {
        closesocket(s);
        return false;
    }
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    m_sock = (long long)s;
    m_port = ntohs(me.sin_port);
    return true;
}

std::string AdminLink::NewSecret()
{
    std::random_device rd; // (the system's cryptographic generator)
    char hex[40] = {};
    for (int k = 0; k < 16; k++) {
        m_token[k] = (uint8_t)(rd() & 0xFF);
        snprintf(hex + 2 * k, 3, "%02x", m_token[k]);
    }
    st = AdminStatus();
    m_gamePort = 0;
    return std::to_string(m_port) + ":" + hex;
}

bool AdminLink::Poll()
{
    if (m_sock == -1) {
        return false;
    }
    bool got = false;
    for (int k = 0; k < 16; k++) {
        uint8_t d[2048];
        sockaddr_in from;
        socklen_t fl = sizeof(from);
        const int n = (int)recvfrom((Sock)m_sock, (char*)d, sizeof(d), 0, (sockaddr*)&from, &fl);
        if (n <= 0) {
            break;
        }
        if (from.sin_addr.s_addr != htonl(INADDR_LOOPBACK) || n < 22 || memcmp(d + 4, m_token, 16) != 0) {
            continue;
        }
        int p = 0;
        bool ok = true;
        auto u8 = [&]() -> int {
            if (p >= n) {
                ok = false;
                return 0;
            }
            return d[p++];
        };
        auto u32 = [&]() -> uint32_t {
            uint32_t v = 0;
            for (int i = 0; i < 4; i++) {
                v |= (uint32_t)u8() << (8 * i);
            }
            return v;
        };
        auto str = [&]() -> std::string {
            const int l = u8();
            if (!ok || p + l > n) {
                ok = false;
                return std::string();
            }
            std::string s((const char*)d + p, l);
            p += l;
            return s;
        };
        if (u32() != kAdminMagic) {
            continue;
        }
        p = 20;
        if (u8() != 'S') {
            continue;
        }
        AdminStatus s;
        s.role = u8();
        s.steam = u8() != 0;
        s.code = netinfo::CleanText(str(), 40, false);
        s.needPassword = u8() != 0;
        s.status = netinfo::CleanText(str(), 160, false);
        const int count = u8();
        for (int i = 0; i < count && ok; i++) {
            AdminPlayer a;
            a.id = u32();
            a.name = netinfo::CleanName(str());
            a.inGame = u8() != 0;
            a.seats = u8();
            s.players.push_back(a);
        }
        s.sessionId = u32();
        if (!ok) {
            continue;
        }
        s.heard = true;
        m_gamePort = ntohs(from.sin_port);
        st = s;
        got = true;
    }
    return got;
}

void AdminLink::send(const std::vector<uint8_t>& body)
{
    if (m_sock == -1 || !m_gamePort) {
        return;
    }
    std::vector<uint8_t> b;
    for (int k = 0; k < 4; k++) {
        b.push_back((uint8_t)(kAdminMagic >> (8 * k)));
    }
    b.insert(b.end(), m_token, m_token + 16);
    b.insert(b.end(), body.begin(), body.end());
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    to.sin_port = htons((uint16_t)m_gamePort);
    sendto((Sock)m_sock, (const char*)b.data(), (int)b.size(), 0, (const sockaddr*)&to, sizeof(to));
}

void AdminLink::Kick(uint32_t id, bool ban)
{
    std::vector<uint8_t> b;
    b.push_back(ban ? 'B' : 'K');
    for (int k = 0; k < 4; k++) {
        b.push_back((uint8_t)(id >> (8 * k)));
    }
    send(b);
}

void AdminLink::Password(const std::string& pw)
{
    std::vector<uint8_t> b;
    b.push_back('P');
    const size_t l = std::min<size_t>(255, pw.size());
    b.push_back((uint8_t)l);
    b.insert(b.end(), pw.begin(), pw.begin() + l);
    send(b);
}
