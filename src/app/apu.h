#pragma once

// Emulacion del sonido de la NES (APU 2A03): dos canales de pulso, triangulo
// y ruido (el DMC no se usa en Battle City). Recibe las mismas escrituras a
// $4000-$4017 que haria el juego y genera audio PCM mono de 16 bits.

#include <cstdint>

namespace app {

class Apu {
public:
    static constexpr int kSampleRate = 44100;

    Apu();
    void Reset();

    // Escritura a un registro ($4000-$4017).
    void Write(uint16_t addr, uint8_t value);

    // Genera "count" muestras (mono, 16 bits) avanzando el reloj de la CPU.
    void Generate(int16_t* out, int count);

private:
    struct Envelope {
        bool start = false, loop = false, constant = false;
        uint8_t period = 0, divider = 0, decay = 0;
        void Clock();
        uint8_t Volume() const { return constant ? period : decay; }
    };

    struct Pulse {
        bool enabled = false, onesComplement = false;
        uint8_t duty = 0, step = 0;
        uint16_t timer = 0, timerValue = 0;
        uint8_t length = 0;
        bool lengthHalt = false;
        Envelope env;
        bool sweepEnabled = false, sweepNegate = false, sweepReload = false;
        uint8_t sweepPeriod = 0, sweepShift = 0, sweepDivider = 0;
        int TargetPeriod() const;
        bool Muted() const;
        void ClockSweep();
        int Output() const;
    };

    struct Triangle {
        bool enabled = false, control = false, reload = false;
        uint8_t linearReload = 0, linear = 0, step = 0;
        uint16_t timer = 0, timerValue = 0;
        uint8_t length = 0;
        int Output() const;
    };

    struct Noise {
        bool enabled = false, mode = false, lengthHalt = false;
        uint16_t shift = 1;
        uint16_t timer = 0, timerValue = 0;
        uint8_t length = 0;
        Envelope env;
        int Output() const;
    };

    void ClockCpuCycle();
    void ClockQuarterFrame();
    void ClockHalfFrame();
    float Mix() const;

    Pulse pulse_[2];
    Triangle tri_;
    Noise noise_;
    bool fiveStep_ = false;
    int frameCycle_ = 0;
    uint32_t cpuCycle_ = 0;
    double sampleDebt_ = 0.0;
    float hpState_ = 0.0f, lastIn_ = 0.0f;
};

} // namespace app
