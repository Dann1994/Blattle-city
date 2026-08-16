#include "BulletImpactSprites.h"

namespace bc {

void BulletImpactSprites::Load(const std::string& assetsDir) {
    for (int i = 0; i < 3; ++i) {
        const std::string path = assetsDir + "sprites/bullet_impact_" + std::to_string(i + 1) + ".png";
        textures_[i] = LoadTexture(path.c_str());
        SetTextureFilter(textures_[i], TEXTURE_FILTER_POINT);
    }
}

void BulletImpactSprites::Unload() {
    for (int i = 0; i < 3; ++i) {
        UnloadTexture(textures_[i]);
    }
}

} // namespace bc
