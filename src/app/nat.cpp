#include "nat.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
using SockLen = int;
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
using SockLen = socklen_t;
inline int closesocket(SOCKET s) { return close(s); }
#endif

namespace app {

namespace {

double Now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void EnsureSockets() {
#ifdef _WIN32
    static struct Init {
        Init() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
        ~Init() { WSACleanup(); }
    } init;
#endif
}

void SetNonBlocking(SOCKET s, bool nb) {
#ifdef _WIN32
    u_long v = nb ? 1 : 0;
    ioctlsocket(s, FIONBIO, &v);
#else
    const int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, nb ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK));
#endif
}

bool Resolve(const std::string& host, uint16_t port, sockaddr_in& out) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) return false;
    out = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
    freeaddrinfo(res);
    return true;
}

// ---- HTTP minimo por TCP (para UPnP) ----
bool HttpRequest(const std::string& ip, uint16_t port, const std::string& request, std::string& response, double timeout) {
    EnsureSockets();
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &a.sin_addr);
    SetNonBlocking(s, true);
    ::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a);
    fd_set wf;
    FD_ZERO(&wf);
    FD_SET(s, &wf);
    timeval tv{static_cast<long>(timeout), 0};
    if (::select(static_cast<int>(s) + 1, nullptr, &wf, nullptr, &tv) <= 0) { closesocket(s); return false; }
    SetNonBlocking(s, false);
#ifdef _WIN32
    DWORD to = static_cast<DWORD>(timeout * 1000);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&to), sizeof to);
#else
    timeval rt{static_cast<long>(timeout), 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof rt);
#endif
    if (::send(s, request.data(), static_cast<int>(request.size()), 0) < 0) { closesocket(s); return false; }
    response.clear();
    char buf[4096];
    for (;;) {
        const int n = ::recv(s, buf, sizeof buf, 0);
        if (n <= 0) break;
        response.append(buf, static_cast<size_t>(n));
        if (response.size() > 200000) break;
    }
    closesocket(s);
    return !response.empty();
}

struct Igd {
    std::string ip;
    uint16_t port = 0;
    std::string controlPath;
    std::string serviceType;
    std::string localIp;
};

bool ParseUrl(const std::string& url, std::string& host, uint16_t& port, std::string& path) {
    if (url.compare(0, 7, "http://") != 0) return false;
    const size_t slash = url.find('/', 7);
    std::string hp = url.substr(7, slash == std::string::npos ? std::string::npos : slash - 7);
    path = slash == std::string::npos ? "/" : url.substr(slash);
    const size_t colon = hp.find(':');
    host = hp.substr(0, colon);
    port = colon == std::string::npos ? 80 : static_cast<uint16_t>(std::atoi(hp.substr(colon + 1).c_str()));
    return true;
}

