#pragma once

#include <vector>

#include "ScorePopup.h"

namespace bc {

// Varios popups pueden estar animandose a la vez (varias eliminaciones o
// pickups cerca en el mismo instante), igual que BulletImpactSystem.
class ScorePopupSystem {
public:
    void Spawn(float x, float y, int points);
    void Update(double dt);

    const std::vector<ScorePopup>& Popups() const { return popups_; }

private:
    std::vector<ScorePopup> popups_;
};

} // namespace bc
