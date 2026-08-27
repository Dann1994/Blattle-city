#pragma once

#include "Tank.h"

namespace bc {

// Los jugadores usan ownerId 0-3 (ver Game::kPlayer1Id..kPlayer4Id); los
// tanques enemigo arrancan aca en adelante (uno distinto por tanque), para
// poder distinguir el origen de una bala/impacto de un vistazo sin
// necesitar una lista aparte de "que IDs son enemigos".
constexpr int kEnemyOwnerIdBase = 1000;

struct Bullet {
    float x = 0.0f; // celdas, centro de la bala
    float y = 0.0f;
    Direction direction = Direction::Up;
    int ownerId = -1;
    bool alive = false;
    float speed = 8.0f; // celdas por segundo, depende del nivel de arma de quien dispara
    int weaponLevel = 1; // nivel de quien dispara (1-4): decide potencia/salud (ver kBulletPowerByLevel/kBulletHealthByLevel) y velocidad
    bool isSpecial = false; // disparo especial: veloz, potencia/salud propias (ver kSpecialBulletPower/kSpecialBulletHealth), explota al agotarse
    int health = 1; // sistema de resistencia (pedido explicitamente): cuantos puntos de salud de objetivo puede seguir absorbiendo antes de destruirse ella misma (ver BulletPower/BulletHealthForLevel mas abajo). Se inicializa al disparar y baja en cada impacto segun la salud del objetivo golpeado (no 1 por impacto); al llegar a 0 la bala se destruye (o explota, si es especial).
};

// Sistema de resistencia bala-vs-objeto (pedido explicitamente): cada nivel
// de arma define una POTENCIA (cuanta salud de un objetivo puede consumir de
// un impacto) y una SALUD PROPIA (cuantos puntos de salud de objetivo puede
// seguir absorbiendo en total antes de destruirse ella misma, permitiendole
// atravesar mas de un objeto/tanque en un mismo disparo si le sobra salud
// despues del primer impacto). Misma tabla para jugadores y enemigos: cada
// tipo de enemigo dispara con el weaponLevel fijo que le corresponde (ver
// EnemySystem/FastEnemySystem/ArmorEnemySystem/PowerEnemySystem), asi que
// aplica igual sin necesitar una tabla aparte.
constexpr int kBulletPowerByLevel[4] = {1, 1, 2, 4};
constexpr int kBulletHealthByLevel[4] = {1, 1, 1, 2};

// Disparo especial (nivel 4 + carga de estrella extra): potencia altisima en
// el choque directo, salud propia para poder atravesar varias cosas antes de
// finalmente agotarse y explotar (ver TriggerSpecialExplosion), y una
// potencia de explosion aparte (mas baja que el impacto directo) para el
// area que alcanza al morir.
constexpr int kSpecialBulletPower = 5;
constexpr int kSpecialBulletHealth = 4;
constexpr int kSpecialExplosionPower = 4;

inline int BulletPowerForLevel(int weaponLevel) {
    return kBulletPowerByLevel[std::clamp(weaponLevel, 1, 4) - 1];
}

inline int BulletHealthForLevel(int weaponLevel) {
    return kBulletHealthByLevel[std::clamp(weaponLevel, 1, 4) - 1];
}

// Potencia real de ESTA bala (especial o normal segun su nivel).
inline int BulletPower(const Bullet& bullet) {
    return bullet.isSpecial ? kSpecialBulletPower : BulletPowerForLevel(bullet.weaponLevel);
}

} // namespace bc
