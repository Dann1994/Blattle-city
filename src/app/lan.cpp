#include "lan.h"

#include "nat.h"
#include "voice.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <atomic>
#include <map>
#include <memory>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SockLen = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
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

#ifdef _WIN32
struct WinsockInit {
    WinsockInit() { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
    ~WinsockInit() { WSACleanup(); }
};
void EnsureSockets() { static WinsockInit init; }
#else
void EnsureSockets() {}
#endif

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

std::string Hex(uint8_t v) {
    static const char* d = "0123456789ABCDEF";
    std::string s;
    s.push_back(d[v >> 4]);
    s.push_back(d[v & 15]);
    return s;
}

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Lee `n` bytes en hexadecimal desde pos; devuelve false si el texto es corto o invalido.
bool ReadHex(const std::string& s, size_t pos, int n, uint8_t* out) {
    if (pos + static_cast<size_t>(n) * 2 > s.size()) return false;
    for (int i = 0; i < n; ++i) {
        const int hi = HexVal(s[pos + static_cast<size_t>(i) * 2]), lo = HexVal(s[pos + static_cast<size_t>(i) * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>(hi * 16 + lo);
    }
    return true;
}

int ToInt(const std::string& s, int def = 0) { return s.empty() ? def : std::atoi(s.c_str()); }

constexpr double kMemberTimeout = 6.0;
constexpr int kMaxFramesPerPacket = 50;

} // namespace

// Lo que hace el hilo de fondo al abrir la sala a internet (DNS de STUN y UPnP: pueden tardar varios segundos).
struct NatJob {
    std::atomic<bool> done{false};
    std::atomic<bool> cancelled{false};
    std::vector<StunServer> servers;
    UpnpResult upnp;
    uint16_t port = 0;
};

struct Lan::Impl {
    struct Member {
        sockaddr_in addr{};
        int id = 0;                        // identificador de esta PC en la sala (estable aunque otras se vayan)
        std::string baseName;
        double lastSeen = 0;
        int ack = 0;                       // cuadros recibidos por esa PC (sin huecos)
        bool active = true;
        int bit = 0;                       // bit de esta PC en la mascara de cuadros (se fija al empezar)
        std::vector<int> players;          // sus jugadores (se fija al empezar o al entrar)
        bool inGame = true;                // false: espectador que todavia no entro a la partida en marcha
        int joinFrame = 0;                 // primer cuadro en que se le piden botones
        bool mapsOk = false;               // ya tiene todos los mapas compartidos
    };
    double lastMapSend = 0;
    std::deque<std::pair<int, std::vector<uint8_t>>> voiceIn;     // voz recibida (hablante, paquete)
    size_t nextMap = 0;

    SOCKET sock = INVALID_SOCKET;
    SOCKET beacon = INVALID_SOCKET;        // buscando: escucha los avisos que el anfitrion manda a toda la red (puerto + 1)
    double lastBeacon = 0;
    std::vector<std::string> localIps;    // buscando: las IP de esta PC (una sala con una de ellas esta en esta misma PC: se entra por 127.0.0.1)
    uint16_t port = Lan::kDefaultPort;
    sockaddr_in hostAddr{};                // miembro: direccion del anfitrion
    std::vector<Member> members;           // anfitrion: las demas PC
    int nextId = 1;
    int slotJoinFrame[Lan::kMaxPlayers] = {-1, -1, -1, -1, -1, -1, -1, -1};    // anfitrion: cuadro en que entra el jugador de ese lugar (-1: ninguno)
    int slotJoinColor[Lan::kMaxPlayers] = {};
    double lastLobby = 0, lastSearch = 0, lastBeat = 0, lastHeard = 0, joinStart = 0, lastJoin = 0;
    // miembro: lo que pidio cada jugador local (se reenvia unos segundos hasta que el anfitrion lo confirme)
    int wantColor[Lan::kMaxLocal] = {0, 0, 0, 0};
    bool wantReady[Lan::kMaxLocal] = {false, false, false, false};
    double wantUntil[Lan::kMaxLocal] = {0, 0, 0, 0};

    // partida
    std::vector<std::array<uint8_t, Lan::kMaxPlayers>> in;   // botones por cuadro y jugador
    std::vector<uint8_t> have;                // anfitrion: bits de PC con dato; miembro: 1 si llego el cuadro
    std::vector<uint8_t> own;                 // miembro: mis botones por cuadro y jugador local
    std::vector<int> hostPlayers;             // anfitrion: sus jugadores (al empezar)
    int localCount = 0;                       // cuantos jugadores locales tengo en la partida
    int broadcastNext = 0;                    // anfitrion: primer cuadro todavia incompleto
    int contiguous = 0;                       // miembro: cuadros recibidos sin huecos
    int submitted = 0;                        // proximo cuadro propio por pedir
    std::map<int, uint32_t> hostHash, memberHash;

    // internet
    std::shared_ptr<NatJob> job;
    bool jobCollected = false;
    uint8_t stunTid[12] = {};
    double stunStart = 0, stunLast = 0;
    PublicEndpoint stunResult;
    bool upnpOpened = false;
    double lastRenew = 0;

    void CloseBeacon() {
        if (beacon != INVALID_SOCKET) closesocket(beacon);
        beacon = INVALID_SOCKET;
    }

    bool OpenBeacon(uint16_t bindPort) {
        EnsureSockets();
        CloseBeacon();
        beacon = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (beacon == INVALID_SOCKET) return false;
#ifdef _WIN32
        u_long nb = 1;
        ioctlsocket(beacon, FIONBIO, &nb);
#else
        fcntl(beacon, F_SETFL, fcntl(beacon, F_GETFL, 0) | O_NONBLOCK);
#endif
        int yes = 1;
        setsockopt(beacon, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons(bindPort);
        if (::bind(beacon, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
            CloseBeacon();
            return false;
        }
        return true;
    }

    // Direcciones de difusion de cada red de esta PC (wifi, cable...): la difusion general (255.255.255.255) sale por una sola.
    static std::vector<std::string> Broadcasts() {
        std::vector<std::string> v;
        for (const auto& i : LocalInterfaces()) v.push_back(i.broadcast);
        return v;
    }

    void Close() {
        CloseBeacon();
        if (sock != INVALID_SOCKET) closesocket(sock);
        sock = INVALID_SOCKET;
    }

    bool Open(uint16_t bindPort) {
        EnsureSockets();
        Close();
        sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) return false;
#ifdef _WIN32
        u_long nb = 1;
        ioctlsocket(sock, FIONBIO, &nb);
#else
        fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof yes);
        if (bindPort != 0) setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons(bindPort);
        if (::bind(sock, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
            Close();
            return false;
        }
        return true;
    }

    void Send(const sockaddr_in& to, const std::string& msg) {
        if (sock == INVALID_SOCKET) return;
        ::sendto(sock, msg.data(), static_cast<int>(msg.size()), 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
    }

    static sockaddr_in Addr(const std::string& ip, uint16_t port) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &a.sin_addr);
        return a;
    }

    static std::string IpOf(const sockaddr_in& a) {
        char buf[64] = {};
        inet_ntop(AF_INET, const_cast<in_addr*>(&a.sin_addr), buf, sizeof buf);
        return buf;
    }

    static bool SameAddr(const sockaddr_in& a, const sockaddr_in& b) {
        return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
    }

    void EnsureFrame(size_t f) {
        if (in.size() <= f) {
            in.resize(f + 1, std::array<uint8_t, Lan::kMaxPlayers>{});
            have.resize(f + 1, 0);
        }
    }

    Member* Find(const sockaddr_in& a) {
        for (auto& m : members) {
            if (SameAddr(m.addr, a)) return &m;
        }
        return nullptr;
    }
};

// La version del programa para el juego en red: tiene que ser la misma en Windows y en Android (se compilan en momentos distintos),
// asi que es una constante que se cambia a mano cada vez que cambia la simulacion o el protocolo.
static std::string g_levelsTag;
void Lan::SetLevelsTag(const std::string& tag) { g_levelsTag = tag; }

std::string Lan::BuildId() {
    return "BCNES-2026.10.31-v112" + (g_levelsTag.empty() ? std::string() : "-" + g_levelsTag);
}

Lan::Lan(uint16_t port) : impl_(new Impl) {
    impl_->port = port != 0 ? port : kDefaultPort;
}

Lan::~Lan() {
    Leave();
    delete impl_;
}

std::string Lan::CleanName(const std::string& name) {
    std::string out;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        char up = static_cast<char>(std::toupper(u));
        const bool ok = (up >= 'A' && up <= 'Z') || (up >= '0' && up <= '9') || up == ' ' || up == '-' || up == '_' || up == '.';
        if (ok && static_cast<int>(out.size()) < kMaxName && !(up == ' ' && out.empty())) out.push_back(up);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

// ---------------------------------------------------------------- jugadores de la sala

bool Lan::IsMine(int player) const { return LocalIndexOf(player) >= 0; }

int Lan::LocalIndexOf(int player) const {
    for (size_t k = 0; k < mine_.size(); ++k) {
        if (mine_[k] == player) return static_cast<int>(k);
    }
    return -1;
}

void Lan::RecomputeMine() {
    mine_ = PlayersOf(myId_);
}

std::vector<int> Lan::PlayersOf(int ownerId) const {
    std::vector<int> out;
    for (size_t i = 0; i < owners_.size(); ++i) {
        if (owners_[i] == ownerId) out.push_back(static_cast<int>(i));
    }
    return out;
}

bool Lan::ColorUsedByOther(int color, int exceptPlayer) const {
    for (size_t i = 0; i < colors_.size(); ++i) {
        if (static_cast<int>(i) != exceptPlayer && colors_[i] == color) return true;
    }
    return false;
}

int Lan::FirstFreeColor() const {
    for (int c = 0; c < kColorCount; ++c) {
        if (!ColorUsedByOther(c, -1)) return c;
    }
    return 0;
}

std::string Lan::UniqueName(const std::string& base) const {
    std::string name = base.empty() ? "JUGADOR" : base;
    for (int tries = 2; tries < 20; ++tries) {
        bool dup = false;
        for (const auto& n : players_) dup = dup || n == name;
        if (!dup) break;
        name = CleanName(base.substr(0, static_cast<size_t>(kMaxName - 3))) + " " + std::to_string(tries);
    }
    return name;
}

// Anfitrion: crea el jugador k de la PC `ownerId` (el primero con su nombre; los otros con un numero).
int Lan::AddPlayerFor(int ownerId, const std::string& base, int k) {
    int slot = -1;
    for (size_t i = 0; i < owners_.size(); ++i) {                        // con la partida en marcha se reusa un lugar libre
        if (owners_[i] < 0) { slot = static_cast<int>(i); break; }
    }
    if (slot < 0 && static_cast<int>(players_.size()) >= capacity_) return -1;
    std::string name = base;
    if (k > 0) name = CleanName(base.substr(0, static_cast<size_t>(kMaxName - 2))) + " " + std::to_string(k + 1);
    name = UniqueName(name);
    if (slot < 0) {
        players_.push_back(name);
        colors_.push_back(FirstFreeColor());
        ready_.push_back(false);
        owners_.push_back(ownerId);
        return static_cast<int>(players_.size()) - 1;
    }
    players_[static_cast<size_t>(slot)] = name;
    colors_[static_cast<size_t>(slot)] = FirstFreeColor();
    ready_[static_cast<size_t>(slot)] = false;
    owners_[static_cast<size_t>(slot)] = ownerId;
    return slot;
}

void Lan::RemovePlayerAt(int index) {
    if (index < 0 || static_cast<size_t>(index) >= players_.size()) return;
    if (started_) {                                                      // en partida el lugar queda libre (no se corren los demas)
        players_[static_cast<size_t>(index)].clear();
        colors_[static_cast<size_t>(index)] = -1;
        ready_[static_cast<size_t>(index)] = false;
        owners_[static_cast<size_t>(index)] = -1;
        return;
    }
    players_.erase(players_.begin() + index);
    colors_.erase(colors_.begin() + index);
    ready_.erase(ready_.begin() + index);
    owners_.erase(owners_.begin() + index);
}

int Lan::PresentPlayers() const {
    int n = 0;
    for (int o : owners_) n += o >= 0 ? 1 : 0;
    return n;
}

void Lan::RemovePlayersOf(int ownerId) {
    for (size_t i = players_.size(); i-- > 0;) {
        if (owners_[i] == ownerId) RemovePlayerAt(static_cast<int>(i));
    }
}

// ---------------------------------------------------------------- sala

bool Lan::Host(const std::string& myName) {
    Leave();
    myName_ = CleanName(myName);
    if (myName_.empty()) { error_ = "NOMBRE VACÍO"; state_ = State::Failed; return false; }
    if (!impl_->Open(impl_->port)) { error_ = "NO SE PUDO ABRIR EL PUERTO DE RED"; state_ = State::Failed; return false; }
    state_ = State::Hosting;
    lanCodes_.clear();
    for (const auto& i : LocalInterfaces()) lanCodes_.push_back({i.ip, MakeRoomCode(i.ip, impl_->port)});
    if (lanCodes_.empty()) {
        const std::string ip = LocalAddress();
        if (!ip.empty()) lanCodes_.push_back({ip, MakeRoomCode(ip, impl_->port)});
    }
    lanCode_ = lanCodes_.empty() ? std::string() : lanCodes_[0].second;
    impl_->lastBeacon = 0;
    players_.clear();
    colors_.clear();
    ready_.clear();
    owners_.clear();
    myId_ = 0;
    AddPlayerFor(0, myName_, 0);
    RecomputeMine();
    hostName_ = myName_;
    impl_->members.clear();
    impl_->nextId = 1;
    return true;
}

std::string Lan::SearchDiag() const {
    std::string ips;
    for (const std::string& ip : impl_->localIps) ips += (ips.empty() ? "" : " ") + ip;
    std::string s = "MI IP " + (ips.empty() ? std::string("?") : ips) + "  AVISOS " + std::to_string(diagSeen_);
    if (!diagFrom_.empty()) s += " DE " + diagFrom_;
    return s;
}

bool Lan::Search() {
    diagSeen_ = 0;
    diagFrom_.clear();
    if (state_ == State::Hosting || state_ == State::Joined) return false;
    if (impl_->sock == INVALID_SOCKET && !impl_->Open(0)) { error_ = "NO SE PUDO ABRIR LA RED"; state_ = State::Failed; return false; }
    state_ = State::Searching;
    games_.clear();
    impl_->lastSearch = 0;                              // manda el primer pedido enseguida
    impl_->OpenBeacon(static_cast<uint16_t>(impl_->port + 1));      // y escucha los avisos de las salas
    impl_->localIps.clear();
    for (const auto& i : LocalInterfaces()) impl_->localIps.push_back(i.ip);
    return true;
}

bool Lan::JoinCode(const std::string& code, const std::string& myName) {
    std::string ip;
    uint16_t port = 0;
    if (!ParseRoomCode(code, ip, port)) { error_ = "CÓDIGO INVÁLIDO"; return false; }
    LanGame g;
    g.name = "ONLINE";
    g.address = ip;
    g.port = port;
    const bool ok = Join(g, myName);
    if (ok) joinTimeout_ = 9.0;                          // por internet puede tardar mas en contestar
    return ok;
}

bool Lan::Join(const LanGame& game, const std::string& myName) {
    joinTimeout_ = 5.0;
    myName_ = CleanName(myName);
    if (myName_.empty()) { error_ = "NOMBRE VACÍO"; return false; }
    if (impl_->sock == INVALID_SOCKET && !impl_->Open(0)) { error_ = "NO SE PUDO ABRIR LA RED"; state_ = State::Failed; return false; }
    impl_->CloseBeacon();                                // ya no se busca: no dejar el puerto de avisos ocupado
    impl_->hostAddr = Impl::Addr(game.address, game.port);
    hostName_ = game.name;
    state_ = State::Joining;
    impl_->joinStart = impl_->lastJoin = Now();
    impl_->lastHeard = Now();
    impl_->Send(impl_->hostAddr, "BCNES1J|" + myName_ + "|" + BuildId());
    players_.clear();
    colors_.clear();
    ready_.clear();
    owners_.clear();
    mine_.clear();
    started_ = false;
    lost_ = false;
    lateJoiner_ = false;
    seenLobby_ = false;
    return true;
}

void Lan::SendVoice(const std::vector<uint8_t>& packet) {
    if (impl_->sock == INVALID_SOCKET || packet.empty()) return;
    const std::string b64 = Base64Encode(packet.data(), packet.size());
    if (state_ == State::Hosting) {
        const std::string out = "BCNES1a|0|" + b64;
        for (const auto& m : impl_->members) impl_->Send(m.addr, out);
    } else if (state_ == State::Joined) {
        impl_->Send(impl_->hostAddr, "BCNES1A|" + b64);
    }
}

bool Lan::PopVoice(int& speaker, std::vector<uint8_t>& packet) {
    if (impl_->voiceIn.empty()) return false;
    speaker = impl_->voiceIn.front().first;
    packet = std::move(impl_->voiceIn.front().second);
    impl_->voiceIn.pop_front();
    return true;
}

void Lan::Leave() {
    impl_->voiceIn.clear();
    CloseInternet();
    if (impl_->sock != INVALID_SOCKET) {
        if (state_ == State::Hosting) {
            for (const auto& m : impl_->members) impl_->Send(m.addr, "BCNES1C");
        } else if (state_ == State::Joined || state_ == State::Joining) {
            impl_->Send(impl_->hostAddr, "BCNES1X");
        }
    }
    impl_->Close();
    impl_->members.clear();
    impl_->in.clear();
    impl_->have.clear();
    impl_->own.clear();
    impl_->hostPlayers.clear();
    impl_->localCount = impl_->broadcastNext = impl_->contiguous = impl_->submitted = 0;
    impl_->hostHash.clear();
    impl_->memberHash.clear();
    for (int k = 0; k < kMaxLocal; ++k) impl_->wantUntil[k] = 0;
    maps_.clear();
    mapGot_.clear();
    mapTotal_ = 0;
    noLateJoin_ = false;
    games_.clear();
    players_.clear();
    colors_.clear();
    ready_.clear();
    owners_.clear();
    mine_.clear();
    state_ = State::Idle;
    started_ = false;
    lateJoiner_ = false;
    seenLobby_ = false;
    startMask_ = 0;
    for (int i = 0; i < kMaxPlayers; ++i) { startColors_[i] = i % kColorCount; impl_->slotJoinFrame[i] = -1; }
    capacity_ = 4;
    desync_ = false;
    lost_ = false;
    myId_ = 0;
}

std::string Lan::LobbyMessage(bool started) const {
    std::ostringstream os;
    os << "BCNES1L|" << (started ? 1 : 0) << "|" << friendly_ << "|" << inputDelay_ << "|" << startMask_ << "|"
       << startColors_[0] << startColors_[1] << startColors_[2] << startColors_[3] << startColors_[4] << startColors_[5] << startColors_[6] << startColors_[7] << "|" << players_.size();
    for (size_t i = 0; i < players_.size(); ++i) {
        os << "|" << players_[i] << "|" << colors_[i] << "|" << (ready_[i] ? 1 : 0) << "|" << owners_[i];
    }
    os << "|" << maps_.size() << "|" << capacity_;                           // cuantos mapas propios va a mandar el anfitrion y cuantos jugadores admite la sala
    return os.str();
}

// Miembro: avisa al anfitrion como esta mi jugador local k (color y listo).
void Lan::SendChoice(int k) {
    if (state_ != State::Joined || (started_ && !lateJoiner_) || k < 0 || static_cast<size_t>(k) >= mine_.size()) return;
    const size_t p = static_cast<size_t>(mine_[static_cast<size_t>(k)]);
    if (p >= colors_.size()) return;
    const bool useWant = impl_->wantUntil[k] > Now();
    std::ostringstream os;
    os << "BCNES1P|" << k << "|" << (useWant ? impl_->wantColor[k] : colors_[p]) << "|" << ((useWant ? impl_->wantReady[k] : static_cast<bool>(ready_[p])) ? 1 : 0);
    impl_->Send(impl_->hostAddr, os.str());
}

bool Lan::AddLocalPlayer() {
    if ((started_ && !lateJoiner_) || PresentPlayers() >= capacity_) return false;
    if (state_ == State::Hosting) {
        if (AddPlayerFor(0, myName_, LocalCount()) < 0) return false;
        RecomputeMine();
        impl_->lastLobby = 0;
        return true;
    }
    if (state_ != State::Joined || LocalCount() >= kMaxLocal) return false;
    const int k = LocalCount();
    const int color = FirstFreeColor();
    std::string name = CleanName(myName_.substr(0, static_cast<size_t>(kMaxName - 2))) + " " + std::to_string(k + 1);
    {
        int slot = -1;
        for (size_t i = 0; i < owners_.size(); ++i) if (owners_[i] < 0) { slot = static_cast<int>(i); break; }
        if (slot < 0) {                                    // se ve enseguida; el anfitrion confirma
            players_.push_back(UniqueName(name));
            colors_.push_back(color);
            ready_.push_back(false);
            owners_.push_back(myId_);
        } else {
            players_[static_cast<size_t>(slot)] = UniqueName(name);
            colors_[static_cast<size_t>(slot)] = color;
            ready_[static_cast<size_t>(slot)] = false;
            owners_[static_cast<size_t>(slot)] = myId_;
        }
    }
    RecomputeMine();
    impl_->wantColor[k] = color;
    impl_->wantReady[k] = false;
    impl_->wantUntil[k] = Now() + 2.5;
    SendChoice(k);
    return true;
}

void Lan::RemoveLocalPlayer(int k) {
    if ((started_ && !lateJoiner_) || k < 1 || static_cast<size_t>(k) >= mine_.size()) return;
    const int index = mine_[static_cast<size_t>(k)];
    RemovePlayerAt(index);
    RecomputeMine();
    if (state_ == State::Hosting) {
        impl_->lastLobby = 0;
    } else if (state_ == State::Joined) {
        impl_->Send(impl_->hostAddr, "BCNES1R|" + std::to_string(k));
    }
}

void Lan::CycleColor(int k, int direction) {
    if ((started_ && !lateJoiner_) || (state_ != State::Hosting && state_ != State::Joined)) return;
    if (k < 0 || static_cast<size_t>(k) >= mine_.size()) return;
    const int p = mine_[static_cast<size_t>(k)];
    int c = colors_[static_cast<size_t>(p)];
    for (int i = 0; i < kColorCount; ++i) {
        c = ((c + direction) % kColorCount + kColorCount) % kColorCount;
        if (!ColorUsedByOther(c, p)) break;
    }
    colors_[static_cast<size_t>(p)] = c;
    ready_[static_cast<size_t>(p)] = false;
    if (state_ == State::Hosting) {
        impl_->lastLobby = 0;                                              // avisa enseguida
    } else {
        impl_->wantColor[k] = c;
        impl_->wantReady[k] = false;
        impl_->wantUntil[k] = Now() + 2.5;
        SendChoice(k);
    }
}

void Lan::ToggleReady(int k) {
    if ((started_ && !lateJoiner_) || (state_ != State::Hosting && state_ != State::Joined)) return;
    if (k < 0 || static_cast<size_t>(k) >= mine_.size()) return;
    const size_t p = static_cast<size_t>(mine_[static_cast<size_t>(k)]);
    ready_[p] = !ready_[p];
    if (state_ == State::Hosting) {
        impl_->lastLobby = 0;
    } else {
        impl_->wantColor[k] = colors_[p];
        impl_->wantReady[k] = ready_[p];
        impl_->wantUntil[k] = Now() + 2.5;
        SendChoice(k);
    }
}

// ---------------------------------------------------------------- partida

void Lan::SetSharedMaps(const std::vector<std::string>& maps) {
    maps_ = maps;
    mapTotal_ = static_cast<int>(maps.size());
    mapGot_.assign(maps.size(), true);
    noLateJoin_ = noLateJoin_ || !maps.empty();
    for (auto& m : impl_->members) m.mapsOk = false;
}

bool Lan::SharedMapsComplete() const {
    if (state_ == State::Hosting) return true;
    if (mapTotal_ <= 0) return true;
    for (int i = 0; i < mapTotal_; ++i) if (static_cast<size_t>(i) >= mapGot_.size() || !mapGot_[static_cast<size_t>(i)]) return false;
    return true;
}

bool Lan::AllMembersHaveMaps() const {
    if (maps_.empty()) return true;
    for (const auto& m : impl_->members) if (!m.mapsOk) return false;
    return true;
}

bool Lan::StartGame(int friendlyFire) {
    if (state_ != State::Hosting || started_) return false;
    started_ = true;
    friendly_ = friendlyFire;
    inputDelay_ = internetWanted_ ? kOnlineInputDelay : kInputDelay;     // por internet hace falta mas colchon
    impl_->hostPlayers = PlayersOf(0);
    impl_->localCount = static_cast<int>(impl_->hostPlayers.size());
    startMask_ = 0;
    for (int i = 0; i < kMaxPlayers; ++i) {
        startColors_[i] = static_cast<size_t>(i) < colors_.size() && colors_[static_cast<size_t>(i)] >= 0 ? colors_[static_cast<size_t>(i)] : i % kColorCount;
        if (static_cast<size_t>(i) < owners_.size() && owners_[static_cast<size_t>(i)] >= 0) startMask_ |= 1 << i;
    }
    int bit = 1;
    for (auto& m : impl_->members) {
        m.active = true;
        m.inGame = true;
        m.joinFrame = 0;
        m.bit = bit++;
        m.players = PlayersOf(m.id);
        m.ack = 0;
    }
    impl_->in.clear();
    impl_->have.clear();
    for (int f = 0; f < inputDelay_; ++f) {                // los primeros cuadros van sin botones, para todos
        impl_->EnsureFrame(static_cast<size_t>(f));
        impl_->have[static_cast<size_t>(f)] = 0xFF;
    }
    impl_->broadcastNext = inputDelay_;
    impl_->submitted = inputDelay_;
    for (const auto& m : impl_->members) impl_->Send(m.addr, LobbyMessage(true));    // avisa enseguida
    return true;
}

int Lan::FramesAvailable() const {
    if (!started_) return 0;
    return state_ == State::Hosting ? impl_->broadcastNext : impl_->contiguous;
}

bool Lan::FrameReady(int f) const {
    if (!started_ || f < 0) return false;
    if (state_ == State::Hosting) return f < impl_->broadcastNext;
    return f < impl_->contiguous;
}

void Lan::FrameInputs(int f, uint8_t out[8]) const {
    for (int i = 0; i < kMaxPlayers; ++i) out[i] = 0;
    if (f >= 0 && static_cast<size_t>(f) < impl_->in.size()) {
        for (int i = 0; i < kMaxPlayers; ++i) out[i] = impl_->in[static_cast<size_t>(f)][static_cast<size_t>(i)];
    }
}

void Lan::SubmitLocal(int simFrame, const uint8_t* buttons, int count) {
    if (!started_) return;
    while (impl_->submitted <= simFrame + inputDelay_) {
        const size_t f = static_cast<size_t>(impl_->submitted);
        if (state_ == State::Hosting) {
            impl_->EnsureFrame(f);
            for (int k = 0; k < impl_->localCount; ++k) {
                impl_->in[f][static_cast<size_t>(impl_->hostPlayers[static_cast<size_t>(k)])] = k < count ? buttons[k] : 0;
            }
            impl_->have[f] = static_cast<uint8_t>(impl_->have[f] | 1);
        } else {
            if (impl_->own.size() < (f + 1) * 4) impl_->own.resize((f + 1) * 4, 0);                  // 4 bytes por cuadro (hasta 4 jugadores por PC)
            for (size_t k = 0; k < 4; ++k) impl_->own[f * 4 + k] = static_cast<int>(k) < count ? buttons[k] : 0;
        }
        ++impl_->submitted;
    }
}

void Lan::ReportHash(int frame, uint32_t hash) {
    if (!started_) return;
    if (state_ == State::Hosting) {
        impl_->hostHash[frame] = hash;
        auto it = impl_->memberHash.find(frame);
        if (it != impl_->memberHash.end()) {
            if (it->second != hash) desync_ = true;
            impl_->memberHash.erase(it);
        }
        while (impl_->hostHash.size() > 64) impl_->hostHash.erase(impl_->hostHash.begin());
    } else if (state_ == State::Joined) {
        std::ostringstream os;
        os << "BCNES1K|" << frame << "|" << hash;
        impl_->Send(impl_->hostAddr, os.str());
    }
}

// ---------------------------------------------------------------- internet

void Lan::OpenToInternet(bool simulate) {
    if (state_ != State::Hosting || internet_ == Internet::Working || internet_ == Internet::Ready) return;
    internetWanted_ = true;
    roomCode_.clear();
    internetNote_.clear();
    if (simulate) {
        roomCode_ = MakeRoomCode("127.0.0.1", impl_->port);
        internet_ = Internet::Ready;
        internetNote_ = "SIMULADO";
        return;
    }
    internet_ = Internet::Working;
    internetNote_ = "BUSCANDO TU DIRECCIÓN PÚBLICA...";
    impl_->stunResult = PublicEndpoint{};
    impl_->jobCollected = false;
    impl_->stunStart = 0;
    for (int i = 0; i < 12; ++i) impl_->stunTid[i] = static_cast<uint8_t>(std::rand() & 0xFF);
    auto job = std::make_shared<NatJob>();
    job->port = impl_->port;
    impl_->job = job;
    std::thread([job]() {
        job->servers = StunServers();
        job->upnp = UpnpOpenUdp(job->port, "BattleCityNES");
        if (job->cancelled && job->upnp.ok) UpnpCloseUdp(job->port);       // se cerro la sala mientras tanto
        job->done = true;
    }).detach();
}

void Lan::CloseInternet() {
    if (impl_->job) {
        impl_->job->cancelled = true;
        if (impl_->job->done && impl_->upnpOpened) {
            const uint16_t port = impl_->port;
            std::thread([port]() { UpnpCloseUdp(port); }).detach();       // cierra el puerto del router sin trabar el juego
        }
        impl_->job.reset();
    }
    impl_->upnpOpened = false;
    internet_ = Internet::Off;
    internetWanted_ = false;
    roomCode_.clear();
    internetNote_.clear();
}

void Lan::UpdateInternet() {
    if (internet_ == Internet::Ready && impl_->upnpOpened && Now() - impl_->lastRenew > 3000.0) {     // el puerto del router se renueva cada ~50 min
        impl_->lastRenew = Now();
        const uint16_t port = impl_->port;
        std::thread([port]() { UpnpOpenUdp(port, "BattleCityNES"); }).detach();
    }
    if (internet_ != Internet::Working || !impl_->job) return;
    const double now = Now();
    if (!impl_->job->done) return;
    if (impl_->stunStart == 0) impl_->stunStart = now;
    const auto& job = *impl_->job;
    // pide la direccion publica desde el socket del juego (para que el puerto sea el correcto)
    if (!impl_->stunResult.ok && now - impl_->stunLast > 0.4) {
        impl_->stunLast = now;
        const std::string req = StunRequest(impl_->stunTid);
        for (const auto& sv : job.servers) impl_->Send(Impl::Addr(sv.ip, sv.port), req);
    }
    const bool stunTimeout = now - impl_->stunStart > 8.0;
    if (!impl_->stunResult.ok && !stunTimeout) return;
    impl_->upnpOpened = job.upnp.ok;
    impl_->lastRenew = Now();
    std::string ip = impl_->stunResult.ok ? impl_->stunResult.ip : job.upnp.externalIp;
    uint16_t port = impl_->port;
    if (!job.upnp.ok && impl_->stunResult.ok) port = impl_->stunResult.port;
    if (ip.empty()) {
        internet_ = Internet::Failed;
        internetNote_ = "SIN INTERNET: NO SE PUDO VER TU DIRECCIÓN";
        return;
    }
    roomCode_ = MakeRoomCode(ip, port);
    internet_ = Internet::Ready;
    if (job.upnp.ok) internetNote_ = "PUERTO ABIERTO EN EL ROUTER (UPNP)";
    else internetNote_ = "SIN UPNP: ABRÍ EL PUERTO UDP " + std::to_string(port) + " EN EL ROUTER";
    if (!job.upnp.ok && impl_->stunResult.ok && impl_->stunResult.port != impl_->port) internetNote_ = "SIN UPNP: TU ROUTER CAMBIA EL PUERTO, ABRÍ EL " + std::to_string(impl_->port);
}

// ---------------------------------------------------------------- red

void Lan::Update() {
    if (impl_->sock == INVALID_SOCKET) return;
    const double now = Now();

    // ---------------- recibir ----------------
    char buf[2048];
    for (int si = 0; si < 2; ++si) {
    const SOCKET cur = si == 0 ? impl_->sock : impl_->beacon;
    if (cur == INVALID_SOCKET) continue;
    for (;;) {
        sockaddr_in from{};
        SockLen fl = sizeof from;
        const int n = ::recvfrom(cur, buf, sizeof buf - 1, 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) break;
        const std::string msg(buf, static_cast<size_t>(n));
        if (internet_ == Internet::Working && !(msg.compare(0, 6, "BCNES1") == 0)) {          // respuesta de STUN a mi pedido
            PublicEndpoint pe;
            if (StunParse(buf, n, impl_->stunTid, pe)) impl_->stunResult = pe;
            continue;
        }
        if (msg.compare(0, 6, "BCNES1") != 0 || msg.size() < 7) continue;
        const char kind = msg[6];
        const std::vector<std::string> f = Split(msg, '|');

        if (state_ == State::Hosting) {
            Impl::Member* member = impl_->Find(from);
            if (kind == '?') {
                std::ostringstream os;
                os << "BCNES1!|" << myName_ << "|" << players_.size() << "|" << capacity_ << "|" << BuildId();
                impl_->Send(from, os.str());
            } else if (kind == 'J' && f.size() >= 2) {
                if (f.size() < 3 || f[2] != BuildId()) {
                    impl_->Send(from, "BCNES1V");                           // otra version del programa
                } else if (member) {                                        // ya estaba: repite la bienvenida
                    member->lastSeen = now;
                    impl_->Send(from, "BCNES1W|" + std::to_string(member->id));
                } else if (PresentPlayers() >= capacity_ || (started_ && noLateJoin_)) {
                    impl_->Send(from, "BCNES1F");
                } else {
                    Impl::Member m;
                    m.addr = from;
                    m.inGame = !started_;                                    // con la partida en marcha entra mirando hasta tocar "listo"
                    m.id = impl_->nextId++;
                    m.baseName = CleanName(f[1]);
                    if (m.baseName.empty()) m.baseName = "JUGADOR";
                    m.lastSeen = now;
                    impl_->members.push_back(m);
                    AddPlayerFor(m.id, m.baseName, 0);
                    impl_->Send(from, "BCNES1W|" + std::to_string(m.id));
                    impl_->lastLobby = 0;                                    // avisa a todos ya
                }
            } else if (member) {
                member->lastSeen = now;
                if (kind == 'H' && f.size() >= 2) member->mapsOk = f[1] == "1";
                if (kind == 'A' && f.size() >= 2) {                                    // voz de un miembro: la oigo y la mando a los demas
                    std::vector<uint8_t> pk;
                    if (Base64Decode(f[1], pk) && !pk.empty() && pk.size() < 700) {
                        impl_->voiceIn.emplace_back(member->id, pk);
                        while (impl_->voiceIn.size() > 100) impl_->voiceIn.pop_front();
                        const std::string out = "BCNES1a|" + std::to_string(member->id) + "|" + f[1];
                        for (const auto& o : impl_->members) if (o.id != member->id) impl_->Send(o.addr, out);
                    }
                }
                if (kind == 'X') {
                    if (!started_) {
                        RemovePlayersOf(member->id);
                        impl_->members.erase(impl_->members.begin() + (member - impl_->members.data()));
                        RecomputeMine();
                        impl_->lastLobby = 0;
                    } else {
                        const int id = member->id;                                     // en partida: sus lugares quedan libres
                        RemovePlayersOf(id);
                        impl_->members.erase(impl_->members.begin() + (member - impl_->members.data()));
                        impl_->lastLobby = 0;
                        (void)id;
                    }
                } else if (kind == 'P' && f.size() >= 4 && (!started_ || !member->inGame)) {
                    const int k = ToInt(f[1], -1);
                    std::vector<int> owned = PlayersOf(member->id);
                    int p = -1;
                    if (k >= 0 && static_cast<size_t>(k) < owned.size()) p = owned[static_cast<size_t>(k)];
                    else if (k >= 0 && static_cast<size_t>(k) == owned.size()) p = AddPlayerFor(member->id, member->baseName, k);   // jugador nuevo de esa PC
                    if (p >= 0) {
                        const int c = ToInt(f[2], -1);
                        if (c >= 0 && c < kColorCount && !ColorUsedByOther(c, p)) colors_[static_cast<size_t>(p)] = c;
                        ready_[static_cast<size_t>(p)] = f[3] == "1";
                    }
                    RecomputeMine();
                    impl_->lastLobby = 0;
                } else if (kind == 'R' && f.size() >= 2 && (!started_ || !member->inGame)) {
                    const int k = ToInt(f[1], -1);
                    std::vector<int> owned = PlayersOf(member->id);
                    if (k >= 1 && static_cast<size_t>(k) < owned.size()) {
                        RemovePlayerAt(owned[static_cast<size_t>(k)]);
                        RecomputeMine();
                        impl_->lastLobby = 0;
                    }
                } else if (kind == 'I' && f.size() >= 4 && started_) {
                    member->ack = std::max(member->ack, ToInt(f[1]));
                    if (member->inGame) {
                        const size_t per = std::max<size_t>(1, member->players.size());
                        const int f0 = ToInt(f[2]);
                        for (size_t k = 0; (k + 1) * per * 2 <= f[3].size(); ++k) {
                            uint8_t b[4] = {0, 0, 0, 0};
                            if (!ReadHex(f[3], k * per * 2, static_cast<int>(per), b)) break;
                            const size_t fr = static_cast<size_t>(f0) + k;
                            if (f0 < 0 || fr > 1000000) break;
                            if (static_cast<int>(fr) < member->joinFrame) continue;           // antes de entrar no se le piden botones
                            impl_->EnsureFrame(fr);
                            if (!(impl_->have[fr] & (1 << member->bit))) {
                                for (size_t q = 0; q < member->players.size() && q < static_cast<size_t>(kMaxLocal); ++q) {
                                    impl_->in[fr][static_cast<size_t>(member->players[q])] = static_cast<uint8_t>(b[q] & ~0x0C);   // sin marcas ni Start
                                }
                                impl_->have[fr] = static_cast<uint8_t>(impl_->have[fr] | (1 << member->bit));
                            }
                        }
                    }
                } else if (kind == 'K' && f.size() >= 3) {
                    const int fr = ToInt(f[1]);
                    const uint32_t h = static_cast<uint32_t>(std::strtoul(f[2].c_str(), nullptr, 10));
                    auto it = impl_->hostHash.find(fr);
                    if (it != impl_->hostHash.end()) {
                        if (it->second != h) desync_ = true;
                    } else {
                        impl_->memberHash[fr] = h;
                        while (impl_->memberHash.size() > 64) impl_->memberHash.erase(impl_->memberHash.begin());
                    }
                }
            }
        } else if (state_ == State::Searching || state_ == State::Joining || state_ == State::Joined) {
            if (kind == '!' && f.size() >= 4 && state_ == State::Searching) {
                LanGame g;
                g.name = f[1];
                g.address = Impl::IpOf(from);
                ++diagSeen_;
                diagFrom_ = g.address;
                for (const std::string& ip : impl_->localIps) if (g.address == ip) g.address = "127.0.0.1";
                g.port = ntohs(from.sin_port);
                g.players = ToInt(f[2], 1);
                g.maxPlayers = ToInt(f[3], kMaxPlayers);
                g.compatible = f.size() >= 5 && f[4] == BuildId();
                g.lastSeen = now;
                bool found = false;
                for (auto& old : games_) {
                    if (old.address == g.address && old.port == g.port) { old = g; found = true; }
                }
                if (!found) games_.push_back(g);
                // el mismo anfitrion puede contestar por su IP de red y por 127.0.0.1 (si esta en esta misma PC): una sola entrada
                for (size_t i = 0; i < games_.size();) {
                    bool twin = false;
                    if (games_[i].address != "127.0.0.1") {                      // se queda la de 127.0.0.1: es la que siempre se puede alcanzar
                        for (const auto& o2 : games_) twin = twin || (o2.address == "127.0.0.1" && o2.name == games_[i].name && o2.port == games_[i].port);
                    }
                    if (twin) games_.erase(games_.begin() + static_cast<long>(i));
                    else ++i;
                }
            } else if (Impl::SameAddr(from, impl_->hostAddr)) {
                impl_->lastHeard = now;
                if (kind == 'W' && f.size() >= 2 && state_ == State::Joining) {
                    state_ = State::Joined;
                    myId_ = ToInt(f[1]);
                    games_.clear();
                } else if (kind == 'V' && state_ == State::Joining) {
                    state_ = State::Failed;
                    error_ = "EL ANFITRIÓN TIENE OTRA VERSIÓN DEL JUEGO";
                } else if (kind == 'F' && state_ == State::Joining) {
                    state_ = State::Full;
                    error_ = "LA PARTIDA ESTÁ LLENA O YA EMPEZÓ";
                } else if (kind == 'a' && f.size() >= 3 && state_ == State::Joined) {
                    std::vector<uint8_t> pk;
                    if (Base64Decode(f[2], pk) && !pk.empty() && pk.size() < 700) {
                        impl_->voiceIn.emplace_back(ToInt(f[1]), pk);
                        while (impl_->voiceIn.size() > 100) impl_->voiceIn.pop_front();
                    }
                } else if (kind == 'C') {
                    lost_ = true;
                    state_ = State::Closed;
                    error_ = "EL ANFITRIÓN CERRÓ LA PARTIDA";
                } else if (kind == 'L' && f.size() >= 7 && state_ == State::Joined) {
                    const bool wasStarted = started_;
                    started_ = started_ || f[1] == "1";
                    if (f[1] != "1") seenLobby_ = true;
                    friendly_ = ToInt(f[2]);
                    inputDelay_ = std::max(2, std::min(20, ToInt(f[3], kInputDelay)));
                    startMask_ = ToInt(f[4]) & 0xFF;
                    for (int i = 0; i < kMaxPlayers && i < static_cast<int>(f[5].size()); ++i) startColors_[i] = std::max(0, std::min(kColorCount - 1, f[5][static_cast<size_t>(i)] - '0'));
                    const int cnt = ToInt(f[6]);
                    players_.clear();
                    colors_.clear();
                    ready_.clear();
                    owners_.clear();
                    for (int i = 0; i < cnt && static_cast<size_t>(7 + i * 4 + 3) < f.size(); ++i) {
                        const size_t b = static_cast<size_t>(7 + i * 4);
                        players_.push_back(f[b]);
                        colors_.push_back(std::max(-1, std::min(kColorCount - 1, ToInt(f[b + 1]))));
                        ready_.push_back(f[b + 2] == "1");
                        owners_.push_back(ToInt(f[b + 3]));
                    }
                    {
                        const size_t extra = static_cast<size_t>(7 + cnt * 4);
                        if (f.size() > extra + 1) capacity_ = std::max(2, std::min(kMaxPlayers, ToInt(f[extra + 1], 4)));
                        const int total = f.size() > extra ? ToInt(f[extra]) : 0;
                        if (total >= 0 && total <= 99 && total != mapTotal_) {
                            mapTotal_ = total;
                            maps_.assign(static_cast<size_t>(total), std::string());
                            mapGot_.assign(static_cast<size_t>(total), false);
                        }
                    }
                    RecomputeMine();
                    if (started_ && !wasStarted) {                          // llegue con la partida en marcha si no estaba entre los del principio
                        bool inMask = false;
                        for (int p : mine_) inMask = inMask || ((startMask_ >> p) & 1);
                        lateJoiner_ = !inMask;
                    }
                    if (!players_.empty()) hostName_ = players_[0];
                    if (started_ && impl_->in.empty()) {                    // empieza: los primeros cuadros van vacios
                        for (int fr = 0; fr < inputDelay_; ++fr) {
                            impl_->EnsureFrame(static_cast<size_t>(fr));
                            impl_->have[static_cast<size_t>(fr)] = 1;
                        }
                        impl_->contiguous = inputDelay_;
                        impl_->submitted = inputDelay_;
                        impl_->localCount = LocalCount();
                    }
                } else if (kind == 'G' && f.size() >= 4 && started_) {
                    const int f0 = ToInt(f[1]);
                    const int cnt = ToInt(f[2]);
                    for (int k = 0; k < cnt; ++k) {
                        uint8_t b[kMaxPlayers];
                        if (!ReadHex(f[3], static_cast<size_t>(k) * 16, kMaxPlayers, b)) break;
                        const int fr = f0 + k;
                        if (fr < 0 || fr > 1000000) break;
                        impl_->EnsureFrame(static_cast<size_t>(fr));
                        if (!impl_->have[static_cast<size_t>(fr)]) {
                            for (int i = 0; i < kMaxPlayers; ++i) impl_->in[static_cast<size_t>(fr)][static_cast<size_t>(i)] = b[i];
                            impl_->have[static_cast<size_t>(fr)] = 1;
                        }
                    }
                    while (static_cast<size_t>(impl_->contiguous) < impl_->have.size() && impl_->have[static_cast<size_t>(impl_->contiguous)]) ++impl_->contiguous;
                } else if (kind == 'D') {
                    desync_ = true;
                } else if (kind == 'M' && f.size() >= 4 && state_ == State::Joined && !started_) {         // un mapa propio del anfitrion
                    const int total = ToInt(f[1]), idx = ToInt(f[2]);
                    if (total > 0 && total <= 99 && idx >= 0 && idx < total) {
                        if (mapTotal_ != total) { maps_.assign(static_cast<size_t>(total), std::string()); mapGot_.assign(static_cast<size_t>(total), false); mapTotal_ = total; }
                        std::string text = f[3];
                        for (char& c : text) if (c == '~') c = '\n';
                        maps_[static_cast<size_t>(idx)] = text;
                        mapGot_[static_cast<size_t>(idx)] = true;
                    }
                }
            }
        }
    }
    }

    // ---------------- enviar / vencimientos ----------------
    if (state_ == State::Hosting) {
        UpdateInternet();
        if (!maps_.empty() && !started_ && now - impl_->lastMapSend > 0.12) {          // los mapas propios, de a uno, a quien todavia no los tiene
            impl_->lastMapSend = now;
            impl_->nextMap = (impl_->nextMap + 1) % maps_.size();
            std::string text = maps_[impl_->nextMap];
            for (char& c : text) if (c == '\n') c = '~';
            const std::string msg = "BCNES1M|" + std::to_string(maps_.size()) + "|" + std::to_string(impl_->nextMap) + "|" + text;
            for (const auto& m : impl_->members) if (!m.mapsOk) impl_->Send(m.addr, msg);
        }
        if (now - impl_->lastBeacon > 1.5) {                              // aviso a toda la red: asi los que buscan la ven sin tener que preguntar
            impl_->lastBeacon = now;
            std::ostringstream os;
            os << "BCNES1!|" << myName_ << "|" << players_.size() << "|" << capacity_ << "|" << BuildId();
            const uint16_t bp = static_cast<uint16_t>(impl_->port + 1);
            impl_->Send(Impl::Addr("255.255.255.255", bp), os.str());
            for (const std::string& b : Impl::Broadcasts()) impl_->Send(Impl::Addr(b, bp), os.str());       // por cada red de esta PC
        }
        // PC que dejaron de dar senales
        for (size_t i = 0; i < impl_->members.size();) {
            auto& m = impl_->members[i];
            if (now - m.lastSeen > kMemberTimeout && m.active) {
                if (!started_) {
                    RemovePlayersOf(m.id);
                    impl_->members.erase(impl_->members.begin() + static_cast<long>(i));
                    RecomputeMine();
                    impl_->lastLobby = 0;
                    continue;
                }
                RemovePlayersOf(m.id);                                     // en partida: sus lugares quedan libres (marca de "se fue")
                impl_->members.erase(impl_->members.begin() + static_cast<long>(i));
                impl_->lastLobby = 0;
                continue;
            }
            ++i;
        }
        if (now - impl_->lastLobby > 0.5) {
            impl_->lastLobby = now;
            const std::string msg = LobbyMessage(started_);
            for (const auto& m : impl_->members) impl_->Send(m.addr, msg);
        }
        if (started_) {
            // un espectador que ya eligio color, esta "listo" y ya se puso al dia entra en un cuadro futuro
            for (auto& m : impl_->members) {
                if (m.inGame || !m.active) continue;
                const std::vector<int> mine = PlayersOf(m.id);
                bool allReady = !mine.empty();
                for (int p : mine) allReady = allReady && ready_[static_cast<size_t>(p)];
                if (!allReady || m.ack < impl_->broadcastNext - 3) continue;
                int usedBits = 0;
                for (const auto& o : impl_->members) if (o.inGame) usedBits |= 1 << o.bit;
                int bit = 1;
                while (bit < 8 && (usedBits & (1 << bit))) ++bit;
                if (bit >= 8) continue;
                const int joinAt = impl_->broadcastNext + inputDelay_ + 4;
                for (size_t fr = static_cast<size_t>(impl_->broadcastNext); fr < impl_->have.size(); ++fr) impl_->have[fr] = static_cast<uint8_t>(impl_->have[fr] & ~(1 << bit));
                m.inGame = true;
                m.bit = bit;
                m.joinFrame = joinAt;
                m.players = mine;
                for (int p : mine) {
                    impl_->slotJoinFrame[p] = joinAt;
                    impl_->slotJoinColor[p] = std::max(0, colors_[static_cast<size_t>(p)]);
                }
            }
            // cuadros completos: los de todas las PC que ya estan en la partida (las que entran, desde su cuadro)
            while (static_cast<size_t>(impl_->broadcastNext) < impl_->in.size()) {
                const int f = impl_->broadcastNext;
                uint8_t need = 1;
                for (const auto& m : impl_->members) {
                    if (m.inGame && f >= m.joinFrame) need = static_cast<uint8_t>(need | (1 << m.bit));
                }
                if ((impl_->have[static_cast<size_t>(f)] & need) != need) break;
                for (int s = 0; s < kMaxPlayers; ++s) {                    // marcas de los que se fueron y de los que entran
                    if (static_cast<size_t>(s) < owners_.size() && owners_[static_cast<size_t>(s)] < 0) impl_->in[static_cast<size_t>(f)][static_cast<size_t>(s)] = kGoneMark;
                    if (impl_->slotJoinFrame[s] == f) {
                        impl_->in[static_cast<size_t>(f)][static_cast<size_t>(s)] = static_cast<uint8_t>(0x04 | (impl_->slotJoinColor[s] << 4));
                        impl_->slotJoinFrame[s] = -1;
                    }
                }
                ++impl_->broadcastNext;
            }
            // manda a cada PC los cuadros que le faltan (los que llegan tarde reciben el historial en varios paquetes)
            for (auto& m : impl_->members) {
                if (!m.active) continue;
                int first = std::max(0, m.ack);
                for (int packet = 0; packet < 4; ++packet) {
                    const int cnt = std::min(impl_->broadcastNext - first, kMaxFramesPerPacket);
                    if (cnt <= 0) break;
                    std::ostringstream os;
                    os << "BCNES1G|" << first << "|" << cnt << "|";
                    for (int k = 0; k < cnt; ++k) {
                        for (int s = 0; s < kMaxPlayers; ++s) os << Hex(impl_->in[static_cast<size_t>(first + k)][static_cast<size_t>(s)]);
                    }
                    impl_->Send(m.addr, os.str());
                    first += cnt;
                }
            }
            if (desync_ && now - impl_->lastBeat > 1.0) {
                impl_->lastBeat = now;
                for (const auto& m : impl_->members) impl_->Send(m.addr, "BCNES1D");
            }
        }
    } else if (state_ == State::Searching) {
        for (size_t i = 0; i < games_.size();) {                           // las que dejaron de contestar, afuera
            if (now - games_[i].lastSeen > 4.0) games_.erase(games_.begin() + static_cast<long>(i));
            else ++i;
        }
        if (now - impl_->lastSearch > 1.0) {
            impl_->lastSearch = now;
            impl_->Send(Impl::Addr("255.255.255.255", impl_->port), "BCNES1?");
            for (const std::string& b : Impl::Broadcasts()) impl_->Send(Impl::Addr(b, impl_->port), "BCNES1?");
            impl_->Send(Impl::Addr("127.0.0.1", impl_->port), "BCNES1?");   // por si el anfitrion esta en esta misma PC
        }
    } else if (state_ == State::Joining) {
        if (now - impl_->lastJoin > 0.5) {
            impl_->lastJoin = now;
            impl_->Send(impl_->hostAddr, "BCNES1J|" + myName_ + "|" + BuildId());
        }
        if (now - impl_->joinStart > joinTimeout_) {
            state_ = State::Failed;
            error_ = "LA PARTIDA NO RESPONDE";
        }
    } else if (state_ == State::Joined) {
        if (now - impl_->lastBeat > 1.0) {
            impl_->lastBeat = now;
            impl_->Send(impl_->hostAddr, std::string("BCNES1H|") + (SharedMapsComplete() ? "1" : "0"));
            for (int k = 0; k < LocalCount(); ++k) SendChoice(k);          // por si se perdio el aviso de un color
        }
        if (started_) {
            // mis botones de los cuadros que el anfitrion todavia no me confirmo
            const int first = impl_->contiguous;
            const int cnt = std::min(impl_->submitted - first, kMaxFramesPerPacket);
            const size_t per = static_cast<size_t>(std::max(1, LocalCount()));
            if (cnt > 0) {
                std::ostringstream os;
                os << "BCNES1I|" << impl_->contiguous << "|" << first << "|";
                for (int k = 0; k < cnt; ++k) {
                    const size_t fr = static_cast<size_t>(first + k);
                    for (size_t q = 0; q < per; ++q) os << Hex(fr * 4 + q < impl_->own.size() ? impl_->own[fr * 4 + q] : 0);
                }
                impl_->Send(impl_->hostAddr, os.str());
            }
        }
        if (now - impl_->lastHeard > kMemberTimeout) {
            lost_ = true;
            state_ = State::Closed;
            error_ = "SE PERDIÓ LA CONEXIÓN CON EL ANFITRIÓN";
        }
    }
}

} // namespace app
