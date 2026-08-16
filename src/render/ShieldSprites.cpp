#include "ShieldSprites.h"

namespace bc {

void ShieldSprites::Load(const std::string& assetsDir) {
    for (int i = 0; i < 2; ++i) {
        const std::string path = assetsDir + "sprites/shield_" + std::to_string(i) + ".png";
        textures_[i] = LoadTexture(path.c_str());
        SetTextureFilter(textures_[i], TEXTURE_FILTER_POINT);
    }
}

void ShieldSprites::Unload() {
    for (int i = 0; i < 2; ++i) {
        UnloadTexture(textures_[i]);
    }
}

} // namespace bc
