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
