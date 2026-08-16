#include "SpecialExplosionSystem.h"

#include <algorithm>

namespace bc {

namespace {
constexpr double kFrameDuration = 0.08; // segundos por frame
constexpr int kFrameCount = 5;
} // namespace

void SpecialExplosionSystem::Spawn(float x, float y) {
    SpecialExplosion explosion;
    explosion.x = x;
    explosion.y = y;
    explosion.alive = true;
    explosions_.push_back(explosion);
}

void SpecialExplosionSystem::Update(double dt) {
    for (SpecialExplosion& explosion : explosions_) {
        if (!explosion.alive) {
            continue;
        }
        explosion.frameTimer += dt;
        if (explosion.frameTimer >= kFrameDuration) {
            explosion.frameTimer -= kFrameDuration;
            ++explosion.frameIndex;
            if (explosion.frameIndex >= kFrameCount) {
                explosion.alive = false;
            }
        }
    }
    explosions_.erase(std::remove_if(explosions_.begin(), explosions_.end(), [](const SpecialExplosion& e) { return !e.alive; }), explosions_.end());
}

} // namespace bc
