#include "BulletSprites.h"

namespace bc {

namespace {
constexpr const char* kDirNames[4] = {"up", "down", "left", "right"};
}

void BulletSpriteSet::Load(const std::string& assetsDir) {
    for (int d = 0; d < 4; ++d) {
        const std::string path = assetsDir + "sprites/bullet_" + kDirNames[d] + ".png";
        textures_[d] = LoadTexture(path.c_str());
        SetTextureFilter(textures_[d], TEXTURE_FILTER_POINT);
    }
}

void BulletSpriteSet::Unload() {
    for (int d = 0; d < 4; ++d) {
        UnloadTexture(textures_[d]);
    }
}

} // namespace bc
