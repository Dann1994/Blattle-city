#include "flow.h"

#include "levels.h"

#include <algorithm>

namespace app {

GameFlow::GameFlow(nes::Battle& battle, Hooks hooks) : battle_(battle), hooks_(std::move(hooks)) {}

void GameFlow::Begin(int level) {
    level_ = level;
    clearTimer_ = 0;
    gameOverTimer_ = 0;
    startPressed_ = false;
    if (level_ > 0) {
        StartIntro();
    } else {
        mode_ = Mode::Playing;
        curtainRows_ = 0;
    }
}

void GameFlow::UpdateHiScore() {
    for (int p = 0; p < battle_.Players(); ++p) {
        if (battle_.Score(p) > hiScore_) hiScore_ = battle_.Score(p);
    }
}

void GameFlow::StartIntro() {
    mode_ = Mode::StageIntro;
    introFrame_ = 0;
    curtainRows_ = 15;
    startPressed_ = false;
}

int GameFlow::NextLevelNumber() const {
    if (hooks_.nextLevel) return hooks_.nextLevel(level_);
    return level_ >= MainLevelCount() ? 1 : level_ + 1;       // despues del ultimo (el 40, o el ultimo nivel agregado) vuelve al 1
}

void GameFlow::SkipToNextLevel() {
    if (level_ <= 0 || !hooks_.loadNextLevel) return;
    const int next = NextLevelNumber();
    if (next <= 0) return;
    level_ = next;
    hooks_.loadNextLevel(level_);
    clearTimer_ = 0;
    StartIntro();
}

void GameFlow::StartScore() {
    tally_ = ScoreTally{};
    tally_.players = std::min(4, battle_.Players());      // (la pantalla de puntos muestra hasta 4)
    tally_.level = level_;
    for (int p = 0; p < tally_.players; ++p) {
        tally_.score[p] = battle_.Score(p);
        tally_.palette[p] = battle_.PlayerPaletteIndex(p);
        for (int k = 0; k < 4; ++k) tally_.kills[p][k] = battle_.KillCount(p, k);
    }
    UpdateHiScore();
    if (++cleared_ % every_ == 0) ++continues_;           // nivel superado: suma un continue cada `every_` niveles
    tally_.hiScore = hiScore_;
    mode_ = Mode::ScoreScreen;
    phase_ = 0;
    wait_ = 30;
    startPressed_ = false;
}

void GameFlow::FinishScore() {
    const int next = NextLevelNumber();
    if (next <= 0) {                                     // era el ultimo nivel de la serie: de vuelta al titulo
        if (hooks_.returnToTitle) hooks_.returnToTitle();
        return;
    }
    level_ = next;
    if (hooks_.loadNextLevel) hooks_.loadNextLevel(level_);
    StartIntro();
}

void GameFlow::StepScore() {
    battle_.RunSoundFrame();
    if (wait_ > 0) {
        --wait_;
        if (startPressed_ && phase_ >= 6) FinishScore();
        startPressed_ = false;
        return;
    }
    startPressed_ = false;
    if (phase_ <= 3) {
        const int kind = phase_;
        bool counted = false;
        for (int p = 0; p < tally_.players; ++p) {
            if (tally_.shown[p][kind] < tally_.kills[p][kind]) {
                ++tally_.shown[p][kind];
                counted = true;
            }
        }
        if (counted) {
            battle_.PlayScoreTick();
            wait_ = 8;                                   // un enemigo por jugador cada 8 cuadros, como el original
        } else {
            ++phase_;
            wait_ = kind == 3 ? 30 : 20;
        }
        return;
    }
    if (phase_ == 4) {                                   // totales
        for (int p = 0; p < tally_.players; ++p) {
            for (int k = 0; k < 4; ++k) tally_.totals[p] += tally_.kills[p][k];
        }
        tally_.showTotals = true;
        phase_ = 5;
        wait_ = 15;
        return;
    }
    if (phase_ == 5) {                                   // bonus de 1000 puntos al que mas destruyo (si sigue vivo y no hay empate)
        int best = -1, bestKills = 0;
        bool tie = false;
        for (int p = 0; p < tally_.players; ++p) {
            if (tally_.totals[p] > bestKills) {
                best = p;
                bestKills = tally_.totals[p];
                tie = false;
            } else if (tally_.totals[p] == bestKills && best >= 0) {
                tie = true;
            }
        }
        if (best >= 0 && !tie && battle_.Lives(best) > 0 && tally_.players > 1) {
            tally_.bonusPlayer = best;
            battle_.AddBonusPoints(best, 0);
            tally_.score[best] = battle_.Score(best);
            UpdateHiScore();
            tally_.hiScore = hiScore_;
        }
        phase_ = 6;
        wait_ = 90;
        return;
    }
    FinishScore();                                       // phase 6, espera terminada
}

void GameFlow::Step() {
    switch (mode_) {
        case Mode::Playing: {
            battle_.RunFrame();
            if (versus_) {
                if (battle_.VersusWinner() >= 0) {
                    mode_ = Mode::VersusResult;
                    gameOverTimer_ = 0;
                }
                startPressed_ = false;
                return;
            }
            UpdateHiScore();
            if (battle_.GameOver()) {
                battle_.StartGameOverMessage();                  // el cartel rojo sube desde abajo mientras los jugadores quedan quietos
                mode_ = Mode::GameOverMessage;
                gameOverTimer_ = 0;
                startPressed_ = false;
                return;
            }
            gameOverTimer_ = 0;
            if (battle_.StageCleared()) {
                if (++clearTimer_ >= kClearDelay) StartScore();
            } else {
                clearTimer_ = 0;
            }
            startPressed_ = false;
            return;
        }
        case Mode::StageIntro: {
            waitingStart_ = false;
            if (requireStart_) {
                if (introFrame_ < kIntroHold) {                  // la cortina cierra y muestra el cartel
                    curtainRows_ = 15;
                    battle_.RunSoundFrame();
                    ++introFrame_;
                    startPressed_ = false;
                    return;
                }
                if (introFrame_ == kIntroHold && !startPressed_) {   // y espera a que el anfitrion toque Start
                    curtainRows_ = 15;
                    battle_.RunSoundFrame();
                    waitingStart_ = true;
                    return;
                }
            } else if (startPressed_ && introFrame_ < kIntroHold) {
                introFrame_ = kIntroHold;
            }
            startPressed_ = false;
            if (introFrame_ < kIntroHold) {
                curtainRows_ = 15;                       // cortina cerrada con el cartel
                battle_.RunSoundFrame();
            } else {
                const int opened = introFrame_ - kIntroHold;
                curtainRows_ = 15 - opened;
                battle_.RunFrame();                      // detras de la cortina los jugadores ya van apareciendo
                if (curtainRows_ <= 0) {
                    curtainRows_ = 0;
                    mode_ = Mode::Playing;
                }
            }
            ++introFrame_;
            return;
        }
        case Mode::ScoreScreen:
            StepScore();
            return;
        case Mode::GameOverMessage: {
            battle_.RunFrame();
            if (!battle_.GameOverMessageActive()) {              // termino el cartel: si quedan continues se pregunta; si no, pantalla de GAME OVER
                if (continues_ > 0 && level_ > 0) {
                    mode_ = Mode::ContinuePrompt;
                    continueTimer_ = kContinueFrames;
                    startPressed_ = false;
                    return;
                }
                mode_ = Mode::GameOver;
                gameOverTimer_ = 0;
                battle_.DebugPlaySound(2);                       // la musica de GAME OVER
            }
            startPressed_ = false;
            return;
        }
        case Mode::ContinuePrompt: {
            battle_.RunSoundFrame();
            if (startPressed_) {                                 // seguir: todos vuelven con sus vidas y se recarga el nivel
                startPressed_ = false;
                --continues_;
                battle_.ReviveForContinue();
                if (hooks_.loadNextLevel) hooks_.loadNextLevel(level_);
                clearTimer_ = 0;
                gameOverTimer_ = 0;
                StartIntro();
                return;
            }
            if (--continueTimer_ <= 0) {                         // no se contesto: se acabo
                mode_ = Mode::GameOver;
                gameOverTimer_ = 0;
                battle_.DebugPlaySound(2);
            }
            return;
        }
        case Mode::GameOver: {
            battle_.RunSoundFrame();
            ++gameOverTimer_;
            if (gameOverTimer_ >= 360 || (startPressed_ && gameOverTimer_ > 30)) {
                startPressed_ = false;
                if (hiScore_ > hiAtStart_) {                     // se superó el record: pantalla de HI-SCORE
                    mode_ = Mode::HighScore;
                    gameOverTimer_ = 0;
                    battle_.DebugPlaySound(1);
                } else if (hooks_.returnToTitle) {
                    hooks_.returnToTitle();
                }
            }
            startPressed_ = false;
            return;
        }
        case Mode::VersusResult: {
            battle_.RunFrame();                                  // las explosiones terminan de verse
            ++gameOverTimer_;
            if (gameOverTimer_ >= 420 || (startPressed_ && gameOverTimer_ > 60)) {
                if (hooks_.returnToTitle) hooks_.returnToTitle();
            }
            startPressed_ = false;
            return;
        }
        case Mode::HighScore: {
            battle_.RunSoundFrame();
            ++gameOverTimer_;
            if (gameOverTimer_ >= 300 || (startPressed_ && gameOverTimer_ > 30)) {
                if (hooks_.returnToTitle) hooks_.returnToTitle();
            }
            startPressed_ = false;
            return;
        }
    }
}

} // namespace app
