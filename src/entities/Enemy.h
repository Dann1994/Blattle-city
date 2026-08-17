#pragma once

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

    // Cuantos frames seguidos intento moverse sin lograrlo (no cambio de
    // posicion): pasado un umbral, la IA prueba el eje perpendicular
    // (esquivar) y si sigue atascada le dispara al ladrillo que tenga
    // enfrente (ver EnemySystem::Update).
    int stuckFrames = 0;

    // Direccion de movimiento en la que esta "comprometida" ahora mismo, y
    // cuanto le queda comprometida con ella antes de poder re-elegir (fiel al
    // original: se mueve en linea recta un rato, no re-apunta al objetivo
    // cada frame, que se veria como si caminara en diagonal). Se re-elige
    // antes de tiempo si choca (ver stuckFrames) o si cambia de prioridad
    // (SeekBase <-> AttackPlayer).
    Direction moveDir = Direction::Down;
    double redecideTimer = 0.0;
    EnemyState previousState = EnemyState::SeekBase;

    // Cuenta regresiva para el proximo disparo "porque si" (ver
    // EnemySystem::Update): dispara de vez en cuando aunque no este atascada
    // ni tenga a nadie enfilado, para sentirse mas viva.
    double randomShootTimer = 0.0;
};

} // namespace bc
