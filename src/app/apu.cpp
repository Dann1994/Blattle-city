#include "apu.h"

#include <algorithm>
#include <cmath>

namespace app {

namespace {

constexpr double kCpuHz = 1789773.0;

constexpr uint8_t kLengthTable[32] = {10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
                                      12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};
constexpr uint8_t kDutyTable[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0},
    {1, 0, 0, 1, 1, 1, 1, 1},
};
constexpr uint16_t kNoisePeriods[16] = {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};
constexpr uint8_t kTriangleSeq[32] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
                                      0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

} // namespace

void Apu::Envelope::Clock() {
    if (start) {
        start = false;
        decay = 15;
        divider = period;
        return;
    }
    if (divider == 0) {
        divider = period;
        if (decay > 0) {
            --decay;
        } else if (loop) {
            decay = 15;
        }
    } else {
        --divider;
    }
}

int Apu::Pulse::TargetPeriod() const {
    const int change = timer >> sweepShift;
    if (sweepNegate) {
        return timer - change - (onesComplement ? 1 : 0);
    }
    return timer + change;
}

bool Apu::Pulse::Muted() const {
    return timer < 8 || TargetPeriod() > 0x7FF;
}

void Apu::Pulse::ClockSweep() {
    if (sweepDivider == 0 && sweepEnabled && sweepShift > 0 && !Muted()) {
        timer = static_cast<uint16_t>(TargetPeriod());
    }
    if (sweepDivider == 0 || sweepReload) {
        sweepDivider = sweepPeriod;
        sweepReload = false;
    } else {
        --sweepDivider;
    }
}

int Apu::Pulse::Output() const {
    if (!enabled || length == 0 || Muted() || !kDutyTable[duty][step]) {
        return 0;
    }
    return env.Volume();
}

int Apu::Triangle::Output() const {
    if (!enabled || length == 0 || linear == 0) {
        return 0;
    }
    return kTriangleSeq[step];
}

int Apu::Noise::Output() const {
    if (!enabled || length == 0 || (shift & 1)) {
        return 0;
    }
    return env.Volume();
}

Apu::Apu() { Reset(); }

void Apu::Reset() {
    pulse_[0] = Pulse{};
    pulse_[1] = Pulse{};
    pulse_[0].onesComplement = true;                 // el pulso 1 niega en complemento a uno
    tri_ = Triangle{};
    noise_ = Noise{};
    noise_.shift = 1;
    noise_.timer = kNoisePeriods[0];
    fiveStep_ = false;
    frameCycle_ = 0;
    cpuCycle_ = 0;
    sampleDebt_ = 0.0;
    hpState_ = lastIn_ = 0.0f;
}

void Apu::Write(uint16_t addr, uint8_t v) {
    if (addr >= 0x4000 && addr <= 0x4007) {
        Pulse& p = pulse_[(addr >> 2) & 1];
        switch (addr & 3) {
            case 0:
                p.duty = v >> 6;
                p.lengthHalt = p.env.loop = (v & 0x20) != 0;
                p.env.constant = (v & 0x10) != 0;
                p.env.period = v & 0x0F;
                break;
            case 1:
                p.sweepEnabled = (v & 0x80) != 0;
                p.sweepPeriod = (v >> 4) & 7;
                p.sweepNegate = (v & 0x08) != 0;
                p.sweepShift = v & 7;
                p.sweepReload = true;
                break;
            case 2:
                p.timer = static_cast<uint16_t>((p.timer & 0x700) | v);
                break;
            default:
                p.timer = static_cast<uint16_t>((p.timer & 0x0FF) | ((v & 7) << 8));
                if (p.enabled) p.length = kLengthTable[v >> 3];
                p.step = 0;
                p.env.start = true;
                break;
        }
        return;
    }
    switch (addr) {
        case 0x4008:
            tri_.control = (v & 0x80) != 0;
            tri_.linearReload = v & 0x7F;
            break;
        case 0x400A:
            tri_.timer = static_cast<uint16_t>((tri_.timer & 0x700) | v);
            break;
        case 0x400B:
            tri_.timer = static_cast<uint16_t>((tri_.timer & 0x0FF) | ((v & 7) << 8));
            if (tri_.enabled) tri_.length = kLengthTable[v >> 3];
            tri_.reload = true;
            break;
        case 0x400C:
            noise_.lengthHalt = noise_.env.loop = (v & 0x20) != 0;
            noise_.env.constant = (v & 0x10) != 0;
            noise_.env.period = v & 0x0F;
            break;
        case 0x400E:
            noise_.mode = (v & 0x80) != 0;
            noise_.timer = kNoisePeriods[v & 0x0F];
            break;
        case 0x400F:
            if (noise_.enabled) noise_.length = kLengthTable[v >> 3];
            noise_.env.start = true;
            break;
        case 0x4015:
            pulse_[0].enabled = (v & 1) != 0;
            pulse_[1].enabled = (v & 2) != 0;
            tri_.enabled = (v & 4) != 0;
            noise_.enabled = (v & 8) != 0;
            if (!pulse_[0].enabled) pulse_[0].length = 0;
            if (!pulse_[1].enabled) pulse_[1].length = 0;
            if (!tri_.enabled) tri_.length = 0;
            if (!noise_.enabled) noise_.length = 0;
            break;
        case 0x4017:
            fiveStep_ = (v & 0x80) != 0;
            frameCycle_ = 0;
            if (fiveStep_) {
                ClockQuarterFrame();
                ClockHalfFrame();
            }
            break;
        default:
            break;
    }
}

