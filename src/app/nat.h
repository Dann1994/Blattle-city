#pragma once

// Ayudas para jugar por internet sin servidor ("p2p"): descubrir la direccion publica (STUN) y abrir el puerto del router
// (UPnP). Con eso el anfitrion arma un codigo corto con su IP publica y su puerto, y los demas se conectan directo a el.

#include <cstdint>
#include <string>
#include <vector>

namespace app {

struct PublicEndpoint {
    bool ok = false;
    std::string ip;            // IPv4 publica
    uint16_t port = 0;         // puerto publico con el que sale el socket (STUN)
    std::string error;
};

// STUN (descubrir la direccion publica de un socket UDP). El juego manda el pedido desde SU socket (para que el puerto que ve el
// servidor sea el que van a usar los demas) y lee la respuesta en su bucle normal; aca solo estan las piezas:
// - StunServers(): resuelve (DNS, puede tardar) las direcciones de los servidores publicos;
// - StunRequest(): arma el pedido de 20 bytes con un identificador;
// - StunParse(): lee la respuesta (false si no es una respuesta STUN a ese pedido).
struct StunServer {
    std::string ip;
    uint16_t port = 0;
};
std::vector<StunServer> StunServers();
std::string StunRequest(const uint8_t transaction[12]);
bool StunParse(const char* data, int size, const uint8_t transaction[12], PublicEndpoint& out);

struct UpnpResult {
    bool ok = false;           // el router acepto abrir el puerto
    std::string externalIp;    // IP publica segun el router (puede estar vacia)
    std::string error;
};

// Le pide al router (UPnP) que reenvie el puerto UDP externo `port` a este equipo. Bloquea unos segundos.
UpnpResult UpnpOpenUdp(uint16_t port, const std::string& description);
// Solo mira si hay un router con UPnP (no cambia nada).
std::string UpnpProbe(bool& found);
// Cierra el puerto abierto con UpnpOpenUdp.
void UpnpCloseUdp(uint16_t port);

// IPv4 de este equipo en la red local (la de la salida a internet); vacia si no se puede saber.
std::string LocalAddress();

// ---- codigo de sala: IP publica + puerto -> 12 caracteres ("ABCD-EFGH-JKLM") ----
std::string MakeRoomCode(const std::string& ip, uint16_t port);
bool ParseRoomCode(const std::string& code, std::string& ip, uint16_t& port);   // acepta minusculas, espacios y guiones

} // namespace app
