#pragma once

#include <string>

#include <raylib.h>

namespace bc {

// 5 frames crecientes recortados de Documentaciones/Exploción.png.
class SpecialExplosionSprites {
public:
    void Load(const std::string& assetsDir);
    void Unload();

    Texture2D Get(int frameIndex) const { return textures_[frameIndex]; }

private:
    Texture2D textures_[5]{};
};

} // namespace bc
