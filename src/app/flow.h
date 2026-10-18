#pragma once

// Flujo de pantallas del juego, como el original pero para hasta 4 jugadores:
//   introduccion de nivel (cortina gris + "STAGE N")  ->  partida  ->
//   recuento de puntos (enemigos destruidos por tipo y jugador, total y bonus)  ->  siguiente nivel
//   y, si la base cae o se quedan todos sin vidas, la pantalla de GAME OVER.
// No depende de ventana ni de audio: se maneja con un Step() por cuadro de juego.

#include <functional>

#include "../nes/battle.h"

namespace app {

// Datos de la pantalla de recuento de puntos.
struct ScoreTally {
    int players = 1;
    int level = 1;
    int hiScore = 0;
    int palette[4] = {0, 1, 4, 5};   // indice de paleta de sprites de cada jugador (su color)
    int score[4] = {};
    int kills[4][4] = {};        // enemigos destruidos por jugador y tipo (basico, rapido, power, blindado)
    int shown[4][4] = {};        // cuantos de esos ya se contaron en pantalla
    bool showTotals = false;
    int totals[4] = {};
    int bonusPlayer = -1;        // quien se llevo el bonus de 1000 puntos (-1: nadie)
};

class GameFlow {
public:
    enum class Mode { Playing, StageIntro, ScoreScreen, GameOverMessage, GameOver, HighScore };

    struct Hooks {
        std::function<void(int level)> loadNextLevel;   // genera el mapa y llama a Battle::NextStage
        std::function<void()> returnToTitle;            // termino todo: de vuelta a la pantalla de titulo
    };

    GameFlow(nes::Battle& battle, Hooks hooks);

    // Arranca el flujo con la partida ya creada y el mapa ya cargado (despues de StartStage).
    // level 0 = mapa de pruebas (sin introduccion de nivel).
    void Begin(int level);
    void Step();
    void PressStart() { startPressed_ = true; }
    void SkipToNextLevel();                              // tecla de prueba: pasa de nivel de una

    Mode mode() const { return mode_; }
    int level() const { return level_; }
    int curtainRows() const { return curtainRows_; }     // 0..15 filas tapadas arriba y abajo (15 = pantalla cerrada)
    int clearTimer() const { return clearTimer_; }
    const ScoreTally& tally() const { return tally_; }
    int hiScore() const { return hiScore_; }
    void SetHiScore(int v) { hiScore_ = hiAtStart_ = v; }   // el record con el que arranca la partida
    int highScoreTimer() const { return gameOverTimer_; }

    static constexpr int kClearDelay = 120;              // cuadros entre matar al ultimo enemigo y el recuento
    static constexpr int kIntroHold = 60;
    static constexpr int kCurtainFrames = 16;

private:
    void StartScore();
    void StepScore();
    void FinishScore();
    void StartIntro();
    void UpdateHiScore();

    nes::Battle& battle_;
    Hooks hooks_;
    Mode mode_ = Mode::Playing;
    int level_ = 0;
    int curtainRows_ = 0;
    int clearTimer_ = 0;
    int gameOverTimer_ = 0;
    int introFrame_ = 0;
    int phase_ = 0;
    int wait_ = 0;
    int hiScore_ = 20000;
    int hiAtStart_ = 20000;
    bool startPressed_ = false;
    ScoreTally tally_;
};

} // namespace app
