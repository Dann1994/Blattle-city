#pragma once

#include <raylib.h>

#include "BulletSprites.h"
#include "BulletSystem.h"
#include "PowerUpSystem.h"
#include "SpawnFlash.h"
#include "SpawnFlashSprites.h"
#include "Tank.h"
#include "TankSprites.h"
#include "TileMap.h"

namespace bc {

class Game {
public:
    void Run();

private:
    void Init();
    void ProcessInput();
    void Update(double fixedDt);
    void Render(double interpolationAlpha);
    void Shutdown();

    static constexpr int kPlayer1Id = 0;

    TileMap map_;
    Tank player1_;
    PlayerInput input1_;
    TankSpriteSet player1Sprites_;
    BulletSystem bullets_;
    BulletSpriteSet bulletSprites_;
    PowerUpSystem powerUps_;
    Texture2D starTexture_{};
    SpawnFlash player1Spawn_;
    SpawnFlashSprites spawnFlashSprites_;
    int windowWidth_ = 0;
    int windowHeight_ = 0;
};

} // namespace bc
