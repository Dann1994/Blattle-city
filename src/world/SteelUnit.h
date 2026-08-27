#pragma once

namespace bc {

constexpr int kSteelGridSize = 2;
constexpr int kSteelUnitCount = kSteelGridSize * kSteelGridSize; // 4

// Resistencia base de cada unidad de hierro (pedido explicitamente:
// "Hierro salud X4"): el dano por disparo es la potencia real de quien
// dispara (ver kBulletPowerByLevel en Bullet.h, restada directo en
// HandleSteelHit), asi que una unidad se rompe de 1 a 4 impactos segun el
// nivel de arma (1 con potencia 4, hasta 4 con potencia 1).
constexpr int kSteelUnitMaxHp = 4;

struct SteelUnit {
    bool alive = true;
    int hp = kSteelUnitMaxHp;
};

} // namespace bc