void Apu::ClockQuarterFrame() {
    pulse_[0].env.Clock();
    pulse_[1].env.Clock();
    noise_.env.Clock();
    if (tri_.reload) {
        tri_.linear = tri_.linearReload;
    } else if (tri_.linear > 0) {
        --tri_.linear;
    }
    if (!tri_.control) {
        tri_.reload = false;
    }
}

void Apu::ClockHalfFrame() {
    for (Pulse& p : pulse_) {
        if (!p.lengthHalt && p.length > 0) --p.length;
        p.ClockSweep();
    }
    if (!tri_.control && tri_.length > 0) --tri_.length;
    if (!noise_.lengthHalt && noise_.length > 0) --noise_.length;
}

void Apu::ClockCpuCycle() {
    // secuenciador de cuadro (ciclos de CPU: 7457, 14913, 22371, 29829, 37281)
    ++frameCycle_;
    if (fiveStep_) {
        switch (frameCycle_) {
            case 7457: ClockQuarterFrame(); break;
            case 14913: ClockQuarterFrame(); ClockHalfFrame(); break;
            case 22371: ClockQuarterFrame(); break;
            case 37281: ClockQuarterFrame(); ClockHalfFrame(); break;
            case 37282: frameCycle_ = 0; break;
            default: break;
        }
    } else {
        switch (frameCycle_) {
            case 7457: ClockQuarterFrame(); break;
            case 14913: ClockQuarterFrame(); ClockHalfFrame(); break;
            case 22371: ClockQuarterFrame(); break;
            case 29829: ClockQuarterFrame(); ClockHalfFrame(); frameCycle_ = 0; break;
            default: break;
        }
    }

    // triangulo: un paso cada (timer+1) ciclos de CPU
    if (tri_.timerValue == 0) {
        tri_.timerValue = tri_.timer;
        if (tri_.length > 0 && tri_.linear > 0) {
            tri_.step = (tri_.step + 1) & 31;
        }
    } else {
        --tri_.timerValue;
    }

    // pulsos y ruido: se mueven cada 2 ciclos de CPU (ciclos de APU)
    if ((cpuCycle_ & 1) == 0) {
        for (Pulse& p : pulse_) {
            if (p.timerValue == 0) {
                p.timerValue = p.timer;
                p.step = (p.step + 1) & 7;
            } else {
                --p.timerValue;
            }
        }
    }

    // ruido: el periodo de la tabla esta en ciclos de CPU
    if (noise_.timerValue == 0) {
        noise_.timerValue = static_cast<uint16_t>(noise_.timer > 0 ? noise_.timer - 1 : 0);
        const int bit = noise_.mode ? 6 : 1;
        const uint16_t feedback = static_cast<uint16_t>((noise_.shift & 1) ^ ((noise_.shift >> bit) & 1));
        noise_.shift = static_cast<uint16_t>((noise_.shift >> 1) | (feedback << 14));
    } else {
        --noise_.timerValue;
    }
    ++cpuCycle_;
}

float Apu::Mix() const {
    const int p = pulse_[0].Output() + pulse_[1].Output();
    const int t = tri_.Output();
    const int n = noise_.Output();
    float out = 0.0f;
    if (p > 0) out += 95.88f / (8128.0f / static_cast<float>(p) + 100.0f);
    const float tnd = static_cast<float>(t) / 8227.0f + static_cast<float>(n) / 12241.0f;
    if (tnd > 0.0f) out += 159.79f / (1.0f / tnd + 100.0f);
    return out;
}

void Apu::Generate(int16_t* out, int count) {
    const double cyclesPerSample = kCpuHz / kSampleRate;
    for (int i = 0; i < count; ++i) {
        // promedio de la mezcla durante los ciclos de CPU que caen en esta muestra
        sampleDebt_ += cyclesPerSample;
        int cycles = 0;
        float acc = 0.0f;
        while (sampleDebt_ >= 1.0) {
            ClockCpuCycle();
            acc += Mix();
            sampleDebt_ -= 1.0;
            ++cycles;
        }
        const float in = cycles > 0 ? acc / static_cast<float>(cycles) : lastIn_;
        // filtro pasa-altos (quita la componente continua, como el hardware)
        hpState_ = in - lastIn_ + 0.9975f * hpState_;
        lastIn_ = in;
        const float s = std::clamp(hpState_ * 1.7f, -1.0f, 1.0f);
        out[i] = static_cast<int16_t>(s * 32000.0f);
    }
}

} // namespace app
