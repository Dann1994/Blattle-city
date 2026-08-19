#include "TileMap.h"

#include <cmath>

namespace bc {

namespace {
// 'F' (o el campo ausente/mas corto que la grilla, en niveles guardados
// antes de este campo existir) equivale a Full.
BlockShape ShapeAt(const LevelData& level, int x, int y) {
    if (y >= static_cast<int>(level.block_shapes.size())) {
        return BlockShape::Full;
    }
    const std::string& row = level.block_shapes[y];
    if (x >= static_cast<int>(row.size())) {
        return BlockShape::Full;
    }
    return BlockShapeFromChar(row[x]);
}
} // namespace

void TileMap::LoadFrom(const LevelData& level) {
    width_ = level.width;
    height_ = level.height;
    cells_.assign(static_cast<size_t>(width_) * height_, Cell{});

    for (int y = 0; y < height_; ++y) {
        const std::string& row = level.tiles[y];
        for (int x = 0; x < width_; ++x) {
            const TileType type = TileTypeFromChar(row[x]);
            Cell& cell = At(x, y);
            cell.type = type;
            if (type == TileType::Brick) {
                const BlockShape shape = ShapeAt(level, x, y);
                for (int r = 0; r < kBrickGridSize; ++r) {
                    for (int c = 0; c < kBrickGridSize; ++c) {
                        BrickUnit& unit = cell.brickUnits[r * kBrickGridSize + c];
                        unit.alive = IsUnitAliveForShape(shape, r, c, kBrickGridSize);
                        unit.frame = BrickUnit::FrameFor(r, c);
                    }
                }
            } else if (type == TileType::Steel) {
                const BlockShape shape = ShapeAt(level, x, y);
                for (int r = 0; r < kSteelGridSize; ++r) {
                    for (int c = 0; c < kSteelGridSize; ++c) {
                        SteelUnit& unit = cell.steelUnits[r * kSteelGridSize + c];
                        unit.alive = IsUnitAliveForShape(shape, r, c, kSteelGridSize);
                        unit.hp = kSteelUnitMaxHp;
                    }
                }
            }
        }
    }

    if (level.base_position[0] >= 0 && level.base_position[1] >= 0 &&
        InBounds(level.base_position[0], level.base_position[1])) {
        At(level.base_position[0], level.base_position[1]).type = TileType::Base;
    }
}

bool TileMap::IsBoxBlocked(float left, float right, float top, float bottom) const {
    const int minCellX = static_cast<int>(std::floor(left));
    const int maxCellX = static_cast<int>(std::ceil(right)) - 1;
    const int minCellY = static_cast<int>(std::floor(top));
    const int maxCellY = static_cast<int>(std::ceil(bottom)) - 1;

    constexpr float kBrickUnitSize = 1.0f / kBrickGridSize;
    constexpr float kSteelUnitSize = 1.0f / kSteelGridSize;

    for (int cy = minCellY; cy <= maxCellY; ++cy) {
        for (int cx = minCellX; cx <= maxCellX; ++cx) {
            if (!InBounds(cx, cy)) {
                return true;
            }

            const Cell& cell = At(cx, cy);
            if (cell.type == TileType::Brick) {
                for (int r = 0; r < kBrickGridSize; ++r) {
                    for (int c = 0; c < kBrickGridSize; ++c) {
                        const BrickUnit& unit = cell.brickUnits[r * kBrickGridSize + c];
                        if (!unit.alive) {
                            continue;
                        }
                        const float unitLeft = cx + c * kBrickUnitSize;
                        const float unitTop = cy + r * kBrickUnitSize;
                        if (left < unitLeft + kBrickUnitSize && right > unitLeft && top < unitTop + kBrickUnitSize && bottom > unitTop) {
                            return true;
                        }
                    }
                }
            } else if (cell.type == TileType::Steel) {
                for (int r = 0; r < kSteelGridSize; ++r) {
                    for (int c = 0; c < kSteelGridSize; ++c) {
                        const SteelUnit& unit = cell.steelUnits[r * kSteelGridSize + c];
                        if (!unit.alive) {
                            continue;
                        }
                        const float unitLeft = cx + c * kSteelUnitSize;
                        const float unitTop = cy + r * kSteelUnitSize;
                        if (left < unitLeft + kSteelUnitSize && right > unitLeft && top < unitTop + kSteelUnitSize && bottom > unitTop) {
                            return true;
                        }
                    }
                }
            } else if (TileBlocksMovement(cell.type)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace bc
