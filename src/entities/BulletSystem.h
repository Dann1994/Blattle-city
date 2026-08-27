#pragma once

#include <functional>
#include <vector>

#include <raylib.h>

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
// 1-3 sin escudo, o un enemigo Basico/Rapido/Blindado sin escudo): la bala
// no explota, sigue de largo. Game se encarga de la animacion de muerte y,
// segun sea jugador o enemigo, resetear el nivel+respawnear o acreditarle
// el puntaje a shooterOwnerId.
struct SpecialDirectKillEvent {
    int targetOwnerId = -1;
    int shooterOwnerId = -1;
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
    // brickBreakSound suena cada vez que se rompe una unidad de ladrillo;
    // blockResistSound cuando una bala pega en hierro (o un obstaculo
    // generico) y no lo rompe; explosionSound cuando se destruye el aguila
    // o explota la bala especial (pedido explicitamente).
    void Update(double dt, TileMap& map, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, std::vector<SpecialExplosionEvent>& explosionEvents, const std::vector<TankCombatState>& tanks, std::vector<SpecialDirectKillEvent>& directKillEvents, Sound brickBreakSound, Sound blockResistSound, Sound explosionSound);

    const std::vector<Bullet>& Bullets() const { return bullets_; }

    // Hay alguna bala viva de este dueño en pantalla ahora mismo (ver
    // EnemySystem: el enemigo espera a que la suya impacte/desaparezca antes
    // de arrancar la cuenta regresiva para el proximo disparo).
    bool HasAliveBullet(int ownerId) const;

    // Cuantas balas vivas de este dueño hay en pantalla ahora mismo (ver
    // ArmorEnemySystem: puede tener hasta 2 balas propias a la vez, asi que
    // HasAliveBullet -que solo dice "al menos 1"- no alcanza para saber si
    // todavia puede disparar otra mas).
    int AliveBulletCount(int ownerId) const;

    // Sistema de resistencia bala-vs-tanque (pedido explicitamente, ver
    // Bullet::health): cada bala que efectivamente toca la caja recibe una
    // consulta a "resolve" (weaponLevel, isSpecial de esa bala) -> cuanta
    // salud del OBJETIVO se consumio con ese impacto (lo decide quien llama,
    // que es quien conoce al tanque real: aplica el dano/escudo ahi mismo).
    // Esa cantidad se le resta a la salud propia de la bala: si llega a 0 la
    // bala se destruye (mismo destello de siempre); si le queda salud, la
    // bala SIGUE DE LARGO (pedido explicitamente: "ciertas balas puedan
    // atravesar objetos y tanques") reubicada justo pasando el borde de la
    // caja en su direccion de avance, para no volver a chocar contra el
    // MISMO tanque al cuadro siguiente mientras siga solapada.
    using TankHitResolver = std::function<int(int weaponLevel, bool isSpecial)>;

    // Fuego amigo (seccion custom, configurable): balas vivas, no
    // especiales, de OTRO JUGADOR (ownerId < kEnemyOwnerIdBase, y distinto a
    // excludeOwnerId, el tanque que se esta chequeando) que esten dentro de
    // la caja indicada — las balas enemigas quedan afuera a proposito (el
    // fuego amigo es entre jugadores nada mas, ver KillEnemyBulletsHittingBox
    // para esas). Devuelve true si toco al menos una.
    bool KillBulletsHittingBox(int excludeOwnerId, float left, float right, float top, float bottom, BulletImpactSystem& impacts, const TankHitResolver& resolve);

    // Igual que KillBulletsHittingBox, pero solo cuenta balas de tanques
    // enemigo (ownerId >= kEnemyOwnerIdBase): un jugador siempre puede
    // recibir dano de un enemigo, sin importar el modo de fuego amigo (que
    // solo aplica entre jugadores, ver Game::ApplyFriendlyFire). Devuelve
    // true si toco al menos una.
    bool KillEnemyBulletsHittingBox(float left, float right, float top, float bottom, BulletImpactSystem& impacts, const TankHitResolver& resolve);

