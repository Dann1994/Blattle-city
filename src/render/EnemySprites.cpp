#include "EnemySprites.h"

namespace bc {

namespace {
constexpr const char* kDirNames[4] = {"up", "down", "left", "right"};
}

void EnemySprites::Load(const std::string& assetsDir, const std::string& filePrefix) {
    for (int d = 0; d < 4; ++d) {
        for (int f = 0; f < 2; ++f) {
            const std::string path = assetsDir + "sprites/tank_enemy_" + filePrefix + "_" + kDirNames[d] + "_" + std::to_string(f) + ".png";
            textures_[d][f] = LoadTexture(path.c_str());
            SetTextureFilter(textures_[d][f], TEXTURE_FILTER_POINT);
        }
    }
}

void EnemySprites::Unload() {
    for (int d = 0; d < 4; ++d) {
        for (int f = 0; f < 2; ++f) {
            UnloadTexture(textures_[d][f]);
        }
    }
}

} // namespace bc
