#include "LanDiscovery.h"

#include <algorithm>
#include <cstring>

#include "NetMessages.h" // kProtocolVersion

// OJO: nunca incluir <raylib.h> en este archivo (ver Update(double now) en
// el header): raylib redefine Rectangle/CloseWindow/ShowCursor, que chocan
// de frente con <windows.h> (que arrastra <winsock2.h> aca abajo) y no
// compila. Por eso "now" (GetTime() de raylib) entra como parametro desde
// afuera en vez de llamarse aca directo.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace bc::net {

namespace {

#ifdef _WIN32
// WSAStartup/WSACleanup: 1 vez por proceso, igual criterio que
// EnsureEnetInitialized en NetHost.cpp (nunca se hace WSACleanup explicito,
// el proceso libera todo solo al salir).
bool EnsureWinsockInitialized() {
    static const bool initialized = [] {
        WSADATA wsaData;
        return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
    }();
    return initialized;
}

// Paquete de anuncio tal cual viaja por la red (ver LanDiscovery.h): magic
// primero para poder descartar de un vistazo cualquier ruido UDP que no sea
// nuestro, sin necesidad de parsear el resto.
#pragma pack(push, 1)
struct WireAnnouncePacket {
    std::uint32_t magic = kDiscoveryMagic;
    std::uint16_t protocolVersion = 0;
    std::uint16_t gamePort = 0;
    std::uint8_t connectedCount = 0;
    std::uint8_t maxCount = 0;
    char hostName[kDiscoveryHostNameMaxLen + 1] = {};
};
#pragma pack(pop)

void CopyHostNameInto(char (&dest)[kDiscoveryHostNameMaxLen + 1], const std::string& src) {
    const size_t len = std::min(src.size(), static_cast<size_t>(kDiscoveryHostNameMaxLen));
    std::memcpy(dest, src.data(), len);
    dest[len] = '\0';
}
#endif

} // namespace

LanAnnouncer::~LanAnnouncer() {
    Stop();
}

bool LanAnnouncer::Start(const std::string& hostName, std::uint16_t gamePort) {
#ifdef _WIN32
    if (!EnsureWinsockInitialized()) {
        return false;
    }
    Stop();

    const SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        return false;
    }
    const BOOL broadcastEnabled = TRUE;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&broadcastEnabled), sizeof(broadcastEnabled)) != 0) {
        closesocket(sock);
        return false;
    }

    socket_ = static_cast<std::uintptr_t>(sock);
    hostName_ = hostName;
    gamePort_ = gamePort;
    timer_ = kDiscoveryAnnounceInterval; // pedido explicitamente: que el primer anuncio salga ya, no recien tras 1s
    return true;
#else
    (void)hostName;
    (void)gamePort;
    return false;
#endif
}

void LanAnnouncer::Stop() {
#ifdef _WIN32
    if (socket_ != kInvalidSocket) {
        closesocket(static_cast<SOCKET>(socket_));
        socket_ = kInvalidSocket;
    }
#endif
}

void LanAnnouncer::Update(double dt, int connectedCount, int maxCount) {
#ifdef _WIN32
    if (socket_ == kInvalidSocket) {
        return;
    }
    timer_ += dt;
    if (timer_ < kDiscoveryAnnounceInterval) {
        return;
    }
    timer_ -= kDiscoveryAnnounceInterval;

    WireAnnouncePacket packet{};
    packet.protocolVersion = kProtocolVersion;
    packet.gamePort = gamePort_;
    packet.connectedCount = static_cast<std::uint8_t>(std::clamp(connectedCount, 0, 255));
    packet.maxCount = static_cast<std::uint8_t>(std::clamp(maxCount, 0, 255));
    CopyHostNameInto(packet.hostName, hostName_);

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(kDiscoveryPort);
    destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(static_cast<SOCKET>(socket_), reinterpret_cast<const char*>(&packet), sizeof(packet), 0,
           reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
#else
    (void)dt;
    (void)connectedCount;
    (void)maxCount;
#endif
}

LanBrowser::~LanBrowser() {
    Stop();
}

bool LanBrowser::Start() {
#ifdef _WIN32
    if (!EnsureWinsockInitialized()) {
        return false;
    }
    Stop();

    const SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        return false;
    }
    // SO_REUSEADDR: pedido explicitamente para poder probar host+cliente en
    // la misma PC (dos procesos escuchando el mismo puerto de broadcast) —
    // sin esto el segundo bind() fallaria.
    const BOOL reuseEnabled = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuseEnabled), sizeof(reuseEnabled));

    sockaddr_in bindAddress{};
    bindAddress.sin_family = AF_INET;
    bindAddress.sin_port = htons(kDiscoveryPort);
    bindAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<const sockaddr*>(&bindAddress), sizeof(bindAddress)) != 0) {
        closesocket(sock);
        return false;
    }

    u_long nonBlocking = 1;
    ioctlsocket(sock, FIONBIO, &nonBlocking);

    socket_ = static_cast<std::uintptr_t>(sock);
    hosts_.clear();
    return true;
#else
    return false;
#endif
}

void LanBrowser::Stop() {
#ifdef _WIN32
    if (socket_ != kInvalidSocket) {
        closesocket(static_cast<SOCKET>(socket_));
        socket_ = kInvalidSocket;
    }
#endif
    hosts_.clear();
}

void LanBrowser::Update(double now) {
#ifdef _WIN32
    if (socket_ == kInvalidSocket) {
        return;
    }

    WireAnnouncePacket packet{};
    sockaddr_in sender{};
    int senderLen = sizeof(sender);
    while (true) {
        const int received = recvfrom(static_cast<SOCKET>(socket_), reinterpret_cast<char*>(&packet), sizeof(packet), 0,
                                       reinterpret_cast<sockaddr*>(&sender), &senderLen);
        if (received <= 0) {
            break; // WSAEWOULDBLOCK (no hay mas paquetes esperando) o error: en ambos casos, nada mas que leer ahora
        }
        if (received != static_cast<int>(sizeof(packet)) || packet.magic != kDiscoveryMagic) {
            continue; // ruido UDP random en el puerto, o un anuncio de una version vieja/nueva con otro tamaño de paquete
        }
        packet.hostName[kDiscoveryHostNameMaxLen] = '\0';

        char addressText[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &sender.sin_addr, addressText, sizeof(addressText));

        bool updated = false;
        for (DiscoveredHost& existing : hosts_) {
            if (existing.address == addressText && existing.gamePort == packet.gamePort) {
                existing.hostName = packet.hostName;
                existing.connectedCount = packet.connectedCount;
                existing.maxCount = packet.maxCount;
                existing.lastSeenAt = now;
                updated = true;
                break;
            }
        }
        if (!updated) {
            DiscoveredHost host;
            host.address = addressText;
            host.hostName = packet.hostName;
            host.gamePort = packet.gamePort;
            host.connectedCount = packet.connectedCount;
            host.maxCount = packet.maxCount;
            host.lastSeenAt = now;
            hosts_.push_back(std::move(host));
        }
    }

    hosts_.erase(std::remove_if(hosts_.begin(), hosts_.end(), [now](const DiscoveredHost& host) {
        return now - host.lastSeenAt > kDiscoveryStaleTimeout;
    }), hosts_.end());
#endif
}

} // namespace bc::net
