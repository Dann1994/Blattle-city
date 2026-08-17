#pragma once

#include <vector>

#include "PowerUp.h"
#include "TileMap.h"

namespace bc {

// Caja de un tanque activo (celda 1x1 o el area que ocupe), para que el
// power-up no aparezca encima de un jugador. Independiente de Tank para no
// acoplar este sistema al resto del combate.
struct TankOccupiedBounds {
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
};

// En el juego real, el power-up aparece al destruir un tanque enemigo
// "parpadeante" (seccion 6), pero todavia no hay enemigos (llegan en la
// Fase 3). Mientras tanto, para poder probar la mejora de arma, aparece
// solo cada tanto en una celda abierta al azar (que no tenga un bloque que
// bloquee movimiento ni un tanque encima).
class PowerUpSystem {
public:
    void Update(double dt, const TileMap& map, const std::vector<TankOccupiedBounds>& occupiedByTanks);

    // Si el tanque (celda 1x1 en tankX,tankY) se solapa con el power-up activo,
    // lo recoge y devuelve el tipo en outType.
    bool TryPickup(float tankX, float tankY, PowerUpType& outType);

    const PowerUp& Active() const { return powerUp_; }

    // Los power-ups parpadean mientras estan en el mapa (comportamiento del
    // juego original): hay que consultar esto ademas de Active().alive antes
    // de dibujar el icono.
    bool IsBlinkVisible() const { return blinkVisible_; }

    // Fuerza la aparicion de un tipo especifico en una celda abierta al azar,
    // saltando el timer. Botones de prueba (F1, F2, ...).
    void ForceSpawn(PowerUpType type, const TileMap& map, const std::vector<TankOccupiedBounds>& occupiedByTanks);

private:
    void SpawnAt(PowerUpType type, const TileMap& map, const std::vector<TankOccupiedBounds>& occupiedByTanks);

    PowerUp powerUp_;
    double spawnTimer_ = 5.0; // primera aparicion a los 5s de iniciada la partida
    double lifeTimer_ = 0.0;
    double blinkTimer_ = 0.0;
    bool blinkVisible_ = true;
};

} // namespace bc
