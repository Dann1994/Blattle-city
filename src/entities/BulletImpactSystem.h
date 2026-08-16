#pragma once

#include <vector>

#include "BulletImpact.h"

namespace bc {

// Varios impactos pueden estar animandose a la vez (varias balas chocando
// cerca en el mismo instante), a diferencia del destello de aparicion que es
// uno solo por tanque.
class BulletImpactSystem {
public:
    void Spawn(float x, float y);
    void Update(double dt);

    const std::vector<BulletImpact>& Impacts() const { return impacts_; }

private:
    std::vector<BulletImpact> impacts_;
};

} // namespace bc
