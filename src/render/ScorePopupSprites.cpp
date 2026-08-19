#include "ScorePopupSprites.h"

namespace bc {

namespace {
Texture2D LoadScoreTexture(const std::string& assetsDir, int points) {
    const std::string path = assetsDir + "sprites/score_" + std::to_string(points) + ".png";
    Texture2D texture = LoadTexture(path.c_str());
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    return texture;
}
} // namespace

void ScorePopupSprites::Load(const std::string& assetsDir) {
    texture100_ = LoadScoreTexture(assetsDir, 100);
    texture200_ = LoadScoreTexture(assetsDir, 200);
    texture300_ = LoadScoreTexture(assetsDir, 300);
    texture400_ = LoadScoreTexture(assetsDir, 400);
    texture500_ = LoadScoreTexture(assetsDir, 500);
}

void ScorePopupSprites::Unload() {
    UnloadTexture(texture100_);
    UnloadTexture(texture200_);
    UnloadTexture(texture300_);
    UnloadTexture(texture400_);
    UnloadTexture(texture500_);
}

Texture2D ScorePopupSprites::Get(int points) const {
    switch (points) {
        case 100: return texture100_;
        case 200: return texture200_;
        case 300: return texture300_;
        case 400: return texture400_;
        default: return texture500_;
    }
}

} // namespace bc
