#pragma once

// Modo LAN: crear una partida en la red local, unirse (con busqueda automatica) y jugar en red.
// Cada PC puede traer hasta 4 jugadores (por ejemplo 2 + 2, o 1 + 3). En total, la sala admite 4 jugadores (el anfitrion puede
// ampliarla hasta 8 para la batalla vs 3 contra 3 y 4 contra 4: SetCapacity).
// La sala puede abrirse ademas a internet (p2p, sin servidor): el anfitrion averigua su direccion publica (STUN), abre el puerto
// del router (UPnP) y muestra un CODIGO corto; los de afuera entran con el codigo y los de la red local la encuentran buscando.
// Los dos tipos de jugadores conviven en la misma sala.
//
// Protocolo sencillo sobre UDP, puerto 47800 (mensajes de texto separados por '|'):
//   buscar:     cliente -> broadcast        "BCNES1?"
//   anuncio:    anfitrion -> cliente        "BCNES1!|nombre|jugadores|maximo|version"
//   unirse:     cliente -> anfitrion        "BCNES1J|nombre|version"        (crea el primer jugador de esa PC)
//   respuesta:  anfitrion -> cliente        "BCNES1W|id"  /  "BCNES1F" (llena o ya empezo)  /  "BCNES1V" (version distinta)
//   sala:       anfitrion -> miembros       "BCNES1L|empezo|fuegoamigo|retraso|mascara|colores|n|nombre|color|listo|dueno|..."  (cada medio segundo)
//               (mascara/colores: quienes estaban al empezar la partida y de que color; dueno -1 = lugar libre)
//   mis jugadores: miembro -> anfitrion     "BCNES1P|k|color|listo"   (k = mi jugador local 0..3; si k es el siguiente, lo crea)
//                                           "BCNES1R|k"               (quita mi jugador local k, salvo el primero)
//   latido:     miembro -> anfitrion        "BCNES1H"  (cada segundo; sin latidos 6 s = se fue)
//   salir:      miembro -> anfitrion "BCNES1X"  /  anfitrion -> miembros "BCNES1C" (partida cerrada)
// Partida: pasos sincronizados ("lockstep"). Cada PC manda los botones de sus jugadores del cuadro f + RETRASO al anfitrion;
// cuando el anfitrion tiene los de todas las PC para un cuadro, manda los botones de los jugadores y cada equipo simula ese
// cuadro. Como el motor del juego es determinista, todos ven lo mismo.
//   entradas:   miembro -> anfitrion        "BCNES1I|recibidos|f0|hex..."   (2 hex por jugador local y cuadro)
//   cuadros:    anfitrion -> miembro        "BCNES1G|f0|n|hex..."           (16 hex por cuadro: botones de los 8 lugares)
//   control:    miembro -> anfitrion        "BCNES1K|cuadro|hash"           (comprobacion de sincronia)
//               anfitrion -> miembros       "BCNES1D"                       (se detecto una desincronizacion)

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace app {

struct LanGame {
    std::string name;          // nombre del anfitrion
    std::string address;       // IPv4 del anfitrion
    uint16_t port = 0;
    int players = 1;
    int maxPlayers = 4;
    double lastSeen = 0;       // segundos (reloj interno)
    bool compatible = true;    // misma version del juego (si no, la simulacion no coincidiria)
};

class Lan {
public:
    enum class State { Idle, Hosting, Searching, Joining, Joined, Closed, Full, Failed };
    static constexpr uint16_t kDefaultPort = 47800;
    static constexpr int kMaxPlayers = 8;               // lugares de jugador como maximo (la sala normal usa 4)
    static constexpr int kMaxLocal = 4;                 // jugadores por PC
    static constexpr int kMaxName = 12;
    static constexpr int kColorCount = 8;               // colores de tanque disponibles (los mismos que la seleccion local)
    static constexpr int kInputDelay = 4;               // cuadros de retraso de los botones en LAN (unos 66 ms)
    static constexpr int kOnlineInputDelay = 8;         // si la sala esta abierta a internet (unos 133 ms)

    // port = 0 usa el puerto por defecto (las pruebas usan otro).
    explicit Lan(uint16_t port = 0);
    ~Lan();
    Lan(const Lan&) = delete;
    Lan& operator=(const Lan&) = delete;

    // Identifica la version del programa: todos los equipos tienen que correr el mismo ejecutable.
    static std::string BuildId();
    // Huella de los niveles del modo principal que usa esta PC: se suma a la version, asi nadie juega con niveles distintos.
    static void SetLevelsTag(const std::string& tag);

    // Deja el nombre limpio: sin '|' ni caracteres de control, en mayusculas y de hasta kMaxName letras.
    static std::string CleanName(const std::string& name);

    // ---- sala ----
    bool Host(const std::string& myName);               // crear una partida: abre el puerto y responde a las busquedas
    bool Search();                                      // buscar partidas en la red local
    bool Join(const LanGame& game, const std::string& myName);   // unirse a una de las partidas encontradas
    // Unirse por internet con el codigo de la sala ("ABCD-EFGH-JKLM"). Devuelve false si el codigo es invalido.
    bool JoinCode(const std::string& code, const std::string& myName);
    void Leave();                                       // cierra todo (avisa a los demas)
    void Update();                                      // una vez por cuadro: recibe y envia

    State state() const { return state_; }
    const std::vector<LanGame>& Games() const { return games_; }
    const std::vector<std::string>& Players() const { return players_; }    // nombres de los jugadores de la sala
    const std::vector<int>& Colors() const { return colors_; }               // color de cada jugador (0..6, sin repetir)
    const std::vector<bool>& Ready() const { return ready_; }                // quienes ya eligieron y apretaron "listo"
    const std::vector<int>& Owners() const { return owners_; }               // a que PC pertenece cada jugador (0 = anfitrion)
    const std::string& HostName() const { return hostName_; }
    int MyId() const { return myId_; }                   // esta PC en la sala (0 = anfitrion)
    int MySlot() const { return myId_; }
    const std::vector<int>& MyPlayers() const { return mine_; }             // indices de los jugadores de esta PC, en orden local
    int LocalCount() const { return static_cast<int>(mine_.size()); }
    bool IsMine(int player) const;
    int LocalIndexOf(int player) const;                  // jugador local k de ese jugador (-1 si no es de esta PC)
    bool IsHost() const { return state_ == State::Hosting; }
    const std::string& Error() const { return error_; }

    // Cuantos jugadores admite la sala (4, o hasta 8 en la batalla vs): lo fija el anfitrion antes de abrir la sala y los demas lo reciben.
    void SetCapacity(int n) { capacity_ = n < 2 ? 2 : (n > kMaxPlayers ? kMaxPlayers : n); }
    int Capacity() const { return capacity_; }
    // Jugadores locales (varios por PC, hasta 4 por PC). k = jugador local 0..LocalCount()-1.
    bool AddLocalPlayer();                              // suma otro jugador de esta PC (si hay lugar)
    void RemoveLocalPlayer(int k);                      // lo quita (el primero no se puede quitar)
    // Color: pasa al siguiente (+1) o anterior (-1) que no use otro, y deja de estar listo.
    void CycleColor(int k, int direction);
    // Marca / desmarca "listo" (con el color que tiene).
    void ToggleReady(int k);
    void CycleMyColor(int direction) { CycleColor(0, direction); }
    void ToggleMyReady() { ToggleReady(0); }

    // ---- internet (solo anfitrion) ----
    enum class Internet { Off, Working, Ready, Failed };
    // Abre la sala a jugadores de afuera: averigua la direccion publica y abre el puerto del router; con `simulate` solo arma un
    // codigo para 127.0.0.1 (pruebas, sin tocar la red).
    void OpenToInternet(bool simulate = false);
    void CloseInternet();
    Internet InternetState() const { return internet_; }
    const std::string& RoomCode() const { return roomCode_; }
    const std::string& InternetNote() const { return internetNote_; }
    int InputDelay() const { return inputDelay_; }
    // Codigo de la sala para la misma red (la IP local en el mismo formato): sirve para entrar con "unirse con codigo" cuando la busqueda
    // no la encuentra (algunos routers y telefonos no dejan pasar los mensajes a toda la red).
    const std::string& LanCode() const { return lanCode_; }
    // Un codigo por cada red de esta PC (con mas de una red, el que sirve es el de la red en la que esta el otro equipo): ip y codigo.
    const std::vector<std::pair<std::string, std::string>>& LanCodes() const { return lanCodes_; }
    // Diagnostico de la busqueda: la IP de esta PC y cuantos avisos de salas llegaron (y de quien el ultimo).
    std::string SearchDiag() const;

    // ---- entrar a una partida ya empezada ----
    // Quien se une con la partida en marcha ve la sala, elige su color y toca "listo"; el anfitrion lo agrega en un cuadro
    // futuro (marca en el flujo de botones) y su tanque aparece. Mientras tanto su PC reproduce toda la partida desde el
    // principio (es determinista), asi que llega al mismo estado que los demas.
    bool LateJoiner() const { return lateJoiner_; }
    int StartMask() const { return startMask_; }          // jugadores que estaban al empezar (bit p)
    int StartColor(int p) const { return p >= 0 && p < kMaxPlayers ? startColors_[p] : 0; }
    int PresentPlayers() const;                           // jugadores de la sala (sin contar lugares libres)
    // Marcas en el flujo de botones (el bit Select no lo usa nadie, asi que no se confunden con botones):
    //   0x0C = ese jugador se fue;  0x04 | color << 4 = ese jugador entra con ese color.
    static constexpr uint8_t kGoneMark = 0x0C;
    static bool IsMark(uint8_t b) { return (b & 0x04) != 0; }
    static bool IsJoinMark(uint8_t b) { return (b & 0x0C) == 0x04; }
    static int JoinColor(uint8_t b) { return (b >> 4) & 0x0F; }

    // ---- partida ----
    bool StartGame(int friendlyFire);                   // solo el anfitrion: empieza la partida para todos
    bool Started() const { return started_; }
    int FriendlyFire() const { return friendly_; }       // (tambien lleva las opciones de la sala: modo batalla vs, dificultad, bombardeo y mapa)
    void SetOptions(int v) { friendly_ = v; }             // solo el anfitrion: lo que la sala muestra antes de empezar
    void SetNoLateJoin(bool v) { noLateJoin_ = v; }       // sin entrar con la partida en marcha (batalla vs)
    // ---- mapas propios compartidos ----
    // El anfitrion fija los mapas (cada uno es un texto) y la sala se los manda a todos hasta que cada PC confirma que los tiene.
    // Con mapas fijados no se puede empezar hasta que todos los tengan, ni entrar con la partida en marcha.
    void SetSharedMaps(const std::vector<std::string>& maps);
    const std::vector<std::string>& SharedMaps() const { return maps_; }       // anfitrion: los suyos; miembro: los recibidos
    bool SharedMapsComplete() const;                      // miembro: ya llegaron todos (anfitrion: true)
    bool AllMembersHaveMaps() const;                      // anfitrion: todas las PC de la sala los tienen
    int SharedMapsExpected() const { return mapTotal_; }  // cuantos mapas va a haber (lo dice el anfitrion; 0 = ninguno)
    int NumPlayers() const { return static_cast<int>(players_.size()); }
    // El juego pide el cuadro `f`: se puede simular cuando ya llegaron los botones de todos.
    bool FrameReady(int f) const;
    int FramesAvailable() const;                        // cuadros ya recibidos (hasta ahi se puede simular)
    void FrameInputs(int f, uint8_t out[8]) const;      // botones de los 8 lugares de jugador en el cuadro f
    // Botones locales (uno por jugador de esta PC): se piden hasta el cuadro simulado + retraso (una vez por cuadro del juego).
    void SubmitLocal(int simFrame, const uint8_t* buttons, int count);
    void SubmitLocal(int simFrame, uint8_t buttons) { SubmitLocal(simFrame, &buttons, 1); }
    // Comprobacion de sincronia (cada tanto, con el hash del estado del juego en ese cuadro).
    void ReportHash(int frame, uint32_t hash);
    bool Desynced() const { return desync_; }
    // ---- chat de voz ----
    // Paquetes de voz (ya codificados, ver voice.h): el miembro los manda al anfitrion y este los retransmite a los demas.
    void SendVoice(const std::vector<uint8_t>& packet);
    bool PopVoice(int& speaker, std::vector<uint8_t>& packet);       // llegado de la sala; speaker = id de PC (0 = anfitrion)
    bool Lost() const { return lost_; }                 // se perdio la conexion con el anfitrion (o la partida se cerro)

private:
    std::string LobbyMessage(bool started) const;
    void SendChoice(int k);
    void RecomputeMine();
    bool ColorUsedByOther(int color, int exceptPlayer) const;
    int FirstFreeColor() const;
    std::string UniqueName(const std::string& base) const;
    int AddPlayerFor(int ownerId, const std::string& base, int k);    // anfitrion: crea un jugador; -1 si no hay lugar
    std::vector<int> PlayersOf(int ownerId) const;
    void RemovePlayerAt(int index);
    void RemovePlayersOf(int ownerId);
    void UpdateInternet();
    struct Impl;
    Impl* impl_;
    State state_ = State::Idle;
    std::vector<LanGame> games_;
    std::vector<std::string> players_;
    std::vector<int> colors_;
    std::vector<bool> ready_;
    std::vector<int> owners_;
    std::vector<int> mine_;
    std::string hostName_, myName_, error_, roomCode_, internetNote_;
    Internet internet_ = Internet::Off;
    bool internetWanted_ = false;                       // la sala se abrio a internet (aunque todavia no haya codigo)
    int inputDelay_ = kInputDelay;
    double joinTimeout_ = 5.0;
    int myId_ = 0;
    bool started_ = false;
    int friendly_ = 0;
    bool noLateJoin_ = false;
    std::vector<std::string> maps_;
    std::vector<bool> mapGot_;
    int mapTotal_ = 0;
    std::string lanCode_;
    std::vector<std::pair<std::string, std::string>> lanCodes_;
    int diagSeen_ = 0;
    std::string diagFrom_;
    bool desync_ = false;
    bool lost_ = false;
    bool lateJoiner_ = false;
    bool seenLobby_ = false;                            // miembro: ya vi la sala antes de que empezara
    int startMask_ = 0;
    int startColors_[kMaxPlayers] = {0, 1, 2, 3, 4, 5, 6, 7};
    int capacity_ = 4;
};

} // namespace app
