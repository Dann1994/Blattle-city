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
struct SpecialExplosionEvent {
    float x = 0.0f;
    float y = 0.0f;
    float radius = 0.0f;
    int ownerId = -1;
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
    // explosionEvents se limpia y se llena con las explosiones especiales de
    // este frame, para que Game aplique el dano al tanque si corresponde.
    void Update(double dt, TileMap& map, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, std::vector<SpecialExplosionEvent>& explosionEvents);

    const std::vector<Bullet>& Bullets() const { return bullets_; }

private:
    // Destruye unidades minimas alrededor del punto de impacto (hitX,hitY):
    // si el golpe cae en una fila/columna central de la grilla 4x4, destruye
    // toda esa fila/columna (4 unidades); si cae en el borde ("esquina"),
    // destruye solo la mitad cercana (2 unidades). Devuelve true si frena la
    // bala (habia material ahi), false si ya estaba todo destruido y la bala
    // puede seguir de largo. Con doubleLayer, ademas destruye la capa
    // siguiente en la direccion de avance (nivel 4).
    static bool HandleBrickHit(TileMap& map, int cellX, int cellY, float hitX, float hitY, Direction hitFrom, bool doubleLayer);

    // Igual que HandleBrickHit pero para hierro (grilla 2x2, ver SteelUnit.h):
    // golpe centrado destruye la capa entera (2 unidades), golpe en la punta
    // destruye solo 1. A diferencia del ladrillo, cada unidad tiene puntos de
    // vida (resistencia) y el dano por disparo depende de weaponLevel, asi
    // que no siempre muere de un golpe. Devuelve true si la bala se frena
    // (habia una unidad viva ahi), false si ya estaba vacio.
    static bool HandleSteelHit(TileMap& map, int cellX, int cellY, float hitX, float hitY, Direction hitFrom, int weaponLevel);

    // Destruye por completo todo lo destructible (ladrillo, acero, base) en
    // un radio alrededor de (x,y) y registra el destello + el evento.
    static void TriggerSpecialExplosion(TileMap& map, float x, float y, int ownerId, SpecialExplosionSystem& specialExplosions, std::vector<SpecialExplosionEvent>& explosionEvents);

    std::vector<Bullet> bullets_;
};

} // namespace bc
