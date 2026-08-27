#include "Game.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <raylib.h>

#include "Camera.h"
#include "Config.h"
#include "LevelFormat.h"

namespace bc {

namespace {

// Interruptor unico para todas las teclas/HUD de depuracion (spawns
// manuales, forzar power-ups, subir/bajar agresividad, saltar de nivel,
// grilla de referencia, contador de FPS, etc.): pedido explicitamente que
// la interfaz del juego principal quede limpia. BC_DEBUG_BUILD lo prende
// solo en el target "battlecity_debug" (ver CMakeLists.txt), un ejecutable
// aparte pensado para hacer pruebas de gameplay — "battlecity" (el juego de
// verdad) siempre lo compila en false.
#ifdef BC_DEBUG_BUILD
constexpr bool kDebugFeaturesEnabled = true;
#else
constexpr bool kDebugFeaturesEnabled = false;
#endif

// Transicion de nivel (seccion custom, calcada del original): pantalla gris
// solida con "NIVEL N" un tiempo fijo, despues esa misma pantalla partida al
// medio se abre como un telon (ver LevelTransitionPhase/RenderLevelTransition).
constexpr double kLevelTransitionTextDuration = 2.0;
constexpr double kLevelTransitionCurtainDuration = 0.8;
constexpr Color kLevelTransitionGray = Color{168, 168, 168, 255};

// Espera post-cuota-cumplida antes de pasar de nivel (pedido explicito): el
// juego sigue andando normal durante esta espera, a diferencia de la
// transicion de arriba que congela todo.
constexpr double kLevelClearDelay = 5.0;

// Pantalla de puntuacion de fin de nivel (ver ScoreTallyPhase): cuanto tarda
// cada incremento de un contador, la pausa antes de revelar el siguiente
// icono de esa fila, y la pausa final (Holding) con todo ya revelado antes
// de pasar a la transicion del siguiente nivel.
constexpr double kScoreTallyCountInterval = 0.12; // pedido explicitamente: un poco mas rapido (antes 0.2)
constexpr double kScoreTallyRowPause = 0.6;
constexpr double kScoreTallyHoldDuration = 2.0;
constexpr double kScoreTallyBonusRevealDelay = 1.0; // pedido explicitamente: el bono espera este tiempo (dentro de Holding) antes de aparecer

// Bono de fin de nivel (pedido explicito) para quien mas puntos gano en el
// nivel (ver TickScoreTally/scoreTallyBonusAwarded_).
constexpr int kScoreTallyClearBonus = 1000;

// Vida extra por puntaje (pedido explicitamente, investigado del juego
// original NES de 1985: cada 20.000 puntos otorga una vida — ver
// Game::CheckExtraLifeByScore/extraLifeMilestonesClaimed_).
constexpr int kExtraLifeScoreThreshold = 20000;

// Secuencia de fin de partida (ver GameOverPhase): cuanto tarda el sprite
// "GAME OVER" en subir del todo hasta el centro, cuanto se queda ahi quieto
// como maximo (se puede saltear antes, ver ProcessGameOverInput), y el largo
// maximo del nombre que se puede tipear en NameEntry.
constexpr double kGameOverRiseDuration = 2.5;
constexpr double kGameOverHoldDuration = 5.0;
constexpr int kGameOverNameMaxLen = 10;

// Duraciones del escudo (seccion 6). No hay un numero exacto documentado del
// juego original para la proteccion al aparecer; 10s para el item Casco esta
// confirmado por una recreacion fiel del juego, y se uso como referencia
// para elegir un valor mas corto (proporcional al tiempo del destello) para
// el respawn, tal como se pidio.
constexpr double kRespawnShieldDuration = 2.0;
constexpr double kHelmetShieldDuration = 10.0;
constexpr double kShieldBlinkInterval = 0.05; // segundos entre cada frame del escudo

// Impacto del disparo especial: sacude toda la pantalla y paraliza a todos
// los tanques (por ahora solo el jugador 1) durante el mismo tiempo.
constexpr double kSpecialImpactFreezeDuration = 2.0;
constexpr double kScreenShakeDuration = 2.0;
constexpr float kScreenShakeMaxOffsetPx = 12.0f;
constexpr float kSpecialShotRecoilDistance = 0.75f; // celdas que retrocede el tanque al disparar el especial

// Explosion del disparo especial contra un "Power" (enemigo 4, ver
// Enemy::hp): no lo mata de un saque (a diferencia de Basico/Rapido/
// Blindado) salvo que ya estuviera en este mismo HP reducido — en ese caso
// la explosion lo termina de rematar (ver ApplySpecialExplosionsToEnemies).
constexpr int kSpecialExplosionPowerWeakenHp = 1;

// Fuego amigo nivel 1: parpadeo + paralisis al ser tocado por una bala aliada.
constexpr double kFriendlyFireParalyzeDuration = 5.0;
constexpr double kFrozenBlinkInterval = 0.1; // segundos entre cada parpadeo mientras esta paralizado

// Reloj: paraliza a todos los enemigos en pantalla (los 4 tipos) durante
// este tiempo, igual que el clasico.
constexpr double kClockFreezeDuration = 10.0;

// Agua: alterna entre sus 2 frames para animar el oleaje.
constexpr double kWaterFrameInterval = 0.5;

// Mandos (seccion custom): D-pad/analogico mueven siempre (fijo, no
// configurable, ver ApplyGamepadInput); disparo/especial/inicio/pausa son
// por jugador y configurables desde el menu (ver playerGamepadBindings_/
// InitDefaultGamepadBindings/MenuScreen::ControlDeviceChoice).
constexpr float kGamepadStickDeadzone = 0.4f;

// Que acciones se pueden reasignar a un boton de mando (ver MenuScreen::
// PlayerActions/CapturingKey con mappingGamepad_ en true): Up/Down/Left/
// Right quedan afuera, el movimiento con mando es siempre D-pad +
// analogico; Inicio tambien queda afuera (pedido explicitamente, es
// innecesario reasignarlo: solo suma/saca al jugador de la partida, sigue
// andando con el boton por defecto de InitDefaultGamepadBindings, nunca
// aparece en este menu). El indice dentro de este array (0-2) es el que usa
// playerActionIndex_ mientras se mapea el mando; kGamepadMappableActions[i]
// da la InputAction real (y su posicion en playerGamepadBindings_.buttons).
constexpr InputAction kGamepadMappableActions[] = {InputAction::Shoot, InputAction::Special, InputAction::Pause};
constexpr int kGamepadMappableActionCount = 3;

// Mismo criterio para teclado (ver mas arriba): Inicio queda afuera del
// menu de reasignacion, sigue con la tecla por defecto de
// InitDefaultKeyBindings. A diferencia del mando, el teclado SI permite
// reasignar Up/Down/Left/Right (el movimiento con teclado no es fijo).
constexpr InputAction kKeyMappableActions[] = {InputAction::Up, InputAction::Down, InputAction::Left, InputAction::Right, InputAction::Shoot, InputAction::Special, InputAction::Pause};
constexpr int kKeyMappableActionCount = 7;

// HUD: parpadeo de texto (calor al 100% / municion especial lista).
constexpr double kHudTextBlinkInterval = 0.3;

// Item Pala: cuanto dura el hierro temporal alrededor de la base.
constexpr double kShovelFortifyDuration = 15.0;

// Escenario rectangular (seccion custom, en prueba): el campo de juego
// (mapa rectangular, ver test_map.json) se encaja en el area que queda a la
// izquierda de una barra gris vertical a la derecha, para aprovechar todo
// el alto de la ventana. Los datos de los 4 jugadores se apilan en esa
// barra, todos a la misma distancia entre si.
constexpr float kHudPanelWidth = 250.0f;
constexpr float kHudPanelMargin = 10.0f;
constexpr float kHudRowHeight = 100.0f;  // separacion entre 2 jugadores consecutivos
constexpr Color kHudPanelColor = Color{45, 45, 48, 255};
constexpr Color kHudPanelBorderColor = Color{90, 90, 96, 255};

// Color de los datos de cada jugador en el HUD (pedido explicitamente),
// tomado de la paleta real de su tanque (Tanques.png/tank_pN_*.png), en una
// variante clara para que se lea bien sobre el panel oscuro. Solo tine el
// texto "de datos" (etiqueta, vidas, nivel, calor, puntaje) — las alertas
// (sobrecalentado, especial listo, paralizado) siguen en rojo/amarillo
// siempre, para que resalten igual sin importar de que jugador sean.
constexpr Color kPlayer1HudColor = Color{231, 178, 60, 255};  // amarillo/naranja
constexpr Color kPlayer2HudColor = Color{70, 200, 100, 255};  // verde
constexpr Color kPlayer3HudColor = Color{130, 210, 220, 255}; // gris/celeste
constexpr Color kPlayer4HudColor = Color{90, 150, 230, 255};  // azul

Color ColorForTile(TileType type) {
    switch (type) {
        case TileType::Steel: return Color{0xA0, 0xA0, 0xA8, 0xFF};
        case TileType::Water: return Color{0x30, 0x60, 0xD0, 0xFF};
        case TileType::Trees: return Color{0x30, 0x90, 0x30, 0xFF};
        case TileType::Ice:   return Color{0xC0, 0xE8, 0xF8, 0xFF};
        case TileType::Base:  return Color{0xE0, 0xC0, 0x20, 0xFF};
        default:              return Color{0x10, 0x10, 0x10, 0xFF};
    }
}

// Color principal de cada jugador (ver tank_p1..p4_*.png): P1 amarillo/dorado,
// P2 verde, P3 gris/plateado, P4 azul — el mismo tono dominante de su propio
// sprite, asi la bala se nota de quien es. El sprite de bala es gris parejo
// de base, asi que tintarlo (multiplicativo) alcanza sin necesitar arte
// nuevo. Los enemigos (ownerId >= kEnemyOwnerIdBase) siguen en WHITE (sin
// tintar): ver bullet.ownerId en el loop de renderizado. ownerId 0-3 son
// literales, no Game::kPlayer1Id..kPlayer4Id (son privados y esta funcion
// esta fuera de la clase), pero son los mismos valores.
Color BulletTintForOwner(int ownerId) {
    switch (ownerId) {
        case 0: return Color{231, 156, 33, 255}; // P1
        case 1: return Color{0, 140, 49, 255};   // P2
        case 2: return Color{173, 173, 173, 255}; // P3
        case 3: return Color{0, 85, 164, 255};   // P4
        default: return WHITE;
    }
}

// Etiqueta corta (para el menu de Opciones) y explicacion (texto de ayuda
// abajo del todo) de cada FriendlyFireMode. El comportamiento real ya
// estaba implementado (ver ApplyFriendlyFire/ProcessFriendlyFireDamageHit);
// esto solo lo expone en el menu en vez de F3.
const char* FriendlyFireLabel(FriendlyFireMode mode) {
    switch (mode) {
        case FriendlyFireMode::Paralyze: return "TIPO 1";
        case FriendlyFireMode::Damage:   return "TIPO 2";
        default:                         return "NO";
    }
}

const char* FriendlyFireExplanation(FriendlyFireMode mode) {
    switch (mode) {
        case FriendlyFireMode::Paralyze: return "La bala de un aliado te paraliza un momento, sin quitarte nada.";
        case FriendlyFireMode::Damage:   return "La bala de un aliado te hace dano real, segun el nivel de arma de quien dispara.";
        default:                         return "Las balas de otro jugador te atraviesan sin ningun efecto.";
    }
}

// Nombre corto para mostrar en el mapeo de controles (Opciones > Mapeo de
// controles): cubre las teclas usadas por los esquemas por defecto mas las
// mas comunes para reasignar a mano. No es exhaustivo (raylib no trae un
// KeyName de fabrica); una tecla no cubierta cae en el numero crudo.
const char* KeyName(int key) {
    switch (key) {
        case KEY_SPACE: return "ESPACIO";
        case KEY_ENTER: return "ENTER";
        case KEY_ESCAPE: return "ESC";
        case KEY_TAB: return "TAB";
        case KEY_LEFT_CONTROL: return "CTRL IZQ";
        case KEY_RIGHT_CONTROL: return "CTRL DER";
        case KEY_LEFT_SHIFT: return "SHIFT IZQ";
        case KEY_RIGHT_SHIFT: return "SHIFT DER";
        case KEY_LEFT_ALT: return "ALT IZQ";
        case KEY_RIGHT_ALT: return "ALT DER";
        case KEY_UP: return "FLECHA ARRIBA";
        case KEY_DOWN: return "FLECHA ABAJO";
        case KEY_LEFT: return "FLECHA IZQ";
        case KEY_RIGHT: return "FLECHA DER";
        case KEY_KP_0: return "NUM 0";
        case KEY_KP_1: return "NUM 1";
        case KEY_KP_2: return "NUM 2";
        case KEY_KP_3: return "NUM 3";
        case KEY_KP_4: return "NUM 4";
        case KEY_KP_5: return "NUM 5";
        case KEY_KP_6: return "NUM 6";
        case KEY_KP_7: return "NUM 7";
        case KEY_KP_8: return "NUM 8";
        case KEY_KP_9: return "NUM 9";
        case KEY_KP_ENTER: return "NUM ENTER";
        case KEY_KP_DECIMAL: return "NUM .";
        case KEY_ONE: return "1";
        case KEY_TWO: return "2";
        case KEY_THREE: return "3";
        case KEY_FOUR: return "4";
        case KEY_FIVE: return "5";
        case KEY_SIX: return "6";
        case KEY_SEVEN: return "7";
        case KEY_EIGHT: return "8";
        case KEY_NINE: return "9";
        case KEY_ZERO: return "0";
        case KEY_A: return "A"; case KEY_B: return "B"; case KEY_C: return "C"; case KEY_D: return "D";
        case KEY_E: return "E"; case KEY_F: return "F"; case KEY_G: return "G"; case KEY_H: return "H";
        case KEY_I: return "I"; case KEY_J: return "J"; case KEY_K: return "K"; case KEY_L: return "L";
        case KEY_M: return "M"; case KEY_N: return "N"; case KEY_O: return "O"; case KEY_P: return "P";
        case KEY_Q: return "Q"; case KEY_R: return "R"; case KEY_S: return "S"; case KEY_T: return "T";
        case KEY_U: return "U"; case KEY_V: return "V"; case KEY_W: return "W"; case KEY_X: return "X";
        case KEY_Y: return "Y"; case KEY_Z: return "Z";
        default: return TextFormat("TECLA %d", key);
    }
}

// Nombre corto para mostrar en el mapeo de controles de mando (pedido
// explicitamente, pensado para un control de Xbox: A/B/X/Y en vez de
// "boton 7"/etc.). Un boton no cubierto cae en el numero crudo, igual que
// KeyName con las teclas.
const char* GamepadButtonName(int button) {
    switch (button) {
        case GAMEPAD_BUTTON_RIGHT_FACE_DOWN: return "A";
        case GAMEPAD_BUTTON_RIGHT_FACE_RIGHT: return "B";
        case GAMEPAD_BUTTON_RIGHT_FACE_LEFT: return "X";
        case GAMEPAD_BUTTON_RIGHT_FACE_UP: return "Y";
        case GAMEPAD_BUTTON_LEFT_FACE_UP: return "D-PAD ARRIBA";
        case GAMEPAD_BUTTON_LEFT_FACE_DOWN: return "D-PAD ABAJO";
        case GAMEPAD_BUTTON_LEFT_FACE_LEFT: return "D-PAD IZQ";
        case GAMEPAD_BUTTON_LEFT_FACE_RIGHT: return "D-PAD DER";
        case GAMEPAD_BUTTON_LEFT_TRIGGER_1: return "LB";
        case GAMEPAD_BUTTON_LEFT_TRIGGER_2: return "LT";
        case GAMEPAD_BUTTON_RIGHT_TRIGGER_1: return "RB";
        case GAMEPAD_BUTTON_RIGHT_TRIGGER_2: return "RT";
        case GAMEPAD_BUTTON_MIDDLE_LEFT: return "VIEW";
        case GAMEPAD_BUTTON_MIDDLE: return "GUIDE";
        case GAMEPAD_BUTTON_MIDDLE_RIGHT: return "MENU";
        case GAMEPAD_BUTTON_LEFT_THUMB: return "STICK IZQ";
        case GAMEPAD_BUTTON_RIGHT_THUMB: return "STICK DER";
        default: return TextFormat("BOTON %d", button);
    }
}

// Triangulo chico que apunta hacia (dirX, dirY) (vector unidad, ej. (0,-1)
// para arriba): usado en vez de texto para las 4 acciones de movimiento del
// mapeo de controles, pedido explicitamente.
void DrawArrowGlyph(float centerX, float centerY, float size, float dirX, float dirY, Color color) {
    const Vector2 tip{centerX + dirX * size, centerY + dirY * size};
    const float perpX = -dirY;
    const float perpY = dirX;
    const Vector2 base1{centerX - dirX * size * 0.6f + perpX * size * 0.7f, centerY - dirY * size * 0.6f + perpY * size * 0.7f};
    const Vector2 base2{centerX - dirX * size * 0.6f - perpX * size * 0.7f, centerY - dirY * size * 0.6f - perpY * size * 0.7f};
    DrawTriangle(tip, base2, base1, color);
}

int CountAliveInList(const std::vector<Enemy>& enemies) {
    int count = 0;
    for (const Enemy& enemy : enemies) {
        count += enemy.alive ? 1 : 0;
    }
    return count;
}

// Automatizacion de oleadas (40 niveles, ver Game::TickEnemySpawning/
// CheckLevelCompletion): tope de niveles y cada cuanto se intenta un spawn.
constexpr int kMaxGameLevel = 40;
// Cadencia despues del arranque simultaneo de las 3 celdas (ver
// Game::SpawnInitialWaveForLevel): cada cuanto se intenta 1 spawn mas.
constexpr double kEnemySpawnInterval = 5.0;

// Nivel de agresividad y cuotas (cuantos enemigos hay que eliminar para
// terminar el nivel / cuantos puede haber en pantalla a la vez) segun el
// nivel de juego actual (1-40) y la cantidad de jugadores activos: con 3 o
// 4 jugadores las cuotas son mas altas (mas jugadores = mas fuego, hace
// falta mas presion enemiga). 1-10 = agresividad 1, 11-20 = 2, 21-30 = 3,
// 31-40 = 4 (y se queda en 4 si currentLevel_ se pasa de 40).
struct EnemyWaveTier {
    int aggressivenessLevel;
    int killQuotaFewPlayers;
    int killQuotaManyPlayers;
    int maxOnScreenFewPlayers;
    int maxOnScreenManyPlayers;
};

const EnemyWaveTier& WaveTierForLevel(int level) {
    static constexpr EnemyWaveTier kTiers[4] = {
        {1, 15, 20, 5, 10},  // niveles 1-10
        {2, 20, 25, 6, 12},  // niveles 11-20
        {3, 30, 35, 10, 15}, // niveles 21-30
        {4, 50, 60, 15, 20}, // niveles 31-40 (y de ahi en mas)
    };
    if (level <= 10) {
        return kTiers[0];
    }
    if (level <= 20) {
        return kTiers[1];
    }
    if (level <= 30) {
        return kTiers[2];
    }
    return kTiers[3];
}

// Composicion de tipos de enemigo por rango de nivel (pedido explicitamente,
// con conteos fijos por tipo y el resto siempre Basico/tipo1): tipo1=Basico,
// tipo2=Rapido, tipo3=Blindado, tipo4=Power. Los conteos son absolutos (no
// escalan con la cantidad de jugadores); el 11-15 es la unica excepcion, ahi
// se pide mitad y mitad en vez de un numero fijo.
struct EnemyTypeCounts {
    int basic;
    int fast;
    int armor;
    int power;
};

EnemyTypeCounts EnemyTypeCountsForLevel(int level, int totalQuota) {
    int fast = 0, armor = 0, power = 0;
    if (level <= 5) {
        // Solo Basico.
    } else if (level <= 10) {
        fast = 3;
    } else if (level <= 15) {
        fast = totalQuota / 2; // mitad tipo2, la otra mitad (resto) tipo1
    } else if (level <= 20) {
        fast = 5;
        armor = 3;
    } else if (level <= 25) {
        fast = 5;
        armor = 5;
    } else if (level <= 30) {
        fast = 5;
        armor = 5;
        power = 3;
    } else if (level <= 35) {
        fast = 5;
        armor = 5;
        power = 5;
    } else if (level <= 39) {
        fast = 10;
        armor = 10;
        power = 5;
    } else {
        fast = 10;
        armor = 10;
        power = 10;
    }
    const int basic = std::max(0, totalQuota - fast - armor - power);
    return EnemyTypeCounts{basic, fast, armor, power};
}

} // namespace

void Game::Init() {
    windowWidth_ = kDefaultWindowWidth;
    windowHeight_ = kDefaultWindowHeight;

    InitWindow(windowWidth_, windowHeight_, "Battle City Clon - Fase 0");
    SetWindowState(FLAG_WINDOW_RESIZABLE);
    SetTargetFPS(60);
    SetExitKey(KEY_NULL); // ESC ya no cierra la ventana: la usamos para reiniciar (ver ProcessInput)

    InitAudioDevice();
    battleLoopMusic_ = LoadMusicStream((std::string(BC_ASSETS_DIR) + "sounds/battle_loop.wav").c_str());
    movementLoopMusic_ = LoadMusicStream((std::string(BC_ASSETS_DIR) + "sounds/movement_loop.wav").c_str());
    scoreTickSound_ = LoadSound((std::string(BC_ASSETS_DIR) + "sounds/score_tick.wav").c_str());
    hitNonLethalSound_ = LoadSound((std::string(BC_ASSETS_DIR) + "sounds/hit_nonlethal.wav").c_str());
    blockResistSound_ = LoadSound((std::string(BC_ASSETS_DIR) + "sounds/block_resist.wav").c_str());
    brickBreakSound_ = LoadSound((std::string(BC_ASSETS_DIR) + "sounds/brick_break.wav").c_str());
    playerShootSound_ = LoadSound((std::string(BC_ASSETS_DIR) + "sounds/player_shoot.wav").c_str());
    tankDeathSound_ = LoadSound((std::string(BC_ASSETS_DIR) + "sounds/tank_death.wav").c_str());
    scoreBonusSound_ = LoadSound((std::string(BC_ASSETS_DIR) + "sounds/score_bonus.wav").c_str());
    bigExplosionSound_ = LoadSound((std::string(BC_ASSETS_DIR) + "sounds/big_explosion.wav").c_str());
    itemAppearSound_ = LoadSound((std::string(BC_ASSETS_DIR) + "sounds/item_appear.wav").c_str());
    pauseToggleSound_ = LoadSound((std::string(BC_ASSETS_DIR) + "sounds/pause_toggle.wav").c_str());
    itemPickupSound_ = LoadSound((std::string(BC_ASSETS_DIR) + "sounds/item_pickup.wav").c_str());
    extraLifeSound_ = LoadSound((std::string(BC_ASSETS_DIR) + "sounds/extra_life.wav").c_str());
    stageStartSound_ = LoadSound((std::string(BC_ASSETS_DIR) + "sounds/stage_start.mp3").c_str());
    specialShootSound_ = LoadSound((std::string(BC_ASSETS_DIR) + "sounds/special_shoot.wav").c_str());

    // Icono de la ventana: el mismo tanque P1 nivel 1 (mirando arriba) que
    // se uso para el icono del .exe (ver assets/icon/app.rc), a diferencia
    // del .exe esto se puede setear en runtime con una Image comun.
    Image windowIcon = LoadImage((std::string(BC_ASSETS_DIR) + "icon/app_icon.png").c_str());
    SetWindowIcon(windowIcon);
    UnloadImage(windowIcon);

    player1Sprites_.LoadPlayer1(BC_ASSETS_DIR);
    player2Sprites_.LoadPlayer2(BC_ASSETS_DIR);
    player3Sprites_.LoadPlayer3(BC_ASSETS_DIR);
    player4Sprites_.LoadPlayer4(BC_ASSETS_DIR);
    enemySprites_.Load(BC_ASSETS_DIR, "basic");
    fastEnemySprites_.Load(BC_ASSETS_DIR, "fast");
    armorEnemySprites_.Load(BC_ASSETS_DIR, "armor");
    powerEnemySprites_.Load(BC_ASSETS_DIR, "power");
    bulletSprites_.Load(BC_ASSETS_DIR);
    starTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_star.png").c_str());
    SetTextureFilter(starTexture_, TEXTURE_FILTER_POINT);
    helmetTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_helmet.png").c_str());
    SetTextureFilter(helmetTexture_, TEXTURE_FILTER_POINT);
    gunTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_gun.png").c_str());
    SetTextureFilter(gunTexture_, TEXTURE_FILTER_POINT);
    lifeTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_life.png").c_str());
    SetTextureFilter(lifeTexture_, TEXTURE_FILTER_POINT);
    grenadeTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_grenade.png").c_str());
    SetTextureFilter(grenadeTexture_, TEXTURE_FILTER_POINT);
    shovelTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_shovel.png").c_str());
    SetTextureFilter(shovelTexture_, TEXTURE_FILTER_POINT);
    clockTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/powerup_clock.png").c_str());
    SetTextureFilter(clockTexture_, TEXTURE_FILTER_POINT);
    brickUnitTextures_[0] = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/brick_unit_0.png").c_str());
    brickUnitTextures_[1] = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/brick_unit_1.png").c_str());
    SetTextureFilter(brickUnitTextures_[0], TEXTURE_FILTER_POINT);
    SetTextureFilter(brickUnitTextures_[1], TEXTURE_FILTER_POINT);
    steelUnitTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/steel_unit.png").c_str());
    SetTextureFilter(steelUnitTexture_, TEXTURE_FILTER_POINT);
    treesTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/terrain_trees.png").c_str());
    SetTextureFilter(treesTexture_, TEXTURE_FILTER_POINT);
    waterTextures_[0] = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/terrain_water_0.png").c_str());
    waterTextures_[1] = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/terrain_water_1.png").c_str());
    SetTextureFilter(waterTextures_[0], TEXTURE_FILTER_POINT);
    SetTextureFilter(waterTextures_[1], TEXTURE_FILTER_POINT);
    iceTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/terrain_ice.png").c_str());
    SetTextureFilter(iceTexture_, TEXTURE_FILTER_POINT);
    baseEagleTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/base_eagle.png").c_str());
    SetTextureFilter(baseEagleTexture_, TEXTURE_FILTER_POINT);
    baseEagleDestroyedTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/base_eagle_destroyed.png").c_str());
    SetTextureFilter(baseEagleDestroyedTexture_, TEXTURE_FILTER_POINT);
    hudLifeIconTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/icon_life_hud.png").c_str());
    SetTextureFilter(hudLifeIconTexture_, TEXTURE_FILTER_POINT);
    stageFlagIconTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/icon_stage_flag.png").c_str());
    SetTextureFilter(stageFlagIconTexture_, TEXTURE_FILTER_POINT);
    spawnFlashSprites_.Load(BC_ASSETS_DIR);
    shieldSprites_.Load(BC_ASSETS_DIR);
    bulletImpactSprites_.Load(BC_ASSETS_DIR);
    scorePopupSprites_.Load(BC_ASSETS_DIR);
    specialExplosionSprites_.Load(BC_ASSETS_DIR);
    titleLogoTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/title_logo.png").c_str());
    SetTextureFilter(titleLogoTexture_, TEXTURE_FILTER_POINT);
    menuSelectorTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/menu_selector_tank.png").c_str());
    SetTextureFilter(menuSelectorTexture_, TEXTURE_FILTER_POINT);
    gameOverTexture_ = LoadTexture((std::string(BC_ASSETS_DIR) + "sprites/game_over.png").c_str());
    SetTextureFilter(gameOverTexture_, TEXTURE_FILTER_POINT);
    LoadHighScore();
    InitDefaultKeyBindings();
    InitDefaultGamepadBindings();
    LoadControls(); // pisa los valores por defecto de arriba si el jugador ya habia guardado una configuracion antes

    ResetState();
}

void Game::InitDefaultKeyBindings() {
    using A = InputAction;
    auto set = [](PlayerKeyBindings& b, int up, int down, int left, int right, int shoot, int special, int start, int pause) {
        b.keys[static_cast<int>(A::Up)] = up;
        b.keys[static_cast<int>(A::Down)] = down;
        b.keys[static_cast<int>(A::Left)] = left;
        b.keys[static_cast<int>(A::Right)] = right;
        b.keys[static_cast<int>(A::Shoot)] = shoot;
        b.keys[static_cast<int>(A::Special)] = special;
        b.keys[static_cast<int>(A::Start)] = start;
        b.keys[static_cast<int>(A::Pause)] = pause;
    };
    // Pausa: Enter para los 4 por defecto (ya es, de paso, la tecla fija que
    // siempre pausa sin importar el mapeo, ver ProcessInput); cada jugador
    // la puede reasignar a otra por separado desde el menu si quiere.
    set(playerBindings_[0], KEY_W, KEY_S, KEY_A, KEY_D, KEY_LEFT_CONTROL, KEY_SPACE, KEY_ONE, KEY_ENTER);
    set(playerBindings_[1], KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_RIGHT_CONTROL, KEY_RIGHT_SHIFT, KEY_TWO, KEY_ENTER);
    set(playerBindings_[2], KEY_I, KEY_K, KEY_J, KEY_L, KEY_U, KEY_O, KEY_THREE, KEY_ENTER);
    set(playerBindings_[3], KEY_KP_8, KEY_KP_5, KEY_KP_4, KEY_KP_6, KEY_KP_0, KEY_KP_ENTER, KEY_FOUR, KEY_ENTER);
}

void Game::InitDefaultGamepadBindings() {
    using A = InputAction;
    // Movimiento (Up/Down/Left/Right) queda sin usar: con mando es siempre
    // D-pad + analogico (ver ApplyGamepadInput), no una asignacion de un
    // boton por direccion.
    auto set = [](PlayerGamepadBindings& b, int shoot, int special, int start, int pause) {
        b.buttons[static_cast<int>(A::Shoot)] = shoot;
        b.buttons[static_cast<int>(A::Special)] = special;
        b.buttons[static_cast<int>(A::Start)] = start;
        b.buttons[static_cast<int>(A::Pause)] = pause;
    };
    // Pedido explicitamente, pensado para un control de Xbox: Disparar = A,
    // Especial = B, Inicio y Pausa = el boton central derecho ("Menu"/Play).
    // Los 4 jugadores arrancan con el mismo esquema (el mando que le toque a
    // cada uno via ApplyGamepadInput usa estos mismos botones); cada uno se
    // puede reasignar por separado desde el menu si hace falta.
    for (PlayerGamepadBindings& bindings : playerGamepadBindings_) {
        set(bindings, GAMEPAD_BUTTON_RIGHT_FACE_DOWN, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, GAMEPAD_BUTTON_MIDDLE_RIGHT, GAMEPAD_BUTTON_MIDDLE_RIGHT);
    }
}

// Vuelve a dejar la partida como recien arrancada: recarga el mapa (repone
// los ladrillos rotos), y reinicia tanque, balas, explosiones y power-ups.
// Pensado para probar rapido (ESC), sin tener que cerrar y volver a abrir.
std::string Game::LevelFilePath(int levelNumber) const {
    char buf[32];
    snprintf(buf, sizeof(buf), "levels/level_%02d.json", levelNumber);
    const std::string customPath = std::string(BC_ASSETS_DIR) + buf;
    if (std::filesystem::exists(customPath)) {
        return customPath;
    }
    // Todavia no se diseño ese nivel con el editor: se usa el mapa de
    // siempre como respaldo, para que el juego nunca se rompa por un nivel
    // faltante (van a ir apareciendo los 40 de a poco).
    return std::string(BC_ASSETS_DIR) + "levels/test_map.json";
}

void Game::LoadCurrentLevelMap() {
    const LevelData level = LoadLevel(LevelFilePath(currentLevel_));
    map_.LoadFrom(level);

    basePositionX_ = level.base_position[0];
    basePositionY_ = level.base_position[1];
    fortifiedCells_.clear();
    baseFortifyTimer_ = 0.0;

    // Anillo de proteccion de la base (ladrillo arriba-izquierda/arriba-centro/
    // arriba-derecha, pegado directo a la base, y a los costados): se recorta
    // a 2 unidades de espesor, pegadas al lado que da hacia la base.
    ThinBrickCellVertical(basePositionX_ - 1, basePositionY_ - 1, true);
    ThinBrickCellVertical(basePositionX_, basePositionY_ - 1, true);
    ThinBrickCellVertical(basePositionX_ + 1, basePositionY_ - 1, true);
    ThinBrickCellHorizontal(basePositionX_ - 1, basePositionY_, true);
    ThinBrickCellHorizontal(basePositionX_ + 1, basePositionY_, false);

    if (!level.player_spawns.empty()) {
        player1SpawnX_ = static_cast<float>(level.player_spawns[0][0]);
        player1SpawnY_ = static_cast<float>(level.player_spawns[0][1]);
    }
    if (level.player_spawns.size() > 1) {
        player2SpawnX_ = static_cast<float>(level.player_spawns[1][0]);
        player2SpawnY_ = static_cast<float>(level.player_spawns[1][1]);
    } else {
        player2SpawnX_ = player1SpawnX_;
        player2SpawnY_ = player1SpawnY_;
    }
    if (level.player_spawns.size() > 2) {
        player3SpawnX_ = static_cast<float>(level.player_spawns[2][0]);
        player3SpawnY_ = static_cast<float>(level.player_spawns[2][1]);
    } else {
        player3SpawnX_ = player1SpawnX_;
        player3SpawnY_ = player1SpawnY_;
    }
    if (level.player_spawns.size() > 3) {
        player4SpawnX_ = static_cast<float>(level.player_spawns[3][0]);
        player4SpawnY_ = static_cast<float>(level.player_spawns[3][1]);
    } else {
        player4SpawnX_ = player1SpawnX_;
        player4SpawnY_ = player1SpawnY_;
    }

    enemySpawnPositions_ = level.enemy_spawns;
}

void Game::ResetState() {
    // El entorno de pruebas (BC_DEBUG_BUILD, ver kDebugFeaturesEnabled y
    // CMakeLists.txt) arranca siempre en el "escenario 0" pedido
    // explicitamente: al no existir levels/level_00.json, LevelFilePath cae
    // sola en el mapa de prueba de siempre (levels/test_map.json), asi las
    // pruebas de gameplay no dependen de (ni tocan) los niveles 1-40 reales.
#ifdef BC_DEBUG_BUILD
    currentLevel_ = 0;
#else
    currentLevel_ = 1;
#endif
    LoadCurrentLevelMap();
    gameOver_ = false;
    gameOverPhase_ = GameOverPhase::None;
    gameOverTimer_ = 0.0;
    gameOverWinnerOwnerId_ = -1;
    gameOverWinnerScore_ = 0;
    gameOverNameEntry_.clear();
    // Foto del record ANTES de esta partida (ver matchStartHighScore_):
    // highScore_ ya esta cargado por LoadHighScore (o pisado por la ultima
    // partida jugada esta misma sesion), asi que esto es lo que hay que
    // superar para que valga como record nuevo al terminar.
    matchStartHighScore_ = highScore_;

    player1_ = Tank{};
    player2_ = Tank{};
    player3_ = Tank{};
    player4_ = Tank{};
    // TODO: exponer como opcion en el menu de configuracion (seccion 12.5).
    player1_.SetFireMode(FireMode::SinglePress);
    player2_.SetFireMode(FireMode::SinglePress);
    player3_.SetFireMode(FireMode::SinglePress);
    player4_.SetFireMode(FireMode::SinglePress);

    bullets_ = BulletSystem{};
    bulletImpacts_ = BulletImpactSystem{};
    scorePopups_ = ScorePopupSystem{};
    specialExplosions_ = SpecialExplosionSystem{};
    specialExplosionEvents_.clear();
    powerUps_ = PowerUpSystem{};
    screenShakeTimer_ = 0.0;

    enemies_ = EnemySystem{};
    fastEnemies_ = FastEnemySystem{};
    armorEnemies_ = ArmorEnemySystem{};
    powerEnemies_ = PowerEnemySystem{};

    enemiesKilledThisLevel_ = 0;
    enemiesSpawnedThisLevel_ = 0;
    enemySpawnTimer_ = 0.0;
    nextEnemySpawnCellIndex_ = 0;
    player1Score_ = 0;
    player2Score_ = 0;
    player3Score_ = 0;
    player4Score_ = 0;
    extraLifeMilestonesClaimed_ = {};
    playerLevelStats_ = {};
    highScoreHolderId_ = -1;
    scoreTallyPhase_ = ScoreTallyPhase::None;
    scoreTallyBonusAwarded_ = {};
    paused_ = false;
    pauseMenuIndex_ = 0;
    pauseEndChoiceIndex_ = 0;
    pauseConfirmIndex_ = 1;
    ApplyAggressivenessForCurrentLevel();
    BuildEnemyTypeSequenceForLevel();
    // El enjambre inicial NO se arma aca: recien lo hace SpawnInitialWaveForLevel
    // cuando termina la transicion de nivel (ver StartArcadeLocal/BeginLevelTransition).

    // A diferencia de antes, ResetState() YA NO decide quien esta activo:
    // eso ahora lo elige la pantalla de seleccion de personaje
    // (EnterCharacterSelect/ProcessCharacterSelectInput) antes de llamar
    // aca (ver StartArcadeLocal), asi que player1Active_.._4 quedan tal
    // cual estaban al entrar. Los otros 2 llamadores de ResetState() que no
    // pasan por esa pantalla (Init() y el salto de nivel de depuracion,
    // JumpToLevel) los fijan ellos mismos justo antes de llamar aca.
    RespawnPlayer1();
    if (player2Active_) RespawnPlayer2();
    if (player3Active_) RespawnPlayer3();
    if (player4Active_) RespawnPlayer4();
}

void Game::RespawnPlayer1() {
    player1_.SetPosition(player1SpawnX_, player1SpawnY_);
    player1_.SetFacing(Direction::Up);
    player1Spawn_.Start(player1SpawnX_, player1SpawnY_);
    player1_.ActivateShield(kRespawnShieldDuration);
}

void Game::RespawnPlayer2() {
    player2_.SetPosition(player2SpawnX_, player2SpawnY_);
    player2_.SetFacing(Direction::Up);
    player2Spawn_.Start(player2SpawnX_, player2SpawnY_);
    player2_.ActivateShield(kRespawnShieldDuration);
}

void Game::RespawnPlayer3() {
    player3_.SetPosition(player3SpawnX_, player3SpawnY_);
    player3_.SetFacing(Direction::Up);
    player3Spawn_.Start(player3SpawnX_, player3SpawnY_);
    player3_.ActivateShield(kRespawnShieldDuration);
}

void Game::RespawnPlayer4() {
    player4_.SetPosition(player4SpawnX_, player4SpawnY_);
    player4_.SetFacing(Direction::Up);
    player4Spawn_.Start(player4SpawnX_, player4SpawnY_);
    player4_.ActivateShield(kRespawnShieldDuration);
}

void Game::RespawnByOwnerId(int ownerId) {
    if (ownerId == kPlayer1Id) {
        RespawnPlayer1();
    } else if (ownerId == kPlayer2Id) {
        RespawnPlayer2();
    } else if (ownerId == kPlayer3Id) {
        RespawnPlayer3();
    } else if (ownerId == kPlayer4Id) {
        RespawnPlayer4();
    }
}

void Game::SetPlayerActive(bool& activeFlag, Tank& tank, SpawnFlash& spawn, float spawnX, float spawnY, bool active, int slotIndex) {
    activeFlag = active;
    if (!active) {
        return; // Update()/Render() ya no lo van a tocar mientras este en false
    }
    playerColorIndex_[slotIndex] = FirstAvailableColor(); // pedido explicitamente: nunca 2 jugadores con el mismo color en pantalla
    tank = Tank{};
    tank.SetFireMode(FireMode::SinglePress);
    tank.SetPosition(spawnX, spawnY);
    tank.SetFacing(Direction::Up);
    spawn.Start(spawnX, spawnY);
    tank.ActivateShield(kRespawnShieldDuration);
}

void Game::ThinBrickCellVertical(int x, int y, bool keepBottomHalf) {
    if (!map_.InBounds(x, y)) {
        return;
    }
    Cell& cell = map_.At(x, y);
    if (cell.type != TileType::Brick) {
        return;
    }
    for (int r = 0; r < kBrickGridSize; ++r) {
        const bool keep = keepBottomHalf ? (r >= kBrickGridSize / 2) : (r < kBrickGridSize / 2);
        if (keep) {
            continue;
        }
        for (int c = 0; c < kBrickGridSize; ++c) {
            cell.brickUnits[r * kBrickGridSize + c].alive = false;
        }
    }
}

void Game::ThinBrickCellHorizontal(int x, int y, bool keepRightHalf) {
    if (!map_.InBounds(x, y)) {
        return;
    }
    Cell& cell = map_.At(x, y);
    if (cell.type != TileType::Brick) {
        return;
    }
    for (int c = 0; c < kBrickGridSize; ++c) {
        const bool keep = keepRightHalf ? (c >= kBrickGridSize / 2) : (c < kBrickGridSize / 2);
        if (keep) {
            continue;
        }
        for (int r = 0; r < kBrickGridSize; ++r) {
            cell.brickUnits[r * kBrickGridSize + c].alive = false;
        }
    }
}

void Game::ApplyShovelFortification() {
    if (basePositionX_ < 0 || basePositionY_ < 0) {
        return;
    }
    fortifiedCells_.clear();
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue; // la base misma, no una celda a fortificar
            }
            const int x = basePositionX_ + dx;
            const int y = basePositionY_ + dy;
            if (!map_.InBounds(x, y)) {
                continue;
            }
            Cell& cell = map_.At(x, y);
            if (cell.type == TileType::Steel) {
                continue; // ya es hierro, no hace falta tocarla ni recordarla
            }
            fortifiedCells_.push_back(FortifiedCell{x, y, cell.type});
            cell.type = TileType::Steel;
            for (SteelUnit& unit : cell.steelUnits) {
                unit.alive = true;
                unit.hp = kSteelUnitMaxHp;
            }
        }
    }
    baseFortifyTimer_ = kShovelFortifyDuration;
}

