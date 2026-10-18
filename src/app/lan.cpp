#include "lan.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>

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
        const int hi = HexVal(s[pos + i * 2]), lo = HexVal(s[pos + i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>(hi * 16 + lo);
    }
    return true;
}

int ToInt(const std::string& s, int def = 0) { return s.empty() ? def : std::atoi(s.c_str()); }

constexpr double kMemberTimeout = 6.0;
constexpr int kMaxFramesPerPacket = 24;

} // namespace

struct Lan::Impl {
    struct Member {
        sockaddr_in addr{};
        std::string name;
        double lastSeen = 0;
        int ack = 0;               // cuadros recibidos por ese miembro (sin huecos)
        bool active = true;
    };

    SOCKET sock = INVALID_SOCKET;
    uint16_t port = Lan::kDefaultPort;
    sockaddr_in hostAddr{};                // miembro: direccion del anfitrion
    std::vector<Member> members;           // anfitrion: los demas jugadores (ranura = indice + 1)
    double lastLobby = 0, lastSearch = 0, lastBeat = 0, lastHeard = 0, joinStart = 0, lastJoin = 0;
    int wantColor = 0;                    // miembro: lo que el jugador pidio (se reenvia unos segundos hasta que el anfitrion lo confirme)
    bool wantReady = false;
    double wantUntil = 0;

    // partida
    std::vector<std::array<uint8_t, 4>> in;   // botones por cuadro y ranura
    std::vector<uint8_t> have;                // anfitrion: bits de ranura con dato; miembro: 1 si llego el cuadro
    std::vector<uint8_t> own;                 // miembro: mis botones por cuadro
    int broadcastNext = 0;                    // anfitrion: primer cuadro todavia incompleto
    int contiguous = 0;                       // miembro: cuadros recibidos sin huecos
    int submitted = 0;                        // proximo cuadro propio por pedir
    std::map<int, uint32_t> hostHash, memberHash;

    void Close() {
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
            in.resize(f + 1, std::array<uint8_t, 4>{0, 0, 0, 0});
            have.resize(f + 1, 0);
        }
    }
};

