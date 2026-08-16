#include "BulletImpactSystem.h"

#include <algorithm>

namespace bc {

namespace {
constexpr double kFrameDuration = 0.06; // segundos por frame
constexpr int kNormalFrameCount = 2; // el original solo usa los dos primeros frames
constexpr int kBigFrameCount = 3;    // nivel de arma 4: explosion mas grande, un frame extra
} // namespace

void BulletImpactSystem::Spawn(float x, float y, bool bigExplosion) {
    BulletImpact impact;
    impact.x = x;
    impact.y = y;
    impact.frameCount = bigExplosion ? kBigFrameCount : kNormalFrameCount;
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
            if (impact.frameIndex >= impact.frameCount) {
                impact.alive = false;
            }
        }
    }
    impacts_.erase(std::remove_if(impacts_.begin(), impacts_.end(), [](const BulletImpact& i) { return !i.alive; }), impacts_.end());
}

} // namespace bc
