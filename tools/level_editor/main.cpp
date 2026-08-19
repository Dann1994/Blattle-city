// Editor de niveles (Fase 6, seccion 12.7 del documento de diseno): herramienta
// interna de desarrollo, NO se empaqueta con el juego (ver CMakeLists.txt: es
// un add_executable aparte de battlecity). Lee/escribe el mismo formato JSON
// de nivel que el juego (ver shared/LevelFormat.h), asi que el mapa que arma
// aca es directamente compatible.
//
// Primera version (se va a ir ampliando por indicaciones): grilla fija del
// tamano del "campo de batalla" actual (se toma prestado el ancho/alto y los
// spawns/base de assets/levels/test_map.json como plantilla de referencia,
// para no tener que definirlos a mano todavia), paleta de bloques a la
// derecha seleccionable con el mouse, un bloque entero por click/celda
// (nunca se superponen), y guardado con prompt de numero de nivel (1-40).

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <raylib.h>

#include "BrickUnit.h"
#include "LevelFormat.h"
#include "SteelUnit.h"
#include "TileTypes.h"

using namespace bc;

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 800;
constexpr int kTitleBarHeight = 32;
constexpr int kSidebarWidth = 220;
constexpr int kBaseTileSize = 16; // igual que Config::kTileSize del juego

constexpr int kMaxLevelNumber = 40;

// Los 6 bloques que se pueden pintar desde la paleta. El aguila (Base) NO
// esta aca: aparece ya puesta en el mapa (ver ResetCanvas) y no se puede
// mover/borrar en esta primera version.
constexpr TileType kPaletteTypes[] = {
    TileType::Empty, TileType::Brick, TileType::Steel,
    TileType::Water, TileType::Trees, TileType::Ice,
};
constexpr int kPaletteCount = 6;

const char* LabelForType(TileType type) {
    switch (type) {
        case TileType::Brick: return "Ladrillo";
        case TileType::Steel: return "Hierro";
        case TileType::Water: return "Agua";
        case TileType::Trees: return "Arbusto";
        case TileType::Ice:   return "Hielo";
        default:              return "Vacio";
    }
}

const char* LabelForShape(BlockShape shape) {
    switch (shape) {
        case BlockShape::Left:        return "Izquierda";
        case BlockShape::Right:       return "Derecha";
        case BlockShape::Top:         return "Arriba";
        case BlockShape::Bottom:      return "Abajo";
        case BlockShape::TopLeft:     return "Esquina arriba-izq.";
        case BlockShape::TopRight:    return "Esquina arriba-der.";
        case BlockShape::BottomLeft:  return "Esquina abajo-izq.";
        case BlockShape::BottomRight: return "Esquina abajo-der.";
        default:                      return "Completo";
    }
}

bool TypeHasShapes(TileType type) {
    return type == TileType::Brick || type == TileType::Steel;
}

// La forma final es la combinacion de 2 ejes independientes, uno por par de
// flechas (Arriba/Abajo e Izquierda/Derecha): ningun eje activo = Completo,
// un solo eje = mitad de ese lado, los 2 ejes juntos = la esquina
// correspondiente (un cuarto del bloque). Deja elegir las 9 formas con las
// mismas 4 flechas, sin atajos nuevos.
enum class VerticalHalf { None, Top, Bottom };
enum class HorizontalHalf { None, Left, Right };

BlockShape CombineShape(VerticalHalf v, HorizontalHalf h) {
    if (v == VerticalHalf::None && h == HorizontalHalf::None) return BlockShape::Full;
    if (v == VerticalHalf::None) return h == HorizontalHalf::Left ? BlockShape::Left : BlockShape::Right;
    if (h == HorizontalHalf::None) return v == VerticalHalf::Top ? BlockShape::Top : BlockShape::Bottom;
    if (v == VerticalHalf::Top) return h == HorizontalHalf::Left ? BlockShape::TopLeft : BlockShape::TopRight;
    return h == HorizontalHalf::Left ? BlockShape::BottomLeft : BlockShape::BottomRight;
}

Color ColorForEmpty() { return Color{0x10, 0x10, 0x10, 0xFF}; }

enum class EditorMode {
    Editing,
    EnteringLevelNumber,
    ConfirmOverwrite,
    ConfirmSaveAnother,
    LevelBrowser,
    ConfirmDeleteLevel,
};

bool Clicked(Rectangle rect, Vector2 mouse) {
    return CheckCollisionPointRec(mouse, rect) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

std::string LevelFileName(int levelNumber) {
    char buf[32];
    snprintf(buf, sizeof(buf), "level_%02d.json", levelNumber);
    return buf;
}

} // namespace

