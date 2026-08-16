#include "SpecialExplosionSprites.h"

namespace bc {

void SpecialExplosionSprites::Load(const std::string& assetsDir) {
    for (int i = 0; i < 5; ++i) {
        const std::string path = assetsDir + "sprites/bullet_special_impact_" + std::to_string(i + 1) + ".png";
        textures_[i] = LoadTexture(path.c_str());
        SetTextureFilter(textures_[i], TEXTURE_FILTER_POINT);
    }
}

void SpecialExplosionSprites::Unload() {
    for (int i = 0; i < 5; ++i) {
        UnloadTexture(textures_[i]);
    }
}

} // namespace bc
