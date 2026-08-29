#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace bc::net {

// Fase 7 (seccion 11 del documento de diseno). Protocolo completo: union
// (handshake), lobby en red (seleccion de personaje distribuida), y
// gameplay real (input del cliente, snapshot del mundo, terreno).

constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::uint16_t kDefaultGamePort = 7777;   // puerto ENet (host/join)
constexpr int kMaxRemoteClients = 3;               // hasta 3 clientes + el host = 4 jugadores
constexpr int kPlayerNameMaxLen = 31;              // 31 + terminador nulo
constexpr int kMaxLocalSlotsPerPeer = 4;           // seccion 11.4: hasta 4 jugadores locales por PC conectada

enum class MessageType : std::uint8_t {
    JoinRequest = 1,
    JoinAccept = 2,
    JoinReject = 3,
    LobbyClaim = 4,       // cliente -> servidor: un dispositivo local suyo quiere unirse/salir/cambiar color
    LobbySlotUpdate = 5,  // servidor -> clientes: estado completo de los 4 casilleros del lobby
    MatchStart = 6,       // servidor -> clientes: arranca la partida de verdad
    PlayerInputBatch = 7, // cliente -> servidor, no confiable: input de cada jugador local suyo, este paso fijo
    WorldSnapshot = 8,    // servidor -> clientes, no confiable: estado del mundo, este paso fijo
    TerrainSync = 9,      // servidor -> clientes, confiable: la grilla de terreno entera
    ServerClosing = 10,   // servidor -> clientes, confiable: se cierra la partida, volver prolijo al menu
};

enum class JoinRejectReason : std::uint8_t {
    ServerFull = 1,
    ProtocolMismatch = 2,
};

// Cliente -> Servidor: pedido de union (primer paquete que manda el
// cliente apenas ENet confirma la conexion de bajo nivel).
struct JoinRequestMsg {
    std::uint16_t protocolVersion = kProtocolVersion;
    char playerName[kPlayerNameMaxLen + 1] = {};
};

// Servidor -> Cliente: union aceptada. assignedSlot es 1-3 (0 es siempre el
// host, que ya esta "adentro" sin pasar por este handshake). Este es el
// slot de RED (que ENetPeer es), no un casillero del lobby — un mismo
// cliente puede despues aportar hasta kMaxLocalSlotsPerPeer jugadores
// locales, todos bajo esta misma conexion (ver LobbyClaimMsg).
struct JoinAcceptMsg {
    std::uint8_t assignedSlot = 0;
    char hostName[kPlayerNameMaxLen + 1] = {};
};

// Servidor -> Cliente: union rechazada (no habia lugar, o version de
// protocolo distinta — dos builds del juego que no se entienden entre si).
struct JoinRejectMsg {
    JoinRejectReason reason = JoinRejectReason::ServerFull;
};

// Lobby en red (seleccion de personaje distribuida, ver
// Game::ProcessCharacterSelectInput): cada PC conectada (host o cliente)
// tiene hasta kMaxLocalSlotsPerPeer dispositivos de input LOCALES propios
// (su propio teclado + hasta 3 mandos, igual criterio que el lobby local de
// siempre, ver IsSlotShootPressed/IsSlotLeftPressed etc.) que compiten por
// los 4 CASILLEROS globales del lobby (los 4 cuadros en pantalla). El
// casillero 0 (Jugador 1) siempre es del host (igual que hoy en modo
// local); los casilleros 1-3 los puede tomar CUALQUIER dispositivo local,
// sea del host o de cualquier cliente conectado, a quien primero apriete
// Disparar.
enum class LobbyClaimAction : std::uint8_t { Join = 1, Leave = 2, CycleLeft = 3, CycleRight = 4 };

// Cliente -> Servidor: un dispositivo local del cliente (localSlotIndex,
// 0-3, indexado igual que GamepadIdForSlot/playerBindings_ pero del lado
// de ESE cliente) quiere unirse/salir/cambiar de color. El servidor decide
// que casillero global le corresponde (ver Game::slotOwner_) y rebroadcastea
// LobbySlotUpdateMsg con el resultado.
struct LobbyClaimMsg {
    std::uint8_t localSlotIndex = 0;
    LobbyClaimAction action = LobbyClaimAction::Join;
};

struct LobbySlotEntry {
    std::uint8_t active = 0;         // bool como byte: mas simple de empaquetar que un bitfield
    std::uint8_t colorIndex = 0;     // 0-3, ver Game::playerColorIndex_
    std::uint8_t ownerPeerSlot = 0;  // 0 = host, 1-3 = ese cliente remoto (ver NetHost::Slot); solo valido si active
    std::uint8_t ownerLocalIndex = 0; // 0-3, indice de dispositivo local EN LA PC dueña, para que cada cliente sepa si un casillero es "suyo"
};

// Servidor -> Clientes: estado completo de los 4 casilleros, se re-manda
// entero cada vez que cambia algo (unirse/salir/cambiar color, de
// cualquier PC) — no hace falta un diff, son 8 bytes.
struct LobbySlotUpdateMsg {
    LobbySlotEntry slots[4];
};

// Servidor -> Clientes: cierra el lobby y arranca la partida de verdad. El
// primer TerrainSyncMsg (con el mapa inicial) llega aparte, inmediatamente
// despues de este mismo mensaje.
struct MatchStartMsg {};

