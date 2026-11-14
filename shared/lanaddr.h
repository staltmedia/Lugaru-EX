// LugaruEX - the broadcast address of every network this PC is on (GPLv2+): real LANs and
// virtual ones (Radmin VPN, Hamachi, ZeroTier...), so a LAN search reaches hosts on all of
// them, not only on the main network. (Tailscale carries no broadcasts: type its address.)
#pragma once
#include <cstdint>
#include <vector>

// IPv4 broadcast addresses in network byte order, 255.255.255.255 first
std::vector<uint32_t> LanBroadcasts();
