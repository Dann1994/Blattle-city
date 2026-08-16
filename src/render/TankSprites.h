#pragma once

#include <string>

#include <raylib.h>

#include "Tank.h"

namespace bc {

// 4 direcciones x 2 frames de animacion, recortados/rotados de Tanques.png,
// una fila de la hoja por nivel de arma (fila 1 = nivel 1, fila 2 = nivel 2, ...).
// Todavia solo hay arte para los niveles 1 y 2; los niveles 3-4 reusan el
// aspecto del nivel 2 hasta que se agreguen sus frames.
class TankSpriteSet {
public:
    void LoadPlayer1(const std::string& assetsDir);
    void Unload();

    Texture2D Get(int weaponLevel, Direction dir, int frame) const;

private:
    static constexpr int kLoadedLevels = 2;
    Texture2D textures_[kLoadedLevels][4][2]{};
};

} // namespace bc
