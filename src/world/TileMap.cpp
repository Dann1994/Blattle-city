#include "TileMap.h"

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
            cell.subMask = (type == TileType::Brick || type == TileType::Steel) ? kSubCellMaskFull : 0;
        }
    }

    if (level.base_position[0] >= 0 && level.base_position[1] >= 0 &&
        InBounds(level.base_position[0], level.base_position[1])) {
        At(level.base_position[0], level.base_position[1]).type = TileType::Base;
    }
}

} // namespace bc
