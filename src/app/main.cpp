// Pantalla de pruebas de Battle City (port del codigo original de la NES).
//
// Uso:
//   battlecitynes.exe                       ventana 16:9 jugable
//   battlecitynes.exe --wav out.wav --frames 300 [--do ...]   guarda el audio generado (sin ventana)
//   battlecitynes.exe                       pantalla de titulo (jugar / modo construccion / configuraciones)
//   battlecitynes.exe --menu --shot t.png --do 5:mok,6:mok,7:mfire2   capturas de los menus (mup mdown mok mback mstart mfireN...)
//   battlecitynes.exe --classic              el campo clasico de 13x13 (sin panoramica)
//   battlecitynes.exe --shot out.png --frames 300 [--players 1-4] [--do 10:e0,20:b3 ...]
//                                           corre N cuadros sin ventana y guarda una captura
//
// Acciones de --do (una lista separada por comas, "cuadro:accion"):
//   e0..e3 enemigo basico/rapido/power/blindado   f0..f3 lo mismo pero parpadeante (con item)
//   b0..b6 item: casco, reloj, pala, estrella, granada, tanque, pistola
//   s0 musica de inicio de etapa   s1 musica de record   s2 game over
//   k matar a todos los enemigos   c limpiar enemigos   +up -up +down -down +left -left +right -right
//   +fire -fire (jugador 1; con un 2, 3 o 4 al final, ese jugador: +up3, +fire4...)   p pausa

#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "../nes/battle.h"
#include "apu.h"
#include "arena.h"
#include "config.h"
#include "flow.h"
#include "lan.h"
#include "levels.h"
#include "lan.h"
#include "lobby.h"
#include "renderer.h"
#include "samples.h"

namespace {

using nes::u8;

struct Action {
    int frame;
    std::string name;
};

// Cola de audio entre el hilo del juego (productor: 735 muestras por cuadro de
// juego, con las escrituras de la APU de ese cuadro) y el hilo de audio del
// dispositivo (consumidor). Se precarga unos cuadros para absorber el jitter.
class AudioRing {
public:
    static constexpr int kFrameSamples = app::Apu::kSampleRate / 60;
    static constexpr size_t kCapacity = kFrameSamples * 12;
    static constexpr size_t kPrefill = kFrameSamples * 3;      // ~50 ms antes de empezar a sonar
    static constexpr size_t kMaxLatency = kFrameSamples * 8;   // si se acumula mas, se descarta lo viejo

    void Push(const int16_t* data, int count) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (int i = 0; i < count; ++i) {
            buf_[(head_ + size_) % kCapacity] = data[i];
            if (size_ < kCapacity) ++size_; else head_ = (head_ + 1) % kCapacity;
        }
        while (size_ > kMaxLatency) {                              // el juego va mas rapido que el audio
            head_ = (head_ + 1) % kCapacity;
            --size_;
            ++dropped_;
        }
    }

    // Llamado desde el hilo de audio.
    void Pull(int16_t* out, unsigned frames) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) {
            if (size_ < kPrefill) { std::memset(out, 0, frames * sizeof(int16_t)); return; }
            started_ = true;
        }
        for (unsigned i = 0; i < frames; ++i) {
            if (size_ == 0) {                                      // se quedo sin datos: silencio y vuelve a precargar
                out[i] = 0;
                if (i == 0 || out[i - 1] != 0) ++underruns_;
                started_ = false;
                for (unsigned j = i + 1; j < frames; ++j) out[j] = 0;
                return;
            }
            out[i] = buf_[head_];
            head_ = (head_ + 1) % kCapacity;
            --size_;
        }
    }

    int Underruns() { std::lock_guard<std::mutex> lock(mutex_); return underruns_; }
    int Dropped() { std::lock_guard<std::mutex> lock(mutex_); return dropped_; }

private:
    std::mutex mutex_;
    int16_t buf_[kCapacity] = {};
    size_t head_ = 0, size_ = 0;
    bool started_ = false;
    int underruns_ = 0, dropped_ = 0;
};

AudioRing g_ring;

void AudioCallback(void* buffer, unsigned frames) {
    g_ring.Pull(static_cast<int16_t*>(buffer), frames);
}

app::SampleBank g_samples;
bool g_useSamples = true;      // true: grabaciones del proyecto anterior; false: sintetizador de la NES (APU)
app::Config g_cfg;             // teclas, mando, vibracion y demas ajustes (se guardan en un archivo)
std::string g_cfgPath = "battlecitynes_settings.txt";
int g_padOfSlot[4] = {-1, -1, -1, -1};   // mando de cada jugador: se reparten solos al conectarlos

void SaveConfig() {
    g_cfg.useSamples = g_useSamples;
    g_cfg.Save(g_cfgPath);
}


// Vibracion del mando. raylib con GLFW (el backend de escritorio) no implementa SetGamepadVibration(), asi que en Windows se
// usa XInput directamente (mandos de Xbox). XInput no tiene duracion: cada mando recuerda cuando debe apagarse.
#ifdef _WIN32
extern "C" {
__declspec(dllimport) void* __stdcall LoadLibraryA(const char* name);
__declspec(dllimport) void* __stdcall GetProcAddress(void* module, const char* name);
}
#endif

struct XVibration { unsigned short left, right; };
using XInputSetStateFn = unsigned long (__stdcall*)(unsigned long, XVibration*);
double g_rumbleEnd[4] = {0, 0, 0, 0};

void PadMotors(int pad, float left, float right) {
#ifdef _WIN32
    static XInputSetStateFn setState = [] {
        for (const char* dll : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"}) {
            if (void* m = LoadLibraryA(dll)) {
                if (void* f = GetProcAddress(m, "XInputSetState")) return reinterpret_cast<XInputSetStateFn>(f);
            }
        }
        return static_cast<XInputSetStateFn>(nullptr);
    }();
    if (setState && pad >= 0 && pad < 4) {
        XVibration v{static_cast<unsigned short>(std::min(1.0f, std::max(0.0f, left)) * 65535.0f),
                     static_cast<unsigned short>(std::min(1.0f, std::max(0.0f, right)) * 65535.0f)};
        setState(static_cast<unsigned long>(pad), &v);
    }
#else
    if (pad >= 0) SetGamepadVibration(pad, left, right, 0.1f);
#endif
}

// Hace vibrar el mando `pad` (id de raylib) durante `seconds`.
void PadRumble(int pad, float left, float right, float seconds) {
    if (pad < 0 || pad >= 4) return;
    PadMotors(pad, left, right);
    g_rumbleEnd[pad] = GetTime() + seconds;
}

// Una vez por cuadro: apaga los motores cuya vibracion ya termino.
void ServiceRumbleTimers() {
    for (int p = 0; p < 4; ++p) {
        if (g_rumbleEnd[p] > 0 && GetTime() >= g_rumbleEnd[p]) {
            PadMotors(p, 0.0f, 0.0f);
            g_rumbleEnd[p] = 0;
        }
    }
}

// Pantalla completa (ventana sin bordes): Alt+Enter, F11 y la opcion de Configuracion. Se recuerda entre sesiones.
void ToggleFullscreen() {
    ToggleBorderlessWindowed();
    g_cfg.fullscreen = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    SaveConfig();
}

// Valores por defecto: teclas de los 4 jugadores y mando tipo Xbox (A dispara, X cancela, el boton de menu/play es Start).
void ApplyDefaultConfig(app::Config& c) {
    const int keys[4][app::Config::kActions] = {
        {KEY_W, KEY_S, KEY_A, KEY_D, KEY_F, KEY_G},
        {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ENTER, KEY_RIGHT_CONTROL},
        {KEY_I, KEY_K, KEY_J, KEY_L, KEY_U, KEY_O},
        {KEY_KP_8, KEY_KP_5, KEY_KP_4, KEY_KP_6, KEY_KP_0, KEY_KP_ENTER},
    };
    for (int p = 0; p < 4; ++p)
        for (int a = 0; a < app::Config::kActions; ++a) c.defaultKey[p][a] = keys[p][a];
    c.defaultPad[0] = GAMEPAD_BUTTON_RIGHT_FACE_DOWN;     // A: disparar / aceptar
    c.defaultPad[1] = GAMEPAD_BUTTON_RIGHT_FACE_LEFT;     // X: segundo disparo
    c.defaultPad[2] = GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;    // B: cancelar / volver
    c.defaultPad[3] = GAMEPAD_BUTTON_MIDDLE_RIGHT;        // Start (el boton de "play"/menu)
    c.ResetKeys();
    c.ResetPad();
}

std::string KeyName(int key) {
    if (key == 0) return "SIN ASIGNAR";
    if (key >= KEY_A && key <= KEY_Z) return std::string(1, static_cast<char>('A' + key - KEY_A));
    if (key >= KEY_ZERO && key <= KEY_NINE) return std::string(1, static_cast<char>('0' + key - KEY_ZERO));
    if (key >= KEY_F1 && key <= KEY_F12) return "F" + std::to_string(key - KEY_F1 + 1);
    if (key >= KEY_KP_0 && key <= KEY_KP_9) return "NUM " + std::to_string(key - KEY_KP_0);
    switch (key) {
        case KEY_UP: return "FLECHA ARRIBA";
        case KEY_DOWN: return "FLECHA ABAJO";
        case KEY_LEFT: return "FLECHA IZQUIERDA";
        case KEY_RIGHT: return "FLECHA DERECHA";
        case KEY_SPACE: return "ESPACIO";
        case KEY_ENTER: return "ENTER";
        case KEY_TAB: return "TAB";
        case KEY_BACKSPACE: return "RETROCESO";
        case KEY_LEFT_SHIFT: return "SHIFT IZQ";
        case KEY_RIGHT_SHIFT: return "SHIFT DER";
        case KEY_LEFT_CONTROL: return "CTRL IZQ";
        case KEY_RIGHT_CONTROL: return "CTRL DER";
        case KEY_LEFT_ALT: return "ALT IZQ";
        case KEY_RIGHT_ALT: return "ALT DER";
        case KEY_KP_ENTER: return "NUM ENTER";
        case KEY_KP_ADD: return "NUM +";
        case KEY_KP_SUBTRACT: return "NUM -";
        case KEY_KP_MULTIPLY: return "NUM *";
        case KEY_KP_DIVIDE: return "NUM /";
        case KEY_KP_DECIMAL: return "NUM .";
        case KEY_COMMA: return ",";
        case KEY_PERIOD: return ".";
        case KEY_MINUS: return "-";
        case KEY_INSERT: return "INSERT";
        case KEY_DELETE: return "SUPR";
        case KEY_HOME: return "INICIO";
        case KEY_END: return "FIN";
        case KEY_PAGE_UP: return "RE PAG";
        case KEY_PAGE_DOWN: return "AV PAG";
        default: return "TECLA " + std::to_string(key);
    }
}

// Nombre de un boton del mando; A, B, X e Y salen como iconos ({A}...).
std::string PadButtonName(int b) {
    switch (b) {
        case GAMEPAD_BUTTON_RIGHT_FACE_DOWN: return "{A}";
        case GAMEPAD_BUTTON_RIGHT_FACE_RIGHT: return "{B}";
        case GAMEPAD_BUTTON_RIGHT_FACE_LEFT: return "{X}";
        case GAMEPAD_BUTTON_RIGHT_FACE_UP: return "{Y}";
        case GAMEPAD_BUTTON_MIDDLE_RIGHT: return "{START}";
        case GAMEPAD_BUTTON_MIDDLE_LEFT: return "VISTA";
        case GAMEPAD_BUTTON_MIDDLE: return "XBOX";
        case GAMEPAD_BUTTON_LEFT_TRIGGER_1: return "LB";
        case GAMEPAD_BUTTON_LEFT_TRIGGER_2: return "LT";
        case GAMEPAD_BUTTON_RIGHT_TRIGGER_1: return "RB";
        case GAMEPAD_BUTTON_RIGHT_TRIGGER_2: return "RT";
        case GAMEPAD_BUTTON_LEFT_THUMB: return "L3";
        case GAMEPAD_BUTTON_RIGHT_THUMB: return "R3";
        case GAMEPAD_BUTTON_LEFT_FACE_UP: return "CRUZ ARRIBA";
        case GAMEPAD_BUTTON_LEFT_FACE_DOWN: return "CRUZ ABAJO";
        case GAMEPAD_BUTTON_LEFT_FACE_LEFT: return "CRUZ IZQ";
        case GAMEPAD_BUTTON_LEFT_FACE_RIGHT: return "CRUZ DER";
        default: return "BOTON " + std::to_string(b);
    }
}

