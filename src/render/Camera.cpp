#include "Camera.h"

#include <algorithm>

namespace bc {

MapViewport MapViewport::Compute(int windowWidth, int windowHeight, int mapWidthCells, int mapHeightCells, int baseTileSize) {
    const float mapPixelWidth = static_cast<float>(mapWidthCells * baseTileSize);
    const float mapPixelHeight = static_cast<float>(mapHeightCells * baseTileSize);

    const float scaleX = static_cast<float>(windowWidth) / mapPixelWidth;
    const float scaleY = static_cast<float>(windowHeight) / mapPixelHeight;
    const float scale = std::min(scaleX, scaleY);

    const float scaledWidth = mapPixelWidth * scale;
    const float scaledHeight = mapPixelHeight * scale;

    MapViewport viewport;
    viewport.tileScreenSize = static_cast<float>(baseTileSize) * scale;
    viewport.offsetX = (static_cast<float>(windowWidth) - scaledWidth) * 0.5f;
    viewport.offsetY = (static_cast<float>(windowHeight) - scaledHeight) * 0.5f;
    return viewport;
}

} // namespace bc
