#include "Game.h"

#include <algorithm>

#include <raylib.h>

#include "Camera.h"
#include "Config.h"
#include "LevelFormat.h"

namespace bc {

namespace {

// Duraciones del escudo (seccion 6). No hay un numero exacto documentado del
// juego original para la proteccion al aparecer; 10s para el item Casco esta
// confirmado por una recreacion fiel del juego, y se uso como referencia
// para elegir un valor mas corto (proporcional al tiempo del destello) para
// el respawn, tal como se pidio.
constexpr double kRespawnShieldDuration = 2.0;
constexpr double kHelmetShieldDuration = 10.0;
constexpr double kShieldBlinkInterval = 0.05; // segundos entre cada frame del escudo

// Impacto del disparo especial: sacude toda la pantalla y paraliza a todos
// los tanques (por ahora solo el jugador 1) durante el mismo tiempo.
constexpr double kSpecialImpactFreezeDuration = 2.0;
constexpr double kScreenShakeDuration = 2.0;
constexpr float kScreenShakeMaxOffsetPx = 12.0f;
constexpr float kSpecialShotRecoilDistance = 0.75f; // celdas que retrocede el tanque al disparar el especial

// Fuego amigo nivel 1: parpadeo + paralisis al ser tocado por una bala aliada.
constexpr double kFriendlyFireParalyzeDuration = 5.0;
constexpr double kFrozenBlinkInterval = 0.1; // segundos entre cada parpadeo mientras esta paralizado

// HUD: parpadeo de texto (calor al 100% / municion especial lista).
constexpr double kHudTextBlinkInterval = 0.3;

// Escenario rectangular (seccion custom): el campo de juego (ahora un mapa
// rectangular, ver test_map.json) se encaja en el area central, con una
// barra gris arriba (justo lo alto necesario para los datos de los
// jugadores: P1/P2 agrupados a la izquierda, P3/P4 agrupados a la derecha) y
// un marco gris delgado en los otros 3 lados.
constexpr float kHudTopBarHeight = 85.0f;
constexpr float kHudThinBorder = 8.0f;
constexpr float kHudPanelMargin = 10.0f;
constexpr float kHudColumnWidth = 280.0f; // ancho de cada columna dentro de un grupo de 2 jugadores
constexpr Color kHudPanelColor = Color{45, 45, 48, 255};
constexpr Color kHudPanelBorderColor = Color{90, 90, 96, 255};

Color ColorForTile(TileType type) {
    switch (type) {
        case TileType::Steel: return Color{0xA0, 0xA0, 0xA8, 0xFF};
        case TileType::Water: return Color{0x30, 0x60, 0xD0, 0xFF};
        case TileType::Trees: return Color{0x30, 0x90, 0x30, 0xFF};
        case TileType::Ice:   return Color{0xC0, 0xE8, 0xF8, 0xFF};
        case TileType::Base:  return Color{0xE0, 0xC0, 0x20, 0xFF};
        default:              return Color{0x10, 0x10, 0x10, 0xFF};
    }
}

} // namespace

void Game::Init() {
    windowWidth_ = kDefaultWindowWidth;
    windowHeight_ = kDefaultWindowHeight;

    InitWindow(windowWidth_, windowHeight_, "Battle City Clon - Fase 0");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);
    SetExitKey(KEY_NULL); // ESC ya no cierra la ventana: la usamos para reiniciar (ver ProcessInput)

    player1Sprites_.LoadPlayer1(BC_ASSETS_DIR);
    player2Sprites_.LoadPlayer2(BC_ASSETS_DIR);
    player3Sprites_.LoadPlayer3(BC_ASSETS_DIR);
    player4Sprites_.LoadPlayer4(BC_ASSETS_DIR);
    bulletSprites_.Load(BC_ASSETS_DIR);
    starTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_star.png").c_str());
    SetTextureFilter(starTexture_, TEXTURE_FILTER_POINT);
    helmetTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_helmet.png").c_str());
    SetTextureFilter(helmetTexture_, TEXTURE_FILTER_POINT);
    gunTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_gun.png").c_str());
    SetTextureFilter(gunTexture_, TEXTURE_FILTER_POINT);
    lifeTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_life.png").c_str());
    SetTextureFilter(lifeTexture_, TEXTURE_FILTER_POINT);
    brickUnitTextures_[0] = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/brick_unit_0.png").c_str());
    brickUnitTextures_[1] = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/brick_unit_1.png").c_str());
    SetTextureFilter(brickUnitTextures_[0], TEXTURE_FILTER_POINT);
    SetTextureFilter(brickUnitTextures_[1], TEXTURE_FILTER_POINT);
    steelUnitTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/steel_unit.png").c_str());
    SetTextureFilter(steelUnitTexture_, TEXTURE_FILTER_POINT);
    spawnFlashSprites_.Load(BC_ASSETS_DIR);
    shieldSprites_.Load(BC_ASSETS_DIR);
    bulletImpactSprites_.Load(BC_ASSETS_DIR);
    specialExplosionSprites_.Load(BC_ASSETS_DIR);

    ResetState();
}