class TestScreen {
public:
    enum class Scene { Title, PlayMenu, Lobby, Settings, KeysPlayer, KeysList, PadSettings, Construction, Pause, Game,
                       LanMenu, NameEntry, LanHost, LanSearch, LanJoined, OnlineMenu, CodeEntry };
    enum class Capture { None, Key, Pad };
    struct Rumble { int slot; float left, right, seconds; float delay = 0.0f; };   // delay: segundos hasta empezar
    enum class Ev { Up, Down, Left, Right, Accept, Back, Fire, Start };

    bool Init(const std::string& chrPath) {
        return renderer_.LoadChr(chrPath);
    }

    void Reset(int players) {
        players_ = players;
        showHelp_ = false;
        apu_.Reset();
        battle_ = std::make_unique<nes::Battle>();
        battle_->apuWrite = [this](uint16_t addr, u8 value) { apu_.Write(addr, value); };
        battle_->sfxStartHook = [](int sfx) {
            if (g_useSamples) g_samples.Trigger(sfx);
        };
        battle_->SetWide(wide_);
        battle_->SetPlayerColors(colors_);
        battle_->SetLevel(wide_ ? level_ : 0);
        battle_->SetFriendlyFire(friendly_);
        for (int i = 0; i < 4; ++i) { prevAlive_[i] = false; prevUpgrade_[i] = 0; prevFire_[i] = false; prevHits_[i] = prevPickups_[i] = prevKills_[i] = 0; }
        prevBlasts_ = prevEagles_ = 0;
        rumble_.clear();
        battle_->NewGame(players_, level_ > 0 ? level_ : 1);
        if (wide_) {
            u8 blocks[nes::Battle::kWideBlocksY][nes::Battle::kWideBlocksX];
            if (level_ > 0) {
                app::GenerateWideLevel(level_, blocks);
            } else {
                app::ArenaToWideBlocks(blocks);                    // nivel 0: el mapa de pruebas con un bloque de cada tipo
            }
            u8 strip[app::kLevelCols];
            app::GenerateTopStrip(level_ > 0 ? level_ : 1, strip);
            battle_->LoadWideStage(blocks, level_ > 0 ? strip : nullptr);
        } else {
            u8 data[91];
            app::ArenaToStageData(data);
            battle_->LoadStageData(data);
        }
        battle_->StartStage();
        // Solo para pruebas: hasta 6 enemigos a la vez y sin oleada automatica
        // (la oleada del original se activa con la tecla V).
        if (level_ == 0) battle_->SetEnemyLimitMax();      // en el mapa de pruebas, todos los enemigos posibles a la vez
        if (level_ == 0) battle_->R(nes::ram_enemy_spawn_cnt) = 0;
        battle_->R(nes::ram_enemies_left_cnt) = 99;
        autoWave_ = level_ > 0;                            // en los niveles 1-40 la oleada de enemigos va sola
        if (level_ > 0 && !battle_->IsBossLevel()) {
            battle_->R(nes::ram_enemy_spawn_cnt) = static_cast<u8>(battle_->EnemyTotal());
        }
        frame_ = 0;
        pendingRestart_ = false;
        pendingTitle_ = false;
        // flujo de pantallas: introduccion de nivel, partida, recuento de puntos y game over
        app::GameFlow::Hooks hooks;
        hooks.loadNextLevel = [this](int level) {
            level_ = level;
            u8 blocks[nes::Battle::kWideBlocksY][nes::Battle::kWideBlocksX];
            app::GenerateWideLevel(level_, blocks);
            u8 strip[app::kLevelCols];
            app::GenerateTopStrip(level_, strip);
            battle_->NextStage(level_, blocks, strip);
            frame_ = 0;
        };
        hooks.returnToTitle = [this]() { pendingTitle_ = true; };     // se aplica fuera del Step (destruye el flujo)
        flow_ = std::make_unique<app::GameFlow>(*battle_, hooks);
        flow_->SetHiScore(hiScore_);
        flow_->Begin(wide_ ? level_ : 0);
    }

    nes::Battle& Battle() { return *battle_; }
    app::Apu& Audio() { return apu_; }

    void SetPad(int player, u8 bits) { pad_[player] = bits; }

    // Tecla de prueba: pasa al nivel siguiente de una (conserva vidas, puntajes y armas; despues del 40 vuelve al 1).
    void AdvanceLevel() {
        if (wide_ && flow_) flow_->SkipToNextLevel();
    }

    void Step() {
        if (scene_ != Scene::Game) {
            TickMenu();
            return;
        }
        if (netGame_) {
            pulseStart_ = false;
            StepNet();
            return;
        }
        const bool start = pulseStart_;
        pulseStart_ = false;
        if (start && flow_->mode() == app::GameFlow::Mode::Playing && !showHelp_) {
            scene_ = Scene::Pause;                               // Start abre el menu de pausa
            menuIndex_ = 0;
            g_samples.StopLoops();
            if (g_useSamples) g_samples.Trigger(0);
            return;
        }
        if (start && flow_->mode() != app::GameFlow::Mode::Playing) flow_->PressStart();
        for (int i = 0; i < 4; ++i) {
            u8 buttons = pad_[slotOf_[i]];                         // el jugador i del motor usa la ranura que eligio
            // Turbo: con el disparo mantenido se simulan apretadas seguidas (3 cuadros si, 3 no, unas 10 por segundo); la
            // primera es inmediata. El motor sigue limitando las balas en el aire segun el nivel de arma.
            if (g_cfg.turbo && (buttons & nes::con_btns_AB)) {
                if ((fireHeld_[i]++ % 6) >= 3) buttons = static_cast<u8>(buttons & ~nes::con_btns_AB);
            } else {
                fireHeld_[i] = 0;
            }
            battle_->SetButtons(i, buttons);
        }
        flow_->Step();
        ++frame_;
        CollectRumble();
        if (pendingTitle_) {
            ReturnToTitle();
            pendingTitle_ = false;
            return;
        }
        if (pendingRestart_) {
            if (level_ > 0) level_ = 1;
            Reset(players_);
        }
    }

    // Eventos de vibracion de este cuadro: disparo suave; recibir un tiro (corta); morir (fuerte y mas larga); agarrar un item
    // (pulsos al ritmo del sonido del item) y cada explosion del bombardeo (fuerte, en todos los mandos).
    void CollectRumble() {
        if (flow_->mode() != app::GameFlow::Mode::Playing) return;
        for (int i = 0; i < battle_->Players(); ++i) {
            if (netGame_ && !lan_.IsMine(i)) continue;                  // en red solo vibran los mandos de los jugadores de esta PC
            const int slot = netGame_ ? lan_.LocalIndexOf(i) : slotOf_[i];
            const u8 flags = battle_->TankFlags(i);
            const bool alive = (flags & 0x80) != 0 && flags < 0xE0;
            const bool exploding = (flags >> 4) >= 1 && (flags >> 4) <= 7;
            const bool died = prevAlive_[i] && exploding;
            if (died) rumble_.push_back({slot, 1.0f, 1.0f, 0.7f});                          // un tiro que te elimina
            const int hits = battle_->PlayerHits(i);
            if (!died && (hits > prevHits_[i] || battle_->Upgrade(i) < prevUpgrade_[i])) {
                rumble_.push_back({slot, 0.5f, 0.5f, 0.18f});                              // recibir un tiro: corta
            }
            const int pickups = battle_->ItemPickups(i);
            if (pickups > prevPickups_[i]) {
                // el sonido del item es un trino de ~0.68 s con un pulso cada ~55 ms
                for (int k = 0; k < 12; ++k) rumble_.push_back({slot, 0.3f, 0.8f, 0.03f, k * 0.055f});
            }
            const bool fire = (pad_[slot] & nes::con_btn_A) != 0;
            if (fire && !prevFire_[i] && alive) rumble_.push_back({slot, 0.0f, 0.25f, 0.07f});
            prevAlive_[i] = alive;
            prevUpgrade_[i] = battle_->Upgrade(i);
            prevFire_[i] = fire;
            prevHits_[i] = hits;
            prevPickups_[i] = pickups;
            int kills = 0;
            for (int k = 0; k < 4; ++k) kills += battle_->KillCount(i, k);
            if (kills > prevKills_[i]) rumble_.push_back({slot, 0.7f, 0.7f, 0.15f});       // destruiste un enemigo
            prevKills_[i] = kills;
        }
        const int blasts = battle_->BlastCount();
        if (blasts > prevBlasts_) {
            for (int slot = 0; slot < 4; ++slot) rumble_.push_back({slot, 1.0f, 1.0f, 0.55f});   // cada bomba, en todos los mandos
        }
        prevBlasts_ = blasts;
        const int eagles = battle_->EaglesDestroyed();
        if (eagles > prevEagles_) {
            for (int slot = 0; slot < 4; ++slot) rumble_.push_back({slot, 1.0f, 1.0f, 1.0f});   // cae un aguila (la propia o la enemiga): fuerte y larga
        }
        prevEagles_ = eagles;
    }
    std::vector<Rumble> TakeRumble() {
        std::vector<Rumble> out;
        out.swap(rumble_);
        return out;
    }

    // ---- menus: titulo -> jugar -> local (seleccion de jugadores), configuracion y pausa ----
    void StartInMenu() {
        scene_ = Scene::Title;
        menuIndex_ = 0;
        titleScroll_ = 240;                                    // el titulo sube desde abajo, como en la ROM
    }
    // Animacion del titulo: sube 1 px por cuadro (240 cuadros en total).
    void TickMenu() {
        if (scene_ == Scene::Title && titleScroll_ > 0) --titleScroll_;
        ++menuTick_;
        if (scene_ == Scene::LanHost || scene_ == Scene::LanSearch || scene_ == Scene::LanJoined) {
            lan_.Update();
            if (scene_ == Scene::LanJoined && lan_.Started()) StartNetGame();      // el anfitrion empezo la partida
            if (scene_ == Scene::LanSearch) {
                const int count = MenuItemCount();
                if (menuIndex_ >= count) menuIndex_ = count - 1;
            }
        }
    }
    bool InGame() const { return scene_ == Scene::Game; }
    Scene CurrentScene() const { return scene_; }
    // Los bucles de motor (zumbido de enemigos, motor del jugador) solo suenan jugando; en game over, record,
    // recuento de puntos y menus se cortan.
    bool LoopsAllowed() const {
        if (scene_ != Scene::Game || !flow_) return false;
        using M = app::GameFlow::Mode;
        const M m = flow_->mode();
        return m == M::Playing || m == M::StageIntro;
    }
    bool Capturing() const { return capture_ != Capture::None; }
    bool CapturingPad() const { return capture_ == Capture::Pad; }
    void SetUsingPad(bool v) { usingPad_ = v; }
    // En la seleccion de jugadores, cada mando conectado hace entrar a su jugador.
    void AutoJoinPads(const int padOfSlot[4]) {
        if (scene_ != Scene::Lobby) return;
        for (int s = 0; s < 4; ++s) {
            if (padOfSlot[s] >= 0) lobby_.Join(s);
        }
    }
    void PressStart() { pulseStart_ = true; }
    void SetHiScore(int v) { hiScore_ = v; }
    void ReturnToTitle() {
        if (netGame_) { netGame_ = false; lan_.Leave(); }
        if (battle_ && flow_) {
            lastScore_ = battle_->Score(0);
            hiScore_ = std::max(hiScore_, flow_->hiScore());
            if (g_cfg.hiScore != hiScore_) { g_cfg.hiScore = hiScore_; SaveConfig(); }
        }
        scene_ = Scene::Title;
        menuIndex_ = 0;
        showHelp_ = false;
        titleScroll_ = 240;
    }

    void StartLocalGame() {
        const int n = lobby_.Count();
        for (int i = 0; i < 4; ++i) {
            const int slot = i < n ? lobby_.SlotOfPlayer(i) : i;
            slotOf_[i] = slot;
            colors_[i] = i < n ? lobby_.Color(slot) : i;
        }
        level_ = 1;
        scene_ = Scene::Game;
        Reset(n);
    }