int main() {
    InitWindow(kWindowWidth, kWindowHeight, "Editor de Niveles - Battle City (herramienta interna, no se distribuye)");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL); // ESC se usa para cancelar prompts, no para cerrar de golpe

    const std::string assetsDir = BC_ASSETS_DIR;
    const std::string levelsDir = assetsDir + "levels/";

    // Plantilla de referencia: de aca se toman el ancho/alto del "campo de
    // batalla" y los spawns/base (todavia no editables desde esta
    // herramienta, ver el comentario de cabecera) para que el nivel
    // guardado sea inmediatamente compatible con el juego.
    const LevelData templateLevel = LoadLevel(levelsDir + "test_map.json");
    const int gridWidth = templateLevel.width;
    const int gridHeight = templateLevel.height;
    const int baseX = templateLevel.base_position[0];
    const int baseY = templateLevel.base_position[1];

    // Copias editables de los spawns (arrancan igual que la plantilla, pero
    // ahora se pueden arrastrar, ver mas abajo). El aguila sigue fija.
    std::vector<std::array<int, 2>> playerSpawns = templateLevel.player_spawns;
    std::vector<std::array<int, 2>> enemySpawns = templateLevel.enemy_spawns;

    // Celdas que no se pueden pintar/borrar directamente: el aguila y los
    // puntos de spawn (esos se mueven arrastrando, no pintando encima).
    auto IsProtectedCell = [&](int x, int y) {
        if (x == baseX && y == baseY) {
            return true;
        }
        for (const std::array<int, 2>& spawn : playerSpawns) {
            if (spawn[0] == x && spawn[1] == y) {
                return true;
            }
        }
        for (const std::array<int, 2>& spawn : enemySpawns) {
            if (spawn[0] == x && spawn[1] == y) {
                return true;
            }
        }
        return false;
    };

    std::vector<TileType> cells(static_cast<size_t>(gridWidth) * gridHeight, TileType::Empty);
    std::vector<BlockShape> cellShapes(static_cast<size_t>(gridWidth) * gridHeight, BlockShape::Full);
    auto ResetCanvas = [&]() {
        std::fill(cells.begin(), cells.end(), TileType::Empty);
        std::fill(cellShapes.begin(), cellShapes.end(), BlockShape::Full);
        if (baseX >= 0 && baseX < gridWidth && baseY >= 0 && baseY < gridHeight) {
            cells[static_cast<size_t>(baseY) * gridWidth + baseX] = TileType::Base;
        }
        playerSpawns = templateLevel.player_spawns;
        enemySpawns = templateLevel.enemy_spawns;
    };
    ResetCanvas();

    auto SaveCurrentCanvas = [&](int levelNumber) {
        LevelData toSave = templateLevel;
        toSave.width = gridWidth;
        toSave.height = gridHeight;
        toSave.tiles.assign(gridHeight, std::string(gridWidth, '.'));
        toSave.block_shapes.assign(gridHeight, std::string(gridWidth, 'F'));
        for (int y = 0; y < gridHeight; ++y) {
            for (int x = 0; x < gridWidth; ++x) {
                const size_t idx = static_cast<size_t>(y) * gridWidth + x;
                toSave.tiles[y][x] = TileTypeToChar(cells[idx]);
                toSave.block_shapes[y][x] = BlockShapeToChar(cellShapes[idx]);
            }
        }
        toSave.player_spawns = playerSpawns;
        toSave.enemy_spawns = enemySpawns;
        SaveLevel(levelsDir + LevelFileName(levelNumber), toSave);
    };

    // Vuelca un LevelData ya cargado (ver navegador de niveles, boton
    // Editar) al lienzo actual, pisando lo que hubiera. Asume las mismas
    // dimensiones que gridWidth/gridHeight (todo nivel de este editor
    // comparte el mismo campo de batalla); si el nivel guardado fuera mas
    // chico (no deberia pasar), las celdas de mas quedan Vacio.
    auto LoadLevelIntoCanvas = [&](const LevelData& level) {
        for (int y = 0; y < gridHeight; ++y) {
            const bool hasRow = y < static_cast<int>(level.tiles.size());
            for (int x = 0; x < gridWidth; ++x) {
                const size_t idx = static_cast<size_t>(y) * gridWidth + x;
                const char tileChar = (hasRow && x < static_cast<int>(level.tiles[y].size())) ? level.tiles[y][x] : '.';
                cells[idx] = TileTypeFromChar(tileChar);
                char shapeChar = 'F';
                if (y < static_cast<int>(level.block_shapes.size()) && x < static_cast<int>(level.block_shapes[y].size())) {
                    shapeChar = level.block_shapes[y][x];
                }
                cellShapes[idx] = BlockShapeFromChar(shapeChar);
            }
        }
        if (baseX >= 0 && baseX < gridWidth && baseY >= 0 && baseY < gridHeight) {
            cells[static_cast<size_t>(baseY) * gridWidth + baseX] = TileType::Base;
        }
        playerSpawns = level.player_spawns.empty() ? templateLevel.player_spawns : level.player_spawns;
        enemySpawns = level.enemy_spawns.empty() ? templateLevel.enemy_spawns : level.enemy_spawns;
    };

    // Texturas de icono de paleta / render del lienzo: se reusan las mismas
    // que el juego real (ver Game::Init), asi el editor se ve igual.
    Texture2D brickUnitTextures[2];
    brickUnitTextures[0] = LoadTexture((assetsDir + "sprites/brick_unit_0.png").c_str());
    brickUnitTextures[1] = LoadTexture((assetsDir + "sprites/brick_unit_1.png").c_str());
    Texture2D steelUnitTexture = LoadTexture((assetsDir + "sprites/steel_unit.png").c_str());
    Texture2D waterTexture = LoadTexture((assetsDir + "sprites/terrain_water_0.png").c_str());
    Texture2D treesTexture = LoadTexture((assetsDir + "sprites/terrain_trees.png").c_str());
    Texture2D iceTexture = LoadTexture((assetsDir + "sprites/terrain_ice.png").c_str());
    Texture2D baseEagleTexture = LoadTexture((assetsDir + "sprites/base_eagle.png").c_str());
    for (Texture2D* tex : {&brickUnitTextures[0], &brickUnitTextures[1], &steelUnitTexture, &waterTexture, &treesTexture, &iceTexture, &baseEagleTexture}) {
        SetTextureFilter(*tex, TEXTURE_FILTER_POINT);
    }

    TileType selectedType = TileType::Brick;
    VerticalHalf selectedVertical = VerticalHalf::None;
    HorizontalHalf selectedHorizontal = HorizontalHalf::None;
    bool toolActive = true; // false tras Esc: deselecciona la herramienta y deja arrastrar spawns
    EditorMode mode = EditorMode::Editing;
    std::string levelNumberInput;
    int pendingLevelNumber = 0;
    std::string statusMessage = "Clic izq: pintar. Clic der: borrar. Ladrillo/Hierro: flechas para elegir mitad/esquina.";

    // Arrastre de un punto de spawn (solo mientras !toolActive, ver Esc).
    enum class SpawnKind { None, Player, Enemy };
    SpawnKind draggingKind = SpawnKind::None;
    size_t draggingIndex = 0;

    // Navegador de niveles (boton "Niveles"): que numero se esta mirando y
    // una cache de su contenido (para no releer el archivo 60 veces por
    // segundo) que se refresca solo al cambiar de numero o al borrar.
    int browsingLevelNumber = 1;
    bool browsingLevelExists = false;
    LevelData browsingLevelData;
    auto RefreshBrowsingLevel = [&]() {
        const std::string path = levelsDir + LevelFileName(browsingLevelNumber);
        if (std::filesystem::exists(path)) {
            try {
                browsingLevelData = LoadLevel(path);
                browsingLevelExists = true;
            } catch (const std::exception&) {
                browsingLevelExists = false;
            }
        } else {
            browsingLevelExists = false;
        }
    };

    // Geometria del dialogo Si/No (confirmaciones): un solo lugar, usado
    // tanto para detectar el click como para dibujarlo, asi nunca se
    // desincroniza entre el input y el render.
    constexpr int kDialogW = 480, kDialogH = 140;
    constexpr int kDialogX = (kWindowWidth - kDialogW) / 2;
    constexpr int kDialogY = (kWindowHeight - kDialogH) / 2;
    const Rectangle yesButtonRect{static_cast<float>(kDialogX + 60), static_cast<float>(kDialogY + 80), 140, 40};
    const Rectangle noButtonRect{static_cast<float>(kDialogX + kDialogW - 200), static_cast<float>(kDialogY + 80), 140, 40};

    // Geometria del navegador de niveles (boton "Niveles"): panel grande
    // centrado con una vista previa chica, tambien un solo lugar para
    // input y render.
    constexpr int kBrowserW = 760, kBrowserH = 620;
    constexpr int kBrowserX = (kWindowWidth - kBrowserW) / 2;
    constexpr int kBrowserY = (kWindowHeight - kBrowserH) / 2;
    constexpr int kBrowserPreviewX = kBrowserX + 20;
    constexpr int kBrowserPreviewY = kBrowserY + 60;
    constexpr int kBrowserPreviewW = kBrowserW - 40;
    constexpr int kBrowserPreviewH = 420;
    const Rectangle deleteButtonRect{static_cast<float>(kBrowserX + 40), static_cast<float>(kBrowserY + kBrowserH - 70), 160, 44};
    const Rectangle editButtonRect{static_cast<float>(kBrowserX + kBrowserW - 200), static_cast<float>(kBrowserY + kBrowserH - 70), 160, 44};

    while (!WindowShouldClose()) {
        const Vector2 mouse = GetMousePosition();

        // --- Area del lienzo (a la izquierda, debajo del titulo) ---
        const int canvasAreaW = kWindowWidth - kSidebarWidth;
        const int canvasAreaH = kWindowHeight - kTitleBarHeight;
        const float mapPixelW = static_cast<float>(gridWidth * kBaseTileSize);
        const float mapPixelH = static_cast<float>(gridHeight * kBaseTileSize);
        const float scale = std::min(static_cast<float>(canvasAreaW) / mapPixelW, static_cast<float>(canvasAreaH) / mapPixelH);
        const float tileScreenSize = kBaseTileSize * scale;
        const float canvasOffsetX = (canvasAreaW - mapPixelW * scale) * 0.5f;
        const float canvasOffsetY = kTitleBarHeight + (canvasAreaH - mapPixelH * scale) * 0.5f;
        auto CellToScreen = [&](int cellX, int cellY) {
            return Vector2{canvasOffsetX + cellX * tileScreenSize, canvasOffsetY + cellY * tileScreenSize};
        };
        auto ScreenToCell = [&](Vector2 p, int& outX, int& outY) -> bool {
            const float localX = p.x - canvasOffsetX;
            const float localY = p.y - canvasOffsetY;
            if (localX < 0.0f || localY < 0.0f || localX >= mapPixelW * scale || localY >= mapPixelH * scale) {
                return false;
            }
            outX = static_cast<int>(localX / tileScreenSize);
            outY = static_cast<int>(localY / tileScreenSize);
            return true;
        };
        auto SpawnScreenCenter = [&](int cellX, int cellY) {
            const Vector2 p = CellToScreen(cellX, cellY);
            return Vector2{p.x + tileScreenSize * 0.5f, p.y + tileScreenSize * 0.5f};
        };

        // Dibuja un tipo+forma en la posicion de pantalla p con el tamano de
        // celda cellSize (el del lienzo principal, o uno mas chico para el
        // preview del navegador de niveles) y el tint dado (WHITE opaco
        // para una celda real, semitransparente para el preview bajo el
        // mouse). Unico lugar que sabe dibujar cada tipo de terreno, para
        // que se vea igual en todos lados.
        auto DrawTerrainCell = [&](TileType type, BlockShape shape, Vector2 p, float cellSize, Color tint) {
            if (type == TileType::Brick) {
                const float unitSize = cellSize / kBrickGridSize;
                for (int row = 0; row < kBrickGridSize; ++row) {
                    for (int col = 0; col < kBrickGridSize; ++col) {
                        if (!IsUnitAliveForShape(shape, row, col, kBrickGridSize)) {
                            continue;
                        }
                        const Texture2D& unitTex = brickUnitTextures[BrickUnit::FrameFor(row, col)];
                        const Rectangle src{0, 0, static_cast<float>(unitTex.width), static_cast<float>(unitTex.height)};
                        const Rectangle dst{p.x + col * unitSize, p.y + row * unitSize, unitSize, unitSize};
                        DrawTexturePro(unitTex, src, dst, Vector2{0, 0}, 0.0f, tint);
                    }
                }
            } else if (type == TileType::Steel) {
                const float unitSize = cellSize / kSteelGridSize;
                const Rectangle src{0, 0, static_cast<float>(steelUnitTexture.width), static_cast<float>(steelUnitTexture.height)};
                for (int row = 0; row < kSteelGridSize; ++row) {
                    for (int col = 0; col < kSteelGridSize; ++col) {
                        if (!IsUnitAliveForShape(shape, row, col, kSteelGridSize)) {
                            continue;
                        }
                        const Rectangle dst{p.x + col * unitSize, p.y + row * unitSize, unitSize, unitSize};
                        DrawTexturePro(steelUnitTexture, src, dst, Vector2{0, 0}, 0.0f, tint);
                    }
                }
            } else if (type == TileType::Water) {
                const Rectangle src{0, 0, static_cast<float>(waterTexture.width), static_cast<float>(waterTexture.height)};
                DrawTexturePro(waterTexture, src, Rectangle{p.x, p.y, cellSize, cellSize}, Vector2{0, 0}, 0.0f, tint);
            } else if (type == TileType::Trees) {
                const Rectangle src{0, 0, static_cast<float>(treesTexture.width), static_cast<float>(treesTexture.height)};
                DrawTexturePro(treesTexture, src, Rectangle{p.x, p.y, cellSize, cellSize}, Vector2{0, 0}, 0.0f, tint);
            } else if (type == TileType::Ice) {
                const Rectangle src{0, 0, static_cast<float>(iceTexture.width), static_cast<float>(iceTexture.height)};
                DrawTexturePro(iceTexture, src, Rectangle{p.x, p.y, cellSize, cellSize}, Vector2{0, 0}, 0.0f, tint);
            } else if (type == TileType::Base) {
                const Rectangle src{0, 0, static_cast<float>(baseEagleTexture.width), static_cast<float>(baseEagleTexture.height)};
                DrawTexturePro(baseEagleTexture, src, Rectangle{p.x, p.y, cellSize, cellSize}, Vector2{0, 0}, 0.0f, tint);
            } else {
                Color c = ColorForEmpty();
                c.a = tint.a;
                DrawRectangle(static_cast<int>(p.x), static_cast<int>(p.y), static_cast<int>(cellSize), static_cast<int>(cellSize), c);
            }
        };

        // --- Paleta (sidebar derecho) ---
        constexpr int kPaletteButtonH = 64;
        constexpr int kPaletteButtonMargin = 10;
        const int paletteX = canvasAreaW + kPaletteButtonMargin;
        const int paletteW = kSidebarWidth - kPaletteButtonMargin * 2;
        std::array<Rectangle, kPaletteCount> paletteRects{};
        for (int i = 0; i < kPaletteCount; ++i) {
            const int by = kTitleBarHeight + kPaletteButtonMargin + i * (kPaletteButtonH + kPaletteButtonMargin);
            paletteRects[i] = Rectangle{static_cast<float>(paletteX), static_cast<float>(by), static_cast<float>(paletteW), static_cast<float>(kPaletteButtonH)};
        }
        // Franja fija para el indicador de forma (Ladrillo/Hierro), aunque
        // este vacia para otros tipos: asi el boton Guardar no se mueve
        // segun el tipo seleccionado.
        const int shapeLabelY = kTitleBarHeight + kPaletteButtonMargin + kPaletteCount * (kPaletteButtonH + kPaletteButtonMargin) + 4;
        const int saveButtonY = shapeLabelY + 30;
        const Rectangle saveButtonRect{static_cast<float>(paletteX), static_cast<float>(saveButtonY), static_cast<float>(paletteW), 44.0f};
        const int browseButtonY = saveButtonY + 44 + 10;
        const Rectangle browseButtonRect{static_cast<float>(paletteX), static_cast<float>(browseButtonY), static_cast<float>(paletteW), 44.0f};

        // --- Input ---
        if (mode == EditorMode::Editing) {
            // La paleta siempre responde al click, incluso si Esc habia
            // deseleccionado la herramienta (asi se vuelve a pintar).
            for (int i = 0; i < kPaletteCount; ++i) {
                if (Clicked(paletteRects[i], mouse)) {
                    selectedType = kPaletteTypes[i];
                    selectedVertical = VerticalHalf::None;
                    selectedHorizontal = HorizontalHalf::None;
                    toolActive = true;
                }
            }

            if (IsKeyPressed(KEY_ESCAPE)) {
                toolActive = false;
                draggingKind = SpawnKind::None;
                statusMessage = "Herramienta deseleccionada: arrastra un punto de spawn con el mouse. Clic en un bloque de la paleta para volver a pintar.";
            }

            if (toolActive) {
                // Solo para Ladrillo/Hierro: las flechas eligen la mitad
                // pegada a ese lado; las dos flechas de un mismo "cruce"
                // juntas (por ej. Arriba + Izquierda) dan la esquina, un
                // cuarto del bloque (ver CombineShape). Apretar la misma
                // de nuevo la saca.
                if (TypeHasShapes(selectedType)) {
                    if (IsKeyPressed(KEY_LEFT))  selectedHorizontal = (selectedHorizontal == HorizontalHalf::Left)  ? HorizontalHalf::None : HorizontalHalf::Left;
                    if (IsKeyPressed(KEY_RIGHT)) selectedHorizontal = (selectedHorizontal == HorizontalHalf::Right) ? HorizontalHalf::None : HorizontalHalf::Right;
                    if (IsKeyPressed(KEY_UP))    selectedVertical   = (selectedVertical   == VerticalHalf::Top)    ? VerticalHalf::None : VerticalHalf::Top;
                    if (IsKeyPressed(KEY_DOWN))  selectedVertical   = (selectedVertical   == VerticalHalf::Bottom) ? VerticalHalf::None : VerticalHalf::Bottom;
                }
                int cellX = 0, cellY = 0;
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && ScreenToCell(mouse, cellX, cellY)) {
                    // El aguila y los spawns quedan fijos: no se pintan ni
                    // se borran pintando encima (se mueven arrastrando, ver
                    // el else de abajo).
                    if (!IsProtectedCell(cellX, cellY)) {
                        const size_t idx = static_cast<size_t>(cellY) * gridWidth + cellX;
                        cells[idx] = selectedType;
                        cellShapes[idx] = TypeHasShapes(selectedType) ? CombineShape(selectedVertical, selectedHorizontal) : BlockShape::Full;
                    }
                }
            } else {
                // Herramienta deseleccionada (Esc): arrastrar puntos de spawn.
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && draggingKind == SpawnKind::None) {
                    for (size_t i = 0; i < playerSpawns.size() && draggingKind == SpawnKind::None; ++i) {
                        if (CheckCollisionPointCircle(mouse, SpawnScreenCenter(playerSpawns[i][0], playerSpawns[i][1]), tileScreenSize * 0.35f)) {
                            draggingKind = SpawnKind::Player;
                            draggingIndex = i;
                        }
                    }
                    for (size_t i = 0; i < enemySpawns.size() && draggingKind == SpawnKind::None; ++i) {
                        if (CheckCollisionPointCircle(mouse, SpawnScreenCenter(enemySpawns[i][0], enemySpawns[i][1]), tileScreenSize * 0.35f)) {
                            draggingKind = SpawnKind::Enemy;
                            draggingIndex = i;
                        }
                    }
                }
                if (draggingKind != SpawnKind::None) {
                    int cellX = 0, cellY = 0;
                    if (ScreenToCell(mouse, cellX, cellY)) {
                        std::array<int, 2>& spawn = (draggingKind == SpawnKind::Player) ? playerSpawns[draggingIndex] : enemySpawns[draggingIndex];
                        spawn[0] = cellX;
                        spawn[1] = cellY;
                        // Si cae encima de un bloque ya puesto, lo limpia:
                        // un spawn nunca deberia quedar tapado por terreno.
                        if (!(cellX == baseX && cellY == baseY)) {
                            const size_t idx = static_cast<size_t>(cellY) * gridWidth + cellX;
                            cells[idx] = TileType::Empty;
                            cellShapes[idx] = BlockShape::Full;
                        }
                    }
                    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                        draggingKind = SpawnKind::None;
                    }
                }
            }

            // Clic derecho borra (vuelve a Vacio) sin importar la
            // herramienta seleccionada, para no tener que cambiar a "Vacio"
            // en la paleta solo para borrar.
            int eraseCellX = 0, eraseCellY = 0;
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && ScreenToCell(mouse, eraseCellX, eraseCellY) && !IsProtectedCell(eraseCellX, eraseCellY)) {
                const size_t idx = static_cast<size_t>(eraseCellY) * gridWidth + eraseCellX;
                cells[idx] = TileType::Empty;
                cellShapes[idx] = BlockShape::Full;
            }

            if (Clicked(saveButtonRect, mouse)) {
                mode = EditorMode::EnteringLevelNumber;
                levelNumberInput.clear();
                statusMessage = "Que numero de nivel es (1-40)? Enter para confirmar, Esc para cancelar.";
            }
            if (Clicked(browseButtonRect, mouse)) {
                mode = EditorMode::LevelBrowser;
                RefreshBrowsingLevel();
            }
        } else if (mode == EditorMode::EnteringLevelNumber) {
            int ch = GetCharPressed();
            while (ch > 0) {
                if (ch >= '0' && ch <= '9' && levelNumberInput.size() < 2) {
                    levelNumberInput.push_back(static_cast<char>(ch));
                }
                ch = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE) && !levelNumberInput.empty()) {
                levelNumberInput.pop_back();
            }
            if (IsKeyPressed(KEY_ESCAPE)) {
                mode = EditorMode::Editing;
                statusMessage = "Guardado cancelado.";
            }
            if (IsKeyPressed(KEY_ENTER) && !levelNumberInput.empty()) {
                const int n = std::atoi(levelNumberInput.c_str());
                if (n >= 1 && n <= kMaxLevelNumber) {
                    pendingLevelNumber = n;
                    const std::string path = levelsDir + LevelFileName(pendingLevelNumber);
                    if (std::filesystem::exists(path)) {
                        mode = EditorMode::ConfirmOverwrite;
                        statusMessage = TextFormat("Ya existe un nivel %d guardado. Reemplazar? (S/N)", pendingLevelNumber);
                    } else {
                        SaveCurrentCanvas(pendingLevelNumber);
                        mode = EditorMode::ConfirmSaveAnother;
                        statusMessage = TextFormat("Nivel %d guardado. Queres hacer otro? (S/N)", pendingLevelNumber);
                    }
                } else {
                    statusMessage = "Numero invalido: tiene que ser entre 1 y 40.";
                    levelNumberInput.clear();
                }
            }
        } else if (mode == EditorMode::ConfirmOverwrite) {
            const bool yes = IsKeyPressed(KEY_S) || IsKeyPressed(KEY_Y) || Clicked(yesButtonRect, mouse);
            const bool no = IsKeyPressed(KEY_N) || IsKeyPressed(KEY_ESCAPE) || Clicked(noButtonRect, mouse);
            if (yes) {
                SaveCurrentCanvas(pendingLevelNumber);
                mode = EditorMode::ConfirmSaveAnother;
                statusMessage = TextFormat("Nivel %d reemplazado. Queres hacer otro? (S/N)", pendingLevelNumber);
            } else if (no) {
                mode = EditorMode::Editing;
                statusMessage = "Guardado cancelado (no se reemplazo el nivel existente).";
            }
        } else if (mode == EditorMode::ConfirmSaveAnother) {
            const bool yes = IsKeyPressed(KEY_S) || IsKeyPressed(KEY_Y) || Clicked(yesButtonRect, mouse);
            const bool no = IsKeyPressed(KEY_N) || IsKeyPressed(KEY_ESCAPE) || Clicked(noButtonRect, mouse);
            if (yes) {
                ResetCanvas();
                mode = EditorMode::Editing;
                statusMessage = "Lienzo limpio: arranca un nivel nuevo.";
            } else if (no) {
                mode = EditorMode::Editing;
                statusMessage = "Listo. Podes seguir editando este mismo mapa o cerrar la ventana.";
            }
        } else if (mode == EditorMode::LevelBrowser) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                mode = EditorMode::Editing;
            }
            if (IsKeyPressed(KEY_LEFT) && browsingLevelNumber > 1) {
                --browsingLevelNumber;
                RefreshBrowsingLevel();
            }
            if (IsKeyPressed(KEY_RIGHT) && browsingLevelNumber < kMaxLevelNumber) {
                ++browsingLevelNumber;
                RefreshBrowsingLevel();
            }
            if (browsingLevelExists && Clicked(deleteButtonRect, mouse)) {
                mode = EditorMode::ConfirmDeleteLevel;
            }
            if (browsingLevelExists && Clicked(editButtonRect, mouse)) {
                LoadLevelIntoCanvas(browsingLevelData);
                mode = EditorMode::Editing;
                statusMessage = TextFormat("Editando el nivel %d.", browsingLevelNumber);
            }
        } else if (mode == EditorMode::ConfirmDeleteLevel) {
            const bool yes = IsKeyPressed(KEY_S) || IsKeyPressed(KEY_Y) || Clicked(yesButtonRect, mouse);
            const bool no = IsKeyPressed(KEY_N) || IsKeyPressed(KEY_ESCAPE) || Clicked(noButtonRect, mouse);
            if (yes) {
                std::filesystem::remove(levelsDir + LevelFileName(browsingLevelNumber));
                RefreshBrowsingLevel();
                mode = EditorMode::LevelBrowser;
            } else if (no) {
                mode = EditorMode::LevelBrowser;
            }
        }

        // --- Render ---
        BeginDrawing();
        ClearBackground(Color{0x30, 0x30, 0x34, 0xFF});

        // Titulo, fijo arriba de todo.
        DrawRectangle(0, 0, kWindowWidth, kTitleBarHeight, Color{0x20, 0x20, 0x24, 0xFF});
        DrawText("Editor de Niveles (herramienta interna - no se distribuye con el juego)", 10, 8, 16, RAYWHITE);

        // Lienzo: fondo negro + cuadricula + bloques.
        DrawRectangle(static_cast<int>(canvasOffsetX), static_cast<int>(canvasOffsetY), static_cast<int>(mapPixelW * scale), static_cast<int>(mapPixelH * scale), ColorForEmpty());
        for (int y = 0; y < gridHeight; ++y) {
            for (int x = 0; x < gridWidth; ++x) {
                const size_t idx = static_cast<size_t>(y) * gridWidth + x;
                if (cells[idx] == TileType::Empty) {
                    continue; // el fondo negro del lienzo ya lo cubre
                }
                DrawTerrainCell(cells[idx], cellShapes[idx], CellToScreen(x, y), tileScreenSize, WHITE);
            }
        }
        // Marca los puntos de spawn de jugadores/enemigos (no se pueden
        // pintar encima, ver IsProtectedCell; se mueven arrastrando con la
        // herramienta deseleccionada, ver Esc): un circulo chico con la
        // etiqueta correspondiente, resaltado si es el que se esta
        // arrastrando ahora mismo.
        for (size_t i = 0; i < playerSpawns.size(); ++i) {
            const Vector2 center = SpawnScreenCenter(playerSpawns[i][0], playerSpawns[i][1]);
            const bool dragging = (draggingKind == SpawnKind::Player && draggingIndex == i);
            DrawCircleV(center, tileScreenSize * 0.35f, Color{0x30, 0xA0, 0xE0, 0xC0});
            if (dragging) {
                DrawCircleLinesV(center, tileScreenSize * 0.4f, YELLOW);
            }
            const char* label = TextFormat("P%d", static_cast<int>(i) + 1);
            DrawText(label, static_cast<int>(center.x - MeasureText(label, 10) * 0.5f), static_cast<int>(center.y - 5), 10, BLACK);
        }
        for (size_t i = 0; i < enemySpawns.size(); ++i) {
            const Vector2 center = SpawnScreenCenter(enemySpawns[i][0], enemySpawns[i][1]);
            const bool dragging = (draggingKind == SpawnKind::Enemy && draggingIndex == i);
            DrawCircleV(center, tileScreenSize * 0.35f, Color{0xE0, 0x50, 0x20, 0xC0});
            if (dragging) {
                DrawCircleLinesV(center, tileScreenSize * 0.4f, YELLOW);
            }
            DrawText("E", static_cast<int>(center.x - MeasureText("E", 10) * 0.5f), static_cast<int>(center.y - 5), 10, BLACK);
        }
        // Cuadricula, encima de los bloques (lineas finas para guiar sin tapar el contenido).
        for (int x = 0; x <= gridWidth; ++x) {
            const float lx = canvasOffsetX + x * tileScreenSize;
            DrawLine(static_cast<int>(lx), static_cast<int>(canvasOffsetY), static_cast<int>(lx), static_cast<int>(canvasOffsetY + mapPixelH * scale), Color{255, 255, 255, 40});
        }
        for (int y = 0; y <= gridHeight; ++y) {
            const float ly = canvasOffsetY + y * tileScreenSize;
            DrawLine(static_cast<int>(canvasOffsetX), static_cast<int>(ly), static_cast<int>(canvasOffsetX + mapPixelW * scale), static_cast<int>(ly), Color{255, 255, 255, 40});
        }
        // Resalta la celda bajo el mouse y (con la herramienta activa)
        // previsualiza ahi lo que se colocaria (tipo + forma seleccionados),
        // semitransparente, para distinguir bien las mitades/esquinas de
        // Ladrillo/Hierro antes de hacer clic.
        if (mode == EditorMode::Editing) {
            int hoverX = 0, hoverY = 0;
            if (ScreenToCell(mouse, hoverX, hoverY)) {
                const Vector2 p = CellToScreen(hoverX, hoverY);
                if (toolActive && !IsProtectedCell(hoverX, hoverY)) {
                    DrawTerrainCell(selectedType, CombineShape(selectedVertical, selectedHorizontal), p, tileScreenSize, Color{255, 255, 255, 140});
                }
                DrawRectangleLines(static_cast<int>(p.x), static_cast<int>(p.y), static_cast<int>(tileScreenSize), static_cast<int>(tileScreenSize), YELLOW);
            }
        }

        // Paleta.
        DrawRectangle(canvasAreaW, kTitleBarHeight, kSidebarWidth, kWindowHeight - kTitleBarHeight, Color{0x24, 0x24, 0x28, 0xFF});
        for (int i = 0; i < kPaletteCount; ++i) {
            const TileType type = kPaletteTypes[i];
            const Rectangle& r = paletteRects[i];
            const bool selected = (type == selectedType);
            const bool hovered = CheckCollisionPointRec(mouse, r);
            DrawRectangleRec(r, selected ? Color{0x50, 0x50, 0x20, 0xFF} : (hovered ? Color{0x38, 0x38, 0x3C, 0xFF} : Color{0x2C, 0x2C, 0x30, 0xFF}));
            DrawRectangleLinesEx(r, selected ? 2.0f : 1.0f, selected ? YELLOW : GRAY);

            constexpr float kIconSize = 40.0f;
            const Rectangle iconDst{r.x + 8, r.y + (r.height - kIconSize) * 0.5f, kIconSize, kIconSize};
            switch (type) {
                case TileType::Brick: {
                    const Rectangle src{0, 0, static_cast<float>(brickUnitTextures[0].width), static_cast<float>(brickUnitTextures[0].height)};
                    DrawTexturePro(brickUnitTextures[0], src, iconDst, Vector2{0, 0}, 0.0f, WHITE);
                    break;
                }
                case TileType::Steel: {
                    const Rectangle src{0, 0, static_cast<float>(steelUnitTexture.width), static_cast<float>(steelUnitTexture.height)};
                    DrawTexturePro(steelUnitTexture, src, iconDst, Vector2{0, 0}, 0.0f, WHITE);
                    break;
                }
                case TileType::Water: {
                    const Rectangle src{0, 0, static_cast<float>(waterTexture.width), static_cast<float>(waterTexture.height)};
                    DrawTexturePro(waterTexture, src, iconDst, Vector2{0, 0}, 0.0f, WHITE);
                    break;
                }
                case TileType::Trees: {
                    const Rectangle src{0, 0, static_cast<float>(treesTexture.width), static_cast<float>(treesTexture.height)};
                    DrawTexturePro(treesTexture, src, iconDst, Vector2{0, 0}, 0.0f, WHITE);
                    break;
                }
                case TileType::Ice: {
                    const Rectangle src{0, 0, static_cast<float>(iceTexture.width), static_cast<float>(iceTexture.height)};
                    DrawTexturePro(iceTexture, src, iconDst, Vector2{0, 0}, 0.0f, WHITE);
                    break;
                }
                default:
                    DrawRectangleRec(iconDst, ColorForEmpty());
                    DrawRectangleLinesEx(iconDst, 1.0f, GRAY);
                    break;
            }
            DrawText(LabelForType(type), static_cast<int>(r.x + 8 + kIconSize + 8), static_cast<int>(r.y + r.height * 0.5f - 8), 16, RAYWHITE);
        }

        // Forma actual (solo Ladrillo/Hierro): flechas para cambiarla.
        if (TypeHasShapes(selectedType)) {
            const char* shapeText = TextFormat("Forma: %s (flechas)", LabelForShape(CombineShape(selectedVertical, selectedHorizontal)));
            DrawText(shapeText, paletteX, shapeLabelY, 14, YELLOW);
        }

        // Boton Guardar.
        const bool saveHovered = CheckCollisionPointRec(mouse, saveButtonRect);
        DrawRectangleRec(saveButtonRect, saveHovered ? Color{0x30, 0x70, 0x30, 0xFF} : Color{0x24, 0x50, 0x24, 0xFF});
        DrawRectangleLinesEx(saveButtonRect, 1.0f, GREEN);
        DrawText("Guardar", static_cast<int>(saveButtonRect.x + saveButtonRect.width * 0.5f - MeasureText("Guardar", 20) * 0.5f), static_cast<int>(saveButtonRect.y + 12), 20, RAYWHITE);

        // Boton Niveles (abre el navegador de niveles guardados).
        const bool browseHovered = CheckCollisionPointRec(mouse, browseButtonRect);
        DrawRectangleRec(browseButtonRect, browseHovered ? Color{0x30, 0x50, 0x70, 0xFF} : Color{0x24, 0x38, 0x50, 0xFF});
        DrawRectangleLinesEx(browseButtonRect, 1.0f, SKYBLUE);
        DrawText("Niveles", static_cast<int>(browseButtonRect.x + browseButtonRect.width * 0.5f - MeasureText("Niveles", 20) * 0.5f), static_cast<int>(browseButtonRect.y + 12), 20, RAYWHITE);

        // Barra de estado, abajo del todo, ancho completo (debajo del lienzo y la paleta).
        DrawRectangle(0, kWindowHeight - 30, kWindowWidth, 30, Color{0x20, 0x20, 0x24, 0xFF});
        DrawText(statusMessage.c_str(), 10, kWindowHeight - 24, 18, RAYWHITE);

        // Navegador de niveles (panel grande con preview), y su confirmacion
        // de borrado anidada encima.
        if (mode == EditorMode::LevelBrowser || mode == EditorMode::ConfirmDeleteLevel) {
            DrawRectangle(0, 0, kWindowWidth, kWindowHeight, Color{0, 0, 0, 150});
            DrawRectangle(kBrowserX, kBrowserY, kBrowserW, kBrowserH, Color{0x24, 0x24, 0x28, 0xFF});
            DrawRectangleLinesEx(Rectangle{static_cast<float>(kBrowserX), static_cast<float>(kBrowserY), static_cast<float>(kBrowserW), static_cast<float>(kBrowserH)}, 2.0f, SKYBLUE);

            const char* titleText = TextFormat("Nivel %d / %d%s", browsingLevelNumber, kMaxLevelNumber, browsingLevelExists ? "" : "  (no existe)");
            DrawText(titleText, kBrowserX + 20, kBrowserY + 16, 24, browsingLevelExists ? RAYWHITE : GRAY);

            // Vista previa: fondo negro + el mapa guardado, a una escala mas
            // chica que el lienzo principal (misma funcion de dibujo,
            // DrawTerrainCell, para que se vea igual).
            DrawRectangle(kBrowserPreviewX, kBrowserPreviewY, kBrowserPreviewW, kBrowserPreviewH, ColorForEmpty());
            DrawRectangleLines(kBrowserPreviewX, kBrowserPreviewY, kBrowserPreviewW, kBrowserPreviewH, GRAY);
            if (browsingLevelExists) {
                const float previewScale = std::min(static_cast<float>(kBrowserPreviewW) / (browsingLevelData.width * kBaseTileSize), static_cast<float>(kBrowserPreviewH) / (browsingLevelData.height * kBaseTileSize));
                const float previewTileSize = kBaseTileSize * previewScale;
                const float previewOffsetX = kBrowserPreviewX + (kBrowserPreviewW - browsingLevelData.width * previewTileSize) * 0.5f;
                const float previewOffsetY = kBrowserPreviewY + (kBrowserPreviewH - browsingLevelData.height * previewTileSize) * 0.5f;
                for (int y = 0; y < browsingLevelData.height; ++y) {
                    const bool hasRow = y < static_cast<int>(browsingLevelData.tiles.size());
                    for (int x = 0; x < browsingLevelData.width; ++x) {
                        const char tileChar = (hasRow && x < static_cast<int>(browsingLevelData.tiles[y].size())) ? browsingLevelData.tiles[y][x] : '.';
                        const TileType type = TileTypeFromChar(tileChar);
                        if (type == TileType::Empty) {
                            continue;
                        }
                        char shapeChar = 'F';
                        if (y < static_cast<int>(browsingLevelData.block_shapes.size()) && x < static_cast<int>(browsingLevelData.block_shapes[y].size())) {
                            shapeChar = browsingLevelData.block_shapes[y][x];
                        }
                        const Vector2 p{previewOffsetX + x * previewTileSize, previewOffsetY + y * previewTileSize};
                        DrawTerrainCell(type, BlockShapeFromChar(shapeChar), p, previewTileSize, WHITE);
                    }
                }
            } else {
                const char* emptyText = "Todavia no se guardo nada en este numero.";
                DrawText(emptyText, kBrowserPreviewX + (kBrowserPreviewW - MeasureText(emptyText, 18)) / 2, kBrowserPreviewY + kBrowserPreviewH / 2 - 9, 18, GRAY);
            }

            DrawText("Flechas izq/der: cambiar de nivel - Esc: volver al editor", kBrowserX + 20, kBrowserPreviewY + kBrowserPreviewH + 12, 16, GRAY);

            const bool deleteHovered = browsingLevelExists && CheckCollisionPointRec(mouse, deleteButtonRect);
            const Color deleteBg = !browsingLevelExists ? Color{0x30, 0x30, 0x30, 0xFF} : (deleteHovered ? Color{0x70, 0x30, 0x30, 0xFF} : Color{0x50, 0x24, 0x24, 0xFF});
            DrawRectangleRec(deleteButtonRect, deleteBg);
            DrawRectangleLinesEx(deleteButtonRect, 1.0f, browsingLevelExists ? RED : GRAY);
            DrawText("Borrar", static_cast<int>(deleteButtonRect.x + deleteButtonRect.width * 0.5f - MeasureText("Borrar", 20) * 0.5f), static_cast<int>(deleteButtonRect.y + 12), 20, browsingLevelExists ? RAYWHITE : GRAY);

            const bool editHovered = browsingLevelExists && CheckCollisionPointRec(mouse, editButtonRect);
            const Color editBg = !browsingLevelExists ? Color{0x30, 0x30, 0x30, 0xFF} : (editHovered ? Color{0x30, 0x70, 0x30, 0xFF} : Color{0x24, 0x50, 0x24, 0xFF});
            DrawRectangleRec(editButtonRect, editBg);
            DrawRectangleLinesEx(editButtonRect, 1.0f, browsingLevelExists ? GREEN : GRAY);
            DrawText("Editar", static_cast<int>(editButtonRect.x + editButtonRect.width * 0.5f - MeasureText("Editar", 20) * 0.5f), static_cast<int>(editButtonRect.y + 12), 20, browsingLevelExists ? RAYWHITE : GRAY);

            // Confirmacion de borrado, anidada encima del navegador.
            if (mode == EditorMode::ConfirmDeleteLevel) {
                DrawRectangle(0, 0, kWindowWidth, kWindowHeight, Color{0, 0, 0, 100});
                DrawRectangle(kDialogX, kDialogY, kDialogW, kDialogH, Color{0x24, 0x24, 0x28, 0xFF});
                DrawRectangleLinesEx(Rectangle{static_cast<float>(kDialogX), static_cast<float>(kDialogY), static_cast<float>(kDialogW), static_cast<float>(kDialogH)}, 2.0f, RED);
                const char* confirmText = TextFormat("Borrar el nivel %d? No se puede deshacer.", browsingLevelNumber);
                DrawText(confirmText, kDialogX + 20, kDialogY + 20, 18, RAYWHITE);
                const bool yesHover = CheckCollisionPointRec(mouse, yesButtonRect);
                const bool noHover = CheckCollisionPointRec(mouse, noButtonRect);
                DrawRectangleRec(yesButtonRect, yesHover ? Color{0x30, 0x70, 0x30, 0xFF} : Color{0x24, 0x50, 0x24, 0xFF});
                DrawRectangleLinesEx(yesButtonRect, 1.0f, GREEN);
                DrawText("Si (S)", static_cast<int>(yesButtonRect.x + 30), static_cast<int>(yesButtonRect.y + 10), 20, RAYWHITE);
                DrawRectangleRec(noButtonRect, noHover ? Color{0x70, 0x30, 0x30, 0xFF} : Color{0x50, 0x24, 0x24, 0xFF});
                DrawRectangleLinesEx(noButtonRect, 1.0f, RED);
                DrawText("No (N)", static_cast<int>(noButtonRect.x + 30), static_cast<int>(noButtonRect.y + 10), 20, RAYWHITE);
            }
        } else if (mode != EditorMode::Editing) {
            // Overlay de prompt (numero de nivel / confirmaciones de
            // guardado), centrado, encima de todo.
            DrawRectangle(0, 0, kWindowWidth, kWindowHeight, Color{0, 0, 0, 150});
            DrawRectangle(kDialogX, kDialogY, kDialogW, kDialogH, Color{0x24, 0x24, 0x28, 0xFF});
            DrawRectangleLinesEx(Rectangle{static_cast<float>(kDialogX), static_cast<float>(kDialogY), static_cast<float>(kDialogW), static_cast<float>(kDialogH)}, 2.0f, YELLOW);

            if (mode == EditorMode::EnteringLevelNumber) {
                DrawText("Que nivel del juego es? (1-40)", kDialogX + 20, kDialogY + 20, 20, RAYWHITE);
                const std::string display = levelNumberInput + "_";
                DrawText(display.c_str(), kDialogX + 20, kDialogY + 60, 32, YELLOW);
                DrawText("Enter para confirmar - Esc para cancelar", kDialogX + 20, kDialogY + 105, 16, GRAY);
            } else {
                DrawText(statusMessage.c_str(), kDialogX + 20, kDialogY + 20, 18, RAYWHITE);
                const bool yesHover = CheckCollisionPointRec(mouse, yesButtonRect);
                const bool noHover = CheckCollisionPointRec(mouse, noButtonRect);
                DrawRectangleRec(yesButtonRect, yesHover ? Color{0x30, 0x70, 0x30, 0xFF} : Color{0x24, 0x50, 0x24, 0xFF});
                DrawRectangleLinesEx(yesButtonRect, 1.0f, GREEN);
                DrawText("Si (S)", static_cast<int>(yesButtonRect.x + 30), static_cast<int>(yesButtonRect.y + 10), 20, RAYWHITE);
                DrawRectangleRec(noButtonRect, noHover ? Color{0x70, 0x30, 0x30, 0xFF} : Color{0x50, 0x24, 0x24, 0xFF});
                DrawRectangleLinesEx(noButtonRect, 1.0f, RED);
                DrawText("No (N)", static_cast<int>(noButtonRect.x + 30), static_cast<int>(noButtonRect.y + 10), 20, RAYWHITE);
            }
        }

        EndDrawing();
    }

    UnloadTexture(brickUnitTextures[0]);
    UnloadTexture(brickUnitTextures[1]);
    UnloadTexture(steelUnitTexture);
    UnloadTexture(waterTexture);
    UnloadTexture(treesTexture);
    UnloadTexture(iceTexture);
    UnloadTexture(baseEagleTexture);
    CloseWindow();
    return 0;
}
