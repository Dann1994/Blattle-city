#pragma once

#include <string>

#include <raylib.h>

#include "Tank.h"

namespace bc {

// Tanque enemigo (fila 5 = "Basico", fila 6 = "Rapido" de Tanques.png,
// paleta gris/teal de P3): 4 direcciones x 2 frames de animacion de oruga,
// sprite propio (no el del jugador 3, que usa otra fila/pose distinta).
// filePrefix distingue el set de archivos (ver tank_enemy_<prefix>_*.png).
class EnemySprites {
public:
    void Load(const std::string& assetsDir, const std::string& filePrefix);
    void Unload();

    Texture2D Get(Direction dir, int frame) const { return textures_[static_cast<int>(dir)][frame]; }

private:
    Texture2D textures_[4][2]{};
};

} // namespace bc
