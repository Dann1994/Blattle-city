#pragma once

#include <algorithm>

namespace bc {

constexpr int kSteelGridSize = 2;
constexpr int kSteelUnitCount = kSteelGridSize * kSteelGridSize; // 4

// Resistencia de cada unidad de hierro por nivel de arma de quien dispara
// (pedido explicitamente: "Nivel 1 requiere 30 disparos para romper, Nivel
// 2 35 disparos, Nivel 3 15 disparos, Nivel 4 1 disparo, Bala especial 1
// disparo"). kSteelUnitMaxHp usa el MCM de 30/35/15 (210) para que cada
// nivel rompa la unidad en exactamente esa cantidad de impactos PROPIOS,
// incluso si se mezclan niveles distintos contra la misma unidad a lo largo
// del tiempo (la resta de hp de cada impacto es proporcional al nivel:
// 210/30=7, 210/35=6, 210/15=14). Nivel 4 resta el total de una (1 disparo);
// la bala especial tambien la rompe de 1 (ver kSteelUnitMaxHp en
// BulletSystem.cpp) y ademas explota al impactar, como cualquier obstaculo
// solido.
constexpr int kSteelUnitMaxHp = 210;
constexpr int kSteelDamageByLevel[4] = {7, 6, 14, kSteelUnitMaxHp};

inline int SteelDamageForLevel(int weaponLevel) {
    return kSteelDamageByLevel[std::clamp(weaponLevel, 1, 4) - 1];
}

struct SteelUnit {
    bool alive = true;
    int hp = kSteelUnitMaxHp;
};

} // namespace bc