// Busca el router (SSDP) y lee de su descripcion donde se controla el reenvio de puertos.
bool DiscoverIgd(Igd& igd, std::string& error) {
    EnsureSockets();
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { error = "SIN RED"; return false; }
    SetNonBlocking(s, true);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &to.sin_addr);
    const char* targets[] = {"urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:service:WANIPConnection:1",
                             "urn:schemas-upnp-org:service:WANPPPConnection:1"};
    std::string location;
    const double t0 = Now();
    int sent = 0;
    while (Now() - t0 < 4.0 && location.empty()) {
        if (sent < 6 && Now() - t0 > sent * 0.5) {
            std::string req = std::string("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") +
                              targets[sent % 3] + "\r\n\r\n";
            ::sendto(s, req.data(), static_cast<int>(req.size()), 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
            ++sent;
        }
        char buf[2048];
        const int n = ::recvfrom(s, buf, sizeof buf - 1, 0, nullptr, nullptr);
        if (n > 0) {
            std::string r(buf, static_cast<size_t>(n));
            std::string low = r;
            std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            const size_t p = low.find("location:");
            if (p != std::string::npos) {
                size_t e = r.find("\r\n", p);
                std::string v = r.substr(p + 9, e == std::string::npos ? std::string::npos : e - p - 9);
                while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
                location = v;
            }
        } else {
#ifdef _WIN32
            Sleep(20);
#else
            usleep(20000);
#endif
        }
    }
    closesocket(s);
    if (location.empty()) { error = "EL ROUTER NO RESPONDE A UPNP"; return false; }
    std::string host, path;
    uint16_t port = 0;
    if (!ParseUrl(location, host, port, path)) { error = "UPNP: DIRECCIÓN RARA"; return false; }
    std::string resp;
    const std::string get = "GET " + path + " HTTP/1.1\r\nHOST: " + host + ":" + std::to_string(port) + "\r\nConnection: close\r\n\r\n";
    if (!HttpRequest(host, port, get, resp, 3.0)) { error = "UPNP: NO SE PUDO LEER EL ROUTER"; return false; }
    // el servicio que reenvia puertos
    for (const char* type : {"urn:schemas-upnp-org:service:WANIPConnection:1", "urn:schemas-upnp-org:service:WANIPConnection:2",
                             "urn:schemas-upnp-org:service:WANPPPConnection:1"}) {
        const size_t sp = resp.find(type);
        if (sp == std::string::npos) continue;
        const size_t cp = resp.find("<controlURL>", sp);
        if (cp == std::string::npos) continue;
        const size_t ce = resp.find("</controlURL>", cp);
        if (ce == std::string::npos) continue;
        igd.controlPath = resp.substr(cp + 12, ce - cp - 12);
        igd.serviceType = type;
        break;
    }
    if (igd.controlPath.empty()) { error = "UPNP: EL ROUTER NO REENVÍA PUERTOS"; return false; }
    if (igd.controlPath.compare(0, 7, "http://") == 0) {
        std::string h2, p2;
        uint16_t pt2 = 0;
        if (ParseUrl(igd.controlPath, h2, pt2, p2)) { host = h2; port = pt2; igd.controlPath = p2; }
    } else if (igd.controlPath.empty() || igd.controlPath[0] != '/') {
        igd.controlPath = "/" + igd.controlPath;
    }
    igd.ip = host;
    igd.port = port;
    return true;
}

bool Soap(const Igd& igd, const std::string& action, const std::string& args, std::string& response) {
    const std::string body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                             "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" + action + " xmlns:u=\"" + igd.serviceType +
                             "\">" + args + "</u:" + action + "></s:Body></s:Envelope>";
    std::ostringstream req;
    req << "POST " << igd.controlPath << " HTTP/1.1\r\nHOST: " << igd.ip << ":" << igd.port << "\r\n"
        << "Content-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: " << body.size() << "\r\n"
        << "SOAPACTION: \"" << igd.serviceType << "#" << action << "\"\r\nConnection: close\r\n\r\n" << body;
    if (!HttpRequest(igd.ip, igd.port, req.str(), response, 4.0)) return false;
    return response.compare(0, 12, "HTTP/1.1 200") == 0 || response.compare(0, 12, "HTTP/1.0 200") == 0;
}

} // namespace

std::string LocalAddress() {
    EnsureSockets();
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return "";
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &a.sin_addr);
    std::string out;
    if (::connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {            // UDP: no manda nada, solo elige la interfaz
        sockaddr_in me{};
        SockLen l = sizeof me;
        if (::getsockname(s, reinterpret_cast<sockaddr*>(&me), &l) == 0) {
            char buf[64] = {};
            inet_ntop(AF_INET, &me.sin_addr, buf, sizeof buf);
            out = buf;
        }
    }
    closesocket(s);
    return out;
}

