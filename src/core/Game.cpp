#include "Game.h"

#include <raylib.h>

#include "Camera.h"
#include "Config.h"
#include "LevelFormat.h"

namespace bc {

namespace {

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
    if (!level.player_spawns.empty()) {
        player1_.SetPosition(static_cast<float>(level.player_spawns[0][0]), static_cast<float>(level.player_spawns[0][1]));
    }
}

void Game::ProcessInput() {
    // Esquema de teclado WASD para P1 (seccion 9); mandos llegan en Fase 5.
    input1_.moveUp = IsKeyDown(KEY_W);
    input1_.moveDown = IsKeyDown(KEY_S);
    input1_.moveLeft = IsKeyDown(KEY_A);
    input1_.moveRight = IsKeyDown(KEY_D);
    input1_.shoot = IsKeyDown(KEY_LEFT_CONTROL);
}

void Game::Update(double fixedDt) {
    player1_.Update(fixedDt, input1_, map_);

    if (input1_.shoot) {
        float muzzleX = 0.0f, muzzleY = 0.0f;
        player1_.MuzzlePosition(muzzleX, muzzleY);
        bullets_.TryShoot(kPlayer1Id, muzzleX, muzzleY, player1_.Facing());
    }

    bullets_.Update(fixedDt, map_);
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

    const Texture2D tankTex = player1Sprites_.Get(player1_.Facing(), player1_.AnimFrame());
    const Rectangle src{0.0f, 0.0f, static_cast<float>(tankTex.width), static_cast<float>(tankTex.height)};
    const Rectangle dst{viewport.TileToScreenX(player1_.X()), viewport.TileToScreenY(player1_.Y()), viewport.tileScreenSize, viewport.tileScreenSize};
    DrawTexturePro(tankTex, src, dst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);

    const float pixelScale = viewport.tileScreenSize / static_cast<float>(kTileSize);
    for (const Bullet& bullet : bullets_.Bullets()) {
        const Texture2D bulletTex = bulletSprites_.Get(bullet.direction);
        const float w = static_cast<float>(bulletTex.width) * pixelScale;
        const float h = static_cast<float>(bulletTex.height) * pixelScale;
        const Rectangle bulletSrc{0.0f, 0.0f, static_cast<float>(bulletTex.width), static_cast<float>(bulletTex.height)};
        const Rectangle bulletDst{viewport.TileToScreenX(bullet.x) - w * 0.5f, viewport.TileToScreenY(bullet.y) - h * 0.5f, w, h};
        DrawTexturePro(bulletTex, bulletSrc, bulletDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    DrawFPS(10, 10);
    EndDrawing();
}

void Game::Shutdown() {
    bulletSprites_.Unload();
    player1Sprites_.Unload();
    CloseWindow();
}

void Game::Run() {
    Init();

    double accumulator = 0.0;
    while (!WindowShouldClose()) {
        accumulator += GetFrameTime();

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
