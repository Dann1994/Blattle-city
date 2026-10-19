// Microfono de Windows (waveIn, 8 kHz, mono, 16 bits). Un hilo junta los buffers que van terminando y los entrega de a 20 ms.
#include "voice.h"

#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>

namespace app {
namespace {

class WinMic : public MicCapture {
public:
    ~WinMic() override { Stop(); }
    bool Start() override {
        if (run_) return true;
        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 1;
        fmt.nSamplesPerSec = kVoiceRate;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 2;
        fmt.nAvgBytesPerSec = kVoiceRate * 2;
        if (waveInGetNumDevs() == 0) return false;
        if (waveInOpen(&h_, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) { h_ = nullptr; return false; }
        for (int i = 0; i < kBufs; ++i) {
            hdr_[i] = WAVEHDR{};
            hdr_[i].lpData = reinterpret_cast<LPSTR>(pcm_[i]);
            hdr_[i].dwBufferLength = sizeof pcm_[i];
            waveInPrepareHeader(h_, &hdr_[i], sizeof(WAVEHDR));
            waveInAddBuffer(h_, &hdr_[i], sizeof(WAVEHDR));
        }
        if (waveInStart(h_) != MMSYSERR_NOERROR) { Release(); return false; }
        run_ = true;
        th_ = std::thread([this] {
            while (run_) {
                bool any = false;
                for (int i = 0; i < kBufs; ++i) {
                    if (hdr_[i].dwFlags & WHDR_DONE) {
                        any = true;
                        if (onFrame && hdr_[i].dwBytesRecorded >= sizeof pcm_[i]) onFrame(pcm_[i], kVoiceFrame);
                        hdr_[i].dwFlags &= ~static_cast<DWORD>(WHDR_DONE);
                        waveInAddBuffer(h_, &hdr_[i], sizeof(WAVEHDR));
                    }
                }
                if (!any) Sleep(5);
            }
        });
        return true;
    }
    void Stop() override {
        if (!run_) return;
        run_ = false;
        if (th_.joinable()) th_.join();
        Release();
    }
    bool Running() const override { return run_; }

private:
    static constexpr int kBufs = 6;
    void Release() {
        if (!h_) return;
        waveInReset(h_);
        for (int i = 0; i < kBufs; ++i) waveInUnprepareHeader(h_, &hdr_[i], sizeof(WAVEHDR));
        waveInClose(h_);
        h_ = nullptr;
    }
    HWAVEIN h_ = nullptr;
    WAVEHDR hdr_[kBufs];
    int16_t pcm_[kBufs][kVoiceFrame];
    std::atomic<bool> run_{false};
    std::thread th_;
};

} // namespace

std::unique_ptr<MicCapture> CreateMicCapture() { return std::make_unique<WinMic>(); }

} // namespace app
#elif !defined(__ANDROID__)
namespace app {
std::unique_ptr<MicCapture> CreateMicCapture() { return nullptr; }
}
#endif
