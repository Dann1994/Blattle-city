#pragma once

#include <random>

#include "EnemyPersonality.h"
#include "SpawnFlash.h"
#include "Tank.h"

namespace bc {

// Solo "Basico" (ver EnemySystem.cpp): a que le apunta este frame, elegido
// por puntaje (ver EnemySystem::SelectTarget), no fijo. "Rapido" no usa esto
// (siempre el jugador detectado, ver FastEnemySystem.cpp).
enum class EnemyTargetType { Base, Player };

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

    // Solo "Power" (ver PowerEnemySystem.cpp): a diferencia del resto (que
    // muere de 1 impacto de bala de jugador, sin importar el nivel), este
    // tiene resistencia real: cada impacto le resta el dano que corresponda
    // al nivel de arma de quien disparo (ver kPowerEnemyDamageByLevel), y
    // recien muere cuando llega a 0. Arranca en kPowerEnemyMaxHp al
    // aparecer.
    int hp = 1;

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

    // Solo "Basico" (ver EnemySystem.cpp), especificacion de IA en
    // Documentaciones/Especificacion_IA_Battle_City_para_Claude.docx:
    // parametros de personalidad de ESTE enemigo en particular (copiados del
    // perfil de su nivel de agresividad al aparecer, con una pequena
    // variacion aleatoria, seccion 21 del documento, para que no se
    // comporten todos identico).
    EnemyPersonality personality;

    // Solo "Basico": a que le apunta este frame (ver EnemyTargetType).
    // Se reevalua en cada "evento de decision" (ver directionHoldTimer), no
    // cada frame, para no titilar entre jugador y base sin sentido.
    EnemyTargetType targetType = EnemyTargetType::Base;

    // Solo "Basico": cuenta regresiva minima antes de poder reconsiderar la
    // direccion elegida (seccion 9 del documento: no cambiar de direccion
    // cada frame). Se reinicia cada vez que efectivamente elige una nueva
    // direccion (ver DirectionHoldSeconds en EnemySystem.cpp).
    double directionHoldTimer = 0.0;

    // Solo "Basico"/"Rapido"/"Blindado" (los 3 tipos reconstruidos desde
    // cero, ver EnemySystem/FastEnemySystem/ArmorEnemySystem.cpp):
    // generador de numeros aleatorios propio de este enemigo, sembrado con
    // una semilla global + su ownerId al aparecer, para que su
    // comportamiento sea reproducible con una semilla fija sin depender del
    // orden en que se actualizan los demas.
    std::mt19937 rng;

    // Solo "Blindado" (ver ArmorEnemySystem.cpp): -1 mientras no hay un
    // segundo disparo pendiente. Al disparar el primer tiro de una tanda
    // "de rutina", se arranca a un valor al azar entre 0.5 y 2 segundos
    // (ver kSecondShotMinDelay/kSecondShotMaxDelay); mientras siga >= 0,
    // cuenta regresiva hacia el segundo tiro de esa misma tanda, en vez de
    // salir los dos juntos.
    double secondShotDelay = -1.0;

    // Solo "Power" (ver PowerEnemySystem.cpp/Game::ApplyEnemyPowerUpPickups):
    // -1 mientras no tiene un disparo especial armado. Al agarrar una
    // estrella o una pistola (unico power-up que le da, en vez de
    // convertirlo en el siguiente tipo de tanque como al resto) se arranca
    // a 5.0; mientras siga >= 0, cuenta regresiva hacia el disparo especial
    // automatico (hacia donde este mirando en ese momento) y brilla (ver
    // Game::RenderEnemy), y recien vuelve a -1 cuando dispara.
    double specialShotFuseTimer = -1.0;

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

    // Arranca en true al aparecer (ver SpawnAt de los 4 sistemas): si el
    // destello termino justo encima de otro tanque (jugador u otro
    // enemigo), en vez de bloquearse contra el para siempre (cualquier
    // movimiento, por chico que sea, seguiria "tocandolo"), lo atraviesa
    // hasta que deje de solaparlo. Ahi se apaga solo y nunca se vuelve a
    // prender: pasa a chocar como cualquier tanque para el resto de su vida
    // (ver el filtro de 'others' en el Update de cada sistema).
    bool ignoreSpawnOverlap = true;
};

} // namespace bc
