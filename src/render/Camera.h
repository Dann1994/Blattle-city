#pragma once

namespace bc {

// Encaja el mapa (ancho x alto en celdas) dentro de la ventana manteniendo su
// relacion de aspecto real, con pilarboxing/letterboxing automatico (seccion 12.6).
struct MapViewport {
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float tileScreenSize = 0.0f; // tamano en pixeles de pantalla de una celda ya escalada

    static MapViewport Compute(int windowWidth, int windowHeight, int mapWidthCells, int mapHeightCells, int baseTileSize);

    float TileToScreenX(int cellX) const { return offsetX + static_cast<float>(cellX) * tileScreenSize; }
    float TileToScreenY(int cellY) const { return offsetY + static_cast<float>(cellY) * tileScreenSize; }
};

} // namespace bc
