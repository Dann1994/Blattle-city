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

// Agua: alterna entre sus 2 frames para animar el oleaje.
constexpr double kWaterFrameInterval = 0.5;

// Mandos (seccion custom): boton de disparo/especial. GAMEPAD_BUTTON_LEFT_FACE_*
// (1-4) ya los usa el D-pad, asi que disparo/especial usan los botones de
// accion (RIGHT_FACE_*, 5-8). Valores de arranque, no confirmados en
// hardware real todavia: si no coinciden con "boton 3"/"boton 1" del
// jugador, ver lastGamepadButtonPressed_ (Render, al lado del FPS) para
// leer el numero real que reporta el mando y ajustar aca.
constexpr int kGamepadShootButton = GAMEPAD_BUTTON_RIGHT_FACE_DOWN;
constexpr int kGamepadSpecialButton = GAMEPAD_BUTTON_RIGHT_FACE_LEFT;
constexpr float kGamepadStickDeadzone = 0.4f;

// HUD: parpadeo de texto (calor al 100% / municion especial lista).
constexpr double kHudTextBlinkInterval = 0.3;

// Item Pala: cuanto dura el hierro temporal alrededor de la base.
constexpr double kShovelFortifyDuration = 15.0;

// Escenario rectangular (seccion custom, en prueba): el campo de juego
// (mapa rectangular, ver test_map.json) se encaja en el area que queda a la
// izquierda de una barra gris vertical a la derecha, para aprovechar todo
// el alto de la ventana. Los datos de los 4 jugadores se apilan en esa
// barra, todos a la misma distancia entre si.
constexpr float kHudPanelWidth = 250.0f;
constexpr float kHudPanelMargin = 10.0f;
constexpr float kHudRowHeight = 100.0f;  // separacion entre 2 jugadores consecutivos
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

    // Icono de la ventana: el mismo tanque P1 nivel 1 (mirando arriba) que
    // se uso para el icono del .exe (ver assets/icon/app.rc), a diferencia
    // del .exe esto se puede setear en runtime con una Image comun.
    Image windowIcon = LoadImage((std::string(BC_ASSETS_DIR) + "icon/app_icon.png").c_str());
    SetWindowIcon(windowIcon);
    UnloadImage(windowIcon);

    player1Sprites_.LoadPlayer1(BC_ASSETS_DIR);
    player2Sprites_.LoadPlayer2(BC_ASSETS_DIR);
    player3Sprites_.LoadPlayer3(BC_ASSETS_DIR);
    player4Sprites_.LoadPlayer4(BC_ASSETS_DIR);
    enemySprites_.Load(BC_ASSETS_DIR, "basic");
    fastEnemySprites_.Load(BC_ASSETS_DIR, "fast");
    bulletSprites_.Load(BC_ASSETS_DIR);
    starTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_star.png").c_str());
    SetTextureFilter(starTexture_, TEXTURE_FILTER_POINT);
    helmetTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_helmet.png").c_str());
    SetTextureFilter(helmetTexture_, TEXTURE_FILTER_POINT);
    gunTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_gun.png").c_str());
    SetTextureFilter(gunTexture_, TEXTURE_FILTER_POINT);
    lifeTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_life.png").c_str());
    SetTextureFilter(lifeTexture_, TEXTURE_FILTER_POINT);
    grenadeTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_grenade.png").c_str());
    SetTextureFilter(grenadeTexture_, TEXTURE_FILTER_POINT);
    shovelTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_shovel.png").c_str());
    SetTextureFilter(shovelTexture_, TEXTURE_FILTER_POINT);
    clockTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_clock.png").c_str());
    SetTextureFilter(clockTexture_, TEXTURE_FILTER_POINT);
    brickUnitTextures_[0] = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/brick_unit_0.png").c_str());
    brickUnitTextures_[1] = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/brick_unit_1.png").c_str());
    SetTextureFilter(brickUnitTextures_[0], TEXTURE_FILTER_POINT);
    SetTextureFilter(brickUnitTextures_[1], TEXTURE_FILTER_POINT);
    steelUnitTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/steel_unit.png").c_str());
    SetTextureFilter(steelUnitTexture_, TEXTURE_FILTER_POINT);
    treesTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/terrain_trees.png").c_str());
    SetTextureFilter(treesTexture_, TEXTURE_FILTER_POINT);
    waterTextures_[0] = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/terrain_water_0.png").c_str());
    waterTextures_[1] = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/terrain_water_1.png").c_str());
    SetTextureFilter(waterTextures_[0], TEXTURE_FILTER_POINT);
    SetTextureFilter(waterTextures_[1], TEXTURE_FILTER_POINT);
    iceTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/terrain_ice.png").c_str());
    SetTextureFilter(iceTexture_, TEXTURE_FILTER_POINT);
    baseEagleTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/base_eagle.png").c_str());
    SetTextureFilter(baseEagleTexture_, TEXTURE_FILTER_POINT);
    hudLifeIconTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/icon_life_hud.png").c_str());
    SetTextureFilter(hudLifeIconTexture_, TEXTURE_FILTER_POINT);
    stageFlagIconTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/icon_stage_flag.png").c_str());
    SetTextureFilter(stageFlagIconTexture_, TEXTURE_FILTER_POINT);
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

    basePositionX_ = level.base_position[0];
    basePositionY_ = level.base_position[1];
    fortifiedCells_.clear();
    baseFortifyTimer_ = 0.0;
    gameOver_ = false;

    // Anillo de proteccion de la base (ver test_map.json: ladrillo arriba
    // -izquierda/arriba-centro/arriba-derecha, pegado directo a la base, y a
    // los costados): se recorta a 2 unidades de espesor, pegadas al lado que
    // da hacia la base.
    ThinBrickCellVertical(basePositionX_ - 1, basePositionY_ - 1, true);
    ThinBrickCellVertical(basePositionX_, basePositionY_ - 1, true);
    ThinBrickCellVertical(basePositionX_ + 1, basePositionY_ - 1, true);
    ThinBrickCellHorizontal(basePositionX_ - 1, basePositionY_, true);
    ThinBrickCellHorizontal(basePositionX_ + 1, basePositionY_, false);

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

    // Primer enemigo (seccion 5, prueba): arranca en el primer punto de
    // spawn de enemigos del nivel. El resto se agregan a mano con F10
    // mientras no haya oleadas (eso llega mas adelante en la Fase 3).
    enemies_ = EnemySystem{};
    enemySpawnPositions_ = level.enemy_spawns;
    if (!enemySpawnPositions_.empty()) {
        enemies_.SpawnAt(static_cast<float>(enemySpawnPositions_[0][0]), static_cast<float>(enemySpawnPositions_[0][1]));
    }
    // Tanque "Rapido" (prueba): no aparece solo, se agrega a mano con
    // Shift+F10 mientras no haya oleadas de verdad.
    fastEnemies_ = FastEnemySystem{};

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