    int MenuItemCount() const {
        switch (scene_) {
            case Scene::Title: return 3;
            case Scene::PlayMenu: return 3;
            case Scene::Settings: return 7;
            case Scene::KeysPlayer: return 5;
            case Scene::KeysList: return 8;
            case Scene::PadSettings: return 6;
            case Scene::Pause: return 3;
            case Scene::LanMenu: return 4;
            case Scene::OnlineMenu: return 4;
            case Scene::LanSearch: return lan_.Games().empty() ? 1 : static_cast<int>(lan_.Games().size());
            default: return 0;
        }
    }

    void GoBack() {
        switch (scene_) {
            case Scene::PlayMenu: ReturnToTitle(); break;
            case Scene::Settings: scene_ = settingsReturn_; menuIndex_ = settingsReturn_ == Scene::Pause ? 1 : 2; break;
            case Scene::KeysPlayer: scene_ = Scene::Settings; menuIndex_ = 0; break;
            case Scene::KeysList: scene_ = Scene::KeysPlayer; menuIndex_ = keysPlayer_; break;
            case Scene::PadSettings: scene_ = Scene::Settings; menuIndex_ = 1; break;
            case Scene::Pause: scene_ = Scene::Game; break;                    // Cancelar en la pausa = continuar
            case Scene::Construction: ReturnToTitle(); break;
            case Scene::LanMenu: scene_ = Scene::PlayMenu; menuIndex_ = 1; break;
            case Scene::OnlineMenu: scene_ = Scene::PlayMenu; menuIndex_ = 2; break;
            case Scene::NameEntry: {
                const bool online = nameNext_ == Scene::OnlineMenu;
                if (g_cfg.playerName.empty()) { scene_ = Scene::PlayMenu; menuIndex_ = online ? 2 : 1; }      // sin nombre no se puede entrar a LAN / online
                else { scene_ = online ? Scene::OnlineMenu : Scene::LanMenu; menuIndex_ = 2; }
                break;
            }
            case Scene::CodeEntry: scene_ = Scene::OnlineMenu; menuIndex_ = 1; break;
            case Scene::LanHost: case Scene::LanSearch: case Scene::LanJoined:
                lan_.Leave();                                                  // se sale de la sala (avisa a los demas y cierra el puerto)
                scene_ = roomFrom_;
                menuIndex_ = 0;
                break;
            default: break;
        }
    }

    // ---- modo LAN ----
    std::string PlayerName() const { return g_cfg.playerName; }
    bool TextEntry() const { return scene_ == Scene::NameEntry || scene_ == Scene::CodeEntry; }
    bool InHostRoom() const { return scene_ == Scene::LanHost; }
    bool NetGame() const { return netGame_; }
    void TypeChar(int cp) {
        if (cp < 32 || cp > 126) return;
        if (scene_ == Scene::CodeEntry) {                                      // el codigo: letras y numeros, hasta 12
            const char c = static_cast<char>(std::toupper(cp));
            if (std::isalnum(static_cast<unsigned char>(c)) && codeBuf_.size() < 12) codeBuf_.push_back(c);
            return;
        }
        const std::string t = nameBuf_ + static_cast<char>(cp);
        const std::string clean = app::Lan::CleanName(t);
        if (clean.size() == t.size()) nameBuf_ = clean;
    }
    void EraseChar() {
        std::string& b = scene_ == Scene::CodeEntry ? codeBuf_ : nameBuf_;
        if (!b.empty()) b.pop_back();
    }
    void PasteText(const char* text) {                                         // Ctrl+V en el codigo
        if (scene_ != Scene::CodeEntry || !text) return;
        for (const char* p = text; *p; ++p) TypeChar(static_cast<unsigned char>(*p));
    }
    void BeginCodeEntry() {
        codeBuf_.clear();
        lanNote_.clear();
        scene_ = Scene::CodeEntry;
        menuIndex_ = 0;
    }
    void AcceptCode() {
        if (lan_.JoinCode(codeBuf_, g_cfg.playerName)) {
            lanNote_.clear();
            roomFrom_ = Scene::OnlineMenu;
            scene_ = Scene::LanJoined;
            menuIndex_ = 0;
        } else {
            lanNote_ = lan_.Error().empty() ? "CÓDIGO INVÁLIDO" : lan_.Error();
        }
    }
    // Sala del anfitrion: I abre/cierra la sala a internet, C copia el codigo.
    void ToggleInternet() {
        if (scene_ != Scene::LanHost) return;
        if (lan_.InternetState() == app::Lan::Internet::Off || lan_.InternetState() == app::Lan::Internet::Failed) {
            if (lan_.InternetState() == app::Lan::Internet::Failed) lan_.CloseInternet();
            lan_.OpenToInternet();
        } else {
            lan_.CloseInternet();
        }
    }
    const std::string& RoomCodeText() const { return lan_.RoomCode(); }
    void BeginNameEntry(Scene next) {
        nameNext_ = next;
        nameForced_ = g_cfg.playerName.empty();
        nameBuf_ = g_cfg.playerName;
        scene_ = Scene::NameEntry;
        menuIndex_ = 0;
    }
    void AcceptName() {
        std::string name = app::Lan::CleanName(nameBuf_);
        if (name.empty()) name = "JUGADOR";
        g_cfg.playerName = name;
        SaveConfig();
        const int then = nameThen_;
        nameThen_ = 0;
        if (nameNext_ == Scene::LanHost) StartHost(false);
        else if (nameNext_ == Scene::LanSearch) StartSearch();
        else if (nameNext_ == Scene::OnlineMenu) {
            if (then == 1) StartHost(true);
            else if (then == 2) BeginCodeEntry();
            else { scene_ = Scene::OnlineMenu; menuIndex_ = nameForced_ ? 0 : 2; }
        }
        else { scene_ = Scene::LanMenu; menuIndex_ = nameForced_ ? 0 : 2; }
    }
    void StartHost(bool online) {
        lanNote_.clear();
        roomFrom_ = online ? Scene::OnlineMenu : Scene::LanMenu;
        if (lan_.Host(g_cfg.playerName)) {
            scene_ = Scene::LanHost;
            menuIndex_ = 0;
            if (online) lan_.OpenToInternet();                                 // codigo para los de afuera (la red local la encuentra igual)
        } else {
            lanNote_ = lan_.Error();
            scene_ = roomFrom_;
            menuIndex_ = 0;
        }
    }
    void StartSearch() {
        lanNote_.clear();
        if (lan_.Search()) { scene_ = Scene::LanSearch; menuIndex_ = 0; }
        else { lanNote_ = lan_.Error(); scene_ = Scene::LanMenu; menuIndex_ = 1; }
        roomFrom_ = Scene::LanMenu;
    }
    void JoinSelected() {
        if (lan_.Games().empty()) return;
        const size_t i = std::min(static_cast<size_t>(std::max(0, menuIndex_)), lan_.Games().size() - 1);
        const app::LanGame g = lan_.Games()[i];
        if (!g.compatible) { lanNote_ = "ESA PARTIDA TIENE OTRA VERSIÓN DEL JUEGO"; return; }
        lanNote_.clear();
        if (lan_.Join(g, g_cfg.playerName)) { scene_ = Scene::LanJoined; menuIndex_ = 0; }
        else lanNote_ = lan_.Error();
    }
    void TryStartLan() {
        if (lan_.NumPlayers() < 2) { lanNote_ = "SE NECESITAN 2 JUGADORES O MÁS"; return; }
        lan_.StartGame(friendly_ == nes::Battle::FriendlyFire::Off ? 0 : (friendly_ == nes::Battle::FriendlyFire::Stun ? 1 : 2));
        StartNetGame();
    }
    void StartNetGame() {
        const int n = std::max(1, std::min(4, lan_.NumPlayers()));
        for (int i = 0; i < 4; ++i) {
            slotOf_[i] = i;
            colors_[i] = static_cast<size_t>(i) < lan_.Colors().size() ? lan_.Colors()[static_cast<size_t>(i)] : i;     // el color que eligio cada uno
        }
        const int ff = lan_.FriendlyFire();
        friendly_ = ff == 0 ? nes::Battle::FriendlyFire::Off : (ff == 2 ? nes::Battle::FriendlyFire::Damage : nes::Battle::FriendlyFire::Stun);
        level_ = 1;
        scene_ = Scene::Game;
        netGame_ = true;
        netFrame_ = 0;
        netStall_ = 0;
        Reset(n);
    }
    // Prueba automatica por red (sin ventana): arma o busca la partida, la empieza y avanza solo.
    int MySlot() const { return lan_.MySlot(); }
    const std::vector<int>& LocalPlayers() const { return lan_.MyPlayers(); }
    void SetAutoLan(int local, int expect) { autoLocal_ = local; autoExpect_ = expect; }
    // Pruebas: entra a una sala por internet con un codigo (sin pasar por los menus).
    bool AutoJoinCode(const std::string& code) {
        BeginCodeEntry();
        codeBuf_.clear();
        for (char c : code) TypeChar(static_cast<unsigned char>(c));
        AcceptCode();
        return scene_ == Scene::LanJoined;
    }
    std::string LanStatusText() const {
        return std::string("estado=") + std::to_string(static_cast<int>(lan_.state())) + " internet=" + std::to_string(static_cast<int>(lan_.InternetState())) +
               " codigo=" + lan_.RoomCode() + " nota=" + lan_.InternetNote() + " error=" + lan_.Error();
    }
    bool LanFailed() const { const auto st = lan_.state(); return st == app::Lan::State::Failed || st == app::Lan::State::Full || st == app::Lan::State::Closed; }
    const std::string& RoomCodeValue() const { return lan_.RoomCode(); }
    bool RoomInternetReady() const { return lan_.InternetState() == app::Lan::Internet::Ready; }
    bool RoomInternetFailed() const { return lan_.InternetState() == app::Lan::Internet::Failed; }
    int NetFrame() const { return netFrame_; }
    // Sala lista para la captura: el cliente ya eligio color y esta "listo" (lo ve el anfitrion).
    bool LanLobbyReady() const {
        return (scene_ == Scene::LanHost || (scene_ == Scene::LanJoined && lan_.state() == app::Lan::State::Joined)) &&
               lan_.NumPlayers() >= 2 && lan_.Ready().size() >= 2 && lan_.Ready()[1];
    }
    void LanAutoDrive(bool host, bool holdStart) {
        const bool inRoom = scene_ == Scene::LanHost || (scene_ == Scene::LanJoined && lan_.state() == app::Lan::State::Joined);
        if (inRoom) {
            while (lan_.LocalCount() < autoLocal_ && lan_.AddLocalPlayer()) {}        // suma los jugadores de esta PC
        }
        if (host && scene_ == Scene::LanHost && lan_.NumPlayers() >= autoExpect_ && lan_.LocalCount() >= autoLocal_ && !holdStart) TryStartLan();
        if (!host && scene_ == Scene::LanSearch && !lan_.Games().empty()) JoinSelected();
        if (!host && inRoom && !autoPicked_ && lan_.LocalCount() >= autoLocal_) {
            autoPicked_ = true;
            for (int k = 0; k < lan_.LocalCount(); ++k) { lan_.CycleColor(k, +1); lan_.ToggleReady(k); }    // elige color y marca "listo"
        }
    }
    // Termina la partida en red y vuelve al menu LAN con un aviso.
    void EndNet(const std::string& note) {
        netGame_ = false;
        lan_.Leave();
        lanNote_ = note;
        scene_ = roomFrom_;
        menuIndex_ = 0;
        showHelp_ = false;
    }
    // Un cuadro de la partida en red: botones locales -> anfitrion -> botones de todos -> simulacion.
    void StepNet() {
        lan_.Update();
        if (lan_.Lost()) { EndNet(lan_.Error().empty() ? "SE PERDIÓ LA CONEXIÓN" : lan_.Error()); return; }
        // los jugadores de esta PC usan los controles de las ranuras 1, 2, ... (como en la partida local)
        u8 local[4] = {0, 0, 0, 0};
        const int nLocal = std::min(4, lan_.LocalCount());
        for (int k = 0; k < nLocal; ++k) {
            u8 b = pad_[k];
            if (g_cfg.turbo && (b & nes::con_btns_AB)) {
                if ((fireHeld_[k]++ % 6) >= 3) b = static_cast<u8>(b & ~nes::con_btns_AB);
            } else {
                fireHeld_[k] = 0;
            }
            local[k] = static_cast<u8>(b & ~(nes::con_btn_Start | nes::con_btn_Select));    // sin pausa en red
        }
        lan_.SubmitLocal(netFrame_, local, nLocal);
        if (!lan_.FrameReady(netFrame_)) { ++netStall_; return; }                           // faltan botones de alguien: espera
        netStall_ = 0;
        u8 in[4];
        lan_.FrameInputs(netFrame_, in);
        for (int i = 0; i < 4; ++i) battle_->SetButtons(i, static_cast<u8>(in[i] & ~(nes::con_btn_Start | nes::con_btn_Select)));
        flow_->Step();
        ++frame_;
        ++netFrame_;
        CollectRumble();
        if (netFrame_ % 60 == 0) lan_.ReportHash(netFrame_, battle_->StateHash());
        if (pendingTitle_) {
            pendingTitle_ = false;
            lastScore_ = battle_->Score(lan_.MyPlayers().empty() ? 0 : lan_.MyPlayers()[0]);
            EndNet("");
        }
    }

