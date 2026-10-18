#pragma once

// Efectos de sonido grabados (los archivos del proyecto anterior), disparados
// por los mismos eventos que el motor de sonido de la ROM: cada efecto de la
// ROM es un indice 0-27 (ver ram_sounds, $0300-$031B).

#include <raylib.h>

#include <string>

#include "../nes/battle.h"

namespace app {

class SampleBank {
public:
    bool Load(const std::string& dir);
    void Unload();

    // Un efecto de la ROM empezo a sonar (lo llama el motor de sonido).
    void Trigger(int sfxIndex);

    // Una vez por cuadro: bucles de motor segun las banderas del juego.
    // allowLoops = false en las pantallas de game over, record, recuento y menus: se cortan los bucles de motor.
    void Update(const nes::Battle& battle, bool allowLoops = true);

    // true si algun efecto sin archivo propio (record, game over...) esta activo
    // y hay que dejar sonar el sintetizador de la NES para ese.
    static bool NeedsSynth(const nes::Battle& battle);

    void StopLoops();

private:
    Sound stageStart_{}, pause_{}, extraLife_{}, itemPickup_{}, bigExplosion_{}, itemAppear_{};
    Sound tankDeath_{}, brickBreak_{}, blockResist_{}, hitNonLethal_{}, shot_{}, scoreTick_{}, scoreBonus_{};
    Sound bomber_[3]{};                      // el avion del bombardeo (tres voces: pasan varios a la vez)
    int bomberVoice_ = 0;
    int bomberSeen_ = 0;
    Music battleLoop_{}, movementLoop_{};
    bool loaded_ = false;
};

} // namespace app
