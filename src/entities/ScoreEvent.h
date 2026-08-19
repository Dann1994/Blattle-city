#pragma once

namespace bc {

// Puntaje por tanque enemigo destruido segun tipo, y por item de power-up
// agarrado. Investigado en el juego original (no estaba en el diseno):
// basico/rapido/medio/pesado (4 hits) = 100/200/300/400, y CUALQUIER
// power-up agarrado = 500 parejo, sin importar cual sea.
constexpr int kScoreBasicKill = 100;
constexpr int kScoreFastKill = 200;
constexpr int kScoreArmorKill = 300;
constexpr int kScorePowerKill = 400;
constexpr int kScorePowerUpPickup = 500;

// Reportado por cada sistema de enemigo (EnemySystem::Update y hermanos)
// cuando una bala de JUGADOR mata a uno de sus enemigos: quien disparo (para
// acreditarle el puntaje a ese jugador) y donde aparece el popup del numero.
struct ScoreEvent {
    int ownerId = -1;
    float x = 0.0f;
    float y = 0.0f;
    int points = 0;
};

} // namespace bc