// Cliente -> Servidor, no confiable, un paquete por paso fijo mientras la
// partida esta en curso: el input de cada uno de sus jugadores locales
// activos. entries[i] usa PlayerInput tal cual (Tank.h, 6 bools) — se
// declara aca en vez de incluir Tank.h para no acoplar este header (que
// tambien usa el editor/tools en el futuro) al resto del juego; el layout
// en bytes es identico (6 bools seguidos) asi que un memcpy directo entre
// los dos tipos es seguro.
struct WireInputEntry {
    std::uint8_t localSlotIndex = 0; // 0-3
    bool moveUp = false;
    bool moveDown = false;
    bool moveLeft = false;
    bool moveRight = false;
    bool shoot = false;
    bool specialShoot = false;
};

// Arma el payload completo (byte de tipo + cantidad + entries) de un
// PlayerInputBatchMsg. Devuelto ya listo para mandar via NetHost.
inline std::vector<std::uint8_t> BuildPlayerInputBatchPacket(const std::vector<WireInputEntry>& entries) {
    std::vector<std::uint8_t> buf;
    buf.reserve(2 + entries.size() * sizeof(WireInputEntry));
    buf.push_back(static_cast<std::uint8_t>(MessageType::PlayerInputBatch));
    buf.push_back(static_cast<std::uint8_t>(entries.size()));
    for (const WireInputEntry& e : entries) {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&e);
        buf.insert(buf.end(), bytes, bytes + sizeof(WireInputEntry));
    }
    return buf;
}

// Empaqueta un mensaje de payload fijo (todo menos PlayerInputBatch/
// WorldSnapshot/TerrainSync, que tienen conteos variables y se arman a
// mano donde se usan, ver Game.cpp).
template <typename T>
std::vector<std::uint8_t> PackMessage(MessageType type, const T& payload) {
    std::vector<std::uint8_t> buf(1 + sizeof(T));
    buf[0] = static_cast<std::uint8_t>(type);
    std::memcpy(buf.data() + 1, &payload, sizeof(T));
    return buf;
}

// Vuelca el payload (bytes despues del byte de tipo) de un mensaje de
// tamaño fijo a un struct. Devuelve false si el paquete vino corto/
// corrupto (payload truncado).
template <typename T>
bool UnpackMessage(const std::uint8_t* data, size_t size, T& outPayload) {
    if (size < 1 + sizeof(T)) {
        return false;
    }
    std::memcpy(&outPayload, data + 1, sizeof(T));
    return true;
}

// WorldSnapshotMsg (servidor -> clientes, no confiable, un paquete por paso
// fijo): todo lo que Render() necesita para dibujar el mundo, sin que el
// cliente simule nada (ver Game::SerializeWorldSnapshot/ApplyWorldSnapshot).
// #pragma pack(1): estos structs se mandan tal cual por la red (memcpy
// directo), sin relleno de por medio — host y cliente son siempre el mismo
// ejecutable/arquitectura, asi que alcanza con esto (no hace falta un
// formato portable entre plataformas distintas).
#pragma pack(push, 1)
struct WireTankState {
    float x = 0.0f;
    float y = 0.0f;
    std::uint8_t facing = 0;
    std::uint8_t animFrame = 0;
    std::uint8_t weaponLevel = 1;
    std::uint8_t lives = 0;
    float shieldSeconds = 0.0f;
    std::int32_t shieldHealth = 0;
    std::uint8_t frozen = 0;
    std::uint8_t eliminated = 0;
    std::uint8_t active = 0;
};

struct WireBulletEntry {
    float x = 0.0f;
    float y = 0.0f;
    std::uint8_t direction = 0;
    std::int32_t ownerId = -1;
    std::uint8_t weaponLevel = 1;
    std::uint8_t isSpecial = 0;
    std::int32_t health = 1;
};

// Estado minimo de un enemigo para dibujarlo (posicion/facing/animacion/
// escudo/congelado) — deliberadamente NO manda todo el estado interno de
// IA (rng, personalidad, temporizadores de decision, etc, ver Enemy.h):
// esos solo le importan al servidor, que es el unico que decide como se
// mueve cada enemigo.
struct WireEnemyEntry {
    std::int32_t ownerId = -1;
    float x = 0.0f;
    float y = 0.0f;
    std::uint8_t facing = 0;
    std::uint8_t animFrame = 0;
    std::int32_t hp = 1;
    float shieldSeconds = 0.0f;
    std::int32_t shieldHealth = 0;
    std::uint8_t frozen = 0;
};

// Cabecera fija: los 4 tanques + puntajes/nivel, mas cuantas entradas
// variables vienen despues (balas + las 4 listas de enemigos, en ese
// orden: basico, rapido, blindado, power).
struct WorldSnapshotHeader {
    std::int32_t currentLevel = 1;
    std::int32_t scores[4] = {0, 0, 0, 0};
    std::int32_t highScore = 0;
    std::int32_t enemiesKilledThisLevel = 0; // para el HUD de "enemigos restantes" (ver EnemiesRemainingThisLevel)
    std::int32_t enemiesSpawnedThisLevel = 0;
    WireTankState tanks[4];
    std::uint16_t bulletCount = 0;
    std::uint16_t enemyCounts[4] = {0, 0, 0, 0};
};
#pragma pack(pop)

} // namespace bc::net
