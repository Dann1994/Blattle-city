#include "SpawnFlash.h"

namespace bc {

namespace {
constexpr double kFrameDuration = 0.125; // segundos por frame (4 frames x 2 vueltas ~ 1s)
constexpr int kFrameCount = 4;
constexpr int kLoops = 2;
} // namespace

void SpawnFlash::Start(float cellX, float cellY) {
    x_ = cellX;
    y_ = cellY;
    frameTimer_ = 0.0;
    frameIndex_ = 0;
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
        ++frameIndex_;
        if (frameIndex_ >= kFrameCount) {
            frameIndex_ = 0;
            ++loopsDone_;
            if (loopsDone_ >= kLoops) {
                active_ = false;
                return;
            }
        }
    }
}

} // namespace bc
