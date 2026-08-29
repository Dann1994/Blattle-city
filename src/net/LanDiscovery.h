#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bc::net {

// Descubrimiento de partidas en LAN (seccion 11.2 del documento de diseno):
// un socket UDP de broadcast APARTE del canal ENet (que solo habla con una
// IP conocida) — el servidor anuncia periodicamente por broadcast en la red
// local, y quien busca partidas escucha ese mismo puerto fijo sin tener que
// tipear ninguna IP a mano.
constexpr std::uint16_t kDiscoveryPort = 27015;
constexpr std::uint32_t kDiscoveryMagic = 0x424c434eu; // "BLCN", para distinguir nuestros paquetes de ruido UDP random
constexpr double kDiscoveryAnnounceInterval = 1.0;     // segundos entre cada broadcast del servidor
constexpr double kDiscoveryStaleTimeout = 3.0;         // una partida que no se escucha en este tiempo se cae de la lista
constexpr int kDiscoveryHostNameMaxLen = 31;

// Servidor: emite el broadcast periodico mientras se este hosteando. No
// tiene nada que ver con la conexion ENet en si (esa la maneja NetHost) —
// solo avisa "aca hay una partida" y en que puerto ENet conectarse.
class LanAnnouncer {
public:
    ~LanAnnouncer();

    bool Start(const std::string& hostName, std::uint16_t gamePort);
    void Stop();
    bool IsActive() const { return socket_ != kInvalidSocket; }

    // Llamar cada paso fijo mientras IsActive(): manda un nuevo broadcast
    // cuando toca (ver kDiscoveryAnnounceInterval), sin bloquear.
    void Update(double dt, int connectedCount, int maxCount);

private:
    static constexpr std::uintptr_t kInvalidSocket = static_cast<std::uintptr_t>(-1);

    std::uintptr_t socket_ = kInvalidSocket;
    double timer_ = 0.0;
    std::string hostName_;
    std::uint16_t gamePort_ = 0;
};

struct DiscoveredHost {
    std::string address;   // IP como texto (para pasarle directo a NetHost::Connect)
    std::string hostName;
    std::uint16_t gamePort = 0;
    int connectedCount = 0;
    int maxCount = 4;
    double lastSeenAt = 0.0; // en segundos de GetTime() (raylib), para expirar entradas viejas
};

// Cliente: escucha el broadcast y arma la lista de partidas encontradas en
// la red. No bloqueante.
class LanBrowser {
public:
    ~LanBrowser();

    bool Start();
    void Stop();
    bool IsActive() const { return socket_ != kInvalidSocket; }

    // Llamar cada frame mientras IsActive(): lee los paquetes de broadcast
    // pendientes (sin bloquear) y purga las entradas que dejaron de
    // escucharse (ver kDiscoveryStaleTimeout). now es GetTime() de raylib
    // (pasado desde afuera para no meter <raylib.h> aca: entra en conflicto
    // con <winsock2.h>/<windows.h>, que este .cpp si necesita).
    void Update(double now);

    const std::vector<DiscoveredHost>& Hosts() const { return hosts_; }

private:
    static constexpr std::uintptr_t kInvalidSocket = static_cast<std::uintptr_t>(-1);

    std::uintptr_t socket_ = kInvalidSocket;
    std::vector<DiscoveredHost> hosts_;
};

} // namespace bc::net
