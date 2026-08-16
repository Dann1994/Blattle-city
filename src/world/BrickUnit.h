#pragma once

namespace bc {

// Unidad minima de un bloque de ladrillo (ver Documentaciones/Ladrillos.png):
// el bloque completo se arma con una grilla de 4x4 de estas unidades. Cada
// una usa uno de dos frames posibles (liso o esquina/junta de mortero,
// brick_unit_0.png / brick_unit_1.png) y se destruye de forma individual al
// recibir un disparo.
struct BrickUnit {
    bool alive = true;
    int frame = 0; // 0 = liso, 1 = esquina/junta

    // El patron real alterna en tablero de ajedrez segun fila/columna dentro
    // de la grilla 4x4 (confirmado comparando los 4 sub-cuadros de referencia).
    static int FrameFor(int row, int col) { return (row + col) % 2; }
};

constexpr int kBrickGridSize = 4;
constexpr int kBrickUnitCount = kBrickGridSize * kBrickGridSize;

} // namespace bc
