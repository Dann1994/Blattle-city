#pragma once

#include <string>

#include <raylib.h>

#include "Tank.h"

namespace bc {

// 4 direcciones x 2 frames de animacion, recortados/rotados de Tanques.png,
// una fila de la hoja por nivel de arma (fila 1 = nivel 1, fila 2 = nivel 2, ...).
class TankSpriteSet {
public:
    void LoadPlayer1(const std::string& assetsDir) { Load(assetsDir, "p1"); }
    void LoadPlayer2(const std::string& assetsDir) { Load(assetsDir, "p2"); }
    void Unload();

    Texture2D Get(int weaponLevel, Direction dir, int frame) const;

private:
    void Load(const std::string& assetsDir, const std::string& playerTag);

    static constexpr int kLoadedLevels = 4;
    Texture2D textures_[kLoadedLevels][4][2]{};
};

} // namespace bc