    void MenuEvent(Ev ev, int slot) {
        if (scene_ == Scene::Game) return;
        if (scene_ == Scene::Title && titleScroll_ > 0) {                // cualquier boton salta la animacion del titulo
            titleScroll_ = 0;
            return;
        }
        if (capture_ != Capture::None) {                                       // esperando una tecla o un boton nuevo
            if (ev == Ev::Back) capture_ = Capture::None;
            return;
        }
        if (scene_ == Scene::NameEntry || scene_ == Scene::CodeEntry) {
            if (ev == Ev::Back) GoBack();
            else if (ev == Ev::Accept || ev == Ev::Start || ev == Ev::Fire) { if (scene_ == Scene::NameEntry) AcceptName(); else AcceptCode(); }
            return;
        }
        if (scene_ == Scene::LanHost || (scene_ == Scene::LanJoined && lan_.state() == app::Lan::State::Joined)) {
            // sala de espera: arriba/abajo elige mi color, disparo marca "listo"; el anfitrion empieza con Start
            // `slot` = el control que se uso (1 a 4): cada jugador de esta PC se maneja con el control de su numero
            const int k = ev == Ev::Accept ? 0 : slot;
            if (ev == Ev::Back) GoBack();
            else if (ev == Ev::Up) lan_.CycleColor(k, -1);
            else if (ev == Ev::Down) lan_.CycleColor(k, +1);
            else if (ev == Ev::Left) lan_.RemoveLocalPlayer(k);                      // izquierda: ese jugador se baja (el 1 no)
            else if (ev == Ev::Fire || ev == Ev::Accept) {
                if (k < lan_.LocalCount()) lan_.ToggleReady(k);                      // disparo: "listo"
                else if (k == lan_.LocalCount()) lan_.AddLocalPlayer();              // disparo de un control libre: se suma otro jugador
            }
            else if (ev == Ev::Start && scene_ == Scene::LanHost) TryStartLan();
            return;
        }
        if (scene_ == Scene::LanJoined) {
            const auto st = lan_.state();
            if (ev == Ev::Back || st == app::Lan::State::Failed || st == app::Lan::State::Full || st == app::Lan::State::Closed) {
                if (ev == Ev::Back || ev == Ev::Accept || ev == Ev::Start || ev == Ev::Fire) { lanNote_ = lan_.Error(); GoBack(); }
            }
            return;
        }
        if (scene_ == Scene::LanSearch && (ev == Ev::Accept || ev == Ev::Start || ev == Ev::Fire)) {
            JoinSelected();
            return;
        }
        if (scene_ == Scene::Lobby) {
            if (ev == Ev::Fire) lobby_.Join(slot);
            else if (ev == Ev::Up) lobby_.CycleColor(slot, -1);
            else if (ev == Ev::Down) lobby_.CycleColor(slot, +1);
            else if (ev == Ev::Start) StartLocalGame();
            else if (ev == Ev::Back) {
                scene_ = Scene::PlayMenu;
                menuIndex_ = 0;
            }
            return;
        }
        if (scene_ == Scene::Construction) {
            if (ev == Ev::Back || ev == Ev::Accept || ev == Ev::Start || ev == Ev::Fire) ReturnToTitle();
            return;
        }
        const int count = MenuItemCount();
        if (ev == Ev::Up) {
            menuIndex_ = (menuIndex_ + count - 1) % count;
        } else if (ev == Ev::Down) {
            menuIndex_ = (menuIndex_ + 1) % count;
        } else if (ev == Ev::Back) {
            GoBack();
        } else if ((ev == Ev::Left || ev == Ev::Right) && (scene_ == Scene::Settings || scene_ == Scene::PadSettings)) {
            ChangeSetting(menuIndex_);
        } else if (ev == Ev::Start && scene_ == Scene::Pause) {
            scene_ = Scene::Game;                                              // Start de nuevo: continuar
        } else if (ev == Ev::Accept || ev == Ev::Start || ev == Ev::Fire) {
            Activate(menuIndex_);
        }
    }

    void Activate(int index) {
        switch (scene_) {
            case Scene::Title:
                if (index == 0) { scene_ = Scene::PlayMenu; menuIndex_ = 0; }
                else if (index == 1) scene_ = Scene::Construction;
                else { settingsReturn_ = Scene::Title; scene_ = Scene::Settings; menuIndex_ = 0; }
                break;
            case Scene::PlayMenu:
                if (index == 0) {
                    lobby_ = app::Lobby();
                    scene_ = Scene::Lobby;
                } else if (index == 1) {
                    lanNote_.clear();
                    if (g_cfg.playerName.empty()) {
                        BeginNameEntry(Scene::LanMenu);                        // el nombre es obligatorio: se pide al entrar
                    } else {
                        scene_ = Scene::LanMenu;                               // LAN: crear partida / unirse
                        menuIndex_ = 0;
                    }
                } else if (index == 2) {
                    lanNote_.clear();
                    if (g_cfg.playerName.empty()) {
                        BeginNameEntry(Scene::OnlineMenu);                     // el nombre es obligatorio tambien para jugar online
                    } else {
                        scene_ = Scene::OnlineMenu;
                        menuIndex_ = 0;
                    }
                }
                break;
            case Scene::OnlineMenu:
                lanNote_.clear();
                if (index == 0) { if (g_cfg.playerName.empty()) { nameThen_ = 1; BeginNameEntry(Scene::OnlineMenu); } else StartHost(true); }
                else if (index == 1) { if (g_cfg.playerName.empty()) { nameThen_ = 2; BeginNameEntry(Scene::OnlineMenu); } else BeginCodeEntry(); }
                else if (index == 2) { nameThen_ = 0; BeginNameEntry(Scene::OnlineMenu); }
                else GoBack();
                break;
            case Scene::LanMenu:
                lanNote_.clear();
                if (index == 0) { if (g_cfg.playerName.empty()) BeginNameEntry(Scene::LanHost); else StartHost(false); }
                else if (index == 1) { if (g_cfg.playerName.empty()) BeginNameEntry(Scene::LanSearch); else StartSearch(); }
                else if (index == 2) { nameThen_ = 0; BeginNameEntry(Scene::LanMenu); }
                else GoBack();
                break;
            case Scene::Settings:
                if (index == 0) { scene_ = Scene::KeysPlayer; menuIndex_ = 0; }
                else if (index == 1) { scene_ = Scene::PadSettings; menuIndex_ = 0; }
                else if (index >= 2 && index <= 5) ChangeSetting(index);
                else GoBack();
                break;
            case Scene::KeysPlayer:
                if (index < 4) { keysPlayer_ = index; scene_ = Scene::KeysList; menuIndex_ = 0; }
                else GoBack();
                break;
            case Scene::KeysList:
                if (index < app::Config::kActions) { capture_ = Capture::Key; captureTarget_ = index; }
                else if (index == 6) { RestoreKeys(keysPlayer_); }
                else GoBack();
                break;
            case Scene::PadSettings:
                if (index <= 2) { capture_ = Capture::Pad; captureTarget_ = index; }
                else if (index == 3) ChangeSetting(3);
                else if (index == 4) { g_cfg.ResetPad(); SaveConfig(); }
                else GoBack();
                break;
            case Scene::Pause:
                if (index == 0) scene_ = Scene::Game;
                else if (index == 1) { settingsReturn_ = Scene::Pause; scene_ = Scene::Settings; menuIndex_ = 0; }
                else ReturnToTitle();
                break;
            default: break;
        }
    }

    void RestoreKeys(int player) {
        for (int a = 0; a < app::Config::kActions; ++a) g_cfg.key[player][a] = g_cfg.defaultKey[player][a];
        SaveConfig();
    }

    // Opcion con valor: en Configuracion (2 fuego amigo, 3 sonido) y en Mando (3 vibracion).
    void ChangeSetting(int index) {
        if (scene_ == Scene::Settings) {
            if (index == 2) {
                CycleFriendlyFire();
            } else if (index == 3) {
                g_useSamples = !g_useSamples;
                if (!g_useSamples) g_samples.StopLoops();
            } else if (index == 4) {
                ToggleFullscreen();
            } else if (index == 5) {
                g_cfg.turbo = !g_cfg.turbo;
            }
        } else if (scene_ == Scene::PadSettings && index == 3) {
            g_cfg.vibration = !g_cfg.vibration;
            if (g_cfg.vibration) {                                   // al activarla vibra fuerte para confirmar que funciona
                for (int slot = 0; slot < 4; ++slot) PadRumble(g_padOfSlot[slot], 1.0f, 1.0f, 0.6f);
            }
        }
        SaveConfig();
    }

    void CaptureKey(int key) {
        if (capture_ != Capture::Key) return;
        g_cfg.key[keysPlayer_][captureTarget_] = key;
        capture_ = Capture::None;
        SaveConfig();
    }
    void CapturePad(int button) {
        if (capture_ != Capture::Pad) return;
        if (captureTarget_ == 0) g_cfg.padFire = button;
        else if (captureTarget_ == 1) g_cfg.padCancel = button;
        else g_cfg.padStart = button;
        capture_ = Capture::None;
        SaveConfig();
    }

    // Textos de ayuda segun el dispositivo en uso (teclado o mando).
    std::string BackHint() const { return usingPad_ ? PadButtonName(g_cfg.padCancel) + " VOLVER" : "ESC VOLVER"; }
    std::string NavHint() const {
        return usingPad_ ? "{DPAD} ELEGIR  " + PadButtonName(g_cfg.padFire) + " ACEPTAR" : "ARRIBA ABAJO ELEGIR  P ACEPTAR";
    }
    std::string ChangeHint() const {
        return usingPad_ ? "{DPAD} CAMBIAR  " + PadButtonName(g_cfg.padFire) + " ACEPTAR  " + PadButtonName(g_cfg.padCancel) + " VOLVER"
                         : "IZQUIERDA DERECHA CAMBIAR  ESC VOLVER";
    }

