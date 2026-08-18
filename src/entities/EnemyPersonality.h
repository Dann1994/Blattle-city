#pragma once

namespace bc {

// Parametros de "personalidad" para la IA del enemigo Basico (ver
// EnemySystem.cpp), siguiendo la especificacion de
// Documentaciones/Especificacion_IA_Battle_City_para_Claude.docx (seccion
// 19): todos los enemigos comparten el mismo algoritmo de decision, y lo que
// los distingue son estos numeros. Rangos, salvo aclaracion, van de 0
// (minimo) a 1 (maximo).
struct EnemyPersonality {
    // Que tan directo va al objetivo vs. cuanto rodea/duda; tambien alarga
    // la paciencia para plantarse a romper un ladrillo del camino.
    float aggression = 0.5f;

    // Peso del termino aleatorio en el puntaje de cada direccion candidata
    // (ver chooseDirection en EnemySystem.cpp): mas alto, menos predecible.
    float randomness = 0.35f;

    // Preferencia relativa por perseguir/atacar al jugador vs. a la base al
    // elegir objetivo (ver selectTarget). No tienen que sumar 1 entre si.
    float playerPriority = 0.6f;
    float basePriority = 0.4f;

    // 0..1: mas alto dispara "de rutina" mas seguido (se traduce a un
    // intervalo en segundos, ver RoutineShootIntervalSeconds).
    float shootingFrequency = 0.5f;

    // 0..1: mas alto reconsidera direccion mas seguido (intervalo minimo
    // entre decisiones, ver DirectionHoldSeconds).
    float directionChangeFrequency = 0.55f;

    // Multiplicador de velocidad de movimiento. La dificultad NO debe
    // depender solo de esto (ver seccion 26 del documento); se deja en 1.0
    // para los 5 niveles y la curva de dificultad pasa por el resto de los
    // parametros.
    float speed = 1.0f;

    // Puntos que se restan al puntaje de la direccion opuesta a la actual
    // (evita el ping-pong izquierda-derecha, seccion 24). Sigue siendo
    // elegible si es la unica opcion valida.
    float reversePenalty = 3.0f;

    // Puntos que se restan a una direccion que acerca demasiado a otro
    // enemigo (seccion 22).
    float crowdingPenalty = 1.0f;

    // Probabilidad de ignorar el scoring jugador/base y elegir un objetivo
    // al azar entre los dos (seccion 7).
    float randomTargetChance = 0.05f;

    // Probabilidad de un disparo "especulativo" (sin linea de vision clara
    // ni ladrillo de por medio) cuando el temporizador de rutina todavia no
    // vencio (seccion 17).
    float speculativeShotProbability = 0.05f;

    // Probabilidad de dispararle a un ladrillo que bloquea la ruta en vez de
    // simplemente esperar/rodearlo (seccion 14).
    float brickShotProbability = 0.55f;

    // Cuantas celdas se adelanta la puntaria al jugador segun su direccion
    // actual (seccion 18); 0 = sin prediccion. Se mantiene chico a
    // proposito para no sentirse un aimbot.
    float predictionDistance = 0.3f;
};

// Perfil "de base" (nivel 3, ver EnemySystem::SetAggressivenessLevel): los
// valores de arriba son justo este. El resto de los niveles interpola desde
// mas pasivo/aleatorio (nivel 1) a mas directo/preciso (nivel 5), siguiendo
// la seccion 20 del documento (perfil BasicTank) como punto de referencia
// para el nivel 3.
constexpr EnemyPersonality kPersonalityByLevel[5] = {
    // Nivel 1: muy pasivo y errático.
    {/*aggression*/ 0.25f, /*randomness*/ 0.55f, /*playerPriority*/ 0.5f,
     /*basePriority*/ 0.5f, /*shootingFrequency*/ 0.15f,
     /*directionChangeFrequency*/ 0.35f, /*speed*/ 1.0f,
     /*reversePenalty*/ 2.0f, /*crowdingPenalty*/ 1.0f,
     /*randomTargetChance*/ 0.12f, /*speculativeShotProbability*/ 0.02f,
     /*brickShotProbability*/ 0.35f, /*predictionDistance*/ 0.0f},
    // Nivel 2.
    {0.4f, 0.45f, 0.55f, 0.45f, 0.3f, 0.45f, 1.0f, 2.5f, 1.0f, 0.08f, 0.03f,
     0.45f, 0.0f},
    // Nivel 3: perfil "BasicTank" del documento (baseline).
    {0.5f, 0.35f, 0.6f, 0.4f, 0.5f, 0.55f, 1.0f, 3.0f, 1.0f, 0.05f, 0.05f,
     0.55f, 0.3f},
    // Nivel 4.
    {0.65f, 0.2f, 0.7f, 0.5f, 0.75f, 0.7f, 1.0f, 3.5f, 0.8f, 0.03f, 0.08f,
     0.7f, 0.6f},
    // Nivel 5: muy directo, agresivo y preciso.
    {0.85f, 0.08f, 0.85f, 0.65f, 0.95f, 0.85f, 1.0f, 4.0f, 0.6f, 0.0f, 0.12f,
     0.9f, 1.0f},
};

} // namespace bc
