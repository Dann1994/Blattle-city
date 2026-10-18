#include "lobby.h"

namespace app {

Lobby::Lobby() {
    joined_[0] = true;       // quien entro al modo local ya esta adentro con el amarillo
    color_[0] = 0;
}

bool Lobby::ColorTaken(int color, int exceptSlot) const {
    for (int s = 0; s < 4; ++s) {
        if (s != exceptSlot && joined_[s] && color_[s] == color) return true;
    }
    return false;
}

bool Lobby::Join(int slot) {
    if (slot < 0 || slot > 3 || joined_[slot]) return false;
    // color por defecto de la ranura si esta libre; si no, el primero libre
    int c = slot < kColorCount ? slot : 0;
    for (int i = 0; i < kColorCount && ColorTaken(c, slot); ++i) c = (c + 1) % kColorCount;
    color_[slot] = c;
    joined_[slot] = true;
    return true;
}

bool Lobby::Leave(int slot) {
    if (slot < 0 || slot > 3 || !joined_[slot] || Count() <= 1) return false;
    joined_[slot] = false;
    return true;
}

void Lobby::CycleColor(int slot, int direction) {
    if (slot < 0 || slot > 3 || !joined_[slot]) return;
    int c = color_[slot];
    for (int i = 0; i < kColorCount; ++i) {
        c = (c + direction + kColorCount) % kColorCount;
        if (!ColorTaken(c, slot)) {
            color_[slot] = c;
            return;
        }
    }
}

int Lobby::Count() const {
    int n = 0;
    for (bool j : joined_) n += j ? 1 : 0;
    return n;
}

int Lobby::SlotOfPlayer(int index) const {
    int n = 0;
    for (int s = 0; s < 4; ++s) {
        if (!joined_[s]) continue;
        if (n == index) return s;
        ++n;
    }
    return 0;
}

void AssignPads(const bool available[4], int padOfSlot[4]) {
    for (int s = 0; s < 4; ++s) {
        if (padOfSlot[s] >= 0 && !available[padOfSlot[s]]) padOfSlot[s] = -1;     // se desconecto
    }
    for (int pad = 0; pad < 4; ++pad) {
        if (!available[pad]) continue;
        bool taken = false;
        for (int s = 0; s < 4; ++s) taken = taken || padOfSlot[s] == pad;
        if (taken) continue;
        for (int s = 0; s < 4; ++s) {
            if (padOfSlot[s] < 0) {
                padOfSlot[s] = pad;
                break;
            }
        }
    }
}

} // namespace app
