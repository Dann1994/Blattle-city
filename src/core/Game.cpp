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
    bulletSprites_.Load(BC_ASSETS_DIR);
    starTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_star.png").c_str());
    SetTextureFilter(starTexture_, TEXTURE_FILTER_POINT);
    helmetTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_helmet.png").c_str());
    SetTextureFilter(helmetTexture_, TEXTURE_FILTER_POINT);
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

    player1_ = Tank{};
    // TODO: exponer como opcion en el menu de configuracion (seccion 12.5).
    player1_.SetFireMode(FireMode::SinglePress);

    bullets_ = BulletSystem{};
    bulletImpacts_ = BulletImpactSystem{};
    specialExplosions_ = SpecialExplosionSystem{};
    specialExplosionEvents_.clear();
    powerUps_ = PowerUpSystem{};
    screenShakeTimer_ = 0.0;

    RespawnPlayer1();
}

void Game::RespawnPlayer1() {
    player1_.SetPosition(player1SpawnX_, player1SpawnY_);
    player1Spawn_.Start(player1SpawnX_, player1SpawnY_);
    player1_.ActivateShield(kRespawnShieldDuration);
}

void Game::ProcessInput() {
    // Esquema de teclado WASD para P1 (seccion 9); mandos llegan en Fase 5.
    input1_.moveUp = IsKeyDown(KEY_W);
    input1_.moveDown = IsKeyDown(KEY_S);
    input1_.moveLeft = IsKeyDown(KEY_A);
    input1_.moveRight = IsKeyDown(KEY_D);
    input1_.shoot = IsKeyDown(KEY_LEFT_CONTROL);
    input1_.specialShoot = IsKeyDown(KEY_SPACE);

    // Boton de prueba: repite el respawn en el punto de spawn inicial, para
    // poder ver el destello sin tener que reiniciar el juego.
    if (IsKeyPressed(KEY_R)) {
        RespawnPlayer1();
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
}

void Game::Update(double fixedDt) {
    player1_.TickShield(fixedDt);
    player1_.TickShootCooldown(fixedDt);
    player1_.TickHeatDecay(fixedDt);
    player1_.TickFreeze(fixedDt);
    if (screenShakeTimer_ > 0.0) {
        screenShakeTimer_ = std::max(0.0, screenShakeTimer_ - fixedDt);
    }

    if (player1Spawn_.IsActive()) {
        // Mientras dura el destello de aparicion, el tanque no se mueve ni
        // dispara ni puede ser tocado por power-ups (seccion 5 y 13).
        player1Spawn_.Update(fixedDt);
    } else {
        player1_.Update(fixedDt, input1_, map_);

        // Se consumen los dos triggers siempre (para no perder el flanco de
        // subida del boton mientras hay cooldown), pero solo disparan de
        // verdad si el tanque puede disparar. El especial tiene prioridad si
        // ambos se piden en el mismo frame.
        const bool normalTrigger = player1_.ConsumeShootTrigger(input1_);
        const bool specialTrigger = player1_.ConsumeSpecialShotTrigger(input1_);

        if (player1_.CanShoot() && !player1_.IsFrozen()) {
            float muzzleX = 0.0f, muzzleY = 0.0f;
            player1_.MuzzlePosition(muzzleX, muzzleY);
            if (specialTrigger && player1_.HasSpecialShotReady()) {
                bullets_.TryShootSpecial(kPlayer1Id, muzzleX, muzzleY, player1_.Facing());
                player1_.ConsumeSpecialShot();
                player1_.RegisterSpecialShotHeat();
            } else if (normalTrigger) {
                if (bullets_.TryShoot(kPlayer1Id, muzzleX, muzzleY, player1_.Facing(), player1_.BulletSpeed(), player1_.WeaponLevel(), player1_.MaxBullets())) {
                    player1_.RegisterNormalShotHeat();
                }
            }
        }

        PowerUpType pickedType{};
        if (powerUps_.TryPickup(player1_.X(), player1_.Y(), pickedType)) {
            if (pickedType == PowerUpType::Star) {
                player1_.PickupStar();
            } else if (pickedType == PowerUpType::Helmet) {
                player1_.ActivateShield(kHelmetShieldDuration);
            }
        }
    }

    bullets_.Update(fixedDt, map_, bulletImpacts_, specialExplosions_, specialExplosionEvents_);
    bulletImpacts_.Update(fixedDt);
    specialExplosions_.Update(fixedDt);
    powerUps_.Update(fixedDt, map_);

    if (!specialExplosionEvents_.empty()) {
        // Efecto global, no depende de la distancia (a diferencia del dano):
        // toda explosion especial sacude la pantalla y paraliza a TODOS los
        // tanques (por ahora solo el jugador 1; al agregar enemigos/aliados,
        // congelarlos aca tambien).
        screenShakeTimer_ = kScreenShakeDuration;
        player1_.Freeze(kSpecialImpactFreezeDuration);
    }

    // La explosion especial daña a quien alcance, incluido el propio tanque
    // que la disparo (seccion pedida explicitamente: "amigo-enemigo" a
    // proposito, es el riesgo de usar el disparo especial).
    for (const SpecialExplosionEvent& event : specialExplosionEvents_) {
        const float tankCenterX = player1_.X() + 0.5f;
        const float tankCenterY = player1_.Y() + 0.5f;
        const float dx = tankCenterX - event.x;
        const float dy = tankCenterY - event.y;
        const float hitRadius = event.radius + 0.5f; // + medio tanque de margen
        if (dx * dx + dy * dy <= hitRadius * hitRadius) {
            player1_.ApplyWeaponLevelPenalty(2);
        }
    }
}

void Game::Render(double /*interpolationAlpha*/) {
    windowWidth_ = GetScreenWidth();
    windowHeight_ = GetScreenHeight();

    const MapViewport viewport = MapViewport::Compute(windowWidth_, windowHeight_, map_.Width(), map_.Height(), kTileSize);

    BeginDrawing();
    // Mismo color que las celdas vacias (ColorForTile de Empty), asi una
    // unidad de ladrillo destruida no deja un negro puro que desentona con
    // el resto del fondo.
    ClearBackground(ColorForTile(TileType::Empty));

    // Sacudida de pantalla (onda expansiva del disparo especial): desplaza
    // todo el dibujado (mapa, tanque, UI incluida) con un offset aleatorio
    // que decae a medida que pasa el tiempo.
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

    if (player1Spawn_.IsActive()) {
        const Texture2D flashTex = spawnFlashSprites_.Get(player1Spawn_.FrameIndex());
        const Rectangle flashSrc{0.0f, 0.0f, static_cast<float>(flashTex.width), static_cast<float>(flashTex.height)};
        const Rectangle flashDst{viewport.TileToScreenX(player1Spawn_.X()), viewport.TileToScreenY(player1Spawn_.Y()), viewport.tileScreenSize, viewport.tileScreenSize};
        DrawTexturePro(flashTex, flashSrc, flashDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    } else {
        const Texture2D tankTex = player1Sprites_.Get(player1_.WeaponLevel(), player1_.Facing(), player1_.AnimFrame());
        const Rectangle src{0.0f, 0.0f, static_cast<float>(tankTex.width), static_cast<float>(tankTex.height)};
        const Rectangle dst{viewport.TileToScreenX(player1_.X()), viewport.TileToScreenY(player1_.Y()), viewport.tileScreenSize, viewport.tileScreenSize};
        DrawTexturePro(tankTex, src, dst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);

        if (player1_.IsShielded()) {
            const int shieldFrame = static_cast<int>(GetTime() / kShieldBlinkInterval) % 2;
            const Texture2D shieldTex = shieldSprites_.Get(shieldFrame);
            const Rectangle shieldSrc{0.0f, 0.0f, static_cast<float>(shieldTex.width), static_cast<float>(shieldTex.height)};
            DrawTexturePro(shieldTex, shieldSrc, dst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        }
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
        const Rectangle explosionDst{viewport.TileToScreenX(explosion.x) - specialExplosionSize * 0.5f, viewport.TileToScreenY(explosion.y) - specialExplosionSize * 0.5f, specialExplosionSize, specialExplosionSize};
        DrawTexturePro(explosionTex, explosionSrc, explosionDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    if (powerUps_.Active().alive && powerUps_.IsBlinkVisible()) {
        const Texture2D iconTex = (powerUps_.Active().type == PowerUpType::Star) ? starTexture_ : helmetTexture_;
        const Rectangle iconSrc{0.0f, 0.0f, static_cast<float>(iconTex.width), static_cast<float>(iconTex.height)};
        const Rectangle iconDst{viewport.TileToScreenX(powerUps_.Active().x), viewport.TileToScreenY(powerUps_.Active().y), viewport.tileScreenSize, viewport.tileScreenSize};
        DrawTexturePro(iconTex, iconSrc, iconDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    DrawFPS(10, 10);
    DrawText(TextFormat("Nivel de arma: %d", player1_.WeaponLevel()), 10, 30, 20, RAYWHITE);
    if (player1_.IsShielded()) {
        DrawText(TextFormat("Escudo: %.1fs", player1_.ShieldSecondsRemaining()), 10, 55, 20, RAYWHITE);
    }
    if (player1_.WeaponLevel() == 4) {
        DrawText(TextFormat("Disparo especial: %d/1", player1_.HasSpecialShotReady() ? 1 : 0), 10, 80, 20, RAYWHITE);
    }

    {
        constexpr int kHeatBarX = 10;
        constexpr int kHeatBarY = 105;
        constexpr int kHeatBarWidth = 200;
        constexpr int kHeatBarHeight = 16;
        const float heatPercent = player1_.HeatPercent();
        DrawRectangle(kHeatBarX, kHeatBarY, kHeatBarWidth, kHeatBarHeight, DARKGRAY);
        const int filledWidth = static_cast<int>(kHeatBarWidth * (heatPercent / 100.0f));
        DrawRectangle(kHeatBarX, kHeatBarY, filledWidth, kHeatBarHeight, heatPercent >= 100.0f ? RED : ORANGE);
        DrawRectangleLines(kHeatBarX, kHeatBarY, kHeatBarWidth, kHeatBarHeight, RAYWHITE);
        DrawText(TextFormat("Calor: %.0f%%", heatPercent), kHeatBarX + kHeatBarWidth + 10, kHeatBarY - 2, 20, RAYWHITE);
    }
    if (!player1_.CanShoot()) {
        DrawText(TextFormat("Sin poder disparar: %.1fs", player1_.ShootCooldownRemaining()), 10, 130, 20, RAYWHITE);
    }
    if (player1_.IsFrozen()) {
        DrawText("PARALIZADO", 10, 155, 20, RED);
    }

    EndMode2D();
    EndDrawing();
}

void Game::Shutdown() {
    specialExplosionSprites_.Unload();
    bulletImpactSprites_.Unload();
    shieldSprites_.Unload();
    spawnFlashSprites_.Unload();
    UnloadTexture(starTexture_);
    UnloadTexture(helmetTexture_);
    UnloadTexture(brickUnitTextures_[0]);
    UnloadTexture(brickUnitTextures_[1]);
    UnloadTexture(steelUnitTexture_);
    bulletSprites_.Unload();
    player1Sprites_.Unload();
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