std::vector<IfaceAddr> LocalInterfaces() {
    EnsureSockets();
    std::vector<IfaceAddr> out;
    auto add = [&](uint32_t ip, uint32_t mask) {                      // ip y mascara en orden de host
        if ((ip >> 24) == 127 || (ip >> 16) == 0xA9FE || mask == 0) return;     // ni loopback ni 169.254.x.x (sin DHCP)
        const uint32_t bc = ip | ~mask;
        in_addr a{}, b{};
        a.s_addr = htonl(ip);
        b.s_addr = htonl(bc);
        char t1[32] = {}, t2[32] = {};
        inet_ntop(AF_INET, &a, t1, sizeof t1);
        inet_ntop(AF_INET, &b, t2, sizeof t2);
        for (const auto& o : out) if (o.ip == t1) return;
        out.push_back({t1, t2});
    };
#ifdef _WIN32
    ULONG size = 16000;
    std::vector<uint8_t> buf(size);
    ULONG r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                   reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    if (r == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                                 reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (r == NO_ERROR) {
        for (IP_ADAPTER_ADDRESSES* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
                if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
                const sockaddr_in* sa = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
                const unsigned prefix = u->OnLinkPrefixLength;
                add(ntohl(sa->sin_addr.s_addr), prefix == 0 ? 0u : (prefix >= 32 ? 0xFFFFFFFFu : ~0u << (32 - prefix)));
            }
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* a = list; a; a = a->ifa_next) {
            if (!a->ifa_addr || !a->ifa_netmask || a->ifa_addr->sa_family != AF_INET) continue;
            if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
            add(ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr), ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr));
        }
        freeifaddrs(list);
    }
#endif
    return out;
}

std::vector<StunServer> StunServers() {
    EnsureSockets();
    struct Def { const char* host; uint16_t port; };
    const Def defs[] = {{"stun.l.google.com", 19302}, {"stun1.l.google.com", 19302}, {"stun.cloudflare.com", 3478}};
    std::vector<StunServer> out;
    for (const auto& d : defs) {
        sockaddr_in a{};
        if (!Resolve(d.host, d.port, a)) continue;
        char buf[64] = {};
        inet_ntop(AF_INET, &a.sin_addr, buf, sizeof buf);
        out.push_back({buf, d.port});
    }
    return out;
}

std::string StunRequest(const uint8_t transaction[12]) {
    uint8_t req[20] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
    std::memcpy(req + 8, transaction, 12);
    return std::string(reinterpret_cast<const char*>(req), sizeof req);
}

