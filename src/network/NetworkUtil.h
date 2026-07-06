#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <string>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")

struct LocalIPEntry {
    std::string ip;
    ULONG       prefixLength = 0;  // subnet mask in CIDR bits
};

inline std::vector<LocalIPEntry> enumerateLocalIPs() {
    std::vector<LocalIPEntry> result;
    ULONG bufSize = 15000;
    std::vector<BYTE> buf(bufSize);
    PIP_ADAPTER_ADDRESSES addrs = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data());
    ULONG ret = GetAdaptersAddresses(AF_INET,
        GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
        nullptr, addrs, &bufSize);
    if (ret != NO_ERROR) return result;

    for (auto* a = addrs; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        for (auto* ua = a->FirstUnicastAddress; ua; ua = ua->Next) {
            if (ua->Address.lpSockaddr->sa_family != AF_INET) continue;
            char ip[64];
            sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            std::string sip(ip);
            if (sip != "127.0.0.1" && sip != "0.0.0.0") {
                LocalIPEntry entry;
                entry.ip = sip;
                entry.prefixLength = ua->OnLinkPrefixLength;
                result.push_back(entry);
            }
        }
    }
    return result;
}