// Vuelve a dejar la partida como recien arrancada: recarga el mapa (repone
// los ladrillos rotos), y reinicia tanque, balas, explosiones y power-ups.
// Pensado para probar rapido (ESC), sin tener que cerrar y volver a abrir.
void Game::ResetState() {
    const LevelData level = LoadLevel(std::string(BC_ASSETS_DIR) + "levels/test_map.json");
    map_.LoadFrom(level);

    if (!level.player_spawns.empty()) {
        player1SpawnX_ = static_cast<float>(level.player_spawns[0][0]);
        player1SpawnY_ = static_cast<float>(level.player_spawns[0][1]);
    }
    if (level.player_spawns.size() > 1) {
        player2SpawnX_ = static_cast<float>(level.player_spawns[1][0]);
        player2SpawnY_ = static_cast<float>(level.player_spawns[1][1]);
    } else {
        player2SpawnX_ = player1SpawnX_;
        player2SpawnY_ = player1SpawnY_;
    }
    if (level.player_spawns.size() > 2) {
        player3SpawnX_ = static_cast<float>(level.player_spawns[2][0]);
        player3SpawnY_ = static_cast<float>(level.player_spawns[2][1]);
    } else {
        player3SpawnX_ = player1SpawnX_;
        player3SpawnY_ = player1SpawnY_;
    }
    if (level.player_spawns.size() > 3) {
        player4SpawnX_ = static_cast<float>(level.player_spawns[3][0]);
        player4SpawnY_ = static_cast<float>(level.player_spawns[3][1]);
    } else {
        player4SpawnX_ = player1SpawnX_;
        player4SpawnY_ = player1SpawnY_;
    }

    player1_ = Tank{};
    player2_ = Tank{};
    player3_ = Tank{};
    player4_ = Tank{};
    // TODO: exponer como opcion en el menu de configuracion (seccion 12.5).
    player1_.SetFireMode(FireMode::SinglePress);
    player2_.SetFireMode(FireMode::SinglePress);
    player3_.SetFireMode(FireMode::SinglePress);
    player4_.SetFireMode(FireMode::SinglePress);

    bullets_ = BulletSystem{};
    bulletImpacts_ = BulletImpactSystem{};
    specialExplosions_ = SpecialExplosionSystem{};
    specialExplosionEvents_.clear();
    powerUps_ = PowerUpSystem{};
    screenShakeTimer_ = 0.0;

    // Al empezar (o reiniciar con ESC) solo esta presente el jugador 1; P2/P3/P4
    // se traen con las teclas 1/2/3/4 (ver ProcessInput y SetPlayerActive).
    player1Active_ = true;
    player2Active_ = false;
    player3Active_ = false;
    player4Active_ = false;

    RespawnPlayer1();
}

void Game::RespawnPlayer1() {
    player1_.SetPosition(player1SpawnX_, player1SpawnY_);
    player1_.SetFacing(Direction::Up);
    player1Spawn_.Start(player1SpawnX_, player1SpawnY_);
    player1_.ActivateShield(kRespawnShieldDuration);
}

void Game::RespawnPlayer2() {
    player2_.SetPosition(player2SpawnX_, player2SpawnY_);
    player2_.SetFacing(Direction::Up);
    player2Spawn_.Start(player2SpawnX_, player2SpawnY_);
    player2_.ActivateShield(kRespawnShieldDuration);
}

void Game::RespawnPlayer3() {
    player3_.SetPosition(player3SpawnX_, player3SpawnY_);
    player3_.SetFacing(Direction::Up);
    player3Spawn_.Start(player3SpawnX_, player3SpawnY_);
    player3_.ActivateShield(kRespawnShieldDuration);
}

void Game::RespawnPlayer4() {
    player4_.SetPosition(player4SpawnX_, player4SpawnY_);
    player4_.SetFacing(Direction::Up);
    player4Spawn_.Start(player4SpawnX_, player4SpawnY_);
    player4_.ActivateShield(kRespawnShieldDuration);
}

void Game::RespawnByOwnerId(int ownerId) {
    if (ownerId == kPlayer1Id) {
        RespawnPlayer1();
    } else if (ownerId == kPlayer2Id) {
        RespawnPlayer2();
    } else if (ownerId == kPlayer3Id) {
        RespawnPlayer3();
    } else if (ownerId == kPlayer4Id) {
        RespawnPlayer4();
    }
}

void Game::SetPlayerActive(bool& activeFlag, Tank& tank, SpawnFlash& spawn, float spawnX, float spawnY, bool active) {
    activeFlag = active;
    if (!active) {
        return; // Update()/Render() ya no lo van a tocar mientras este en false
    }
    tank = Tank{};
    tank.SetFireMode(FireMode::SinglePress);
    tank.SetPosition(spawnX, spawnY);
    tank.SetFacing(Direction::Up);
    spawn.Start(spawnX, spawnY);
    tank.ActivateShield(kRespawnShieldDuration);
}

