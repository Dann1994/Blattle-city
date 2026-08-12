#pragma once

#include <cstdint>
#include <vector>

#include "LevelFormat.h"
#include "TileTypes.h"

namespace bc {

struct Cell {
    TileType type = TileType::Empty;
    uint8_t subMask = 0; // ver kSubCellMaskFull en TileTypes.h
};

class TileMap {
public:
    void LoadFrom(const LevelData& level);

    int Width() const { return width_; }
    int Height() const { return height_; }

    const Cell& At(int x, int y) const { return cells_[y * width_ + x]; }
    Cell& At(int x, int y) { return cells_[y * width_ + x]; }

    bool InBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<Cell> cells_;
};

} // namespace bc