std::string Lan::BuildId() {
    return std::string(__DATE__) + " " + __TIME__;
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

bool Lan::Host(const std::string& myName) {
    Leave();
    myName_ = CleanName(myName);
    if (myName_.empty()) { error_ = "NOMBRE VACÍO"; state_ = State::Failed; return false; }
    if (!impl_->Open(impl_->port)) { error_ = "NO SE PUDO ABRIR EL PUERTO DE RED"; state_ = State::Failed; return false; }
    state_ = State::Hosting;
    players_ = {myName_};
    colors_ = {0};
    ready_ = {false};
    hostName_ = myName_;
    mySlot_ = 0;
    impl_->members.clear();
    return true;
}

bool Lan::Search() {
    if (state_ == State::Hosting || state_ == State::Joined) return false;
    if (impl_->sock == INVALID_SOCKET && !impl_->Open(0)) { error_ = "NO SE PUDO ABRIR LA RED"; state_ = State::Failed; return false; }
    state_ = State::Searching;
    games_.clear();
    impl_->lastSearch = 0;                              // manda el primer pedido enseguida
    return true;
}

bool Lan::Join(const LanGame& game, const std::string& myName) {
    myName_ = CleanName(myName);
    if (myName_.empty()) { error_ = "NOMBRE VACÍO"; return false; }
    if (impl_->sock == INVALID_SOCKET && !impl_->Open(0)) { error_ = "NO SE PUDO ABRIR LA RED"; state_ = State::Failed; return false; }
    impl_->hostAddr = Impl::Addr(game.address, game.port);
    hostName_ = game.name;
    state_ = State::Joining;
    impl_->joinStart = impl_->lastJoin = Now();
    impl_->lastHeard = Now();
    impl_->Send(impl_->hostAddr, "BCNES1J|" + myName_ + "|" + BuildId());
    players_.clear();
    started_ = false;
    lost_ = false;
    return true;
}

void Lan::Leave() {
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
    impl_->broadcastNext = impl_->contiguous = impl_->submitted = 0;
    impl_->hostHash.clear();
    impl_->memberHash.clear();
    games_.clear();
    players_.clear();
    colors_.clear();
    ready_.clear();
    state_ = State::Idle;
    started_ = false;
    desync_ = false;
    lost_ = false;
    mySlot_ = 0;
}

std::string Lan::LobbyMessage(bool started) const {
    std::ostringstream os;
    os << "BCNES1L|" << (started ? 1 : 0) << "|" << friendly_ << "|" << players_.size();
    for (size_t i = 0; i < players_.size(); ++i) {
        os << "|" << players_[i] << "|" << (i < colors_.size() ? colors_[i] : 0) << "|" << ((i < ready_.size() && ready_[i]) ? 1 : 0);
    }
    return os.str();
}

bool Lan::ColorUsedByOther(int color, int exceptSlot) const {
    for (size_t i = 0; i < colors_.size(); ++i) {
        if (static_cast<int>(i) != exceptSlot && colors_[i] == color) return true;
    }
    return false;
}

void Lan::SendMyChoice() {
    if (state_ == State::Joined && mySlot_ >= 0 && static_cast<size_t>(mySlot_) < colors_.size() && !started_) {
        const bool useWant = impl_->wantUntil > Now();
        std::ostringstream os;
        os << "BCNES1P|" << (useWant ? impl_->wantColor : colors_[static_cast<size_t>(mySlot_)]) << "|"
           << ((useWant ? impl_->wantReady : static_cast<bool>(ready_[static_cast<size_t>(mySlot_)])) ? 1 : 0);
        impl_->Send(impl_->hostAddr, os.str());
    }
}

void Lan::CycleMyColor(int direction) {
    if (started_ || (state_ != State::Hosting && state_ != State::Joined)) return;
    const size_t me = static_cast<size_t>(mySlot_);
    if (me >= colors_.size()) return;
    int c = colors_[me];
    for (int i = 0; i < kColorCount; ++i) {
        c = ((c + direction) % kColorCount + kColorCount) % kColorCount;
        if (!ColorUsedByOther(c, mySlot_)) break;
    }
    colors_[me] = c;
    ready_[me] = false;
    if (state_ == State::Hosting) {
        impl_->lastLobby = 0;                                              // avisa enseguida
    } else {
        impl_->wantColor = c;
        impl_->wantReady = false;
        impl_->wantUntil = Now() + 2.5;
        SendMyChoice();
    }
}

void Lan::ToggleMyReady() {
    if (started_ || (state_ != State::Hosting && state_ != State::Joined)) return;
    const size_t me = static_cast<size_t>(mySlot_);
    if (me >= ready_.size()) return;
    ready_[me] = !ready_[me];
    if (state_ == State::Hosting) {
        impl_->lastLobby = 0;
    } else {
        impl_->wantColor = colors_[me];
        impl_->wantReady = ready_[me];
        impl_->wantUntil = Now() + 2.5;
        SendMyChoice();
    }
}

bool Lan::StartGame(int friendlyFire) {
    if (state_ != State::Hosting || started_) return false;
    started_ = true;
    friendly_ = friendlyFire;
    for (size_t i = 0; i < impl_->members.size(); ++i) impl_->members[i].active = true;
    impl_->in.clear();
    impl_->have.clear();
    for (int f = 0; f < kInputDelay; ++f) {                // los primeros cuadros van sin botones, para todos
        impl_->EnsureFrame(static_cast<size_t>(f));
        impl_->have[static_cast<size_t>(f)] = 0xFF;
    }
    impl_->broadcastNext = kInputDelay;
    impl_->submitted = kInputDelay;
    // avisa enseguida a los miembros
    for (const auto& m : impl_->members) impl_->Send(m.addr, LobbyMessage(true));
    return true;
}

bool Lan::FrameReady(int f) const {
    if (!started_ || f < 0) return false;
    if (state_ == State::Hosting) return f < impl_->broadcastNext;
    return f < impl_->contiguous;
}

void Lan::FrameInputs(int f, uint8_t out[4]) const {
    for (int i = 0; i < 4; ++i) out[i] = 0;
    if (f >= 0 && static_cast<size_t>(f) < impl_->in.size()) {
        for (int i = 0; i < 4; ++i) out[i] = impl_->in[static_cast<size_t>(f)][static_cast<size_t>(i)];
    }
}

void Lan::SubmitLocal(int simFrame, uint8_t buttons) {
    if (!started_) return;
    while (impl_->submitted <= simFrame + kInputDelay) {
        const size_t f = static_cast<size_t>(impl_->submitted);
        if (state_ == State::Hosting) {
            impl_->EnsureFrame(f);
            impl_->in[f][0] = buttons;
            impl_->have[f] = static_cast<uint8_t>(impl_->have[f] | 1);
        } else {
            if (impl_->own.size() <= f) impl_->own.resize(f + 1, 0);
            impl_->own[f] = buttons;
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

void Lan::Update() {
    if (impl_->sock == INVALID_SOCKET) return;
    const double now = Now();

    // ---------------- recibir ----------------
    char buf[2048];
    for (;;) {
        sockaddr_in from{};
        SockLen fl = sizeof from;
        const int n = ::recvfrom(impl_->sock, buf, sizeof buf - 1, 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) break;
        const std::string msg(buf, static_cast<size_t>(n));
        if (msg.compare(0, 6, "BCNES1") != 0 || msg.size() < 7) continue;
        const char kind = msg[6];
        const std::vector<std::string> f = Split(msg, '|');

        if (state_ == State::Hosting) {
            Impl::Member* member = nullptr;
            for (auto& m : impl_->members) {
                if (Impl::SameAddr(m.addr, from)) member = &m;
            }
            if (kind == '?') {
                std::ostringstream os;
                os << "BCNES1!|" << myName_ << "|" << players_.size() << "|" << kMaxPlayers << "|" << BuildId();
                impl_->Send(from, os.str());
            } else if (kind == 'J' && f.size() >= 2) {
                if (f.size() < 3 || f[2] != BuildId()) {
                    impl_->Send(from, "BCNES1V");                           // otra version del programa
                } else if (member) {                                              // ya estaba: repite la bienvenida
                    member->lastSeen = now;
                    impl_->Send(from, "BCNES1W|" + std::to_string(1 + (member - impl_->members.data())));
                } else if (started_ || static_cast<int>(players_.size()) >= kMaxPlayers) {
                    impl_->Send(from, "BCNES1F");
                } else {
                    Impl::Member m;
                    m.addr = from;
                    m.name = CleanName(f[1]);
                    if (m.name.empty()) m.name = "JUGADOR";
                    // nombre repetido: se le agrega un numero
                    for (int tries = 2; tries < 10; ++tries) {
                        bool dup = false;
                        for (const auto& n2 : players_) dup = dup || n2 == m.name;
                        if (!dup) break;
                        m.name = CleanName(m.name.substr(0, kMaxName - 2)) + std::to_string(tries);
                    }
                    m.lastSeen = now;
                    impl_->members.push_back(m);
                    players_.push_back(m.name);
                    int freeColor = 0;
                    while (freeColor < kColorCount && ColorUsedByOther(freeColor, -1)) ++freeColor;
                    colors_.push_back(freeColor % kColorCount);
                    ready_.push_back(false);
                    impl_->Send(from, "BCNES1W|" + std::to_string(impl_->members.size()));
                    impl_->lastLobby = 0;                                    // avisa a todos ya
                }
            } else if (member) {
                member->lastSeen = now;
                if (kind == 'X') {
                    const size_t idx = static_cast<size_t>(member - impl_->members.data());
                    if (!started_) {
                        impl_->members.erase(impl_->members.begin() + static_cast<long>(idx));
                        players_.erase(players_.begin() + static_cast<long>(idx) + 1);
                        colors_.erase(colors_.begin() + static_cast<long>(idx) + 1);
                        ready_.erase(ready_.begin() + static_cast<long>(idx) + 1);
                        impl_->lastLobby = 0;
                    } else {
                        member->active = false;
                    }
                } else if (kind == 'P' && f.size() >= 3 && !started_) {
                    const size_t slot = static_cast<size_t>(member - impl_->members.data()) + 1;
                    const int c = ToInt(f[1], -1);
                    if (c >= 0 && c < kColorCount && slot < colors_.size() && !ColorUsedByOther(c, static_cast<int>(slot))) colors_[slot] = c;
                    if (slot < ready_.size()) ready_[slot] = f[2] == "1";
                    impl_->lastLobby = 0;
                } else if (kind == 'I' && f.size() >= 4 && started_) {
                    const size_t slot = static_cast<size_t>(member - impl_->members.data()) + 1;
                    member->ack = std::max(member->ack, ToInt(f[1]));
                    const int f0 = ToInt(f[2]);
                    for (size_t k = 0; k * 2 + 2 <= f[3].size(); ++k) {
                        uint8_t b;
                        if (!ReadHex(f[3], k * 2, 1, &b)) break;
                        const size_t fr = static_cast<size_t>(f0) + k;
                        if (f0 < 0 || fr > 1000000) break;
                        impl_->EnsureFrame(fr);
                        if (!(impl_->have[fr] & (1 << slot))) {
                            impl_->in[fr][slot] = b;
                            impl_->have[fr] = static_cast<uint8_t>(impl_->have[fr] | (1 << slot));
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
                    if (games_[i].address == "127.0.0.1") {
                        for (const auto& o2 : games_) twin = twin || (o2.address != "127.0.0.1" && o2.name == games_[i].name && o2.port == games_[i].port);
                    }
                    if (twin) games_.erase(games_.begin() + static_cast<long>(i));
                    else ++i;
                }
            } else if (Impl::SameAddr(from, impl_->hostAddr) || (from.sin_addr.s_addr == impl_->hostAddr.sin_addr.s_addr && from.sin_port == impl_->hostAddr.sin_port)) {
                impl_->lastHeard = now;
                if (kind == 'W' && f.size() >= 2 && state_ == State::Joining) {
                    state_ = State::Joined;
                    mySlot_ = ToInt(f[1]);
                    games_.clear();
                } else if (kind == 'V' && state_ == State::Joining) {
                    state_ = State::Failed;
                    error_ = "EL ANFITRIÓN TIENE OTRA VERSIÓN DEL JUEGO";
                } else if (kind == 'F' && state_ == State::Joining) {
                    state_ = State::Full;
                    error_ = "LA PARTIDA ESTÁ LLENA O YA EMPEZÓ";
                } else if (kind == 'C') {
                    lost_ = true;
                    state_ = State::Closed;
                    error_ = "EL ANFITRIÓN CERRÓ LA PARTIDA";
                } else if (kind == 'L' && f.size() >= 4 && state_ == State::Joined) {
                    started_ = started_ || f[1] == "1";
                    friendly_ = ToInt(f[2]);
                    const int cnt = ToInt(f[3]);
                    players_.clear();
                    colors_.clear();
                    ready_.clear();
                    for (int i = 0; i < cnt && static_cast<size_t>(4 + i * 3 + 2) < f.size(); ++i) {
                        players_.push_back(f[static_cast<size_t>(4 + i * 3)]);
                        colors_.push_back(std::max(0, std::min(kColorCount - 1, ToInt(f[static_cast<size_t>(5 + i * 3)]))));
                        ready_.push_back(f[static_cast<size_t>(6 + i * 3)] == "1");
                    }
                    if (!players_.empty()) hostName_ = players_[0];
                    if (started_ && impl_->in.empty()) {                    // empieza: los primeros cuadros van vacios
                        for (int fr = 0; fr < kInputDelay; ++fr) {
                            impl_->EnsureFrame(static_cast<size_t>(fr));
                            impl_->have[static_cast<size_t>(fr)] = 1;
                        }
                        impl_->contiguous = kInputDelay;
                        impl_->submitted = kInputDelay;
                    }
                } else if (kind == 'G' && f.size() >= 4 && started_) {
                    const int f0 = ToInt(f[1]);
                    const int cnt = ToInt(f[2]);
                    for (int k = 0; k < cnt; ++k) {
                        uint8_t b[4];
                        if (!ReadHex(f[3], static_cast<size_t>(k) * 8, 4, b)) break;
                        const int fr = f0 + k;
                        if (fr < 0 || fr > 1000000) break;
                        impl_->EnsureFrame(static_cast<size_t>(fr));
                        if (!impl_->have[static_cast<size_t>(fr)]) {
                            for (int i = 0; i < 4; ++i) impl_->in[static_cast<size_t>(fr)][static_cast<size_t>(i)] = b[i];
                            impl_->have[static_cast<size_t>(fr)] = 1;
                        }
                    }
                    while (static_cast<size_t>(impl_->contiguous) < impl_->have.size() && impl_->have[static_cast<size_t>(impl_->contiguous)]) ++impl_->contiguous;
                } else if (kind == 'D') {
                    desync_ = true;
                }
            }
        }
    }

    // ---------------- enviar / vencimientos ----------------
    if (state_ == State::Hosting) {
        // miembros que dejaron de dar senales
        for (size_t i = 0; i < impl_->members.size();) {
            auto& m = impl_->members[i];
            if (now - m.lastSeen > kMemberTimeout && m.active) {
                if (!started_) {
                    impl_->members.erase(impl_->members.begin() + static_cast<long>(i));
                    players_.erase(players_.begin() + static_cast<long>(i) + 1);
                    colors_.erase(colors_.begin() + static_cast<long>(i) + 1);
                    ready_.erase(ready_.begin() + static_cast<long>(i) + 1);
                    impl_->lastLobby = 0;
                    continue;
                }
                m.active = false;                                          // en partida: sus botones pasan a ser 0
            }
            ++i;
        }
        if (!started_) {
            if (now - impl_->lastLobby > 0.5) {
                impl_->lastLobby = now;
                const std::string msg = LobbyMessage(false);
                for (const auto& m : impl_->members) impl_->Send(m.addr, msg);
            }
        } else {
            // cuadros completos: los de todas las ranuras activas
            uint8_t need = 1;
            for (size_t i = 0; i < impl_->members.size(); ++i) {
                if (impl_->members[i].active) need = static_cast<uint8_t>(need | (1 << (i + 1)));
            }
            while (static_cast<size_t>(impl_->broadcastNext) < impl_->in.size() &&
                   (impl_->have[static_cast<size_t>(impl_->broadcastNext)] & need) == need) {
                ++impl_->broadcastNext;
            }
            // manda a cada miembro los cuadros que le faltan
            for (auto& m : impl_->members) {
                if (!m.active) continue;
                const int first = std::max(0, m.ack);
                const int cnt = std::min(impl_->broadcastNext - first, kMaxFramesPerPacket);
                if (cnt <= 0) continue;
                std::ostringstream os;
                os << "BCNES1G|" << first << "|" << cnt << "|";
                for (int k = 0; k < cnt; ++k) {
                    for (int s = 0; s < 4; ++s) os << Hex(impl_->in[static_cast<size_t>(first + k)][static_cast<size_t>(s)]);
                }
                impl_->Send(m.addr, os.str());
            }
            if (desync_ && now - impl_->lastLobby > 1.0) {
                impl_->lastLobby = now;
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
            impl_->Send(Impl::Addr("127.0.0.1", impl_->port), "BCNES1?");   // por si el anfitrion esta en esta misma PC
        }
    } else if (state_ == State::Joining) {
        if (now - impl_->lastJoin > 0.5) {
            impl_->lastJoin = now;
            impl_->Send(impl_->hostAddr, "BCNES1J|" + myName_ + "|" + BuildId());
        }
        if (now - impl_->joinStart > 5.0) {
            state_ = State::Failed;
            error_ = "LA PARTIDA NO RESPONDE";
        }
    } else if (state_ == State::Joined) {
        if (now - impl_->lastBeat > 1.0) {
            impl_->lastBeat = now;
            impl_->Send(impl_->hostAddr, "BCNES1H");
            SendMyChoice();                                              // por si se perdio el aviso de mi color
        }
        if (started_) {
            // mis botones de los cuadros que el anfitrion todavia no me confirmo
            const int first = impl_->contiguous;
            const int cnt = std::min(impl_->submitted - first, kMaxFramesPerPacket);
            if (cnt > 0) {
                std::ostringstream os;
                os << "BCNES1I|" << impl_->contiguous << "|" << first << "|";
                for (int k = 0; k < cnt; ++k) {
                    const size_t fr = static_cast<size_t>(first + k);
                    os << Hex(fr < impl_->own.size() ? impl_->own[fr] : 0);
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
