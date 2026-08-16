#include "BulletImpactSystem.h"

#include <algorithm>

namespace bc {

namespace {
constexpr double kFrameDuration = 0.06; // segundos por frame
constexpr int kFrameCount = 3;
} // namespace

void BulletImpactSystem::Spawn(float x, float y) {
    BulletImpact impact;
    impact.x = x;
    impact.y = y;
    impact.alive = true;
    impacts_.push_back(impact);
}

void BulletImpactSystem::Update(double dt) {
    for (BulletImpact& impact : impacts_) {
        if (!impact.alive) {
            continue;
        }
        impact.frameTimer += dt;
        if (impact.frameTimer >= kFrameDuration) {
            impact.frameTimer -= kFrameDuration;
            ++impact.frameIndex;
            if (impact.frameIndex >= kFrameCount) {
                impact.alive = false;
            }
        }
    }
    impacts_.erase(std::remove_if(impacts_.begin(), impacts_.end(), [](const BulletImpact& i) { return !i.alive; }), impacts_.end());
}

} // namespace bc