void Game::ProcessInput() {
    // Esquema de teclado WASD para P1 (seccion 9); mandos llegan en Fase 5.
    input1_.moveUp = IsKeyDown(KEY_W);
    input1_.moveDown = IsKeyDown(KEY_S);
    input1_.moveLeft = IsKeyDown(KEY_A);
    input1_.moveRight = IsKeyDown(KEY_D);
    input1_.shoot = IsKeyDown(KEY_LEFT_CONTROL);
    input1_.specialShoot = IsKeyDown(KEY_SPACE);

    // Esquema de flechas para P2.
    input2_.moveUp = IsKeyDown(KEY_UP);
    input2_.moveDown = IsKeyDown(KEY_DOWN);
    input2_.moveLeft = IsKeyDown(KEY_LEFT);
    input2_.moveRight = IsKeyDown(KEY_RIGHT);
    input2_.shoot = IsKeyDown(KEY_RIGHT_CONTROL);
    input2_.specialShoot = IsKeyDown(KEY_RIGHT_SHIFT);

    // Esquema IJKL para P3.
    input3_.moveUp = IsKeyDown(KEY_I);
    input3_.moveDown = IsKeyDown(KEY_K);
    input3_.moveLeft = IsKeyDown(KEY_J);
    input3_.moveRight = IsKeyDown(KEY_L);
    input3_.shoot = IsKeyDown(KEY_U);
    input3_.specialShoot = IsKeyDown(KEY_O);

    // Esquema numpad (8456) para P4.
    input4_.moveUp = IsKeyDown(KEY_KP_8);
    input4_.moveDown = IsKeyDown(KEY_KP_5);
    input4_.moveLeft = IsKeyDown(KEY_KP_4);
    input4_.moveRight = IsKeyDown(KEY_KP_6);
    input4_.shoot = IsKeyDown(KEY_KP_0);
    input4_.specialShoot = IsKeyDown(KEY_KP_ENTER);

    // Botones de prueba: traen o hacen desaparecer a cada jugador (al
    // arrancar solo esta presente el jugador 1, ver ResetState).
    if (IsKeyPressed(KEY_ONE)) {
        SetPlayerActive(player1Active_, player1_, player1Spawn_, player1SpawnX_, player1SpawnY_, !player1Active_);
    }
    if (IsKeyPressed(KEY_TWO)) {
        SetPlayerActive(player2Active_, player2_, player2Spawn_, player2SpawnX_, player2SpawnY_, !player2Active_);
    }
    if (IsKeyPressed(KEY_THREE)) {
        SetPlayerActive(player3Active_, player3_, player3Spawn_, player3SpawnX_, player3SpawnY_, !player3Active_);
    }
    if (IsKeyPressed(KEY_FOUR)) {
        SetPlayerActive(player4Active_, player4_, player4Spawn_, player4SpawnX_, player4SpawnY_, !player4Active_);
    }

    // Boton de prueba: repite el respawn en el punto de spawn inicial, para
    // poder ver el destello sin tener que reiniciar el juego (solo a los
    // jugadores presentes).
    if (IsKeyPressed(KEY_R)) {
        if (player1Active_) RespawnPlayer1();
        if (player2Active_) RespawnPlayer2();
        if (player3Active_) RespawnPlayer3();
        if (player4Active_) RespawnPlayer4();
    }

    // Boton de prueba: reinicia toda la partida (mapa, tanque, balas, power-ups).
    if (IsKeyPressed(KEY_ESCAPE)) {
        ResetState();
    }

    // Botones de prueba: fuerzan la aparicion de cada power-up (F1 Estrella,
    // F2 Casco, ...). No son mecanicas del juego final.
    if (IsKeyPressed(KEY_F1)) {
        powerUps_.ForceSpawn(PowerUpType::Star, map_);
    }
    if (IsKeyPressed(KEY_F2)) {
        powerUps_.ForceSpawn(PowerUpType::Helmet, map_);
    }
    if (IsKeyPressed(KEY_F4)) {
        powerUps_.ForceSpawn(PowerUpType::Gun, map_);
    }
    if (IsKeyPressed(KEY_F5)) {
        powerUps_.ForceSpawn(PowerUpType::Life, map_);
    }

    // Boton de prueba: rota el modo de fuego amigo Off -> nivel 1 -> nivel 2 -> Off.
    if (IsKeyPressed(KEY_F3)) {
        switch (friendlyFireMode_) {
            case FriendlyFireMode::Off:      friendlyFireMode_ = FriendlyFireMode::Paralyze; break;
            case FriendlyFireMode::Paralyze: friendlyFireMode_ = FriendlyFireMode::Damage;    break;
            case FriendlyFireMode::Damage:   friendlyFireMode_ = FriendlyFireMode::Off;       break;
        }
    }
}

void Game::UpdatePlayer(Tank& tank, PlayerInput& input, SpawnFlash& spawn, int ownerId, const std::vector<Tank*>& others, double fixedDt) {
    if (tank.IsEliminated()) {
        return; // sin vidas: fuera de la partida, no se mueve ni dispara
    }

    tank.TickShield(fixedDt);
    tank.TickShootCooldown(fixedDt);
    tank.TickHeatDecay(fixedDt);
    tank.TickFreeze(fixedDt);
    tank.TickRecoil(fixedDt, map_, others);

    if (spawn.IsActive()) {
        // Mientras dura el destello de aparicion, el tanque no se mueve ni
        // dispara ni puede ser tocado por power-ups (seccion 5 y 13).
        spawn.Update(fixedDt);
        return;
    }

    tank.Update(fixedDt, input, map_, others);

    // Se consumen los dos triggers siempre (para no perder el flanco de
    // subida del boton mientras hay cooldown), pero solo disparan de verdad
    // si el tanque puede disparar. El especial tiene prioridad si ambos se
    // piden en el mismo frame.
    const bool normalTrigger = tank.ConsumeShootTrigger(input);
    const bool specialTrigger = tank.ConsumeSpecialShotTrigger(input);

    if (tank.CanShoot() && !tank.IsFrozen()) {
        float muzzleX = 0.0f, muzzleY = 0.0f;
        tank.MuzzlePosition(muzzleX, muzzleY);
        if (specialTrigger && tank.HasSpecialShotReady()) {
            bullets_.TryShootSpecial(ownerId, muzzleX, muzzleY, tank.Facing());
            tank.ConsumeSpecialShot();
            tank.RegisterSpecialShotHeat();
            tank.StartRecoil(kSpecialShotRecoilDistance);
        } else if (normalTrigger) {
            if (bullets_.TryShoot(ownerId, muzzleX, muzzleY, tank.Facing(), tank.BulletSpeed(), tank.WeaponLevel(), tank.MaxBullets())) {
                tank.RegisterNormalShotHeat();
            }
        }
    }

    PowerUpType pickedType{};
    if (powerUps_.TryPickup(tank.X(), tank.Y(), pickedType)) {
        if (pickedType == PowerUpType::Star) {
            tank.PickupStar();
        } else if (pickedType == PowerUpType::Helmet) {
            tank.ActivateShield(kHelmetShieldDuration);
        } else if (pickedType == PowerUpType::Gun) {
            tank.PickupGun();
        } else if (pickedType == PowerUpType::Life) {
            tank.AddLife();
        }
    }
}

