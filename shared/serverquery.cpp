// LugaruEX - server list for the launchers (GPLv2+)
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
static unsigned ticks() { return GetTickCount(); }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#define closesocket close
static unsigned ticks()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
#endif

#include "serverquery.h"
#include "lanaddr.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace
{
std::string addrText(const sockaddr_in& a)
{
    char ip[64];
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
}

bool resolve(const std::string& text, int defPort, sockaddr_in& out)
{
    std::string a = text;
    int port = defPort;
    const size_t c = a.rfind(':');
    if (c != std::string::npos) {
        port = atoi(a.substr(c + 1).c_str());
        a = a.substr(0, c);
    }
    if (a.empty() || port <= 0 || port > 65535) {
        return false;
    }
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(a.c_str(), nullptr, &hints, &res) != 0 || !res) {
        return false;
    }
    out = *(sockaddr_in*)res->ai_addr;
    out.sin_port = htons((uint16_t)port);
    freeaddrinfo(res);
    return true;
}
} // namespace

ServerQuery::~ServerQuery()
{
    if (m_sock != -1) {
        closesocket((decltype(socket(0, 0, 0)))m_sock);
    }
}

bool ServerQuery::open()
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
    auto s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) {
        return false;
    }
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
#else
    if (s < 0) {
        return false;
    }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(s, (const sockaddr*)&a, sizeof(a));
    m_sock = (long long)s;
    return true;
}

void ServerQuery::Refresh(const std::vector<std::string>& addrs, int port)
{
    if (!open()) {
        return;
    }
    auto s = (decltype(socket(0, 0, 0)))m_sock;
    uint8_t q[netinfo::kQueryLen] = {};
    for (int k = 0; k < 4; k++) {
        q[k] = (uint8_t)(netinfo::kMagic >> (8 * k));
    }
    q[4] = netinfo::kDiscover;
    m_sentAt = ticks();
    m_asked.clear();
    // every network this PC is on: the LAN and virtual ones (Radmin, Hamachi, ZeroTier)
    for (uint32_t bc : LanBroadcasts()) {
        sockaddr_in b = {};
        b.sin_family = AF_INET;
        b.sin_addr.s_addr = bc;
        b.sin_port = htons((uint16_t)port);
        sendto(s, (const char*)q, sizeof(q), 0, (const sockaddr*)&b, sizeof(b));
    }
    for (const std::string& t : addrs) {
        sockaddr_in a;
        if (!resolve(t, port, a)) {
            continue;
        }
        const std::string key = addrText(a);
        if (std::find_if(m_asked.begin(), m_asked.end(), [&](const std::pair<std::string, unsigned>& p) { return p.first == key; }) != m_asked.end()) {
            continue;
        }
        m_asked.push_back({ key, ticks() });
        sendto(s, (const char*)q, sizeof(q), 0, (const sockaddr*)&a, sizeof(a));
        if (std::none_of(m_entries.begin(), m_entries.end(), [&](const ServerEntry& e) { return e.addr == key; })) {
            ServerEntry e;
            e.addr = key;
            e.info.name = t; // until it answers
            m_entries.push_back(e);
        }
    }
}

void ServerQuery::Poll()
{
    if (m_sock == -1) {
        return;
    }
    auto s = (decltype(socket(0, 0, 0)))m_sock;
    uint8_t buf[1500];
    for (int k = 0; k < 256; k++) {
        sockaddr_in from;
        socklen_t fl = sizeof(from);
        const int n = (int)recvfrom(s, (char*)buf, sizeof(buf), 0, (sockaddr*)&from, &fl);
        if (n <= 0) {
            break;
        }
        uint32_t magic = 0;
        for (int i = 0; i < 4 && i < n; i++) {
            magic |= (uint32_t)buf[i] << (8 * i);
        }
        if (n < 6 || magic != netinfo::kMagic || buf[4] != netinfo::kHere) {
            continue;
        }
        netinfo::ServerInfo info;
        if (!netinfo::DecodeInfo(buf + 5, n - 5, info)) {
            continue;
        }
        const std::string key = addrText(from);
        auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const ServerEntry& e) { return e.addr == key; });
        // the same server answering at another address (its LAN address and 127.0.0.1, say):
        // one entry, under the address that was asked by name
        auto same = std::find_if(m_entries.begin(), m_entries.end(),
                                 [&](const ServerEntry& e) { return e.answered && info.id != 0 && e.info.id == info.id && e.addr != key; });
        if (same != m_entries.end()) {
            const bool keyAsked = std::any_of(m_asked.begin(), m_asked.end(), [&](const std::pair<std::string, unsigned>& a) { return a.first == key; });
            if (!keyAsked || !same->lan) {
                if (it != m_entries.end() && it != same) {
                    m_entries.erase(it); // (an unanswered placeholder for this address)
                }
                continue;
            }
            m_entries.erase(same); // the LAN copy goes, the asked address stays
            it = std::find_if(m_entries.begin(), m_entries.end(), [&](const ServerEntry& e) { return e.addr == key; });
        }
        if (it == m_entries.end()) {
            m_entries.push_back(ServerEntry());
            it = m_entries.end() - 1;
            it->addr = key;
            it->lan = true;
        }
        unsigned sent = m_sentAt;
        for (auto& a : m_asked) {
            if (a.first == key) {
                sent = a.second;
            }
        }
        it->info = info;
        it->answered = true;
        it->lastAnswer = ticks();
        it->pingMs = (int)(ticks() - sent);
    }
    // a server that stopped answering (closed, or gone from the LAN) shows as such
    for (ServerEntry& e : m_entries) {
        if (e.answered && ticks() - e.lastAnswer > 25000) {
            e.answered = false;
        }
    }
}

