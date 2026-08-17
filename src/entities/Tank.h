#pragma once

#include <algorithm>
#include <vector>

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
    bool specialShoot = false; // disparo especial (nivel 4 + carga de estrella extra)
};

// SinglePress: un disparo por cada pulsacion del boton (por defecto, como el original).
// HoldToFire: dispara en cada oportunidad mientras el boton se mantiene apretado.
// Pensado para exponerse como opcion de configuracion (ver seccion 12.5).
enum class FireMode { SinglePress, HoldToFire };

class Tank {
public:
    void SetPosition(float cellX, float cellY) { x_ = cellX; y_ = cellY; }
    void SetFacing(Direction facing) { facing_ = facing; }

    // Multiplicador de velocidad de movimiento (1.0 = normal). Pensado para
    // los tanques enemigo "Basico", mas lentos que los del jugador.
    void SetSpeedMultiplier(float multiplier) { speedMultiplier_ = multiplier; }

    // others son los demas tanques contra los que colisionar (activos, sin
    // contarse a si mismo): no se pueden atravesar entre si, sin excepcion
    // (sin empuje).
    void Update(double dt, const PlayerInput& input, const TileMap& map, const std::vector<Tank*>& others);

    float X() const { return x_; }
    float Y() const { return y_; }
    Direction Facing() const { return facing_; }
    int AnimFrame() const { return animFrame_; }

    // Caja de colision actual (en celdas), para que otro tanque chequee solapamiento.
    void GetBounds(float& outLeft, float& outRight, float& outTop, float& outBottom) const;

    // Punto desde donde salen las balas: el borde del tanque en la direccion que mira.
    void MuzzlePosition(float& outX, float& outY) const;

    // Retroceso progresivo (p.ej. al disparar el especial): arranca un
    // desplazamiento de distanceCells celdas hacia atras (opuesto a donde
    // miraba en el momento de disparar), que se anima solo a lo largo de
    // varios frames via TickRecoil (no es un salto instantaneo). Si algo lo
    // bloquea a mitad de camino (mapa u otro tanque), se corta ahi.
    void StartRecoil(float distanceCells);
    void TickRecoil(double dt, const TileMap& map, const std::vector<Tank*>& others);

    void SetFireMode(FireMode mode) { fireMode_ = mode; }
    FireMode GetFireMode() const { return fireMode_; }

    // Devuelve true en el frame exacto en que corresponde disparar, segun fireMode_.
    // Hay que llamarla una vez por frame (consume el estado previo del boton).
    bool ConsumeShootTrigger(const PlayerInput& input);

    // Niveles de mejora del power-up Estrella (seccion 4.3): 1 (base) a 4 (maximo).
    int WeaponLevel() const { return weaponLevel_; }
    void ResetWeaponLevel() { weaponLevel_ = 1; specialShotCharges_ = 0; chipHitCount_ = 0; }

    // Llamar al agarrar el item Estrella: sube de nivel si no esta al maximo;
    // si ya esta en nivel 4, en cambio carga el disparo especial (maximo 1).
    void PickupStar();

    // Item Pistola (seccion custom): sube directo a nivel 4 y deja cargado
    // el disparo especial de una, sin importar el nivel que tuviera antes.
    // Si ya estaba en nivel 4, esto es lo que habilita el disparo especial.
    void PickupGun();

    float BulletSpeed() const;
    int MaxBullets() const; // 1 salvo nivel 3+ (doble disparo)

    // Disparo especial: solo en nivel 4 y con una carga disponible (se gana
    // al agarrar una estrella estando ya en nivel 4). Su explosion es capaz
    // de dañar al propio tanque, quitandole niveles de arma.
    bool HasSpecialShotReady() const;
    void ConsumeSpecialShot() { specialShotCharges_ = 0; }
    bool ConsumeSpecialShotTrigger(const PlayerInput& input);
    void ApplyWeaponLevelPenalty(int levels);

    // Fuego amigo nivel 2 contra un disparo de nivel 2 (seccion custom):
    // cada golpe de este tipo es "medio nivel"; hacen falta 2 para bajar 1
    // nivel de verdad. Devuelve true si este golpe fue el que hizo bajar el
    // nivel (el contador se reinicia despues).
    bool RegisterChipHit();