void Game::HandlePlayerDeath(Tank& tank, int ownerId) {
    tank.LoseLife();
    if (tank.IsOutOfLives()) {
        tank.Eliminate();
        return;
    }
    RespawnByOwnerId(ownerId);
}

std::vector<Tank*> Game::ActiveOthers(int excludeOwnerId) {
    std::vector<Tank*> others;
    others.reserve(3);
    if (player1Active_ && excludeOwnerId != kPlayer1Id) {
        others.push_back(&player1_);
    }
    if (player2Active_ && excludeOwnerId != kPlayer2Id) {
        others.push_back(&player2_);
    }
    if (player3Active_ && excludeOwnerId != kPlayer3Id) {
        others.push_back(&player3_);
    }
    if (player4Active_ && excludeOwnerId != kPlayer4Id) {
        others.push_back(&player4_);
    }
    return others;
}

void Game::DestroyTank(Tank& tank) {
    // Reusa la animacion de 5 frames de Documentaciones/Exploción.png que ya
    // carga el disparo especial, a escala nativa (mas chica).
    specialExplosions_.Spawn(tank.X() + 0.5f, tank.Y() + 0.5f, /*nativeScale=*/true);
    tank.ResetWeaponLevel();
}

bool Game::ApplyExplosionSelfDamage(Tank& tank, int ownerId) {
    if (tank.IsEliminated()) {
        return false; // fuera de la partida: no le pasa nada mas
    }
    bool destroyed = false;
    for (const SpecialExplosionEvent& event : specialExplosionEvents_) {
        const bool directHit = (event.directHitOwnerId == ownerId);

        if (!directHit) {
            const float tankCenterX = tank.X() + 0.5f;
            const float tankCenterY = tank.Y() + 0.5f;
            const float dx = tankCenterX - event.x;
            const float dy = tankCenterY - event.y;
            const float hitRadius = event.radius + 0.5f; // + medio tanque de margen
            if (dx * dx + dy * dy > hitRadius * hitRadius) {
                continue;
            }
        }
        // directHit == true ya esta garantizado dentro del radio: es el
        // punto exacto donde el especial choco contra este tanque.

        const bool wasShielded = tank.IsShielded();
        if (wasShielded) {
            // El escudo absorbe la explosion: se pierde el escudo, baja 1
            // nivel (2 sin escudo, salvo el choque directo en nivel 4 de
            // abajo) y queda paralizado 5s.
            tank.ActivateShield(0.0);
            tank.Freeze(kFriendlyFireParalyzeDuration);
        }

        if (directHit && !wasShielded && tank.WeaponLevel() == 4) {
            // Choque directo del especial contra un nivel 4 sin escudo:
            // -3 niveles y paralizado 5s (mas fuerte que el dano generico
            // por radio).
            tank.ApplyWeaponLevelPenalty(3);
            tank.Freeze(kFriendlyFireParalyzeDuration);
            continue;
        }

        if (tank.WeaponLevel() <= 1) {
            tank.ResetWeaponLevel();
            destroyed = true;
        } else {
            tank.ApplyWeaponLevelPenalty(wasShielded ? 1 : 2);
        }
    }
    return destroyed;
}

bool Game::ApplyFriendlyFire(Tank& tank, int ownerId) {
    if (tank.IsEliminated()) {
        return false; // fuera de la partida: no le pasa nada mas
    }
    if (friendlyFireMode_ == FriendlyFireMode::Off) {
        return false; // las balas del otro jugador atraviesan sin chequeo alguno
    }

    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    tank.GetBounds(left, right, top, bottom);
    std::vector<int> shooterLevels;
    if (!bullets_.KillBulletsHittingBox(ownerId, left, right, top, bottom, bulletImpacts_, shooterLevels)) {
        return false;
    }

    if (friendlyFireMode_ == FriendlyFireMode::Paralyze) {
        // El escudo bloquea el golpe por completo: no paraliza y no se pierde.
        if (!tank.IsShielded()) {
            tank.Freeze(kFriendlyFireParalyzeDuration);
        }
        return false;
    }

    // Damage (nivel 2): cada bala que impacto este frame se procesa en
    // orden; si alguna destruye al tanque, no tiene sentido seguir.
    for (int shooterLevel : shooterLevels) {
        if (ProcessFriendlyFireDamageHit(tank, shooterLevel)) {
            return true;
        }
    }
    return false;
}

bool Game::ProcessFriendlyFireDamageHit(Tank& tank, int shooterWeaponLevel) {
    if (shooterWeaponLevel == 4) {
        // Nivel 4: atraviesa el escudo (se lo lleva puesto, sin dano extra
        // ese golpe); sin escudo, pega directo y saca 2 niveles.
        if (tank.IsShielded()) {
            tank.ActivateShield(0.0);
            return false;
        }
        if (tank.WeaponLevel() <= 2) {
            DestroyTank(tank);
            return true;
        }
        tank.ApplyWeaponLevelPenalty(2);
        return false;
    }

    // Niveles 1-3: el escudo bloquea el golpe por completo.
    if (tank.IsShielded()) {
        return false;
    }

    if (tank.WeaponLevel() <= 1) {
        // Ya esta en el nivel minimo: cualquier impacto sin escudo lo destruye.
        DestroyTank(tank);
        return true;
    }

    switch (shooterWeaponLevel) {
        case 1:
            tank.ApplyWeaponLevelPenalty(1);
            break;
        case 2:
            tank.RegisterChipHit(); // -1 nivel recien al segundo golpe de este tipo
            break;
        case 3:
            tank.ApplyWeaponLevelPenalty(1);
            tank.Freeze(kFriendlyFireParalyzeDuration);
            break;
        default:
            break;
    }
    return false;
}

