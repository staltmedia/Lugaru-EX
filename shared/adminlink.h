// LugaruEX - the launchers' link to the game they started (GPLv2+)
//
// The launcher stays open while an online game runs. It starts the game with
// LUGARUEX_ADMIN=<its port>:<32 hex secret>; once a second the game sends how things are
// (host or guest, who is on the server, whether a password is wanted) and the launcher
// can send back kick / ban / a password. Only on this PC (127.0.0.1), only with the secret.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct AdminPlayer
{
    uint32_t id = 0;
    std::string name;
    bool inGame = false;
    int seats = 1; // players on that PC
};

struct AdminStatus
{
    bool heard = false; // a status came from the game
    int role = 0;       // 1 host, 2 guest
    bool steam = false;
    std::string code;   // host through Steam: the lobby (empty until it is up)
    std::string status; // host: why the server isn't up / guest: waiting, playing, refused...
    bool needPassword = false;
    uint32_t sessionId = 0; // host: the server list id of this hosting session
    std::vector<AdminPlayer> players;
};

class AdminLink
{
public:
    ~AdminLink();
    bool Open();             // the launcher's socket (once)
    std::string NewSecret(); // for LUGARUEX_ADMIN; forgets the last game's status
    bool Poll();             // non-blocking; true when a status came
    void Kick(uint32_t id, bool ban);
    void Password(const std::string& pw);
    AdminStatus st;

private:
    void send(const std::vector<uint8_t>& body);
    long long m_sock = -1;
    uint8_t m_token[16] = {};
    int m_port = 0;
    int m_gamePort = 0; // the game's end (from its status packets)
};
