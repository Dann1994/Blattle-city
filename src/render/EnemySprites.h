#pragma once

#include <string>

#include <raylib.h>

#include "Tank.h"

namespace bc {

// Tanque enemigo "Basico" (fila 5 de Tanques.png, paleta gris/teal de P3):
// 4 direcciones x 2 frames de animacion de oruga, sprite propio (no el del
// jugador 3, que usa otra fila/pose distinta).
class EnemySprites {
public:
    void Load(const std::string& assetsDir);
    void Unload();

    Texture2D Get(Direction dir, int frame) const { return textures_[static_cast<int>(dir)][frame]; }

private:
    Texture2D textures_[4][2]{};
};

} // namespace bc
