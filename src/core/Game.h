#pragma once

#include <vector>

#include <raylib.h>

#include "BulletImpactSprites.h"
#include "BulletImpactSystem.h"
#include "BulletSprites.h"
#include "BulletSystem.h"
#include "PowerUpSystem.h"
#include "ShieldSprites.h"
#include "SpawnFlash.h"
#include "SpawnFlashSprites.h"
#include "SpecialExplosionSprites.h"
#include "SpecialExplosionSystem.h"
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

    // Recarga el mapa y reinicia tanque/balas/power-ups a estado inicial
    // (ver Game.cpp). Llamada desde Init() y desde el boton de prueba ESC.
    void ResetState();

    // Reutilizable: vuelve a ubicar al jugador 1 en su punto de spawn inicial
    // y dispara el destello ahi. Pensado para reusarse al iniciar un nivel o
    // cuando el jugador pierde una vida (Fase 3/4), ademas del boton de
    // prueba (R).
    void RespawnPlayer1();

    static constexpr int kPlayer1Id = 0;

    TileMap map_;
    Tank player1_;
    PlayerInput input1_;
    TankSpriteSet player1Sprites_;
    BulletSystem bullets_;
    BulletSpriteSet bulletSprites_;
    BulletImpactSystem bulletImpacts_;
    BulletImpactSprites bulletImpactSprites_;
    SpecialExplosionSystem specialExplosions_;
    SpecialExplosionSprites specialExplosionSprites_;
    std::vector<SpecialExplosionEvent> specialExplosionEvents_; // scratch: se llena de nuevo cada Update
    PowerUpSystem powerUps_;
    Texture2D starTexture_{};
    Texture2D helmetTexture_{};
    Texture2D brickUnitTextures_[2]{}; // ver BrickUnit.h: 0 = liso, 1 = esquina/junta
    Texture2D steelUnitTexture_{};      // ver SteelUnit.h: unico frame, se repite en la grilla 2x2
    SpawnFlash player1Spawn_;
    SpawnFlashSprites spawnFlashSprites_;
    ShieldSprites shieldSprites_;
    float player1SpawnX_ = 0.0f;
    float player1SpawnY_ = 0.0f;
    double screenShakeTimer_ = 0.0; // onda expansiva del disparo especial
    int windowWidth_ = 0;
    int windowHeight_ = 0;
};

} // namespace bc
