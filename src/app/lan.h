#pragma once

// Modo LAN: crear una partida en la red local, unirse (con busqueda automatica) y jugar en red.
//
// Protocolo sencillo sobre UDP, puerto 47800 (mensajes de texto separados por '|'):
//   buscar:     cliente -> broadcast        "BCNES1?"
//   anuncio:    anfitrion -> cliente        "BCNES1!|nombre|jugadores|maximo|version"
//   unirse:     cliente -> anfitrion        "BCNES1J|nombre|version"
//   respuesta:  anfitrion -> cliente        "BCNES1W|ranura"  /  "BCNES1F" (llena o ya empezo)  /  "BCNES1V" (version distinta)
//   sala:       anfitrion -> miembros       "BCNES1L|empezo|fuegoamigo|n|nombre0|color0|listo0|nombre1|color1|listo1|..."  (cada medio segundo)
//   mi color:   miembro -> anfitrion        "BCNES1P|color|listo"   (el anfitrion valida que el color no lo use otro)
//   latido:     miembro -> anfitrion        "BCNES1H"  (cada segundo; 6 s sin latidos = se fue)
//   salir:      miembro -> anfitrion "BCNES1X"  /  anfitrion -> miembros "BCNES1C" (partida cerrada)
// Partida: pasos sincronizados ("lockstep"). Cada jugador manda sus botones del cuadro f + RETRASO al anfitrion; cuando el
// anfitrion tiene los de todos para un cuadro, manda los botones de todos y cada equipo simula ese cuadro. Como el motor del
// juego es determinista, todos ven lo mismo.
//   entradas:   miembro -> anfitrion        "BCNES1I|recibidos|f0|hex..."   (4 hex por cuadro: botones de ese jugador)
//   cuadros:    anfitrion -> miembro        "BCNES1G|f0|n|hex..."           (8 hex por cuadro: botones de los 4 jugadores)
//   control:    miembro -> anfitrion        "BCNES1K|cuadro|hash"           (comprobacion de sincronia)
//               anfitrion -> miembros       "BCNES1D"                       (se detecto una desincronizacion)

#include <array>
#include <cstdint>
#include <string>
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
    static constexpr int kMaxPlayers = 4;
    static constexpr int kMaxName = 12;
    static constexpr int kColorCount = 7;               // colores de tanque disponibles (los mismos que la seleccion local)
    static constexpr int kInputDelay = 4;               // cuadros de retraso de los botones (unos 66 ms)

    // port = 0 usa el puerto por defecto (las pruebas usan otro).
    explicit Lan(uint16_t port = 0);
    ~Lan();
    Lan(const Lan&) = delete;
    Lan& operator=(const Lan&) = delete;

    // Identifica la version del programa: todos los equipos tienen que correr el mismo ejecutable.
    static std::string BuildId();

    // Deja el nombre limpio: sin '|' ni caracteres de control, en mayusculas y de hasta kMaxName letras.
    static std::string CleanName(const std::string& name);

    // ---- sala ----
    bool Host(const std::string& myName);               // crear una partida: abre el puerto y responde a las busquedas
    bool Search();                                      // buscar partidas en la red local
    bool Join(const LanGame& game, const std::string& myName);   // unirse a una de las partidas encontradas
    void Leave();                                       // cierra todo (avisa a los demas)
    void Update();                                      // una vez por cuadro: recibe y envia

    State state() const { return state_; }
    const std::vector<LanGame>& Games() const { return games_; }
    const std::vector<std::string>& Players() const { return players_; }    // nombres en la sala (el anfitrion primero)
    const std::vector<int>& Colors() const { return colors_; }               // color de cada jugador (0..6, sin repetir)
    const std::vector<bool>& Ready() const { return ready_; }                // quienes ya eligieron y apretaron "listo"
    // Mi color: pasa al siguiente (+1) o anterior (-1) que no use otro, y vuelvo a no estar listo.
    void CycleMyColor(int direction);
    // Marca / desmarca "listo" (con el color que tengo).
    void ToggleMyReady();
    const std::string& HostName() const { return hostName_; }
    int MySlot() const { return mySlot_; }               // 0 = anfitrion
    bool IsHost() const { return state_ == State::Hosting; }
    const std::string& Error() const { return error_; }

    // ---- partida ----
    bool StartGame(int friendlyFire);                   // solo el anfitrion: empieza la partida para todos
    bool Started() const { return started_; }
    int FriendlyFire() const { return friendly_; }
    int NumPlayers() const { return static_cast<int>(players_.size()); }
    // El juego pide el cuadro `f`: se puede simular cuando ya llegaron los botones de todos.
    bool FrameReady(int f) const;
    void FrameInputs(int f, uint8_t out[4]) const;      // botones de los 4 jugadores en el cuadro f
    // Botones locales: se piden hasta el cuadro simulado + retraso (se llama una vez por cuadro del juego).
    void SubmitLocal(int simFrame, uint8_t buttons);
    // Comprobacion de sincronia (cada tanto, con el hash del estado del juego en ese cuadro).
    void ReportHash(int frame, uint32_t hash);
    bool Desynced() const { return desync_; }
    bool Lost() const { return lost_; }                 // se perdio la conexion con el anfitrion (o la partida se cerro)

private:
    std::string LobbyMessage(bool started) const;
    void SendMyChoice();
    bool ColorUsedByOther(int color, int exceptSlot) const;
    struct Impl;
    Impl* impl_;
    State state_ = State::Idle;
    std::vector<LanGame> games_;
    std::vector<std::string> players_;
    std::vector<int> colors_;
    std::vector<bool> ready_;
    std::string hostName_, myName_, error_;
    int mySlot_ = 0;
    bool started_ = false;
    int friendly_ = 0;
    bool desync_ = false;
    bool lost_ = false;
};

} // namespace app
