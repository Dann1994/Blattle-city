#pragma once

#include <array>
#include <string>
#include <vector>

#include "NetMessages.h"

// Forward declarations de ENet (evita meter <enet/enet.h> en este header:
// asi cualquier .cpp que solo necesite conocer la interfaz de NetHost, sin
// tocar ENet directo, no se lleva de arrastre sus includes/macros de socket).
struct _ENetHost;
struct _ENetPeer;

namespace bc::net {

enum class NetRole { None, Server, Client };

// Eventos de "alto nivel" que le importan a Game (join/reject/leave del
// handshake), en vez de que Game tenga que conocer los eventos crudos de
// ENet. Se devuelven en una lista desde Update(), ya que en un mismo paso
// fijo pueden pasar varios (p.ej. 2 clientes conectandose el mismo frame).
enum class NetEventType {
    ClientJoined,    // servidor: un cliente completo el handshake (ya se le mando JoinAccept)
    ClientRejected,  // servidor: alguien pidio unirse pero no habia lugar / version distinta
    ClientLeft,      // servidor: un cliente conectado se desconecto
    Connected,       // cliente: conexion ENet de bajo nivel lista (todavia no el handshake)
    Accepted,        // cliente: el servidor lo acepto (JoinAccept recibido)
    Rejected,        // cliente: el servidor lo rechazo (JoinReject recibido)
    Disconnected,    // cliente: se cayo la conexion, o el servidor cerro
    RawMessage,      // cualquier mensaje de gameplay (lobby/input/snapshot/terreno, ver NetMessages.h): NetHost no lo interpreta, Game lo desarma segun rawData[0]
};

struct NetEvent {
    NetEventType type = NetEventType::ClientJoined;
    int slot = -1;                                             // servidor: slot 1-3 del cliente (0 no puede pasar aca: el host nunca se manda paquetes a si mismo); cliente: su propio slot asignado, o 0 para RawMessage (siempre viene del servidor)
    std::string peerName;                                      // servidor: nombre de ese cliente; cliente: nombre del host
    JoinRejectReason rejectReason = JoinRejectReason::ServerFull;
    std::vector<std::uint8_t> rawData;                         // solo type==RawMessage: el paquete completo, incluido el byte de MessageType (ver NetMessages.h)
};

// Envuelve un ENetHost propio (servidor o cliente, nunca los dos a la vez
// en la misma instancia) y el handshake de union de NetMessages.h. Un solo
// canal ENet por ahora (todo confiable, ver seccion 11.3 — PLAYER_INPUT/
// WORLD_SNAPSHOT por canal no confiable son de un tramo futuro, cuando haya
// gameplay real viajando).
class NetHost {
public:
    ~NetHost();

    // Arranca como servidor: acepta hasta kMaxRemoteClients conexiones.
    // hostName es el nombre propio (se le manda a cada cliente que se une,
    // ver JoinAcceptMsg::hostName). Devuelve false si no pudo abrir el
    // socket (puerto ocupado, etc.).
    bool StartServer(const std::string& hostName, std::uint16_t port = kDefaultGamePort);

    // Arranca como cliente e inicia la conexion hacia ip:port (no bloquea:
    // el resultado del handshake llega como NetEventType::Connected seguido
    // de Accepted/Rejected en llamados posteriores a Update()).
    bool Connect(const std::string& ip, const std::string& playerName, std::uint16_t port = kDefaultGamePort);

    // Cierra la conexion/servidor y libera el ENetHost. Deja la instancia
    // lista para volver a usarse (StartServer/Connect de nuevo).
    void Shutdown();

    // Procesa los eventos ENet pendientes (llamar 1 vez por paso fijo,
    // mientras IsActive()). No bloquea.
    std::vector<NetEvent> Update();

    NetRole Role() const { return role_; }
    bool IsActive() const { return host_ != nullptr; }

    // Servidor: estado de los slots 1-3 (index 0 = slot 1, etc. — no hay
    // entrada para el propio host, que es slot 0 implicito).
    struct RemoteSlot {
        bool connected = false;
        std::string name;
    };
    const RemoteSlot& Slot(int remoteIndex) const { return slots_[static_cast<size_t>(remoteIndex)]; }
    int ConnectedCount() const; // 1 (el host) + cuantos remoteSlots_ esten conectados

    // Cliente: el slot que le asigno el servidor (1-3) una vez Accepted;
    // -1 mientras no este confirmado.
    int MySlot() const { return mySlot_; }

    // Mensajes de gameplay (lobby/input/snapshot/terreno, ver
    // NetMessages.h): NetHost no los interpreta, solo los transporta. data
    // ya tiene que incluir el byte de MessageType al principio (ver
    // PackMessage/BuildPlayerInputBatchPacket). Devuelven false si no
    // corresponde (cliente mandando a un slot que no existe, servidor
    // llamando a SendToServer, etc.), nunca lanzan.
    bool SendToServer(const std::uint8_t* data, size_t size, bool reliable); // solo cliente
    bool SendToClient(int remoteSlot, const std::uint8_t* data, size_t size, bool reliable); // solo servidor, remoteSlot 1-3
    void Broadcast(const std::uint8_t* data, size_t size, bool reliable); // solo servidor, a todos los clientes conectados

    // Fuerza a mandar YA lo que este encolado (enet_host_service normalmente
    // se encarga de esto, pero Shutdown()/enet_host_destroy no espera a que
    // se termine de mandar nada pendiente) — llamar justo antes de
    // Shutdown() si se mando algo que tiene que llegar si o si antes de
    // cerrar (ver Game::StopLanActivity, MessageType::ServerClosing).
    void Flush();

private:
    void HandleServerPacket(_ENetPeer* peer, const std::uint8_t* data, size_t size, std::vector<NetEvent>& outEvents);
    void HandleClientPacket(const std::uint8_t* data, size_t size, std::vector<NetEvent>& outEvents);
    int FindFreeSlot() const;   // -1 si no hay lugar
    int FindSlotForPeer(_ENetPeer* peer) const; // -1 si ese peer no tiene slot asignado (todavia no mando JoinRequest)

    _ENetHost* host_ = nullptr;
    _ENetPeer* serverPeer_ = nullptr; // solo como cliente: el peer del servidor
    NetRole role_ = NetRole::None;
    std::string localName_;
    std::array<RemoteSlot, kMaxRemoteClients> slots_{};
    std::array<_ENetPeer*, kMaxRemoteClients> slotPeers_{}; // servidor: que ENetPeer* ocupa cada slot (nullptr = libre)
    int mySlot_ = -1; // solo cliente
};

} // namespace bc::net