void Game::Update(double fixedDt) {
    if (screenShakeTimer_ > 0.0) {
        screenShakeTimer_ = std::max(0.0, screenShakeTimer_ - fixedDt);
    }

    if (player1Active_) UpdatePlayer(player1_, input1_, player1Spawn_, kPlayer1Id, ActiveOthers(kPlayer1Id), fixedDt);
    if (player2Active_) UpdatePlayer(player2_, input2_, player2Spawn_, kPlayer2Id, ActiveOthers(kPlayer2Id), fixedDt);
    if (player3Active_) UpdatePlayer(player3_, input3_, player3Spawn_, kPlayer3Id, ActiveOthers(kPlayer3Id), fixedDt);
    if (player4Active_) UpdatePlayer(player4_, input4_, player4Spawn_, kPlayer4Id, ActiveOthers(kPlayer4Id), fixedDt);

    // Estado actual de los tanques presentes para que BulletSystem resuelva
    // un choque directo del disparo especial (ver TankCombatState).
    std::vector<TankCombatState> tanks;
    tanks.reserve(4);
    struct ActivePlayerRef { int ownerId; Tank* tank; bool active; };
    const ActivePlayerRef activePlayers[4] = {
        {kPlayer1Id, &player1_, player1Active_},
        {kPlayer2Id, &player2_, player2Active_},
        {kPlayer3Id, &player3_, player3Active_},
        {kPlayer4Id, &player4_, player4Active_},
    };
    for (const ActivePlayerRef& ref : activePlayers) {
        if (!ref.active || ref.tank->IsEliminated()) {
            continue; // ausente o fuera de la partida: no es un blanco valido
        }
        TankCombatState state;
        state.ownerId = ref.ownerId;
        ref.tank->GetBounds(state.left, state.right, state.top, state.bottom);
        state.shielded = ref.tank->IsShielded();
        state.weaponLevel = ref.tank->WeaponLevel();
        tanks.push_back(state);
    }

    bullets_.Update(fixedDt, map_, bulletImpacts_, specialExplosions_, specialExplosionEvents_, tanks, specialDirectKillEvents_);
    bulletImpacts_.Update(fixedDt);
    specialExplosions_.Update(fixedDt);
    powerUps_.Update(fixedDt, map_);

    for (const ActivePlayerRef& ref : activePlayers) {
        if (!ref.active) {
            continue;
        }
        if (ApplyFriendlyFire(*ref.tank, ref.ownerId)) {
            HandlePlayerDeath(*ref.tank, ref.ownerId);
        }
    }

    // Choque directo del especial contra un tanque nivel 1-3 sin escudo: se
    // destruye y la bala seguia de largo (ver BulletSystem::Update), asi que
    // no paso por ninguna explosion.
    for (const SpecialDirectKillEvent& kill : specialDirectKillEvents_) {
        for (const ActivePlayerRef& ref : activePlayers) {
            if (ref.active && kill.targetOwnerId == ref.ownerId) {
                DestroyTank(*ref.tank);
                HandlePlayerDeath(*ref.tank, ref.ownerId);
                break;
            }
        }
    }

    if (!specialExplosionEvents_.empty()) {
        // Efecto global, no depende de la distancia (a diferencia del dano):
        // toda explosion especial sacude la pantalla y paraliza a TODOS los
        // tanques presentes.
        screenShakeTimer_ = kScreenShakeDuration;
        for (const ActivePlayerRef& ref : activePlayers) {
            if (ref.active) {
                ref.tank->Freeze(kSpecialImpactFreezeDuration);
            }
        }
    }

    // La explosion especial daña a quien alcance, incluido el propio tanque
    // que la disparo (seccion pedida explicitamente: "amigo-enemigo" a
    // proposito, es el riesgo de usar el disparo especial).
    for (const ActivePlayerRef& ref : activePlayers) {
        if (!ref.active) {
            continue;
        }
        if (ApplyExplosionSelfDamage(*ref.tank, ref.ownerId)) {
            HandlePlayerDeath(*ref.tank, ref.ownerId);
        }
    }
}

