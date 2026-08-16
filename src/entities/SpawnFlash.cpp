#include "SpawnFlash.h"

namespace bc {

namespace {
constexpr double kFrameDuration = 0.08; // segundos por frame
constexpr int kFrameCount = 4;
constexpr int kLoops = 2; // vueltas completas del rebote 1-4-1
} // namespace

void SpawnFlash::Start(float cellX, float cellY) {
    x_ = cellX;
    y_ = cellY;
    frameTimer_ = 0.0;
    frameIndex_ = 0;
    direction_ = 1;
    loopsDone_ = 0;
    active_ = true;
}

void SpawnFlash::Update(double dt) {
    if (!active_) {
        return;
    }

    frameTimer_ += dt;
    while (frameTimer_ >= kFrameDuration) {
        frameTimer_ -= kFrameDuration;

        // Rebote entre el primer y el ultimo frame: 1 2 3 4 3 2 1 2 3 4 3 2 1...
        frameIndex_ += direction_;
        if (frameIndex_ >= kFrameCount - 1) {
            frameIndex_ = kFrameCount - 1;
            direction_ = -1;
        } else if (frameIndex_ <= 0) {
            frameIndex_ = 0;
            direction_ = 1;
            ++loopsDone_;
            if (loopsDone_ >= kLoops) {
                active_ = false;
                return;
            }
        }
    }
}

} // namespace bc
