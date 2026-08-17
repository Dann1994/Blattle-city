#pragma once

#include <vector>

#include "SpecialExplosion.h"

namespace bc {

// Igual que BulletImpactSystem pero para la explosion grande del disparo
// especial: distinta duracion/cantidad de frames y sprites propios.
class SpecialExplosionSystem {
public:
    void Spawn(float x, float y, bool nativeScale = false);
    void Update(double dt);

    const std::vector<SpecialExplosion>& Explosions() const { return explosions_; }

private:
    std::vector<SpecialExplosion> explosions_;
};

} // namespace bc
