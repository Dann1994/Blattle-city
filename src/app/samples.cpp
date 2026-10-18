#include "samples.h"

namespace app {

namespace {

// Indices de los efectos de la ROM (orden de ram_sounds).
enum Sfx {
    kPause = 0, kStageLoad1, kStageLoad2, kStageLoad3, kGainLife1, kGainLife2, kBonusPickup, kExplosionPlayer,
    kUnused08, kBonusAppear, kExplosionEnemy, kExplosionHq, kHitBrick, kHitWall, kHitTank, kShot, kMoveIce,
    kMovePlayer, kMoveEnemy, kScoreCount1, kScoreCount2, kHiScore1, kHiScore2, kHiScore3, kGameOver1, kGameOver2,
    kGameOver3, kBonus1000,
};

Sound LoadOne(const std::string& dir, const char* name) {
    return LoadSound((dir + name).c_str());
}

void Play(const Sound& s) {
    StopSound(s);            // el mismo efecto vuelve a empezar (como en la NES)
    PlaySound(s);
}

} // namespace

bool SampleBank::Load(const std::string& dir) {
    stageStart_ = LoadOne(dir, "stage_start.mp3");
    pause_ = LoadOne(dir, "pause_toggle.wav");
    extraLife_ = LoadOne(dir, "extra_life.wav");
    itemPickup_ = LoadOne(dir, "item_pickup.wav");
    bigExplosion_ = LoadOne(dir, "big_explosion.wav");
    itemAppear_ = LoadOne(dir, "item_appear.wav");
    tankDeath_ = LoadOne(dir, "tank_death.wav");
    brickBreak_ = LoadOne(dir, "brick_break.wav");
    blockResist_ = LoadOne(dir, "block_resist.wav");
    hitNonLethal_ = LoadOne(dir, "hit_nonlethal.wav");
    shot_ = LoadOne(dir, "player_shoot.wav");
    scoreTick_ = LoadOne(dir, "score_tick.wav");
    scoreBonus_ = LoadOne(dir, "score_bonus.wav");
    bomber_[0] = LoadOne(dir, "bomber.wav");
    for (int i = 1; i < 3; ++i) bomber_[i] = IsSoundValid(bomber_[0]) ? LoadSoundAlias(bomber_[0]) : Sound{};
    battleLoop_ = LoadMusicStream((dir + "battle_loop.wav").c_str());
    movementLoop_ = LoadMusicStream((dir + "movement_loop.wav").c_str());
    loaded_ = IsSoundValid(shot_) && IsSoundValid(stageStart_) && IsMusicValid(battleLoop_);
    if (loaded_) {
        battleLoop_.looping = true;
        movementLoop_.looping = true;
        SetMusicVolume(battleLoop_, 0.6f);
        SetMusicVolume(movementLoop_, 0.6f);
    }
    return loaded_;
}

void SampleBank::Unload() {
    if (!loaded_) return;
    for (Sound* s : {&stageStart_, &pause_, &extraLife_, &itemPickup_, &bigExplosion_, &itemAppear_, &tankDeath_, &brickBreak_,
                     &blockResist_, &hitNonLethal_, &shot_, &scoreTick_, &scoreBonus_}) {
        UnloadSound(*s);
    }
    for (int i = 1; i < 3; ++i) if (IsSoundValid(bomber_[i])) UnloadSoundAlias(bomber_[i]);
    UnloadSound(bomber_[0]);
    UnloadMusicStream(battleLoop_);
    UnloadMusicStream(movementLoop_);
    loaded_ = false;
}

void SampleBank::Trigger(int sfx) {
    if (!loaded_) return;
    switch (sfx) {
        case kPause: Play(pause_); break;
        case kStageLoad1: Play(stageStart_); break;
        case kGainLife1: Play(extraLife_); break;
        case kBonusPickup: Play(itemPickup_); break;
        case kExplosionPlayer: Play(bigExplosion_); break;
        case kExplosionHq: Play(bigExplosion_); break;
        case kBonusAppear: Play(itemAppear_); break;
        case kExplosionEnemy: Play(tankDeath_); break;
        case kHitBrick: Play(brickBreak_); break;
        case kHitWall: Play(blockResist_); break;
        case kHitTank: Play(hitNonLethal_); break;
        case kShot: Play(shot_); break;
        case kScoreCount1: Play(scoreTick_); break;
        case kBonus1000: Play(scoreBonus_); break;
        default: break;   // los bucles de motor se manejan en Update; el resto suena en el sintetizador
    }
}

void SampleBank::Update(const nes::Battle& battle, bool allowLoops) {
    if (!loaded_) return;
    const bool paused = battle.R(nes::ram_pause_flag) != 0;
    const int bombers = battle.BomberSoundCount();       // un avion entro: suena "1942 SFX (9)"
    if (bombers != bomberSeen_) {
        if (bombers > bomberSeen_ && IsSoundValid(bomber_[bomberVoice_])) {
            StopSound(bomber_[bomberVoice_]);
            PlaySound(bomber_[bomberVoice_]);
            bomberVoice_ = (bomberVoice_ + 1) % 3;
        }
        bomberSeen_ = bombers;
    }
    const bool jingle = IsSoundPlaying(stageStart_);
    const bool enemyHum = battle.R(nes::ram_sounds + kMoveEnemy) != 0;
    const bool playerEngine = battle.R(nes::ram_sounds + kMovePlayer) != 0;
    const bool hum = enemyHum && !paused && !jingle && allowLoops;
    const bool engine = playerEngine && !paused && !jingle && allowLoops;

    auto drive = [](Music& m, bool want) {
        if (want && !IsMusicStreamPlaying(m)) PlayMusicStream(m);
        else if (!want && IsMusicStreamPlaying(m)) StopMusicStream(m);
        if (IsMusicStreamPlaying(m)) UpdateMusicStream(m);
    };
    drive(battleLoop_, hum);
    drive(movementLoop_, engine);
}

bool SampleBank::NeedsSynth(const nes::Battle& battle) {
    for (int i = kHiScore1; i <= kGameOver3; ++i) {
        if (battle.R(nes::ram_sounds + i) != 0) return true;
    }
    return battle.R(nes::ram_sounds + kMoveIce) != 0;
}

void SampleBank::StopLoops() {
    if (!loaded_) return;
    if (IsMusicStreamPlaying(battleLoop_)) StopMusicStream(battleLoop_);
    if (IsMusicStreamPlaying(movementLoop_)) StopMusicStream(movementLoop_);
}

} // namespace app