void Game::RenderTank(const Tank& tank, const SpawnFlash& spawn, const TankSpriteSet& sprites, const MapViewport& viewport) {
    if (tank.IsEliminated()) {
        return; // sin vidas: no se dibuja mas
    }

    if (spawn.IsActive()) {
        const Texture2D flashTex = spawnFlashSprites_.Get(spawn.FrameIndex());
        const Rectangle flashSrc{0.0f, 0.0f, static_cast<float>(flashTex.width), static_cast<float>(flashTex.height)};
        const Rectangle flashDst{viewport.TileToScreenX(spawn.X()), viewport.TileToScreenY(spawn.Y()), viewport.tileScreenSize, viewport.tileScreenSize};
        DrawTexturePro(flashTex, flashSrc, flashDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        return;
    }

    // Paralizado (fuego amigo nivel 1, o la onda del especial): parpadea en
    // vez de dibujarse fijo, para que se note claramente que no responde.
    const bool blinkedOut = tank.IsFrozen() && (static_cast<int>(GetTime() / kFrozenBlinkInterval) % 2 == 0);

    // Calor al 100% (bloqueado por sobrecalentamiento): igual que el brillo
    // del disparo especial de abajo, progresivo en 3 pasos (normal -> rojo
    // suave -> rojo pleno -> rojo suave -> normal) en vez de un parpadeo
    // duro de 2 estados, tiñendo el sprite en vez de necesitar otro set.
    Color tint = WHITE;
    if (tank.HeatPercent() >= 100.0f) {
        constexpr double kOverheatStepDuration = 0.12;
        constexpr int kOverheatSequence[4] = {0, 1, 2, 1};
        const int step = static_cast<int>(GetTime() / kOverheatStepDuration) % 4;
        const int level = kOverheatSequence[step];
        if (level == 1) {
            tint = Color{255, 150, 150, 255};
        } else if (level == 2) {
            tint = RED;
        }
    }

    const Texture2D tankTex = sprites.Get(tank.WeaponLevel(), tank.Facing(), tank.AnimFrame());
    const Rectangle src{0.0f, 0.0f, static_cast<float>(tankTex.width), static_cast<float>(tankTex.height)};
    const Rectangle dst{viewport.TileToScreenX(tank.X()), viewport.TileToScreenY(tank.Y()), viewport.tileScreenSize, viewport.tileScreenSize};
    if (!blinkedOut) {
        DrawTexturePro(tankTex, src, dst, Vector2{0.0f, 0.0f}, 0.0f, tint);
    }

    // Disparo especial disponible: brillo progresivo (normal -> brillo1 ->
    // brillo2 -> brillo1 -> normal, ida y vuelta) en vez de un parpadeo duro
    // de 2 estados, para que el cambio se sienta suave. Un tint comun no
    // puede aclarar mas alla del blanco, asi que se redibuja el mismo
    // sprite con mezcla aditiva y alpha creciente encima.
    if (tank.HasSpecialShotReady() && !blinkedOut) {
        constexpr double kPulseStepDuration = 0.12;
        constexpr int kPulseSequence[4] = {0, 1, 2, 1};
        const int step = static_cast<int>(GetTime() / kPulseStepDuration) % 4;
        const int level = kPulseSequence[step];
        if (level > 0) {
            const unsigned char alpha = (level == 1) ? 70 : 150;
            BeginBlendMode(BLEND_ADDITIVE);
            DrawTexturePro(tankTex, src, dst, Vector2{0.0f, 0.0f}, 0.0f, Color{255, 255, 255, alpha});
            EndBlendMode();
        }
    }

    if (tank.IsShielded() && !blinkedOut) {
        const int shieldFrame = static_cast<int>(GetTime() / kShieldBlinkInterval) % 2;
        const Texture2D shieldTex = shieldSprites_.Get(shieldFrame);
        const Rectangle shieldSrc{0.0f, 0.0f, static_cast<float>(shieldTex.width), static_cast<float>(shieldTex.height)};
        DrawTexturePro(shieldTex, shieldSrc, dst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }
}

void Game::RenderPlayerHud(const Tank& tank, const char* label, int x, int y) {
    // Parpadeo de texto (calor al 100% / municion especial lista): mismo
    // criterio de "visible la mitad del tiempo" que ya se usa para el tanque
    // paralizado (ver kFrozenBlinkInterval), pero un poco mas lento porque es
    // texto de HUD, no el sprite del tanque.
    const bool textBlinkOn = static_cast<int>(GetTime() / kHudTextBlinkInterval) % 2 == 0;

    if (tank.IsEliminated()) {
        DrawText(TextFormat("%s - SIN VIDAS", label), x, y, 20, RED);
        return;
    }

    DrawText(TextFormat("%s X %d - Nivel %d", label, tank.Lives(), tank.WeaponLevel()), x, y, 20, RAYWHITE);

    constexpr int kHeatBarWidth = 160;
    constexpr int kHeatBarHeight = 14;
    constexpr int kLinePitch = 20; // alto de renglon + separacion minima entre lineas de texto
    const int heatBarY = y + kLinePitch;
    const float heatPercent = tank.HeatPercent();
    const bool overheated = heatPercent >= 100.0f;
    DrawRectangle(x, heatBarY, kHeatBarWidth, kHeatBarHeight, DARKGRAY);
    const int filledWidth = static_cast<int>(kHeatBarWidth * (heatPercent / 100.0f));
    DrawRectangle(x, heatBarY, filledWidth, kHeatBarHeight, overheated ? RED : ORANGE);
    DrawRectangleLines(x, heatBarY, kHeatBarWidth, kHeatBarHeight, RAYWHITE);
    if (!overheated || textBlinkOn) {
        DrawText(TextFormat(":%.0f%%", heatPercent), x + kHeatBarWidth + 6, heatBarY - 2, 18, overheated ? RED : RAYWHITE);
    }

    if (tank.HasSpecialShotReady() && textBlinkOn) {
        DrawText("Municion especial lista!", x, heatBarY + kLinePitch, 18, RED);
    }
}

void Game::Render(double /*interpolationAlpha*/) {
    windowWidth_ = GetScreenWidth();
    windowHeight_ = GetScreenHeight();

    // El campo de juego (rectangular, ver test_map.json) se encaja en el area
    // central: barra gris gruesa arriba (datos de jugadores) y marco gris
    // delgado en los otros 3 lados.
    const int topBarH = static_cast<int>(kHudTopBarHeight);
    const int thinBorder = static_cast<int>(kHudThinBorder);
    const float playAreaWidth = static_cast<float>(windowWidth_) - kHudThinBorder * 2.0f;
    const float playAreaHeight = static_cast<float>(windowHeight_) - kHudTopBarHeight - kHudThinBorder;
    MapViewport viewport = MapViewport::Compute(static_cast<int>(playAreaWidth), static_cast<int>(playAreaHeight), map_.Width(), map_.Height(), kTileSize);
    viewport.offsetX += kHudThinBorder;
    viewport.offsetY += kHudTopBarHeight;

    BeginDrawing();
    // Mismo color que las celdas vacias (ColorForTile de Empty), asi una
    // unidad de ladrillo destruida no deja un negro puro que desentona con
    // el resto del fondo.
    ClearBackground(ColorForTile(TileType::Empty));

    // Marco gris (barra superior gruesa + bordes delgados en los otros 3
    // lados), fijo y fuera de la sacudida de pantalla para que el texto no salte.
    DrawRectangle(0, 0, windowWidth_, topBarH, kHudPanelColor);
    DrawRectangle(0, topBarH, thinBorder, windowHeight_ - topBarH, kHudPanelColor);
    DrawRectangle(windowWidth_ - thinBorder, topBarH, thinBorder, windowHeight_ - topBarH, kHudPanelColor);
    DrawRectangle(0, windowHeight_ - thinBorder, windowWidth_, thinBorder, kHudPanelColor);
    DrawLine(0, topBarH, windowWidth_, topBarH, kHudPanelBorderColor);
    DrawLine(thinBorder, topBarH, thinBorder, windowHeight_, kHudPanelBorderColor);
    DrawLine(windowWidth_ - thinBorder, topBarH, windowWidth_ - thinBorder, windowHeight_, kHudPanelBorderColor);
    DrawLine(0, windowHeight_ - thinBorder, windowWidth_, windowHeight_ - thinBorder, kHudPanelBorderColor);

    // Sacudida de pantalla (onda expansiva del disparo especial): desplaza el
    // campo de juego (mapa, tanques, balas) con un offset aleatorio que decae
    // a medida que pasa el tiempo. Los paneles laterales quedan fijos.
    Camera2D shakeCamera{};
    shakeCamera.zoom = 1.0f;
    if (screenShakeTimer_ > 0.0) {
        const float magnitude = kScreenShakeMaxOffsetPx * static_cast<float>(screenShakeTimer_ / kScreenShakeDuration);
        const float shakeX = (static_cast<float>(GetRandomValue(-100, 100)) / 100.0f) * magnitude;
        const float shakeY = (static_cast<float>(GetRandomValue(-100, 100)) / 100.0f) * magnitude;
        shakeCamera.target = Vector2{-shakeX, -shakeY};
    }
    BeginMode2D(shakeCamera);

    const float brickUnitDst = viewport.tileScreenSize / kBrickGridSize;
    const float steelUnitDst = viewport.tileScreenSize / kSteelGridSize;

    for (int y = 0; y < map_.Height(); ++y) {
        for (int x = 0; x < map_.Width(); ++x) {
            const Cell& cell = map_.At(x, y);
            const float screenX = viewport.TileToScreenX(x);
            const float screenY = viewport.TileToScreenY(y);

            if (cell.type == TileType::Brick) {
                // El bloque se arma con una grilla de 4x4 unidades minimas
                // (ver BrickUnit.h / Documentaciones/Ladrillos.png), cada una
                // destruida de forma individual.
                for (int row = 0; row < kBrickGridSize; ++row) {
                    for (int col = 0; col < kBrickGridSize; ++col) {
                        const BrickUnit& unit = cell.brickUnits[row * kBrickGridSize + col];
                        if (!unit.alive) {
                            continue;
                        }
                        const Texture2D unitTex = brickUnitTextures_[unit.frame];
                        const Rectangle unitSrc{0.0f, 0.0f, static_cast<float>(unitTex.width), static_cast<float>(unitTex.height)};
                        const Rectangle unitDst{screenX + col * brickUnitDst, screenY + row * brickUnitDst, brickUnitDst, brickUnitDst};
                        DrawTexturePro(unitTex, unitSrc, unitDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
                    }
                }
            } else if (cell.type == TileType::Steel) {
                // El bloque se arma con una grilla de 2x2 unidades minimas
                // (ver SteelUnit.h), cada una con su propia resistencia.
                const Rectangle steelSrc{0.0f, 0.0f, static_cast<float>(steelUnitTexture_.width), static_cast<float>(steelUnitTexture_.height)};
                for (int row = 0; row < kSteelGridSize; ++row) {
                    for (int col = 0; col < kSteelGridSize; ++col) {
                        const SteelUnit& unit = cell.steelUnits[row * kSteelGridSize + col];
                        if (!unit.alive) {
                            continue;
                        }
                        const Rectangle unitDst{screenX + col * steelUnitDst, screenY + row * steelUnitDst, steelUnitDst, steelUnitDst};
                        DrawTexturePro(steelUnitTexture_, steelSrc, unitDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
                    }
                }
            } else {
                const Rectangle rect{screenX, screenY, viewport.tileScreenSize, viewport.tileScreenSize};
                DrawRectangleRec(rect, ColorForTile(cell.type));
            }
        }
    }

    if (player1Active_) RenderTank(player1_, player1Spawn_, player1Sprites_, viewport);
    if (player2Active_) RenderTank(player2_, player2Spawn_, player2Sprites_, viewport);
    if (player3Active_) RenderTank(player3_, player3Spawn_, player3Sprites_, viewport);
    if (player4Active_) RenderTank(player4_, player4Spawn_, player4Sprites_, viewport);

    const float pixelScale = viewport.tileScreenSize / static_cast<float>(kTileSize);
    for (const Bullet& bullet : bullets_.Bullets()) {
        const Texture2D bulletTex = bulletSprites_.Get(bullet.direction);
        const float w = static_cast<float>(bulletTex.width) * pixelScale;
        const float h = static_cast<float>(bulletTex.height) * pixelScale;
        const Rectangle bulletSrc{0.0f, 0.0f, static_cast<float>(bulletTex.width), static_cast<float>(bulletTex.height)};
        const Rectangle bulletDst{viewport.TileToScreenX(bullet.x) - w * 0.5f, viewport.TileToScreenY(bullet.y) - h * 0.5f, w, h};
        DrawTexturePro(bulletTex, bulletSrc, bulletDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    const float impactSize = viewport.tileScreenSize * 0.9f;
    for (const BulletImpact& impact : bulletImpacts_.Impacts()) {
        const Texture2D impactTex = bulletImpactSprites_.Get(impact.frameIndex);
        const Rectangle impactSrc{0.0f, 0.0f, static_cast<float>(impactTex.width), static_cast<float>(impactTex.height)};
        const Rectangle impactDst{viewport.TileToScreenX(impact.x) - impactSize * 0.5f, viewport.TileToScreenY(impact.y) - impactSize * 0.5f, impactSize, impactSize};
        DrawTexturePro(impactTex, impactSrc, impactDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    // Diametro del radio de la explosion especial, en pantalla.
    const float specialExplosionSize = viewport.tileScreenSize * kSpecialExplosionRadius * 2.0f;
    for (const SpecialExplosion& explosion : specialExplosions_.Explosions()) {
        const Texture2D explosionTex = specialExplosionSprites_.Get(explosion.frameIndex);
        const Rectangle explosionSrc{0.0f, 0.0f, static_cast<float>(explosionTex.width), static_cast<float>(explosionTex.height)};
        // El estallido de muerte usa la escala nativa (mismo pixel-a-pantalla
        // que los tanques/balas), mas chico; el del disparo especial usa el
        // tamano fijo segun su radio de destruccion.
        const float explosionW = explosion.nativeScale ? static_cast<float>(explosionTex.width) * pixelScale : specialExplosionSize;
        const float explosionH = explosion.nativeScale ? static_cast<float>(explosionTex.height) * pixelScale : specialExplosionSize;
        const Rectangle explosionDst{viewport.TileToScreenX(explosion.x) - explosionW * 0.5f, viewport.TileToScreenY(explosion.y) - explosionH * 0.5f, explosionW, explosionH};
        DrawTexturePro(explosionTex, explosionSrc, explosionDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    if (powerUps_.Active().alive && powerUps_.IsBlinkVisible()) {
        Texture2D iconTex = starTexture_;
        if (powerUps_.Active().type == PowerUpType::Helmet) {
            iconTex = helmetTexture_;
        } else if (powerUps_.Active().type == PowerUpType::Gun) {
            iconTex = gunTexture_;
        } else if (powerUps_.Active().type == PowerUpType::Life) {
            iconTex = lifeTexture_;
        }
        const Rectangle iconSrc{0.0f, 0.0f, static_cast<float>(iconTex.width), static_cast<float>(iconTex.height)};
        const Rectangle iconDst{viewport.TileToScreenX(powerUps_.Active().x), viewport.TileToScreenY(powerUps_.Active().y), viewport.tileScreenSize, viewport.tileScreenSize};
        DrawTexturePro(iconTex, iconSrc, iconDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    EndMode2D();

    // Texto de UI (barra superior + aviso de fuego amigo): dibujado fuera de
    // la sacudida de pantalla, para que quede fijo mientras el campo de
    // juego tiembla. P1/P2 agrupados en el borde izquierdo, P3/P4 agrupados
    // en el borde derecho.
    const int hudMargin = static_cast<int>(kHudPanelMargin);
    const int columnW = static_cast<int>(kHudColumnWidth);
    if (player1Active_) RenderPlayerHud(player1_, "P1", hudMargin, hudMargin);
    if (player2Active_) RenderPlayerHud(player2_, "P2", columnW + hudMargin, hudMargin);
    if (player3Active_) RenderPlayerHud(player3_, "P3", windowWidth_ - hudMargin - columnW * 2, hudMargin);
    if (player4Active_) RenderPlayerHud(player4_, "P4", windowWidth_ - hudMargin - columnW, hudMargin);
    DrawText(TextFormat("FPS: %d", GetFPS()), windowWidth_ - 90, 2, 12, RAYWHITE);

    const char* friendlyFireLabel = "Apagado";
    if (friendlyFireMode_ == FriendlyFireMode::Paralyze) {
        friendlyFireLabel = "Nivel 1 (paraliza)";
    } else if (friendlyFireMode_ == FriendlyFireMode::Damage) {
        friendlyFireLabel = "Nivel 2 (dana)";
    }
    const char* friendlyFireText = TextFormat("Fuego amigo (F3): %s", friendlyFireLabel);
    const int friendlyFireTextX = (windowWidth_ - MeasureText(friendlyFireText, 20)) / 2;
    DrawText(friendlyFireText, friendlyFireTextX, topBarH - 25, 20, RAYWHITE);

    EndDrawing();
}

void Game::Shutdown() {
    specialExplosionSprites_.Unload();
    bulletImpactSprites_.Unload();
    shieldSprites_.Unload();
    spawnFlashSprites_.Unload();
    UnloadTexture(starTexture_);
    UnloadTexture(helmetTexture_);
    UnloadTexture(gunTexture_);
    UnloadTexture(lifeTexture_);
    UnloadTexture(brickUnitTextures_[0]);
    UnloadTexture(brickUnitTextures_[1]);
    UnloadTexture(steelUnitTexture_);
    bulletSprites_.Unload();
    player1Sprites_.Unload();
    player2Sprites_.Unload();
    player3Sprites_.Unload();
    player4Sprites_.Unload();
    CloseWindow();
}

void Game::Run() {
    Init();

    // Limite al salto de tiempo de un frame a otro. Sin esto, el primer frame
    // despues de Init() (que puede tardar varios cientos de ms en cargar
    // texturas) se reporta como un unico salto enorme, y el bucle de paso
    // fijo "recupera" todo ese tiempo de una sola vez antes de dibujar nada:
    // animaciones cortas como el destello de aparicion terminarian antes de
    // que se vea un solo frame en pantalla.
    constexpr double kMaxFrameTime = 0.25;

    double accumulator = 0.0;
    while (!WindowShouldClose()) {
        double frameTime = GetFrameTime();
        if (frameTime > kMaxFrameTime) {
            frameTime = kMaxFrameTime;
        }
        accumulator += frameTime;

        while (accumulator >= kFixedTimestep) {
            ProcessInput();
            Update(kFixedTimestep);
            accumulator -= kFixedTimestep;
        }

        Render(accumulator / kFixedTimestep);
    }

    Shutdown();
}

} // namespace bc