bool StunParse(const char* data, int n, const uint8_t transaction[12], PublicEndpoint& result) {
    const uint8_t* buf = reinterpret_cast<const uint8_t*>(data);
    if (n < 20 || buf[0] != 0x01 || buf[1] != 0x01 || std::memcmp(buf + 8, transaction, 12) != 0) return false;
    int pos = 20;
    const int len = 20 + ((buf[2] << 8) | buf[3]);
    while (pos + 4 <= n && pos + 4 <= len) {
        const int type = (buf[pos] << 8) | buf[pos + 1];
        const int alen = (buf[pos + 2] << 8) | buf[pos + 3];
        if ((type == 0x0020 || type == 0x0001) && alen >= 8 && pos + 12 <= n && buf[pos + 5] == 0x01) {   // XOR-MAPPED-ADDRESS / MAPPED-ADDRESS (IPv4)
            uint16_t port = static_cast<uint16_t>((buf[pos + 6] << 8) | buf[pos + 7]);
            uint8_t ip[4] = {buf[pos + 8], buf[pos + 9], buf[pos + 10], buf[pos + 11]};
            if (type == 0x0020) {
                port = static_cast<uint16_t>(port ^ 0x2112);
                ip[0] ^= 0x21; ip[1] ^= 0x12; ip[2] ^= 0xA4; ip[3] ^= 0x42;
            }
            char text[32];
            std::snprintf(text, sizeof text, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
            result.ok = true;
            result.ip = text;
            result.port = port;
            return true;
        }
        pos += 4 + ((alen + 3) & ~3);
    }
    return false;
}

// Solo mira si hay un router con UPnP (no cambia nada): devuelve su direccion de control o el motivo del fallo.
std::string UpnpProbe(bool& found) {
    Igd igd;
    std::string err;
    found = DiscoverIgd(igd, err);
    return found ? igd.ip + ":" + std::to_string(igd.port) + igd.controlPath + " (" + igd.serviceType + ")" : err;
}

UpnpResult UpnpOpenUdp(uint16_t port, const std::string& description) {
    UpnpResult r;
    Igd igd;
    if (!DiscoverIgd(igd, r.error) && !DiscoverIgd(igd, r.error)) return r;      // se reintenta: el multicast a veces se pierde
    const std::string local = LocalAddress();
    if (local.empty()) { r.error = "NO SE PUDO SABER LA IP LOCAL"; return r; }
    std::string resp;
    // si quedo abierto un reenvio viejo (por ejemplo el juego se cerro sin avisar) el router puede rechazar el nuevo: se limpia antes
    Soap(igd, "DeletePortMapping", "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) + "</NewExternalPort><NewProtocol>UDP</NewProtocol>", resp);
    for (const char* lease : {"7200", "0"}) {                       // con vencimiento (se renueva); si el router no lo acepta, sin vencimiento
        const std::string args = "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) + "</NewExternalPort><NewProtocol>UDP</NewProtocol>"
                                 "<NewInternalPort>" + std::to_string(port) + "</NewInternalPort><NewInternalClient>" + local + "</NewInternalClient>"
                                 "<NewEnabled>1</NewEnabled><NewPortMappingDescription>" + description + "</NewPortMappingDescription>"
                                 "<NewLeaseDuration>" + lease + "</NewLeaseDuration>";
        if (Soap(igd, "AddPortMapping", args, resp)) {
            r.ok = true;
            break;
        }
    }
    if (!r.ok) { r.error = "EL ROUTER RECHAZÓ ABRIR EL PUERTO"; return r; }
    if (Soap(igd, "GetExternalIPAddress", "", resp)) {
        const size_t a = resp.find("<NewExternalIPAddress>");
        const size_t b = resp.find("</NewExternalIPAddress>");
        if (a != std::string::npos && b != std::string::npos) r.externalIp = resp.substr(a + 22, b - a - 22);
    }
    return r;
}

void UpnpCloseUdp(uint16_t port) {
    Igd igd;
    std::string err;
    if (!DiscoverIgd(igd, err)) return;
    std::string resp;
    Soap(igd, "DeletePortMapping", "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port) + "</NewExternalPort><NewProtocol>UDP</NewProtocol>", resp);
}

// ---------------------------------------------------------------- codigo de sala

namespace {
const char* kAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";           // 32 simbolos, sin I, O, 0 ni 1

uint8_t Checksum(const uint8_t* b) {
    uint8_t c = 0x5A;
    for (int i = 0; i < 6; ++i) c = static_cast<uint8_t>((c * 31 + b[i] + 7) & 0xFF);
    return c;
}
} // namespace

std::string MakeRoomCode(const std::string& ip, uint16_t port) {
    in_addr a{};
    if (inet_pton(AF_INET, ip.c_str(), &a) != 1) return "";
    uint8_t b[7];
    std::memcpy(b, &a, 4);                                             // en orden de red
    b[4] = static_cast<uint8_t>(port >> 8);
    b[5] = static_cast<uint8_t>(port & 0xFF);
    b[6] = Checksum(b);
    uint64_t v = 0;
    for (int i = 0; i < 7; ++i) v = (v << 8) | b[i];                   // 56 bits
    v <<= 4;                                                           // 60 bits = 12 simbolos de 5 bits
    std::string out;
    for (int i = 0; i < 12; ++i) out.push_back(kAlphabet[(v >> (55 - i * 5)) & 0x1F]);
    return out.substr(0, 4) + "-" + out.substr(4, 4) + "-" + out.substr(8, 4);
}

bool ParseRoomCode(const std::string& code, std::string& ip, uint16_t& port) {
    std::string clean;
    for (char c : code) {
        if (c == '-' || c == ' ') continue;
        clean.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    if (clean.size() != 12) return false;
    uint64_t v = 0;
    for (char c : clean) {
        const char* p = std::strchr(kAlphabet, c);
        if (!p || c == '\0') return false;
        v = (v << 5) | static_cast<uint64_t>(p - kAlphabet);
    }
    v >>= 4;
    uint8_t b[7];
    for (int i = 6; i >= 0; --i) { b[i] = static_cast<uint8_t>(v & 0xFF); v >>= 8; }
    if (b[6] != Checksum(b)) return false;
    char text[32];
    std::snprintf(text, sizeof text, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    ip = text;
    port = static_cast<uint16_t>((b[4] << 8) | b[5]);
    return port != 0;
}

} // namespace app
