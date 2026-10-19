#include "voice.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace app {

// ---------------------------------------------------------------------------
// ADPCM IMA
// ---------------------------------------------------------------------------

namespace {

const int kStep[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
    2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};
const int kIndexAdj[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

inline int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Aplica un codigo de 4 bits al estado y devuelve la muestra reconstruida.
inline int ApplyCode(AdpcmState& st, int code) {
    const int step = kStep[st.index];
    int diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;
    st.predictor = Clamp(st.predictor + ((code & 8) ? -diff : diff), -32768, 32767);
    st.index = Clamp(st.index + kIndexAdj[code & 15], 0, 88);
    return st.predictor;
}

} // namespace

void AdpcmEncode(const int16_t* in, int n, AdpcmState& st, uint8_t* out) {
    for (int i = 0; i < n; ++i) {
        const int step = kStep[st.index];
        int diff = in[i] - st.predictor;
        int code = 0;
        if (diff < 0) { code = 8; diff = -diff; }
        int s = step;
        if (diff >= s) { code |= 4; diff -= s; }
        s >>= 1;
        if (diff >= s) { code |= 2; diff -= s; }
        s >>= 1;
        if (diff >= s) code |= 1;
        ApplyCode(st, code);
        if ((i & 1) == 0) out[i >> 1] = static_cast<uint8_t>(code);
        else out[i >> 1] = static_cast<uint8_t>(out[i >> 1] | (code << 4));
    }
}

void AdpcmDecode(const uint8_t* in, int n, AdpcmState& st, int16_t* out) {
    for (int i = 0; i < n; ++i) {
        const int code = (i & 1) == 0 ? (in[i >> 1] & 15) : (in[i >> 1] >> 4);
        out[i] = static_cast<int16_t>(ApplyCode(st, code));
    }
}

// ---------------------------------------------------------------------------
// base64
// ---------------------------------------------------------------------------

std::string Base64Encode(const uint8_t* d, size_t n) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = (static_cast<uint32_t>(d[i]) << 16) | (i + 1 < n ? static_cast<uint32_t>(d[i + 1]) << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        out += t[(v >> 18) & 63];
        out += t[(v >> 12) & 63];
        out += i + 1 < n ? t[(v >> 6) & 63] : '=';
        out += i + 2 < n ? t[v & 63] : '=';
    }
    return out;
}

bool Base64Decode(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=') break;
        else return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Paquete de voz: [secuencia u16][predictor s16][indice u8][cuadros u8] + cuadros ADPCM (80 bytes cada uno)
// ---------------------------------------------------------------------------

std::vector<uint8_t> VoicePacketBuild(uint16_t seq, const AdpcmState& st, int frames, const uint8_t* adpcm) {
    std::vector<uint8_t> p(6 + static_cast<size_t>(frames) * (kVoiceFrame / 2));
    p[0] = static_cast<uint8_t>(seq & 0xFF);
    p[1] = static_cast<uint8_t>(seq >> 8);
    p[2] = static_cast<uint8_t>(st.predictor & 0xFF);
    p[3] = static_cast<uint8_t>((st.predictor >> 8) & 0xFF);
    p[4] = static_cast<uint8_t>(st.index);
    p[5] = static_cast<uint8_t>(frames);
    std::memcpy(p.data() + 6, adpcm, p.size() - 6);
    return p;
}

bool VoicePacketParse(const uint8_t* d, size_t n, uint16_t& seq, AdpcmState& st, int& frames, const uint8_t*& adpcm) {
    if (n < 6) return false;
    seq = static_cast<uint16_t>(d[0] | (d[1] << 8));
    st.predictor = static_cast<int16_t>(d[2] | (d[3] << 8));
    st.index = d[4];
    frames = d[5];
    if (st.index > 88 || frames < 1 || frames > 8 || n != 6 + static_cast<size_t>(frames) * (kVoiceFrame / 2)) return false;
    adpcm = d + 6;
    return true;
}

// ---------------------------------------------------------------------------
// Chat
// ---------------------------------------------------------------------------

VoiceChat::VoiceChat() = default;

VoiceChat::~VoiceChat() { Stop(); }

bool VoiceChat::Start() {
    if (!mic_) mic_ = CreateMicCapture();
    bool ok = false;
    if (mic_) {
        mic_->onFrame = [this](const int16_t* s, int n) { PushMic(s, n); };
        ok = mic_->Running() || mic_->Start();
    }
    micOk_ = ok;
    active_ = true;
    return ok;
}

void VoiceChat::Stop() {
    if (mic_) mic_->Stop();
    micOk_ = false;
    active_ = false;
}

bool VoiceChat::TransmittingNow() const {
    const auto last = std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(lastTx_.load()));
    return lastTx_.load() != 0 && std::chrono::steady_clock::now() - last < std::chrono::milliseconds(350);
}

