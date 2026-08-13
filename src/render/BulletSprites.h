#pragma once

#include <string>

#include <raylib.h>

#include "Tank.h" // Direction

namespace bc {

// Sprites recortados de Documentaciones/Tanques.png (fila de proyectiles grises,
// cada uno con una muesca que indica la direccion de viaje).
class BulletSpriteSet {
public:
    void Load(const std::string& assetsDir);
    void Unload();

    Texture2D Get(Direction dir) const { return textures_[static_cast<int>(dir)]; }

private:
    Texture2D textures_[4]{};
};

} // namespace bc
