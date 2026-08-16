#pragma once

namespace bc {

constexpr int kSteelGridSize = 2;
constexpr int kSteelUnitCount = kSteelGridSize * kSteelGridSize; // 4

// Resistencia base de cada unidad de hierro; el daño por disparo depende del
// nivel de arma de quien dispara (ver kSteelDamageByLevel en BulletSystem.cpp).
// Con esos valores, romper el bloque entero (2 capas de 2 unidades) a puro
// impacto central toma exactamente 50/76/10/2 disparos en nivel 1/2/3/4
// (76 en vez de los 75 pedidos: 75 no es divisible por 2 capas, asi que
// queda el numero entero mas cercano).
constexpr int kSteelUnitMaxHp = 75;

struct SteelUnit {
    bool alive = true;
    int hp = kSteelUnitMaxHp;
};

} // namespace bc