void VoiceChat::PushMic(const int16_t* samples, int n) {
    std::lock_guard<std::mutex> lk(mu_);
    pend_.insert(pend_.end(), samples, samples + n);
    while (static_cast<int>(pend_.size()) >= kVoiceFrame) {
        OnFrame(pend_.data(), kVoiceFrame);
        pend_.erase(pend_.begin(), pend_.begin() + kVoiceFrame);
    }
}

// Un cuadro de 20 ms del microfono (con el cerrojo tomado).
void VoiceChat::OnFrame(const int16_t* s, int n) {
    double e = 0;
    for (int i = 0; i < n; ++i) e += static_cast<double>(s[i]) * s[i];
    const double rms = std::sqrt(e / n);
    lastLevel_ = static_cast<int>(rms);
    const bool talking = !muted_.load() && rms > std::max(450.0, noise_ * 3.0);
    if (!talking) noise_ = std::max(60.0, std::min(1500.0, noise_ * 0.98 + rms * 0.02));
    if (talking) hang_ = 15;                                      // 300 ms de "sigo hablando" para no cortar el final de las palabras
    const bool send = !muted_.load() && (talking || hang_ > 0);
    if (!talking && hang_ > 0) --hang_;
    if (!send) {
        if (packetFrames_ > 0) {                                  // se callo: sale lo que quedaba del paquete
            out_.push_back(VoicePacketBuild(seq_++, packetStart_, packetFrames_, packetBytes_.data()));
            packetFrames_ = 0;
            packetBytes_.clear();
        }
        return;
    }
    if (packetFrames_ == 0) packetStart_ = encState_;
    packetBytes_.resize(static_cast<size_t>(packetFrames_ + 1) * (kVoiceFrame / 2));
    AdpcmEncode(s, n, encState_, packetBytes_.data() + static_cast<size_t>(packetFrames_) * (kVoiceFrame / 2));
    ++packetFrames_;
    lastTx_ = std::chrono::steady_clock::now().time_since_epoch().count();
    if (packetFrames_ >= kVoiceFramesPerPacket) {
        out_.push_back(VoicePacketBuild(seq_++, packetStart_, packetFrames_, packetBytes_.data()));
        packetFrames_ = 0;
        packetBytes_.clear();
    }
    while (out_.size() > 40) out_.pop_front();                    // (si nadie los manda no se acumulan)
}

bool VoiceChat::PopPacket(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lk(mu_);
    if (out_.empty()) return false;
    out = std::move(out_.front());
    out_.pop_front();
    return true;
}

void VoiceChat::Receive(int speaker, const uint8_t* data, size_t n) {
    uint16_t seq;
    AdpcmState st;
    int frames;
    const uint8_t* adpcm;
    if (!VoicePacketParse(data, n, seq, st, frames, adpcm)) return;
    std::vector<int16_t> pcm(static_cast<size_t>(frames) * kVoiceFrame);
    AdpcmDecode(adpcm, frames * kVoiceFrame, st, pcm.data());
    std::lock_guard<std::mutex> lk(mu_);
    Speaker& sp = speakers_[speaker];
    if (sp.lastSeq >= 0) {
        const int diff = static_cast<int16_t>(static_cast<uint16_t>(seq - static_cast<uint16_t>(sp.lastSeq)));
        if (diff <= 0 && diff > -200) return;                     // repetido o viejo
    }
    sp.lastSeq = seq;
    sp.last = std::chrono::steady_clock::now();
    sp.buf.insert(sp.buf.end(), pcm.begin(), pcm.end());
    while (static_cast<int>(sp.buf.size()) > kMaxBuffered) sp.buf.pop_front();
}

void VoiceChat::Mix(int16_t* out, int n) {
    std::vector<int32_t> acc(static_cast<size_t>(n), 0);
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& kv : speakers_) {
            Speaker& sp = kv.second;
            if (!sp.primed) {
                if (static_cast<int>(sp.buf.size()) < kPrimeSamples) continue;
                sp.primed = true;
            }
            const int take = std::min(n, static_cast<int>(sp.buf.size()));
            for (int i = 0; i < take; ++i) { acc[static_cast<size_t>(i)] += sp.buf.front(); sp.buf.pop_front(); }
            if (take < n) sp.primed = false;                      // se vacio: espera a juntar colchon de nuevo
        }
    }
    const float g = volume_.load();
    for (int i = 0; i < n; ++i) out[i] = static_cast<int16_t>(std::max(-32768, std::min(32767, static_cast<int>(acc[static_cast<size_t>(i)] * g))));
}

bool VoiceChat::Speaking(int speaker) const {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = speakers_.find(speaker);
    return it != speakers_.end() && std::chrono::steady_clock::now() - it->second.last < std::chrono::milliseconds(400);
}

void VoiceChat::ClearSpeakers() {
    std::lock_guard<std::mutex> lk(mu_);
    speakers_.clear();
    out_.clear();
    pend_.clear();
    packetFrames_ = 0;
    packetBytes_.clear();
}

} // namespace app
