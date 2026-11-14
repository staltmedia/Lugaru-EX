// LugaruEX - server list for the launchers: asks hosts on the LAN (broadcast) and at
// known addresses what they are, without joining. (GPLv2+)
#pragma once
#include "netinfo.h"

#include <string>
#include <vector>

struct ServerEntry
{
    std::string addr; // "ip:port", or a Steam lobby code
    bool steam = false; // a Steam lobby (addr = its code)
    netinfo::ServerInfo info;
    int pingMs = -1;
    bool lan = false;      // found by the LAN search
    bool answered = false; // answered recently (false: asked, no answer, or quiet for a while)
    unsigned lastAnswer = 0;
};

// Steam lobbies (through the lugaruex-steam helper, Valve's network: no addresses).
// Blocking (a few seconds): run it on a thread. false + error when Steam can't be asked.
bool SteamList_Run(const std::string& helperPath, std::vector<ServerEntry>& out, std::string& error);

class ServerQuery
{
public:
    ~ServerQuery();
    // ask again: the LAN (broadcast to port) and every address given ("host[:port]")
    void Refresh(const std::vector<std::string>& addrs, int port);
    void Poll(); // non-blocking: collect answers
    const std::vector<ServerEntry>& Entries() const { return m_entries; }
    bool Busy() const; // asked less than ~1.5 s ago

private:
    bool open();
    long long m_sock = -1;
    unsigned m_sentAt = 0;
    std::vector<ServerEntry> m_entries;
    std::vector<std::pair<std::string, unsigned>> m_asked; // resolved "ip:port", when
};