bool SteamList_Run(const std::string& helperPath, std::vector<ServerEntry>& out, std::string& error)
{
    out.clear();
    std::string text;
#ifdef _WIN32
    const int wn = MultiByteToWideChar(CP_UTF8, 0, helperPath.c_str(), -1, NULL, 0);
    std::wstring wpath(wn > 0 ? wn - 1 : 0, 0);
    if (wn > 1) {
        MultiByteToWideChar(CP_UTF8, 0, helperPath.c_str(), -1, &wpath[0], wn);
    }
    if (GetFileAttributesW(wpath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = "Steam play isn't in this build";
        return false;
    }
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        error = "can't ask Steam";
        return false;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = NULL; // (Steam's library writes the player's Steam ID there: not kept)
    std::wstring cmd = L"\"" + wpath + L"\" list";
    std::vector<wchar_t> cl(cmd.begin(), cmd.end());
    cl.push_back(0);
    PROCESS_INFORMATION pi = {};
    const BOOL ok = CreateProcessW(NULL, cl.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        error = "can't start the Steam helper";
        return false;
    }
    char buf[4096];
    DWORD got;
    while (ReadFile(rd, buf, sizeof(buf), &got, NULL) && got > 0 && text.size() < 4u << 20) {
        text.append(buf, got);
    }
    WaitForSingleObject(pi.hProcess, 30000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
#else
    if (helperPath.find('\'') != std::string::npos || access(helperPath.c_str(), X_OK) != 0) {
        error = "Steam play isn't in this build";
        return false;
    }
    FILE* p = popen(("'" + helperPath + "' list 2>/dev/null").c_str(), "r");
    if (!p) {
        error = "can't start the Steam helper";
        return false;
    }
    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), p)) > 0 && text.size() < 4u << 20) {
        text.append(buf, got);
    }
    pclose(p);
#endif
    // "L <code> <ping> <hex info>" per lobby, then "END"; "ERR <why>" when Steam can't
    bool ended = false;
    size_t a = 0;
    while (a < text.size()) {
        size_t b = text.find('\n', a);
        if (b == std::string::npos) {
            b = text.size();
        }
        std::string l = text.substr(a, b - a);
        a = b + 1;
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) {
            l.pop_back();
        }
        if (l.compare(0, 4, "ERR ") == 0) {
            error = netinfo::CleanText(l.substr(4), 160, false);
            return false;
        }
        if (l == "END") {
            ended = true;
            continue;
        }
        if (l.compare(0, 2, "L ") != 0 || out.size() >= 200) {
            continue;
        }
        const size_t s1 = l.find(' ', 2), s2 = s1 == std::string::npos ? s1 : l.find(' ', s1 + 1);
        if (s2 == std::string::npos) {
            continue;
        }
        const std::string code = l.substr(2, s1 - 2), hexInfo = l.substr(s2 + 1);
        std::vector<uint8_t> bytes;
        for (size_t i = 0; i + 1 < hexInfo.size() && bytes.size() < 2000; i += 2) {
            bytes.push_back((uint8_t)strtoul(hexInfo.substr(i, 2).c_str(), nullptr, 16));
        }
        ServerEntry e;
        e.steam = true;
        e.addr = netinfo::CleanText(code, 20, false);
        e.pingMs = atoi(l.substr(s1 + 1, s2 - s1 - 1).c_str());
        if (!e.addr.empty() && netinfo::DecodeInfo(bytes.data(), (int)bytes.size(), e.info)) {
            e.answered = true;
            out.push_back(e);
        }
    }
    if (!ended) {
        error = "Steam didn't answer";
        return false;
    }
    return true;
}

bool ServerQuery::Busy() const
{
    return m_sentAt != 0 && ticks() - m_sentAt < 1500;
}
