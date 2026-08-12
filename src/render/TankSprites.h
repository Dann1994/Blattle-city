#pragma once

#include <string>

#include <raylib.h>

#include "Tank.h"

namespace bc {

// 4 direcciones x 2 frames de animacion, recortados/rotados de Tanques.png
// (ver Documentaciones/Tanques.png, los 2 primeros frames arriba-izquierda).
class TankSpriteSet {
public:
    void LoadPlayer1(const std::string& assetsDir);
    void Unload();

    Texture2D Get(Direction dir, int frame) const { return textures_[static_cast<int>(dir)][frame]; }

private:
    Texture2D textures_[4][2]{};
};

} // namespace bc