    void RenderMenuScene(std::vector<uint8_t>& rgba) const {
        app::MenuView v;
        v.selected = menuIndex_;
        if (scene_ == Scene::Lobby) {
            app::LobbyView lv;
            for (int sl = 0; sl < 4; ++sl) {
                lv.joined[sl] = lobby_.Joined(sl);
                lv.color[sl] = lobby_.Color(sl);
                lv.joinKey[sl] = (usingPad_ && g_padOfSlot[sl] >= 0) ? PadButtonName(g_cfg.padFire) : KeyName(g_cfg.key[sl][app::Config::Fire]);
            }
            lv.hint = usingPad_ ? PadButtonName(g_cfg.padFire) + " PARA UNIRSE  {DPAD} COLOR" : "DISPARO PARA UNIRSE  ARRIBA ABAJO COLOR";
            lv.startText = usingPad_ ? PadButtonName(g_cfg.padStart) + " PARA EMPEZAR" : "P START PARA EMPEZAR";
            lv.backText = BackHint();
            lv.upDownText = usingPad_ ? "{DPAD}" : "ARRIBA ABAJO";
            renderer_.RenderLobby(lv, rgba);
            return;
        }
        if (scene_ == Scene::LanHost || (scene_ == Scene::LanJoined && lan_.state() == app::Lan::State::Joined)) {
            app::LobbyView lv;                                                       // sala de espera: como la seleccion de color
            lv.net = true;
            for (size_t i = 0; i < 4 && i < lan_.Players().size(); ++i) {
                lv.joined[i] = true;
                lv.name[i] = lan_.Players()[i];
                lv.color[i] = lan_.Colors()[i];
                lv.ready[i] = lan_.Ready()[i];
                lv.mine[i] = lan_.IsMine(static_cast<int>(i));
                const int k = lan_.LocalIndexOf(static_cast<int>(i));
                if (k >= 0) {                                                        // la tecla (o boton) de disparo de ese jugador local
                    lv.meLine2[i] = ((usingPad_ && g_padOfSlot[k] >= 0) ? PadButtonName(g_cfg.padFire) : KeyName(g_cfg.key[k][app::Config::Fire])) + " LISTO";
                }
            }
            lv.upDownText = "ELEGÍ COLOR";
            if (lan_.NumPlayers() < 4 && lan_.LocalCount() < 4) {
                const int next = lan_.LocalCount();
                lv.addHint = "OTRO JUGADOR EN ESTA PC: DISPARO J" + std::to_string(next + 1) + " (" +
                             ((usingPad_ && g_padOfSlot[next] >= 0) ? PadButtonName(g_cfg.padFire) : KeyName(g_cfg.key[next][app::Config::Fire])) + ")";
            }
            const bool host = scene_ == Scene::LanHost;
            int notReady = 0;
            for (int i = 0; i < lan_.NumPlayers(); ++i) notReady += lan_.Ready()[static_cast<size_t>(i)] ? 0 : 1;
            const std::string readyKey = usingPad_ ? PadButtonName(g_cfg.padFire) : KeyName(g_cfg.key[0][app::Config::Fire]);
            if (host) {
                using Net = app::Lan::Internet;
                const Net net = lan_.InternetState();
                lv.hint = "SOS EL ANFITRIÓN: EMPEZÁ CUANDO QUIERAS";
                std::string waiting = lan_.NumPlayers() < 2 ? "ESPERANDO JUGADORES..." : (notReady > 0 ? "TODAVÍA HAY JUGADORES SIN ESTAR LISTOS" : "");
                if (net == Net::Ready) {
                    lv.hint = "CÓDIGO PARA JUGAR POR INTERNET: " + lan_.RoomCode();
                    lv.addHint = "C: COPIAR EL CÓDIGO   I: CERRAR INTERNET";
                    if (lan_.InternetNote().compare(0, 8, "SIN UPNP") == 0) waiting = lan_.InternetNote();      // hay que abrir el puerto a mano
                } else if (net == Net::Working) {
                    lv.addHint = lan_.InternetNote();
                } else if (net == Net::Failed) {
                    lv.addHint = "NO SE PUDO ABRIR A INTERNET. I: REINTENTAR";
                    waiting = lan_.InternetNote();
                } else if (lv.addHint.empty()) {
                    lv.addHint = "I: ABRIR LA SALA A INTERNET (CÓDIGO PARA LOS DE AFUERA)";
                }
                lv.note = !lanNote_.empty() ? lanNote_ : waiting;
                lv.startText = (usingPad_ ? PadButtonName(g_cfg.padStart) : std::string("ENTER")) + " EMPEZAR  " + readyKey + " LISTO";
            } else {
                lv.hint = "SALA DE " + lan_.HostName();
                lv.startText = "ESPERANDO AL ANFITRIÓN  " + readyKey + " LISTO";
            }
            lv.backText = BackHint();
            renderer_.RenderLobby(lv, rgba);
            return;
        }
        const char* const yesNo[2] = {"NO", "SÍ"};
        switch (scene_) {
            case Scene::Title:
                v.logo = true;
                v.scrollY = titleScroll_;
                v.p1Score = lastScore_;
                v.hiScore = hiScore_;
                v.items = {"JUGAR", "CONSTRUCCIÓN", "CONFIGURACIÓN"};
                v.footer = NavHint();
                break;
            case Scene::PlayMenu:
                v.title = "JUGAR";
                v.items = {"LOCAL", "LAN", "ONLINE"};
                v.enabled = {true, true, true};
                v.footer = BackHint();
                break;
            case Scene::LanMenu:
                v.title = "LAN";
                v.items = {"CREAR PARTIDA", "UNIRSE", "NOMBRE: " + (g_cfg.playerName.empty() ? std::string("(SIN NOMBRE)") : g_cfg.playerName), "VOLVER"};
                v.note = lanNote_;
                v.footer = NavHint();
                break;
            case Scene::OnlineMenu:
                v.title = "ONLINE";
                v.items = {"CREAR PARTIDA", "UNIRSE CON CÓDIGO", "NOMBRE: " + (g_cfg.playerName.empty() ? std::string("(SIN NOMBRE)") : g_cfg.playerName), "VOLVER"};
                v.note = lanNote_;
                v.footer = NavHint();
                break;
            case Scene::CodeEntry: {
                v.title = "CÓDIGO DE LA SALA";
                std::string shown;
                for (size_t i = 0; i < codeBuf_.size(); ++i) {
                    if (i == 4 || i == 8) shown.push_back('-');
                    shown.push_back(codeBuf_[i]);
                }
                v.items = {shown + (((menuTick_ / 30) & 1) ? " " : "_")};
                v.selected = -1;
                v.note = lanNote_.empty() ? "ESCRIBÍ O PEGÁ (CTRL+V) EL CÓDIGO DEL ANFITRIÓN" : lanNote_;
                v.footer = BackHint() + "  ENTER UNIRSE";
                break;
            }
            case Scene::NameEntry:
                v.title = "TU NOMBRE EN LA RED";
                v.items = {nameBuf_ + (((menuTick_ / 30) & 1) ? " " : "_")};
                v.selected = -1;
                v.note = "ESCRIBÍ TU NOMBRE Y APRETÁ ENTER";
                v.footer = BackHint();
                break;
            case Scene::LanSearch: {
                v.title = "BUSCANDO PARTIDAS" + std::string(static_cast<size_t>((menuTick_ / 20) % 4), '.');
                for (const auto& g : lan_.Games()) {
                    v.items.push_back(g.name + "  " + std::to_string(g.players) + "/" + std::to_string(g.maxPlayers) + (g.compatible ? "" : " (OTRA VERSIÓN)"));
                }
                if (lan_.Games().empty()) { v.items.push_back("..."); v.enabled = {false}; v.selected = -1; }
                v.note = !lanNote_.empty() ? lanNote_ : (lan_.Games().empty() ? "NO SE ENCONTRÓ NINGUNA PARTIDA TODAVÍA" : "");
                v.footer = BackHint() + "  ENTER UNIRSE";
                break;
            }
            case Scene::LanJoined: {
                const auto st = lan_.state();
                if (st == app::Lan::State::Joining) {
                    v.title = "CONECTANDO...";
                } else {
                    v.title = "NO SE PUDO ENTRAR";
                    v.note = lan_.Error();
                }
                v.footer = BackHint();
                break;
            }
            case Scene::Settings:
                v.title = "CONFIGURACIÓN";
                v.items = {"MAPEO DE CONTROLES", "MAPEO DE MANDO", "FUEGO AMIGO: " + FriendlyText(),
                           std::string("SONIDO: ") + (g_useSamples ? "REMASTERIZADO" : "ORIGINAL"),
                           std::string("PANTALLA COMPLETA: ") + yesNo[IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) ? 1 : 0],
                           std::string("TURBO: ") + yesNo[g_cfg.turbo ? 1 : 0], "VOLVER"};
                v.footer = ChangeHint();
                break;
            case Scene::KeysPlayer:
                v.title = "MAPEO DE CONTROLES";
                v.items = {"JUGADOR 1", "JUGADOR 2", "JUGADOR 3", "JUGADOR 4", "VOLVER"};
                v.footer = BackHint();
                break;
            case Scene::KeysList: {
                static const char* const kNames[app::Config::kActions] = {"ARRIBA", "ABAJO", "IZQUIERDA", "DERECHA", "DISPARO", "DISPARO 2"};
                v.title = "CONTROLES JUGADOR " + std::to_string(keysPlayer_ + 1);
                for (int a = 0; a < app::Config::kActions; ++a) {
                    const bool waiting = capture_ == Capture::Key && captureTarget_ == a;
                    v.items.push_back(std::string(kNames[a]) + ": " + (waiting ? "..." : KeyName(g_cfg.key[keysPlayer_][a])));
                }
                v.items.push_back("RESTABLECER");
                v.items.push_back("VOLVER");
                if (capture_ == Capture::Key) v.note = "PRESIONÁ UNA TECLA";
                v.footer = BackHint();
                break;
            }
            case Scene::PadSettings: {
                auto row = [&](const char* name, int button, int index) {
                    const bool waiting = capture_ == Capture::Pad && captureTarget_ == index;
                    return std::string(name) + ": " + (waiting ? "..." : PadButtonName(button));
                };
                v.title = "MAPEO DE MANDO";
                v.items = {row("DISPARAR", g_cfg.padFire, 0), row("CANCELAR", g_cfg.padCancel, 1), row("INICIO", g_cfg.padStart, 2),
                           std::string("VIBRACIÓN: ") + yesNo[g_cfg.vibration ? 1 : 0], "RESTABLECER", "VOLVER"};
                if (capture_ == Capture::Pad) v.note = "PRESIONÁ UN BOTÓN DEL MANDO";
                v.footer = BackHint();
                break;
            }
            default:
                v.title = "CONSTRUCCIÓN";
                v.note = "PRÓXIMAMENTE";
                v.footer = BackHint();
                break;
        }
        renderer_.RenderMenu(v, rgba);
    }

    void ToggleAutoWave() {
        autoWave_ = !autoWave_;
        battle_->R(nes::ram_enemy_spawn_cnt) = static_cast<u8>(autoWave_ ? battle_->EnemyTotal() : 0);
    }

    void Render(std::vector<uint8_t>& rgba) const {
        if (scene_ != Scene::Game && scene_ != Scene::Pause) {
            RenderMenuScene(rgba);
            return;
        }
        app::HudInfo hud;
        hud.players = players_;
        hud.stage = level_ > 0 ? level_ : 1;
        using Mode = app::GameFlow::Mode;
        hud.curtain = flow_->mode() == Mode::StageIntro ? flow_->curtainRows() : 0;
        if (scene_ == Scene::Pause) {
            hud.pauseItems = {"CONTINUAR", "CONFIGURACIÓN", "SALIR"};
            hud.pauseSelected = menuIndex_;
        }
        hud.introLevel = level_;
        if (netGame_ && lan_.Desynced()) hud.banner = "DESINCRONIZADO";
        else if (netGame_ && netStall_ > 20) hud.banner = "ESPERANDO A LOS DEMÁS...";
        if (battle_->IsBossLevel()) hud.introSubtitle = "DESTRUYE LA BASE ENEMIGA";
        hud.paused = battle_->R(nes::ram_pause_flag) != 0;
        hud.overlay = showHelp_;
        hud.leftLines = {
            "TECLAS - ESC",
            "P1 WASD",
            " FIRE F G",
            "P2 FLECHAS",
            " FIRE ENTER",
            "P3 IJKL",
            " FIRE U O",
            "P4 NUM 8456",
            " FIRE NUM0 ENTER",
            "1-4 ENEMIGO",
            " SHIFT BRILLA",
            "F1 CASCO",
            "F2 RELOJ",
            "F3 PALA",
            "F4 ESTRELLA",
            "F5 GRANADA",
            "F6 TANQUE",
            "F7 PISTOLA",
            "F8 MUSICA",
            "F9 RECORD",
            "F10 GAME OVER",
            "M SILENCIO",
            "ALT+ENTER PANTALLA",
            "N ORIGINAL/REMASTER.",
            "K MATAR TODOS",
            "C LIMPIAR",
            "R REINICIAR",
            "T 1-4 JUGADORES",
            "V OLEADA",
            "REPAG AVPAG NIVEL",
            "L SIGUIENTE NIVEL",
            "F12 ITEM ALIADOS",
            "B BOMBARDEO",
            "H FUEGO AMIGO",
            "P PAUSA",
            "RETROCESO MENU",
            "Q SALIR",
        };
        hud.rightLines = {
            "ESTADO",
            "FRAME " + std::to_string(frame_),
            "ENEMIGOS " + std::to_string(CountEnemies()),
            "ITEM " + (battle_->BonusActive() ? std::to_string(battle_->R(nes::ram_bonus_id)) : std::string("-")),
            "NIVEL " + std::to_string(level_) + " ESC " + std::to_string(battle_->Tier()),
            "JUGADORES " + std::to_string(players_),
            "F AMIGO " + FriendlyText(),
            "VIDAS " + LivesText(),
            "NIVEL P1 " + std::to_string(battle_->Upgrade(0) / 0x20),
            "CASCO P1 " + std::to_string(battle_->Helmet(0)),
            "RELOJ " + std::to_string(battle_->R(nes::ram_clock_timer)),
            "PALA " + std::to_string(battle_->R(nes::ram_shovel_timer)),
            "POS P1 " + std::to_string(battle_->TankX(0)) + "-" + std::to_string(battle_->TankY(0)),
        };
        if (flow_->mode() == Mode::ScoreScreen) {
            renderer_.RenderScoreScreen(flow_->tally(), rgba);
        } else if (flow_->mode() == Mode::GameOver) {
            renderer_.RenderGameOver(rgba);
        } else if (flow_->mode() == Mode::HighScore) {
            renderer_.RenderHighScore(flow_->hiScore(), flow_->highScoreTimer(), rgba);
        } else {
            renderer_.Render(*battle_, hud, rgba);
        }
    }

    int CountEnemies() const {
        int n = 0;
        for (int x = battle_->Players(); x <= battle_->LastEnemy(); ++x) {
            if (battle_->TankFlags(x) != 0) ++n;
        }
        return n;
    }

    std::string LivesText() const {
        std::string out;
        for (int p = 0; p < battle_->Players(); ++p) {
            out += std::to_string(battle_->Lives(p));
            if (p + 1 < battle_->Players()) out += "-";
        }
        return out;
    }

    void Do(const std::string& name) {
        if (name.size() == 2 && (name[0] == 'e' || name[0] == 'f')) {
            battle_->DebugSpawnEnemy(name[1] - '0', name[0] == 'f');
        } else if (name.size() == 2 && name[0] == 'b') {
            battle_->DebugSpawnBonus(name[1] - '0');
        } else if (name.size() == 2 && name[0] == 's') {
            battle_->DebugPlaySound(name[1] - '0');
        } else if (!name.empty() && name[0] == 'm' && name.size() >= 3) {
            // acciones de menu: mup mdown mleft mright mok mback mstart mfireN mupN mdownN (N = ranura 1..4)
            std::string key = name.substr(1);
            int slot = 0;
            if (key.back() >= '1' && key.back() <= '4') {
                slot = key.back() - '1';
                key.pop_back();
            }
            if (key == "up") MenuEvent(Ev::Up, slot);
            else if (key == "down") MenuEvent(Ev::Down, slot);
            else if (key == "left") MenuEvent(Ev::Left, slot);
            else if (key == "right") MenuEvent(Ev::Right, slot);
            else if (key == "ok") MenuEvent(Ev::Accept, slot);
            else if (key == "back") MenuEvent(Ev::Back, slot);
            else if (key == "start") MenuEvent(Ev::Start, slot);
            else if (key == "fire") MenuEvent(Ev::Fire, slot);
        } else if (name == "pad") {
            usingPad_ = true;                                  // simula que se esta usando el mando (textos de ayuda)
        } else if (name == "win") {                          // simula ganar el nivel con algunos enemigos destruidos
            battle_->R(nes::ram_enemy_spawn_cnt) = 0;
            battle_->DebugClearEnemies();
            for (int p = 0; p < battle_->Players(); ++p)
                for (int k = 0; k < 4; ++k) battle_->DebugSetKills(p, k, (p + 1) * (4 - k) % 6 + (p == 0 ? 2 : 0));
        } else if (name == "over") {
            battle_->R(nes::ram_game_over_flag) = 0;
        } else if (name == "hs") {                            // prueba: record superado + game over
            for (int i = 0; i < 40; ++i) battle_->AddBonusPoints(0, 0);
            battle_->R(nes::ram_game_over_flag) = 0;
        } else if (name == "steal") {                          // prueba: el jugador 1 se queda sin vidas (robo de vidas)
            battle_->SetLives(0, 0);
            battle_->DebugSetTankFlags(0, 0);
        } else if (name == "bomb") {                          // prueba: arranca el bombardeo ya
            battle_->DebugStartBombing();
        } else if (name == "al") {
            battle_->SpawnAllies(0);
        } else if (name == "esc") {
            ToggleHelp();
        } else if (name == "k") {
            battle_->DebugKillAllEnemies();
        } else if (name == "c") {
            battle_->DebugClearEnemies();
        } else if (name == "p") {
            pulseStart_ = true;
        } else if (!name.empty() && (name[0] == '+' || name[0] == '-')) {
            // +up -up +down +left +right +fire, con un 2, 3 o 4 al final para esos jugadores
            std::string key = name.substr(1);
            int player = 0;
            if (!key.empty() && key.back() >= '2' && key.back() <= '4') {
                player = key.back() - '1';
                key.pop_back();
            }
            u8 bit = 0;
            if (key == "up") bit = nes::con_btn_Up;
            else if (key == "down") bit = nes::con_btn_Down;
            else if (key == "left") bit = nes::con_btn_Left;
            else if (key == "right") bit = nes::con_btn_Right;
            else if (key == "fire") bit = nes::con_btn_A;
            if (name[0] == '+') pad_[player] |= bit; else pad_[player] &= static_cast<u8>(~bit);
        }
    }

    // Aplica el pulso de Start pedido por una accion (un cuadro).
    u8 StartBit() {
        const bool p = pulseStart_;
        pulseStart_ = false;
        return p ? nes::con_btn_Start : 0;
    }

    void ToggleHelp() { showHelp_ = !showHelp_; }
    void SetFriendlyFromConfig() {
        friendly_ = g_cfg.friendlyFire == 0 ? nes::Battle::FriendlyFire::Off
                  : (g_cfg.friendlyFire == 2 ? nes::Battle::FriendlyFire::Damage : nes::Battle::FriendlyFire::Stun);
    }
    void CycleFriendlyFire() {
        using FF = nes::Battle::FriendlyFire;
        const FF cur = battle_->GetFriendlyFire();
        friendly_ = cur == FF::Off ? FF::Stun : (cur == FF::Stun ? FF::Damage : FF::Off);
        g_cfg.friendlyFire = friendly_ == FF::Off ? 0 : (friendly_ == FF::Stun ? 1 : 2);
        battle_->SetFriendlyFire(friendly_);
    }
    std::string FriendlyText() const {
        using FF = nes::Battle::FriendlyFire;
        const FF f = battle_->GetFriendlyFire();
        return f == FF::Off ? "NO" : (f == FF::Stun ? "PARALIZA" : "DAÑA");
    }
    void SetWide(bool wide) { wide_ = wide; }
    void SetLevel(int level) { level_ = level < 0 ? 0 : (level > app::kLevelCount ? app::kLevelCount : level); }
    int level() const { return level_; }
    bool HelpShown() const { return showHelp_; }
    int players() const { return players_; }
    int frame() const { return frame_; }

