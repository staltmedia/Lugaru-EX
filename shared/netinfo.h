// LugaruEX - online play: what a host tells the server list, player names, colour codes
// and password proofs. Shared by the engine (ExNet) and both launchers. (GPLv2+)
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace netinfo
{
const uint32_t kMagic = 0x3958474C; // "LGX9" (0.4.13: names, passwords, server list, chat)
// packet types the launchers use too (same numbers as ExNet's)
const uint8_t kDiscover = 1; // query: padded to kQueryLen bytes, so an answer is never much bigger
const uint8_t kHere = 2;     // answer: magic, type, EncodeInfo()
const int kQueryLen = 64;
const int kMaxName = 20;       // player names
const int kMaxServerName = 40; // server names (without colour codes)
const int kMaxChat = 120;

enum PlayerState : uint8_t
{
    PS_WAITING = 0, // connected, not in the level (menu, loading, downloading mods)
    PS_INGAME = 1,
};

struct Player
{
    std::string name;
    uint8_t state = PS_WAITING;
};

struct ServerInfo
{
    uint32_t id = 0; // this hosting session (the same server answering at two addresses shows once)
    std::string version;
    std::string name; // may carry colour codes: ^0 .. ^9
    bool password = false;
    bool shareMods = false;
    int mods = 0; // mods it shares
    int players = 0, maxPlayers = 0;
    bool inGame = false;      // the host is playing a level (else in its menu)
    std::vector<Player> list; // who is there; empty when password protected
};

std::vector<uint8_t> EncodeInfo(const ServerInfo& info); // fits one packet
bool DecodeInfo(const uint8_t* d, int n, ServerInfo& out);

// printable ASCII only, no leading/trailing spaces, at most maxLen visible characters;
// colour codes (^ + digit) kept when allowed, else removed. Empty stays empty.
std::string CleanText(const std::string& s, size_t maxLen, bool allowColours);
std::string CleanName(const std::string& s); // player name, "User" when empty
std::string StripColours(const std::string& s);

struct Segment
{
    int colour; // -1 = the default colour
    std::string text;
};
std::vector<Segment> ColourSegments(const std::string& s);
void ColourRGB(int colour, unsigned char rgb[3]); // ^0 .. ^9 (bright, for dark backgrounds)

// paths and names from the other PC, before they touch files:
// a data path ("Textures/..."): relative, no "..", no drive / backslash, plain characters
bool SafeDataPath(const std::string& p);
// a level name: plain characters, no path at all
bool SafeLevelName(const std::string& n);
// a character choice "model[:skin][|outfit]": anything unusual becomes "random"
std::string SafeLook(const std::string& spec);

// what a guest sends for a password: SHA-256 of the host's session number + the password
std::vector<uint8_t> PasswordProof(uint64_t nonce, const std::string& password);
const char* StateName(int state); // "Waiting", "In game"
} // namespace netinfo
