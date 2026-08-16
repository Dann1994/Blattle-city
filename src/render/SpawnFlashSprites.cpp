#include "SpawnFlashSprites.h"

namespace bc {

void SpawnFlashSprites::Load(const std::string& assetsDir) {
    for (int i = 0; i < 4; ++i) {
        const std::string path = assetsDir + "sprites/spawn_flash_" + std::to_string(i + 1) + ".png";
        textures_[i] = LoadTexture(path.c_str());
        SetTextureFilter(textures_[i], TEXTURE_FILTER_POINT);
    }
}

void SpawnFlashSprites::Unload() {
    for (int i = 0; i < 4; ++i) {
        UnloadTexture(textures_[i]);
    }
}

} // namespace bc