private:
    app::Renderer renderer_;
    app::Apu apu_;
    std::unique_ptr<nes::Battle> battle_;
    u8 pad_[4] = {0, 0, 0, 0};
    int players_ = 1;
    int frame_ = 0;
    int fireHeld_[4] = {0, 0, 0, 0};
    app::Lan lan_;                                        // modo LAN
    bool netGame_ = false;                                // jugando una partida en red
    int netFrame_ = 0, netStall_ = 0, menuTick_ = 0, lanBackIndex_ = 0;
    std::string nameBuf_, lanNote_;
    Scene nameNext_ = Scene::LanMenu;
    bool nameForced_ = false;
    int nameThen_ = 0;                                    // despues de pedir el nombre en ONLINE: 1 crear sala, 2 pedir codigo
    std::string codeBuf_;
    Scene roomFrom_ = Scene::LanMenu;                     // menu al que se vuelve al salir de una sala
    bool autoPicked_ = false;
    int autoLocal_ = 1, autoExpect_ = 2;                  // pruebas por red: jugadores de esta PC y total que espera el anfitrion                      // cuadros que lleva apretado el disparo (turbo)
    bool autoWave_ = false;
    bool pendingRestart_ = false;
    bool pendingTitle_ = false;
    Scene scene_ = Scene::Game;
    Scene settingsReturn_ = Scene::Title;
    Capture capture_ = Capture::None;
    int captureTarget_ = 0;
    int keysPlayer_ = 0;
    bool usingPad_ = false;
    std::vector<Rumble> rumble_;
    bool prevAlive_[4] = {false, false, false, false};
    int prevUpgrade_[4] = {0, 0, 0, 0};
    bool prevFire_[4] = {false, false, false, false};
    int prevHits_[4] = {0, 0, 0, 0}, prevPickups_[4] = {0, 0, 0, 0}, prevKills_[4] = {0, 0, 0, 0}, prevBlasts_ = 0, prevEagles_ = 0;
    int menuIndex_ = 0;
    int titleScroll_ = 0;
    int lastScore_ = 0;
    int hiScore_ = 20000;
    app::Lobby lobby_;
    int slotOf_[4] = {0, 1, 2, 3};
    int colors_[4] = {0, 1, 2, 3};
    std::unique_ptr<app::GameFlow> flow_;
    bool showHelp_ = false;
    nes::Battle::FriendlyFire friendly_ = nes::Battle::FriendlyFire::Stun;
    int level_ = 0;
    bool wide_ = true;                  // campo panoramico (por defecto) o el clasico de 13x13
    bool pulseStart_ = false;
};

std::string FindChr(const char* argv0) {
    std::vector<std::string> candidates = {"assets/chr.bin", "../assets/chr.bin", "../../assets/chr.bin", "../../../assets/chr.bin"};
    std::string dir = argv0 ? argv0 : "";
    const size_t slash = dir.find_last_of("/\\");
    if (slash != std::string::npos) {
        const std::string base = dir.substr(0, slash + 1);
        candidates.push_back(base + "assets/chr.bin");
        candidates.push_back(base + "../assets/chr.bin");
        candidates.push_back(base + "../../assets/chr.bin");
    }
#ifdef BCNES_ASSETS_DIR
    candidates.push_back(std::string(BCNES_ASSETS_DIR) + "chr.bin");
#endif
    for (const std::string& c : candidates) {
        FILE* f = std::fopen(c.c_str(), "rb");
        if (f) {
            std::fclose(f);
            return c;
        }
    }
    return "assets/chr.bin";
}

std::vector<Action> ParseActions(const std::string& spec) {
    std::vector<Action> out;
    std::stringstream ss(spec);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const size_t colon = item.find(':');
        if (colon == std::string::npos) continue;
        out.push_back(Action{std::atoi(item.substr(0, colon).c_str()), item.substr(colon + 1)});
    }
    return out;
}

int RunShot(TestScreen& screen, const std::string& file, int frames, const std::vector<Action>& actions, int scale) {
    for (int f = 0; f < frames; ++f) {
        for (const Action& a : actions) {
            if (a.frame == f) screen.Do(a.name);
        }
        screen.Step();
    }
    std::vector<uint8_t> rgba;
    screen.Render(rgba);
    std::vector<uint8_t> big;
    int w = app::kScreenW;
    int h = app::kScreenH;
    if (scale > 1) {                                    // zoom entero para revisar sprites de cerca
        w *= scale;
        h *= scale;
        big.resize(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                std::memcpy(&big[(static_cast<size_t>(y) * w + x) * 4],
                            &rgba[(static_cast<size_t>(y / scale) * app::kScreenW + x / scale) * 4], 4);
            }
        }
    }
    Image img{};
    img.data = scale > 1 ? big.data() : rgba.data();
    img.width = w;
    img.height = h;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    return ExportImage(img, file.c_str()) ? 0 : 1;
}

// Corre N cuadros sin ventana y guarda el audio generado (mono, 16 bits, 44100 Hz).
int RunWav(TestScreen& screen, const std::string& file, int frames, const std::vector<Action>& actions) {
    constexpr int kChunk = app::Apu::kSampleRate / 60;
    std::vector<int16_t> pcm;
    std::vector<int16_t> chunk(kChunk);
    for (int f = 0; f < frames; ++f) {
        for (const Action& a : actions) {
            if (a.frame == f) screen.Do(a.name);
        }
        screen.Step();
        screen.Audio().Generate(chunk.data(), kChunk);
        pcm.insert(pcm.end(), chunk.begin(), chunk.end());
    }
    FILE* out = std::fopen(file.c_str(), "wb");
    if (!out) return 1;
    const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * 2);
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, out); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, out); };
    std::fwrite("RIFF", 1, 4, out);
    w32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, out);
    w32(16);
    w16(1);
    w16(1);
    w32(app::Apu::kSampleRate);
    w32(app::Apu::kSampleRate * 2);
    w16(2);
    w16(16);
    std::fwrite("data", 1, 4, out);
    w32(dataBytes);
    std::fwrite(pcm.data(), 2, pcm.size(), out);
    std::fclose(out);
    return 0;
}