void Game::TickBaseFortification(double dt) {
    if (baseFortifyTimer_ <= 0.0) {
        return;
    }
    baseFortifyTimer_ -= dt;
    if (baseFortifyTimer_ > 0.0) {
        return;
    }
    for (const FortifiedCell& fc : fortifiedCells_) {
        Cell& cell = map_.At(fc.x, fc.y);
        cell.type = fc.originalType;
        if (fc.originalType == TileType::Brick) {
            for (int r = 0; r < kBrickGridSize; ++r) {
                for (int c = 0; c < kBrickGridSize; ++c) {
                    BrickUnit& unit = cell.brickUnits[r * kBrickGridSize + c];
                    unit.alive = true;
                    unit.frame = BrickUnit::FrameFor(r, c);
                }
            }
        }
    }
    fortifiedCells_.clear();
}

void Game::ApplyGamepadInput(PlayerInput& input, int gamepadId, int playerIndex) const {
    if (gamepadId < 0 || !IsGamepadAvailable(gamepadId)) {
        return;
    }
    const float axisX = GetGamepadAxisMovement(gamepadId, GAMEPAD_AXIS_LEFT_X);
    const float axisY = GetGamepadAxisMovement(gamepadId, GAMEPAD_AXIS_LEFT_Y);
    input.moveUp = input.moveUp || IsGamepadButtonDown(gamepadId, GAMEPAD_BUTTON_LEFT_FACE_UP) || axisY < -kGamepadStickDeadzone;
    input.moveDown = input.moveDown || IsGamepadButtonDown(gamepadId, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || axisY > kGamepadStickDeadzone;
    input.moveLeft = input.moveLeft || IsGamepadButtonDown(gamepadId, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || axisX < -kGamepadStickDeadzone;
    input.moveRight = input.moveRight || IsGamepadButtonDown(gamepadId, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || axisX > kGamepadStickDeadzone;
    const PlayerGamepadBindings& bindings = playerGamepadBindings_[playerIndex];
    input.shoot = input.shoot || IsGamepadButtonDown(gamepadId, bindings.buttons[static_cast<int>(InputAction::Shoot)]);
    input.specialShoot = input.specialShoot || IsGamepadButtonDown(gamepadId, bindings.buttons[static_cast<int>(InputAction::Special)]);
}

void Game::ProcessInput() {
    if (appState_ == AppState::CharacterSelect) {
        ProcessCharacterSelectInput();
        return;
    }
    if (appState_ != AppState::Playing) {
        ProcessMenuInput();
        return;
    }

    // Menu de pausa ya abierto (ver paused_): se reusa ProcessMenuInput tal
    // cual (menuScreen_ esta en PauseMain o algo que cuelga de ahi), en vez
    // de tocar nada del input de gameplay de aca abajo. Excepcion pedida
    // explicitamente: si se vuelve a apretar el MISMO boton de Pausa
    // estando justo en la pantalla principal de pausa (no en un submenu
    // como Opciones/Mapeo de controles/Terminar partida — "solo funcione
    // durante un nivel"), se toma como "Continuar" y se cierra de una, sin
    // pasar por ProcessMenuInput este cuadro.
    if (paused_) {
        if (menuScreen_ == MenuScreen::PauseMain && IsPauseButtonPressed()) {
            paused_ = false;
            PlaySound(pauseToggleSound_);
            return;
        }
        ProcessMenuInput();
        return;
    }

    // Secuencia de fin de partida ya en curso (ver GameOverPhase): su propio
    // manejo de input (saltear la espera, tipear el nombre), nada del resto
    // de esta funcion aplica (los jugadores estan congelados, ver Update).
    if (gameOverPhase_ != GameOverPhase::None) {
        ProcessGameOverInput();
        return;
    }

    // Esquema de teclado por jugador (seccion 9): configurable desde el menu
    // (Opciones > Mapeo de controles, ver playerBindings_/InitDefaultKeyBindings),
    // arranca en WASD/flechas/IJKL/numpad como antes de que existiera el menu.
    auto ApplyKeyBindings = [](PlayerInput& input, const PlayerKeyBindings& bindings) {
        input.moveUp = IsKeyDown(bindings.keys[static_cast<int>(InputAction::Up)]);
        input.moveDown = IsKeyDown(bindings.keys[static_cast<int>(InputAction::Down)]);
        input.moveLeft = IsKeyDown(bindings.keys[static_cast<int>(InputAction::Left)]);
        input.moveRight = IsKeyDown(bindings.keys[static_cast<int>(InputAction::Right)]);
        input.shoot = IsKeyDown(bindings.keys[static_cast<int>(InputAction::Shoot)]);
        input.specialShoot = IsKeyDown(bindings.keys[static_cast<int>(InputAction::Special)]);
    };
    ApplyKeyBindings(input1_, playerBindings_[0]);
    ApplyKeyBindings(input2_, playerBindings_[1]);
    ApplyKeyBindings(input3_, playerBindings_[2]);
    ApplyKeyBindings(input4_, playerBindings_[3]);

    // Mandos (seccion custom): se suman al teclado de cada jugador (no lo
    // reemplazan, cualquiera de los dos mueve/dispara). El primer mando que
    // este conectado controla a P1, el segundo a P2, el tercero a P3 y el
    // cuarto a P4 (ver ApplyGamepadInput) — se recalcula entero cada frame,
    // asi que un mando enchufado despues de arrancar el juego (o
    // desconectado y vuelto a enchufar) se detecta y asigna solo, sin
    // reiniciar nada.
    int connectedGamepads[4] = {-1, -1, -1, -1};
    int connectedCount = 0;
    for (int i = 0; i < 4 && connectedCount < 4; ++i) {
        if (IsGamepadAvailable(i)) {
            connectedGamepads[connectedCount] = i;
            ++connectedCount;
        }
    }
    ApplyGamepadInput(input1_, connectedGamepads[0], 0);
    ApplyGamepadInput(input2_, connectedGamepads[1], 1);
    ApplyGamepadInput(input3_, connectedGamepads[2], 2);
    ApplyGamepadInput(input4_, connectedGamepads[3], 3);

    // Debug: que numero de boton reporta el mando al apretar cada cosa,
    // para calibrar (se muestra en el HUD, ver Render). Se guarda el ultimo
    // apretado en cualquier mando, no se pierde hasta el siguiente boton.
    if (kDebugFeaturesEnabled) {
        const int pressedButton = GetGamepadButtonPressed();
        if (pressedButton != -1) {
            lastGamepadButtonPressed_ = pressedButton;
        }
    }

    // Traen o hacen desaparecer a cada jugador (al arrancar solo esta
    // presente el jugador 1, ver ResetState); la tecla usada es la de
    // Inicio de cada uno (InputAction::Start), configurable desde el menu
    // igual que el resto (antes fija en 1/2/3/4). Solo por teclado (pedido
    // explicitamente: se quito del mando porque comparte fisicamente el
    // mismo boton "Play"/Menu con Pausa, y apretarlo terminaba disparando
    // las dos cosas a la vez).
    if (IsKeyPressed(playerBindings_[0].keys[static_cast<int>(InputAction::Start)])) {
        SetPlayerActive(player1Active_, player1_, player1Spawn_, player1SpawnX_, player1SpawnY_, !player1Active_, 0);
    }
    if (IsKeyPressed(playerBindings_[1].keys[static_cast<int>(InputAction::Start)])) {
        SetPlayerActive(player2Active_, player2_, player2Spawn_, player2SpawnX_, player2SpawnY_, !player2Active_, 1);
    }
    if (IsKeyPressed(playerBindings_[2].keys[static_cast<int>(InputAction::Start)])) {
        SetPlayerActive(player3Active_, player3_, player3Spawn_, player3SpawnX_, player3SpawnY_, !player3Active_, 2);
    }
    if (IsKeyPressed(playerBindings_[3].keys[static_cast<int>(InputAction::Start)])) {
        SetPlayerActive(player4Active_, player4_, player4Spawn_, player4SpawnX_, player4SpawnY_, !player4Active_, 3);
    }

    // Boton de prueba: repite el respawn en el punto de spawn inicial, para
    // poder ver el destello sin tener que reiniciar el juego (solo a los
    // jugadores presentes).
    if (kDebugFeaturesEnabled && IsKeyPressed(KEY_R)) {
        if (player1Active_) RespawnPlayer1();
        if (player2Active_) RespawnPlayer2();
        if (player3Active_) RespawnPlayer3();
        if (player4Active_) RespawnPlayer4();
    }

    // Esc corta la partida en curso y vuelve al menu principal (antes
    // reiniciaba la partida entera; era un boton de prueba, superado ahora
    // que existe un menu de verdad).
    if (IsKeyPressed(KEY_ESCAPE)) {
        appState_ = AppState::Menu;
        menuScreen_ = MenuScreen::Main;
    }

    // Pausa (pedido explicitamente, ver IsPauseButtonPressed): abre el menu
    // de pausa. Solo funciona durante un nivel en curso (pedido
    // explicitamente) — no durante la transicion de nivel, la pantalla de
    // puntuacion, ni con la partida ya terminada: esas pantallas ya
    // congelan/toman el control por su cuenta.
    if (IsPauseButtonPressed() && levelTransitionPhase_ == LevelTransitionPhase::None && scoreTallyPhase_ == ScoreTallyPhase::None && !gameOver_) {
        paused_ = true;
        menuScreen_ = MenuScreen::PauseMain;
        pauseMenuIndex_ = 0;
        PlaySound(pauseToggleSound_);
    }

    // Todo lo de aca abajo (F1-F12, +/-) son botones de prueba: forzar
    // power-ups, spawnear enemigos a mano, subir/bajar agresividad, saltar
    // de nivel, alternar fuego amigo por tecla y la grilla de referencia.
    // Ninguno es mecanica del juego final (fuego amigo y grilla ya tienen
    // su lugar en el menu/editor), por eso quedan atras de kDebugFeaturesEnabled.
    if (kDebugFeaturesEnabled) {
        if (IsKeyPressed(KEY_F1)) {
            powerUps_.ForceSpawn(PowerUpType::Star, map_, ActiveTankBounds());
        }
        if (IsKeyPressed(KEY_F2)) {
            powerUps_.ForceSpawn(PowerUpType::Helmet, map_, ActiveTankBounds());
        }
        if (IsKeyPressed(KEY_F4)) {
            powerUps_.ForceSpawn(PowerUpType::Gun, map_, ActiveTankBounds());
        }
        if (IsKeyPressed(KEY_F5)) {
            powerUps_.ForceSpawn(PowerUpType::Life, map_, ActiveTankBounds());
        }
        if (IsKeyPressed(KEY_F7)) {
            powerUps_.ForceSpawn(PowerUpType::Grenade, map_, ActiveTankBounds());
        }
        if (IsKeyPressed(KEY_F8)) {
            powerUps_.ForceSpawn(PowerUpType::Shovel, map_, ActiveTankBounds());
        }
        if (IsKeyPressed(KEY_F9)) {
            powerUps_.ForceSpawn(PowerUpType::Clock, map_, ActiveTankBounds());
        }

        // Agrega otro enemigo "Basico" (rotando entre los puntos de spawn
        // del nivel). Shift+F10 agrega uno "Rapido", Ctrl+F10 uno
        // "Blindado" y Ctrl+Shift+F10 uno "Power" en su lugar.
        if (IsKeyPressed(KEY_F10) && !enemySpawnPositions_.empty()) {
            const bool ctrlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            const bool shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            if (ctrlDown && shiftDown) {
                const size_t spawnIndex = powerEnemies_.Enemies().size() % enemySpawnPositions_.size();
                const std::array<int, 2>& pos = enemySpawnPositions_[spawnIndex];
                powerEnemies_.SpawnAt(static_cast<float>(pos[0]), static_cast<float>(pos[1]));
            } else if (ctrlDown) {
                const size_t spawnIndex = armorEnemies_.Enemies().size() % enemySpawnPositions_.size();
                const std::array<int, 2>& pos = enemySpawnPositions_[spawnIndex];
                armorEnemies_.SpawnAt(static_cast<float>(pos[0]), static_cast<float>(pos[1]));
            } else if (shiftDown) {
                const size_t spawnIndex = fastEnemies_.Enemies().size() % enemySpawnPositions_.size();
                const std::array<int, 2>& pos = enemySpawnPositions_[spawnIndex];
                fastEnemies_.SpawnAt(static_cast<float>(pos[0]), static_cast<float>(pos[1]));
            } else {
                const size_t spawnIndex = enemies_.Enemies().size() % enemySpawnPositions_.size();
                const std::array<int, 2>& pos = enemySpawnPositions_[spawnIndex];
                enemies_.SpawnAt(static_cast<float>(pos[0]), static_cast<float>(pos[1]));
            }
        }

        // Bajan/suben el nivel de agresividad de los enemigos (1 a 5, 3 es
        // el comportamiento de base), para los 4 tipos a la vez. Ver
        // EnemySystem::SetAggressivenessLevel.
        if (IsKeyPressed(KEY_F11)) {
            enemies_.SetAggressivenessLevel(enemies_.AggressivenessLevel() - 1);
            fastEnemies_.SetAggressivenessLevel(fastEnemies_.AggressivenessLevel() - 1);
            armorEnemies_.SetAggressivenessLevel(armorEnemies_.AggressivenessLevel() - 1);
            powerEnemies_.SetAggressivenessLevel(powerEnemies_.AggressivenessLevel() - 1);
        }
        if (IsKeyPressed(KEY_F12)) {
            enemies_.SetAggressivenessLevel(enemies_.AggressivenessLevel() + 1);
            fastEnemies_.SetAggressivenessLevel(fastEnemies_.AggressivenessLevel() + 1);
            armorEnemies_.SetAggressivenessLevel(armorEnemies_.AggressivenessLevel() + 1);
            powerEnemies_.SetAggressivenessLevel(powerEnemies_.AggressivenessLevel() + 1);
        }

        // +/- saltan directo al siguiente/anterior nivel de juego (1-40),
        // reiniciando todo (ver JumpToLevel) y arrancando ya con los
        // parametros de dificultad (agresividad, cuota, tope en pantalla,
        // y ahora tambien el mapa) que le correspondan a ese nivel.
        if (IsKeyPressed(KEY_KP_ADD) || IsKeyPressed(KEY_EQUAL)) {
            JumpToLevel(currentLevel_ + 1);
        }
        if (IsKeyPressed(KEY_KP_SUBTRACT) || IsKeyPressed(KEY_MINUS)) {
            JumpToLevel(currentLevel_ - 1);
        }

        // Rota el modo de fuego amigo Off -> nivel 1 -> nivel 2 -> Off
        // (mismo control ya disponible en Opciones > Fuego amigo).
        if (IsKeyPressed(KEY_F3)) {
            switch (friendlyFireMode_) {
                case FriendlyFireMode::Off:      friendlyFireMode_ = FriendlyFireMode::Paralyze; break;
                case FriendlyFireMode::Paralyze: friendlyFireMode_ = FriendlyFireMode::Damage;    break;
                case FriendlyFireMode::Damage:   friendlyFireMode_ = FriendlyFireMode::Off;       break;
            }
        }

        // Muestra/oculta la grilla de referencia sobre el mapa (ver
        // Render), para ubicar bloques al diseñar el escenario.
        if (IsKeyPressed(KEY_F6)) {
            showGrid_ = !showGrid_;
        }

        // Muestra/oculta un recuadro verde con la colision real de cada
        // objeto vivo (tanques, balas, unidades minimas de ladrillo/acero,
        // base, power-up), pedido explicitamente para depurar coliciones.
        if (IsKeyPressed(KEY_H)) {
            showHitboxes_ = !showHitboxes_;
        }
    }
}

bool Game::IsPauseButtonPressed() const {
    if (IsKeyPressed(KEY_ENTER)
        || IsKeyPressed(playerBindings_[0].keys[static_cast<int>(InputAction::Pause)])
        || IsKeyPressed(playerBindings_[1].keys[static_cast<int>(InputAction::Pause)])
        || IsKeyPressed(playerBindings_[2].keys[static_cast<int>(InputAction::Pause)])
        || IsKeyPressed(playerBindings_[3].keys[static_cast<int>(InputAction::Pause)])) {
        return true;
    }
    // Mismo criterio de asignacion que ApplyGamepadInput: el mando g-esimo
    // conectado (por orden de conexion) le corresponde al jugador g-esimo.
    int slotIndex = 0;
    for (int g = 0; g < 4 && slotIndex < 4; ++g) {
        if (IsGamepadAvailable(g)) {
            if (IsGamepadButtonPressed(g, playerGamepadBindings_[slotIndex].buttons[static_cast<int>(InputAction::Pause)])) {
                return true;
            }
            ++slotIndex;
        }
    }
    return false;
}

void Game::StartArcadeLocal() {
    ResetState();
    appState_ = AppState::Playing;
    BeginLevelTransition();
}

void Game::EnterCharacterSelect() {
    appState_ = AppState::CharacterSelect;
    player1Active_ = true;
    player2Active_ = false;
    player3Active_ = false;
    player4Active_ = false;
    playerColorIndex_[0] = 0; // amarillo por defecto; se cicla con izquierda/derecha (ver ProcessCharacterSelectInput)
}

TankSpriteSet& Game::SpriteSetForColor(int colorIndex) {
    switch (colorIndex) {
        case 1: return player2Sprites_;
        case 2: return player3Sprites_;
        case 3: return player4Sprites_;
        default: return player1Sprites_;
    }
}

Color Game::HudColorForColor(int colorIndex) const {
    switch (colorIndex) {
        case 1: return kPlayer2HudColor;
        case 2: return kPlayer3HudColor;
        case 3: return kPlayer4HudColor;
        default: return kPlayer1HudColor;
    }
}

int Game::FirstAvailableColor() const {
    bool used[4] = {false, false, false, false};
    if (player1Active_) used[playerColorIndex_[0]] = true;
    if (player2Active_) used[playerColorIndex_[1]] = true;
    if (player3Active_) used[playerColorIndex_[2]] = true;
    if (player4Active_) used[playerColorIndex_[3]] = true;
    for (int c = 0; c < 4; ++c) {
        if (!used[c]) {
            return c;
        }
    }
    return 0; // no deberia pasar: solo hay 4 slots para 4 colores
}

void Game::CycleLobbyColor(int slot, int direction) {
    const bool active[4] = {player1Active_, player2Active_, player3Active_, player4Active_};
    for (int attempt = 0; attempt < 4; ++attempt) {
        playerColorIndex_[slot] = (playerColorIndex_[slot] + direction + 4) % 4;
        bool takenByOther = false;
        for (int other = 0; other < 4; ++other) {
            if (other == slot) {
                continue;
            }
            if (active[other] && playerColorIndex_[other] == playerColorIndex_[slot]) {
                takenByOther = true;
                break;
            }
        }
        if (!takenByOther) {
            return;
        }
    }
    // Si llega aca, los otros 3 slots activos ya usan los otros 3 colores:
    // no queda ninguno libre para ciclar, se deja como quedo el ultimo intento.
}

int Game::GamepadIdForSlot(int slot) const {
    int slotIndex = 0;
    for (int g = 0; g < 4; ++g) {
        if (IsGamepadAvailable(g)) {
            if (slotIndex == slot) {
                return g;
            }
            ++slotIndex;
        }
    }
    return -1;
}

bool Game::IsSlotShootPressed(int slot) const {
    if (IsKeyPressed(playerBindings_[slot].keys[static_cast<int>(InputAction::Shoot)])) {
        return true;
    }
    const int gamepadId = GamepadIdForSlot(slot);
    return gamepadId != -1 && IsGamepadButtonPressed(gamepadId, playerGamepadBindings_[slot].buttons[static_cast<int>(InputAction::Shoot)]);
}

bool Game::IsSlotSpecialPressed(int slot) const {
    if (IsKeyPressed(playerBindings_[slot].keys[static_cast<int>(InputAction::Special)])) {
        return true;
    }
    const int gamepadId = GamepadIdForSlot(slot);
    return gamepadId != -1 && IsGamepadButtonPressed(gamepadId, playerGamepadBindings_[slot].buttons[static_cast<int>(InputAction::Special)]);
}

bool Game::IsSlotLeftPressed(int slot) const {
    if (IsKeyPressed(playerBindings_[slot].keys[static_cast<int>(InputAction::Left)])) {
        return true;
    }
    // El mando siempre mueve con D-pad/analogico, no es remapeable (ver
    // ApplyGamepadInput) — el mismo criterio aplica aca.
    const int gamepadId = GamepadIdForSlot(slot);
    return gamepadId != -1 && IsGamepadButtonPressed(gamepadId, GAMEPAD_BUTTON_LEFT_FACE_LEFT);
}

bool Game::IsSlotRightPressed(int slot) const {
    if (IsKeyPressed(playerBindings_[slot].keys[static_cast<int>(InputAction::Right)])) {
        return true;
    }
    const int gamepadId = GamepadIdForSlot(slot);
    return gamepadId != -1 && IsGamepadButtonPressed(gamepadId, GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
}

void Game::ProcessCharacterSelectInput() {
    if (IsKeyPressed(KEY_ESCAPE)) {
        // Se cancela del todo, sin dejar jugadores 2-4 colgados a medio
        // unir para la proxima vez que se entre a esta pantalla.
        player2Active_ = false;
        player3Active_ = false;
        player4Active_ = false;
        appState_ = AppState::Menu;
        menuScreen_ = MenuScreen::Main;
        return;
    }

    // Jugador 1 (slot 0): siempre presente, elige su color con izquierda/derecha.
    if (IsSlotLeftPressed(0)) {
        CycleLobbyColor(0, -1);
    } else if (IsSlotRightPressed(0)) {
        CycleLobbyColor(0, 1);
    }

    // Jugadores 2-4 (slots 1-3): se suman apretando Disparar (color libre
    // asignado solo, ver FirstAvailableColor), se van apretando Especial, y
    // mientras esten unidos tambien pueden cambiar su propio color con
    // izquierda/derecha (pedido explicitamente), igual que el jugador 1.
    bool* const activeFlags[4] = {&player1Active_, &player2Active_, &player3Active_, &player4Active_};
    for (int slot = 1; slot < 4; ++slot) {
        if (!*activeFlags[slot] && IsSlotShootPressed(slot)) {
            playerColorIndex_[slot] = FirstAvailableColor();
            *activeFlags[slot] = true;
        } else if (*activeFlags[slot] && IsSlotSpecialPressed(slot)) {
            *activeFlags[slot] = false;
        } else if (*activeFlags[slot] && IsSlotLeftPressed(slot)) {
            CycleLobbyColor(slot, -1);
        } else if (*activeFlags[slot] && IsSlotRightPressed(slot)) {
            CycleLobbyColor(slot, 1);
        }
    }

    // Cualquier jugador presente confirma y arranca la partida (mismo boton
    // que abre/cierra la pausa durante el juego).
    if (IsPauseButtonPressed()) {
        StartArcadeLocal();
    }
}

void Game::DrawBrickBorder(Rectangle box, float tileSize, int thickness) {
    const Texture2D tex = brickUnitTextures_[0];
    const Rectangle src{0.0f, 0.0f, static_cast<float>(tex.width), static_cast<float>(tex.height)};
    const int cols = static_cast<int>((box.width + tileSize - 1.0f) / tileSize) + thickness * 2;
    const int rows = static_cast<int>((box.height + tileSize - 1.0f) / tileSize) + thickness * 2;
    const float originX = box.x - static_cast<float>(thickness) * tileSize;
    const float originY = box.y - static_cast<float>(thickness) * tileSize;
    for (int r = 0; r < rows; ++r) {
        const bool inTopBorder = r < thickness;
        const bool inBottomBorder = r >= rows - thickness;
        for (int c = 0; c < cols; ++c) {
            const bool inLeftBorder = c < thickness;
            const bool inRightBorder = c >= cols - thickness;
            if (!(inTopBorder || inBottomBorder || inLeftBorder || inRightBorder)) {
                continue; // interior: lo pinta el rectangulo de fondo, no un ladrillo
            }
            const Rectangle dst{originX + static_cast<float>(c) * tileSize, originY + static_cast<float>(r) * tileSize, tileSize, tileSize};
            DrawTexturePro(tex, src, dst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        }
    }
}

void Game::RenderCharacterSelect() {
    BeginDrawing();
    ClearBackground(BLACK);

    const int centerX = windowWidth_ / 2;
    constexpr float kBoxW = 150.0f;
    constexpr float kBoxH = 200.0f;
    constexpr float kGap = 28.0f;
    constexpr float kBorderTile = 16.0f;
    constexpr int kBorderThickness = 2;

    const float totalWidth = 4.0f * kBoxW + 3.0f * kGap;
    const float startX = static_cast<float>(centerX) - totalWidth * 0.5f;
    const float boxY = static_cast<float>(windowHeight_) * 0.5f - kBoxH * 0.5f;

    const bool active[4] = {player1Active_, player2Active_, player3Active_, player4Active_};
    static const char* kLabels[4] = {"JUGADOR 1", "JUGADOR 2", "JUGADOR 3", "JUGADOR 4"};
    const bool blinkOn = static_cast<int>(GetTime() / kHudTextBlinkInterval) % 2 == 0;

    for (int slot = 0; slot < 4; ++slot) {
        const float boxX = startX + static_cast<float>(slot) * (kBoxW + kGap);
        const Rectangle box{boxX, boxY, kBoxW, kBoxH};
        const float boxCenterX = boxX + kBoxW * 0.5f;

        const char* label = kLabels[slot];
        DrawText(label, static_cast<int>(boxCenterX) - MeasureText(label, 20) / 2, static_cast<int>(boxY) - 34, 20, RAYWHITE);

        DrawRectangleRec(box, Color{18, 18, 22, 255});
        DrawBrickBorder(box, kBorderTile, kBorderThickness);

        if (active[slot]) {
            const Texture2D tankTex = SpriteSetForColor(playerColorIndex_[slot]).Get(1, Direction::Down, 0);
            constexpr float kTankScale = 6.0f; // sprite nativo de 16px -> 96px en pantalla
            const float tankSize = 16.0f * kTankScale;
            const Rectangle tankSrc{0.0f, 0.0f, static_cast<float>(tankTex.width), static_cast<float>(tankTex.height)};
            const Rectangle tankDst{boxCenterX - tankSize * 0.5f, boxY + kBoxH * 0.5f - tankSize * 0.5f, tankSize, tankSize};
            DrawTexturePro(tankTex, tankSrc, tankDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        } else if (blinkOn) {
            const char* line1 = "PULSE";
            const char* line2 = "DISPARAR";
            constexpr int kFontSize = 22;
            const float textY = boxY + kBoxH * 0.5f - static_cast<float>(kFontSize);
            DrawText(line1, static_cast<int>(boxCenterX) - MeasureText(line1, kFontSize) / 2, static_cast<int>(textY), kFontSize, RED);
            DrawText(line2, static_cast<int>(boxCenterX) - MeasureText(line2, kFontSize) / 2, static_cast<int>(textY) + kFontSize + 4, kFontSize, RED);
        }
    }

    const char* title = "SELECCION DE JUGADORES";
    DrawText(title, centerX - MeasureText(title, 26) / 2, static_cast<int>(boxY) - 90, 26, YELLOW);

    const char* hint1 = "Izquierda/Derecha: elegir color";
    const char* hint2 = "Disparar: unirse   Especial: salir   Pausa/Start: comenzar";
    DrawText(hint1, centerX - MeasureText(hint1, 16) / 2, windowHeight_ - 58, 16, GRAY);
    DrawText(hint2, centerX - MeasureText(hint2, 16) / 2, windowHeight_ - 34, 16, GRAY);

    EndDrawing();
}

void Game::ProcessMenuInput() {
    // El mando tambien puede manejar el menu (pedido explicitamente): el
    // primer mando conectado (se recalcula cada frame, asi que un mando
    // enchufado despues de arrancar el juego se detecta solo), con D-pad +
    // stick analogico izquierdo para moverse y A/B FIJOS (no el mapeo
    // reasignable de ningun jugador, ver playerGamepadBindings_) para
    // confirmar/volver — el menu se comporta siempre igual sin importar como
    // cada jugador tenga configurado Disparar/Especial para la partida. El
    // resto de los botones del mando no hacen nada aca.
    int menuGamepadId = -1;
    for (int g = 0; g < 4; ++g) {
        if (IsGamepadAvailable(g)) {
            menuGamepadId = g;
            break;
        }
    }
    const bool gamepadForMenu = menuGamepadId != -1;
    constexpr int kMenuConfirmButton = GAMEPAD_BUTTON_RIGHT_FACE_DOWN; // A
    constexpr int kMenuBackButton = GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;   // B

    // Stick analogico izquierdo: IsGamepadButtonPressed ya da deteccion de
    // flanco gratis para el D-pad, pero un eje analogico no tiene eso — hay
    // que armarla a mano comparando contra si ya estaba inclinado el cuadro
    // anterior (menuStickXHeld_), para no repetir el movimiento sin control
    // mientras se lo mantiene inclinado.
    constexpr float kStickThreshold = 0.5f;
    const float stickX = gamepadForMenu ? GetGamepadAxisMovement(menuGamepadId, GAMEPAD_AXIS_LEFT_X) : 0.0f;
    const float stickY = gamepadForMenu ? GetGamepadAxisMovement(menuGamepadId, GAMEPAD_AXIS_LEFT_Y) : 0.0f;
    const bool stickUpNow = gamepadForMenu && stickY < -kStickThreshold;
    const bool stickDownNow = gamepadForMenu && stickY > kStickThreshold;
    const bool stickLeftNow = gamepadForMenu && stickX < -kStickThreshold;
    const bool stickRightNow = gamepadForMenu && stickX > kStickThreshold;
    const bool stickUpPressed = stickUpNow && !menuStickUpHeld_;
    const bool stickDownPressed = stickDownNow && !menuStickDownHeld_;
    const bool stickLeftPressed = stickLeftNow && !menuStickLeftHeld_;
    const bool stickRightPressed = stickRightNow && !menuStickRightHeld_;
    menuStickUpHeld_ = stickUpNow;
    menuStickDownHeld_ = stickDownNow;
    menuStickLeftHeld_ = stickLeftNow;
    menuStickRightHeld_ = stickRightNow;

    const bool up = IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || stickUpPressed
        || (gamepadForMenu && IsGamepadButtonPressed(menuGamepadId, GAMEPAD_BUTTON_LEFT_FACE_UP));
    const bool down = IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) || stickDownPressed
        || (gamepadForMenu && IsGamepadButtonPressed(menuGamepadId, GAMEPAD_BUTTON_LEFT_FACE_DOWN));
    const bool left = IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A) || stickLeftPressed
        || (gamepadForMenu && IsGamepadButtonPressed(menuGamepadId, GAMEPAD_BUTTON_LEFT_FACE_LEFT));
    const bool right = IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D) || stickRightPressed
        || (gamepadForMenu && IsGamepadButtonPressed(menuGamepadId, GAMEPAD_BUTTON_LEFT_FACE_RIGHT));
    const bool confirm = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)
        || (gamepadForMenu && IsGamepadButtonPressed(menuGamepadId, kMenuConfirmButton));
    const bool back = IsKeyPressed(KEY_ESCAPE)
        || (gamepadForMenu && IsGamepadButtonPressed(menuGamepadId, kMenuBackButton));

    if (menuScreen_ == MenuScreen::Main) {
        constexpr int kItemCount = 6; // Arcade/Supervivencia/Versus/Modo construccion/Opciones/Salir
        if (up)   mainMenuIndex_ = (mainMenuIndex_ + kItemCount - 1) % kItemCount;
        if (down) mainMenuIndex_ = (mainMenuIndex_ + 1) % kItemCount;
        if (confirm) {
            if (mainMenuIndex_ <= 2) {
                selectedMainModeIndex_ = mainMenuIndex_;
                modeSubmenuIndex_ = 0;
                menuScreen_ = MenuScreen::ModeSubmenu;
            } else if (mainMenuIndex_ == 4) {
                optionsIndex_ = 0;
                optionsBackTarget_ = MenuScreen::Main;
                menuScreen_ = MenuScreen::Options;
            } else if (mainMenuIndex_ == 5) { // Salir: pide confirmacion antes de cerrar (pedido explicitamente)
                mainConfirmExitIndex_ = 1; // arranca en "No" cada vez, por seguridad
                menuScreen_ = MenuScreen::MainConfirmExit;
            }
            // mainMenuIndex_ == 3 (Modo construccion): todavia no
            // seleccionable, Enter no hace nada (ver RenderMenu para el
            // aviso en pantalla).
        }
    } else if (menuScreen_ == MenuScreen::MainConfirmExit) {
        constexpr int kItemCount = 2; // Si/No
        if (up)   mainConfirmExitIndex_ = (mainConfirmExitIndex_ + kItemCount - 1) % kItemCount;
        if (down) mainConfirmExitIndex_ = (mainConfirmExitIndex_ + 1) % kItemCount;
        if (confirm) {
            if (mainConfirmExitIndex_ == 0) { // Si: cerrar el juego (ver wantsExit_/Run())
                wantsExit_ = true;
            } else { // No
                menuScreen_ = MenuScreen::Main;
            }
        }
        if (back) {
            menuScreen_ = MenuScreen::Main; // Esc = No
        }
    } else if (menuScreen_ == MenuScreen::ModeSubmenu) {
        constexpr int kItemCount = 2; // Local/Lan ("Opciones" solo esta en la pantalla principal)
        if (up)   modeSubmenuIndex_ = (modeSubmenuIndex_ + kItemCount - 1) % kItemCount;
        if (down) modeSubmenuIndex_ = (modeSubmenuIndex_ + 1) % kItemCount;
        if (confirm && modeSubmenuIndex_ == 0 && selectedMainModeIndex_ == 0) {
            // Arcade > Local: el unico modo con partida real por ahora.
            // Local en Supervivencia/Versus, y Lan en cualquier modo:
            // todavia no implementados, Enter no hace nada. Pedido
            // explicitamente: ya no arranca directo, primero pasa por la
            // pantalla de seleccion de personaje (ver EnterCharacterSelect).
            EnterCharacterSelect();
        }
        if (back) {
            menuScreen_ = MenuScreen::Main;
        }
    } else if (menuScreen_ == MenuScreen::Options) {
        constexpr int kItemCount = 3; // Fuego amigo/Pantalla/Mapeo de controles
        if (up)   optionsIndex_ = (optionsIndex_ + kItemCount - 1) % kItemCount;
        if (down) optionsIndex_ = (optionsIndex_ + 1) % kItemCount;
        if (left || right) {
            if (optionsIndex_ == 0) {
                switch (friendlyFireMode_) {
                    case FriendlyFireMode::Off:      friendlyFireMode_ = FriendlyFireMode::Paralyze; break;
                    case FriendlyFireMode::Paralyze: friendlyFireMode_ = FriendlyFireMode::Damage;    break;
                    case FriendlyFireMode::Damage:   friendlyFireMode_ = FriendlyFireMode::Off;       break;
                }
            } else if (optionsIndex_ == 1) {
                ToggleFullscreen();
            }
            // optionsIndex_ == 2 (Mapeo de controles): no es un valor para
            // ciclar con flechas, se entra con Enter.
        }
        if (confirm && optionsIndex_ == 2) {
            controlMappingIndex_ = 0;
            menuScreen_ = MenuScreen::ControlMapping;
        }
        if (back) {
            menuScreen_ = optionsBackTarget_; // Main (titulo) o PauseMain (pausa), segun de donde se entro
        }
    } else if (menuScreen_ == MenuScreen::ControlMapping) {
        constexpr int kItemCount = 4; // Jugador 1-4
        if (up)   controlMappingIndex_ = (controlMappingIndex_ + kItemCount - 1) % kItemCount;
        if (down) controlMappingIndex_ = (controlMappingIndex_ + 1) % kItemCount;
        if (confirm) {
            mappingPlayerIndex_ = controlMappingIndex_;
            controlDeviceIndex_ = 0;
            menuScreen_ = MenuScreen::ControlDeviceChoice;
        }
        if (back) {
            menuScreen_ = MenuScreen::Options;
        }
    } else if (menuScreen_ == MenuScreen::ControlDeviceChoice) {
        constexpr int kItemCount = 2; // Teclado/Mando
        if (up)   controlDeviceIndex_ = (controlDeviceIndex_ + kItemCount - 1) % kItemCount;
        if (down) controlDeviceIndex_ = (controlDeviceIndex_ + 1) % kItemCount;
        if (confirm) {
            mappingGamepad_ = (controlDeviceIndex_ == 1);
            playerActionIndex_ = 0;
            menuScreen_ = MenuScreen::PlayerActions;
        }
        if (back) {
            menuScreen_ = MenuScreen::ControlMapping;
        }
    } else if (menuScreen_ == MenuScreen::PlayerActions) {
        // Teclado: las 7 acciones mapeables (ver kKeyMappableActions).
        // Mando: solo las 3 mapeables a un boton (ver
        // kGamepadMappableActions) — el movimiento con mando es siempre
        // D-pad + analogico, no se remapea. Inicio queda afuera de los dos.
        const int itemCount = mappingGamepad_ ? kGamepadMappableActionCount : kKeyMappableActionCount;
        if (up)   playerActionIndex_ = (playerActionIndex_ + itemCount - 1) % itemCount;
        if (down) playerActionIndex_ = (playerActionIndex_ + 1) % itemCount;
        if (confirm) {
            capturedKey_ = 0;
            menuScreen_ = MenuScreen::CapturingKey;
        }
        if (back) {
            menuScreen_ = MenuScreen::ControlDeviceChoice;
        }
    } else if (menuScreen_ == MenuScreen::PauseMain) {
        constexpr int kItemCount = 5; // Continuar/Guardar/Cargar/Opciones/Terminar partida
        if (up)   pauseMenuIndex_ = (pauseMenuIndex_ + kItemCount - 1) % kItemCount;
        if (down) pauseMenuIndex_ = (pauseMenuIndex_ + 1) % kItemCount;
        if (confirm) {
            if (pauseMenuIndex_ == 0) { // Continuar
                paused_ = false;
                PlaySound(pauseToggleSound_);
            } else if (pauseMenuIndex_ == 3) { // Opciones: identico al de la pantalla de titulo
                optionsIndex_ = 0;
                optionsBackTarget_ = MenuScreen::PauseMain;
                menuScreen_ = MenuScreen::Options;
            } else if (pauseMenuIndex_ == 4) { // Terminar partida
                pauseEndChoiceIndex_ = 0;
                menuScreen_ = MenuScreen::PauseEndChoice;
            }
            // Guardar/Cargar (1/2): todavia no disponibles (ver
            // RenderPauseOverlay), Enter no hace nada.
        }
        if (back) {
            paused_ = false; // Esc en el menu de pausa = Continuar
            PlaySound(pauseToggleSound_);
        }
    } else if (menuScreen_ == MenuScreen::PauseEndChoice) {
        constexpr int kItemCount = 2; // Menu principal/Cerrar el juego
        if (up)   pauseEndChoiceIndex_ = (pauseEndChoiceIndex_ + kItemCount - 1) % kItemCount;
        if (down) pauseEndChoiceIndex_ = (pauseEndChoiceIndex_ + 1) % kItemCount;
        if (confirm) {
            pauseConfirmIndex_ = 1; // arranca en "No" cada vez, por seguridad
            menuScreen_ = MenuScreen::PauseConfirmEnd;
        }
        if (back) {
            menuScreen_ = MenuScreen::PauseMain;
        }
    } else if (menuScreen_ == MenuScreen::PauseConfirmEnd) {
        constexpr int kItemCount = 2; // Si/No
        if (up)   pauseConfirmIndex_ = (pauseConfirmIndex_ + kItemCount - 1) % kItemCount;
        if (down) pauseConfirmIndex_ = (pauseConfirmIndex_ + 1) % kItemCount;
        if (confirm) {
            if (pauseConfirmIndex_ == 0) { // Si
                if (pauseEndChoiceIndex_ == 0) {
                    // Menu principal: mismo criterio que Esc durante la
                    // partida (ver ProcessInput), sin pasar por ResetState
                    // aca: la proxima vez que se elija Arcade > Local se
                    // arma todo de nuevo desde cero.
                    paused_ = false;
                    appState_ = AppState::Menu;
                    menuScreen_ = MenuScreen::Main;
                } else {
                    // Cerrar el juego: no se puede cerrar la ventana en el
                    // momento (ver wantsExit_/Run()).
                    wantsExit_ = true;
                }
            } else { // No
                menuScreen_ = MenuScreen::PauseEndChoice;
            }
        }
        if (back) {
            menuScreen_ = MenuScreen::PauseEndChoice; // Esc = No
        }
    } else { // MenuScreen::CapturingKey
        // Guardar/cancelar siempre por teclado (Enter/Esc) aca, aunque se
        // este mapeando un boton de mando: usar el confirm/back de arriba
        // (que tambien escuchan al mando) haria que apretar el boton que se
        // esta justo capturando lo guarde solo, sin poder reconsiderar.
        const bool keyboardConfirm = IsKeyPressed(KEY_ENTER);
        const bool keyboardBack = IsKeyPressed(KEY_ESCAPE);
        if (mappingGamepad_) {
            // Cualquier boton de mando detectado queda como candidato (se
            // puede seguir probando otros antes de confirmar), igual que
            // con el teclado.
            const int pressed = GetGamepadButtonPressed();
            if (pressed != -1) {
                capturedKey_ = pressed;
            }
            if (keyboardConfirm && capturedKey_ != 0) {
                const InputAction action = kGamepadMappableActions[playerActionIndex_];
                playerGamepadBindings_[mappingPlayerIndex_].buttons[static_cast<int>(action)] = capturedKey_;
                SaveControls();
                menuScreen_ = MenuScreen::PlayerActions;
            }
        } else {
            // Cualquier tecla detectada (menos Enter/Esc, reservadas para
            // guardar/cancelar) queda como candidata; se puede seguir
            // probando otras antes de confirmar.
            int pressed = GetKeyPressed();
            while (pressed != 0) {
                if (pressed != KEY_ENTER && pressed != KEY_ESCAPE) {
                    capturedKey_ = pressed;
                }
                pressed = GetKeyPressed();
            }
            if (keyboardConfirm && capturedKey_ != 0) {
                const InputAction action = kKeyMappableActions[playerActionIndex_];
                playerBindings_[mappingPlayerIndex_].keys[static_cast<int>(action)] = capturedKey_;
                SaveControls();
                menuScreen_ = MenuScreen::PlayerActions;
            }
        }
        if (keyboardBack) {
            menuScreen_ = MenuScreen::PlayerActions;
        }
    }
}

void Game::ProcessGameOverInput() {
    if (gameOverPhase_ == GameOverPhase::Holding) {
        // Mismo gesto que abre el menu de pausa durante la partida (pedido
        // explicitamente: "podemos tocar el boton de pausa"): el boton de
        // Pausa de cualquier jugador, en su teclado o en su mando asignado,
        // o Enter.
        int connectedGamepads[4] = {-1, -1, -1, -1};
        int connectedCount = 0;
        for (int i = 0; i < 4 && connectedCount < 4; ++i) {
            if (IsGamepadAvailable(i)) {
                connectedGamepads[connectedCount] = i;
                ++connectedCount;
            }
        }
        auto gamepadPausePressed = [&](int slotIndex) {
            const int gamepadId = connectedGamepads[slotIndex];
            return gamepadId != -1 && IsGamepadButtonPressed(gamepadId, playerGamepadBindings_[slotIndex].buttons[static_cast<int>(InputAction::Pause)]);
        };
        const bool skip = IsKeyPressed(KEY_ENTER)
            || IsKeyPressed(playerBindings_[0].keys[static_cast<int>(InputAction::Pause)])
            || IsKeyPressed(playerBindings_[1].keys[static_cast<int>(InputAction::Pause)])
            || IsKeyPressed(playerBindings_[2].keys[static_cast<int>(InputAction::Pause)])
            || IsKeyPressed(playerBindings_[3].keys[static_cast<int>(InputAction::Pause)])
            || gamepadPausePressed(0) || gamepadPausePressed(1) || gamepadPausePressed(2) || gamepadPausePressed(3);
        if (skip) {
            FinishGameOverHolding();
        }
        return;
    }

    if (gameOverPhase_ == GameOverPhase::NameEntry) {
        // Cualquier caracter imprimible se agrega (hasta kGameOverNameMaxLen),
        // Backspace borra el ultimo, Enter confirma (solo con algo tipeado).
        int pressed = GetCharPressed();
        while (pressed != 0) {
            if (pressed >= 32 && pressed < 127 && gameOverNameEntry_.size() < static_cast<size_t>(kGameOverNameMaxLen)) {
                gameOverNameEntry_.push_back(static_cast<char>(pressed));
            }
            pressed = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) && !gameOverNameEntry_.empty()) {
            gameOverNameEntry_.pop_back();
        }
        if (IsKeyPressed(KEY_ENTER) && !gameOverNameEntry_.empty()) {
            ConfirmGameOverNameEntry();
        }
        return;
    }

    // GameOverPhase::Rising: nada que hacer, la animacion corre sola (ver
    // TickGameOverSequence). No se puede saltear a proposito: es la unica
    // parte fija de la secuencia.
}

void Game::UpdatePlayer(Tank& tank, PlayerInput& input, SpawnFlash& spawn, int ownerId, const std::vector<Tank*>& others, double fixedDt) {
    if (tank.IsEliminated()) {
        return; // sin vidas: fuera de la partida, no se mueve ni dispara
    }

    tank.TickShield(fixedDt);
    tank.TickShootCooldown(fixedDt);
    tank.TickHeatDecay(fixedDt);
    tank.TickFreeze(fixedDt);
    tank.TickRecoil(fixedDt, map_, others);

    if (spawn.IsActive()) {
        // Mientras dura el destello de aparicion, el tanque no se mueve ni
        // dispara ni puede ser tocado por power-ups (seccion 5 y 13).
        spawn.Update(fixedDt);
        return;
    }

    tank.Update(fixedDt, input, map_, others);

    // Se consumen los dos triggers siempre (para no perder el flanco de
    // subida del boton mientras hay cooldown), pero solo disparan de verdad
    // si el tanque puede disparar. El especial tiene prioridad si ambos se
    // piden en el mismo frame.
    const bool normalTrigger = tank.ConsumeShootTrigger(input);
    const bool specialTrigger = tank.ConsumeSpecialShotTrigger(input);

    if (tank.CanShoot() && !tank.IsFrozen()) {
        float muzzleX = 0.0f, muzzleY = 0.0f;
        tank.MuzzlePosition(muzzleX, muzzleY);
        if (specialTrigger && tank.HasSpecialShotReady()) {
            bullets_.TryShootSpecial(ownerId, muzzleX, muzzleY, tank.Facing());
            tank.ConsumeSpecialShot();
            tank.RegisterSpecialShotHeat();
            tank.StartRecoil(kSpecialShotRecoilDistance);
            PlaySound(specialShootSound_);
        } else if (normalTrigger) {
            if (bullets_.TryShoot(ownerId, muzzleX, muzzleY, tank.Facing(), tank.BulletSpeed(), tank.WeaponLevel(), tank.MaxBullets())) {
                tank.RegisterNormalShotHeat();
                PlaySound(playerShootSound_);
            }
        }
    }

    PowerUpType pickedType{};
    if (powerUps_.TryPickup(tank.X(), tank.Y(), pickedType)) {
        if (pickedType == PowerUpType::Star) {
            tank.PickupStar();
        } else if (pickedType == PowerUpType::Helmet) {
            tank.ActivateShield(kHelmetShieldDuration);
        } else if (pickedType == PowerUpType::Gun) {
            tank.PickupGun();
        } else if (pickedType == PowerUpType::Life) {
            tank.AddLife();
        } else if (pickedType == PowerUpType::Shovel) {
            ApplyShovelFortification();
        } else if (pickedType == PowerUpType::Grenade) {
            DestroyAllEnemies(ownerId);
        } else if (pickedType == PowerUpType::Clock) {
            FreezeAllEnemies(kClockFreezeDuration);
        }

        // Pedido explicitamente: la vida extra tiene su propio sonido,
        // separado del resto de los items.
        PlaySound(pickedType == PowerUpType::Life ? extraLifeSound_ : itemPickupSound_);

        // Cualquier item agarrado suma lo mismo (investigado del juego
        // original: 500 parejo para los 7, no varia por tipo).
        AwardScoreAt(ownerId, tank.X() + 0.5f, tank.Y() + 0.5f, kScorePowerUpPickup);
    }
}

void Game::HandlePlayerDeath(Tank& tank, int ownerId) {
    tank.LoseLife();
    if (tank.IsOutOfLives()) {
        tank.Eliminate();
        return;
    }
    RespawnByOwnerId(ownerId);
}

std::vector<Tank*> Game::ActiveOthers(int excludeOwnerId) {
    std::vector<Tank*> others;
    others.reserve(3 + enemies_.Enemies().size() + fastEnemies_.Enemies().size() + armorEnemies_.Enemies().size() + powerEnemies_.Enemies().size());
    if (player1Active_ && excludeOwnerId != kPlayer1Id) {
        others.push_back(&player1_);
    }
    if (player2Active_ && excludeOwnerId != kPlayer2Id) {
        others.push_back(&player2_);
    }
    if (player3Active_ && excludeOwnerId != kPlayer3Id) {
        others.push_back(&player3_);
    }
    if (player4Active_ && excludeOwnerId != kPlayer4Id) {
        others.push_back(&player4_);
    }
    // Los jugadores tambien chocan contra los tanques enemigo, de cualquier
    // tipo (antes los atravesaban: solo los enemigos chequeaban colision
    // contra ellos).
    for (Enemy& enemy : enemies_.Enemies()) {
        if (enemy.alive) {
            others.push_back(&enemy.tank);
        }
    }
    for (Enemy& enemy : fastEnemies_.Enemies()) {
        if (enemy.alive) {
            others.push_back(&enemy.tank);
        }
    }
    for (Enemy& enemy : armorEnemies_.Enemies()) {
        if (enemy.alive) {
            others.push_back(&enemy.tank);
        }
    }
    for (Enemy& enemy : powerEnemies_.Enemies()) {
        if (enemy.alive) {
            others.push_back(&enemy.tank);
        }
    }
    return others;
}

std::vector<TankOccupiedBounds> Game::ActiveTankBounds() const {
    std::vector<TankOccupiedBounds> bounds;
    bounds.reserve(4);
    const Tank* activeTanks[4] = {
        player1Active_ ? &player1_ : nullptr,
        player2Active_ ? &player2_ : nullptr,
        player3Active_ ? &player3_ : nullptr,
        player4Active_ ? &player4_ : nullptr,
    };
    for (const Tank* tank : activeTanks) {
        if (tank == nullptr || tank->IsEliminated()) {
            continue;
        }
        TankOccupiedBounds b;
        tank->GetBounds(b.left, b.right, b.top, b.bottom);
        bounds.push_back(b);
    }
    return bounds;
}

void Game::ApplyEnemyPowerUpPickups() {
    // Basico -> Rapido -> Blindado -> Power con la estrella; la pistola
    // salta directo a Power desde cualquiera de los 3. SpawnAt reinicia la
    // direccion a Abajo y dispara el destello de aparicion (sirve como
    // efecto visual "se transformo" gratis); se le devuelve la direccion
    // que traia justo despues para no perderla.
    for (Enemy& enemy : enemies_.Enemies()) {
        if (!enemy.alive) {
            continue;
        }
        PowerUpType pickedType{};
        if (!powerUps_.TryPickup(enemy.tank.X(), enemy.tank.Y(), pickedType)) {
            continue;
        }
        PlaySound(itemPickupSound_);
        const Direction facing = enemy.tank.Facing();
        if (pickedType == PowerUpType::Star) {
            fastEnemies_.SpawnAt(enemy.tank.X(), enemy.tank.Y());
            fastEnemies_.Enemies().back().tank.SetFacing(facing);
            enemy.alive = false;
        } else if (pickedType == PowerUpType::Gun) {
            powerEnemies_.SpawnAt(enemy.tank.X(), enemy.tank.Y());
            powerEnemies_.Enemies().back().tank.SetFacing(facing);
            enemy.alive = false;
        } else if (pickedType == PowerUpType::Helmet) {
            enemy.tank.ActivateShield(kHelmetShieldDuration);
        }
    }
    for (Enemy& enemy : fastEnemies_.Enemies()) {
        if (!enemy.alive) {
            continue;
        }
        PowerUpType pickedType{};
        if (!powerUps_.TryPickup(enemy.tank.X(), enemy.tank.Y(), pickedType)) {
            continue;
        }
        PlaySound(itemPickupSound_);
        const Direction facing = enemy.tank.Facing();
        if (pickedType == PowerUpType::Star) {
            armorEnemies_.SpawnAt(enemy.tank.X(), enemy.tank.Y());
            armorEnemies_.Enemies().back().tank.SetFacing(facing);
            enemy.alive = false;
        } else if (pickedType == PowerUpType::Gun) {
            powerEnemies_.SpawnAt(enemy.tank.X(), enemy.tank.Y());
            powerEnemies_.Enemies().back().tank.SetFacing(facing);
            enemy.alive = false;
        } else if (pickedType == PowerUpType::Helmet) {
            enemy.tank.ActivateShield(kHelmetShieldDuration);
        }
    }
    for (Enemy& enemy : armorEnemies_.Enemies()) {
        if (!enemy.alive) {
            continue;
        }
        PowerUpType pickedType{};
        if (!powerUps_.TryPickup(enemy.tank.X(), enemy.tank.Y(), pickedType)) {
            continue;
        }
        PlaySound(itemPickupSound_);
        if (pickedType == PowerUpType::Star || pickedType == PowerUpType::Gun) {
            const Direction facing = enemy.tank.Facing();
            powerEnemies_.SpawnAt(enemy.tank.X(), enemy.tank.Y());
            powerEnemies_.Enemies().back().tank.SetFacing(facing);
            enemy.alive = false;
        } else if (pickedType == PowerUpType::Helmet) {
            enemy.tank.ActivateShield(kHelmetShieldDuration);
        }
    }
    // El Power no tiene un "siguiente" tipo: en su lugar, agarrar estrella o
    // pistola le arma el disparo especial (ver Enemy::specialShotFuseTimer y
    // PowerEnemySystem::Update), igual que a un jugador ya en nivel maximo
    // (ver Tank::PickupGun/PickupStar).
    for (Enemy& enemy : powerEnemies_.Enemies()) {
        if (!enemy.alive) {
            continue;
        }
        PowerUpType pickedType{};
        if (!powerUps_.TryPickup(enemy.tank.X(), enemy.tank.Y(), pickedType)) {
            continue;
        }
        PlaySound(itemPickupSound_);
        if (pickedType == PowerUpType::Helmet) {
            enemy.tank.ActivateShield(kHelmetShieldDuration);
        } else if (pickedType == PowerUpType::Star || pickedType == PowerUpType::Gun) {
            enemy.specialShotFuseTimer = 5.0;
        }
    }
}

void Game::DestroyAllEnemies(int creditOwnerId) {
    bool killedAny = false;
    for (Enemy& enemy : enemies_.Enemies()) {
        if (!enemy.alive) {
            continue;
        }
        specialExplosions_.Spawn(enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, /*nativeScale=*/true);
        enemy.alive = false;
        killedAny = true;
        ++enemiesKilledThisLevel_;
        AwardScoreAt(creditOwnerId, enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, kScoreBasicKill);
    }
    for (Enemy& enemy : fastEnemies_.Enemies()) {
        if (!enemy.alive) {
            continue;
        }
        specialExplosions_.Spawn(enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, /*nativeScale=*/true);
        enemy.alive = false;
        killedAny = true;
        ++enemiesKilledThisLevel_;
        AwardScoreAt(creditOwnerId, enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, kScoreFastKill);
    }
    for (Enemy& enemy : armorEnemies_.Enemies()) {
        if (!enemy.alive) {
            continue;
        }
        specialExplosions_.Spawn(enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, /*nativeScale=*/true);
        enemy.alive = false;
        killedAny = true;
        ++enemiesKilledThisLevel_;
        AwardScoreAt(creditOwnerId, enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, kScoreArmorKill);
    }
    for (Enemy& enemy : powerEnemies_.Enemies()) {
        if (!enemy.alive) {
            continue;
        }
        specialExplosions_.Spawn(enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, /*nativeScale=*/true);
        enemy.alive = false;
        killedAny = true;
        ++enemiesKilledThisLevel_;
        AwardScoreAt(creditOwnerId, enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, kScorePowerKill);
    }
    if (killedAny) {
        // Un solo sonido para toda la granada (pedido explicitamente "un
        // enemigo muere", pero varios a la vez sonando juntos era un caos):
        // se dispara una sola vez para toda la tanda, no por cada uno.
        PlaySound(tankDeathSound_);
    }
}

void Game::AwardScoreAt(int ownerId, float x, float y, int points) {
    int* score = nullptr;
    switch (ownerId) {
        case kPlayer1Id: score = &player1Score_; break;
        case kPlayer2Id: score = &player2Score_; break;
        case kPlayer3Id: score = &player3Score_; break;
        case kPlayer4Id: score = &player4Score_; break;
        default: return; // no era un jugador (por ejemplo un enemigo Power disparando su propio especial): no hay a quien acreditarle
    }
    *score += points;
    MaybeUpdateHighScore(*score, ownerId);
    CheckExtraLifeByScore(ownerId, *score);
    scorePopups_.Spawn(x, y, points);

    // Estadisticas del nivel actual, solo para la pantalla de puntuacion de
    // fin de nivel (ver PlayerLevelStats/ScoreTallyPhase): el total suma
    // cualquier puntaje (eliminacion o item), pero el conteo por tipo solo
    // se toca si points es exactamente uno de los 4 puntajes de eliminacion.
    PlayerLevelStats& levelStats = playerLevelStats_[ownerId];
    levelStats.scoreGained += points;
    switch (points) {
        case kScoreBasicKill: ++levelStats.kills[0]; break;
        case kScoreFastKill:  ++levelStats.kills[1]; break;
        case kScoreArmorKill: ++levelStats.kills[2]; break;
        case kScorePowerKill: ++levelStats.kills[3]; break;
        default: break; // por ejemplo un item agarrado: no es la eliminacion de ningun tipo
    }
}

void Game::CheckExtraLifeByScore(int ownerId, int newScore) {
    Tank* tank = nullptr;
    switch (ownerId) {
        case kPlayer1Id: tank = &player1_; break;
        case kPlayer2Id: tank = &player2_; break;
        case kPlayer3Id: tank = &player3_; break;
        case kPlayer4Id: tank = &player4_; break;
        default: return; // no era un jugador
    }
    if (tank->IsEliminated()) {
        return; // sin vidas, fuera de la partida: no tiene sentido darle mas
    }
    const int milestonesReached = newScore / kExtraLifeScoreThreshold;
    const int newMilestones = milestonesReached - extraLifeMilestonesClaimed_[ownerId];
    if (newMilestones <= 0) {
        return;
    }
    extraLifeMilestonesClaimed_[ownerId] = milestonesReached;
    for (int i = 0; i < newMilestones; ++i) {
        tank->AddLife();
    }
    PlaySound(extraLifeSound_);
}

void Game::LoadHighScore() {
    std::ifstream file(std::string(BC_ASSETS_DIR) + "highscore.txt");
    if (file.is_open()) {
        file >> highScore_;
        file.ignore(); // salta el salto de linea que deja el >> antes de leer el nombre
        std::getline(file, highScoreHolderName_); // archivos viejos (sin nombre) dejan esto vacio, sin romper nada
    }
}

void Game::SaveHighScore() {
    std::ofstream file(std::string(BC_ASSETS_DIR) + "highscore.txt");
    if (file.is_open()) {
        file << highScore_ << "\n" << highScoreHolderName_;
    }
}

void Game::LoadControls() {
    std::ifstream file(std::string(BC_ASSETS_DIR) + "controls.txt");
    if (!file.is_open()) {
        return; // primera vez / archivo borrado: se queda con los valores por defecto ya cargados
    }
    for (PlayerKeyBindings& bindings : playerBindings_) {
        for (int& key : bindings.keys) {
            if (!(file >> key)) {
                return; // archivo incompleto/corrupto: no pisar el resto con ceros
            }
        }
    }
    for (PlayerGamepadBindings& bindings : playerGamepadBindings_) {
        for (int& button : bindings.buttons) {
            if (!(file >> button)) {
                return;
            }
        }
    }
}

void Game::SaveControls() {
    std::ofstream file(std::string(BC_ASSETS_DIR) + "controls.txt");
    if (!file.is_open()) {
        return;
    }
    for (const PlayerKeyBindings& bindings : playerBindings_) {
        for (int key : bindings.keys) {
            file << key << "\n";
        }
    }
    for (const PlayerGamepadBindings& bindings : playerGamepadBindings_) {
        for (int button : bindings.buttons) {
            file << button << "\n";
        }
    }
}

void Game::MaybeUpdateHighScore(int candidateScore, int ownerId) {
    if (candidateScore > highScore_) {
        highScore_ = candidateScore;
        highScoreHolderId_ = ownerId;
        SaveHighScore();
    }
}

void Game::TriggerGameOver() {
    if (gameOver_) {
        return; // ya estaba en curso (por ejemplo, aguila destruida y ultima vida perdida el mismo frame)
    }
    gameOver_ = true;
    gameOverPhase_ = GameOverPhase::Rising;
    gameOverTimer_ = 0.0;
}

void Game::TickGameOverSequence(double dt) {
    if (gameOverPhase_ == GameOverPhase::Rising) {
        gameOverTimer_ += dt;
        if (gameOverTimer_ >= kGameOverRiseDuration) {
            gameOverPhase_ = GameOverPhase::Holding;
            gameOverTimer_ = 0.0;
        }
        return;
    }
    if (gameOverPhase_ == GameOverPhase::Holding) {
        gameOverTimer_ += dt;
        if (gameOverTimer_ >= kGameOverHoldDuration) {
            FinishGameOverHolding();
        }
        return;
    }
    // GameOverPhase::NameEntry: nada que tickear por tiempo, es por input
    // (ver ProcessGameOverInput).
}

void Game::FinishGameOverHolding() {
    int bestOwnerId = -1;
    int bestScore = -1;
    const int scores[4] = {player1Score_, player2Score_, player3Score_, player4Score_};
    const bool active[4] = {player1Active_, player2Active_, player3Active_, player4Active_};
    for (int i = 0; i < 4; ++i) {
        if (active[i] && scores[i] > bestScore) {
            bestScore = scores[i];
            bestOwnerId = i;
        }
    }

    // Record nuevo = el mejor puntaje de ESTA partida supera lo que habia
    // ANTES de que arrancara (ver matchStartHighScore_), no highScore_ (que
    // ya puede estar al dia con este mismo numero desde hace rato, ver
    // MaybeUpdateHighScore/AwardScoreAt) — asi solo se pide nombre una vez,
    // al final, sin importar cuando exactamente se supero el record.
    if (bestOwnerId != -1 && bestScore > matchStartHighScore_) {
        gameOverWinnerOwnerId_ = bestOwnerId;
        gameOverWinnerScore_ = bestScore;
        gameOverNameEntry_.clear();
        gameOverPhase_ = GameOverPhase::NameEntry;
    } else {
        FinishGameOverSequence();
    }
}

void Game::ConfirmGameOverNameEntry() {
    // highScore_ ya esta al dia (MaybeUpdateHighScore lo actualizo en
    // caliente durante la partida apenas alguien lo supero); lo unico que
    // falta persistir es el nombre.
    highScoreHolderName_ = gameOverNameEntry_;
    SaveHighScore();
    FinishGameOverSequence();
}

void Game::FinishGameOverSequence() {
    gameOverPhase_ = GameOverPhase::None;
    appState_ = AppState::Menu;
    menuScreen_ = MenuScreen::Main;
}

void Game::FreezeAllEnemies(double duration) {
    for (Enemy& enemy : enemies_.Enemies()) {
        if (enemy.alive) {
            enemy.tank.Freeze(duration);
        }
    }
    for (Enemy& enemy : fastEnemies_.Enemies()) {
        if (enemy.alive) {
            enemy.tank.Freeze(duration);
        }
    }
    for (Enemy& enemy : armorEnemies_.Enemies()) {
        if (enemy.alive) {
            enemy.tank.Freeze(duration);
        }
    }
    for (Enemy& enemy : powerEnemies_.Enemies()) {
        if (enemy.alive) {
            enemy.tank.Freeze(duration);
        }
    }
}

void Game::ApplyAggressivenessForCurrentLevel() {
    const int aggro = WaveTierForLevel(currentLevel_).aggressivenessLevel;
    enemies_.SetAggressivenessLevel(aggro);
    fastEnemies_.SetAggressivenessLevel(aggro);
    armorEnemies_.SetAggressivenessLevel(aggro);
    powerEnemies_.SetAggressivenessLevel(aggro);
}

void Game::JumpToLevel(int newLevel) {
    // ResetState ya deja todo (mapa, jugadores, balas, power-ups) como al
    // arrancar la partida, pero siempre carga el mapa del nivel 1: se
    // descarta y se vuelve a cargar entero (mapa + spawns + enjambre
    // inicial) para el nivel destino.
    ResetState();
    currentLevel_ = std::clamp(newLevel, 1, kMaxGameLevel);
    LoadCurrentLevelMap();

    if (player1Active_) RespawnPlayer1();
    if (player2Active_) RespawnPlayer2();
    if (player3Active_) RespawnPlayer3();
    if (player4Active_) RespawnPlayer4();

    enemies_ = EnemySystem{};
    fastEnemies_ = FastEnemySystem{};
    armorEnemies_ = ArmorEnemySystem{};
    powerEnemies_ = PowerEnemySystem{};
    enemiesSpawnedThisLevel_ = 0;
    enemiesKilledThisLevel_ = 0;
    playerLevelStats_ = {};

    ApplyAggressivenessForCurrentLevel();
    BuildEnemyTypeSequenceForLevel();
    BeginLevelTransition();
}

int Game::CountAliveEnemies() const {
    int count = 0;
    for (const Enemy& enemy : enemies_.Enemies()) {
        count += enemy.alive ? 1 : 0;
    }
    for (const Enemy& enemy : fastEnemies_.Enemies()) {
        count += enemy.alive ? 1 : 0;
    }
    for (const Enemy& enemy : armorEnemies_.Enemies()) {
        count += enemy.alive ? 1 : 0;
    }
    for (const Enemy& enemy : powerEnemies_.Enemies()) {
        count += enemy.alive ? 1 : 0;
    }
    return count;
}

bool Game::IsEnemySpawningAtCell(int cellX, int cellY) const {
    auto anySpawningAt = [cellX, cellY](const std::vector<Enemy>& list) {
        for (const Enemy& enemy : list) {
            if (!enemy.alive || !enemy.spawn.IsActive()) {
                continue;
            }
            if (static_cast<int>(std::round(enemy.spawn.X())) == cellX && static_cast<int>(std::round(enemy.spawn.Y())) == cellY) {
                return true;
            }
        }
        return false;
    };
    return anySpawningAt(enemies_.Enemies()) || anySpawningAt(fastEnemies_.Enemies()) ||
           anySpawningAt(armorEnemies_.Enemies()) || anySpawningAt(powerEnemies_.Enemies());
}

void Game::BuildEnemyTypeSequenceForLevel() {
    // Se dimensiona a la cuota MAXIMA del nivel (con 3-4 jugadores), que
    // siempre es >= la cuota con 1-2 jugadores en las 4 franjas de
    // dificultad: asi alcanza sin importar cuantos jugadores esten activos
    // en el momento de cada spawn (killQuota se reevalua dinamicamente).
    const int totalQuota = WaveTierForLevel(currentLevel_).killQuotaManyPlayers;
    const EnemyTypeCounts counts = EnemyTypeCountsForLevel(currentLevel_, totalQuota);

    levelEnemyTypeSequence_.clear();
    levelEnemyTypeSequence_.reserve(totalQuota);
    levelEnemyTypeSequence_.insert(levelEnemyTypeSequence_.end(), counts.basic, 0);
    levelEnemyTypeSequence_.insert(levelEnemyTypeSequence_.end(), counts.fast, 1);
    levelEnemyTypeSequence_.insert(levelEnemyTypeSequence_.end(), counts.armor, 2);
    levelEnemyTypeSequence_.insert(levelEnemyTypeSequence_.end(), counts.power, 3);

    // El orden en que salen si es al azar (pedido explicitamente), aunque
    // los conteos totales de cada tipo sean fijos: Fisher-Yates con
    // GetRandomValue, el mismo generador que ya usa el resto de Game.cpp.
    for (int i = static_cast<int>(levelEnemyTypeSequence_.size()) - 1; i > 0; --i) {
        const int j = GetRandomValue(0, i);
        std::swap(levelEnemyTypeSequence_[i], levelEnemyTypeSequence_[j]);
    }
    nextEnemyTypeIndex_ = 0;
}

void Game::SpawnNextEnemyForLevelAt(float x, float y) {
    // No deberia pasar (la secuencia se arma del tamano de la cuota maxima
    // del nivel), pero si se agotara cae a Basico como resguardo.
    const int type = (nextEnemyTypeIndex_ < levelEnemyTypeSequence_.size())
        ? levelEnemyTypeSequence_[nextEnemyTypeIndex_++]
        : 0;
    switch (type) {
        case 1: fastEnemies_.SpawnAt(x, y); break;
        case 2: armorEnemies_.SpawnAt(x, y); break;
        case 3: powerEnemies_.SpawnAt(x, y); break;
        default: enemies_.SpawnAt(x, y); break;
    }
}

void Game::SpawnInitialWaveForLevel() {
    // Apenas arranca el nivel, las 3 celdas de spawn largan un enemigo cada
    // una al mismo tiempo (pedido explicitamente), en vez de esperar la
    // rotacion de a una que usa TickEnemySpawning despues. El temporizador
    // queda listo para que el primer spawn "de a uno" tarde el intervalo
    // normal, no que salga otro enemigo de inmediato encima de esta tanda.
    for (const std::array<int, 2>& cell : enemySpawnPositions_) {
        SpawnNextEnemyForLevelAt(static_cast<float>(cell[0]), static_cast<float>(cell[1]));
        ++enemiesSpawnedThisLevel_;
    }
    nextEnemySpawnCellIndex_ = 0;
    enemySpawnTimer_ = kEnemySpawnInterval;
}

void Game::BeginLevelTransition() {
    levelTransitionPhase_ = LevelTransitionPhase::ShowingText;
    levelTransitionTimer_ = kLevelTransitionTextDuration;
    PlaySound(stageStartSound_);
}

void Game::TickEnemySpawning(double dt) {
    if (enemySpawnPositions_.empty()) {
        return;
    }

    enemySpawnTimer_ -= dt;
    if (enemySpawnTimer_ > 0.0) {
        return;
    }
    enemySpawnTimer_ = kEnemySpawnInterval;

    const EnemyWaveTier& tier = WaveTierForLevel(currentLevel_);
    const int activePlayers = (player1Active_ ? 1 : 0) + (player2Active_ ? 1 : 0) + (player3Active_ ? 1 : 0) + (player4Active_ ? 1 : 0);
    const bool fewPlayers = activePlayers <= 2;
    const int killQuota = fewPlayers ? tier.killQuotaFewPlayers : tier.killQuotaManyPlayers;
    const int maxOnScreen = fewPlayers ? tier.maxOnScreenFewPlayers : tier.maxOnScreenManyPlayers;

    if (enemiesSpawnedThisLevel_ >= killQuota) {
        return; // ya se termino de spawnear la cuota de este nivel; falta que mueran los que quedan
    }
    if (CountAliveEnemies() >= maxOnScreen) {
        return; // pantalla llena para el nivel actual
    }

    const std::array<int, 2>& cell = enemySpawnPositions_[nextEnemySpawnCellIndex_ % enemySpawnPositions_.size()];
    if (IsEnemySpawningAtCell(cell[0], cell[1])) {
        return; // celda ocupada por otro que todavia esta apareciendo: reintenta la misma celda el proximo tick
    }

    SpawnNextEnemyForLevelAt(static_cast<float>(cell[0]), static_cast<float>(cell[1]));
    ++enemiesSpawnedThisLevel_;
    nextEnemySpawnCellIndex_ = (nextEnemySpawnCellIndex_ + 1) % enemySpawnPositions_.size();
}

void Game::CheckLevelCompletion(double dt) {
    if (levelClearPending_) {
        levelClearTimer_ -= dt;
        if (levelClearTimer_ <= 0.0) {
            levelClearPending_ = false;
            BeginScoreTally();
        }
        return;
    }

    const EnemyWaveTier& tier = WaveTierForLevel(currentLevel_);
    const int activePlayers = (player1Active_ ? 1 : 0) + (player2Active_ ? 1 : 0) + (player3Active_ ? 1 : 0) + (player4Active_ ? 1 : 0);
    const int killQuota = (activePlayers <= 2) ? tier.killQuotaFewPlayers : tier.killQuotaManyPlayers;
    if (enemiesKilledThisLevel_ < killQuota) {
        return;
    }

    levelClearPending_ = true;
    levelClearTimer_ = kLevelClearDelay;
}

void Game::AdvanceToNextLevel() {
    if (currentLevel_ < kMaxGameLevel) {
        ++currentLevel_;
    }

    // Nivel nuevo = mapa nuevo (ver LoadCurrentLevelMap): se limpia todo lo
    // que no tiene sentido llevarse de un mapa al otro (enemigos, balas,
    // power-ups) y se reubica a los jugadores activos en los spawns del
    // mapa nuevo, con el mismo destello+escudo que al aparecer. Vidas,
    // nivel de arma y puntaje de cada jugador NO se tocan: siguen de un
    // nivel al otro.
    LoadCurrentLevelMap();
    bullets_ = BulletSystem{};
    bulletImpacts_ = BulletImpactSystem{};
    specialExplosions_ = SpecialExplosionSystem{};
    specialExplosionEvents_.clear();
    powerUps_ = PowerUpSystem{};
    screenShakeTimer_ = 0.0;
    enemies_ = EnemySystem{};
    fastEnemies_ = FastEnemySystem{};
    armorEnemies_ = ArmorEnemySystem{};
    powerEnemies_ = PowerEnemySystem{};

    if (player1Active_) RespawnPlayer1();
    if (player2Active_) RespawnPlayer2();
    if (player3Active_) RespawnPlayer3();
    if (player4Active_) RespawnPlayer4();

    enemiesKilledThisLevel_ = 0;
    enemiesSpawnedThisLevel_ = 0;
    enemySpawnTimer_ = 0.0;
    nextEnemySpawnCellIndex_ = 0;
    playerLevelStats_ = {}; // el conteo/puntaje de la pantalla de puntuacion es solo del nivel que se esta por empezar
    ApplyAggressivenessForCurrentLevel();
    BuildEnemyTypeSequenceForLevel();
    BeginLevelTransition();
}

void Game::BeginScoreTally() {
    scoreTallyPhase_ = ScoreTallyPhase::Counting;
    scoreTallyLevelShown_ = currentLevel_;
    scoreTallyAnim_ = {};
    scoreTallyBonusAwarded_ = {};
    scoreTallyBonusRevealTimer_ = 0.0;
    scoreTallyBonusRevealed_ = false;
    scoreTallySlot_ = 0;
}

void Game::TickScoreTally(double dt) {
    if (scoreTallyPhase_ == ScoreTallyPhase::Holding) {
        scoreTallyTimer_ -= dt;
        // Pedido explicitamente: el bono (si hay alguno pendiente, ver
        // allRowsDone mas abajo) recien se suma al puntaje real y se
        // muestra en pantalla despues de este segundo extra de espera
        // DENTRO de Holding, no apenas termina el conteo.
        if (!scoreTallyBonusRevealed_ && scoreTallyBonusRevealTimer_ > 0.0) {
            scoreTallyBonusRevealTimer_ -= dt;
            if (scoreTallyBonusRevealTimer_ <= 0.0) {
                scoreTallyBonusRevealed_ = true;
                int* scores[4] = {&player1Score_, &player2Score_, &player3Score_, &player4Score_};
                bool awardedAny = false;
                for (int i = 0; i < 4; ++i) {
                    if (scoreTallyBonusAwarded_[i]) {
                        *scores[i] += kScoreTallyClearBonus;
                        MaybeUpdateHighScore(*scores[i], i);
                        CheckExtraLifeByScore(i, *scores[i]);
                        awardedAny = true;
                    }
                }
                if (awardedAny) {
                    PlaySound(scoreBonusSound_);
                }
            }
        }
        if (scoreTallyTimer_ <= 0.0) {
            scoreTallyPhase_ = ScoreTallyPhase::None;
            AdvanceToNextLevel();
        }
        return;
    }

    // Counting: TODOS los jugadores activos cuentan el MISMO slot de tipo de
    // enemigo a la vez (pedido explicitamente, ver scoreTallySlot_) — cada
    // uno a su propio ritmo segun cuanto mato de ese tipo, pero el que
    // termina antes (mato menos de ese tipo) simplemente espera: recien
    // cuando TODOS los activos terminaron de contar este slot (incluida la
    // pausa de kScoreTallyRowPause) se avanza al siguiente, para todos
    // juntos. Al llegar al slot 4 ya se ve TOTAL en cada fila; cuando
    // scoreTallySlot_ llega ahi, pasa a Holding (pausa final antes de
    // avanzar de nivel).
    const bool active[4] = {player1Active_, player2Active_, player3Active_, player4Active_};
    bool allRowsDone = (scoreTallySlot_ >= 4);
    bool allActiveDoneWithSlot = true;
    for (int i = 0; i < 4; ++i) {
        if (!active[i]) {
            continue;
        }
        ScoreTallyRowAnim& anim = scoreTallyAnim_[i];
        anim.currentSlot = scoreTallySlot_; // reflejado nada mas, ver ScoreTallyRowAnim
        if (scoreTallySlot_ >= 4) {
            continue; // esta fila ya esta en TOTAL, no hay nada que tickear
        }
        const int target = playerLevelStats_[i].kills[scoreTallySlot_];
        anim.timer += dt;
        if (anim.currentCount < target) {
            allActiveDoneWithSlot = false;
            if (anim.timer >= kScoreTallyCountInterval) {
                anim.timer -= kScoreTallyCountInterval;
                ++anim.currentCount;
                PlaySound(scoreTickSound_);
            }
        } else if (anim.timer < kScoreTallyRowPause) {
            allActiveDoneWithSlot = false; // ya llego a su numero, pero todavia le falta la pausa
        }
    }

    if (scoreTallySlot_ < 4 && allActiveDoneWithSlot) {
        ++scoreTallySlot_;
        for (int i = 0; i < 4; ++i) {
            scoreTallyAnim_[i].currentCount = 0;
            scoreTallyAnim_[i].timer = 0.0;
            // Bug corregido: sin esto, currentSlot se quedaba pisado con el
            // valor VIEJO hasta la vuelta de bucle siguiente — pero si este
            // era el ultimo slot, la fase pasaba a Holding en este mismo
            // llamado y esa vuelta siguiente nunca llegaba, dejando
            // currentSlot trabado en 3 para siempre (nunca >= 4): ni TOTAL
            // ni el bono llegaban a dibujarse, y el numero del ultimo tipo
            // se veia resetear a 0 sin explicacion (currentCount ya en 0,
            // pero currentSlot todavia en el slot viejo).
            scoreTallyAnim_[i].currentSlot = scoreTallySlot_;
        }
        allRowsDone = (scoreTallySlot_ >= 4);
    }

    if (allRowsDone) {
        // Bono de fin de nivel (pedido explicito): el/los jugador(es) con
        // mayor scoreGained este nivel (empate incluido) reciben el bono.
        // Pedido explicitamente: solo tiene sentido con MAS de un jugador
        // activo (con uno solo siempre "ganaria" el mismo, sin gracia); con
        // uno solo queda todo en false y nunca se suma ni se muestra nada.
        // El puntaje real y el sonido (ver scoreBonusSound_) recien se
        // aplican 1s despues, al revelarse (ver el tick de Holding arriba);
        // aca solo se decide QUIEN lo va a recibir.
        const int activeCount = (active[0] ? 1 : 0) + (active[1] ? 1 : 0) + (active[2] ? 1 : 0) + (active[3] ? 1 : 0);
        if (activeCount > 1) {
            int maxGained = -1;
            for (int i = 0; i < 4; ++i) {
                if (active[i]) {
                    maxGained = std::max(maxGained, playerLevelStats_[i].scoreGained);
                }
            }
            for (int i = 0; i < 4; ++i) {
                scoreTallyBonusAwarded_[i] = active[i] && maxGained >= 0 && playerLevelStats_[i].scoreGained == maxGained;
            }
            scoreTallyBonusRevealTimer_ = kScoreTallyBonusRevealDelay;
        }

        scoreTallyPhase_ = ScoreTallyPhase::Holding;
        scoreTallyTimer_ = kScoreTallyHoldDuration;
    }
}

int Game::EnemiesRemainingThisLevel() const {
    const EnemyWaveTier& tier = WaveTierForLevel(currentLevel_);
    const int activePlayers = (player1Active_ ? 1 : 0) + (player2Active_ ? 1 : 0) + (player3Active_ ? 1 : 0) + (player4Active_ ? 1 : 0);
    const int killQuota = (activePlayers <= 2) ? tier.killQuotaFewPlayers : tier.killQuotaManyPlayers;
    return std::max(0, killQuota - enemiesKilledThisLevel_);
}

std::vector<Tank*> Game::ActivePlayerTanks() {
    std::vector<Tank*> tanks;
    tanks.reserve(4);
    Tank* activeTanks[4] = {
        player1Active_ ? &player1_ : nullptr,
        player2Active_ ? &player2_ : nullptr,
        player3Active_ ? &player3_ : nullptr,
        player4Active_ ? &player4_ : nullptr,
    };
    for (Tank* tank : activeTanks) {
        if (tank == nullptr || tank->IsEliminated()) {
            continue;
        }
        tanks.push_back(tank);
    }
    return tanks;
}

bool Game::IsPositionBlockedByMap(const Tank& tank, float newX, float newY) const {
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    tank.GetBounds(left, right, top, bottom);
    const float width = right - left;
    const float height = bottom - top;
    const float newLeft = newX;
    const float newRight = newX + width;
    const float newTop = newY;
    const float newBottom = newY + height;
    if (newLeft < 0.0f || newTop < 0.0f || newRight > static_cast<float>(map_.Width()) || newBottom > static_cast<float>(map_.Height())) {
        return true;
    }
    return map_.IsBoxBlocked(newLeft, newRight, newTop, newBottom);
}

void Game::ResolveTankOverlaps() {
    // Solo tanques ya "materializados" (vivos/activos y con el destello de
    // aparicion terminado: mientras dura no se mueven ni son colisionables
    // de verdad, ver UpdatePlayer/EnemySystem::Update). Recolectados en una
    // sola lista pareja (jugadores + los 4 tipos de enemigo) para resolver
    // cualquier combinacion de a pares.
    std::vector<Tank*> tanks;
    tanks.reserve(24);

    struct PlayerRef {
        Tank* tank;
        bool active;
        const SpawnFlash* spawn;
    };
    const PlayerRef playerRefs[4] = {
        {&player1_, player1Active_, &player1Spawn_},
        {&player2_, player2Active_, &player2Spawn_},
        {&player3_, player3Active_, &player3Spawn_},
        {&player4_, player4Active_, &player4Spawn_},
    };
    for (const PlayerRef& ref : playerRefs) {
        if (ref.active && !ref.tank->IsEliminated() && !ref.spawn->IsActive()) {
            tanks.push_back(ref.tank);
        }
    }

    auto addAliveEnemies = [&tanks](auto& enemySystem) {
        for (Enemy& enemy : enemySystem.Enemies()) {
            if (enemy.alive && !enemy.spawn.IsActive()) {
                tanks.push_back(&enemy.tank);
            }
        }
    };
    addAliveEnemies(enemies_);
    addAliveEnemies(fastEnemies_);
    addAliveEnemies(armorEnemies_);
    addAliveEnemies(powerEnemies_);

    // Separa cada par que este solapado, mitad y mitad, por el eje de MENOR
    // penetracion (el "camino mas corto" para dejar de tocarse). Cada
    // propuesta se valida antes contra el mapa (nunca empuja a un tanque
    // adentro de una pared); si quedaria bloqueada, ese tanque en particular
    // simplemente no se mueve este frame (el otro del par si puede, y si
    // ninguno puede, se vuelve a intentar el proximo frame).
    constexpr float kSeparationEpsilon = 0.01f;
    for (size_t i = 0; i < tanks.size(); ++i) {
        for (size_t j = i + 1; j < tanks.size(); ++j) {
            Tank* a = tanks[i];
            Tank* b = tanks[j];
            float aLeft = 0.0f, aRight = 0.0f, aTop = 0.0f, aBottom = 0.0f;
            float bLeft = 0.0f, bRight = 0.0f, bTop = 0.0f, bBottom = 0.0f;
            a->GetBounds(aLeft, aRight, aTop, aBottom);
            b->GetBounds(bLeft, bRight, bTop, bBottom);
            if (!(aLeft < bRight && aRight > bLeft && aTop < bBottom && aBottom > bTop)) {
                continue; // no se solapan: nada que hacer
            }

            const float overlapX = std::min(aRight, bRight) - std::max(aLeft, bLeft);
            const float overlapY = std::min(aBottom, bBottom) - std::max(aTop, bTop);

            if (overlapX < overlapY) {
                const float sign = ((aLeft + aRight) <= (bLeft + bRight)) ? -1.0f : 1.0f;
                const float push = overlapX * 0.5f + kSeparationEpsilon;
                const float aNewX = a->X() + sign * push;
                const float bNewX = b->X() - sign * push;
                if (!IsPositionBlockedByMap(*a, aNewX, a->Y())) {
                    a->SetPosition(aNewX, a->Y());
                }
                if (!IsPositionBlockedByMap(*b, bNewX, b->Y())) {
                    b->SetPosition(bNewX, b->Y());
                }
            } else {
                const float sign = ((aTop + aBottom) <= (bTop + bBottom)) ? -1.0f : 1.0f;
                const float push = overlapY * 0.5f + kSeparationEpsilon;
                const float aNewY = a->Y() + sign * push;
                const float bNewY = b->Y() - sign * push;
                if (!IsPositionBlockedByMap(*a, a->X(), aNewY)) {
                    a->SetPosition(a->X(), aNewY);
                }
                if (!IsPositionBlockedByMap(*b, b->X(), bNewY)) {
                    b->SetPosition(b->X(), bNewY);
                }
            }
        }
    }
}

void Game::DestroyTank(Tank& tank) {
    // Reusa la animacion de 5 frames de Documentaciones/Exploción.png que ya
    // carga el disparo especial, a escala nativa (mas chica).
    specialExplosions_.Spawn(tank.X() + 0.5f, tank.Y() + 0.5f, /*nativeScale=*/true);
    tank.ResetWeaponLevel();
    PlaySound(tankDeathSound_);
}

bool Game::ApplyExplosionSelfDamage(Tank& tank, int ownerId) {
    if (tank.IsEliminated()) {
        return false; // fuera de la partida: no le pasa nada mas
    }
    bool destroyed = false;
    for (const SpecialExplosionEvent& event : specialExplosionEvents_) {
        const bool directHit = (event.directHitOwnerId == ownerId);

        if (!directHit) {
            const float tankCenterX = tank.X() + 0.5f;
            const float tankCenterY = tank.Y() + 0.5f;
            const float dx = tankCenterX - event.x;
            const float dy = tankCenterY - event.y;
            const float hitRadius = event.radius + 0.5f; // + medio tanque de margen
            if (dx * dx + dy * dy > hitRadius * hitRadius) {
                continue;
            }
        }
        // directHit == true ya esta garantizado dentro del radio: es el
        // punto exacto donde el especial choco contra este tanque.

        // Mismo sistema de potencia/salud que el resto (ver ResolveTankHit),
        // con la potencia de explosion del especial (kSpecialExplosionPower):
        // el escudo la absorbe entera si le alcanza la salud; si no hay
        // escudo, se le resta directo al nivel actual. No se llama a
        // ResolveTankHit/DestroyTank aca para no duplicar la animacion de
        // explosion (ya esta la grande del especial en este mismo punto).
        if (tank.IsShielded()) {
            tank.AbsorbShieldHit(kSpecialExplosionPower);
        } else {
            const int currentHealth = tank.WeaponLevel();
            const int dealt = std::min(kSpecialExplosionPower, currentHealth);
            if (dealt >= currentHealth) {
                tank.ResetWeaponLevel();
                destroyed = true;
                PlaySound(tankDeathSound_);
            } else {
                tank.ApplyWeaponLevelPenalty(dealt);
                PlaySound(hitNonLethalSound_);
            }
        }
        tank.Freeze(kFriendlyFireParalyzeDuration);
    }
    return destroyed;
}

// Un enemigo Basico/Rapido/Blindado sin escudo muere directo si la bala lo
// toca (ver SpecialDirectKillEvent mas abajo, en Update): la bala sigue de
// largo, no explota. Esta funcion es para cuando SI explota en otro lado (un
// bloque, una pared, el Power, o un tanque/enemigo con escudo, ver la
// construccion de "tanks" en Update): esos mismos 3 tipos tambien mueren si
// quedan dentro del radio de esa explosion. Power resiste un impacto
// directo (weaponLevel=4 en la lista de "tanks" lo hace explotar siempre),
// pero tiene HP real (ver Enemy::hp): la explosion no lo mata de un saque
// salvo que ya estuviera en kSpecialExplosionPowerWeakenHp.
void Game::ApplySpecialExplosionsToEnemies() {
    for (const SpecialExplosionEvent& event : specialExplosionEvents_) {
        auto withinBlast = [&](const Tank& tank, int ownerId) {
            if (event.directHitOwnerId == ownerId) {
                return true; // el punto exacto donde exploto: ya esta garantizado adentro
            }
            const float dx = (tank.X() + 0.5f) - event.x;
            const float dy = (tank.Y() + 0.5f) - event.y;
            const float hitRadius = event.radius + 0.5f; // + medio tanque de margen, mismo criterio que ApplyExplosionSelfDamage
            return dx * dx + dy * dy <= hitRadius * hitRadius;
        };

        for (Enemy& enemy : enemies_.Enemies()) {
            if (enemy.alive && withinBlast(enemy.tank, enemy.ownerId)) {
                specialExplosions_.Spawn(enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, /*nativeScale=*/true);
                enemy.alive = false;
                PlaySound(tankDeathSound_);
                ++enemiesKilledThisLevel_;
                AwardScoreAt(event.ownerId, enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, kScoreBasicKill);
            }
        }
        for (Enemy& enemy : fastEnemies_.Enemies()) {
            if (enemy.alive && withinBlast(enemy.tank, enemy.ownerId)) {
                specialExplosions_.Spawn(enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, /*nativeScale=*/true);
                enemy.alive = false;
                PlaySound(tankDeathSound_);
                ++enemiesKilledThisLevel_;
                AwardScoreAt(event.ownerId, enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, kScoreFastKill);
            }
        }
        for (Enemy& enemy : armorEnemies_.Enemies()) {
            if (enemy.alive && withinBlast(enemy.tank, enemy.ownerId)) {
                specialExplosions_.Spawn(enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, /*nativeScale=*/true);
                enemy.alive = false;
                PlaySound(tankDeathSound_);
                ++enemiesKilledThisLevel_;
                AwardScoreAt(event.ownerId, enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, kScoreArmorKill);
            }
        }
        for (Enemy& enemy : powerEnemies_.Enemies()) {
            if (!enemy.alive || !withinBlast(enemy.tank, enemy.ownerId)) {
                continue;
            }
            if (enemy.hp <= kSpecialExplosionPowerWeakenHp) {
                specialExplosions_.Spawn(enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, /*nativeScale=*/true);
                enemy.alive = false;
                PlaySound(tankDeathSound_);
                ++enemiesKilledThisLevel_;
                AwardScoreAt(event.ownerId, enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, kScorePowerKill);
            } else {
                enemy.hp = kSpecialExplosionPowerWeakenHp;
                PlaySound(hitNonLethalSound_);
            }
        }
    }
}

bool Game::ApplyFriendlyFire(Tank& tank, int ownerId) {
    if (tank.IsEliminated()) {
        return false; // fuera de la partida: no le pasa nada mas
    }
    if (friendlyFireMode_ == FriendlyFireMode::Off) {
        return false; // las balas del otro jugador atraviesan sin chequeo alguno
    }

    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    tank.GetBounds(left, right, top, bottom);

    if (friendlyFireMode_ == FriendlyFireMode::Paralyze) {
        // No es un modo de dano de verdad (no hay potencia/salud de por
        // medio): cualquier bala que toque se destruye entera ahi mismo
        // (nunca atraviesa) y, si no hay escudo, paraliza; el escudo bloquea
        // el golpe por completo, no se pierde.
        auto resolve = [&](int /*weaponLevel*/, bool /*isSpecial*/) {
            if (!tank.IsShielded()) {
                tank.Freeze(kFriendlyFireParalyzeDuration);
            }
            return 999; // mas que la salud maxima posible de cualquier bala: siempre la destruye entera, nunca atraviesa
        };
        bullets_.KillBulletsHittingBox(ownerId, left, right, top, bottom, bulletImpacts_, resolve);
        return false;
    }

    // Damage: cada bala que efectivamente toque se resuelve contra el
    // tanque de verdad (ver ResolveTankHit); si alguna lo destruye, no
    // hace falta seguir (aunque en la practica solo puede tocar una vez
    // por cuadro salvo que se solapen 2 balas exactamente).
    bool destroyed = false;
    auto resolve = [&](int weaponLevel, bool isSpecial) {
        bool diedNow = false;
        const int power = isSpecial ? kSpecialBulletPower : BulletPowerForLevel(weaponLevel);
        const int dealt = ResolveTankHit(tank, power, diedNow);
        if (diedNow) {
            destroyed = true;
        }
        return dealt;
    };
    bullets_.KillBulletsHittingBox(ownerId, left, right, top, bottom, bulletImpacts_, resolve);
    return destroyed;
}

int Game::ResolveTankHit(Tank& tank, int power, bool& outDestroyed) {
    outDestroyed = false;
    if (tank.IsShielded()) {
        return tank.AbsorbShieldHit(power);
    }
    const int currentHealth = tank.WeaponLevel();
    const int dealt = std::min(power, currentHealth);
    if (dealt >= currentHealth) {
        DestroyTank(tank);
        outDestroyed = true;
    } else {
        tank.ApplyWeaponLevelPenalty(dealt);
        PlaySound(hitNonLethalSound_);
    }
    return dealt;
}

void Game::Update(double fixedDt) {
    if (appState_ != AppState::Playing) {
        return; // en el menu no hay simulacion a paso fijo, solo se dibuja (ver RenderMenu)
    }

    // Menu de pausa (ver paused_): todo el juego queda congelado del todo
    // mientras este abierto, ni siquiera se tickean animaciones/timers (a
    // diferencia de la transicion de nivel o la pantalla de puntuacion, que
    // tienen su propio temporizador corriendo).
    if (paused_) {
        return;
    }

    // Transicion de nivel ("NIVEL N" + telon, ver BeginLevelTransition):
    // mientras dure, el resto del juego queda congelado (nada se mueve, no
    // hay IA ni spawns), solo avanza este temporizador. Al terminar el
    // telon recien ahi arranca el enjambre inicial del nivel.
    if (levelTransitionPhase_ != LevelTransitionPhase::None) {
        levelTransitionTimer_ -= fixedDt;
        if (levelTransitionTimer_ <= 0.0) {
            if (levelTransitionPhase_ == LevelTransitionPhase::ShowingText) {
                levelTransitionPhase_ = LevelTransitionPhase::Curtain;
                levelTransitionTimer_ = kLevelTransitionCurtainDuration;
            } else {
                levelTransitionPhase_ = LevelTransitionPhase::None;
                SpawnInitialWaveForLevel();
            }
        }
        return;
    }

    // Pantalla de puntuacion de fin de nivel (ver ScoreTallyPhase): mientras
    // dure, el juego queda congelado igual que la transicion de arriba, solo
    // avanza su propia animacion (ver TickScoreTally). Al terminar (Holding
    // agotado) llama sola a AdvanceToNextLevel(), que arranca la transicion
    // "NIVEL N" del nivel siguiente.
    if (scoreTallyPhase_ != ScoreTallyPhase::None) {
        TickScoreTally(fixedDt);
        return;
    }

    // Secuencia de fin de partida (ver GameOverPhase/TriggerGameOver): a
    // diferencia de la transicion/pantalla de puntuacion de arriba, esto NO
    // congela el juego entero — solo avanza su propia animacion. El freeze
    // real es mas abajo (gameOver_ salta las 4 UpdatePlayer y todo lo que
    // pueda matarlas/respawnearlas), asi enemigos/balas/spawns siguen
    // andando detras del "GAME OVER" (pedido explicitamente). Una vez en
    // NameEntry ya no hay "espectaculo" que mostrar: recien ahi se congela
    // todo de verdad.
    if (gameOverPhase_ != GameOverPhase::None) {
        TickGameOverSequence(fixedDt);
        if (gameOverPhase_ == GameOverPhase::NameEntry) {
            return;
        }
    }

    if (screenShakeTimer_ > 0.0) {
        screenShakeTimer_ = std::max(0.0, screenShakeTimer_ - fixedDt);
    }
    TickBaseFortification(fixedDt);

    // Jugadores congelados durante la secuencia de fin de partida (ver
    // gameOver_ arriba): no se mueven, no disparan, no agarran power-ups.
    if (!gameOver_) {
        if (player1Active_) UpdatePlayer(player1_, input1_, player1Spawn_, kPlayer1Id, ActiveOthers(kPlayer1Id), fixedDt);
        if (player2Active_) UpdatePlayer(player2_, input2_, player2Spawn_, kPlayer2Id, ActiveOthers(kPlayer2Id), fixedDt);
        if (player3Active_) UpdatePlayer(player3_, input3_, player3Spawn_, kPlayer3Id, ActiveOthers(kPlayer3Id), fixedDt);
        if (player4Active_) UpdatePlayer(player4_, input4_, player4Spawn_, kPlayer4Id, ActiveOthers(kPlayer4Id), fixedDt);
    }

    TickEnemySpawning(fixedDt);
    const int basicAliveBeforeCombat = CountAliveInList(enemies_.Enemies());
    const int fastAliveBeforeCombat = CountAliveInList(fastEnemies_.Enemies());
    const int armorAliveBeforeCombat = CountAliveInList(armorEnemies_.Enemies());
    const int powerAliveBeforeCombat = CountAliveInList(powerEnemies_.Enemies());

    // Cada sistema de enemigos recibe a los otros 2 tipos como obstaculo de
    // colision (nunca como blanco de combate: eso solo aplica a jugadores
    // reales).
    std::vector<Tank*> basicEnemyTanks;
    basicEnemyTanks.reserve(enemies_.Enemies().size());
    for (Enemy& enemy : enemies_.Enemies()) {
        if (enemy.alive) {
            basicEnemyTanks.push_back(&enemy.tank);
        }
    }
    std::vector<Tank*> fastEnemyTanks;
    fastEnemyTanks.reserve(fastEnemies_.Enemies().size());
    for (Enemy& enemy : fastEnemies_.Enemies()) {
        if (enemy.alive) {
            fastEnemyTanks.push_back(&enemy.tank);
        }
    }
    std::vector<Tank*> armorEnemyTanks;
    armorEnemyTanks.reserve(armorEnemies_.Enemies().size());
    for (Enemy& enemy : armorEnemies_.Enemies()) {
        if (enemy.alive) {
            armorEnemyTanks.push_back(&enemy.tank);
        }
    }
    std::vector<Tank*> powerEnemyTanks;
    powerEnemyTanks.reserve(powerEnemies_.Enemies().size());
    for (Enemy& enemy : powerEnemies_.Enemies()) {
        if (enemy.alive) {
            powerEnemyTanks.push_back(&enemy.tank);
        }
    }

    std::vector<Tank*> otherThanBasic = fastEnemyTanks;
    otherThanBasic.insert(otherThanBasic.end(), armorEnemyTanks.begin(), armorEnemyTanks.end());
    otherThanBasic.insert(otherThanBasic.end(), powerEnemyTanks.begin(), powerEnemyTanks.end());
    std::vector<Tank*> otherThanFast = basicEnemyTanks;
    otherThanFast.insert(otherThanFast.end(), armorEnemyTanks.begin(), armorEnemyTanks.end());
    otherThanFast.insert(otherThanFast.end(), powerEnemyTanks.begin(), powerEnemyTanks.end());
    std::vector<Tank*> otherThanArmor = basicEnemyTanks;
    otherThanArmor.insert(otherThanArmor.end(), fastEnemyTanks.begin(), fastEnemyTanks.end());
    otherThanArmor.insert(otherThanArmor.end(), powerEnemyTanks.begin(), powerEnemyTanks.end());
    std::vector<Tank*> otherThanPower = basicEnemyTanks;
    otherThanPower.insert(otherThanPower.end(), fastEnemyTanks.begin(), fastEnemyTanks.end());
    otherThanPower.insert(otherThanPower.end(), armorEnemyTanks.begin(), armorEnemyTanks.end());

    std::vector<ScoreEvent> basicScoreEvents;
    std::vector<ScoreEvent> fastScoreEvents;
    std::vector<ScoreEvent> armorScoreEvents;
    std::vector<ScoreEvent> powerScoreEvents;
    enemies_.Update(fixedDt, map_, bullets_, bulletImpacts_, specialExplosions_, ActivePlayerTanks(), otherThanBasic, static_cast<float>(basePositionX_), static_cast<float>(basePositionY_), basicScoreEvents, tankDeathSound_);
    fastEnemies_.Update(fixedDt, map_, bullets_, bulletImpacts_, specialExplosions_, ActivePlayerTanks(), otherThanFast, static_cast<float>(basePositionX_), static_cast<float>(basePositionY_), fastScoreEvents, tankDeathSound_);
    armorEnemies_.Update(fixedDt, map_, bullets_, bulletImpacts_, specialExplosions_, ActivePlayerTanks(), otherThanArmor, static_cast<float>(basePositionX_), static_cast<float>(basePositionY_), armorScoreEvents, tankDeathSound_);
    powerEnemies_.Update(fixedDt, map_, bullets_, bulletImpacts_, specialExplosions_, ActivePlayerTanks(), otherThanPower, static_cast<float>(basePositionX_), static_cast<float>(basePositionY_), powerScoreEvents, tankDeathSound_, hitNonLethalSound_);

    // Balas normales matando enemigos (ver los 4 Update() de arriba): la
    // diferencia de vivos antes/despues de la combinacion la cuenta como
    // eliminados. ApplyEnemyPowerUpPickups() (justo abajo) tambien puede
    // bajar el conteo de vivos al convertir un enemigo en otro tipo, pero
    // eso NO es una eliminacion, por eso el conteo se toma ANTES de
    // llamarla. El puntaje (a quien disparo, ver ScoreEvent) ya viene
    // resuelto en los 4 vectores de arriba, uno por eliminacion real.
    const int basicKilled = std::max(0, basicAliveBeforeCombat - CountAliveInList(enemies_.Enemies()));
    const int fastKilled = std::max(0, fastAliveBeforeCombat - CountAliveInList(fastEnemies_.Enemies()));
    const int armorKilled = std::max(0, armorAliveBeforeCombat - CountAliveInList(armorEnemies_.Enemies()));
    const int powerKilled = std::max(0, powerAliveBeforeCombat - CountAliveInList(powerEnemies_.Enemies()));
    enemiesKilledThisLevel_ += basicKilled + fastKilled + armorKilled + powerKilled;
    for (const std::vector<ScoreEvent>* events : {&basicScoreEvents, &fastScoreEvents, &armorScoreEvents, &powerScoreEvents}) {
        for (const ScoreEvent& event : *events) {
            AwardScoreAt(event.ownerId, event.x, event.y, event.points);
        }
    }

    ApplyEnemyPowerUpPickups();

    // Estado actual de los tanques presentes para que BulletSystem resuelva
    // un choque directo del disparo especial (ver TankCombatState).
    std::vector<TankCombatState> tanks;
    tanks.reserve(4);
    struct ActivePlayerRef { int ownerId; Tank* tank; bool active; };
    const ActivePlayerRef activePlayers[4] = {
        {kPlayer1Id, &player1_, player1Active_},
        {kPlayer2Id, &player2_, player2Active_},
        {kPlayer3Id, &player3_, player3Active_},
        {kPlayer4Id, &player4_, player4Active_},
    };
    for (const ActivePlayerRef& ref : activePlayers) {
        if (!ref.active || ref.tank->IsEliminated()) {
            continue; // ausente o fuera de la partida: no es un blanco valido
        }
        TankCombatState state;
        state.ownerId = ref.ownerId;
        ref.tank->GetBounds(state.left, state.right, state.top, state.bottom);
        state.shielded = ref.tank->IsShielded();
        state.weaponLevel = ref.tank->WeaponLevel();
        tanks.push_back(state);
    }
    // El "Power" (enemigo 4) siempre entra a esta lista: weaponLevel=4
    // fuerza la rama de "explota" en vez de "atraviesa" (esa rama solo
    // pasa de largo si weaponLevel != 4), asi el especial revienta contra
    // el en vez de seguir de largo como con los demas.
    for (Enemy& enemy : powerEnemies_.Enemies()) {
        if (!enemy.alive) {
            continue;
        }
        TankCombatState state;
        state.ownerId = enemy.ownerId;
        enemy.tank.GetBounds(state.left, state.right, state.top, state.bottom);
        state.shielded = false;
        state.weaponLevel = 4;
        tanks.push_back(state);
    }

    // Basico/Rapido/Blindado (enemigo 1/2/3), todos los vivos: sin escudo,
    // el impacto directo del especial los destruye y la bala sigue de largo
    // sin explotar (mismo criterio que un jugador nivel 1-3 sin escudo, ver
    // SpecialDirectKillEvent/BulletSystem::Update); con escudo, en cambio,
    // frenan/explotan la bala como cualquier otro blanco solido. weaponLevel
    // se deja en 1 (nunca 4) para que la rama de choque directo pueda
    // aplicar cuando corresponda; el escudo real decide cual de las 2 ramas
    // toma (ver BulletSystem::Update).
    for (Enemy& enemy : enemies_.Enemies()) {
        if (enemy.alive) {
            TankCombatState state;
            state.ownerId = enemy.ownerId;
            enemy.tank.GetBounds(state.left, state.right, state.top, state.bottom);
            state.shielded = enemy.tank.IsShielded();
            state.weaponLevel = 1;
            tanks.push_back(state);
        }
    }
    for (Enemy& enemy : fastEnemies_.Enemies()) {
        if (enemy.alive) {
            TankCombatState state;
            state.ownerId = enemy.ownerId;
            enemy.tank.GetBounds(state.left, state.right, state.top, state.bottom);
            state.shielded = enemy.tank.IsShielded();
            state.weaponLevel = 1;
            tanks.push_back(state);
        }
    }
    for (Enemy& enemy : armorEnemies_.Enemies()) {
        if (enemy.alive) {
            TankCombatState state;
            state.ownerId = enemy.ownerId;
            enemy.tank.GetBounds(state.left, state.right, state.top, state.bottom);
            state.shielded = enemy.tank.IsShielded();
            state.weaponLevel = 1;
            tanks.push_back(state);
        }
    }

    bullets_.Update(fixedDt, map_, bulletImpacts_, specialExplosions_, specialExplosionEvents_, tanks, specialDirectKillEvents_, brickBreakSound_, blockResistSound_, bigExplosionSound_);

    // Choque directo del especial contra un enemigo Basico/Rapido/Blindado
    // sin escudo (pedido explicitamente): se destruye ahi mismo y la bala
    // seguia de largo (ver SpecialDirectKillEvent/BulletSystem::Update), asi
    // que no paso por ninguna explosion. No depende de gameOver_: los
    // enemigos siguen reaccionando normalmente aunque los jugadores esten
    // congelados (ver el freeze de arriba).
    for (const SpecialDirectKillEvent& kill : specialDirectKillEvents_) {
        auto killEnemyIfMatch = [&](Enemy& enemy, int scorePoints) {
            if (enemy.alive && enemy.ownerId == kill.targetOwnerId) {
                specialExplosions_.Spawn(enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, /*nativeScale=*/true);
                enemy.alive = false;
                PlaySound(tankDeathSound_);
                ++enemiesKilledThisLevel_;
                AwardScoreAt(kill.shooterOwnerId, enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, scorePoints);
                return true;
            }
            return false;
        };
        for (Enemy& enemy : enemies_.Enemies()) {
            if (killEnemyIfMatch(enemy, kScoreBasicKill)) break;
        }
        for (Enemy& enemy : fastEnemies_.Enemies()) {
            if (killEnemyIfMatch(enemy, kScoreFastKill)) break;
        }
        for (Enemy& enemy : armorEnemies_.Enemies()) {
            if (killEnemyIfMatch(enemy, kScoreArmorKill)) break;
        }
    }

    // Efecto de la explosion del disparo especial sobre los 4 tipos de
    // enemigo (ver ApplySpecialExplosionsToEnemies): Basico/Rapido/Blindado
    // mueren de un saque si la alcanza, Power resiste (lo deja a un
    // impacto de morir, o lo remata si ya estaba asi).
    ApplySpecialExplosionsToEnemies();

    // El aguila (TileType::Base) se destruye de un impacto (ver BulletSystem
    // y TriggerSpecialExplosion): si la celda dejo de ser Base, se acabo la
    // partida (ver TriggerGameOver/GameOverPhase).
    if (!gameOver_ && basePositionX_ >= 0 && map_.InBounds(basePositionX_, basePositionY_) &&
        map_.At(basePositionX_, basePositionY_).type != TileType::Base) {
        TriggerGameOver();
    }

    bulletImpacts_.Update(fixedDt);
    scorePopups_.Update(fixedDt);
    specialExplosions_.Update(fixedDt);
    if (powerUps_.Update(fixedDt, map_, ActiveTankBounds())) {
        PlaySound(itemAppearSound_);
    }

    // Todo lo que sigue puede matar/respawnear a un jugador: se salta
    // entero durante la secuencia de fin de partida (ver gameOver_), para
    // que los jugadores queden realmente congelados (pedido explicitamente)
    // mientras enemigos/balas/power-ups de arriba y abajo siguen andando.
    if (!gameOver_) {
        for (const ActivePlayerRef& ref : activePlayers) {
            if (!ref.active) {
                continue;
            }
            if (ApplyFriendlyFire(*ref.tank, ref.ownerId)) {
                HandlePlayerDeath(*ref.tank, ref.ownerId);
            }
        }

        // Balas enemigas contra jugadores: siempre hacen dano (no depende
        // del modo de fuego amigo, que es solo entre jugadores), con la
        // potencia real del tipo de enemigo que disparo (ver ResolveTankHit
        // y kBulletPowerByLevel — antes se procesaba siempre como si fuera
        // potencia 1 sin importar el tipo, ya corregido).
        for (const ActivePlayerRef& ref : activePlayers) {
            if (!ref.active || ref.tank->IsEliminated()) {
                continue;
            }
            float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
            ref.tank->GetBounds(left, right, top, bottom);
            bool destroyed = false;
            auto resolve = [&](int weaponLevel, bool isSpecial) {
                bool diedNow = false;
                const int power = isSpecial ? kSpecialBulletPower : BulletPowerForLevel(weaponLevel);
                const int dealt = ResolveTankHit(*ref.tank, power, diedNow);
                if (diedNow) {
                    destroyed = true;
                }
                return dealt;
            };
            bullets_.KillEnemyBulletsHittingBox(left, right, top, bottom, bulletImpacts_, resolve);
            if (destroyed) {
                HandlePlayerDeath(*ref.tank, ref.ownerId);
            }
        }

        // Choque directo del especial contra un tanque nivel 1-3 sin
        // escudo: se destruye y la bala seguia de largo (ver
        // BulletSystem::Update), asi que no paso por ninguna explosion.
        for (const SpecialDirectKillEvent& kill : specialDirectKillEvents_) {
            for (const ActivePlayerRef& ref : activePlayers) {
                if (ref.active && kill.targetOwnerId == ref.ownerId) {
                    DestroyTank(*ref.tank);
                    HandlePlayerDeath(*ref.tank, ref.ownerId);
                    break;
                }
            }
        }
    } // !gameOver_

    if (!specialExplosionEvents_.empty()) {
        // Efecto global, no depende de la distancia (a diferencia del dano):
        // toda explosion especial sacude la pantalla y paraliza a TODOS los
        // tanques presentes, jugadores y enemigos por igual.
        screenShakeTimer_ = kScreenShakeDuration;
        for (const ActivePlayerRef& ref : activePlayers) {
            if (ref.active) {
                ref.tank->Freeze(kSpecialImpactFreezeDuration);
            }
        }
        for (Enemy& enemy : enemies_.Enemies()) {
            if (enemy.alive) {
                enemy.tank.Freeze(kSpecialImpactFreezeDuration);
            }
        }
    }

    // La explosion especial daña a quien alcance, incluido el propio tanque
    // que la disparo (seccion pedida explicitamente: "amigo-enemigo" a
    // proposito, es el riesgo de usar el disparo especial).
    if (!gameOver_) {
        for (const ActivePlayerRef& ref : activePlayers) {
            if (!ref.active) {
                continue;
            }
            if (ApplyExplosionSelfDamage(*ref.tank, ref.ownerId)) {
                HandlePlayerDeath(*ref.tank, ref.ownerId);
            }
        }

        // "Todos los jugadores sin vidas" (pedido explicitamente): si CADA
        // jugador activo quedo eliminado (sin vidas) tras lo de arriba, se
        // acabo la partida igual que con el aguila destruida.
        const bool anyActive = player1Active_ || player2Active_ || player3Active_ || player4Active_;
        const bool allEliminated =
            (!player1Active_ || player1_.IsEliminated()) &&
            (!player2Active_ || player2_.IsEliminated()) &&
            (!player3Active_ || player3_.IsEliminated()) &&
            (!player4Active_ || player4_.IsEliminated());
        if (anyActive && allEliminated) {
            TriggerGameOver();
        }
    }

    // Ultimo paso del frame, con todas las posiciones ya finales: corrige
    // cualquier solape de hitboxes que haya quedado (ver ResolveTankOverlaps).
    ResolveTankOverlaps();

    if (!gameOver_) {
        CheckLevelCompletion(fixedDt);
    }
}

void Game::RenderTank(const Tank& tank, const SpawnFlash& spawn, const TankSpriteSet& sprites, const MapViewport& viewport) {
    if (tank.IsEliminated()) {
        return; // sin vidas: no se dibuja mas
    }

    if (spawn.IsActive()) {
        const Texture2D flashTex = spawnFlashSprites_.Get(spawn.FrameIndex());
        const Rectangle flashSrc{0.0f, 0.0f, static_cast<float>(flashTex.width), static_cast<float>(flashTex.height)};
        const Rectangle flashDst{viewport.TileToScreenX(spawn.X()), viewport.TileToScreenY(spawn.Y()), viewport.tileScreenSize, viewport.tileScreenSize};
        DrawTexturePro(flashTex, flashSrc, flashDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        return;
    }

    // Paralizado (fuego amigo nivel 1, o la onda del especial): parpadea en
    // vez de dibujarse fijo, para que se note claramente que no responde.
    const bool blinkedOut = tank.IsFrozen() && (static_cast<int>(GetTime() / kFrozenBlinkInterval) % 2 == 0);

    // Calor al 100% (bloqueado por sobrecalentamiento): igual que el brillo
    // del disparo especial de abajo, progresivo en 3 pasos (normal -> rojo
    // suave -> rojo pleno -> rojo suave -> normal) en vez de un parpadeo
    // duro de 2 estados, tiñendo el sprite en vez de necesitar otro set.
    Color tint = WHITE;
    if (tank.HeatPercent() >= 100.0f) {
        constexpr double kOverheatStepDuration = 0.12;
        constexpr int kOverheatSequence[4] = {0, 1, 2, 1};
        const int step = static_cast<int>(GetTime() / kOverheatStepDuration) % 4;
        const int level = kOverheatSequence[step];
        if (level == 1) {
            tint = Color{255, 150, 150, 255};
        } else if (level == 2) {
            tint = RED;
        }
    }

    const Texture2D tankTex = sprites.Get(tank.WeaponLevel(), tank.Facing(), tank.AnimFrame());
    const Rectangle src{0.0f, 0.0f, static_cast<float>(tankTex.width), static_cast<float>(tankTex.height)};
    const Rectangle dst{viewport.TileToScreenX(tank.X()), viewport.TileToScreenY(tank.Y()), viewport.tileScreenSize, viewport.tileScreenSize};
    if (!blinkedOut) {
        DrawTexturePro(tankTex, src, dst, Vector2{0.0f, 0.0f}, 0.0f, tint);
    }

    // Disparo especial disponible: brillo progresivo (normal -> brillo1 ->
    // brillo2 -> brillo1 -> normal, ida y vuelta) en vez de un parpadeo duro
    // de 2 estados, para que el cambio se sienta suave. Un tint comun no
    // puede aclarar mas alla del blanco, asi que se redibuja el mismo
    // sprite con mezcla aditiva y alpha creciente encima.
    if (tank.HasSpecialShotReady() && !blinkedOut) {
        constexpr double kPulseStepDuration = 0.12;
        constexpr int kPulseSequence[4] = {0, 1, 2, 1};
        const int step = static_cast<int>(GetTime() / kPulseStepDuration) % 4;
        const int level = kPulseSequence[step];
        if (level > 0) {
            const unsigned char alpha = (level == 1) ? 70 : 150;
            BeginBlendMode(BLEND_ADDITIVE);
            DrawTexturePro(tankTex, src, dst, Vector2{0.0f, 0.0f}, 0.0f, Color{255, 255, 255, alpha});
            EndBlendMode();
        }
    }

    if (tank.IsShielded() && !blinkedOut) {
        const int shieldFrame = static_cast<int>(GetTime() / kShieldBlinkInterval) % 2;
        const Texture2D shieldTex = shieldSprites_.Get(shieldFrame);
        const Rectangle shieldSrc{0.0f, 0.0f, static_cast<float>(shieldTex.width), static_cast<float>(shieldTex.height)};
        DrawTexturePro(shieldTex, shieldSrc, dst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }
}

void Game::RenderEnemy(const Enemy& enemy, const EnemySprites& sprites, const MapViewport& viewport) {
    if (!enemy.alive) {
        return;
    }

    if (enemy.spawn.IsActive()) {
        // Mismo destello de aparicion que los jugadores (ver RenderTank).
        const Texture2D flashTex = spawnFlashSprites_.Get(enemy.spawn.FrameIndex());
        const Rectangle flashSrc{0.0f, 0.0f, static_cast<float>(flashTex.width), static_cast<float>(flashTex.height)};
        const Rectangle flashDst{viewport.TileToScreenX(enemy.spawn.X()), viewport.TileToScreenY(enemy.spawn.Y()), viewport.tileScreenSize, viewport.tileScreenSize};
        DrawTexturePro(flashTex, flashSrc, flashDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        return;
    }

    // Paralizado (onda del disparo especial, igual que los jugadores):
    // parpadea en vez de dibujarse fijo (ver kFrozenBlinkInterval).
    const bool blinkedOut = enemy.tank.IsFrozen() && (static_cast<int>(GetTime() / kFrozenBlinkInterval) % 2 == 0);
    if (blinkedOut) {
        return;
    }

    // Sprite propio del tipo de enemigo (fila 5 = Basico, fila 6 = Rapido de
    // Tanques.png, paleta gris/teal de P3) — no el del Jugador 3.
    const Texture2D enemyTex = sprites.Get(enemy.tank.Facing(), enemy.tank.AnimFrame());
    const Rectangle src{0.0f, 0.0f, static_cast<float>(enemyTex.width), static_cast<float>(enemyTex.height)};
    const Rectangle dst{viewport.TileToScreenX(enemy.tank.X()), viewport.TileToScreenY(enemy.tank.Y()), viewport.tileScreenSize, viewport.tileScreenSize};
    DrawTexturePro(enemyTex, src, dst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);

    // Disparo especial armado (solo "Power", ver Enemy::specialShotFuseTimer):
    // mismo brillo pulsante que un jugador con HasSpecialShotReady() (ver
    // RenderTank), redibujando el mismo sprite con mezcla aditiva.
    if (enemy.specialShotFuseTimer >= 0.0) {
        constexpr double kPulseStepDuration = 0.12;
        constexpr int kPulseSequence[4] = {0, 1, 2, 1};
        const int step = static_cast<int>(GetTime() / kPulseStepDuration) % 4;
        const int level = kPulseSequence[step];
        if (level > 0) {
            const unsigned char alpha = (level == 1) ? 70 : 150;
            BeginBlendMode(BLEND_ADDITIVE);
            DrawTexturePro(enemyTex, src, dst, Vector2{0.0f, 0.0f}, 0.0f, Color{255, 255, 255, alpha});
            EndBlendMode();
        }
    }

    // Escudo (item Casco, ver Game::ApplyEnemyPowerUpPickups): mismo overlay
    // parpadeante que un jugador con IsShielded() (ver RenderTank).
    if (enemy.tank.IsShielded()) {
        const int shieldFrame = static_cast<int>(GetTime() / kShieldBlinkInterval) % 2;
        const Texture2D shieldTex = shieldSprites_.Get(shieldFrame);
        const Rectangle shieldSrc{0.0f, 0.0f, static_cast<float>(shieldTex.width), static_cast<float>(shieldTex.height)};
        DrawTexturePro(shieldTex, shieldSrc, dst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    // Debug (para diagnosticar si se traba): modo de movimiento actual (ver
    // Enemy::debugMode) + cuantos frames seguidos lleva sin poder moverse,
    // en cuanto pasa medio segundo.
    if (kDebugFeaturesEnabled && enemy.stuckFrames > 30) {
        const char* label = TextFormat("%c %.1fs", enemy.debugMode, enemy.stuckFrames / 60.0f);
        DrawText(label, static_cast<int>(dst.x), static_cast<int>(dst.y) - 12, 10, RED);
    }
}

void Game::RenderPlayerHud(const Tank& tank, const char* label, int centerX, int y, int score, Color playerColor) {
    // Parpadeo de texto (calor al 100% / municion especial lista): mismo
    // criterio de "visible la mitad del tiempo" que ya se usa para el tanque
    // paralizado (ver kFrozenBlinkInterval), pero un poco mas lento porque es
    // texto de HUD, no el sprite del tanque.
    const bool textBlinkOn = static_cast<int>(GetTime() / kHudTextBlinkInterval) % 2 == 0;

    // Todo el bloque se alinea al centro: cada linea (y la barra de
    // temperatura junto con su texto) se centra en centerX, en vez de
    // arrancar de un borde fijo.
    if (tank.IsEliminated()) {
        const char* text = TextFormat("%s - SIN VIDAS  Puntaje: %d", label, score);
        DrawText(text, centerX - MeasureText(text, 20) / 2, y, 20, RED);
        return;
    }

    // "P1 [icono] 3 - Nivel N": el icono (Tanques.png, junto a "1P") va en
    // vez de una "X" entre la etiqueta y el numero de vidas.
    const char* labelText = TextFormat("%s ", label);
    const int labelWidth = MeasureText(labelText, 20);
    constexpr int kLifeIconDisplayHeight = 18;
    const int lifeIconDisplayWidth = (hudLifeIconTexture_.width * kLifeIconDisplayHeight) / hudLifeIconTexture_.height;
    const char* numberText = TextFormat("%d - Nivel %d", tank.Lives(), tank.WeaponLevel());
    const int numberWidth = MeasureText(numberText, 20);
    const int headerWidth = labelWidth + lifeIconDisplayWidth + numberWidth;
    const int headerX = centerX - headerWidth / 2;

    DrawText(labelText, headerX, y, 20, playerColor);
    const int lifeIconX = headerX + labelWidth;
    const int lifeIconY = y + (20 - kLifeIconDisplayHeight) / 2;
    const Rectangle lifeIconSrc{0.0f, 0.0f, static_cast<float>(hudLifeIconTexture_.width), static_cast<float>(hudLifeIconTexture_.height)};
    const Rectangle lifeIconDst{static_cast<float>(lifeIconX), static_cast<float>(lifeIconY), static_cast<float>(lifeIconDisplayWidth), static_cast<float>(kLifeIconDisplayHeight)};
    DrawTexturePro(hudLifeIconTexture_, lifeIconSrc, lifeIconDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    DrawText(numberText, lifeIconX + lifeIconDisplayWidth, y, 20, playerColor);

    constexpr int kHeatBarWidth = 160;
    constexpr int kHeatBarHeight = 14;
    constexpr int kBarTextGap = 6;
    constexpr int kLinePitch = 20; // alto de renglon + separacion minima entre lineas de texto
    const int heatBarY = y + kLinePitch;
    const float heatPercent = tank.HeatPercent();
    const bool overheated = heatPercent >= 100.0f;
    const char* percentText = TextFormat(":%.0f%%", heatPercent);
    const int percentWidth = MeasureText(percentText, 18);
    const int barX = centerX - (kHeatBarWidth + kBarTextGap + percentWidth) / 2;
    DrawRectangle(barX, heatBarY, kHeatBarWidth, kHeatBarHeight, DARKGRAY);
    const int filledWidth = static_cast<int>(kHeatBarWidth * (heatPercent / 100.0f));
    DrawRectangle(barX, heatBarY, filledWidth, kHeatBarHeight, overheated ? RED : ORANGE);
    DrawRectangleLines(barX, heatBarY, kHeatBarWidth, kHeatBarHeight, RAYWHITE);

    // Porcentaje a la derecha de la barra; al llegar a sobrecalentamiento
    // (100%) se pone en rojo.
    DrawText(percentText, barX + kHeatBarWidth + kBarTextGap, heatBarY - 2, 18, overheated ? RED : playerColor);

    int nextY = heatBarY + kHeatBarHeight + 4;

    // Puntaje propio de este jugador, justo debajo de la barra de calor
    // (pedido explicitamente), en letra chica para no competir con el resto.
    constexpr int kScoreFontSize = 14;
    const char* scoreText = TextFormat("Puntaje: %d", score);
    DrawText(scoreText, centerX - MeasureText(scoreText, kScoreFontSize) / 2, nextY, kScoreFontSize, playerColor);
    nextY += kLinePitch;

    if (overheated && textBlinkOn) {
        const char* warningText = "Sobrecalentamiento";
        DrawText(warningText, centerX - MeasureText(warningText, 18) / 2, nextY, 18, RED);
    }
    if (overheated) {
        nextY += kLinePitch;
    }

    if (tank.HasSpecialShotReady() && textBlinkOn) {
        const char* specialText = "Municion especial lista!";
        DrawText(specialText, centerX - MeasureText(specialText, 18) / 2, nextY, 18, RED);
    }
    if (tank.HasSpecialShotReady()) {
        nextY += kLinePitch;
    }

    if (tank.IsFrozen() && textBlinkOn) {
        const char* frozenText = "Paralizado";
        DrawText(frozenText, centerX - MeasureText(frozenText, 18) / 2, nextY, 18, YELLOW);
    }
}

void Game::Render(double /*interpolationAlpha*/) {
    windowWidth_ = GetScreenWidth();
    windowHeight_ = GetScreenHeight();

    if (appState_ == AppState::CharacterSelect) {
        RenderCharacterSelect();
        return;
    }
    if (appState_ != AppState::Playing) {
        RenderMenu();
        return;
    }

    // Pantalla de puntuacion de fin de nivel (ver ScoreTallyPhase): pantalla
    // negra propia, reemplaza a la escena de juego por completo (no un
    // overlay encima) mientras dure.
    if (scoreTallyPhase_ != ScoreTallyPhase::None) {
        RenderScoreTally();
        return;
    }

    // El campo de juego (rectangular, ver test_map.json) se encaja en el area
    // que queda entre la barra gris de la izquierda (solo de marco, sin
    // datos, del ancho exacto de una celda) y la barra gris de la derecha
    // (datos de jugadores), usando todo el alto de la ventana.
    const int panelW = static_cast<int>(kHudPanelWidth);
    const float playAreaHeight = static_cast<float>(windowHeight_);

    // Primer calculo (sin descontar la barra izquierda todavia) solo para
    // saber cuanto mide una celda en pantalla, y asi la barra izquierda
    // pueda tener exactamente ese ancho.
    const float prelimPlayAreaWidth = static_cast<float>(windowWidth_) - kHudPanelWidth;
    const MapViewport prelimViewport = MapViewport::Compute(static_cast<int>(prelimPlayAreaWidth), static_cast<int>(playAreaHeight), map_.Width(), map_.Height(), kTileSize);
    const int leftBarW = static_cast<int>(prelimViewport.tileScreenSize);

    const float playAreaWidth = prelimPlayAreaWidth - static_cast<float>(leftBarW);
    MapViewport viewport = MapViewport::Compute(static_cast<int>(playAreaWidth), static_cast<int>(playAreaHeight), map_.Width(), map_.Height(), kTileSize);
    viewport.offsetX += static_cast<float>(leftBarW);

    BeginDrawing();
    // Mismo color que las celdas vacias (ColorForTile de Empty), asi una
    // unidad de ladrillo destruida no deja un negro puro que desentona con
    // el resto del fondo.
    ClearBackground(ColorForTile(TileType::Empty));

    // El mapa (29x13) no siempre llena exacto el area a la izquierda de la
    // barra: al mantener celdas cuadradas (ver MapViewport::Compute), sobra
    // un margen angosto arriba/abajo segun la relacion de aspecto de la
    // ventana. Se pinta gris (en vez de dejarlo del mismo color que una
    // celda vacia) para que se note que ahi termina el campo de juego, en
    // vez de que el tanque se frene contra un borde invisible. Importante:
    // solo se pinta gris ESTE margen (y las barras de los costados), nunca
    // el area del mapa en si — si se pintara toda la ventana de gris antes
    // de dibujar el mapa, cualquier hueco de una unidad de ladrillo
    // destruida (que no dibuja nada ahi) dejaria ver ese gris de fondo en
    // vez del negro de "vacio".
    const float mapPixelHeight = viewport.tileScreenSize * static_cast<float>(map_.Height());
    DrawRectangle(0, 0, leftBarW, windowHeight_, kHudPanelColor);
    DrawRectangle(windowWidth_ - panelW, 0, panelW, windowHeight_, kHudPanelColor);
    DrawRectangle(leftBarW, 0, windowWidth_ - panelW - leftBarW, static_cast<int>(viewport.offsetY), kHudPanelColor);
    DrawRectangle(leftBarW, static_cast<int>(viewport.offsetY + mapPixelHeight), windowWidth_ - panelW - leftBarW, windowHeight_ - static_cast<int>(viewport.offsetY + mapPixelHeight), kHudPanelColor);

    // Barras grises verticales fijas, fuera de la sacudida de pantalla para
    // que no salten: izquierda (marco) y derecha (datos de jugadores).
    DrawLine(leftBarW, 0, leftBarW, windowHeight_, kHudPanelBorderColor);
    DrawLine(windowWidth_ - panelW, 0, windowWidth_ - panelW, windowHeight_, kHudPanelBorderColor);

    // Sacudida de pantalla (onda expansiva del disparo especial): desplaza el
    // campo de juego (mapa, tanques, balas) con un offset aleatorio que decae
    // a medida que pasa el tiempo. Los paneles laterales quedan fijos.
    Camera2D shakeCamera{};
    shakeCamera.zoom = 1.0f;
    if (screenShakeTimer_ > 0.0) {
        const float magnitude = kScreenShakeMaxOffsetPx * static_cast<float>(screenShakeTimer_ / kScreenShakeDuration);
        const float shakeX = (static_cast<float>(GetRandomValue(-100, 100)) / 100.0f) * magnitude;
        const float shakeY = (static_cast<float>(GetRandomValue(-100, 100)) / 100.0f) * magnitude;
        shakeCamera.target = Vector2{-shakeX, -shakeY};
    }
    BeginMode2D(shakeCamera);

    const float brickUnitDst = viewport.tileScreenSize / kBrickGridSize;
    const float steelUnitDst = viewport.tileScreenSize / kSteelGridSize;
    const int waterFrame = static_cast<int>(GetTime() / kWaterFrameInterval) % 2;
    const Texture2D waterTex = waterTextures_[waterFrame];
    const Rectangle waterSrc{0.0f, 0.0f, static_cast<float>(waterTex.width), static_cast<float>(waterTex.height)};

    for (int y = 0; y < map_.Height(); ++y) {
        for (int x = 0; x < map_.Width(); ++x) {
            const Cell& cell = map_.At(x, y);
            const float screenX = viewport.TileToScreenX(x);
            const float screenY = viewport.TileToScreenY(y);

            if (cell.type == TileType::Brick) {
                // El bloque se arma con una grilla de 4x4 unidades minimas
                // (ver BrickUnit.h / Documentaciones/Ladrillos.png), cada una
                // destruida de forma individual.
                for (int row = 0; row < kBrickGridSize; ++row) {
                    for (int col = 0; col < kBrickGridSize; ++col) {
                        const BrickUnit& unit = cell.brickUnits[row * kBrickGridSize + col];
                        if (!unit.alive) {
                            continue;
                        }
                        const Texture2D unitTex = brickUnitTextures_[unit.frame];
                        const Rectangle unitSrc{0.0f, 0.0f, static_cast<float>(unitTex.width), static_cast<float>(unitTex.height)};
                        const Rectangle unitDst{screenX + col * brickUnitDst, screenY + row * brickUnitDst, brickUnitDst, brickUnitDst};
                        DrawTexturePro(unitTex, unitSrc, unitDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
                    }
                }
            } else if (cell.type == TileType::Steel) {
                // El bloque se arma con una grilla de 2x2 unidades minimas
                // (ver SteelUnit.h), cada una con su propia resistencia.
                const Rectangle steelSrc{0.0f, 0.0f, static_cast<float>(steelUnitTexture_.width), static_cast<float>(steelUnitTexture_.height)};
                for (int row = 0; row < kSteelGridSize; ++row) {
                    for (int col = 0; col < kSteelGridSize; ++col) {
                        const SteelUnit& unit = cell.steelUnits[row * kSteelGridSize + col];
                        if (!unit.alive) {
                            continue;
                        }
                        const Rectangle unitDst{screenX + col * steelUnitDst, screenY + row * steelUnitDst, steelUnitDst, steelUnitDst};
                        DrawTexturePro(steelUnitTexture_, steelSrc, unitDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
                    }
                }
            } else if (cell.type == TileType::Water) {
                // 2 frames que alternan (ver kWaterFrameInterval) para
                // animar el oleaje.
                const Rectangle waterDst{screenX, screenY, viewport.tileScreenSize, viewport.tileScreenSize};
                DrawTexturePro(waterTex, waterSrc, waterDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
            } else if (cell.type == TileType::Ice) {
                const Rectangle iceSrc{0.0f, 0.0f, static_cast<float>(iceTexture_.width), static_cast<float>(iceTexture_.height)};
                const Rectangle iceDst{screenX, screenY, viewport.tileScreenSize, viewport.tileScreenSize};
                DrawTexturePro(iceTexture_, iceSrc, iceDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
            } else if (cell.type == TileType::Base) {
                // El aguila: objeto a defender (ver Game::Update, un impacto
                // la destruye y termina la partida).
                const Rectangle eagleSrc{0.0f, 0.0f, static_cast<float>(baseEagleTexture_.width), static_cast<float>(baseEagleTexture_.height)};
                const Rectangle eagleDst{screenX, screenY, viewport.tileScreenSize, viewport.tileScreenSize};
                DrawTexturePro(baseEagleTexture_, eagleSrc, eagleDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
            } else if (cell.type == TileType::BaseDestroyed) {
                // Aguila ya destruida (pedido explicitamente): se queda en
                // este frame (Tanques.png) en vez de desaparecer.
                const Rectangle eagleSrc{0.0f, 0.0f, static_cast<float>(baseEagleDestroyedTexture_.width), static_cast<float>(baseEagleDestroyedTexture_.height)};
                const Rectangle eagleDst{screenX, screenY, viewport.tileScreenSize, viewport.tileScreenSize};
                DrawTexturePro(baseEagleDestroyedTexture_, eagleSrc, eagleDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
            } else if (cell.type != TileType::Trees) {
                // El arbusto no se dibuja aca: es un bloque entero (no por
                // unidad) que se pinta mas abajo, encima de tanques y balas,
                // para que los tape (ver el segundo recorrido del mapa).
                const Rectangle rect{screenX, screenY, viewport.tileScreenSize, viewport.tileScreenSize};
                DrawRectangleRec(rect, ColorForTile(cell.type));
            }
        }
    }

    if (player1Active_) RenderTank(player1_, player1Spawn_, SpriteSetForColor(playerColorIndex_[0]), viewport);
    if (player2Active_) RenderTank(player2_, player2Spawn_, SpriteSetForColor(playerColorIndex_[1]), viewport);
    if (player3Active_) RenderTank(player3_, player3Spawn_, SpriteSetForColor(playerColorIndex_[2]), viewport);
    if (player4Active_) RenderTank(player4_, player4Spawn_, SpriteSetForColor(playerColorIndex_[3]), viewport);

    for (const Enemy& enemy : enemies_.Enemies()) {
        RenderEnemy(enemy, enemySprites_, viewport);
    }
    for (const Enemy& enemy : fastEnemies_.Enemies()) {
        RenderEnemy(enemy, fastEnemySprites_, viewport);
    }
    for (const Enemy& enemy : armorEnemies_.Enemies()) {
        RenderEnemy(enemy, armorEnemySprites_, viewport);
    }
    for (const Enemy& enemy : powerEnemies_.Enemies()) {
        RenderEnemy(enemy, powerEnemySprites_, viewport);
    }

    const float pixelScale = viewport.tileScreenSize / static_cast<float>(kTileSize);
    for (const Bullet& bullet : bullets_.Bullets()) {
        const Texture2D bulletTex = bulletSprites_.Get(bullet.direction);
        const float w = static_cast<float>(bulletTex.width) * pixelScale;
        const float h = static_cast<float>(bulletTex.height) * pixelScale;
        const Rectangle bulletSrc{0.0f, 0.0f, static_cast<float>(bulletTex.width), static_cast<float>(bulletTex.height)};
        const Rectangle bulletDst{viewport.TileToScreenX(bullet.x) - w * 0.5f, viewport.TileToScreenY(bullet.y) - h * 0.5f, w, h};
        DrawTexturePro(bulletTex, bulletSrc, bulletDst, Vector2{0.0f, 0.0f}, 0.0f, BulletTintForOwner(bullet.ownerId));
    }

    const float impactSize = viewport.tileScreenSize * 0.9f;
    for (const BulletImpact& impact : bulletImpacts_.Impacts()) {
        const Texture2D impactTex = bulletImpactSprites_.Get(impact.frameIndex);
        const Rectangle impactSrc{0.0f, 0.0f, static_cast<float>(impactTex.width), static_cast<float>(impactTex.height)};
        const Rectangle impactDst{viewport.TileToScreenX(impact.x) - impactSize * 0.5f, viewport.TileToScreenY(impact.y) - impactSize * 0.5f, impactSize, impactSize};
        DrawTexturePro(impactTex, impactSrc, impactDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    // Popup del numero de puntos (100/200/300/400/500), al matar un enemigo
    // o agarrar un item (ver ScorePopupSystem/AwardScoreAt), a escala
    // nativa (achicado a pedido, antes se dibujaba al doble).
    const float scorePopupScale = pixelScale;
    for (const ScorePopup& popup : scorePopups_.Popups()) {
        const Texture2D popupTex = scorePopupSprites_.Get(popup.points);
        const float popupW = static_cast<float>(popupTex.width) * scorePopupScale;
        const float popupH = static_cast<float>(popupTex.height) * scorePopupScale;
        const Rectangle popupSrc{0.0f, 0.0f, static_cast<float>(popupTex.width), static_cast<float>(popupTex.height)};
        const Rectangle popupDst{viewport.TileToScreenX(popup.x) - popupW * 0.5f, viewport.TileToScreenY(popup.y) - popupH * 0.5f, popupW, popupH};
        DrawTexturePro(popupTex, popupSrc, popupDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    // Diametro del radio de la explosion especial, en pantalla.
    const float specialExplosionSize = viewport.tileScreenSize * kSpecialExplosionRadius * 2.0f;
    for (const SpecialExplosion& explosion : specialExplosions_.Explosions()) {
        const Texture2D explosionTex = specialExplosionSprites_.Get(explosion.frameIndex);
        const Rectangle explosionSrc{0.0f, 0.0f, static_cast<float>(explosionTex.width), static_cast<float>(explosionTex.height)};
        // El estallido de muerte usa la escala nativa (mismo pixel-a-pantalla
        // que los tanques/balas), mas chico; el del disparo especial usa el
        // tamano fijo segun su radio de destruccion.
        const float explosionW = explosion.nativeScale ? static_cast<float>(explosionTex.width) * pixelScale : specialExplosionSize;
        const float explosionH = explosion.nativeScale ? static_cast<float>(explosionTex.height) * pixelScale : specialExplosionSize;
        const Rectangle explosionDst{viewport.TileToScreenX(explosion.x) - explosionW * 0.5f, viewport.TileToScreenY(explosion.y) - explosionH * 0.5f, explosionW, explosionH};
        DrawTexturePro(explosionTex, explosionSrc, explosionDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    // El arbusto tapa tanques, balas y explosiones que esten debajo, igual
    // que en el juego original. No bloquea movimiento ni disparos (ver
    // TileBlocksMovement/TileBlocksShots), solo los oculta.
    const Rectangle treesSrc{0.0f, 0.0f, static_cast<float>(treesTexture_.width), static_cast<float>(treesTexture_.height)};
    for (int y = 0; y < map_.Height(); ++y) {
        for (int x = 0; x < map_.Width(); ++x) {
            if (map_.At(x, y).type != TileType::Trees) {
                continue;
            }
            const Rectangle treesDst{viewport.TileToScreenX(x), viewport.TileToScreenY(y), viewport.tileScreenSize, viewport.tileScreenSize};
            DrawTexturePro(treesTexture_, treesSrc, treesDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        }
    }

    // Los power-ups van al final: eje Z mas alto que todo lo demas del campo
    // de juego (tanques, balas, explosiones y hasta el arbusto), para que
    // nunca queden tapados.
    if (powerUps_.Active().alive && powerUps_.IsBlinkVisible()) {
        Texture2D iconTex = starTexture_;
        if (powerUps_.Active().type == PowerUpType::Helmet) {
            iconTex = helmetTexture_;
        } else if (powerUps_.Active().type == PowerUpType::Gun) {
            iconTex = gunTexture_;
        } else if (powerUps_.Active().type == PowerUpType::Life) {
            iconTex = lifeTexture_;
        } else if (powerUps_.Active().type == PowerUpType::Grenade) {
            iconTex = grenadeTexture_;
        } else if (powerUps_.Active().type == PowerUpType::Shovel) {
            iconTex = shovelTexture_;
        } else if (powerUps_.Active().type == PowerUpType::Clock) {
            iconTex = clockTexture_;
        }
        const Rectangle iconSrc{0.0f, 0.0f, static_cast<float>(iconTex.width), static_cast<float>(iconTex.height)};
        const Rectangle iconDst{viewport.TileToScreenX(powerUps_.Active().x), viewport.TileToScreenY(powerUps_.Active().y), viewport.tileScreenSize, viewport.tileScreenSize};
        DrawTexturePro(iconTex, iconSrc, iconDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    // Grilla de referencia (boton de prueba F6): una linea por cada borde de
    // celda, mas marcada cada 5 celdas (estilo hoja cuadriculada) para poder
    // contar de un vistazo cuantos bloques entran al diseñar el escenario.
    if (showGrid_) {
        const float gridRight = viewport.TileToScreenX(static_cast<float>(map_.Width()));
        const float gridBottom = viewport.TileToScreenY(static_cast<float>(map_.Height()));
        for (int x = 0; x <= map_.Width(); ++x) {
            const int lineX = static_cast<int>(viewport.TileToScreenX(static_cast<float>(x)));
            const Color lineColor = (x % 5 == 0) ? Color{255, 255, 255, 120} : Color{255, 255, 255, 40};
            DrawLine(lineX, static_cast<int>(viewport.offsetY), lineX, static_cast<int>(gridBottom), lineColor);
        }
        for (int y = 0; y <= map_.Height(); ++y) {
            const int lineY = static_cast<int>(viewport.TileToScreenY(static_cast<float>(y)));
            const Color lineColor = (y % 5 == 0) ? Color{255, 255, 255, 120} : Color{255, 255, 255, 40};
            DrawLine(static_cast<int>(viewport.offsetX), lineY, static_cast<int>(gridRight), lineY, lineColor);
        }
    }

    // Recuadros verdes de colision (boton de prueba H): un rectangulo por
    // cada objeto vivo, usando exactamente el area que el juego considera
    // colisionable (no el sprite), para depurar bugs de colision a simple
    // vista.
    if (showHitboxes_) {
        auto DrawHitbox = [&](float left, float top, float right, float bottom) {
            const int px = static_cast<int>(viewport.TileToScreenX(left));
            const int py = static_cast<int>(viewport.TileToScreenY(top));
            const int pw = static_cast<int>((right - left) * viewport.tileScreenSize);
            const int ph = static_cast<int>((bottom - top) * viewport.tileScreenSize);
            DrawRectangleLines(px, py, pw, ph, GREEN);
        };

        // Tanques de jugador: mismas dos condiciones de guarda que
        // RenderTank (eliminado o todavia en el destello de spawn = sin
        // colision real).
        auto DrawTankHitbox = [&](const Tank& tank, const SpawnFlash& spawn) {
            if (tank.IsEliminated() || spawn.IsActive()) return;
            float l, r, t, b;
            tank.GetBounds(l, r, t, b);
            DrawHitbox(l, t, r, b);
        };
        if (player1Active_) DrawTankHitbox(player1_, player1Spawn_);
        if (player2Active_) DrawTankHitbox(player2_, player2Spawn_);
        if (player3Active_) DrawTankHitbox(player3_, player3Spawn_);
        if (player4Active_) DrawTankHitbox(player4_, player4Spawn_);

        // Tanques enemigos, en los 4 sistemas de IA.
        auto DrawEnemyHitboxes = [&](const auto& system) {
            for (const Enemy& enemy : system.Enemies()) {
                if (!enemy.alive || enemy.spawn.IsActive()) continue;
                float l, r, t, b;
                enemy.tank.GetBounds(l, r, t, b);
                DrawHitbox(l, t, r, b);
            }
        };
        DrawEnemyHitboxes(enemies_);
        DrawEnemyHitboxes(fastEnemies_);
        DrawEnemyHitboxes(armorEnemies_);
        DrawEnemyHitboxes(powerEnemies_);

        // Balas: cuadrado de kBulletHitRadius alrededor del centro (mismo
        // radio que usa BulletSystem para la anulacion bala-contra-bala).
        constexpr float kDebugBulletHitRadius = 0.2f;
        for (const Bullet& bullet : bullets_.Bullets()) {
            DrawHitbox(bullet.x - kDebugBulletHitRadius, bullet.y - kDebugBulletHitRadius,
                       bullet.x + kDebugBulletHitRadius, bullet.y + kDebugBulletHitRadius);
        }

        // Unidades minimas de ladrillo/acero vivas, y la base, recorriendo
        // el mapa igual que el renderizado normal de terreno.
        constexpr float kBrickUnitSize = 1.0f / kBrickGridSize;
        constexpr float kSteelUnitSize = 1.0f / kSteelGridSize;
        for (int y = 0; y < map_.Height(); ++y) {
            for (int x = 0; x < map_.Width(); ++x) {
                const Cell& cell = map_.At(x, y);
                if (cell.type == TileType::Brick) {
                    for (int row = 0; row < kBrickGridSize; ++row) {
                        for (int col = 0; col < kBrickGridSize; ++col) {
                            if (!cell.brickUnits[row * kBrickGridSize + col].alive) continue;
                            const float ux = static_cast<float>(x) + col * kBrickUnitSize;
                            const float uy = static_cast<float>(y) + row * kBrickUnitSize;
                            DrawHitbox(ux, uy, ux + kBrickUnitSize, uy + kBrickUnitSize);
                        }
                    }
                } else if (cell.type == TileType::Steel) {
                    for (int row = 0; row < kSteelGridSize; ++row) {
                        for (int col = 0; col < kSteelGridSize; ++col) {
                            if (!cell.steelUnits[row * kSteelGridSize + col].alive) continue;
                            const float ux = static_cast<float>(x) + col * kSteelUnitSize;
                            const float uy = static_cast<float>(y) + row * kSteelUnitSize;
                            DrawHitbox(ux, uy, ux + kSteelUnitSize, uy + kSteelUnitSize);
                        }
                    }
                } else if (cell.type == TileType::Base) {
                    DrawHitbox(static_cast<float>(x), static_cast<float>(y), static_cast<float>(x) + 1.0f, static_cast<float>(y) + 1.0f);
                }
            }
        }

        // Power-up activo (una celda completa).
        if (powerUps_.Active().alive) {
            const float px = powerUps_.Active().x;
            const float py = powerUps_.Active().y;
            DrawHitbox(px, py, px + 1.0f, py + 1.0f);
        }
    }

    EndMode2D();

    // Texto de UI (barra vertical + aviso de fuego amigo): dibujado fuera de
    // la sacudida de pantalla, para que quede fijo mientras el campo de
    // juego tiembla. Apilados abajo a la izquierda de la barra, los 4 a la
    // misma distancia entre si, pegados al borde inferior en vez de arrancar
    // desde arriba.
    const int hudMargin = static_cast<int>(kHudPanelMargin);
    const int panelRightX = windowWidth_ - hudMargin;
    const int panelLeftX = windowWidth_ - panelW + hudMargin;
    const int rowH = static_cast<int>(kHudRowHeight);
    const int blockH = 115; // alto aproximado del contenido de un jugador (nivel, barra+%, sobrecalentado, especial, paralizado)

    // Icono de bandera "STAGE" (Tanques.png) arriba de la barra derecha,
    // alineado a la izquierda del panel (igual que los datos de jugadores),
    // con el numero de nivel actual (currentLevel_, 1-40) debajo.
    {
        constexpr int kStageIconDisplayWidth = 32;
        const int stageIconH = (stageFlagIconTexture_.height * kStageIconDisplayWidth) / stageFlagIconTexture_.width;
        constexpr int stageIconY = 6;
        const Rectangle stageIconSrc{0.0f, 0.0f, static_cast<float>(stageFlagIconTexture_.width), static_cast<float>(stageFlagIconTexture_.height)};
        const Rectangle stageIconDst{static_cast<float>(panelLeftX), static_cast<float>(stageIconY), static_cast<float>(kStageIconDisplayWidth), static_cast<float>(stageIconH)};
        DrawTexturePro(stageFlagIconTexture_, stageIconSrc, stageIconDst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        const int stageNumberY = stageIconY + stageIconH + 4;
        DrawText(TextFormat("%d", currentLevel_), panelLeftX, stageNumberY, 20, RAYWHITE);

        // Cuantos enemigos faltan eliminar para terminar el nivel actual
        // (cuota segun cantidad de jugadores activos, ver EnemiesRemainingThisLevel).
        DrawText(TextFormat("Enemigos: %d", EnemiesRemainingThisLevel()), panelLeftX, stageNumberY + 40, 20, RAYWHITE);

        // El puntaje ahora es propio de cada jugador (ver RenderPlayerHud),
        // no compartido: no va aca.

        // Debug: nivel de agresividad actual (F11/F12 para bajar/subir, ver
        // EnemySystem::SetAggressivenessLevel).
        if (kDebugFeaturesEnabled) {
            const char* aggroLabel = TextFormat("Agresividad: %d", enemies_.AggressivenessLevel());
            DrawText(aggroLabel, panelLeftX, stageNumberY + 70, 16, YELLOW);
        }
    }
    const int panelCenterX = windowWidth_ - panelW / 2;
    const int p4Y = windowHeight_ - hudMargin - blockH;
    const int p3Y = p4Y - rowH;
    const int p2Y = p3Y - rowH;
    const int p1Y = p2Y - rowH;
    if (player1Active_) RenderPlayerHud(player1_, "P1", panelCenterX, p1Y, player1Score_, HudColorForColor(playerColorIndex_[0])); else DrawText("P1 - Pulse iniciar", panelCenterX - MeasureText("P1 - Pulse iniciar", 20) / 2, p1Y, 20, RED);
    if (player2Active_) RenderPlayerHud(player2_, "P2", panelCenterX, p2Y, player2Score_, HudColorForColor(playerColorIndex_[1])); else DrawText("P2 - Pulse iniciar", panelCenterX - MeasureText("P2 - Pulse iniciar", 20) / 2, p2Y, 20, RED);
    if (player3Active_) RenderPlayerHud(player3_, "P3", panelCenterX, p3Y, player3Score_, HudColorForColor(playerColorIndex_[2])); else DrawText("P3 - Pulse iniciar", panelCenterX - MeasureText("P3 - Pulse iniciar", 20) / 2, p3Y, 20, RED);
    if (player4Active_) RenderPlayerHud(player4_, "P4", panelCenterX, p4Y, player4Score_, HudColorForColor(playerColorIndex_[3])); else DrawText("P4 - Pulse iniciar", panelCenterX - MeasureText("P4 - Pulse iniciar", 20) / 2, p4Y, 20, RED);
    // Debug: contador de FPS y numero de boton del mando (ver
    // ApplyGamepadInput / lastGamepadButtonPressed_).
    if (kDebugFeaturesEnabled) {
        const char* fpsText = TextFormat("FPS: %d", GetFPS());
        DrawText(fpsText, panelRightX - MeasureText(fpsText, 12), hudMargin, 12, RAYWHITE);
        if (lastGamepadButtonPressed_ != -1) {
            const char* gamepadBtnText = TextFormat("Mando boton: %d", lastGamepadButtonPressed_);
            DrawText(gamepadBtnText, panelRightX - MeasureText(gamepadBtnText, 12), hudMargin + 14, 12, YELLOW);
        }
    }

    const char* friendlyFireLabel = "Apagado";
    if (friendlyFireMode_ == FriendlyFireMode::Paralyze) {
        friendlyFireLabel = "Nivel 1 (paraliza)";
    } else if (friendlyFireMode_ == FriendlyFireMode::Damage) {
        friendlyFireLabel = "Nivel 2 (dana)";
    }
    const char* friendlyFireText = TextFormat("Fuego amigo: %s", friendlyFireLabel);
    const int friendlyFireTextX = leftBarW + (static_cast<int>(playAreaWidth) - MeasureText(friendlyFireText, 20)) / 2;
    DrawText(friendlyFireText, friendlyFireTextX, 10, 20, RAYWHITE);

    // Secuencia de fin de partida (ver GameOverPhase/RenderGameOverOverlay).
    RenderGameOverOverlay();

    RenderLevelTransition();

    // Menu de pausa (ver paused_): encima de todo lo de arriba, en el mismo
    // frame (el juego frenado atras se sigue viendo, atenuado).
    if (paused_) {
        RenderPauseOverlay();
    }

    EndDrawing();
}

void Game::RenderLevelTransition() {
    if (levelTransitionPhase_ == LevelTransitionPhase::None) {
        return;
    }

    if (levelTransitionPhase_ == LevelTransitionPhase::ShowingText) {
        DrawRectangle(0, 0, windowWidth_, windowHeight_, kLevelTransitionGray);
        const char* text = TextFormat("NIVEL %d", currentLevel_);
        constexpr int kFontSize = 40;
        DrawText(text, windowWidth_ / 2 - MeasureText(text, kFontSize) / 2, windowHeight_ / 2 - kFontSize / 2, kFontSize, RAYWHITE);
        return;
    }

    // Curtain: la misma pantalla gris partida al medio, una mitad se va
    // para arriba y la otra para abajo hasta salir del todo, como un telon
    // que se abre (el nivel de fondo, ya dibujado arriba, se ve por el
    // hueco que va creciendo en el medio).
    const float progress = 1.0f - static_cast<float>(levelTransitionTimer_ / kLevelTransitionCurtainDuration);
    const float halfH = static_cast<float>(windowHeight_) * 0.5f;
    const float topY = -halfH * progress;
    const float bottomY = halfH + halfH * progress;
    DrawRectangle(0, static_cast<int>(topY), windowWidth_, static_cast<int>(halfH) + 1, kLevelTransitionGray);
    DrawRectangle(0, static_cast<int>(bottomY), windowWidth_, static_cast<int>(halfH) + 1, kLevelTransitionGray);
}

void Game::RenderScoreTally() {
    BeginDrawing();
    ClearBackground(BLACK);

    const int centerX = windowWidth_ / 2;

    // Puntaje maximo + quien lo tiene en esta partida (ver
    // highScoreHolderId_/MaybeUpdateHighScore), arriba de todo.
    const char* holderLabel = "-";
    switch (highScoreHolderId_) {
        case kPlayer1Id: holderLabel = "P1"; break;
        case kPlayer2Id: holderLabel = "P2"; break;
        case kPlayer3Id: holderLabel = "P3"; break;
        case kPlayer4Id: holderLabel = "P4"; break;
        default: break; // -1: todavia nadie en esta partida supero el record historico
    }
    constexpr int kHiScoreFontSize = 28;
    const char* hiScoreText = TextFormat("HI-SCORE %06d  %s", highScore_, holderLabel);
    DrawText(hiScoreText, centerX - MeasureText(hiScoreText, kHiScoreFontSize) / 2, 30, kHiScoreFontSize, RAYWHITE);

    // "NIVEL N" del nivel recien terminado (currentLevel_ ya se pisa recien
    // en AdvanceToNextLevel, por eso se guarda aparte en scoreTallyLevelShown_
    // al arrancar esta pantalla, ver BeginScoreTally).
    constexpr int kLevelFontSize = 32;
    const char* levelText = TextFormat("NIVEL %d", scoreTallyLevelShown_);
    DrawText(levelText, centerX - MeasureText(levelText, kLevelFontSize) / 2, 80, kLevelFontSize, RAYWHITE);

    // Una fila por jugador activo: [icono jugador] : [icono enemigo + conteo]
    // x4 (Basico/Rapido/Blindado/Power, siempre los 4 aunque el conteo sea
    // 0) y al final TOTAL <suma>. Cada fila se revela de a un slot segun
    // scoreTallyAnim_ (ver TickScoreTally).
    struct RowSource {
        int ownerId;
        const TankSpriteSet* sprites;
        bool active;
    };
    const RowSource sources[4] = {
        {kPlayer1Id, &player1Sprites_, player1Active_},
        {kPlayer2Id, &player2Sprites_, player2Active_},
        {kPlayer3Id, &player3Sprites_, player3Active_},
        {kPlayer4Id, &player4Sprites_, player4Active_},
    };
    const EnemySprites* enemySpriteSets[4] = {&enemySprites_, &fastEnemySprites_, &armorEnemySprites_, &powerEnemySprites_};

    constexpr float kIconSize = 32.0f;
    constexpr float kColonWidth = 24.0f;
    constexpr float kSlotWidth = 90.0f; // icono de enemigo + su numero
    constexpr float kTotalLabelWidth = 90.0f;
    constexpr float kTotalValueWidth = 100.0f;
    constexpr float kRowHeight = 56.0f;
    constexpr float kListTop = 160.0f;
    const float rowWidth = kIconSize + kColonWidth + 4.0f * kSlotWidth + kTotalLabelWidth + kTotalValueWidth;
    const float rowStartX = static_cast<float>(centerX) - rowWidth * 0.5f;

    int rowIndex = 0;
    for (int i = 0; i < 4; ++i) {
        const RowSource& src = sources[i];
        if (!src.active) {
            continue;
        }
        const float rowY = kListTop + static_cast<float>(rowIndex) * kRowHeight;
        ++rowIndex;

        float x = rowStartX;
        const Texture2D playerTex = src.sprites->Get(1, Direction::Down, 0);
        const Rectangle playerSrc{0.0f, 0.0f, static_cast<float>(playerTex.width), static_cast<float>(playerTex.height)};
        DrawTexturePro(playerTex, playerSrc, Rectangle{x, rowY, kIconSize, kIconSize}, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        x += kIconSize;
        DrawText(":", static_cast<int>(x) + 6, static_cast<int>(rowY) + 4, 24, RAYWHITE);
        x += kColonWidth;

        const ScoreTallyRowAnim& anim = scoreTallyAnim_[i];
        const PlayerLevelStats& stats = playerLevelStats_[i];
        for (int type = 0; type < 4; ++type) {
            if (type > anim.currentSlot) {
                x += kSlotWidth;
                continue; // todavia no le toca aparecer a este tipo
            }
            const int shownCount = (type < anim.currentSlot) ? stats.kills[type] : anim.currentCount;
            const Texture2D enemyTex = enemySpriteSets[type]->Get(Direction::Down, 0);
            const Rectangle enemySrc{0.0f, 0.0f, static_cast<float>(enemyTex.width), static_cast<float>(enemyTex.height)};
            DrawTexturePro(enemyTex, enemySrc, Rectangle{x, rowY, kIconSize, kIconSize}, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
            const char* countText = TextFormat("%d", shownCount);
            DrawText(countText, static_cast<int>(x + kIconSize + 8.0f), static_cast<int>(rowY) + 6, 22, RAYWHITE);
            x += kSlotWidth;
        }

        if (anim.currentSlot >= 4) {
            DrawText("TOTAL", static_cast<int>(x), static_cast<int>(rowY) + 4, 22, YELLOW);
            x += kTotalLabelWidth;
            const char* totalText = TextFormat("%d", stats.scoreGained);
            DrawText(totalText, static_cast<int>(x), static_cast<int>(rowY) + 4, 22, YELLOW);

            // Bono de fin de nivel (ver scoreTallyBonusAwarded_/TickScoreTally):
            // recien aparece 1s despues de terminar de contar TODAS las
            // filas (scoreTallyBonusRevealed_, pedido explicitamente), y
            // solo existe con mas de un jugador activo.
            if (scoreTallyBonusAwarded_[i] && scoreTallyBonusRevealed_) {
                x += static_cast<float>(MeasureText(totalText, 22)) + 12.0f;
                const char* bonusText = TextFormat("+%d", kScoreTallyClearBonus);
                DrawText(bonusText, static_cast<int>(x), static_cast<int>(rowY) + 4, 22, RED);
            }
        }
    }

    EndDrawing();
}

void Game::DrawMenuLine(const char* text, float y, bool selected, bool enabled) {
    constexpr int kFontSize = 24;
    const int centerX = windowWidth_ / 2;
    const int textWidth = MeasureText(text, kFontSize);
    const Color color = !enabled ? Color{90, 90, 90, 255} : (selected ? YELLOW : RAYWHITE);
    DrawText(text, centerX - textWidth / 2, static_cast<int>(y), kFontSize, color);

    if (selected) {
        // El tanque amarillo (Documentaciones/Titulo.png) senala la opcion
        // resaltada, a la izquierda del texto, como en el original.
        constexpr float kSelectorScale = 1.6f;
        const float selW = static_cast<float>(menuSelectorTexture_.width) * kSelectorScale;
        const float selH = static_cast<float>(menuSelectorTexture_.height) * kSelectorScale;
        const float selX = centerX - textWidth * 0.5f - selW - 14.0f;
        const float selY = y + (static_cast<float>(kFontSize) - selH) * 0.5f;
        const Rectangle src{0.0f, 0.0f, static_cast<float>(menuSelectorTexture_.width), static_cast<float>(menuSelectorTexture_.height)};
        DrawTexturePro(menuSelectorTexture_, src, Rectangle{selX, selY, selW, selH}, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }
}

void Game::RenderMenu() {
    BeginDrawing();
    ClearBackground(BLACK);

    const int centerX = windowWidth_ / 2;

    // Posiciones fijas (pedido explicitamente: el puntaje pegado arriba de
    // todo, el titulo centrado verticalmente en la ventana, y la lista mas
    // abajo con un margen mas grande) — ya no dependen de cuantas lineas
    // tenga la lista de cada pantalla, a diferencia del centrado dinamico
    // de la primera version.
    constexpr float kHiScoreY = 16.0f;
    constexpr float kGapLogoList = 70.0f;

    // Puntaje maximo historico, pegado arriba de todo, con el nombre de su
    // dueno si ya se cargo uno (ver highScoreHolderName_/GameOverPhase::NameEntry).
    const char* hiScoreText = highScoreHolderName_.empty()
        ? TextFormat("HI-SCORE  %06d", highScore_)
        : TextFormat("HI-SCORE  %06d  %s", highScore_, highScoreHolderName_.c_str());
    DrawText(hiScoreText, centerX - MeasureText(hiScoreText, 24) / 2, static_cast<int>(kHiScoreY), 24, RAYWHITE);

    // Logo "BATTLE CITY" (Documentaciones/Titulo.png, extraido en ladrillos):
    // mas grande y centrado verticalmente en la ventana, pedido explicitamente.
    constexpr float kLogoScale = 3.2f;
    const float logoW = static_cast<float>(titleLogoTexture_.width) * kLogoScale;
    const float logoH = static_cast<float>(titleLogoTexture_.height) * kLogoScale;
    // Centrado, pero corrido mas arriba (pedido explicitamente: centrado a
    // secas quedaba muy abajo, empujando el menu casi al pie de pantalla).
    constexpr float kLogoUpwardBias = 160.0f;
    const float logoY = (static_cast<float>(windowHeight_) - logoH) * 0.5f - kLogoUpwardBias;
    const float logoX = static_cast<float>(centerX) - logoW * 0.5f;
    const Rectangle logoSrc{0.0f, 0.0f, static_cast<float>(titleLogoTexture_.width), static_cast<float>(titleLogoTexture_.height)};
    DrawTexturePro(titleLogoTexture_, logoSrc, Rectangle{logoX, logoY, logoW, logoH}, Vector2{0.0f, 0.0f}, 0.0f, WHITE);

    const float listTop = logoY + logoH + kGapLogoList;

    std::string hintText; // texto de ayuda/explicacion, se llena segun la pantalla y se dibuja abajo del todo
    RenderMenuScreenBody(listTop, hintText);
    RenderMenuFooter(hintText);

    EndDrawing();
}

void Game::RenderMenuScreenBody(float listTop, std::string& hintText) {
    const int centerX = windowWidth_ / 2;
    constexpr float kLineHeight = 40.0f;

    if (menuScreen_ == MenuScreen::Main || menuScreen_ == MenuScreen::MainConfirmExit) {
        // MainConfirmExit dibuja este mismo listado de fondo (con "SALIR" ya
        // seleccionado) y el cuadro de confirmacion encima, en RenderMenuFooter
        // (mismo criterio que CapturingKey sobre PlayerActions).
        static const char* kItems[] = {"ARCADE", "SUPERVIVENCIA", "VERSUS", "MODO CONSTRUCCION", "OPCIONES", "SALIR"};
        static const bool kEnabled[] = {true, true, true, false, true, true};
        for (int i = 0; i < 6; ++i) {
            DrawMenuLine(kItems[i], listTop + static_cast<float>(i) * kLineHeight, i == mainMenuIndex_, kEnabled[i]);
        }
        if (!kEnabled[mainMenuIndex_]) {
            hintText = "Todavia no disponible.";
        }
    } else if (menuScreen_ == MenuScreen::ModeSubmenu) {
        static const char* kModeNames[] = {"ARCADE", "SUPERVIVENCIA", "VERSUS"};
        const char* modeTitle = kModeNames[selectedMainModeIndex_];
        DrawText(modeTitle, centerX - MeasureText(modeTitle, 24) / 2, static_cast<int>(listTop), 24, SKYBLUE);

        static const char* kItems[] = {"LOCAL", "LAN"};
        const bool modeHasGameplay = (selectedMainModeIndex_ == 0); // solo Arcade tiene partida real por ahora
        const bool enabled[2] = {modeHasGameplay, false};
        for (int i = 0; i < 2; ++i) {
            DrawMenuLine(kItems[i], listTop + kLineHeight + static_cast<float>(i) * kLineHeight, i == modeSubmenuIndex_, enabled[i]);
        }
        if (!enabled[modeSubmenuIndex_]) {
            hintText = (modeSubmenuIndex_ == 1) ? "Juego en red: todavia no disponible." : "Este modo todavia no tiene partida implementada.";
        }
    } else if (menuScreen_ == MenuScreen::Options) {
        const char* items[3] = {
            TextFormat("FUEGO AMIGO: %s", FriendlyFireLabel(friendlyFireMode_)),
            TextFormat("PANTALLA: %s", IsWindowFullscreen() ? "COMPLETA" : "VENTANA"),
            "MAPEO DE CONTROLES",
        };
        for (int i = 0; i < 3; ++i) {
            DrawMenuLine(items[i], listTop + static_cast<float>(i) * kLineHeight, i == optionsIndex_, true);
        }
        if (optionsIndex_ == 0) {
            hintText = FriendlyFireExplanation(friendlyFireMode_);
        } else if (optionsIndex_ == 1) {
            hintText = "Flechas: alternar entre ventana y pantalla completa.";
        } else {
            hintText = "Enter: elegir el jugador al que reasignarle los controles.";
        }
    } else if (menuScreen_ == MenuScreen::ControlMapping) {
        static const char* kItems[] = {"JUGADOR 1", "JUGADOR 2", "JUGADOR 3", "JUGADOR 4"};
        for (int i = 0; i < 4; ++i) {
            DrawMenuLine(kItems[i], listTop + static_cast<float>(i) * kLineHeight, i == controlMappingIndex_, true);
        }
        hintText = "Enter: elegir el jugador al que reasignarle los controles.";
    } else if (menuScreen_ == MenuScreen::ControlDeviceChoice) {
        const char* playerTitle = TextFormat("JUGADOR %d", mappingPlayerIndex_ + 1);
        DrawText(playerTitle, centerX - MeasureText(playerTitle, 24) / 2, static_cast<int>(listTop), 24, SKYBLUE);

        static const char* kItems[] = {"TECLADO", "MANDO"};
        for (int i = 0; i < 2; ++i) {
            DrawMenuLine(kItems[i], listTop + kLineHeight + static_cast<float>(i) * kLineHeight, i == controlDeviceIndex_, true);
        }
        hintText = "Enter: elegir que dispositivo configurarle a este jugador.";
    } else { // MenuScreen::PlayerActions o MenuScreen::CapturingKey (el fondo es el mismo)
        const char* playerTitle = TextFormat("JUGADOR %d - %s", mappingPlayerIndex_ + 1, mappingGamepad_ ? "MANDO" : "TECLADO");
        DrawText(playerTitle, centerX - MeasureText(playerTitle, 24) / 2, static_cast<int>(listTop), 24, SKYBLUE);

        const float rowLabelX = static_cast<float>(centerX) - 150.0f;
        const float rowKeyX = static_cast<float>(centerX) + 40.0f;
        const float selectorX = rowLabelX - 40.0f;

        if (mappingGamepad_) {
            static const char* kGamepadActionLabels[kGamepadMappableActionCount] = {"DISPARAR", "ESPECIAL", "PAUSA"};
            const PlayerGamepadBindings& bindings = playerGamepadBindings_[mappingPlayerIndex_];
            for (int i = 0; i < kGamepadMappableActionCount; ++i) {
                const float y = listTop + kLineHeight + static_cast<float>(i) * kLineHeight;
                const bool selected = (i == playerActionIndex_);
                const Color rowColor = selected ? YELLOW : RAYWHITE;
                if (selected) {
                    constexpr float kSelSize = 26.0f;
                    const Rectangle selSrc{0.0f, 0.0f, static_cast<float>(menuSelectorTexture_.width), static_cast<float>(menuSelectorTexture_.height)};
                    DrawTexturePro(menuSelectorTexture_, selSrc, Rectangle{selectorX, y - 1.0f, kSelSize, kSelSize}, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
                }
                DrawText(kGamepadActionLabels[i], static_cast<int>(rowLabelX), static_cast<int>(y), 22, rowColor);
                const int action = static_cast<int>(kGamepadMappableActions[i]);
                DrawText(GamepadButtonName(bindings.buttons[action]), static_cast<int>(rowKeyX), static_cast<int>(y), 22, rowColor);
            }
            hintText = "Enter: reasignar este boton.";
        } else {
            static const char* kActionLabels[kInputActionCount] = {nullptr, nullptr, nullptr, nullptr, "DISPARAR", "ESPECIAL", "INICIO", "PAUSA"};
            const PlayerKeyBindings& bindings = playerBindings_[mappingPlayerIndex_];
            for (int i = 0; i < kKeyMappableActionCount; ++i) {
                const InputAction action = kKeyMappableActions[i];
                const float y = listTop + kLineHeight + static_cast<float>(i) * kLineHeight;
                const bool selected = (i == playerActionIndex_);
                const Color rowColor = selected ? YELLOW : RAYWHITE;
                if (selected) {
                    constexpr float kSelSize = 26.0f;
                    const Rectangle selSrc{0.0f, 0.0f, static_cast<float>(menuSelectorTexture_.width), static_cast<float>(menuSelectorTexture_.height)};
                    DrawTexturePro(menuSelectorTexture_, selSrc, Rectangle{selectorX, y - 1.0f, kSelSize, kSelSize}, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
                }
                if (action == InputAction::Up || action == InputAction::Down || action == InputAction::Left || action == InputAction::Right) {
                    // Flecha en vez de texto para las 4 direcciones, pedido explicitamente.
                    float dx = 0.0f, dy = 0.0f;
                    switch (action) {
                        case InputAction::Up:    dy = -1.0f; break;
                        case InputAction::Down:  dy = 1.0f;  break;
                        case InputAction::Left:  dx = -1.0f; break;
                        default:                 dx = 1.0f;  break; // Right
                    }
                    DrawArrowGlyph(rowLabelX + 12.0f, y + 12.0f, 12.0f, dx, dy, rowColor);
                } else {
                    DrawText(kActionLabels[static_cast<int>(action)], static_cast<int>(rowLabelX), static_cast<int>(y), 22, rowColor);
                }
                DrawText(KeyName(bindings.keys[static_cast<int>(action)]), static_cast<int>(rowKeyX), static_cast<int>(y), 22, rowColor);
            }
            hintText = "Enter: reasignar esta tecla.";
        }
    }
}

void Game::RenderMenuFooter(const std::string& hintText) {
    const int centerX = windowWidth_ / 2;

    // Pie de pagina (ayuda/explicacion + controles), siempre pegado abajo de
    // la ventana, fuera del bloque centrado de arriba. En CapturingKey el
    // cuadro de "Presione una tecla" (mas abajo) ya trae su propia leyenda,
    // asi que no se repite aca.
    if (menuScreen_ != MenuScreen::CapturingKey && menuScreen_ != MenuScreen::MainConfirmExit) {
        if (!hintText.empty()) {
            DrawText(hintText.c_str(), centerX - MeasureText(hintText.c_str(), 18) / 2, windowHeight_ - 62, 18, GRAY);
        }
        const char* controlsHint = (menuScreen_ == MenuScreen::Main)
            ? "Flechas: moverse   Enter: elegir"
            : "Flechas: moverse/cambiar   Enter: elegir   Esc: volver";
        DrawText(controlsHint, centerX - MeasureText(controlsHint, 16) / 2, windowHeight_ - 32, 16, DARKGRAY);
    }

    // Prompt "Presione una tecla", encima de todo (incluida la lista de
    // acciones de fondo), mientras se esta reasignando una tecla.
    if (menuScreen_ == MenuScreen::CapturingKey) {
        constexpr int kBoxW = 420, kBoxH = 140;
        const int boxX = centerX - kBoxW / 2;
        const int boxY = windowHeight_ / 2 - kBoxH / 2;
        DrawRectangle(0, 0, windowWidth_, windowHeight_, Color{0, 0, 0, 150});
        DrawRectangle(boxX, boxY, kBoxW, kBoxH, Color{0x24, 0x24, 0x28, 0xFF});
        DrawRectangleLines(boxX, boxY, kBoxW, kBoxH, YELLOW);
        const char* prompt = mappingGamepad_ ? "Presione un boton del mando" : "Presione una tecla";
        DrawText(prompt, centerX - MeasureText(prompt, 22) / 2, boxY + 20, 22, RAYWHITE);
        const char* capturedText = "...";
        if (capturedKey_ != 0) {
            capturedText = mappingGamepad_ ? GamepadButtonName(capturedKey_) : KeyName(capturedKey_);
        }
        DrawText(capturedText, centerX - MeasureText(capturedText, 28) / 2, boxY + 62, 28, YELLOW);
        const char* boxFooter = "Enter: guardar   Esc: cancelar";
        DrawText(boxFooter, centerX - MeasureText(boxFooter, 16) / 2, boxY + 108, 16, GRAY);
    }

    // Cuadro de confirmacion antes de cerrar el juego (mismo estilo que
    // MenuScreen::PauseConfirmEnd), encima del listado principal ya
    // dibujado con "SALIR" resaltado (ver RenderMenuScreenBody).
    if (menuScreen_ == MenuScreen::MainConfirmExit) {
        constexpr int kBoxW = 420, kBoxH = 170;
        const int boxX = centerX - kBoxW / 2;
        const int boxY = windowHeight_ / 2 - kBoxH / 2;
        DrawRectangle(0, 0, windowWidth_, windowHeight_, Color{0, 0, 0, 150});
        DrawRectangle(boxX, boxY, kBoxW, kBoxH, Color{0x24, 0x24, 0x28, 0xFF});
        DrawRectangleLines(boxX, boxY, kBoxW, kBoxH, YELLOW);
        const char* question = "Cerrar el juego?";
        DrawText(question, centerX - MeasureText(question, 20) / 2, boxY + 24, 20, RAYWHITE);

        static const char* kItems[] = {"SI", "NO"};
        for (int i = 0; i < 2; ++i) {
            DrawMenuLine(kItems[i], static_cast<float>(boxY + 70) + static_cast<float>(i) * 38.0f, i == mainConfirmExitIndex_, true);
        }
        const char* boxFooter2 = "Enter: elegir   Esc: no";
        DrawText(boxFooter2, centerX - MeasureText(boxFooter2, 16) / 2, boxY + kBoxH - 26, 16, GRAY);
    }
}

void Game::RenderPauseOverlay() {
    // Se dibuja encima de la escena de juego ya congelada (paused_), en el
    // mismo BeginDrawing/EndDrawing de Render() — a diferencia de RenderMenu,
    // no arranca el suyo propio ni limpia la pantalla: un rectangulo oscuro
    // semitransparente atenua el juego de fondo, y encima va la lista que
    // corresponda a menuScreen_.
    const int centerX = windowWidth_ / 2;
    DrawRectangle(0, 0, windowWidth_, windowHeight_, Color{0, 0, 0, 180});

    constexpr float kLineHeight = 40.0f;
    constexpr float kListTop = 220.0f; // sin logo/hi-score (no pinta en medio de una partida): arranca mas arriba que en RenderMenu; solo la usa el submenu de Opciones reusado (ver mas abajo)

    if (menuScreen_ == MenuScreen::PauseMain) {
        // Bloque (titulo + lista) centrado vertical Y horizontalmente en la
        // ventana, pedido explicitamente — a diferencia del resto del menu
        // (titulo/Opciones), que usa posiciones fijas.
        constexpr int kItemCount = 5;
        constexpr float kTitleFontSize = 32.0f;
        constexpr float kTitleGap = 30.0f;
        const float totalHeight = kTitleFontSize + kTitleGap + kItemCount * kLineHeight;
        const float titleY = (static_cast<float>(windowHeight_) - totalHeight) * 0.5f;
        const float listTop = titleY + kTitleFontSize + kTitleGap;

        // "PAUSA" en rojo (pedido explicitamente), el resto del menu de
        // pausa sigue la paleta de siempre (blanco/amarillo si esta
        // resaltado, gris si esta deshabilitado, ver DrawMenuLine).
        const char* title = "PAUSA";
        DrawText(title, centerX - MeasureText(title, static_cast<int>(kTitleFontSize)) / 2, static_cast<int>(titleY), static_cast<int>(kTitleFontSize), RED);

        static const char* kItems[] = {"CONTINUAR", "GUARDAR", "CARGAR", "OPCIONES", "TERMINAR PARTIDA"};
        static const bool kEnabled[] = {true, false, false, true, true}; // Guardar/Cargar: el sistema de guardado todavia no existe
        for (int i = 0; i < kItemCount; ++i) {
            DrawMenuLine(kItems[i], listTop + static_cast<float>(i) * kLineHeight, i == pauseMenuIndex_, kEnabled[i]);
        }
        std::string hintText;
        if (!kEnabled[pauseMenuIndex_]) {
            hintText = "Todavia no disponible.";
        }
        RenderMenuFooter(hintText);
        return;
    }

    if (menuScreen_ == MenuScreen::PauseEndChoice) {
        // Mismo criterio de centrado que PauseMain arriba.
        constexpr int kItemCount = 2;
        constexpr float kTitleFontSize = 28.0f;
        constexpr float kTitleGap = 30.0f;
        const float totalHeight = kTitleFontSize + kTitleGap + kItemCount * kLineHeight;
        const float titleY = (static_cast<float>(windowHeight_) - totalHeight) * 0.5f;
        const float listTop = titleY + kTitleFontSize + kTitleGap;

        const char* title = "TERMINAR PARTIDA";
        DrawText(title, centerX - MeasureText(title, static_cast<int>(kTitleFontSize)) / 2, static_cast<int>(titleY), static_cast<int>(kTitleFontSize), RAYWHITE);

        static const char* kItems[] = {"MENU PRINCIPAL", "CERRAR EL JUEGO"};
        for (int i = 0; i < kItemCount; ++i) {
            DrawMenuLine(kItems[i], listTop + static_cast<float>(i) * kLineHeight, i == pauseEndChoiceIndex_, true);
        }
        RenderMenuFooter(std::string());
        return;
    }

    if (menuScreen_ == MenuScreen::PauseConfirmEnd) {
        constexpr int kBoxW = 420, kBoxH = 170;
        const int boxX = centerX - kBoxW / 2;
        const int boxY = windowHeight_ / 2 - kBoxH / 2;
        DrawRectangle(boxX, boxY, kBoxW, kBoxH, Color{0x24, 0x24, 0x28, 0xFF});
        DrawRectangleLines(boxX, boxY, kBoxW, kBoxH, YELLOW);
        const char* question = (pauseEndChoiceIndex_ == 0) ? "Volver al menu principal?" : "Cerrar el juego?";
        DrawText(question, centerX - MeasureText(question, 20) / 2, boxY + 24, 20, RAYWHITE);

        static const char* kItems[] = {"SI", "NO"};
        for (int i = 0; i < 2; ++i) {
            DrawMenuLine(kItems[i], static_cast<float>(boxY + 70) + static_cast<float>(i) * 38.0f, i == pauseConfirmIndex_, true);
        }
        const char* boxFooter = "Enter: elegir   Esc: no";
        DrawText(boxFooter, centerX - MeasureText(boxFooter, 16) / 2, boxY + kBoxH - 26, 16, GRAY);
        return;
    }

    // Options/ControlMapping/PlayerActions/CapturingKey: identico al de la
    // pantalla de titulo (ver RenderMenuScreenBody/RenderMenuFooter), solo
    // que arranca mas arriba (sin logo) y sobre el fondo atenuado en vez de
    // negro puro.
    std::string hintText;
    RenderMenuScreenBody(kListTop, hintText);
    RenderMenuFooter(hintText);
}

void Game::RenderGameOverOverlay() {
    if (gameOverPhase_ == GameOverPhase::None) {
        return;
    }

    const int centerX = windowWidth_ / 2;

    if (gameOverPhase_ == GameOverPhase::NameEntry) {
        // El sprite "GAME OVER" ya cumplio su parte en Rising/Holding: esto
        // es su propio dialogo, centrado como el resto de los cuadros de
        // confirmacion del juego (ver RenderPauseOverlay).
        constexpr int kBoxW = 460, kBoxH = 190;
        const int boxX = centerX - kBoxW / 2;
        const int boxY = windowHeight_ / 2 - kBoxH / 2;
        DrawRectangle(0, 0, windowWidth_, windowHeight_, Color{0, 0, 0, 150});
        DrawRectangle(boxX, boxY, kBoxW, kBoxH, Color{0x24, 0x24, 0x28, 0xFF});
        DrawRectangleLines(boxX, boxY, kBoxW, kBoxH, YELLOW);

        const char* title = "NUEVO RECORD!";
        DrawText(title, centerX - MeasureText(title, 26) / 2, boxY + 20, 26, YELLOW);

        static const char* kOwnerLabels[] = {"P1", "P2", "P3", "P4"};
        const char* ownerLabel = (gameOverWinnerOwnerId_ >= 0 && gameOverWinnerOwnerId_ < 4) ? kOwnerLabels[gameOverWinnerOwnerId_] : "?";
        const char* subtitle = TextFormat("%s - %d puntos", ownerLabel, gameOverWinnerScore_);
        DrawText(subtitle, centerX - MeasureText(subtitle, 18) / 2, boxY + 58, 18, RAYWHITE);

        // Cursor parpadeante al final de lo tipeado (mismo criterio de
        // parpadeo que el resto del HUD, ver kHudTextBlinkInterval).
        const bool cursorOn = static_cast<int>(GetTime() / kHudTextBlinkInterval) % 2 == 0;
        const std::string nameDisplay = gameOverNameEntry_ + (cursorOn ? "_" : "");
        DrawText(nameDisplay.c_str(), centerX - MeasureText(nameDisplay.c_str(), 28) / 2, boxY + 96, 28, YELLOW);

        const char* boxFooter = "Enter: confirmar   Backspace: borrar";
        DrawText(boxFooter, centerX - MeasureText(boxFooter, 16) / 2, boxY + kBoxH - 26, 16, GRAY);
        return;
    }

    // Rising/Holding: el sprite "GAME OVER" (Tanques.png) sube desde abajo
    // de la pantalla hasta el centro y se queda ahi quieto (ver
    // TickGameOverSequence/GameOverPhase), sin atenuar el fondo — el resto
    // del juego sigue bien visible detras (pedido explicitamente).
    constexpr float kScale = 8.0f;
    const float dispW = static_cast<float>(gameOverTexture_.width) * kScale;
    const float dispH = static_cast<float>(gameOverTexture_.height) * kScale;
    const float centerY = (static_cast<float>(windowHeight_) - dispH) * 0.5f;

    float y = centerY;
    if (gameOverPhase_ == GameOverPhase::Rising) {
        const float progress = std::clamp(static_cast<float>(gameOverTimer_ / kGameOverRiseDuration), 0.0f, 1.0f);
        const float startY = static_cast<float>(windowHeight_); // arranca justo debajo del borde inferior
        y = startY + (centerY - startY) * progress;
    }

    const float x = static_cast<float>(centerX) - dispW * 0.5f;
    const Rectangle src{0.0f, 0.0f, static_cast<float>(gameOverTexture_.width), static_cast<float>(gameOverTexture_.height)};
    DrawTexturePro(gameOverTexture_, src, Rectangle{x, y, dispW, dispH}, Vector2{0.0f, 0.0f}, 0.0f, WHITE);

    if (gameOverPhase_ == GameOverPhase::Holding) {
        const bool blinkOn = static_cast<int>(GetTime() / kHudTextBlinkInterval) % 2 == 0;
        if (blinkOn) {
            const char* hint = "Pulse Pausa o Enter para continuar";
            DrawText(hint, centerX - MeasureText(hint, 20) / 2, static_cast<int>(y + dispH) + 20, 20, RAYWHITE);
        }
    }
}

void Game::Shutdown() {
    UnloadTexture(titleLogoTexture_);
    UnloadTexture(menuSelectorTexture_);
    UnloadTexture(gameOverTexture_);
    specialExplosionSprites_.Unload();
    scorePopupSprites_.Unload();
    bulletImpactSprites_.Unload();
    shieldSprites_.Unload();
    spawnFlashSprites_.Unload();
    UnloadTexture(starTexture_);
    UnloadTexture(helmetTexture_);
    UnloadTexture(gunTexture_);
    UnloadTexture(lifeTexture_);
    UnloadTexture(grenadeTexture_);
    UnloadTexture(shovelTexture_);
    UnloadTexture(clockTexture_);
    UnloadTexture(brickUnitTextures_[0]);
    UnloadTexture(brickUnitTextures_[1]);
    UnloadTexture(steelUnitTexture_);
    UnloadTexture(treesTexture_);
    UnloadTexture(waterTextures_[0]);
    UnloadTexture(waterTextures_[1]);
    UnloadTexture(iceTexture_);
    UnloadTexture(baseEagleTexture_);
    UnloadTexture(baseEagleDestroyedTexture_);
    UnloadTexture(hudLifeIconTexture_);
    UnloadTexture(stageFlagIconTexture_);
    bulletSprites_.Unload();
    player1Sprites_.Unload();
    player2Sprites_.Unload();
    player3Sprites_.Unload();
    player4Sprites_.Unload();
    enemySprites_.Unload();
    fastEnemySprites_.Unload();
    armorEnemySprites_.Unload();
    powerEnemySprites_.Unload();
    UnloadMusicStream(battleLoopMusic_);
    UnloadMusicStream(movementLoopMusic_);
    UnloadSound(scoreTickSound_);
    UnloadSound(hitNonLethalSound_);
    UnloadSound(blockResistSound_);
    UnloadSound(brickBreakSound_);
    UnloadSound(playerShootSound_);
    UnloadSound(tankDeathSound_);
    UnloadSound(scoreBonusSound_);
    UnloadSound(bigExplosionSound_);
    UnloadSound(itemAppearSound_);
    UnloadSound(pauseToggleSound_);
    UnloadSound(itemPickupSound_);
    UnloadSound(extraLifeSound_);
    UnloadSound(stageStartSound_);
    UnloadSound(specialShootSound_);
    CloseAudioDevice();
    CloseWindow();
}

void Game::TickAudio() {
    UpdateMusicStream(battleLoopMusic_);
    UpdateMusicStream(movementLoopMusic_);

    // Pedido explicitamente: mientras suene el jingle de inicio de nivel, no
    // suenan los 2 bucles — la transicion visual (NIVEL N + telon) dura un
    // tiempo fijo, pero el jingle puede seguir sonando mas alla de eso, asi
    // que no alcanza con mirar solo levelTransitionPhase_.
    const bool stageStartPlaying = IsSoundPlaying(stageStartSound_);

    const bool inLevel = appState_ == AppState::Playing
        && !paused_
        && levelTransitionPhase_ == LevelTransitionPhase::None
        && scoreTallyPhase_ == ScoreTallyPhase::None
        && !gameOver_
        && !stageStartPlaying;

    const bool shouldPlayBattleLoop = inLevel && EnemiesRemainingThisLevel() > 0;
    if (shouldPlayBattleLoop && !IsMusicStreamPlaying(battleLoopMusic_)) {
        PlayMusicStream(battleLoopMusic_);
    } else if (!shouldPlayBattleLoop && IsMusicStreamPlaying(battleLoopMusic_)) {
        StopMusicStream(battleLoopMusic_);
    }

    bool anyPlayerMoving = false;
    if (inLevel) {
        for (Tank* tank : ActivePlayerTanks()) {
            if (tank->IsMoving()) {
                anyPlayerMoving = true;
                break;
            }
        }
    }
    if (anyPlayerMoving && !IsMusicStreamPlaying(movementLoopMusic_)) {
        PlayMusicStream(movementLoopMusic_);
    } else if (!anyPlayerMoving && IsMusicStreamPlaying(movementLoopMusic_)) {
        StopMusicStream(movementLoopMusic_);
    }
}

void Game::Run() {
    Init();

    // Limite al salto de tiempo de un frame a otro. Sin esto, el primer frame
    // despues de Init() (que puede tardar varios cientos de ms en cargar
    // texturas) se reporta como un unico salto enorme, y el bucle de paso
    // fijo "recupera" todo ese tiempo de una sola vez antes de dibujar nada:
    // animaciones cortas como el destello de aparicion terminarian antes de
    // que se vea un solo frame en pantalla.
    constexpr double kMaxFrameTime = 0.25;

    double accumulator = 0.0;
    while (!WindowShouldClose() && !wantsExit_) {
        double frameTime = GetFrameTime();
        if (frameTime > kMaxFrameTime) {
            frameTime = kMaxFrameTime;
        }
        accumulator += frameTime;

        while (accumulator >= kFixedTimestep) {
            ProcessInput();
            Update(kFixedTimestep);
            accumulator -= kFixedTimestep;
        }

        TickAudio(); // una vez por frame REAL (no por paso fijo): el stream de musica necesita alimentarse a ritmo de reloj de pared, no de simulacion
        Render(accumulator / kFixedTimestep);
    }

    Shutdown();
}

} // namespace bc
