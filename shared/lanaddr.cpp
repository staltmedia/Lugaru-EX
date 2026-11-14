// LugaruEX - broadcast addresses of this PC's networks (GPLv2+)
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#endif

#include "lanaddr.h"

#include <algorithm>

std::vector<uint32_t> LanBroadcasts()
{
    std::vector<uint32_t> out;
    out.push_back(htonl(INADDR_BROADCAST));
    auto add = [&](uint32_t b) {
        if (b != 0 && std::find(out.begin(), out.end(), b) == out.end() && out.size() < 32) {
            out.push_back(b);
        }
    };
#ifdef _WIN32
    ULONG size = 32 * 1024;
    std::vector<unsigned char> buf(size);
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG r = GetAdaptersAddresses(AF_INET, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &size);
    if (r == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        r = GetAdaptersAddresses(AF_INET, flags, nullptr, (IP_ADAPTER_ADDRESSES*)buf.data(), &size);
    }
    if (r != NO_ERROR) {
        return out;
    }
    for (IP_ADAPTER_ADDRESSES* a = (IP_ADAPTER_ADDRESSES*)buf.data(); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
            continue;
        }
        for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET || u->OnLinkPrefixLength == 0 || u->OnLinkPrefixLength >= 31) {
                continue;
            }
            const uint32_t ip = ntohl(((sockaddr_in*)u->Address.lpSockaddr)->sin_addr.s_addr);
            const uint32_t mask = 0xFFFFFFFFu << (32 - u->OnLinkPrefixLength);
            if ((ip >> 24) == 127 || (ip >> 16) == 0xA9FE) {
                continue; // loopback, link-local (no network there)
            }
            add(htonl(ip | ~mask));
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        return out;
    }
    for (ifaddrs* i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK)) {
            continue;
        }
        if ((i->ifa_flags & IFF_BROADCAST) && i->ifa_broadaddr) {
            add(((sockaddr_in*)i->ifa_broadaddr)->sin_addr.s_addr);
        } else if (i->ifa_netmask) { // (some virtual networks don't say: work it out)
            const uint32_t ip = ((sockaddr_in*)i->ifa_addr)->sin_addr.s_addr, mask = ((sockaddr_in*)i->ifa_netmask)->sin_addr.s_addr;
            if (mask != 0xFFFFFFFFu) {
                add(ip | ~mask);
            }
        }
    }
    freeifaddrs(list);
#endif
    return out;
}
