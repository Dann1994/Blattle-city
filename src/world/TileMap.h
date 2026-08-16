#pragma once

#include <array>
#include <vector>

#include "BrickUnit.h"
#include "LevelFormat.h"
#include "TileTypes.h"

namespace bc {

struct Cell {
    TileType type = TileType::Empty;
    // Solo tiene sentido cuando type == Brick (ver BrickUnit.h): grilla 4x4
    // de unidades minimas, indexada como fila*kBrickGridSize + columna.
    std::array<BrickUnit, kBrickUnitCount> brickUnits{};

    bool BrickFullyDestroyed() const {
        for (const BrickUnit& unit : brickUnits) {
            if (unit.alive) {
                return false;
            }
        }
        return true;
    }
};

class TileMap {
public:
    void LoadFrom(const LevelData& level);

    int Width() const { return width_; }
    int Height() const { return height_; }

    const Cell& At(int x, int y) const { return cells_[y * width_ + x]; }
    Cell& At(int x, int y) { return cells_[y * width_ + x]; }

    bool InBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }

    // Colision por unidad minima: una caja (en celdas, coordenadas de mapa)
    // esta bloqueada si se solapa con algun tile solido no-ladrillo, o con
    // alguna unidad de ladrillo todavia viva. Las unidades ya destruidas
    // dejan pasar, aunque el resto de la celda siga en pie.
    bool IsBoxBlocked(float left, float right, float top, float bottom) const;

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<Cell> cells_;
};

} // namespace bc
