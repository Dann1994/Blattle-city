#pragma once

#include <vector>

#include <raylib.h>

#include "BulletImpactSprites.h"
#include "Camera.h"
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

// Fuego amigo entre P1 y P2 (seccion custom, configurable con F3):
// Off: las balas de un jugador atraviesan al otro, sin ningun efecto.
// Paralyze (nivel 1): la bala aliada se frena en el tanque y lo paraliza
//   parpadeando 5 segundos (reusa Freeze/IsFrozen), sin quitarle nada mas.
// Damage (nivel 2): el efecto depende del nivel de arma de quien dispara
//   (ver ProcessFriendlyFireDamageHit) — de "resta 1 nivel" a "atraviesa el
//   escudo y resta 2". Si el objetivo ya esta en nivel 1, cualquier impacto
//   sin escudo lo destruye: explota con la animacion del disparo especial y
//   respawnea como si hubiera perdido una vida.
enum class FriendlyFireMode { Off, Paralyze, Damage };

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

    // Reutilizable: vuelve a ubicar al jugador 1/2 en su punto de spawn
    // inicial y dispara el destello ahi. Pensado para reusarse al iniciar un
    // nivel o cuando el jugador pierde una vida (Fase 3/4), ademas del boton
    // de prueba (R) y de la destruccion por la propia explosion especial.
    void RespawnPlayer1();
    void RespawnPlayer2();

    // Aplica el dano de las explosiones especiales de este frame a un
    // tanque (ownerId identifica cual, para reconocer un choque directo -
    // ver SpecialExplosionEvent::directHitOwnerId): con escudo, se pierde el
    // escudo y 1 nivel; sin escudo, 2 niveles (salvo choque directo en nivel
    // 4: -3 niveles y paralizado 5s). Si ya estaba en nivel 1, en vez de
    // bajar mas queda destruido (devuelve true) y quien llama debe
    // respawnearlo.
    bool ApplyExplosionSelfDamage(Tank& tank, int ownerId);

    // Anima la muerte de un tanque (explosion chica a escala nativa, ver
    // Documentaciones/Exploción.png) y le resetea el nivel de arma. No
    // respawnea: eso queda a cargo de quien llama.
    void DestroyTank(Tank& tank);

    // Fuego amigo (ver FriendlyFireMode): si el modo esta activo, mata las
    // balas del OTRO jugador que esten sobre este tanque y le aplica el
    // efecto que corresponda al modo actual. Devuelve true si el tanque
    // quedo destruido, y quien llama debe respawnearlo.
    bool ApplyFriendlyFire(Tank& tank, int ownerId);

    // Nivel 2 de fuego amigo: efecto de un solo impacto sobre tank, segun el
    // nivel de arma de quien disparo (shooterWeaponLevel). Devuelve true si
    // el tanque quedo destruido (explota y se resetea; RespawnPlayerN queda
    // a cargo de quien llama).
    bool ProcessFriendlyFireDamageHit(Tank& tank, int shooterWeaponLevel);

    // Movimiento, disparo (normal y especial) y recogida de power-ups de un
    // tanque para este frame; comun a P1 y P2. No toca nada relacionado con
    // las balas ya en vuelo ni las explosiones (eso se procesa una sola vez
    // para todos los tanques, ver Update()).
    void UpdatePlayer(Tank& tank, PlayerInput& input, SpawnFlash& spawn, int ownerId, Tank* other, double fixedDt);

    // Dibuja el destello de aparicion (mientras dura) o el tanque + escudo.
    void RenderTank(const Tank& tank, const SpawnFlash& spawn, const TankSpriteSet& sprites, const MapViewport& viewport);

    // Panel de nivel de arma / disparo especial / calor / paralisis de un
    // tanque, anclado en (x,y).
    void RenderPlayerHud(const Tank& tank, const char* label, int x, int y);

    static constexpr int kPlayer1Id = 0;
    static constexpr int kPlayer2Id = 1;

    TileMap map_;
    Tank player1_;
    PlayerInput input1_;
    TankSpriteSet player1Sprites_;
    SpawnFlash player1Spawn_;
    float player1SpawnX_ = 0.0f;
    float player1SpawnY_ = 0.0f;
    Tank player2_;
    PlayerInput input2_;
    TankSpriteSet player2Sprites_;
    SpawnFlash player2Spawn_;
    float player2SpawnX_ = 0.0f;
    float player2SpawnY_ = 0.0f;
    BulletSystem bullets_;
    BulletSpriteSet bulletSprites_;
    BulletImpactSystem bulletImpacts_;
    BulletImpactSprites bulletImpactSprites_;
    SpecialExplosionSystem specialExplosions_;
    SpecialExplosionSprites specialExplosionSprites_;
    std::vector<SpecialExplosionEvent> specialExplosionEvents_; // scratch: se llena de nuevo cada Update
    std::vector<SpecialDirectKillEvent> specialDirectKillEvents_; // scratch: se llena de nuevo cada Update
    PowerUpSystem powerUps_;
    Texture2D starTexture_{};
    Texture2D helmetTexture_{};
    Texture2D brickUnitTextures_[2]{}; // ver BrickUnit.h: 0 = liso, 1 = esquina/junta
    Texture2D steelUnitTexture_{};      // ver SteelUnit.h: unico frame, se repite en la grilla 2x2
    SpawnFlashSprites spawnFlashSprites_;
    ShieldSprites shieldSprites_;
    double screenShakeTimer_ = 0.0; // onda expansiva del disparo especial
    FriendlyFireMode friendlyFireMode_ = FriendlyFireMode::Off;
    int windowWidth_ = 0;
    int windowHeight_ = 0;
};

} // namespace bc