u8 PadFromKeys(int player) {
    u8 pad = 0;
    const int* k = g_cfg.key[player];
    auto down = [&](int action) { return k[action] != 0 && IsKeyDown(k[action]); };
    if (down(app::Config::Up)) pad |= nes::con_btn_Up;
    if (down(app::Config::Down)) pad |= nes::con_btn_Down;
    if (down(app::Config::Left)) pad |= nes::con_btn_Left;
    if (down(app::Config::Right)) pad |= nes::con_btn_Right;
    if (down(app::Config::Fire)) pad |= nes::con_btn_A;
    if (down(app::Config::Fire2)) pad |= nes::con_btn_B;
    const int pad_id = g_padOfSlot[player];
    if (pad_id >= 0 && IsGamepadAvailable(pad_id)) {
        if (IsGamepadButtonDown(pad_id, GAMEPAD_BUTTON_LEFT_FACE_UP)) pad |= nes::con_btn_Up;
        if (IsGamepadButtonDown(pad_id, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) pad |= nes::con_btn_Down;
        if (IsGamepadButtonDown(pad_id, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) pad |= nes::con_btn_Left;
        if (IsGamepadButtonDown(pad_id, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) pad |= nes::con_btn_Right;
        if (IsGamepadButtonDown(pad_id, g_cfg.padFire)) pad |= nes::con_btn_A;
        if (IsGamepadButtonDown(pad_id, g_cfg.padFire2)) pad |= nes::con_btn_B;
        const float ax = GetGamepadAxisMovement(pad_id, GAMEPAD_AXIS_LEFT_X);
        const float ay = GetGamepadAxisMovement(pad_id, GAMEPAD_AXIS_LEFT_Y);
        if (ay < -0.5f) pad |= nes::con_btn_Up;
        if (ay > 0.5f) pad |= nes::con_btn_Down;
        if (ax < -0.5f) pad |= nes::con_btn_Left;
        if (ax > 0.5f) pad |= nes::con_btn_Right;
    }
    return pad;
}

bool AnyPadPressed(int button) {
    for (int g = 0; g < 4; ++g) {
        if (IsGamepadAvailable(g) && button > 0 && IsGamepadButtonPressed(g, button)) return true;
    }
    return false;
}

// Primer boton de un mando que se acaba de apretar en este cuadro (0 si ninguno). GetGamepadButtonPressed() de raylib devuelve
// tambien un boton que sigue apretado, asi que el A con el que se eligio la accion se capturaba solo; este usa el flanco.
int NewPadButton() {
    for (int g = 0; g < 4; ++g) {
        if (!IsGamepadAvailable(g)) continue;
        for (int b = 1; b <= GAMEPAD_BUTTON_RIGHT_THUMB; ++b) {
            if (IsGamepadButtonPressed(g, b)) return b;
        }
    }
    return 0;
}

int RunWindow(TestScreen& screen, double selfTestSeconds, const std::string& assetsDir) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "Battle City");
    if (g_cfg.fullscreen) ToggleBorderlessWindowed();                          // se recuerda la pantalla completa
    {
        Image icon = LoadImage((assetsDir + "icon.png").c_str());              // el tanque amarillo
        if (icon.data) {
            SetWindowIcon(icon);
            UnloadImage(icon);
        }
    }
    InitAudioDevice();
    SetAudioStreamBufferSizeDefault(2048);                         // dos mitades de 1024 muestras (~23 ms)
    AudioStream stream = LoadAudioStream(app::Apu::kSampleRate, 16, 1);
    SetAudioStreamCallback(stream, AudioCallback);
    PlayAudioStream(stream);
    std::vector<int16_t> chunk(AudioRing::kFrameSamples);
    if (!g_samples.Load(assetsDir + "sounds/")) {
        g_useSamples = false;                                       // sin archivos: queda el sintetizador de la NES
        std::fprintf(stderr, "No se encontraron los sonidos en %ssounds/: se usa el sintetizador de la NES\n", assetsDir.c_str());
    }
    bool muted = false;
    SetWindowMinSize(432, 240);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    Image blank = GenImageColor(app::kScreenW, app::kScreenH, BLACK);
    ImageFormat(&blank, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Texture2D tex = LoadTextureFromImage(blank);
    UnloadImage(blank);
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);

    std::vector<uint8_t> rgba;
    double acc = 0.0;
    constexpr double kStep = 1.0 / 60.0;

    while (!WindowShouldClose() && !(selfTestSeconds > 0 && GetTime() > selfTestSeconds)) {
        ServiceRumbleTimers();
        // ---- dispositivo en uso: los textos de ayuda cambian entre teclado y mando ----
        const int keyNow = GetKeyPressed();
        const int padNow = GetGamepadButtonPressed();
        if (padNow > 0) screen.SetUsingPad(true);
        else if (keyNow != 0) screen.SetUsingPad(false);
        {
            bool available[4];
            for (int g = 0; g < 4; ++g) available[g] = IsGamepadAvailable(g);
            app::AssignPads(available, g_padOfSlot);                 // los mandos conectados toman a los jugadores solos
            screen.AutoJoinPads(g_padOfSlot);
        }
        const bool padStartPressed = AnyPadPressed(g_cfg.padStart);

        // ---- menus ----
        if (!screen.InGame()) {
            if (screen.TextEntry()) {                                          // escribiendo el nombre de red
                for (int cp = GetCharPressed(); cp > 0; cp = GetCharPressed()) screen.TypeChar(cp);
                if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) screen.EraseChar();
                if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)) screen.PasteText(GetClipboardText());
                if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || AnyPadPressed(g_cfg.padFire) || AnyPadPressed(g_cfg.padStart)) screen.MenuEvent(TestScreen::Ev::Accept, 0);
                else if (IsKeyPressed(KEY_ESCAPE) || AnyPadPressed(g_cfg.padCancel)) screen.MenuEvent(TestScreen::Ev::Back, 0);
            } else if (screen.InHostRoom() && (IsKeyPressed(KEY_I) || IsKeyPressed(KEY_C))) {
                if (IsKeyPressed(KEY_I)) screen.ToggleInternet();
                if (IsKeyPressed(KEY_C) && !screen.RoomCodeText().empty()) SetClipboardText(screen.RoomCodeText().c_str());
            } else if (screen.Capturing()) {
                if (screen.CapturingPad()) {
                    if (const int newPad = NewPadButton()) screen.CapturePad(newPad);
                    else if (keyNow == KEY_ESCAPE) screen.MenuEvent(TestScreen::Ev::Back, 0);
                } else if (keyNow == KEY_ESCAPE) {
                    screen.MenuEvent(TestScreen::Ev::Back, 0);
                } else if (keyNow != 0) {
                    screen.CaptureKey(keyNow);
                }
            } else {
                static u8 prevPad[4] = {0, 0, 0, 0};
                for (int sl = 0; sl < 4; ++sl) {
                    const u8 pad = PadFromKeys(sl);
                    const u8 pressed = static_cast<u8>(pad & ~prevPad[sl]);
                    prevPad[sl] = pad;
                    if (pressed & nes::con_btn_Up) screen.MenuEvent(TestScreen::Ev::Up, sl);
                    if (pressed & nes::con_btn_Down) screen.MenuEvent(TestScreen::Ev::Down, sl);
                    if (pressed & nes::con_btn_Left) screen.MenuEvent(TestScreen::Ev::Left, sl);
                    if (pressed & nes::con_btn_Right) screen.MenuEvent(TestScreen::Ev::Right, sl);
                    if (pressed & nes::con_btn_A) screen.MenuEvent(TestScreen::Ev::Fire, sl);
                }
                if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || padStartPressed) screen.MenuEvent(TestScreen::Ev::Start, 0);
                if (IsKeyPressed(KEY_SPACE)) screen.MenuEvent(TestScreen::Ev::Accept, 0);
                if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE) || AnyPadPressed(g_cfg.padCancel)) {
                    screen.MenuEvent(TestScreen::Ev::Back, 0);
                }
            }
            screen.TickMenu();
            screen.Render(rgba);
            UpdateTexture(tex, rgba.data());
            BeginDrawing();
            ClearBackground(Color{16, 16, 24, 255});
            {
                const float sw = static_cast<float>(GetScreenWidth());
                const float sh = static_cast<float>(GetScreenHeight());
                const float scale = std::min(sw / app::kScreenW, sh / app::kScreenH);
                const float dw = app::kScreenW * scale;
                const float dh = app::kScreenH * scale;
                DrawTexturePro(tex, Rectangle{0, 0, static_cast<float>(app::kScreenW), static_cast<float>(app::kScreenH)},
                               Rectangle{(sw - dw) / 2, (sh - dh) / 2, dw, dh}, Vector2{0, 0}, 0.0f, WHITE);
            }
            EndDrawing();
            if (!screen.Capturing() && IsKeyPressed(KEY_M)) {
                muted = !muted;
                SetMasterVolume(muted ? 0.0f : 1.0f);
            }
            if (IsKeyPressed(KEY_F11) || ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_ENTER))) {
                ToggleFullscreen();
            }
            acc = 0.0;
            continue;
        }

        // ---- teclas de prueba ----
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        const bool dbg = !screen.NetGame();                                // en red las teclas de prueba no hacen nada
        for (int i = 0; i < 4 && dbg; ++i) {
            if (IsKeyPressed(KEY_ONE + i)) screen.Battle().DebugSpawnEnemy(i, shift);
        }
        for (int i = 0; i < 7 && dbg; ++i) {
            if (IsKeyPressed(KEY_F1 + i)) screen.Battle().DebugSpawnBonus(i);
        }
        if (dbg && IsKeyPressed(KEY_F12)) screen.Battle().DebugSpawnBonus(7);
        for (int i = 0; i < 3 && dbg; ++i) {
            if (IsKeyPressed(KEY_F8 + i)) screen.Battle().DebugPlaySound(i);
        }
        if (IsKeyPressed(KEY_ESCAPE)) screen.ToggleHelp();
        if (screen.HelpShown() && IsKeyPressed(KEY_Q)) break;
        if (screen.HelpShown() && IsKeyPressed(KEY_BACKSPACE)) {
            screen.ReturnToTitle();                                    // Retroceso en el cartel de Esc: vuelve al menu
            continue;
        }
        if (IsKeyPressed(KEY_N)) {
            g_useSamples = !g_useSamples;
            if (!g_useSamples) g_samples.StopLoops();
            SaveConfig();
        }
        if (IsKeyPressed(KEY_M)) {
            muted = !muted;
            SetMasterVolume(muted ? 0.0f : 1.0f);
        }
        if (dbg && IsKeyPressed(KEY_B)) screen.Battle().DebugStartBombing();
        if (dbg && IsKeyPressed(KEY_K)) screen.Battle().DebugKillAllEnemies();
        if (dbg && IsKeyPressed(KEY_C)) screen.Battle().DebugClearEnemies();
        if (dbg && IsKeyPressed(KEY_PAGE_UP)) { screen.SetLevel(screen.level() + 1); screen.Reset(screen.players()); }
        if (dbg && IsKeyPressed(KEY_PAGE_DOWN)) { screen.SetLevel(screen.level() - 1); screen.Reset(screen.players()); }
        if (dbg && IsKeyPressed(KEY_L)) screen.AdvanceLevel();
        if (dbg && IsKeyPressed(KEY_R)) screen.Reset(screen.players());
        if (dbg && IsKeyPressed(KEY_T)) screen.Reset(screen.players() % 4 + 1);
        if (dbg && IsKeyPressed(KEY_V)) screen.ToggleAutoWave();
        if (dbg && IsKeyPressed(KEY_H)) screen.CycleFriendlyFire();
        if (IsKeyPressed(KEY_F11) || ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_ENTER))) {
            ToggleFullscreen();                                        // Alt+Enter: pantalla completa
        }

        // ---- bucle fijo a 60 Hz ----
        acc += std::min(GetFrameTime(), 0.1f);
        while (acc >= kStep) {
            if (IsKeyPressed(KEY_P) || padStartPressed) screen.PressStart();   // Start: abre la pausa / continua en las pantallas
            for (int p = 0; p < 4; ++p) {
                screen.SetPad(p, PadFromKeys(p));
            }
            screen.Step();
            {
                static std::vector<TestScreen::Rumble> g_pendingRumble;               // vibraciones con retardo
                std::vector<TestScreen::Rumble> fresh = screen.TakeRumble();
                if (!g_cfg.vibration) {
                    g_pendingRumble.clear();
                } else {
                    for (const TestScreen::Rumble& r : fresh) g_pendingRumble.push_back(r);
                    for (size_t i = 0; i < g_pendingRumble.size();) {                     // arrancan cuando se cumple su retardo
                        TestScreen::Rumble& r = g_pendingRumble[i];
                        if (r.delay <= 0.0f) {
                            if (g_padOfSlot[r.slot] >= 0 && IsGamepadAvailable(g_padOfSlot[r.slot])) {
                                PadRumble(g_padOfSlot[r.slot], r.left, r.right, r.seconds);
                            }
                            g_pendingRumble.erase(g_pendingRumble.begin() + static_cast<long>(i));
                        } else {
                            r.delay -= static_cast<float>(kStep);
                            ++i;
                        }
                    }
                }
            }
            screen.Audio().Generate(chunk.data(), AudioRing::kFrameSamples);   // el audio de este cuadro, alineado con el juego
            if (g_useSamples && !app::SampleBank::NeedsSynth(screen.Battle())) {
                std::fill(chunk.begin(), chunk.end(), int16_t{0});            // suenan las grabaciones, no el sintetizador
            }
            g_ring.Push(chunk.data(), AudioRing::kFrameSamples);
            acc -= kStep;
        }

        if (g_useSamples) g_samples.Update(screen.Battle(), screen.LoopsAllowed());

        screen.Render(rgba);
        UpdateTexture(tex, rgba.data());

        BeginDrawing();
        ClearBackground(Color{16, 16, 24, 255});
        const float sw = static_cast<float>(GetScreenWidth());
        const float sh = static_cast<float>(GetScreenHeight());
        float scale = std::min(sw / app::kScreenW, sh / app::kScreenH);
        // la imagen ocupa toda la ventana (16:9); con otra proporcion quedan franjas negras
        const float dw = app::kScreenW * scale;
        const float dh = app::kScreenH * scale;
        DrawTexturePro(tex, Rectangle{0, 0, static_cast<float>(app::kScreenW), static_cast<float>(app::kScreenH)},
                       Rectangle{(sw - dw) / 2, (sh - dh) / 2, dw, dh}, Vector2{0, 0}, 0.0f, WHITE);
        EndDrawing();
    }
    if (selfTestSeconds > 0) {
        std::printf("audio: %d cortes (underruns), %d descartes\n", g_ring.Underruns(), g_ring.Dropped());
    }
    g_samples.Unload();
    UnloadAudioStream(stream);
    CloseAudioDevice();
    UnloadTexture(tex);
    CloseWindow();
    return 0;
}

} // namespace

