#include "TileMap.h"

#include <cmath>

namespace bc {

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
                for (int r = 0; r < kBrickGridSize; ++r) {
                    for (int c = 0; c < kBrickGridSize; ++c) {
                        BrickUnit& unit = cell.brickUnits[r * kBrickGridSize + c];
                        unit.alive = true;
                        unit.frame = BrickUnit::FrameFor(r, c);
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

    constexpr float kUnitSize = 1.0f / kBrickGridSize;

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
                        const float unitLeft = cx + c * kUnitSize;
                        const float unitTop = cy + r * kUnitSize;
                        if (left < unitLeft + kUnitSize && right > unitLeft && top < unitTop + kUnitSize && bottom > unitTop) {
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
