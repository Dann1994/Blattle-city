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

// Forma parcial de una celda de Ladrillo/Hierro (construction mode del
// original): ademas del bloque completo, se puede colocar solo la mitad
// pegada a uno de los 4 lados de la celda (2 de las 4 columnas/filas en
// Ladrillo, 1 de las 2 en Hierro), o solo una esquina — un cuarto del
// bloque (2x2 unidades en Ladrillo, exactamente 1 unidad en Hierro, ya que
// ahi cada unidad ya ES un cuarto de la celda). Ver IsUnitAliveForShape. No
// tiene sentido para el resto de los tipos, que siempre son Full.
enum class BlockShape : uint8_t { Full, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };

inline BlockShape BlockShapeFromChar(char c) {
    switch (c) {
        case 'L': return BlockShape::Left;
        case 'R': return BlockShape::Right;
        case 'U': return BlockShape::Top;
        case 'D': return BlockShape::Bottom;
        case '1': return BlockShape::TopLeft;
        case '2': return BlockShape::TopRight;
        case '3': return BlockShape::BottomLeft;
        case '4': return BlockShape::BottomRight;
        default:  return BlockShape::Full;
    }
}

inline char BlockShapeToChar(BlockShape shape) {
    switch (shape) {
        case BlockShape::Left:        return 'L';
        case BlockShape::Right:       return 'R';
        case BlockShape::Top:         return 'U';
        case BlockShape::Bottom:      return 'D';
        case BlockShape::TopLeft:     return '1';
        case BlockShape::TopRight:    return '2';
        case BlockShape::BottomLeft:  return '3';
        case BlockShape::BottomRight: return '4';
        default:                      return 'F';
    }
}

// Si la unidad en (row, col) de una grilla gridSize x gridSize (4 para
// Ladrillo, 2 para Hierro) deberia arrancar viva para la forma dada. Usada
// tanto por TileMap::LoadFrom (juego real) como por el editor (preview y
// render de lo ya colocado), para que las dos coincidan siempre.
inline bool IsUnitAliveForShape(BlockShape shape, int row, int col, int gridSize) {
    const int half = gridSize / 2;
    switch (shape) {
        case BlockShape::Left:        return col < half;
        case BlockShape::Right:       return col >= half;
        case BlockShape::Top:         return row < half;
        case BlockShape::Bottom:      return row >= half;
        case BlockShape::TopLeft:     return row < half && col < half;
        case BlockShape::TopRight:    return row < half && col >= half;
        case BlockShape::BottomLeft:  return row >= half && col < half;
        case BlockShape::BottomRight: return row >= half && col >= half;
        default:                      return true; // Full
    }
}

} // namespace bc
