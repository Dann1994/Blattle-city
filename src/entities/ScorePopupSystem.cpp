#include "ScorePopupSystem.h"

#include <algorithm>

namespace bc {

namespace {
constexpr double kPopupDuration = 0.7; // segundos que se ve el numero antes de desaparecer
}

void ScorePopupSystem::Spawn(float x, float y, int points) {
    ScorePopup popup;
    popup.x = x;
    popup.y = y;
    popup.points = points;
    popup.timer = kPopupDuration;
    popups_.push_back(popup);
}

void ScorePopupSystem::Update(double dt) {
    for (ScorePopup& popup : popups_) {
        popup.timer -= dt;
    }
    popups_.erase(std::remove_if(popups_.begin(), popups_.end(), [](const ScorePopup& p) { return p.timer <= 0.0; }), popups_.end());
}

} // namespace bc
