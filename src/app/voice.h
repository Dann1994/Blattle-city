#pragma once

// Chat de voz de las partidas en red. Todo pasa por la sala (el anfitrion retransmite), asi que funciona igual en la misma red y por internet.
//   microfono (16 kHz, 16 bits) -> deteccion de voz (no manda silencio) -> ADPCM de 4 bits (64 kbps) -> paquetes de 40 ms -> sala
//   sala -> un buffer por hablante -> mezcla -> altavoz
// Este archivo no depende de la red ni de la ventana: el microfono y el altavoz los ponen las plataformas (Windows: waveIn; Android: AudioRecord por JNI;
// la reproduccion la hace el juego con un AudioStream de raylib que pide muestras con Mix()).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace app {

constexpr int kVoiceRate = 16000;                // muestras por segundo
constexpr int kVoiceFrame = 320;                 // 20 ms
constexpr int kVoiceFramesPerPacket = 2;         // 40 ms por paquete

// ---- ADPCM IMA de 4 bits ----
struct AdpcmState {
    int predictor = 0;                           // ultima muestra reconstruida
    int index = 0;                               // paso actual (0..88)
};
// Codifica `n` muestras (n par) a n/2 bytes; deja en `st` el estado al final.
void AdpcmEncode(const int16_t* in, int n, AdpcmState& st, uint8_t* out);
void AdpcmDecode(const uint8_t* in, int n, AdpcmState& st, int16_t* out);      // n muestras (n par) desde n/2 bytes

// ---- base64 (para viajar en los mensajes de texto de la sala) ----
std::string Base64Encode(const uint8_t* data, size_t n);
bool Base64Decode(const std::string& text, std::vector<uint8_t>& out);

// Un paquete de voz: cabecera (secuencia, estado del codificador, cantidad de cuadros) + los cuadros codificados.
std::vector<uint8_t> VoicePacketBuild(uint16_t seq, const AdpcmState& start, int frames, const uint8_t* adpcm);
bool VoicePacketParse(const uint8_t* data, size_t n, uint16_t& seq, AdpcmState& start, int& frames, const uint8_t*& adpcm);

// ---- microfono de la plataforma ----
// Entrega cuadros de kVoiceFrame muestras a un callback (en el hilo de captura). Devuelve false si no hay microfono o falta permiso.
class MicCapture {
public:
    virtual ~MicCapture() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual bool Running() const = 0;
    std::function<void(const int16_t*, int)> onFrame;       // (muestras, cantidad)
};
std::unique_ptr<MicCapture> CreateMicCapture();             // nullptr si la plataforma no tiene

// ---- el chat ----
class VoiceChat {
public:
    VoiceChat();
    ~VoiceChat();
    VoiceChat(const VoiceChat&) = delete;
    VoiceChat& operator=(const VoiceChat&) = delete;

    // Prende el microfono (si hay) y empieza a generar paquetes. false: sin microfono o sin permiso (se puede reintentar).
    bool Start();
    void Stop();
    bool Active() const { return active_.load(); }
    bool MicAvailable() const { return micOk_.load(); }
    void SetMuted(bool m) { muted_ = m; }
    bool Muted() const { return muted_.load(); }
    void ToggleMute() { muted_ = !muted_.load(); }
    void SetVolume(float v) { volume_ = v < 0 ? 0 : (v > 2 ? 2 : v); }       // 1 = normal
    bool TransmittingNow() const;                                              // hablando ahora (para el indicador)

    // Alimentar con muestras del microfono (lo llama la captura; tambien sirve para las pruebas).
    void PushMic(const int16_t* samples, int n);
    // Paquetes listos para mandar a la sala (cada uno ya es el contenido del mensaje, sin base64).
    bool PopPacket(std::vector<uint8_t>& out);

    // Voz que llega de la sala: de quien (id de PC en la sala) y el paquete.
    void Receive(int speaker, const uint8_t* data, size_t n);
    // Mezcla `n` muestras de todos los que hablan (para el altavoz).
    void Mix(int16_t* out, int n);
    bool Speaking(int speaker) const;                                          // llego voz de ese hablante hace poco
    void ClearSpeakers();

    // Parametros (las pruebas los usan)
    static constexpr int kPrimeSamples = kVoiceRate * 6 / 100;                // 60 ms de colchon antes de empezar a sonar
    static constexpr int kMaxBuffered = kVoiceRate;                            // 1 s como maximo por hablante

private:
    struct Speaker {
        std::deque<int16_t> buf;
        bool primed = false;
        int lastSeq = -1;
        std::chrono::steady_clock::time_point last{};
    };
    void OnFrame(const int16_t* s, int n);
    std::unique_ptr<MicCapture> mic_;
    std::atomic<bool> active_{false}, micOk_{false}, muted_{false};
    std::atomic<float> volume_{1.0f};
    mutable std::mutex mu_;
    std::deque<std::vector<uint8_t>> out_;
    std::map<int, Speaker> speakers_;
    // captura
    std::vector<int16_t> pend_;                  // cuadros esperando completar un paquete
    AdpcmState encState_;                        // estado del codificador
    AdpcmState packetStart_;                     // estado al empezar el paquete en curso
    std::vector<uint8_t> packetBytes_;
    int packetFrames_ = 0;
    uint16_t seq_ = 0;
    int hang_ = 0;                               // cuadros que quedan de "sigo hablando" despues de la ultima voz
    double noise_ = 200;                         // piso de ruido estimado
    std::atomic<int> lastLevel_{0};
    std::atomic<std::chrono::steady_clock::rep> lastTx_{0};
};

} // namespace app
