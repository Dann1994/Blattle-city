#pragma once

#include "TileMap.h"

namespace bc {

enum class Direction { Up, Down, Left, Right };

inline void DirectionVector(Direction dir, float& outDx, float& outDy) {
    switch (dir) {
        case Direction::Up:    outDx = 0.0f;  outDy = -1.0f; break;
        case Direction::Down:  outDx = 0.0f;  outDy = 1.0f;  break;
        case Direction::Left:  outDx = -1.0f; outDy = 0.0f;  break;
        case Direction::Right: outDx = 1.0f;  outDy = 0.0f;  break;
    }
}

// Ver seccion 12.4: capa de input unica, sin distinguir origen local/remoto.
struct PlayerInput {
    bool moveUp = false;
    bool moveDown = false;
    bool moveLeft = false;
    bool moveRight = false;
    bool shoot = false;
};

// SinglePress: un disparo por cada pulsacion del boton (por defecto, como el original).
// HoldToFire: dispara en cada oportunidad mientras el boton se mantiene apretado.
// Pensado para exponerse como opcion de configuracion (ver seccion 12.5).
enum class FireMode { SinglePress, HoldToFire };

class Tank {
public:
    void SetPosition(float cellX, float cellY) { x_ = cellX; y_ = cellY; }
    void Update(double dt, const PlayerInput& input, const TileMap& map);

    float X() const { return x_; }
    float Y() const { return y_; }
    Direction Facing() const { return facing_; }
    int AnimFrame() const { return animFrame_; }

    // Punto desde donde salen las balas: el borde del tanque en la direccion que mira.
    void MuzzlePosition(float& outX, float& outY) const;

    void SetFireMode(FireMode mode) { fireMode_ = mode; }
    FireMode GetFireMode() const { return fireMode_; }

    // Devuelve true en el frame exacto en que corresponde disparar, segun fireMode_.
    // Hay que llamarla una vez por frame (consume el estado previo del boton).
    bool ConsumeShootTrigger(const PlayerInput& input);

    // Niveles de mejora del power-up Estrella (seccion 4.3): 1 (base) a 4 (maximo).
    int WeaponLevel() const { return weaponLevel_; }
    void UpgradeWeapon();
    void ResetWeaponLevel() { weaponLevel_ = 1; }

    float BulletSpeed() const;
    int MaxBullets() const;       // 1 salvo nivel 3+ (doble disparo)
    bool CanDestroySteel() const; // desde nivel 3

    // Escudo (power-up Casco / proteccion al aparecer, seccion 6). La duracion
    // la decide quien llama (distinta si es respawn o si se agarro el item).
    void ActivateShield(double durationSeconds) { shieldTimer_ = durationSeconds; }
    void TickShield(double dt);
    bool IsShielded() const { return shieldTimer_ > 0.0; }
    double ShieldSecondsRemaining() const { return shieldTimer_; }

private:
    bool TryMove(float dx, float dy, const TileMap& map);
    bool TryMoveWithAssist(float dx, float dy, const TileMap& map);
    bool TrySlidePerpendicularY(float dx, const TileMap& map);
    bool TrySlidePerpendicularX(float dy, const TileMap& map);

    float x_ = 0.0f;
    float y_ = 0.0f;
    Direction facing_ = Direction::Up;
    float speed_ = 3.5f; // celdas por segundo
    double animTimer_ = 0.0;
    int animFrame_ = 0;
    FireMode fireMode_ = FireMode::SinglePress;
    bool shootHeldLastFrame_ = false;
    int weaponLevel_ = 1;
    double shieldTimer_ = 0.0;
};

} // namespace bc
