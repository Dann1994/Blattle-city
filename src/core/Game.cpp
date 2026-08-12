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
}

void Game::ProcessInput() {
    // Fase 0: sin input de gameplay todavia (llega en Fase 1).
}

void Game::Update(double /*fixedDt*/) {
    // Fase 0: sin simulacion todavia, solo carga y render del mapa de prueba.
}

void Game::Render(double /*interpolationAlpha*/) {
    windowWidth_ = GetScreenWidth();
    windowHeight_ = GetScreenHeight();

    const MapViewport viewport = MapViewport::Compute(windowWidth_, windowHeight_, map_.Width(), map_.Height(), kTileSize);

    BeginDrawing();
    ClearBackground(BLACK);

    for (int y = 0; y < map_.Height(); ++y) {
        for (int x = 0; x < map_.Width(); ++x) {
            const Cell& cell = map_.At(x, y);
            const Rectangle rect{viewport.TileToScreenX(x), viewport.TileToScreenY(y), viewport.tileScreenSize, viewport.tileScreenSize};
            DrawRectangleRec(rect, ColorForTile(cell.type));
        }
    }

    DrawFPS(10, 10);
    EndDrawing();
}

void Game::Shutdown() {
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
