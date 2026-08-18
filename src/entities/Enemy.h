#pragma once

#include "SpawnFlash.h"
#include "Tank.h"

namespace bc {

// SeekBase: prioridad normal, avanza hacia el aguila (y le dispara a los
// ladrillos que le bloqueen el paso si hace falta). AttackPlayer: un jugador
// entro en su radio de deteccion (ver kEnemyDetectionRadius); mientras siga
// cerca, prioriza acercarse a el y dispararle en vez de ir hacia la base.
enum class EnemyState { SeekBase, AttackPlayer };

// Tanque enemigo basico (seccion 5, tipo "Basico"): reusa Tank para
// movimiento/colision/disparo (su logica ya sirve igual sea el input de
// teclado o de una IA), y le suma el estado propio de la maquina de estados.
struct Enemy {
    Tank tank;
    int ownerId = -1;
    bool alive = false;
    EnemyState state = EnemyState::SeekBase;
    EnemyState previousState = EnemyState::SeekBase;

    // Destello de aparicion (igual que los jugadores, ver SpawnFlash): mientras
    // dura, no se mueve, no razona ni dispara (ver EnemySystem::Update).
    SpawnFlash spawn;

    // Cuantos frames seguidos intento moverse sin lograrlo (no cambio de
    // posicion): pasado un umbral, la IA prueba una direccion distinta a la
    // bloqueada (rota entre las 4) y si sigue atascada dispara mas seguido
    // (ver EnemySystem::Update).
    int stuckFrames = 0;

    // Direccion de movimiento en la que esta "comprometida" ahora mismo, y
    // cuantas celdas le quedan por recorrer en ella antes de re-elegir (fiel
    // al original: avanza un tramo recto al azar de 1 a 5 celdas en vez de
    // re-apuntar al objetivo cada frame, que se veria como si caminara en
    // diagonal). Se re-elige antes de tiempo si choca (ver stuckFrames) o si
    // cambia de prioridad (SeekBase <-> AttackPlayer).
    Direction moveDir = Direction::Down;
    float cellsRemaining = 0.0f;

    // Solo puede tener una bala propia viva a la vez (ver BulletSystem::
    // TryShoot con maxPerOwner=1). shootTimer cuenta regresivo hasta el
    // proximo disparo permitido, pero solo arranca a contar (y se vuelve a
    // sortear) una vez que la bala anterior impacto/desaparecio; ver
    // bulletWasAlive, que detecta ese momento exacto.
    double shootTimer = 0.0;
    bool bulletWasAlive = false;

    // Detecta el instante en que un jugador entra a 1 casilla de distancia
    // (ver kAdjacentSnapRadius/kAdjacentSnapChance en EnemySystem.cpp), para
    // sortear el "reflejo" de apuntarle y dispararle de una una sola vez por
    // acercamiento, no en cada frame que se queda ahi pegado.
    bool playerWasAdjacent = false;
};

} // namespace bc
