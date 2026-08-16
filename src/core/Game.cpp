#include "Game.h"

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

Color ColorForTile(TileType type) {
    switch (type) {
        case TileType::Brick: return Color{0xB0, 0x60, 0x28, 0xFF};
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

    const LevelData level = LoadLevel(std::string(BC_ASSETS_DIR) + "levels/test_map.json");
    map_.LoadFrom(level);

    player1Sprites_.LoadPlayer1(BC_ASSETS_DIR);
    bulletSprites_.Load(BC_ASSETS_DIR);
    starTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_star.png").c_str());
    SetTextureFilter(starTexture_, TEXTURE_FILTER_POINT);
    helmetTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_helmet.png").c_str());
    SetTextureFilter(helmetTexture_, TEXTURE_FILTER_POINT);
    spawnFlashSprites_.Load(BC_ASSETS_DIR);
    shieldSprites_.Load(BC_ASSETS_DIR);

    // TODO: exponer como opcion en el menu de configuracion (seccion 12.5).
    player1_.SetFireMode(FireMode::SinglePress);

    if (!level.player_spawns.empty()) {
        player1SpawnX_ = static_cast<float>(level.player_spawns[0][0]);
        player1SpawnY_ = static_cast<float>(level.player_spawns[0][1]);
    }
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

    // Boton de prueba: repite el respawn en el punto de spawn inicial, para
    // poder ver el destello sin tener que reiniciar el juego.
    if (IsKeyPressed(KEY_R)) {
        RespawnPlayer1();
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

    if (player1Spawn_.IsActive()) {
        // Mientras dura el destello de aparicion, el tanque no se mueve ni
        // dispara ni puede ser tocado por power-ups (seccion 5 y 13).
        player1Spawn_.Update(fixedDt);
    } else {
        player1_.Update(fixedDt, input1_, map_);

        if (player1_.ConsumeShootTrigger(input1_)) {
            float muzzleX = 0.0f, muzzleY = 0.0f;
            player1_.MuzzlePosition(muzzleX, muzzleY);
            bullets_.TryShoot(kPlayer1Id, muzzleX, muzzleY, player1_.Facing(), player1_.BulletSpeed(), player1_.CanDestroySteel(), player1_.MaxBullets());
        }

        PowerUpType pickedType{};
        if (powerUps_.TryPickup(player1_.X(), player1_.Y(), pickedType)) {
            if (pickedType == PowerUpType::Star) {
                player1_.UpgradeWeapon();
            } else if (pickedType == PowerUpType::Helmet) {
                player1_.ActivateShield(kHelmetShieldDuration);
            }
        }
    }

    bullets_.Update(fixedDt, map_);
    powerUps_.Update(fixedDt, map_);
}

void Game::Render(double /*interpolationAlpha*/) {
    windowWidth_ = GetScreenWidth();
    windowHeight_ = GetScreenHeight();

    const MapViewport viewport = MapViewport::Compute(windowWidth_, windowHeight_, map_.Width(), map_.Height(), kTileSize);

    BeginDrawing();
    ClearBackground(BLACK);

    const float halfTile = viewport.tileScreenSize * 0.5f;

    for (int y = 0; y < map_.Height(); ++y) {
        for (int x = 0; x < map_.Width(); ++x) {
            const Cell& cell = map_.At(x, y);
            const float screenX = viewport.TileToScreenX(x);
            const float screenY = viewport.TileToScreenY(y);

            if (cell.type == TileType::Brick && cell.subMask != kSubCellMaskFull) {
                const Color color = ColorForTile(cell.type);
                if (cell.subMask & kSubCellTopLeft) DrawRectangleRec(Rectangle{screenX, screenY, halfTile, halfTile}, color);
                if (cell.subMask & kSubCellTopRight) DrawRectangleRec(Rectangle{screenX + halfTile, screenY, halfTile, halfTile}, color);
                if (cell.subMask & kSubCellBottomLeft) DrawRectangleRec(Rectangle{screenX, screenY + halfTile, halfTile, halfTile}, color);
                if (cell.subMask & kSubCellBottomRight) DrawRectangleRec(Rectangle{screenX + halfTile, screenY + halfTile, halfTile, halfTile}, color);
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
    EndDrawing();
}

void Game::Shutdown() {
    shieldSprites_.Unload();
    spawnFlashSprites_.Unload();
    UnloadTexture(starTexture_);
    UnloadTexture(helmetTexture_);
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
