#pragma once

#include <vector>

#include "Bullet.h"
#include "TileMap.h"

namespace bc {

class BulletImpactSystem;
class SpecialExplosionSystem;

constexpr float kSpecialExplosionRadius = 1.5f; // celdas, radio de la explosion del disparo especial (expuesto para el render)

// Un disparo especial exploto: quien procesa esto (Game) decide el dano al
// propio tanque, ya que BulletSystem no conoce tanques, solo el ownerId.
// directHitOwnerId es -1 salvo que la explosion la haya causado un choque
// directo contra un tanque (escudo o nivel 4, ver Update): en ese caso Game
// le aplica a ESE tanque un efecto especial en vez del generico por radio.
struct SpecialExplosionEvent {
    float x = 0.0f;
    float y = 0.0f;
    float radius = 0.0f;
    int ownerId = -1;
    int directHitOwnerId = -1;
};

// Un tanque fue destruido de un choque directo del disparo especial (nivel
// 1-3 sin escudo): la bala no explota, sigue de largo. Game se encarga de
// la animacion de muerte, resetear el nivel y respawnear.
struct SpecialDirectKillEvent {
    int targetOwnerId = -1;
};

// Estado de un tanque que BulletSystem necesita para resolver el choque
// directo del disparo especial contra el (ver Update): posicion, escudo y
// nivel de arma. Lo arma Game cada frame, uno por tanque vivo.
struct TankCombatState {
    int ownerId = -1;
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    bool shielded = false;
    int weaponLevel = 1;
};

class BulletSystem {
public:
    // Respeta el limite de balas propias en pantalla (1 salvo doble disparo nivel 3+, ver seccion 4.3).
    bool TryShoot(int ownerId, float muzzleX, float muzzleY, Direction direction, float speed, int weaponLevel, int maxPerOwner = 1);

    // Disparo especial (nivel 4 + carga de estrella extra): el triple de
    // rapida que la bala normal de nivel 1, y al chocar con lo que sea
    // explota destruyendo todo en un radio.
    bool TryShootSpecial(int ownerId, float muzzleX, float muzzleY, Direction direction);

    // impacts recibe un destello en cada choque real (terreno u otra bala).
    // specialExplosions recibe el destello grande del disparo especial.
    // explosionEvents y directKillEvents se limpian y se llenan de nuevo con
    // lo que haya pasado este frame, para que Game aplique el efecto que
    // corresponda a cada tanque. tanks es el estado actual (posicion, escudo,
    // nivel) de cada tanque vivo, para resolver el choque directo del
    // especial contra alguno de ellos (ver seccion custom de fuego amigo):
    // nivel 1-3 sin escudo destruye directo y la bala sigue de largo; con
    // escudo o nivel 4, la bala explota (con el efecto especial de cada caso
    // aplicado por Game via directHitOwnerId).
    void Update(double dt, TileMap& map, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, std::vector<SpecialExplosionEvent>& explosionEvents, const std::vector<TankCombatState>& tanks, std::vector<SpecialDirectKillEvent>& directKillEvents);

    const std::vector<Bullet>& Bullets() const { return bullets_; }

    // Fuego amigo (seccion custom, configurable): mata las balas vivas, no
    // especiales, de un dueño distinto a excludeOwnerId que esten dentro de
    // la caja indicada (el tanque que se esta chequeando), y les dispara un
    // destello de impacto normal. Devuelve true si mato al menos una; cada
    // una agrega su weaponLevel a outShooterWeaponLevels (en orden), para
    // que Game aplique el efecto que corresponda segun el nivel de quien
    // disparo cada bala.
    bool KillBulletsHittingBox(int excludeOwnerId, float left, float right, float top, float bottom, BulletImpactSystem& impacts, std::vector<int>& outShooterWeaponLevels);

private:
    // Destruye unidades minimas alrededor del punto de impacto (hitX,hitY):
    // si el golpe cae en una fila/columna central de la grilla 4x4, destruye
    // toda esa fila/columna (4 unidades); si cae en el borde ("esquina"),
    // destruye solo la mitad cercana (2 unidades). Devuelve true si frena la
    // bala (habia material ahi), false si ya estaba todo destruido y la bala
    // puede seguir de largo. Con doubleLayer, ademas destruye la capa
    // siguiente en la direccion de avance (nivel 4). weaponLevel solo hace
    // falta para el dano si de rebote golpea de refilon un hierro vecino
    // (ver CheckSeamGraze en el .cpp). oldX/oldY (posicion antes de este
    // frame) se usan junto con hitX/hitY para recorrer TODAS las capas que
    // la bala atraveso este frame, no solo la de llegada: con velocidades
    // altas (nivel 2) un solo paso puede ser mas ancho que una capa y
    // saltearse una si solo se mirara el punto final.
    static bool HandleBrickHit(TileMap& map, int cellX, int cellY, float oldX, float oldY, float hitX, float hitY, Direction hitFrom, bool doubleLayer, int weaponLevel);

    // Igual que HandleBrickHit pero para hierro (grilla 2x2, ver SteelUnit.h):
    // golpe centrado destruye la capa entera (2 unidades), golpe en la punta
    // destruye solo 1. A diferencia del ladrillo, cada unidad tiene puntos de
    // vida (resistencia) y el dano por disparo depende de weaponLevel, asi
    // que no siempre muere de un golpe. Devuelve true si la bala se frena
    // (habia una unidad viva ahi), false si ya estaba vacio.
    static bool HandleSteelHit(TileMap& map, int cellX, int cellY, float hitX, float hitY, Direction hitFrom, int weaponLevel);

    // Destruye por completo todo lo destructible (ladrillo, acero, base) en
    // un radio alrededor de (x,y) y registra el destello + el evento.
    // directHitOwnerId se copia tal cual al evento (ver SpecialExplosionEvent).
    static void TriggerSpecialExplosion(TileMap& map, float x, float y, int ownerId, SpecialExplosionSystem& specialExplosions, std::vector<SpecialExplosionEvent>& explosionEvents, int directHitOwnerId = -1);

    std::vector<Bullet> bullets_;
};

} // namespace bc
