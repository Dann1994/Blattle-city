#pragma once

#include <array>
#include <vector>

#include <raylib.h>

#include "BulletImpactSprites.h"
#include "Camera.h"
#include "BulletImpactSystem.h"
#include "BulletSprites.h"
#include "BulletSystem.h"
#include "EnemySprites.h"
#include "EnemySystem.h"
#include "FastEnemySystem.h"
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

    // Suma el mando gamepadId al input (no reemplaza el teclado, cualquiera
    // de los dos mueve/dispara). D-pad + stick izquierdo mueven; los botones
    // de disparo/especial se ajustan segun kGamepadShootButton/kGamepadSpecialButton
    // (ver ProcessInput). No hace nada si ese mando no esta conectado.
    void ApplyGamepadInput(PlayerInput& input, int gamepadId) const;

    // Recarga el mapa y reinicia tanque/balas/power-ups a estado inicial
    // (ver Game.cpp). Llamada desde Init() y desde el boton de prueba ESC.
    void ResetState();

    // Reutilizable: vuelve a ubicar al jugador 1/2/3 en su punto de spawn
    // inicial y dispara el destello ahi. Pensado para reusarse al iniciar un
    // nivel o cuando el jugador pierde una vida (Fase 3/4), ademas del boton
    // de prueba (R) y de la destruccion por la propia explosion especial.
    void RespawnPlayer1();
    void RespawnPlayer2();
    void RespawnPlayer3();
    void RespawnPlayer4();
    void RespawnByOwnerId(int ownerId);

    // Activa o desactiva un jugador (teclas 1/2/3, seccion de prueba): al
    // activarlo, reinicia su tanque de cero y lo respawnea; al desactivarlo,
    // el flag pasa a false y Update()/Render() simplemente dejan de tocar a
    // ese jugador (no se mueve, no dispara, no es blanco de nada, no se
    // dibuja) hasta que se vuelva a activar. Distinto de Eliminate(): esto es
    // reversible, pensado solo para probar con menos jugadores en pantalla.
    void SetPlayerActive(bool& activeFlag, Tank& tank, SpawnFlash& spawn, float spawnX, float spawnY, bool active);

    // Item Pala (seccion custom): convierte en hierro temporal las hasta 8
    // celdas alrededor de la base (las que sean Ladrillo o Vacio; el hierro
    // ya existente se deja como esta), y guarda su tipo original en
    // fortifiedCells_ para poder devolverlas como estaban al vencer el
    // timer (ver TickBaseFortification, llamado desde Update).
    void ApplyShovelFortification();
    void TickBaseFortification(double dt);

    // Recorta las celdas de ladrillo del anillo alrededor de la base (ver
    // ResetState) a solo 2 unidades de espesor (de las 4 de la grilla),
    // pegadas al lado que da hacia la base. Se llama una vez despues de
    // cargar el nivel; no afecta a ningun otro ladrillo del mapa.
    void ThinBrickCellVertical(int x, int y, bool keepBottomHalf);
    void ThinBrickCellHorizontal(int x, int y, bool keepRightHalf);

    // Punto unico al que llegan todas las formas de morir (fuego amigo,
    // explosion especial, choque directo del especial): resta 1 vida y, si
    // todavia le quedan, respawnea; si no, Eliminate() (no vuelve a jugar
    // hasta reiniciar la partida).
    void HandlePlayerDeath(Tank& tank, int ownerId);

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
    // tanque para este frame; comun a todos los jugadores activos. No toca
    // nada relacionado con las balas ya en vuelo ni las explosiones (eso se
    // procesa una sola vez para todos los tanques, ver Update()). others son
    // los demas tanques activos (para colision), sin contarse a si mismo.
    void UpdatePlayer(Tank& tank, PlayerInput& input, SpawnFlash& spawn, int ownerId, const std::vector<Tank*>& others, double fixedDt);

    // Arma la lista de tanques activos (para colision con others) excluyendo
    // al de excludeOwnerId, ver UpdatePlayer.
    std::vector<Tank*> ActiveOthers(int excludeOwnerId);

    // Caja de cada tanque activo (todos, sin excluir ninguno), para que los
    // power-ups no aparezcan encima de un jugador (ver PowerUpSystem::Update).
    std::vector<TankOccupiedBounds> ActiveTankBounds() const;

    // Dibuja el destello de aparicion (mientras dura) o el tanque + escudo.
    void RenderTank(const Tank& tank, const SpawnFlash& spawn, const TankSpriteSet& sprites, const MapViewport& viewport);

    // Dibuja un tanque enemigo "Basico" (sprite propio, ver EnemySprites).
    void RenderEnemy(const Enemy& enemy, const EnemySprites& sprites, const MapViewport& viewport);

    // Arma la lista de tanques de jugador activos (sin excluir a nadie), para
    // colision/deteccion/blanco de los enemigos (ver EnemySystem::Update).
    std::vector<Tank*> ActivePlayerTanks();

    // Panel de nivel de arma / disparo especial / calor / paralisis de un
    // tanque, alineado al centro: centerX es el centro horizontal del bloque.
    void RenderPlayerHud(const Tank& tank, const char* label, int centerX, int y);

    static constexpr int kPlayer1Id = 0;
    static constexpr int kPlayer2Id = 1;
    static constexpr int kPlayer3Id = 2;
    static constexpr int kPlayer4Id = 3;

    TileMap map_;
    Tank player1_;
    PlayerInput input1_;
    TankSpriteSet player1Sprites_;
    SpawnFlash player1Spawn_;
    float player1SpawnX_ = 0.0f;
    float player1SpawnY_ = 0.0f;
    bool player1Active_ = true;
    Tank player2_;
    PlayerInput input2_;
    TankSpriteSet player2Sprites_;
    SpawnFlash player2Spawn_;
    float player2SpawnX_ = 0.0f;
    float player2SpawnY_ = 0.0f;
    bool player2Active_ = false;
    Tank player3_;
    PlayerInput input3_;
    TankSpriteSet player3Sprites_;
    SpawnFlash player3Spawn_;
    float player3SpawnX_ = 0.0f;
    float player3SpawnY_ = 0.0f;
    bool player3Active_ = false;
    Tank player4_;
    PlayerInput input4_;
    TankSpriteSet player4Sprites_;
    SpawnFlash player4Spawn_;
    float player4SpawnX_ = 0.0f;
    float player4SpawnY_ = 0.0f;
    bool player4Active_ = false;
    BulletSystem bullets_;
    BulletSpriteSet bulletSprites_;
    EnemySystem enemies_;
    EnemySprites enemySprites_;
    FastEnemySystem fastEnemies_;
    EnemySprites fastEnemySprites_;
    std::vector<std::array<int, 2>> enemySpawnPositions_;
    BulletImpactSystem bulletImpacts_;
    BulletImpactSprites bulletImpactSprites_;
    SpecialExplosionSystem specialExplosions_;
    SpecialExplosionSprites specialExplosionSprites_;
    std::vector<SpecialExplosionEvent> specialExplosionEvents_; // scratch: se llena de nuevo cada Update
    std::vector<SpecialDirectKillEvent> specialDirectKillEvents_; // scratch: se llena de nuevo cada Update
    PowerUpSystem powerUps_;
    Texture2D starTexture_{};
    Texture2D helmetTexture_{};
    Texture2D gunTexture_{};
    Texture2D lifeTexture_{};
    Texture2D grenadeTexture_{};
    Texture2D shovelTexture_{};
    Texture2D clockTexture_{};
    Texture2D brickUnitTextures_[2]{}; // ver BrickUnit.h: 0 = liso, 1 = esquina/junta
    Texture2D steelUnitTexture_{};      // ver SteelUnit.h: unico frame, se repite en la grilla 2x2
    Texture2D treesTexture_{};          // arbusto: bloque entero (no por unidad), se dibuja encima de tanques/balas
    Texture2D waterTextures_[2]{};       // agua: 2 frames que alternan para animar el oleaje
    Texture2D iceTexture_{};             // hielo: hace patinar al tanque y enfria el arma el doble de rapido (ver Tank)
    Texture2D baseEagleTexture_{};       // aguila (Tanques.png, misma fila que agua/arbusto/hielo): objeto a defender, un impacto la destruye y termina la partida
    Texture2D hudLifeIconTexture_{};     // icono chico (Tanques.png, junto a "1P") que reemplaza la "X" en "P1 X 3" del HUD
    Texture2D stageFlagIconTexture_{};   // icono de bandera (Tanques.png, junto a "STAGE"), arriba de la barra izquierda
    SpawnFlashSprites spawnFlashSprites_;
    ShieldSprites shieldSprites_;
    double screenShakeTimer_ = 0.0; // onda expansiva del disparo especial
    FriendlyFireMode friendlyFireMode_ = FriendlyFireMode::Off;
    bool showGrid_ = true; // grilla de referencia sobre el mapa (boton de prueba F6), para ubicar bloques al diseñar el escenario
    int lastGamepadButtonPressed_ = -1; // debug: para calibrar a que numero de boton responde cada mando (ver Render)
    bool gameOver_ = false; // el aguila (TileType::Base) recibio un impacto: se congela todo hasta reiniciar (ESC)
    int windowWidth_ = 0;
    int windowHeight_ = 0;

    // Item Pala: ver ApplyShovelFortification/TickBaseFortification.
    struct FortifiedCell {
        int x = 0;
        int y = 0;
        TileType originalType = TileType::Empty;
    };
    int basePositionX_ = -1;
    int basePositionY_ = -1;
    std::vector<FortifiedCell> fortifiedCells_;
    double baseFortifyTimer_ = 0.0;
};

} // namespace bc
