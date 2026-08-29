#include "NetHost.h"

#include <algorithm>
#include <cstring>

#include <enet/enet.h>

namespace bc::net {

namespace {
// ENet pide 1 enet_initialize()/enet_deinitialize() por proceso. Se
// inicializa la primera vez que hace falta (StartServer/Connect) y nunca se
// deinicializa explicitamente — el proceso lo libera solo al salir, igual
// que hacen la mayoria de los juegos que usan ENet (deinicializar a mitad
// de partida, para volver a inicializar despues, es fragil y no hace falta
// aca: NetHost::Shutdown solo destruye el ENetHost propio, no toca esto).
bool EnsureEnetInitialized() {
    static const bool initialized = (enet_initialize() == 0);
    return initialized;
}

// Copia un std::string a un char[] de tamaño fijo, siempre terminado en
// nulo y truncado si hace falta (los nombres de jugador/host viajan asi por
// la red, ver NetMessages.h).
template <size_t N>
void CopyNameInto(char (&dest)[N], const std::string& src) {
    const size_t len = std::min(src.size(), N - 1);
    std::memcpy(dest, src.data(), len);
    dest[len] = '\0';
}

ENetPacket* MakePacket(MessageType type, const void* payload, size_t payloadSize) {
    ENetPacket* packet = enet_packet_create(nullptr, sizeof(std::uint8_t) + payloadSize, ENET_PACKET_FLAG_RELIABLE);
    packet->data[0] = static_cast<std::uint8_t>(type);
    if (payloadSize > 0) {
        std::memcpy(packet->data + 1, payload, payloadSize);
    }
    return packet;
}

// 2 canales ENet: 0 confiable (handshake/lobby/terreno/cierre), 1 no
// confiable (input/snapshot, van a 30-60Hz y no importa perder alguno viejo
// — pedido explicitamente, ver seccion 11.3 del diseno). Van en canales
// distintos para que un paquete confiable reenviado no bloquee (head-of-
// line) a los no confiables mas nuevos que le siguen.
constexpr enet_uint8 kReliableChannel = 0;
constexpr enet_uint8 kUnreliableChannel = 1;
constexpr size_t kChannelCount = 2;
} // namespace

NetHost::~NetHost() {
    Shutdown();
}

bool NetHost::StartServer(const std::string& hostName, std::uint16_t port) {
    if (!EnsureEnetInitialized()) {
        return false;
    }
    Shutdown();

    ENetAddress address{};
    address.host = ENET_HOST_ANY;
    address.port = port;
    // kMaxRemoteClients peers, 2 canales (ver kReliableChannel/
    // kUnreliableChannel), ancho de banda sin limite (0 = ENet decide).
    host_ = enet_host_create(&address, static_cast<size_t>(kMaxRemoteClients), kChannelCount, 0, 0);
    if (host_ == nullptr) {
        return false;
    }
    role_ = NetRole::Server;
    localName_ = hostName;
    slots_ = {};
    slotPeers_ = {};
    mySlot_ = 0; // el host siempre es el jugador 0, "adentro" sin handshake
    return true;
}

bool NetHost::Connect(const std::string& ip, const std::string& playerName, std::uint16_t port) {
    if (!EnsureEnetInitialized()) {
        return false;
    }
    Shutdown();

    // Host de cliente: 0 peers de escucha (no acepta conexiones entrantes),
    // 1 conexion saliente hacia el servidor.
    host_ = enet_host_create(nullptr, 1, kChannelCount, 0, 0);
    if (host_ == nullptr) {
        return false;
    }

    ENetAddress address{};
    if (enet_address_set_host(&address, ip.c_str()) != 0) {
        enet_host_destroy(host_);
        host_ = nullptr;
        return false;
    }
    address.port = port;

    serverPeer_ = enet_host_connect(host_, &address, kChannelCount, 0);
    if (serverPeer_ == nullptr) {
        enet_host_destroy(host_);
        host_ = nullptr;
        return false;
    }

    role_ = NetRole::Client;
    localName_ = playerName;
    mySlot_ = -1;
    return true;
}

void NetHost::Shutdown() {
    if (host_ != nullptr) {
        enet_host_destroy(host_);
        host_ = nullptr;
    }
    serverPeer_ = nullptr;
    role_ = NetRole::None;
    slots_ = {};
    slotPeers_ = {};
    mySlot_ = -1;
}

int NetHost::FindFreeSlot() const {
    for (int i = 0; i < kMaxRemoteClients; ++i) {
        if (slotPeers_[static_cast<size_t>(i)] == nullptr) {
            return i;
        }
    }
    return -1;
}

int NetHost::FindSlotForPeer(_ENetPeer* peer) const {
    for (int i = 0; i < kMaxRemoteClients; ++i) {
        if (slotPeers_[static_cast<size_t>(i)] == peer) {
            return i;
        }
    }
    return -1;
}

void NetHost::HandleServerPacket(_ENetPeer* peer, const std::uint8_t* data, size_t size, std::vector<NetEvent>& outEvents) {
    if (size < 1) {
        return;
    }
    const MessageType type = static_cast<MessageType>(data[0]);
    if (type != MessageType::JoinRequest) {
        // No es el handshake: cualquier otro tipo (lobby/input) es
        // gameplay que Game interpreta, no NetHost — pero solo de un peer
        // que ya complete el handshake antes (si no, no tiene slot).
        const int slot = FindSlotForPeer(peer);
        if (slot == -1) {
            return; // paquete de gameplay de un peer sin handshake completo: se ignora
        }
        NetEvent ev;
        ev.type = NetEventType::RawMessage;
        ev.slot = slot + 1;
        ev.rawData.assign(data, data + size);
        outEvents.push_back(std::move(ev));
        return;
    }
    if (size < 1 + sizeof(JoinRequestMsg)) {
        return; // paquete corrupto/truncado, se ignora
    }
    JoinRequestMsg request{};
    std::memcpy(&request, data + 1, sizeof(JoinRequestMsg));
    request.playerName[kPlayerNameMaxLen] = '\0'; // por si vino sin terminador

    const int freeSlot = FindFreeSlot();
    if (request.protocolVersion != kProtocolVersion) {
        JoinRejectMsg reject{JoinRejectReason::ProtocolMismatch};
        ENetPacket* packet = MakePacket(MessageType::JoinReject, &reject, sizeof(reject));
        enet_peer_send(peer, 0, packet);
        enet_peer_disconnect_later(peer, 0);
        NetEvent ev;
        ev.type = NetEventType::ClientRejected;
        ev.rejectReason = JoinRejectReason::ProtocolMismatch;
        outEvents.push_back(ev);
        return;
    }
    if (freeSlot == -1) {
        JoinRejectMsg reject{JoinRejectReason::ServerFull};
        ENetPacket* packet = MakePacket(MessageType::JoinReject, &reject, sizeof(reject));
        enet_peer_send(peer, 0, packet);
        enet_peer_disconnect_later(peer, 0);
        NetEvent ev;
        ev.type = NetEventType::ClientRejected;
        ev.rejectReason = JoinRejectReason::ServerFull;
        outEvents.push_back(ev);
        return;
    }

    slotPeers_[static_cast<size_t>(freeSlot)] = peer;
    slots_[static_cast<size_t>(freeSlot)].connected = true;
    slots_[static_cast<size_t>(freeSlot)].name = request.playerName;

    JoinAcceptMsg accept{};
    accept.assignedSlot = static_cast<std::uint8_t>(freeSlot + 1); // slots 1-3, el host es 0
    CopyNameInto(accept.hostName, localName_);
    ENetPacket* packet = MakePacket(MessageType::JoinAccept, &accept, sizeof(accept));
    enet_peer_send(peer, 0, packet);

    NetEvent ev;
    ev.type = NetEventType::ClientJoined;
    ev.slot = freeSlot + 1;
    ev.peerName = slots_[static_cast<size_t>(freeSlot)].name;
    outEvents.push_back(ev);
}

void NetHost::HandleClientPacket(const std::uint8_t* data, size_t size, std::vector<NetEvent>& outEvents) {
    if (size < 1) {
        return;
    }
    const MessageType type = static_cast<MessageType>(data[0]);
    if (type == MessageType::JoinAccept) {
        if (size < 1 + sizeof(JoinAcceptMsg)) {
            return;
        }
        JoinAcceptMsg accept{};
        std::memcpy(&accept, data + 1, sizeof(JoinAcceptMsg));
        accept.hostName[kPlayerNameMaxLen] = '\0';
        mySlot_ = accept.assignedSlot;

        NetEvent ev;
        ev.type = NetEventType::Accepted;
        ev.slot = mySlot_;
        ev.peerName = accept.hostName;
        outEvents.push_back(ev);
    } else if (type == MessageType::JoinReject) {
        if (size < 1 + sizeof(JoinRejectMsg)) {
            return;
        }
        JoinRejectMsg reject{};
        std::memcpy(&reject, data + 1, sizeof(JoinRejectMsg));

        NetEvent ev;
        ev.type = NetEventType::Rejected;
        ev.rejectReason = reject.reason;
        outEvents.push_back(ev);
    } else {
        // Cualquier otro tipo (lobby/snapshot/terreno): gameplay que Game
        // interpreta, no NetHost. Siempre viene del servidor (el cliente
        // solo tiene 1 peer posible), por eso slot=0 alcanza.
        NetEvent ev;
        ev.type = NetEventType::RawMessage;
        ev.slot = 0;
        ev.rawData.assign(data, data + size);
        outEvents.push_back(std::move(ev));
    }
}

bool NetHost::SendToServer(const std::uint8_t* data, size_t size, bool reliable) {
    if (role_ != NetRole::Client || serverPeer_ == nullptr) {
        return false;
    }
    ENetPacket* packet = enet_packet_create(data, size, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    return enet_peer_send(serverPeer_, reliable ? kReliableChannel : kUnreliableChannel, packet) == 0;
}

bool NetHost::SendToClient(int remoteSlot, const std::uint8_t* data, size_t size, bool reliable) {
    if (role_ != NetRole::Server || remoteSlot < 1 || remoteSlot > kMaxRemoteClients) {
        return false;
    }
    _ENetPeer* peer = slotPeers_[static_cast<size_t>(remoteSlot - 1)];
    if (peer == nullptr) {
        return false;
    }
    ENetPacket* packet = enet_packet_create(data, size, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    return enet_peer_send(peer, reliable ? kReliableChannel : kUnreliableChannel, packet) == 0;
}

void NetHost::Broadcast(const std::uint8_t* data, size_t size, bool reliable) {
    if (role_ != NetRole::Server) {
        return;
    }
    for (int i = 1; i <= kMaxRemoteClients; ++i) {
        SendToClient(i, data, size, reliable);
    }
}

void NetHost::Flush() {
    if (host_ != nullptr) {
        enet_host_flush(host_);
    }
}

std::vector<NetEvent> NetHost::Update() {
    std::vector<NetEvent> events;
    if (host_ == nullptr) {
        return events;
    }

    ENetEvent enetEvent;
    while (enet_host_service(host_, &enetEvent, 0) > 0) {
        switch (enetEvent.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                if (role_ == NetRole::Client) {
                    // Conexion ENet de bajo nivel lista: recien ahora se
                    // manda el JoinRequest (antes no habia peer con quien
                    // hablar). El handshake en si (Accepted/Rejected) llega
                    // en un Update() posterior, cuando el servidor responda.
                    JoinRequestMsg request{};
                    request.protocolVersion = kProtocolVersion;
                    CopyNameInto(request.playerName, localName_);
                    ENetPacket* packet = MakePacket(MessageType::JoinRequest, &request, sizeof(request));
                    enet_peer_send(serverPeer_, 0, packet);

                    NetEvent ev;
                    ev.type = NetEventType::Connected;
                    events.push_back(ev);
                }
                // Servidor: una conexion ENet nueva todavia no es un
                // "jugador" para Game — recien lo es si manda un
                // JoinRequest valido (ver ENET_EVENT_TYPE_RECEIVE, arriba).
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                if (role_ == NetRole::Server) {
                    HandleServerPacket(enetEvent.peer, enetEvent.packet->data, enetEvent.packet->dataLength, events);
                } else if (role_ == NetRole::Client) {
                    HandleClientPacket(enetEvent.packet->data, enetEvent.packet->dataLength, events);
                }
                enet_packet_destroy(enetEvent.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                if (role_ == NetRole::Server) {
                    const int slot = FindSlotForPeer(enetEvent.peer);
                    if (slot != -1) {
                        slotPeers_[static_cast<size_t>(slot)] = nullptr;
                        slots_[static_cast<size_t>(slot)] = RemoteSlot{};
                        NetEvent ev;
                        ev.type = NetEventType::ClientLeft;
                        ev.slot = slot + 1;
                        events.push_back(ev);
                    }
                    // Si no tenia slot asignado (se cayo antes de completar
                    // el handshake, o fue rechazado), no hay nada que
                    // avisarle a Game.
                } else if (role_ == NetRole::Client) {
                    NetEvent ev;
                    ev.type = NetEventType::Disconnected;
                    events.push_back(ev);
                }
                break;
            }
            case ENET_EVENT_TYPE_NONE:
            default:
                break;
        }
    }
    return events;
}

int NetHost::ConnectedCount() const {
    int count = (role_ == NetRole::Server) ? 1 : 0; // el host se cuenta a si mismo
    for (const RemoteSlot& slot : slots_) {
        if (slot.connected) {
            ++count;
        }
    }
    return count;
}

} // namespace bc::net
