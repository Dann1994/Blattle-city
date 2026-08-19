#pragma once

#include <string>

#include <raylib.h>

namespace bc {

// Los 5 numeros (100/200/300/400/500) recortados de Tanques.png, para el
// popup que aparece al matar un enemigo o agarrar un item (ver
// ScorePopupSystem).
class ScorePopupSprites {
public:
    void Load(const std::string& assetsDir);
    void Unload();

    Texture2D Get(int points) const;

private:
    Texture2D texture100_{};
    Texture2D texture200_{};
    Texture2D texture300_{};
    Texture2D texture400_{};
    Texture2D texture500_{};
};

} // namespace bc
