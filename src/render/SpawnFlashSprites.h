#pragma once

#include <string>

#include <raylib.h>

namespace bc {

// 4 frames crecientes recortados de Documentaciones/Tanques.png.
class SpawnFlashSprites {
public:
    void Load(const std::string& assetsDir);
    void Unload();

    Texture2D Get(int frameIndex) const { return textures_[frameIndex]; }

private:
    Texture2D textures_[4]{};
};

} // namespace bc
