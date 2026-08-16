#pragma once

#include <string>

#include <raylib.h>

namespace bc {

// 2 frames que alternan (Documentaciones/Shield.png) para el efecto de
// escudo girando alrededor del tanque.
class ShieldSprites {
public:
    void Load(const std::string& assetsDir);
    void Unload();

    Texture2D Get(int frame) const { return textures_[frame]; } // 0 o 1

private:
    Texture2D textures_[2]{};
};

} // namespace bc
