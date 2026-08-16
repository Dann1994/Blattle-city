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

    // Si el tanque (celda 1x1 en tankX,tankY) se solapa con el power-up activo, lo recoge.
    bool TryPickup(float tankX, float tankY);

    const PowerUp& Active() const { return powerUp_; }

    // Los power-ups parpadean mientras estan en el mapa (comportamiento del
    // juego original): hay que consultar esto ademas de Active().alive antes
    // de dibujar el icono.
    bool IsBlinkVisible() const { return blinkVisible_; }

private:
    void SpawnRandom(const TileMap& map);

    PowerUp powerUp_;
    double spawnTimer_ = 5.0; // primera aparicion a los 5s de iniciada la partida
    double lifeTimer_ = 0.0;
    double blinkTimer_ = 0.0;
    bool blinkVisible_ = true;
};

} // namespace bc
