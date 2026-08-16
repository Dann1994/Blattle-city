#include "TankSprites.h"

#include <algorithm>

namespace bc {

namespace {
constexpr const char* kDirNames[4] = {"up", "down", "left", "right"};
}

void TankSpriteSet::LoadPlayer1(const std::string& assetsDir) {
    for (int level = 0; level < kLoadedLevels; ++level) {
        // Nivel 1 no lleva sufijo (sprites originales de la Fase 1); nivel 2+ usa "_lvlN_".
        const std::string levelTag = (level == 0) ? "" : ("lvl" + std::to_string(level + 1) + "_");
        for (int d = 0; d < 4; ++d) {
            for (int f = 0; f < 2; ++f) {
                const std::string path = assetsDir + "sprites/tank_p1_" + levelTag + kDirNames[d] + "_" + std::to_string(f) + ".png";
                textures_[level][d][f] = LoadTexture(path.c_str());
                SetTextureFilter(textures_[level][d][f], TEXTURE_FILTER_POINT);
            }
        }
    }
}

void TankSpriteSet::Unload() {
    for (int level = 0; level < kLoadedLevels; ++level) {
        for (int d = 0; d < 4; ++d) {
            for (int f = 0; f < 2; ++f) {
                UnloadTexture(textures_[level][d][f]);
            }
        }
    }
}

Texture2D TankSpriteSet::Get(int weaponLevel, Direction dir, int frame) const {
    const int levelIndex = std::clamp(weaponLevel, 1, kLoadedLevels) - 1;
    return textures_[levelIndex][static_cast<int>(dir)][frame];
}

} // namespace bc
