#pragma once

#include <string>

#include <raylib.h>

namespace bc {

// 3 frames crecientes recortados de Documentaciones/Impacto.png.
class BulletImpactSprites {
public:
    void Load(const std::string& assetsDir);
    void Unload();

    Texture2D Get(int frameIndex) const { return textures_[frameIndex]; }

private:
    Texture2D textures_[3]{};
};

} // namespace bc