void Game::ThinBrickCellVertical(int x, int y, bool keepBottomHalf) {
    if (!map_.InBounds(x, y)) {
        return;
    }
    Cell& cell = map_.At(x, y);
    if (cell.type != TileType::Brick) {
        return;
    }
    for (int r = 0; r < kBrickGridSize; ++r) {
        const bool keep = keepBottomHalf ? (r >= kBrickGridSize / 2) : (r < kBrickGridSize / 2);
        if (keep) {
            continue;
        }
        for (int c = 0; c < kBrickGridSize; ++c) {
            cell.brickUnits[r * kBrickGridSize + c].alive = false;
        }
    }
}

void Game::ThinBrickCellHorizontal(int x, int y, bool keepRightHalf) {
    if (!map_.InBounds(x, y)) {
        return;
    }
    Cell& cell = map_.At(x, y);
    if (cell.type != TileType::Brick) {
        return;
    }
    for (int c = 0; c < kBrickGridSize; ++c) {
        const bool keep = keepRightHalf ? (c >= kBrickGridSize / 2) : (c < kBrickGridSize / 2);
        if (keep) {
            continue;
        }
        for (int r = 0; r < kBrickGridSize; ++r) {
            cell.brickUnits[r * kBrickGridSize + c].alive = false;
        }
    }
}

void Game::ApplyShovelFortification() {
    if (basePositionX_ < 0 || basePositionY_ < 0) {
        return;
    }
    fortifiedCells_.clear();
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue; // la base misma, no una celda a fortificar
            }
            const int x = basePositionX_ + dx;
            const int y = basePositionY_ + dy;
            if (!map_.InBounds(x, y)) {
                continue;
            }
            Cell& cell = map_.At(x, y);
            if (cell.type == TileType::Steel) {
                continue; // ya es hierro, no hace falta tocarla ni recordarla
            }
            fortifiedCells_.push_back(FortifiedCell{x, y, cell.type});
            cell.type = TileType::Steel;
            for (SteelUnit& unit : cell.steelUnits) {
                unit.alive = true;
                unit.hp = kSteelUnitMaxHp;
            }
        }
    }
    baseFortifyTimer_ = kShovelFortifyDuration;
}

void Game::TickBaseFortification(double dt) {
    if (baseFortifyTimer_ <= 0.0) {
        return;
    }
    baseFortifyTimer_ -= dt;
    if (baseFortifyTimer_ > 0.0) {
        return;
    }
    for (const FortifiedCell& fc : fortifiedCells_) {
        Cell& cell = map_.At(fc.x, fc.y);
        cell.type = fc.originalType;
        if (fc.originalType == TileType::Brick) {
            for (int r = 0; r < kBrickGridSize; ++r) {
                for (int c = 0; c < kBrickGridSize; ++c) {
                    BrickUnit& unit = cell.brickUnits[r * kBrickGridSize + c];
                    unit.alive = true;
                    unit.frame = BrickUnit::FrameFor(r, c);
                }
            }
        }
    }
    fortifiedCells_.clear();
}

