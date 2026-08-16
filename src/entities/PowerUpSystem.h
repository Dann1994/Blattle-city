#pragma once

#include "PowerUp.h"
#include "TileMap.h"

namespace bc {

// En el juego real, el power-up aparece al destruir un tanque enemigo
// "parpadeante" (seccion 6), pero todavia no hay enemigos (llegan en la
// Fase 3). Mientras tanto, para poder probar la mejora de arma, aparece
// solo cada tanto en una celda abierta al azar.
class PowerUpSystem {
public:
    void Update(double dt, const TileMap& map);

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
    void ForceSpawn(PowerUpType type, const TileMap& map);

private:
    void SpawnAt(PowerUpType type, const TileMap& map);

    PowerUp powerUp_;
    double spawnTimer_ = 5.0; // primera aparicion a los 5s de iniciada la partida
    double lifeTimer_ = 0.0;
    double blinkTimer_ = 0.0;
    bool blinkVisible_ = true;
};

} // namespace bc