#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall AttachConsole(unsigned long processId);
#endif

// Prueba por red entre dos procesos (el mismo ejecutable, uno con --lan-host y otro con --lan-join): arman la partida por
// los sockets, juegan N cuadros con botones de mentira y escriben el hash del estado final; tienen que dar el mismo.
int RunLanAuto(TestScreen& screen, bool host, const std::string& name, int frames, const std::string& outFile, const std::string& shotFile, int local, int expect,
               bool online, const std::string& codeOut, const std::string& codeIn) {
    screen.SetAutoLan(local, expect);
    screen.StartInMenu();                                  // sin esto la pantalla arranca en la partida de prueba
    g_cfg.playerName = name;
    bool codeWritten = false, joinedByCode = false;
    if (host) screen.StartHost(online);
    else if (codeIn.empty()) screen.StartSearch();
    auto script = [](int player, int frame) -> u8 {
        const int phase = (frame / 20 + player * 3) % 8;
        u8 v = 0;
        if (phase == 0 || phase == 1) v |= nes::con_btn_Up;
        if (phase == 2 || phase == 3) v |= nes::con_btn_Left;
        if (phase == 4 || phase == 5) v |= nes::con_btn_Right;
        if (phase == 6) v |= nes::con_btn_Down;
        if ((frame + player) % 14 < 4) v |= nes::con_btn_A;
        return v;
    };
    const auto t0 = std::chrono::steady_clock::now();
    std::string result = "TIMEOUT";
    int code = 2;
    bool shotTaken = false;
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 40.0) {
        if (!screen.InGame()) {
            screen.TickMenu();
            if (host && online && !codeOut.empty() && !codeWritten && (screen.RoomInternetReady() || screen.RoomInternetFailed())) {
                codeWritten = true;                                              // el anfitrion publica su codigo para el otro proceso
                if (FILE* f = std::fopen(codeOut.c_str(), "w")) {
                    std::fprintf(f, "%s\n%s\n", screen.RoomCodeValue().c_str(), screen.LanStatusText().c_str());
                    std::fclose(f);
                }
                std::printf("%s\n", screen.LanStatusText().c_str());
            }
            if (!host && !codeIn.empty() && !joinedByCode) {                     // el cliente espera el codigo y entra con el
                if (FILE* f = std::fopen(codeIn.c_str(), "r")) {
                    char line[128] = {};
                    if (std::fgets(line, sizeof line, f) && std::strlen(line) >= 12) {
                        std::string code(line);
                        while (!code.empty() && (code.back() == '\n' || code.back() == '\r')) code.pop_back();
                        joinedByCode = true;
                        screen.AutoJoinCode(code);
                    }
                    std::fclose(f);
                }
            }
            static int shotWait = 0;
            const bool wantShot = !shotFile.empty() && !shotTaken;
            if (wantShot && screen.LanLobbyReady() && ++shotWait > 60) {        // captura de la sala con "LISTO" ya visible
                std::vector<uint8_t> rgba;
                screen.Render(rgba);
                Image img{rgba.data(), app::kScreenW, app::kScreenH, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                ExportImage(img, shotFile.c_str());
                shotTaken = true;
            }
            screen.LanAutoDrive(host, wantShot);
            if (screen.CurrentScene() == TestScreen::Scene::LanMenu) { result = "SALIO DEL LAN"; break; }
            if (screen.CurrentScene() == TestScreen::Scene::LanJoined && screen.LanFailed()) { result = "NO SE PUDO ENTRAR: " + screen.LanStatusText(); break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } else {
            for (size_t k = 0; k < screen.LocalPlayers().size() && k < 4; ++k) screen.SetPad(static_cast<int>(k), script(screen.LocalPlayers()[k], screen.NetFrame()));
            screen.Step();
            if (!screen.NetGame()) { result = "SE CORTO LA PARTIDA (" + screen.LanStatusText() + ")"; break; }
            if (screen.NetFrame() >= frames) {
                char buf[128];
                std::snprintf(buf, sizeof buf, "OK pc=%d locales=%d frames=%d hash=%u", screen.MySlot(), static_cast<int>(screen.LocalPlayers().size()), screen.NetFrame(), screen.Battle().StateHash());
                result = buf;
                code = 0;
                // sigue unos segundos para que los demas puedan terminar los ultimos cuadros
                const auto tEnd = std::chrono::steady_clock::now();
                while (std::chrono::duration<double>(std::chrono::steady_clock::now() - tEnd).count() < 4.0 && screen.NetGame()) {
                    screen.Step();
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                break;
            }
        }
    }
    if (result == "TIMEOUT") result += " (" + screen.LanStatusText() + ", frame=" + std::to_string(screen.NetFrame()) + ")";
    if (!outFile.empty()) {
        if (FILE* f = std::fopen(outFile.c_str(), "w")) {
            std::fprintf(f, "%s\n", result.c_str());
            std::fclose(f);
        }
    }
    std::printf("%s\n", result.c_str());
    return code;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    // El programa no abre su propia ventana de consola; si lo lanzan desde una terminal, vuelve a usar la de esa terminal
    // (asi siguen saliendo los mensajes de --shot, --selftest, etc.).
    if (AttachConsole(0xFFFFFFFFul)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
#endif
    std::string shotFile;
    std::string wavFile;
    double selfTest = 0.0;
    bool classic = false;
    int level = 0;
    int frames = 120;
    int players = 1;
    bool direct = false;                                   // con --players/--level/--classic salta los menus
    bool wantMenu = false;
    std::string actionSpec;
    int scale = 1;
    std::string lanName, lanOut;                          // pruebas por red entre dos procesos
    std::string lanShot;
    std::string lanCodeOut, lanCodeIn;
    bool lanOnline = false;
    int lanRole = 0, lanFrames = 600, lanLocal = 1, lanExpect = 2;                     // 1 anfitrion, 2 se une
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--shot" && i + 1 < argc) shotFile = argv[++i];
        else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
        else if (a == "--players" && i + 1 < argc) { players = std::atoi(argv[++i]); direct = true; }
        else if (a == "--menu") wantMenu = true;
        else if (a == "--do" && i + 1 < argc) actionSpec = argv[++i];
        else if (a == "--scale" && i + 1 < argc) scale = std::atoi(argv[++i]);
        else if (a == "--wav" && i + 1 < argc) wavFile = argv[++i];
        else if (a == "--selftest" && i + 1 < argc) selfTest = std::atof(argv[++i]);
        else if (a == "--classic") { classic = true; direct = true; }
        else if (a == "--lan-host" && i + 1 < argc) { lanRole = 1; lanName = argv[++i]; }
        else if (a == "--lan-join" && i + 1 < argc) { lanRole = 2; lanName = argv[++i]; }
        else if (a == "--lan-frames" && i + 1 < argc) lanFrames = std::atoi(argv[++i]);
        else if (a == "--lan-out" && i + 1 < argc) lanOut = argv[++i];
        else if (a == "--lan-shot" && i + 1 < argc) lanShot = argv[++i];
        else if (a == "--lan-local" && i + 1 < argc) lanLocal = std::atoi(argv[++i]);
        else if (a == "--lan-online") lanOnline = true;
        else if (a == "--lan-code-out" && i + 1 < argc) lanCodeOut = argv[++i];
        else if (a == "--lan-code-in" && i + 1 < argc) lanCodeIn = argv[++i];
        else if (a == "--lan-expect" && i + 1 < argc) lanExpect = std::atoi(argv[++i]);
        else if (a == "--level" && i + 1 < argc) { level = std::atoi(argv[++i]); direct = true; }
    }

    const std::string chrPath = FindChr(argv[0]);
    const std::string assetsDir = chrPath.substr(0, chrPath.find_last_of("/\\") == std::string::npos ? 0 : chrPath.find_last_of("/\\") + 1);
    ApplyDefaultConfig(g_cfg);
    {
        const size_t slash = std::string(argv[0]).find_last_of("/\\");
        g_cfgPath = (slash == std::string::npos ? std::string() : std::string(argv[0]).substr(0, slash + 1)) + "battlecitynes_settings.txt";
        if (g_cfg.Load(g_cfgPath)) {
            if (g_cfg.version < 2) g_cfg.ResetPad();
            if (g_cfg.hiScore < 20000) g_cfg.hiScore = 20000;               // configuracion vieja: el mando vuelve a los valores por defecto (B vuelve)
            g_useSamples = g_cfg.useSamples;
        }
    }
    TestScreen screen;
    screen.SetFriendlyFromConfig();
    screen.SetHiScore(g_cfg.hiScore);
    screen.SetWide(!classic);
    screen.SetLevel(level);
    if (!screen.Init(chrPath)) {
        std::fprintf(stderr, "No se encontro assets/chr.bin (los graficos de la ROM)\n");
        return 2;
    }
    screen.Reset(players);
    if (lanRole != 0 && !lanCodeIn.empty() && lanRole == 1) lanRole = 1;
    if (lanRole != 0) return RunLanAuto(screen, lanRole == 1, lanName, lanFrames, lanOut, lanShot, lanLocal, lanExpect, lanOnline, lanCodeOut, lanCodeIn);
    const bool headless = !wavFile.empty() || !shotFile.empty();
    if (wantMenu || (!direct && !headless)) screen.StartInMenu();       // sin argumentos: pantalla de titulo

    if (!wavFile.empty()) {
        return RunWav(screen, wavFile, frames, ParseActions(actionSpec));
    }
    if (!shotFile.empty()) {
        return RunShot(screen, shotFile, frames, ParseActions(actionSpec), scale);
    }
    return RunWindow(screen, selfTest, assetsDir);
}