    // Contador de calor (0-100%, seccion custom): cada disparo normal suma
    // 5% (niveles 1-3) o 10% (nivel 4); se enfria solo, 5% cada medio
    // segundo. Si llega a 100% (o se dispara el especial, que lo llena de
    // una), el tanque queda sin poder disparar 5 segundos con el contador
    // fijo en 100%; pasados esos 5 segundos, vuelve a 0%.
    float HeatPercent() const { return heatPercent_; }
    void RegisterNormalShotHeat();
    void RegisterSpecialShotHeat();
    void TickHeatDecay(double dt);

    bool CanShoot() const { return shootCooldownTimer_ <= 0.0; }
    double ShootCooldownRemaining() const { return shootCooldownTimer_; }
    void TickShootCooldown(double dt);

    // Escudo (power-up Casco / proteccion al aparecer, seccion 6). La duracion
    // la decide quien llama (distinta si es respawn o si se agarro el item).
    void ActivateShield(double durationSeconds) { shieldTimer_ = durationSeconds; }
    void TickShield(double dt);
    bool IsShielded() const { return shieldTimer_ > 0.0; }
    double ShieldSecondsRemaining() const { return shieldTimer_; }

    // Paralisis (onda expansiva del disparo especial): bloquea movimiento y
    // disparo mientras dura. Freeze() extiende, nunca acorta, una paralisis en curso.
    void Freeze(double durationSeconds) { freezeTimer_ = std::max(freezeTimer_, durationSeconds); }
    void TickFreeze(double dt);
    bool IsFrozen() const { return freezeTimer_ > 0.0; }

    // Vidas (item Tanque/Vida extra, seccion 6): arranca en 3. Al morir se
    // llama LoseLife(); si ya no quedan, el tanque queda Eliminate()d (fuera
    // de la partida, no vuelve a respawnear) y quien llama debe manejarlo.
    int Lives() const { return lives_; }
    void AddLife() { lives_ = std::min(lives_ + 1, 9); }
    void LoseLife() {
        if (lives_ > 0) {
            --lives_;
        }
    }
    bool IsOutOfLives() const { return lives_ <= 0; }

    // Fuera de la partida (se quedo sin vidas): no se mueve, no dispara, no
    // se dibuja. No hay forma de volver de este estado (habria que arrancar
    // partida de nuevo, ver Game::ResetState).
    void Eliminate() { eliminated_ = true; }
    bool IsEliminated() const { return eliminated_; }

private:
    bool IsPositionBlocked(float newX, float newY, const TileMap& map, const std::vector<Tank*>& others) const;
    bool TryMove(float dx, float dy, const TileMap& map, const std::vector<Tank*>& others);
    bool TryMoveWithAssist(float dx, float dy, const TileMap& map, const std::vector<Tank*>& others);
    bool TrySlidePerpendicularY(float dx, const TileMap& map, const std::vector<Tank*>& others);
    bool TrySlidePerpendicularX(float dy, const TileMap& map, const std::vector<Tank*>& others);

    // Celda de hielo debajo del centro del tanque (ver TileType::Ice): sobre
    // hielo, el tanque patina un poco (sigue deslizando un instante al
    // soltar el movimiento, ver Update) y ademas se enfria el doble de
    // rapido (ver TickHeatDecay).
    bool IsOnIce(const TileMap& map) const;

    float x_ = 0.0f;
    float y_ = 0.0f;
    Direction facing_ = Direction::Up;
    double animTimer_ = 0.0;
    int animFrame_ = 0;
    FireMode fireMode_ = FireMode::SinglePress;
    bool shootHeldLastFrame_ = false;
    bool specialShootHeldLastFrame_ = false;
    int weaponLevel_ = 1;
    int chipHitCount_ = 0; // ver RegisterChipHit
    int specialShotCharges_ = 0; // 0 o 1
    double shootCooldownTimer_ = 0.0;
    float heatPercent_ = 0.0f;
    double heatDecayAccumulator_ = 0.0;
    double shieldTimer_ = 0.0;
    double freezeTimer_ = 0.0;
    float recoilRemaining_ = 0.0f; // celdas que le quedan por retroceder (ver StartRecoil/TickRecoil)
    float recoilDx_ = 0.0f;
    float recoilDy_ = 0.0f;
    float iceCoastDx_ = 0.0f; // impulso restante al patinar sobre hielo sin apretar nada (ver Update)
    float iceCoastDy_ = 0.0f;
    bool onIce_ = false; // calculado en el Update anterior; lo usa TickHeatDecay (se llama antes que Update en Game::UpdatePlayer)
    float speedMultiplier_ = 1.0f;
    int lives_ = 3;
    bool eliminated_ = false;
};

} // namespace bc