    // Lo opuesto de KillEnemyBulletsHittingBox: solo cuenta balas de
    // jugador (ownerId < kEnemyOwnerIdBase). La usan EnemySystem/
    // FastEnemySystem para chequear si UN ENEMIGO murio: asi las balas
    // enemigas (de cualquier tipo, incluidas las de otro enemigo) los
    // atraviesan sin hacerles nada, en vez de matarse entre ellos.
    // outShooterOwnerIds sale en paralelo (mismo indice = misma bala que
    // efectivamente toco, en el orden en que se encontraron), para poder
    // acreditarle el puntaje al jugador que disparo (ver ScoreEvent);
    // resolve recibe el ownerId de esa bala tambien (ultimo parametro) ya
    // que el dano a un enemigo puede depender de quien la disparo.
    bool KillPlayerBulletsHittingBox(float left, float right, float top, float bottom, BulletImpactSystem& impacts, std::vector<int>& outShooterOwnerIds, const std::function<int(int weaponLevel, bool isSpecial, int shooterOwnerId)>& resolve);

private:
    // Destruye unidades minimas alrededor del punto de impacto (hitX,hitY):
    // si el golpe cae en una fila/columna central de la grilla 4x4, destruye
    // toda esa fila/columna (4 unidades); si cae en el borde ("esquina"),
    // destruye solo la mitad cercana (2 unidades). Devuelve true si frena la
    // bala (habia material ahi), false si ya estaba todo destruido y la bala
    // puede seguir de largo. power solo hace falta para el dano si de rebote
    // golpea de refilon un hierro vecino (ver CheckSeamGraze en el .cpp) —
    // el ladrillo en si tiene salud 1, cualquier potencia >=1 destruye la
    // unidad de un golpe. Recorre TODAS las capas desde la cara de ENTRADA
    // (segun hitFrom) hasta la de llegada, no solo esta ultima: asi nunca se
    // saltea una capa que siga viva mas cerca de la entrada, ni con
    // velocidades altas ni con la bala arrancando ya bien adentro de la
    // celda (tanque pegado al bloque, pedido explicitamente — antes se
    // estimaba la capa de arranque con la posicion anterior de la bala, y
    // ese estimado podia arrancar demasiado adentro y dejar pasar la bala de
    // largo). Ya no existe el antiguo caso especial "doubleLayer" (nivel 4
    // rompia 2 capas de un solo llamado): el sistema de salud de bala (ver
    // Bullet::health en Update) logra lo mismo de forma mas general, dejando
    // que la bala siga de largo sola si le sobra salud.
    static bool HandleBrickHit(TileMap& map, int cellX, int cellY, float hitX, float hitY, Direction hitFrom, int power);

    // Igual que HandleBrickHit pero para hierro (grilla 2x2, ver SteelUnit.h):
    // golpe centrado destruye la capa entera (2 unidades), golpe en la punta
    // destruye solo 1. Cada unidad tiene salud propia (SteelUnit::hp,
    // kSteelUnitMaxHp) y power se le resta directo por impacto, asi que no
    // siempre muere de un golpe. Prueba primero la capa de ENTRADA (segun
    // hitFrom) y, si ahi ya no quedaba nada, la otra (solo hay 2) antes de
    // dejarla pasar — mismo motivo que en HandleBrickHit. Devuelve true si
    // la bala se frena (habia una unidad viva en alguna de las 2), false si
    // ya estaba todo vacio. outDealt recibe cuanta salud le costo a la
    // PRIMERA unidad viva encontrada (min(power, su hp antes del golpe)):
    // eso es lo que Update le resta a la salud propia de la bala.
    // outPrimaryUnitDestroyed dice si esa misma unidad quedo destruida (para
    // decidir el sonido de "resiste" vs "se rompe").
    static bool HandleSteelHit(TileMap& map, int cellX, int cellY, float hitX, float hitY, Direction hitFrom, int power, int& outDealt, bool& outPrimaryUnitDestroyed);

    // Destruye por completo todo lo destructible (ladrillo, acero, base) en
    // un radio alrededor de (x,y) y registra el destello + el evento.
    // directHitOwnerId se copia tal cual al evento (ver SpecialExplosionEvent).
    static void TriggerSpecialExplosion(TileMap& map, float x, float y, int ownerId, SpecialExplosionSystem& specialExplosions, std::vector<SpecialExplosionEvent>& explosionEvents, Sound explosionSound, int directHitOwnerId = -1);

    std::vector<Bullet> bullets_;
};

} // namespace bc
