#pragma once

#include <cstdint>

namespace bc {

enum class TileType : uint8_t {
    Empty,
    Brick,
    Steel,
    Water,
    Trees,
    Ice,
    Base,
};

inline TileType TileTypeFromChar(char c) {
    switch (c) {
        case 'B': return TileType::Brick;
        case 'S': return TileType::Steel;
        case 'W': return TileType::Water;
        case 'T': return TileType::Trees;
        case 'I': return TileType::Ice;
        case 'E': return TileType::Base;
        default:  return TileType::Empty;
    }
}

inline char TileTypeToChar(TileType t) {
    switch (t) {
        case TileType::Brick: return 'B';
        case TileType::Steel: return 'S';
        case TileType::Water: return 'W';
        case TileType::Trees: return 'T';
        case TileType::Ice:   return 'I';
        case TileType::Base:  return 'E';
        default:              return '.';
    }
}

// Mascara de 2x2 sub-celdas (bit 0=arriba-izq, 1=arriba-der, 2=abajo-izq, 3=abajo-der).
// Ver seccion 4.1 del documento de diseno.
constexpr uint8_t kSubCellMaskFull = 0b1111;
constexpr uint8_t kSubCellTopLeft = 1 << 0;
constexpr uint8_t kSubCellTopRight = 1 << 1;
constexpr uint8_t kSubCellBottomLeft = 1 << 2;
constexpr uint8_t kSubCellBottomRight = 1 << 3;
constexpr uint8_t kSubCellLeftHalf = kSubCellTopLeft | kSubCellBottomLeft;
constexpr uint8_t kSubCellRightHalf = kSubCellTopRight | kSubCellBottomRight;
constexpr uint8_t kSubCellTopHalf = kSubCellTopLeft | kSubCellTopRight;
constexpr uint8_t kSubCellBottomHalf = kSubCellBottomLeft | kSubCellBottomRight;

inline bool TileBlocksMovement(TileType t) {
    return t == TileType::Brick || t == TileType::Steel || t == TileType::Water || t == TileType::Base;
}

inline bool TileBlocksShots(TileType t) {
    return t == TileType::Brick || t == TileType::Steel || t == TileType::Base;
}

inline bool TileIsDestructible(TileType t) {
    return t == TileType::Brick;
}

} // namespace bc
