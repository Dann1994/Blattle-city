#pragma once

#include "SpawnFlash.h"
#include "Tank.h"

namespace bc {

// Estado comun a cualquier tanque enemigo (reusa Tank para
// movimiento/colision/disparo, su logica ya sirve igual sea el input de
// teclado o de una IA). Usado tanto por el tipo "Basico" (EnemySystem: su
// unica prioridad es el aguila; un jugador en el camino es un obstaculo mas
// a esquivar/rodear, salvo en el nivel de agresividad maximo, donde si la
// toca lo encara y le dispara a matar) como por el "Rapido" (FastEnemySystem:
// patrulla el escenario al azar sin objetivo fijo, y persigue/dispara a
// cualquier jugador que entre en su radio de deteccion). Cada tipo tiene su
// propio std::vector<Enemy>, asi que reusar los mismos campos (por ejemplo
// deviationDir) para cosas distintas segun el tipo no genera conflicto.
struct Enemy {
    Tank tank;
    int ownerId = -1;
    bool alive = false;

    // Destello de aparicion (igual que los jugadores, ver SpawnFlash): mientras
    // dura, no se mueve, no razona ni dispara (ver EnemySystem::Update).
    SpawnFlash spawn;

    // Cuantos frames seguidos intento moverse sin lograrlo (no cambio de
    // posicion): pasado un umbral, la IA prueba una direccion distinta a la
    // bloqueada (rota entre las 4). Tambien marca el limite de paciencia al
    // plantarse a romper un ladrillo que el camino elegido atraviesa (ver
    // kBrickBreachPatienceFrames en EnemySystem.cpp).
    int stuckFrames = 0;

    // Direccion de movimiento elegida este frame: el mejor vecino segun
    // BaseDistanceField, salvo que este desviandose (ver deviationDir),
    // atascada (esquive) o plantada rompiendo un ladrillo.
    Direction moveDir = Direction::Down;

    // Solo puede tener una bala propia viva a la vez (ver BulletSystem::
    // TryShoot con maxPerOwner=1). shootTimer cuenta regresivo hasta el
    // proximo disparo permitido, pero solo arranca a contar (y se vuelve a
    // sortear) una vez que la bala anterior impacto/desaparecio; ver
    // bulletWasAlive, que detecta ese momento exacto.
    double shootTimer = 0.0;
    bool bulletWasAlive = false;

    // "Basico": desvio del camino optimo hacia el aguila (ver
    // kPathDeviationChance en EnemySystem.cpp) en una direccion al azar por
    // un tramo de celdas, antes de volver al camino optimo. "Rapido": el
    // tramo recto actual de la patrulla al azar (ver FastEnemySystem.cpp),
    // el comportamiento principal en vez de una excepcion ocasional. En
    // ambos casos se cancela/re-elige antes si la direccion se bloquea.
    int decisionCellX = -1000000;
    int decisionCellY = -1000000;
    int deviationCellsRemaining = 0;
    Direction deviationDir = Direction::Up;

    // Solo "Rapido" (ver FastEnemySystem.cpp): cuenta regresiva de "modo
    // matar". Cada vez que detecta a un jugador dentro del radio, se
    // renueva a kKillModeDuration; mientras siga arriba de 0, persigue y
    // dispara (al ultimo jugador visto, aunque se haya alejado del radio de
    // deteccion), y recien vuelve a patrullar cuando llega a 0.
    double killModeTimer = 0.0;

    // Solo "Rapido": cuenta regresiva de "deteccion apagada". Se arranca a
    // kDetectionLockoutDuration justo cuando dispara en modo matar (ver
    // FastEnemySystem.cpp); mientras siga arriba de 0, ni siquiera escanea
    // en busca de jugadores (vuelve a patrullar tranquila), y recien puede
    // volver a detectar/entrar en modo matar cuando llega a 0.
    double detectionLockoutTimer = 0.0;

    // Debug (temporal, para diagnosticar bloqueos): en que "modo" de
    // movimiento decidio moverse este frame. Basico (EnemySystem): 'F'
    // siguiendo el campo normal, 'V' desviandose a proposito, 'B' plantada
    // rompiendo un ladrillo, 'D' esquivando (atascada), 'T' quieta encarando
    // a un jugador que la toca (solo nivel 5). Rapido (FastEnemySystem): 'P'
    // patrullando, 'K' en modo matar (persiguiendo o plantada disparando),
    // 'B'/'D' igual que el Basico. Los 2 tipos comparten 'U': se tuvo que
    // reposicionar a la fuerza (ver kHardUnstuckFrames: algo la tuvo
    // trabada 15s sin resolverse solo), y 'N': ningun caso de arriba
    // aplico todavia (raro, 1 o 2 frames).
    char debugMode = '?';
};

} // namespace bc