void Game::ApplyGamepadInput(PlayerInput& input, int gamepadId) const {
    if (gamepadId < 0 || !IsGamepadAvailable(gamepadId)) {
        return;
    }
    const float axisX = GetGamepadAxisMovement(gamepadId, GAMEPAD_AXIS_LEFT_X);
    const float axisY = GetGamepadAxisMovement(gamepadId, GAMEPAD_AXIS_LEFT_Y);
    input.moveUp = input.moveUp || IsGamepadButtonDown(gamepadId, GAMEPAD_BUTTON_LEFT_FACE_UP) || axisY < -kGamepadStickDeadzone;
    input.moveDown = input.moveDown || IsGamepadButtonDown(gamepadId, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || axisY > kGamepadStickDeadzone;
    input.moveLeft = input.moveLeft || IsGamepadButtonDown(gamepadId, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || axisX < -kGamepadStickDeadzone;
    input.moveRight = input.moveRight || IsGamepadButtonDown(gamepadId, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || axisX > kGamepadStickDeadzone;
    input.shoot = input.shoot || IsGamepadButtonDown(gamepadId, kGamepadShootButton);
    input.specialShoot = input.specialShoot || IsGamepadButtonDown(gamepadId, kGamepadSpecialButton);
}

void Game::ProcessInput() {
    // Esquema de teclado WASD para P1 (seccion 9).
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

    // Mandos (seccion custom): se suman al teclado de cada jugador (no lo
    // reemplazan, cualquiera de los dos mueve/dispara). El primer mando que
    // este conectado controla a P1, el segundo a P2, el tercero a P3 y el
    // cuarto a P4 (ver ApplyGamepadInput).
    int connectedGamepads[4] = {-1, -1, -1, -1};
    int connectedCount = 0;
    for (int i = 0; i < 4 && connectedCount < 4; ++i) {
        if (IsGamepadAvailable(i)) {
            connectedGamepads[connectedCount] = i;
            ++connectedCount;
        }
    }
    ApplyGamepadInput(input1_, connectedGamepads[0]);
    ApplyGamepadInput(input2_, connectedGamepads[1]);
    ApplyGamepadInput(input3_, connectedGamepads[2]);
    ApplyGamepadInput(input4_, connectedGamepads[3]);

    // Debug: que numero de boton reporta el mando al apretar cada cosa, para
    // calibrar kGamepadShootButton/kGamepadSpecialButton en ApplyGamepadInput
    // (se muestra en el HUD, ver Render). Se guarda el ultimo apretado en
    // cualquier mando, no se pierde hasta el siguiente boton.
    const int pressedButton = GetGamepadButtonPressed();
    if (pressedButton != -1) {
        lastGamepadButtonPressed_ = pressedButton;
    }

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
        powerUps_.ForceSpawn(PowerUpType::Star, map_, ActiveTankBounds());
    }
    if (IsKeyPressed(KEY_F2)) {
        powerUps_.ForceSpawn(PowerUpType::Helmet, map_, ActiveTankBounds());
    }
    if (IsKeyPressed(KEY_F4)) {
        powerUps_.ForceSpawn(PowerUpType::Gun, map_, ActiveTankBounds());
    }
    if (IsKeyPressed(KEY_F5)) {
        powerUps_.ForceSpawn(PowerUpType::Life, map_, ActiveTankBounds());
    }
    if (IsKeyPressed(KEY_F7)) {
        powerUps_.ForceSpawn(PowerUpType::Grenade, map_, ActiveTankBounds());
    }
    if (IsKeyPressed(KEY_F8)) {
        powerUps_.ForceSpawn(PowerUpType::Shovel, map_, ActiveTankBounds());
    }
    if (IsKeyPressed(KEY_F9)) {
        powerUps_.ForceSpawn(PowerUpType::Clock, map_, ActiveTankBounds());
    }

    // Boton de prueba: agrega otro enemigo "Basico" (rotando entre los
    // puntos de spawn del nivel), mientras no haya oleadas de verdad
    // todavia. Shift+F10 agrega uno "Rapido" en su lugar.
    if (IsKeyPressed(KEY_F10) && !enemySpawnPositions_.empty()) {
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
            const size_t spawnIndex = fastEnemies_.Enemies().size() % enemySpawnPositions_.size();
            const std::array<int, 2>& pos = enemySpawnPositions_[spawnIndex];
            fastEnemies_.SpawnAt(static_cast<float>(pos[0]), static_cast<float>(pos[1]));
        } else {
            const size_t spawnIndex = enemies_.Enemies().size() % enemySpawnPositions_.size();
            const std::array<int, 2>& pos = enemySpawnPositions_[spawnIndex];
            enemies_.SpawnAt(static_cast<float>(pos[0]), static_cast<float>(pos[1]));
        }
    }

    // Botones de prueba: bajan/suben el nivel de agresividad de los
    // enemigos (1 a 5, 3 es el comportamiento de base), para los 2 tipos a
    // la vez. Ver EnemySystem::SetAggressivenessLevel.
    if (IsKeyPressed(KEY_F11)) {
        enemies_.SetAggressivenessLevel(enemies_.AggressivenessLevel() - 1);
        fastEnemies_.SetAggressivenessLevel(fastEnemies_.AggressivenessLevel() - 1);
    }
    if (IsKeyPressed(KEY_F12)) {
        enemies_.SetAggressivenessLevel(enemies_.AggressivenessLevel() + 1);
        fastEnemies_.SetAggressivenessLevel(fastEnemies_.AggressivenessLevel() + 1);
    }

    // Boton de prueba: rota el modo de fuego amigo Off -> nivel 1 -> nivel 2 -> Off.
    if (IsKeyPressed(KEY_F3)) {
        switch (friendlyFireMode_) {
            case FriendlyFireMode::Off:      friendlyFireMode_ = FriendlyFireMode::Paralyze; break;
            case FriendlyFireMode::Paralyze: friendlyFireMode_ = FriendlyFireMode::Damage;    break;
            case FriendlyFireMode::Damage:   friendlyFireMode_ = FriendlyFireMode::Off;       break;
        }
    }

    // Boton de prueba: muestra/oculta la grilla de referencia sobre el mapa
    // (ver Render), para ubicar bloques al diseñar el escenario.
    if (IsKeyPressed(KEY_F6)) {
        showGrid_ = !showGrid_;
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
        } else if (pickedType == PowerUpType::Shovel) {
            ApplyShovelFortification();
        }
        // Granada y Reloj (seccion custom): en el juego real le pegan a los
        // enemigos (destruirlos todos / paralizarlos), pero todavia no hay
        // enemigos (Fase 3). Por ahora se pueden agarrar (entran en la
        // rotacion de probabilidades) pero no hacen nada.
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
    others.reserve(3 + enemies_.Enemies().size() + fastEnemies_.Enemies().size());
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
    // Los jugadores tambien chocan contra los tanques enemigo, de cualquier
    // tipo (antes los atravesaban: solo los enemigos chequeaban colision
    // contra ellos).
    for (Enemy& enemy : enemies_.Enemies()) {
        if (enemy.alive) {
            others.push_back(&enemy.tank);
        }
    }
    for (Enemy& enemy : fastEnemies_.Enemies()) {
        if (enemy.alive) {
            others.push_back(&enemy.tank);
        }
    }
    return others;
}

std::vector<TankOccupiedBounds> Game::ActiveTankBounds() const {
    std::vector<TankOccupiedBounds> bounds;
    bounds.reserve(4);
    const Tank* activeTanks[4] = {
        player1Active_ ? &player1_ : nullptr,
        player2Active_ ? &player2_ : nullptr,
        player3Active_ ? &player3_ : nullptr,
        player4Active_ ? &player4_ : nullptr,
    };
    for (const Tank* tank : activeTanks) {
        if (tank == nullptr || tank->IsEliminated()) {
            continue;
        }
        TankOccupiedBounds b;
        tank->GetBounds(b.left, b.right, b.top, b.bottom);
        bounds.push_back(b);
    }
    return bounds;
}

std::vector<Tank*> Game::ActivePlayerTanks() {
    std::vector<Tank*> tanks;
    tanks.reserve(4);
    Tank* activeTanks[4] = {
        player1Active_ ? &player1_ : nullptr,
        player2Active_ ? &player2_ : nullptr,
        player3Active_ ? &player3_ : nullptr,
        player4Active_ ? &player4_ : nullptr,
    };
    for (Tank* tank : activeTanks) {
        if (tank == nullptr || tank->IsEliminated()) {
            continue;
        }
        tanks.push_back(tank);
    }
    return tanks;
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
    if (gameOver_) {
        return; // el aguila fue destruida: se congela todo hasta reiniciar (ESC)
    }

    if (screenShakeTimer_ > 0.0) {
        screenShakeTimer_ = std::max(0.0, screenShakeTimer_ - fixedDt);
    }
    TickBaseFortification(fixedDt);

    if (player1Active_) UpdatePlayer(player1_, input1_, player1Spawn_, kPlayer1Id, ActiveOthers(kPlayer1Id), fixedDt);
    if (player2Active_) UpdatePlayer(player2_, input2_, player2Spawn_, kPlayer2Id, ActiveOthers(kPlayer2Id), fixedDt);
    if (player3Active_) UpdatePlayer(player3_, input3_, player3Spawn_, kPlayer3Id, ActiveOthers(kPlayer3Id), fixedDt);
    if (player4Active_) UpdatePlayer(player4_, input4_, player4Spawn_, kPlayer4Id, ActiveOthers(kPlayer4Id), fixedDt);

    // Cada sistema de enemigos recibe al otro como obstaculo de colision
    // (nunca como blanco de combate: eso solo aplica a jugadores reales).
    std::vector<Tank*> basicEnemyTanks;
    basicEnemyTanks.reserve(enemies_.Enemies().size());
    for (Enemy& enemy : enemies_.Enemies()) {
        if (enemy.alive) {
            basicEnemyTanks.push_back(&enemy.tank);
        }
    }
    std::vector<Tank*> fastEnemyTanks;
    fastEnemyTanks.reserve(fastEnemies_.Enemies().size());
    for (Enemy& enemy : fastEnemies_.Enemies()) {
        if (enemy.alive) {
            fastEnemyTanks.push_back(&enemy.tank);
        }
    }
    enemies_.Update(fixedDt, map_, bullets_, bulletImpacts_, specialExplosions_, ActivePlayerTanks(), fastEnemyTanks, static_cast<float>(basePositionX_), static_cast<float>(basePositionY_));
    fastEnemies_.Update(fixedDt, map_, bullets_, bulletImpacts_, specialExplosions_, ActivePlayerTanks(), basicEnemyTanks, static_cast<float>(basePositionX_), static_cast<float>(basePositionY_));

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

    // El aguila (TileType::Base) se destruye de un impacto (ver BulletSystem
    // y TriggerSpecialExplosion): si la celda dejo de ser Base, se acabo la
    // partida.
    if (basePositionX_ >= 0 && map_.InBounds(basePositionX_, basePositionY_) &&
        map_.At(basePositionX_, basePositionY_).type != TileType::Base) {
        gameOver_ = true;
    }

    bulletImpacts_.Update(fixedDt);
    specialExplosions_.Update(fixedDt);
    powerUps_.Update(fixedDt, map_, ActiveTankBounds());

    for (const ActivePlayerRef& ref : activePlayers) {
        if (!ref.active) {
            continue;
        }
        if (ApplyFriendlyFire(*ref.tank, ref.ownerId)) {
            HandlePlayerDeath(*ref.tank, ref.ownerId);
        }
    }

    // Balas enemigas contra jugadores: siempre hacen dano (no depende del
    // modo de fuego amigo, que es solo entre jugadores). Mismo efecto que
    // recibir un disparo nivel 1 de otro jugador (ver ProcessFriendlyFireDamageHit).
    for (const ActivePlayerRef& ref : activePlayers) {
        if (!ref.active || ref.tank->IsEliminated()) {
            continue;
        }
        float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
        ref.tank->GetBounds(left, right, top, bottom);
        if (bullets_.KillEnemyBulletsHittingBox(left, right, top, bottom, bulletImpacts_)) {
            if (ProcessFriendlyFireDamageHit(*ref.tank, 1)) {
                HandlePlayerDeath(*ref.tank, ref.ownerId);
            }
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
        // tanques presentes, jugadores y enemigos por igual.
        screenShakeTimer_ = kScreenShakeDuration;
        for (const ActivePlayerRef& ref : activePlayers) {
            if (ref.active) {
                ref.tank->Freeze(kSpecialImpactFreezeDuration);
            }
        }
        for (Enemy& enemy : enemies_.Enemies()) {
            if (enemy.alive) {
                enemy.tank.Freeze(kSpecialImpactFreezeDuration);
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

void Game::RenderEnemy(const Enemy& enemy, const EnemySprites& sprites, const MapViewport& viewport) {
    if (!enemy.alive) {
        return;
    }

    if (enemy.spawn.IsActive()) {
        // Mismo destello de aparicion que los jugadores (ver RenderTank).
        const Texture2D flashTex = spawnFlashSprites_.Get(enemy.spawn.FrameIndex());
        const Rectangle flashSrc{0.0f, 0.0f, static_cast<float>(flashTex.width), static_cast<float>(flashTex.height)};
        const Rectangle flashDst{viewport.TileToScreenX(enemy.spawn.X()), viewport.TileToScreenY(enemy.spawn.Y()), viewport.tileScreenSize, viewport.tileScreenSize};
        DrawTexturePro(flashTex, flashSrc, flashDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        return;
    }

    // Paralizado (onda del disparo especial, igual que los jugadores):
    // parpadea en vez de dibujarse fijo (ver kFrozenBlinkInterval).
    const bool blinkedOut = enemy.tank.IsFrozen() && (static_cast<int>(GetTime() / kFrozenBlinkInterval) % 2 == 0);
    if (blinkedOut) {
        return;
    }

    // Sprite propio del tipo de enemigo (fila 5 = Basico, fila 6 = Rapido de
    // Tanques.png, paleta gris/teal de P3) — no el del Jugador 3.
    const Texture2D enemyTex = sprites.Get(enemy.tank.Facing(), enemy.tank.AnimFrame());
    const Rectangle src{0.0f, 0.0f, static_cast<float>(enemyTex.width), static_cast<float>(enemyTex.height)};
    const Rectangle dst{viewport.TileToScreenX(enemy.tank.X()), viewport.TileToScreenY(enemy.tank.Y()), viewport.tileScreenSize, viewport.tileScreenSize};
    DrawTexturePro(enemyTex, src, dst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);

    // Debug (temporal, para diagnosticar si se traba): modo de movimiento
    // actual (ver Enemy::debugMode) + cuantos frames seguidos lleva sin
    // poder moverse, en cuanto pasa medio segundo.
    if (enemy.stuckFrames > 30) {
        const char* label = TextFormat("%c %.1fs", enemy.debugMode, enemy.stuckFrames / 60.0f);
        DrawText(label, static_cast<int>(dst.x), static_cast<int>(dst.y) - 12, 10, RED);
    }
}

void Game::RenderPlayerHud(const Tank& tank, const char* label, int centerX, int y) {
    // Parpadeo de texto (calor al 100% / municion especial lista): mismo
    // criterio de "visible la mitad del tiempo" que ya se usa para el tanque
    // paralizado (ver kFrozenBlinkInterval), pero un poco mas lento porque es
    // texto de HUD, no el sprite del tanque.
    const bool textBlinkOn = static_cast<int>(GetTime() / kHudTextBlinkInterval) % 2 == 0;

    // Todo el bloque se alinea al centro: cada linea (y la barra de
    // temperatura junto con su texto) se centra en centerX, en vez de
    // arrancar de un borde fijo.
    if (tank.IsEliminated()) {
        const char* text = TextFormat("%s - SIN VIDAS", label);
        DrawText(text, centerX - MeasureText(text, 20) / 2, y, 20, RED);
        return;
    }

    // "P1 [icono] 3 - Nivel N": el icono (Tanques.png, junto a "1P") va en
    // vez de una "X" entre la etiqueta y el numero de vidas.
    const char* labelText = TextFormat("%s ", label);
    const int labelWidth = MeasureText(labelText, 20);
    constexpr int kLifeIconDisplayHeight = 18;
    const int lifeIconDisplayWidth = (hudLifeIconTexture_.width * kLifeIconDisplayHeight) / hudLifeIconTexture_.height;
    const char* numberText = TextFormat("%d - Nivel %d", tank.Lives(), tank.WeaponLevel());
    const int numberWidth = MeasureText(numberText, 20);
    const int headerWidth = labelWidth + lifeIconDisplayWidth + numberWidth;
    const int headerX = centerX - headerWidth / 2;

    DrawText(labelText, headerX, y, 20, RAYWHITE);
    const int lifeIconX = headerX + labelWidth;
    const int lifeIconY = y + (20 - kLifeIconDisplayHeight) / 2;
    const Rectangle lifeIconSrc{0.0f, 0.0f, static_cast<float>(hudLifeIconTexture_.width), static_cast<float>(hudLifeIconTexture_.height)};
    const Rectangle lifeIconDst{static_cast<float>(lifeIconX), static_cast<float>(lifeIconY), static_cast<float>(lifeIconDisplayWidth), static_cast<float>(kLifeIconDisplayHeight)};
    DrawTexturePro(hudLifeIconTexture_, lifeIconSrc, lifeIconDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    DrawText(numberText, lifeIconX + lifeIconDisplayWidth, y, 20, RAYWHITE);

    constexpr int kHeatBarWidth = 160;
    constexpr int kHeatBarHeight = 14;
    constexpr int kBarTextGap = 6;
    constexpr int kLinePitch = 20; // alto de renglon + separacion minima entre lineas de texto
    const int heatBarY = y + kLinePitch;
    const float heatPercent = tank.HeatPercent();
    const bool overheated = heatPercent >= 100.0f;
    const char* percentText = TextFormat(":%.0f%%", heatPercent);
    const int percentWidth = MeasureText(percentText, 18);
    const int barX = centerX - (kHeatBarWidth + kBarTextGap + percentWidth) / 2;
    DrawRectangle(barX, heatBarY, kHeatBarWidth, kHeatBarHeight, DARKGRAY);
    const int filledWidth = static_cast<int>(kHeatBarWidth * (heatPercent / 100.0f));
    DrawRectangle(barX, heatBarY, filledWidth, kHeatBarHeight, overheated ? RED : ORANGE);
    DrawRectangleLines(barX, heatBarY, kHeatBarWidth, kHeatBarHeight, RAYWHITE);

    // Porcentaje a la derecha de la barra; al llegar a sobrecalentamiento
    // (100%) se pone en rojo.
    DrawText(percentText, barX + kHeatBarWidth + kBarTextGap, heatBarY - 2, 18, overheated ? RED : RAYWHITE);

    int nextY = heatBarY + kHeatBarHeight + 4;
    if (overheated && textBlinkOn) {
        const char* warningText = "Sobrecalentamiento";
        DrawText(warningText, centerX - MeasureText(warningText, 18) / 2, nextY, 18, RED);
    }
    if (overheated) {
        nextY += kLinePitch;
    }

    if (tank.HasSpecialShotReady() && textBlinkOn) {
        const char* specialText = "Municion especial lista!";
        DrawText(specialText, centerX - MeasureText(specialText, 18) / 2, nextY, 18, RED);
    }
    if (tank.HasSpecialShotReady()) {
        nextY += kLinePitch;
    }

    if (tank.IsFrozen() && textBlinkOn) {
        const char* frozenText = "Paralizado";
        DrawText(frozenText, centerX - MeasureText(frozenText, 18) / 2, nextY, 18, YELLOW);
    }
}

void Game::Render(double /*interpolationAlpha*/) {
    windowWidth_ = GetScreenWidth();
    windowHeight_ = GetScreenHeight();

    // El campo de juego (rectangular, ver test_map.json) se encaja en el area
    // que queda entre la barra gris de la izquierda (solo de marco, sin
    // datos, del ancho exacto de una celda) y la barra gris de la derecha
    // (datos de jugadores), usando todo el alto de la ventana.
    const int panelW = static_cast<int>(kHudPanelWidth);
    const float playAreaHeight = static_cast<float>(windowHeight_);

    // Primer calculo (sin descontar la barra izquierda todavia) solo para
    // saber cuanto mide una celda en pantalla, y asi la barra izquierda
    // pueda tener exactamente ese ancho.
    const float prelimPlayAreaWidth = static_cast<float>(windowWidth_) - kHudPanelWidth;
    const MapViewport prelimViewport = MapViewport::Compute(static_cast<int>(prelimPlayAreaWidth), static_cast<int>(playAreaHeight), map_.Width(), map_.Height(), kTileSize);
    const int leftBarW = static_cast<int>(prelimViewport.tileScreenSize);

    const float playAreaWidth = prelimPlayAreaWidth - static_cast<float>(leftBarW);
    MapViewport viewport = MapViewport::Compute(static_cast<int>(playAreaWidth), static_cast<int>(playAreaHeight), map_.Width(), map_.Height(), kTileSize);
    viewport.offsetX += static_cast<float>(leftBarW);

    BeginDrawing();
    // Mismo color que las celdas vacias (ColorForTile de Empty), asi una
    // unidad de ladrillo destruida no deja un negro puro que desentona con
    // el resto del fondo.
    ClearBackground(ColorForTile(TileType::Empty));

    // El mapa (29x13) no siempre llena exacto el area a la izquierda de la
    // barra: al mantener celdas cuadradas (ver MapViewport::Compute), sobra
    // un margen angosto arriba/abajo segun la relacion de aspecto de la
    // ventana. Se pinta gris (en vez de dejarlo del mismo color que una
    // celda vacia) para que se note que ahi termina el campo de juego, en
    // vez de que el tanque se frene contra un borde invisible. Importante:
    // solo se pinta gris ESTE margen (y las barras de los costados), nunca
    // el area del mapa en si — si se pintara toda la ventana de gris antes
    // de dibujar el mapa, cualquier hueco de una unidad de ladrillo
    // destruida (que no dibuja nada ahi) dejaria ver ese gris de fondo en
    // vez del negro de "vacio".
    const float mapPixelHeight = viewport.tileScreenSize * static_cast<float>(map_.Height());
    DrawRectangle(0, 0, leftBarW, windowHeight_, kHudPanelColor);
    DrawRectangle(windowWidth_ - panelW, 0, panelW, windowHeight_, kHudPanelColor);
    DrawRectangle(leftBarW, 0, windowWidth_ - panelW - leftBarW, static_cast<int>(viewport.offsetY), kHudPanelColor);
    DrawRectangle(leftBarW, static_cast<int>(viewport.offsetY + mapPixelHeight), windowWidth_ - panelW - leftBarW, windowHeight_ - static_cast<int>(viewport.offsetY + mapPixelHeight), kHudPanelColor);

    // Barras grises verticales fijas, fuera de la sacudida de pantalla para
    // que no salten: izquierda (marco) y derecha (datos de jugadores).
    DrawLine(leftBarW, 0, leftBarW, windowHeight_, kHudPanelBorderColor);
    DrawLine(windowWidth_ - panelW, 0, windowWidth_ - panelW, windowHeight_, kHudPanelBorderColor);

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
    const int waterFrame = static_cast<int>(GetTime() / kWaterFrameInterval) % 2;
    const Texture2D waterTex = waterTextures_[waterFrame];
    const Rectangle waterSrc{0.0f, 0.0f, static_cast<float>(waterTex.width), static_cast<float>(waterTex.height)};

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
            } else if (cell.type == TileType::Water) {
                // 2 frames que alternan (ver kWaterFrameInterval) para
                // animar el oleaje.
                const Rectangle waterDst{screenX, screenY, viewport.tileScreenSize, viewport.tileScreenSize};
                DrawTexturePro(waterTex, waterSrc, waterDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
            } else if (cell.type == TileType::Ice) {
                const Rectangle iceSrc{0.0f, 0.0f, static_cast<float>(iceTexture_.width), static_cast<float>(iceTexture_.height)};
                const Rectangle iceDst{screenX, screenY, viewport.tileScreenSize, viewport.tileScreenSize};
                DrawTexturePro(iceTexture_, iceSrc, iceDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
            } else if (cell.type == TileType::Base) {
                // El aguila: objeto a defender (ver Game::Update, un impacto
                // la destruye y termina la partida).
                const Rectangle eagleSrc{0.0f, 0.0f, static_cast<float>(baseEagleTexture_.width), static_cast<float>(baseEagleTexture_.height)};
                const Rectangle eagleDst{screenX, screenY, viewport.tileScreenSize, viewport.tileScreenSize};
                DrawTexturePro(baseEagleTexture_, eagleSrc, eagleDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
            } else if (cell.type != TileType::Trees) {
                // El arbusto no se dibuja aca: es un bloque entero (no por
                // unidad) que se pinta mas abajo, encima de tanques y balas,
                // para que los tape (ver el segundo recorrido del mapa).
                const Rectangle rect{screenX, screenY, viewport.tileScreenSize, viewport.tileScreenSize};
                DrawRectangleRec(rect, ColorForTile(cell.type));
            }
        }
    }

    if (player1Active_) RenderTank(player1_, player1Spawn_, player1Sprites_, viewport);
    if (player2Active_) RenderTank(player2_, player2Spawn_, player2Sprites_, viewport);
    if (player3Active_) RenderTank(player3_, player3Spawn_, player3Sprites_, viewport);
    if (player4Active_) RenderTank(player4_, player4Spawn_, player4Sprites_, viewport);

    for (const Enemy& enemy : enemies_.Enemies()) {
        RenderEnemy(enemy, enemySprites_, viewport);
    }
    for (const Enemy& enemy : fastEnemies_.Enemies()) {
        RenderEnemy(enemy, fastEnemySprites_, viewport);
    }

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

    // El arbusto tapa tanques, balas y explosiones que esten debajo, igual
    // que en el juego original. No bloquea movimiento ni disparos (ver
    // TileBlocksMovement/TileBlocksShots), solo los oculta.
    const Rectangle treesSrc{0.0f, 0.0f, static_cast<float>(treesTexture_.width), static_cast<float>(treesTexture_.height)};
    for (int y = 0; y < map_.Height(); ++y) {
        for (int x = 0; x < map_.Width(); ++x) {
            if (map_.At(x, y).type != TileType::Trees) {
                continue;
            }
            const Rectangle treesDst{viewport.TileToScreenX(x), viewport.TileToScreenY(y), viewport.tileScreenSize, viewport.tileScreenSize};
            DrawTexturePro(treesTexture_, treesSrc, treesDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        }
    }

    // Los power-ups van al final: eje Z mas alto que todo lo demas del campo
    // de juego (tanques, balas, explosiones y hasta el arbusto), para que
    // nunca queden tapados.
    if (powerUps_.Active().alive && powerUps_.IsBlinkVisible()) {
        Texture2D iconTex = starTexture_;
        if (powerUps_.Active().type == PowerUpType::Helmet) {
            iconTex = helmetTexture_;
        } else if (powerUps_.Active().type == PowerUpType::Gun) {
            iconTex = gunTexture_;
        } else if (powerUps_.Active().type == PowerUpType::Life) {
            iconTex = lifeTexture_;
        } else if (powerUps_.Active().type == PowerUpType::Grenade) {
            iconTex = grenadeTexture_;
        } else if (powerUps_.Active().type == PowerUpType::Shovel) {
            iconTex = shovelTexture_;
        } else if (powerUps_.Active().type == PowerUpType::Clock) {
            iconTex = clockTexture_;
        }
        const Rectangle iconSrc{0.0f, 0.0f, static_cast<float>(iconTex.width), static_cast<float>(iconTex.height)};
        const Rectangle iconDst{viewport.TileToScreenX(powerUps_.Active().x), viewport.TileToScreenY(powerUps_.Active().y), viewport.tileScreenSize, viewport.tileScreenSize};
        DrawTexturePro(iconTex, iconSrc, iconDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    // Grilla de referencia (boton de prueba F6): una linea por cada borde de
    // celda, mas marcada cada 5 celdas (estilo hoja cuadriculada) para poder
    // contar de un vistazo cuantos bloques entran al diseñar el escenario.
    if (showGrid_) {
        const float gridRight = viewport.TileToScreenX(static_cast<float>(map_.Width()));
        const float gridBottom = viewport.TileToScreenY(static_cast<float>(map_.Height()));
        for (int x = 0; x <= map_.Width(); ++x) {
            const int lineX = static_cast<int>(viewport.TileToScreenX(static_cast<float>(x)));
            const Color lineColor = (x % 5 == 0) ? Color{255, 255, 255, 120} : Color{255, 255, 255, 40};
            DrawLine(lineX, static_cast<int>(viewport.offsetY), lineX, static_cast<int>(gridBottom), lineColor);
        }
        for (int y = 0; y <= map_.Height(); ++y) {
            const int lineY = static_cast<int>(viewport.TileToScreenY(static_cast<float>(y)));
            const Color lineColor = (y % 5 == 0) ? Color{255, 255, 255, 120} : Color{255, 255, 255, 40};
            DrawLine(static_cast<int>(viewport.offsetX), lineY, static_cast<int>(gridRight), lineY, lineColor);
        }
    }

    EndMode2D();

    // Texto de UI (barra vertical + aviso de fuego amigo): dibujado fuera de
    // la sacudida de pantalla, para que quede fijo mientras el campo de
    // juego tiembla. Apilados abajo a la izquierda de la barra, los 4 a la
    // misma distancia entre si, pegados al borde inferior en vez de arrancar
    // desde arriba.
    const int hudMargin = static_cast<int>(kHudPanelMargin);
    const int panelRightX = windowWidth_ - hudMargin;
    const int panelLeftX = windowWidth_ - panelW + hudMargin;
    const int rowH = static_cast<int>(kHudRowHeight);
    const int blockH = 115; // alto aproximado del contenido de un jugador (nivel, barra+%, sobrecalentado, especial, paralizado)

    // Icono de bandera "STAGE" (Tanques.png) arriba de la barra derecha,
    // alineado a la izquierda del panel (igual que los datos de jugadores),
    // con el numero de nivel debajo. Todavia no hay progresion de niveles
    // (Fase 3+), asi que por ahora siempre muestra 0 (nivel de prueba).
    {
        constexpr int kStageIconDisplayWidth = 32;
        const int stageIconH = (stageFlagIconTexture_.height * kStageIconDisplayWidth) / stageFlagIconTexture_.width;
        constexpr int stageIconY = 6;
        const Rectangle stageIconSrc{0.0f, 0.0f, static_cast<float>(stageFlagIconTexture_.width), static_cast<float>(stageFlagIconTexture_.height)};
        const Rectangle stageIconDst{static_cast<float>(panelLeftX), static_cast<float>(stageIconY), static_cast<float>(kStageIconDisplayWidth), static_cast<float>(stageIconH)};
        DrawTexturePro(stageFlagIconTexture_, stageIconSrc, stageIconDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        const int stageNumberY = stageIconY + stageIconH + 4;
        DrawText("0", panelLeftX, stageNumberY, 20, RAYWHITE);

        // Cuantos enemigos faltan eliminar para terminar el nivel. Todavia
        // no hay enemigos (Fase 3), asi que por ahora siempre muestra 0.
        DrawText("Enemigos: 0", panelLeftX, stageNumberY + 40, 20, RAYWHITE);

        // Debug (temporal): nivel de agresividad actual (F11/F12 para
        // bajar/subir, ver EnemySystem::SetAggressivenessLevel).
        const char* aggroLabel = TextFormat("Agresividad: %d", enemies_.AggressivenessLevel());
        DrawText(aggroLabel, panelLeftX, stageNumberY + 70, 16, YELLOW);
    }
    const int panelCenterX = windowWidth_ - panelW / 2;
    const int p4Y = windowHeight_ - hudMargin - blockH;
    const int p3Y = p4Y - rowH;
    const int p2Y = p3Y - rowH;
    const int p1Y = p2Y - rowH;
    if (player1Active_) RenderPlayerHud(player1_, "P1", panelCenterX, p1Y); else DrawText("P1 - Pulse iniciar", panelCenterX - MeasureText("P1 - Pulse iniciar", 20) / 2, p1Y, 20, RED);
    if (player2Active_) RenderPlayerHud(player2_, "P2", panelCenterX, p2Y); else DrawText("P2 - Pulse iniciar", panelCenterX - MeasureText("P2 - Pulse iniciar", 20) / 2, p2Y, 20, RED);
    if (player3Active_) RenderPlayerHud(player3_, "P3", panelCenterX, p3Y); else DrawText("P3 - Pulse iniciar", panelCenterX - MeasureText("P3 - Pulse iniciar", 20) / 2, p3Y, 20, RED);
    if (player4Active_) RenderPlayerHud(player4_, "P4", panelCenterX, p4Y); else DrawText("P4 - Pulse iniciar", panelCenterX - MeasureText("P4 - Pulse iniciar", 20) / 2, p4Y, 20, RED);
    const char* fpsText = TextFormat("FPS: %d", GetFPS());
    DrawText(fpsText, panelRightX - MeasureText(fpsText, 12), hudMargin, 12, RAYWHITE);

    // Debug: numero de boton del mando (ver ApplyGamepadInput / lastGamepadButtonPressed_).
    if (lastGamepadButtonPressed_ != -1) {
        const char* gamepadBtnText = TextFormat("Mando boton: %d", lastGamepadButtonPressed_);
        DrawText(gamepadBtnText, panelRightX - MeasureText(gamepadBtnText, 12), hudMargin + 14, 12, YELLOW);
    }

    const char* friendlyFireLabel = "Apagado";
    if (friendlyFireMode_ == FriendlyFireMode::Paralyze) {
        friendlyFireLabel = "Nivel 1 (paraliza)";
    } else if (friendlyFireMode_ == FriendlyFireMode::Damage) {
        friendlyFireLabel = "Nivel 2 (dana)";
    }
    const char* friendlyFireText = TextFormat("Fuego amigo (F3): %s", friendlyFireLabel);
    const int friendlyFireTextX = leftBarW + (static_cast<int>(playAreaWidth) - MeasureText(friendlyFireText, 20)) / 2;
    DrawText(friendlyFireText, friendlyFireTextX, 10, 20, RAYWHITE);

    // El aguila fue destruida: partida terminada (ver Update), congelado
    // hasta reiniciar con ESC.
    if (gameOver_) {
        const char* gameOverText = "FIN DE LA PARTIDA - Presiona ESC para reiniciar";
        const int gameOverFontSize = 28;
        const int gameOverTextX = leftBarW + (static_cast<int>(playAreaWidth) - MeasureText(gameOverText, gameOverFontSize)) / 2;
        const int gameOverTextY = windowHeight_ / 2 - gameOverFontSize / 2;
        DrawRectangle(gameOverTextX - 12, gameOverTextY - 10, MeasureText(gameOverText, gameOverFontSize) + 24, gameOverFontSize + 20, Color{0, 0, 0, 200});
        DrawText(gameOverText, gameOverTextX, gameOverTextY, gameOverFontSize, RED);
    }

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
    UnloadTexture(grenadeTexture_);
    UnloadTexture(shovelTexture_);
    UnloadTexture(clockTexture_);
    UnloadTexture(brickUnitTextures_[0]);
    UnloadTexture(brickUnitTextures_[1]);
    UnloadTexture(steelUnitTexture_);
    UnloadTexture(treesTexture_);
    UnloadTexture(waterTextures_[0]);
    UnloadTexture(waterTextures_[1]);
    UnloadTexture(iceTexture_);
    UnloadTexture(baseEagleTexture_);
    UnloadTexture(hudLifeIconTexture_);
    UnloadTexture(stageFlagIconTexture_);
    bulletSprites_.Unload();
    player1Sprites_.Unload();
    player2Sprites_.Unload();
    player3Sprites_.Unload();
    player4Sprites_.Unload();
    enemySprites_.Unload();
    fastEnemySprites_.Unload();
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
