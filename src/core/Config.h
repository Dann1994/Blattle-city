#pragma once

namespace bc {

// Tamano base de celda en pixeles (sprites 16x16, ver seccion 13). El render
// escala esto con la camara segun la seccion 12.6, no se usa directo en pantalla.
constexpr int kTileSize = 16;

constexpr double kFixedTimestep = 1.0 / 60.0;

constexpr int kDefaultWindowWidth = 1280;
constexpr int kDefaultWindowHeight = 720;

} // namespace bc
